#include "umm_patch.h"
#include "arc_merge.h"
#include "ini_file.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <xxhash.h>
#include <zlib.h>

namespace ios_mod_manager::patch
{
    namespace fs = std::filesystem;

    // Bump when the generated output changes for the same inputs.
    static constexpr std::string_view CacheVersion = "umm-patch-v1";

    static void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    static std::string Lower(std::string value)
    {
        for (char& c : value)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    }

    struct Call
    {
        size_t line;
        std::string name;
        std::vector<std::string> args;
    };

    // Matches the Sonic '06 Mod Manager parser: everything between the first '('
    // and the final character, split on '|', with quotes stripped from values
    // that start with one. Platform sections ("Xbox 360:") gate "All Systems" patches.
    struct Script
    {
        std::string title, author, blurb, platform;
        std::vector<Call> calls;
    };

    static std::string Metadata(const std::string& line)
    {
        // DeserialiseParameter: drop the leading quote and the trailing '")'.
        auto value = line.substr(line.find('(') + 1);
        if (value.size() >= 2) value.erase(value.size() - 2);
        if (!value.empty()) value.erase(0, 1);
        return value;
    }

    static Script Parse(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        Require(bool(in), "Cannot read " + file.filename().string() + ".");
        std::vector<std::string> lines;
        for (std::string line; std::getline(in, line); )
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            lines.push_back(std::move(line));
        }
        Script script;
        for (const auto& line : lines)
        {
            if (line.starts_with("Title(")) script.title = Metadata(line);
            else if (line.starts_with("Author(")) script.author = Metadata(line);
            else if (line.starts_with("Blurb(")) script.blurb = Metadata(line);
            else if (line.starts_with("Platform(")) script.platform = Metadata(line);
        }
        const bool allSystems = script.platform == "All Systems";
        bool reached = !allSystems;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto& line = lines[i];
            if (allSystems)
            {
                if (line.starts_with("All Systems") || line.starts_with("Xbox 360")) reached = true;
                else if (line.starts_with("PlayStation 3")) reached = false;
            }
            if (!reached || line.starts_with("--")) continue;
            const auto open = line.find('(');
            if (open == std::string::npos || open == 0 || line.back() != ')') continue;
            const auto name = line.substr(0, open);
            if (!std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(uint8_t(c)) || c == '_'; })) continue;
            if (name == "Title" || name == "Author" || name == "Blurb" || name == "Platform" || name == "Description") continue;
            Call call{ i + 1, name, {} };
            const auto body = line.substr(open + 1, line.size() - open - 2);
            size_t start = 0;
            for (;;)
            {
                const auto end = body.find('|', start);
                auto value = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (value.starts_with('"')) value = value.size() >= 2 ? value.substr(1, value.size() - 2) : std::string{};
                call.args.push_back(std::move(value));
                if (end == std::string::npos) break;
                start = end + 1;
            }
            script.calls.push_back(std::move(call));
        }
        return script;
    }

    static uint32_t Hex(const std::string& value, const Call& call)
    {
        try
        {
            size_t used = 0;
            const auto result = std::stoul(value, &used, 16);
            Require(used == value.size() && result <= UINT32_MAX, "");
            return uint32_t(result);
        }
        catch (const std::exception&)
        {
            throw std::runtime_error("Line " + std::to_string(call.line) + ": invalid hexadecimal value \"" + value + "\".");
        }
    }

    static uint32_t Decimal(const std::string& value, const Call& call)
    {
        try
        {
            size_t used = 0;
            const auto result = std::stoul(value, &used, 10);
            Require(used == value.size() && result <= 0x1000000, "");
            return uint32_t(result);
        }
        catch (const std::exception&)
        {
            throw std::runtime_error("Line " + std::to_string(call.line) + ": invalid count \"" + value + "\".");
        }
    }

    static std::vector<uint8_t> HexBytes(std::string value, const Call& call)
    {
        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
        Require(value.size() % 2 == 0, "Line " + std::to_string(call.line) + ": odd number of hex digits.");
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < value.size(); i += 2)
            bytes.push_back(uint8_t(Hex(value.substr(i, 2), call)));
        return bytes;
    }

    static void RequireArgs(const Call& call, size_t count)
    {
        Require(call.args.size() >= count, "Line " + std::to_string(call.line) + ": " + call.name + " needs " + std::to_string(count) + " values.");
    }

    // The bytes an executable write puts in memory, at its guest address.
    static std::optional<MemoryWrite> ExecutableWrite(const Call& call)
    {
        const auto& n = call.name;
        const bool physical = (n == "WriteByte" || n == "WriteBytes" || n == "WriteNullBytes" || n == "WriteTextBytes" || n == "WriteNopPPC") &&
            !call.args.empty() && call.args[0] == "Executable";
        const bool virtualWrite = n == "WriteVirtualBytes" || n == "WriteVirtualNop";
        if (!physical && !virtualWrite) return std::nullopt;
        MemoryWrite write{};
        const auto nops = [](uint32_t count)
        {
            std::vector<uint8_t> bytes;
            for (uint32_t i = 0; i < count; ++i) bytes.insert(bytes.end(), { 0x60, 0x00, 0x00, 0x00 });
            return bytes;
        };
        if (physical)
        {
            RequireArgs(call, n == "WriteNopPPC" ? 2 : 3);
            const auto offset = Hex(call.args[1], call);
            Require(offset >= ExecutableHeaderSize, "Line " + std::to_string(call.line) + ": writes into the XEX header.");
            write.address = ImageBase + (offset - ExecutableHeaderSize);
            if (n == "WriteByte") write.bytes = { uint8_t(Hex(call.args[2], call)) };
            else if (n == "WriteBytes") write.bytes = HexBytes(call.args[2], call);
            else if (n == "WriteNullBytes") write.bytes.assign(Decimal(call.args[2], call), 0);
            else if (n == "WriteTextBytes") write.bytes.assign(call.args[2].begin(), call.args[2].end());
            else write.bytes = nops(call.args.size() >= 3 ? Decimal(call.args[2], call) : 1);
        }
        else
        {
            RequireArgs(call, 1);
            write.address = Hex(call.args[0], call);
            if (n == "WriteVirtualBytes") { RequireArgs(call, 2); write.bytes = HexBytes(call.args[1], call); }
            else write.bytes = nops(call.args.size() >= 2 ? Decimal(call.args[1], call) : 1);
        }
        return write;
    }

    static std::string Address(uint32_t address)
    {
        char text[16];
        snprintf(text, sizeof(text), "0x%08X", address);
        return text;
    }

    static std::string WriteIssue(const MemoryWrite& write)
    {
        const uint64_t begin = write.address, end = begin + write.bytes.size();
        if (begin < ImageBase || end > uint64_t(ImageBase) + ImageSize)
            return "Writes outside the game executable at " + Address(write.address) + ".";
        if (begin < uint64_t(CodeBase) + CodeSize && end > CodeBase)
            return "Rewrites game code at " + Address(write.address) + ". The game's code is recompiled ahead of time, so code patches need a native port.";
        return {};
    }

    static std::string CallIssue(const Call& call)
    {
        const auto& n = call.name;
        if (n == "ParameterEdit" || n == "ParameterAdd" || n == "ParameterRename" || n == "ParameterErase" || n == "StringReplace")
            return "Edits Lua scripts as decompiled text. The game's scripts are compiled, and decompiling them isn't supported on iOS yet.";
        if (n == "TextAdd" || n == "TextEdit")
            return "Edits message tables (.mst), which isn't supported yet.";
        if (auto write = ExecutableWrite(call))
            return WriteIssue(*write);
        return {};
    }

    Info Inspect(const fs::path& mlua)
    {
        Info info{ mlua, mlua.stem().string(), {}, {}, {} };
        try
        {
            const auto script = Parse(mlua);
            if (!script.title.empty()) info.title = script.title;
            info.author = script.author;
            info.blurb = script.blurb;
            if (script.platform != "Xbox 360" && script.platform != "All Systems")
                info.issue = "Made for the " + (script.platform.empty() ? std::string("unknown") : script.platform) + " version.";
            for (const auto& call : script.calls)
            {
                if (!info.issue.empty()) break;
                info.issue = CallIssue(call);
            }
        }
        catch (const std::exception& ex)
        {
            info.issue = ex.what();
        }
        return info;
    }

    fs::path Directory(const fs::path& userPath) { return userPath / "patches"; }

    std::vector<Info> Scan(const fs::path& userPath)
    {
        std::vector<Info> result;
        const auto directory = Directory(userPath);
        std::error_code ec;
        if (!fs::is_directory(directory, ec) || fs::is_symlink(directory, ec)) return result;
        for (const auto& entry : fs::directory_iterator(directory))
            if (entry.is_regular_file() && !entry.is_symlink() && Lower(entry.path().extension().string()) == ".mlua")
                result.push_back(Inspect(entry.path()));
        std::sort(result.begin(), result.end(), [](const Info& a, const Info& b)
        {
            return Lower(a.title) == Lower(b.title) ? a.file < b.file : Lower(a.title) < Lower(b.title);
        });
        return result;
    }

    static fs::path SelectionPath(const fs::path& userPath) { return userPath / "ios-patches.ini"; }

    std::set<std::string> LoadSelection(const fs::path& userPath)
    {
        std::set<std::string> names;
        IniFile ini;
        if (!ini.read(SelectionPath(userPath))) return names;
        const auto count = std::min<size_t>(ini.get<size_t>("Patches", "Count", 0), 4096);
        for (size_t i = 0; i < count; ++i)
        {
            auto name = ini.getString("Patches", "Patch" + std::to_string(i), "");
            if (!name.empty() && fs::path(name).filename() == fs::path(name)) names.insert(std::move(name));
        }
        return names;
    }

    bool SaveSelection(const fs::path& userPath, const std::set<std::string>& names, std::string& error)
    {
        try
        {
            auto temporary = SelectionPath(userPath);
            temporary += ".tmp";
            Require(!fs::is_symlink(temporary), "The patch settings temporary file must not be a symbolic link.");
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                out << "[Patches]\nCount=" << names.size() << '\n';
                size_t i = 0;
                for (const auto& name : names)
                {
                    Require(name.find_first_of("\"\r\n/\\") == std::string::npos, "Invalid patch file name.");
                    out << "Patch" << i++ << "=\"" << name << "\"\n";
                }
                Require(bool(out), "Cannot save the patch selection.");
            }
            fs::rename(temporary, SelectionPath(userPath));
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }

    size_t Import(const fs::path& userPath, const fs::path& folder, std::string& error)
    {
        try
        {
            const auto directory = Directory(userPath);
            Require(!fs::is_symlink(directory), "The patches folder must not be a symbolic link.");
            fs::create_directories(directory);
            size_t count = 0;
            for (const auto& entry : fs::recursive_directory_iterator(folder))
            {
                if (entry.is_symlink() || !entry.is_regular_file() || Lower(entry.path().extension().string()) != ".mlua") continue;
                const auto target = directory / entry.path().filename();
                Require(!fs::is_symlink(target), "A patch file in the patches folder is a symbolic link.");
                // Patches are plain scripts; a newer copy of the same patch replaces the old one.
                fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
                ++count;
            }
            if (count == 0) error = "The folder contains no .mlua patches.";
            return count;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return 0;
        }
    }

    // ---- Sonic '06 asset packages (BINA *.pkg) ----

    static uint32_t ReadU32(const std::vector<uint8_t>& data, uint64_t offset)
    {
        Require(offset + 4 <= data.size(), "Package offset is out of bounds.");
        return uint32_t(data[offset]) << 24 | uint32_t(data[offset + 1]) << 16 | uint32_t(data[offset + 2]) << 8 | data[offset + 3];
    }

    static std::string ReadString(const std::vector<uint8_t>& data, uint64_t offset)
    {
        Require(offset < data.size(), "Package string is out of bounds.");
        auto end = offset;
        while (end < data.size() && data[end]) ++end;
        Require(end < data.size(), "Unterminated package string.");
        return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
    }

    static constexpr uint32_t BinaData = 0x20;

    Package Package::Parse(const std::vector<uint8_t>& data)
    {
        Require(data.size() >= BinaData + 16 && std::string_view(reinterpret_cast<const char*>(data.data()) + 0x18, 4) == "BINA",
            "Not a BINA asset package.");
        Package package;
        const auto categoryCount = ReadU32(data, BinaData + 8), categories = ReadU32(data, BinaData + 12);
        Require(categoryCount <= 65536, "Too many package categories.");
        for (uint32_t i = 0; i < categoryCount; ++i)
        {
            const uint64_t entry = BinaData + uint64_t(categories) + i * 12ull;
            const auto namePos = ReadU32(data, entry), count = ReadU32(data, entry + 4), filesPos = ReadU32(data, entry + 8);
            Require(count <= 1000000, "Too many package files.");
            Category category{ namePos ? ReadString(data, BinaData + uint64_t(namePos)) : std::string{}, {} };
            for (uint32_t f = 0; f < count; ++f)
            {
                const uint64_t fileEntry = BinaData + uint64_t(filesPos) + f * 8ull;
                const auto name = ReadU32(data, fileEntry), path = ReadU32(data, fileEntry + 4);
                category.files.push_back({ name ? ReadString(data, BinaData + uint64_t(name)) : std::string{},
                    path ? ReadString(data, BinaData + uint64_t(path)) : std::string{} });
            }
            package.categories.push_back(std::move(category));
        }
        return package;
    }

    // Matches Marathon's BINAWriter: big-endian data, a deduplicated string table in
    // first-use order, and the packed table of every offset field after it.
    std::vector<uint8_t> Package::Serialise() const
    {
        std::vector<uint8_t> out(BinaData, 0);
        std::vector<uint32_t> offsets; // Absolute positions of offset fields.
        std::vector<std::pair<std::string, std::vector<uint32_t>>> strings;
        const auto put = [&](uint32_t value)
        {
            out.insert(out.end(), { uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value) });
        };
        const auto patch = [&](uint32_t at, uint32_t value)
        {
            out[at] = uint8_t(value >> 24); out[at + 1] = uint8_t(value >> 16); out[at + 2] = uint8_t(value >> 8); out[at + 3] = uint8_t(value);
        };
        const auto addOffset = [&]() { offsets.push_back(uint32_t(out.size())); put(0); return offsets.back(); };
        const auto addString = [&](const std::string& value)
        {
            if (value.empty()) { put(0); return; }
            const auto field = addOffset();
            auto found = std::find_if(strings.begin(), strings.end(), [&](const auto& entry) { return entry.first == value; });
            if (found == strings.end()) strings.push_back({ value, { field } });
            else found->second.push_back(field);
        };
        size_t fileCount = 0;
        for (const auto& category : categories) fileCount += category.files.size();
        put(uint32_t(fileCount));
        const auto filesField = addOffset();
        put(uint32_t(categories.size()));
        const auto categoriesField = addOffset();
        patch(categoriesField, uint32_t(out.size()) - BinaData);
        std::vector<uint32_t> categoryFiles;
        for (const auto& category : categories)
        {
            addString(category.name);
            put(uint32_t(category.files.size()));
            categoryFiles.push_back(addOffset());
        }
        patch(filesField, uint32_t(out.size()) - BinaData);
        for (size_t i = 0; i < categories.size(); ++i)
        {
            patch(categoryFiles[i], uint32_t(out.size()) - BinaData);
            for (const auto& file : categories[i].files)
            {
                addString(file.name);
                addString(file.path);
            }
        }
        const auto align = [&](size_t alignment) { while (out.size() % alignment) out.push_back(0); };
        align(4);
        for (const auto& [value, fields] : strings)
        {
            for (const auto field : fields) patch(field, uint32_t(out.size()) - BinaData);
            out.insert(out.end(), value.begin(), value.end());
            out.push_back(0);
        }
        align(4);
        const auto footer = uint32_t(out.size());
        uint32_t last = BinaData;
        for (const auto field : offsets)
        {
            const uint32_t delta = (field - last) >> 2;
            if (delta <= 0x3F) out.push_back(uint8_t(0x40 | delta));
            else if (delta <= 0x3FFF) out.insert(out.end(), { uint8_t(0x80 | (delta >> 8)), uint8_t(delta) });
            else out.insert(out.end(), { uint8_t(0xC0 | (delta >> 24)), uint8_t(delta >> 16), uint8_t(delta >> 8), uint8_t(delta) });
            last = field;
        }
        align(4);
        patch(0, uint32_t(out.size()));
        patch(4, footer - BinaData);
        patch(8, uint32_t(out.size()) - footer);
        const uint8_t tail[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '1', 'B', 'B', 'I', 'N', 'A', 0, 0, 0, 0 };
        std::copy(std::begin(tail), std::end(tail), out.begin() + 12);
        return out;
    }

    // ---- Patch execution ----

    // Converts a patch path: the leading "core" folder is the platform folder.
    static std::string GamePath(const std::string& value)
    {
        std::string path = value;
        std::replace(path.begin(), path.end(), '\\', '/');
        while (path.starts_with("/")) path.erase(0, 1);
        if (Lower(path) == "core" || Lower(path).starts_with("core/")) path = "xenon" + path.substr(4);
        Require(!path.empty() && path.find("..") == std::string::npos && path.find(':') == std::string::npos,
            "Invalid patch path \"" + value + "\".");
        return path;
    }

    static bool WildcardMatch(std::string_view pattern, std::string_view text)
    {
        if (pattern.empty()) return text.empty();
        if (pattern[0] == '*')
        {
            for (size_t i = 0; i <= text.size(); ++i)
                if (WildcardMatch(pattern.substr(1), text.substr(i))) return true;
            return false;
        }
        if (text.empty()) return false;
        const auto a = pattern[0] >= 'A' && pattern[0] <= 'Z' ? char(pattern[0] + 32) : pattern[0];
        const auto b = text[0] >= 'A' && text[0] <= 'Z' ? char(text[0] + 32) : text[0];
        return (a == '?' || a == b) && WildcardMatch(pattern.substr(1), text.substr(1));
    }

    static std::vector<uint8_t> ReadFile(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        Require(bool(in), "Cannot read " + path.filename().string() + ".");
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    static void WriteFile(const fs::path& path, const std::vector<uint8_t>& data)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        Require(bool(out), "Cannot write " + path.filename().string() + " (check free storage).");
    }

    static std::vector<uint8_t> MemberBytes(const arc::Member& member)
    {
        std::ifstream in(member.source, std::ios::binary);
        in.seekg(member.offset);
        std::vector<uint8_t> payload(member.size);
        Require(bool(in.read(reinterpret_cast<char*>(payload.data()), std::streamsize(payload.size()))), "Cannot read archive member " + member.name + ".");
        if (member.expandedSize == 0) return payload;
        std::vector<uint8_t> expanded(member.expandedSize);
        uLongf length = expanded.size();
        Require(uncompress(expanded.data(), &length, payload.data(), uLong(payload.size())) == Z_OK && length == expanded.size(),
            "Cannot decompress archive member " + member.name + ".");
        return expanded;
    }

    namespace
    {
        // An archive being edited. Untouched members keep referencing their source.
        struct Archive
        {
            fs::path source;
            arc::Members members;
            std::map<std::string, std::vector<uint8_t>> edited;
            bool changed = false;
        };

        struct Engine
        {
            Engine(const std::function<fs::path(const std::string&)>& resolve,
                const std::function<std::vector<std::string>(const std::string&)>& listDirectory)
                : resolve(resolve), listDirectory(listDirectory) {}

            const std::function<fs::path(const std::string&)>& resolve;
            const std::function<std::vector<std::string>(const std::string&)>& listDirectory;
            std::map<std::string, Archive> archives;
            std::string active; // Archive key while inside BeginBlock/EndBlock.
            std::map<std::string, std::vector<uint8_t>> files; // Edited game files.
            std::map<std::string, std::string> moved; // New key -> source key.
            std::set<std::string> hidden;
            std::vector<MemoryWrite> memory;
            std::vector<std::string> ignore, search;

            Archive& Current() { return archives.at(active); }

            bool Filtered(const std::string& key) const
            {
                const auto name = fs::path(key).filename().string();
                for (const auto& phrase : ignore)
                    if (name.find(phrase) != std::string::npos) return true;
                if (search.empty()) return false;
                return std::none_of(search.begin(), search.end(), [&](const std::string& phrase) { return name.find(phrase) != std::string::npos; });
            }

            bool Exists(const std::string& key)
            {
                if (!active.empty())
                {
                    const auto& archive = Current();
                    auto found = archive.members.find(key);
                    return found != archive.members.end() && !found->second.directory;
                }
                if (hidden.contains(key)) return false;
                if (files.contains(key) || moved.contains(key)) return true;
                std::error_code ec;
                const auto path = resolve(key);
                return !path.empty() && fs::is_regular_file(path, ec);
            }

            std::vector<uint8_t> Read(const std::string& key)
            {
                if (!active.empty())
                {
                    auto& archive = Current();
                    if (auto edited = archive.edited.find(key); edited != archive.edited.end()) return edited->second;
                    return MemberBytes(archive.members.at(key));
                }
                if (auto edited = files.find(key); edited != files.end()) return edited->second;
                if (auto source = moved.find(key); source != moved.end()) return ReadFile(resolve(source->second));
                return ReadFile(resolve(key));
            }

            void Write(const std::string& key, const std::string& spelling, std::vector<uint8_t> data)
            {
                if (!active.empty())
                {
                    auto& archive = Current();
                    if (!archive.members.contains(key))
                        archive.members.emplace(key, arc::Member{ spelling, false, {}, 0, 0, 0 });
                    archive.edited[key] = std::move(data);
                    archive.changed = true;
                    return;
                }
                hidden.erase(key);
                moved.erase(key);
                files[key] = std::move(data);
            }

            void Remove(const std::string& key)
            {
                if (!active.empty())
                {
                    auto& archive = Current();
                    archive.members.erase(key);
                    archive.edited.erase(key);
                    archive.changed = true;
                    return;
                }
                files.erase(key);
                moved.erase(key);
                hidden.insert(key);
            }

            void Move(const std::string& from, const std::string& to, const std::string& toSpelling)
            {
                if (!active.empty())
                {
                    Write(to, toSpelling, Read(from));
                    Remove(from);
                    return;
                }
                if (auto edited = files.find(from); edited != files.end())
                    files[to] = std::move(edited->second);
                else
                    moved[to] = moved.contains(from) ? moved[from] : from;
                hidden.erase(to);
                Remove(from);
            }

            // Files directly inside a folder.
            std::vector<std::string> List(const std::string& directory)
            {
                std::set<std::string> result;
                const auto prefix = directory + "/";
                const auto direct = [&](const std::string& key)
                {
                    return key.starts_with(prefix) && key.find('/', prefix.size()) == std::string::npos;
                };
                if (!active.empty())
                {
                    for (const auto& [key, member] : Current().members)
                        if (!member.directory && direct(key)) result.insert(key);
                }
                else
                {
                    for (const auto& key : listDirectory(directory)) result.insert(key);
                    for (const auto& [key, data] : files) if (direct(key)) result.insert(key);
                    for (const auto& [key, source] : moved) if (direct(key)) result.insert(key);
                    for (const auto& key : hidden) result.erase(key);
                }
                return { result.begin(), result.end() };
            }

            bool IsDirectory(const std::string& key)
            {
                if (!active.empty())
                {
                    const auto prefix = key + "/";
                    for (const auto& [memberKey, member] : Current().members)
                        if (memberKey.starts_with(prefix) || (memberKey == key && member.directory)) return true;
                    return false;
                }
                return !List(key).empty();
            }

            // A file, or each matching file directly inside a folder.
            std::vector<std::string> Targets(const std::string& key, const char* extension)
            {
                if (Exists(key)) return { key };
                std::vector<std::string> result;
                if (IsDirectory(key))
                    for (const auto& file : List(key))
                        if (Lower(fs::path(file).extension().string()) == extension && !Filtered(file)) result.push_back(file);
                return result;
            }

            void WriteAt(const std::string& key, uint32_t offset, const std::vector<uint8_t>& bytes)
            {
                if (!Exists(key)) return; // The original manager skips missing files.
                auto data = Read(key);
                if (data.size() < size_t(offset) + bytes.size()) data.resize(size_t(offset) + bytes.size());
                std::copy(bytes.begin(), bytes.end(), data.begin() + offset);
                Write(key, key, std::move(data));
            }

            void EditPackages(const Call& call, const std::string& key)
            {
                for (const auto& target : Targets(key, ".pkg"))
                {
                    auto package = Package::Parse(Read(target));
                    const auto& type = call.args[1];
                    for (auto& category : package.categories)
                    {
                        if (category.name != type) continue;
                        auto& files = category.files;
                        if (call.name == "PackageAdd")
                            files.push_back({ call.args[2], call.args[3] });
                        else if (call.name == "PackageErase")
                            std::erase_if(files, [&](const Package::File& file) { return file.name == call.args[2]; });
                        else
                        {
                            const auto& name = call.args[2];
                            const auto& path = call.args[3];
                            bool found = false;
                            for (auto& file : files)
                                if (file.name == name) { found = true; file.path = path; }
                            if (path.empty()) std::erase_if(files, [&](const Package::File& file) { return file.name == name; });
                            if (!found) files.push_back({ name, path });
                        }
                    }
                    Write(target, target, package.Serialise());
                }
            }

            void Run(const Call& call)
            {
                const auto& n = call.name;
                const auto where = [&](const std::string& message) { return "Line " + std::to_string(call.line) + ": " + message; };
                if (auto write = ExecutableWrite(call))
                {
                    const auto issue = WriteIssue(*write);
                    Require(issue.empty(), where(issue));
                    memory.push_back(std::move(*write));
                    return;
                }
                const auto issue = CallIssue(call);
                Require(issue.empty(), where(issue));
                if (n == "DecryptExecutable" || n == "EncryptExecutable") return;
                if (n == "BeginBlock")
                {
                    RequireArgs(call, 1);
                    const auto key = arc::Key(GamePath(call.args[0]));
                    if (!archives.contains(key))
                    {
                        const auto source = resolve(key);
                        Require(!source.empty() && fs::is_regular_file(source), where("The game has no " + key + "."));
                        archives.emplace(key, Archive{ source, arc::Read(source), {} });
                    }
                    active = key;
                    return;
                }
                if (n == "EndBlock") { active.clear(); return; }
                if (n == "Ignore") { ignore = call.args; return; }
                if (n == "Search") { search = call.args; return; }
                RequireArgs(call, 1);
                const auto spelling = GamePath(call.args[0]);
                const auto key = arc::Key(spelling);
                if (n == "WriteByte") { RequireArgs(call, 3); WriteAt(key, Hex(call.args[1], call), { uint8_t(Hex(call.args[2], call)) }); }
                else if (n == "WriteBytes") { RequireArgs(call, 3); WriteAt(key, Hex(call.args[1], call), HexBytes(call.args[2], call)); }
                else if (n == "WriteNullBytes") { RequireArgs(call, 3); WriteAt(key, Hex(call.args[1], call), std::vector<uint8_t>(Decimal(call.args[2], call), 0)); }
                else if (n == "WriteTextBytes") { RequireArgs(call, 3); WriteAt(key, Hex(call.args[1], call), { call.args[2].begin(), call.args[2].end() }); }
                else if (n == "WriteNopPPC")
                {
                    RequireArgs(call, 2);
                    std::vector<uint8_t> nops;
                    for (uint32_t i = 0, count = call.args.size() >= 3 ? Decimal(call.args[2], call) : 1; i < count; ++i)
                        nops.insert(nops.end(), { 0x60, 0, 0, 0 });
                    WriteAt(key, Hex(call.args[1], call), nops);
                }
                else if (n == "WriteBase64")
                {
                    RequireArgs(call, 2);
                    Write(key, spelling, Base64(call.args[1], call));
                }
                else if (n == "Copy")
                {
                    RequireArgs(call, 3);
                    if (!Exists(key)) return;
                    const auto targetSpelling = GamePath(call.args[1]);
                    const auto target = arc::Key(targetSpelling);
                    Require(!Exists(target) || Lower(call.args[2]) == "true", where(target + " already exists."));
                    Write(target, targetSpelling, Read(key));
                }
                else if (n == "Rename")
                {
                    RequireArgs(call, 2);
                    if (!Exists(key)) return;
                    const auto targetSpelling = (fs::path(spelling).parent_path() / fs::path(call.args[1]).filename()).generic_string();
                    const auto target = arc::Key(targetSpelling);
                    if (!Exists(target)) Move(key, target, targetSpelling);
                    else if (target == key + "_back") Remove(key);
                }
                else if (n == "RenameByExtension")
                {
                    RequireArgs(call, 3);
                    for (const auto& file : List(key))
                    {
                        if (!WildcardMatch(call.args[1], fs::path(file).filename().string())) continue;
                        auto targetSpelling = fs::path(file).replace_extension(call.args[2]).generic_string();
                        const auto target = arc::Key(targetSpelling);
                        if (!Exists(target)) Move(file, target, targetSpelling);
                        else if (target == file + "_back") Remove(file);
                    }
                }
                else if (n == "Delete")
                {
                    if (Exists(key)) Remove(key);
                    else if (!active.empty() && IsDirectory(key))
                    {
                        std::erase_if(Current().members, [&](const auto& entry) { return entry.first == key || entry.first.starts_with(key + "/"); });
                        std::erase_if(Current().edited, [&](const auto& entry) { return entry.first.starts_with(key + "/"); });
                        Current().changed = true;
                    }
                }
                else if (n == "PackageAdd" || n == "PackageEdit") { RequireArgs(call, 4); EditPackages(call, key); }
                else if (n == "PackageErase") { RequireArgs(call, 3); EditPackages(call, key); }
                // Other names are ignored, like the original manager does.
            }

            static std::vector<uint8_t> Base64(const std::string& text, const Call& call)
            {
                static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::vector<uint8_t> out;
                uint32_t buffer = 0;
                int bits = 0;
                for (const char c : text)
                {
                    if (c == '=') break;
                    if (c == ' ' || c == '\n' || c == '\r') continue;
                    const auto value = alphabet.find(c);
                    Require(value != std::string_view::npos, "Line " + std::to_string(call.line) + ": invalid Base64 data.");
                    buffer = (buffer << 6) | uint32_t(value);
                    bits += 6;
                    if (bits >= 8) { bits -= 8; out.push_back(uint8_t(buffer >> bits)); }
                }
                return out;
            }
        };
    }

    Result Apply(const std::vector<fs::path>& patches, const fs::path& cacheRoot,
        const std::function<fs::path(const std::string&)>& resolve,
        const std::function<std::vector<std::string>(const std::string&)>& listDirectory)
    {
        Engine engine(resolve, listDirectory);
        XXH3_state_t* state = XXH3_createState();
        Require(state != nullptr, "Cannot allocate the patch fingerprint.");
        std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(state, XXH3_freeState);
        XXH3_64bits_reset(state);
        XXH3_64bits_update(state, CacheVersion.data(), CacheVersion.size());
        for (const auto& file : patches)
        {
            const auto info = Inspect(file);
            Require(info.issue.empty(), info.title + ": " + info.issue);
            const auto contents = ReadFile(file);
            XXH3_64bits_update(state, contents.data(), contents.size());
            const auto script = Parse(file);
            engine.ignore.clear();
            engine.search.clear();
            engine.active.clear();
            try
            {
                for (const auto& call : script.calls) engine.Run(call);
            }
            catch (const std::exception& ex)
            {
                throw std::runtime_error(info.title + ": " + ex.what());
            }
        }

        // Generated files are content-addressed by the patches and their inputs.
        const auto sourceStamp = [&](const fs::path& source)
        {
            std::error_code ec;
            const auto text = source.string() + '|' + std::to_string(fs::file_size(source, ec)) + '|' +
                std::to_string(static_cast<long long>(fs::last_write_time(source, ec).time_since_epoch().count()));
            XXH3_64bits_update(state, text.data(), text.size());
        };
        for (const auto& [key, archive] : engine.archives) { XXH3_64bits_update(state, key.data(), key.size()); sourceStamp(archive.source); }
        for (const auto& [key, source] : engine.moved) { XXH3_64bits_update(state, key.data(), key.size()); sourceStamp(resolve(source)); }
        for (const auto& [key, data] : engine.files) { XXH3_64bits_update(state, key.data(), key.size()); XXH3_64bits_update(state, data.data(), data.size()); }

        Result result;
        result.memory = std::move(engine.memory);
        const auto patchCache = cacheRoot / "patches";
        const auto output = patchCache / std::to_string(XXH3_64bits_digest(state));
        Require(!fs::is_symlink(cacheRoot) && !fs::is_symlink(patchCache), "The patch cache must not be a symbolic link.");
        const auto ready = output / ".complete";
        if (!fs::exists(ready))
        {
            std::error_code ignored;
            fs::remove_all(output, ignored);
            const auto staging = output.string() + ".building";
            fs::remove_all(staging, ignored);
            try
            {
                for (auto& [key, archive] : engine.archives)
                {
                    if (!archive.changed) continue;
                    size_t index = 0;
                    for (auto& [memberKey, data] : archive.edited)
                    {
                        const auto memberFile = fs::path(staging) / "members" / std::to_string(index++);
                        WriteFile(memberFile, data);
                        auto& member = archive.members.at(memberKey);
                        member.source = memberFile;
                        member.offset = member.expandedSize = 0;
                        member.size = uint32_t(data.size());
                    }
                    const auto target = fs::path(staging) / key;
                    fs::create_directories(target.parent_path());
                    arc::Write(target, archive.members);
                    arc::Read(target); // Verify the generated table.
                }
                fs::remove_all(fs::path(staging) / "members", ignored);
                for (const auto& [key, data] : engine.files) WriteFile(fs::path(staging) / key, data);
                fs::create_directories(staging);
                std::ofstream(fs::path(staging) / ".complete") << "ok";
                fs::rename(staging, output);
            }
            catch (...)
            {
                fs::remove_all(staging, ignored);
                throw;
            }
        }
        // Keep only this selection's output.
        for (const auto& entry : fs::directory_iterator(patchCache))
            if (entry.path() != output) { std::error_code ignored; fs::remove_all(entry.path(), ignored); }

        for (const auto& [key, archive] : engine.archives)
            if (fs::exists(output / key)) result.files[key] = output / key;
        for (const auto& [key, data] : engine.files) result.files[key] = output / key;
        for (const auto& [key, source] : engine.moved) result.files[key] = resolve(source);
        for (const auto& key : engine.hidden)
            if (!result.files.contains(key)) result.files[key] = output / ".hidden" / key; // Never created.
        return result;
    }
}
