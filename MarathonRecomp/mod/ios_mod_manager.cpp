#include "ios_mod_manager.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <xxhash.h>
#include <ankerl/unordered_dense.h>
#include "ini_file.h"
#include "arc_merge.h"

namespace ios_mod_manager
{
    static std::string FoldPath(std::string value)
    {
        for (char& c : value)
        {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        return value;
    }

    static std::vector<std::string> g_builtInCodes;

    // Patch names are compared without the extension, spacing, underscores or letter case.
    static std::string PatchKey(std::string_view name)
    {
        if (name.size() >= 5 && FoldPath(std::string(name.substr(name.size() - 5))) == ".mlua")
            name.remove_suffix(5);
        std::string key;
        for (char c : name)
        {
            if (c == ' ' || c == '_' || c == '-' || c == '\t') continue;
            key += (c >= 'A' && c <= 'Z') ? char(c + 'a' - 'A') : c;
        }
        return key;
    }

    static std::vector<std::string> SplitList(const std::string& value)
    {
        std::vector<std::string> items;
        size_t start = 0;
        for (;;)
        {
            const auto end = value.find(',', start);
            auto item = value.substr(start, end == std::string::npos ? end : end - start);
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (!item.empty()) items.push_back(std::move(item));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return items;
    }

    void SetBuiltInCodes(std::vector<std::string> names) { g_builtInCodes = std::move(names); }

    static std::string FindBuiltInCode(std::string_view patch)
    {
        const auto key = PatchKey(patch);
        for (const auto& code : g_builtInCodes)
            if (PatchKey(code) == key) return code;
        return {};
    }

    std::vector<std::string> RequiredCodes(const std::filesystem::path& modIni)
    {
        IniFile ini;
        std::vector<std::string> codes;
        if (!ini.read(modIni)) return codes;
        for (const auto& patch : SplitList(ini.getString("Patches", "RequiredPatches", "")))
            if (auto code = FindBuiltInCode(patch); !code.empty()) codes.push_back(std::move(code));
        return codes;
    }

    // UMM .mlua patches run against the Xbox executable before boot. Asset edits are harmless to
    // skip, but code edits cannot reach recompiled code, and mods built on them crash in-game.
    static std::string ScriptPatchIssue(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::string text(std::istreambuf_iterator<char>(stream), {});
        const auto folded = FoldPath(text);
        if (folded.find("core.xex") != std::string::npos)
            return "Loads the Khronos code DLL (Core.xex) by patching the game executable. Xbox DLLs cannot run in the recompiled game, so this mod and mods built on Khronos need a native port.";
        for (const char* call : { "writevirtualbytes", "decryptexecutable", "executable|", "writenopstoexecutable" })
            if (folded.find(call) != std::string::npos)
                return "Its " + path.filename().string() + " patches the game executable, which the recompiled game cannot apply.";
        return {};
    }

    std::filesystem::path DatabasePath(const std::filesystem::path& userPath)
    {
        return userPath / "ios-mods.ini";
    }

    std::filesystem::path ResolveRelative(const std::filesystem::path& base, std::string value)
    {
        std::replace(value.begin(), value.end(), '\\', '/');
        std::filesystem::path path = std::u8string_view(reinterpret_cast<const char8_t*>(value.c_str()));
        // Windows absolute paths cannot be transplanted into an iOS sandbox.
        if (value.find(':') != std::string::npos || path.is_absolute())
            return {};
        auto result = (base / path).lexically_normal();
        auto relative = result.lexically_relative(base.lexically_normal());
        if (relative.empty() || *relative.begin() == "..")
            return {};
        return result;
    }

    std::string CompatibilityIssue(const std::filesystem::path& modIni)
    {
        try
        {
            IniFile ini;
            if (std::filesystem::is_symlink(modIni) || std::filesystem::is_symlink(modIni.parent_path()))
                return "Symbolic links are not supported in iOS mods.";
            if (!ini.read(modIni))
                return "Cannot read mod.ini.";
            if (!ini.getString("Main", "DLLFile", "").empty())
                return "Requires a native plugin; this mod needs an iOS port.";
            if (!ini.getString("Main", "CodeFile", "").empty())
                return "Custom HMM code is not supported by this loader.";
            if (ini.get<size_t>("Main", "DependsCount", 0) > 256)
                return "Invalid dependency count.";
            std::string missingPatches;
            for (const auto& patch : SplitList(ini.getString("Patches", "RequiredPatches", "")))
                if (FindBuiltInCode(patch).empty())
                    missingPatches += (missingPatches.empty() ? "" : ", ") + patch;
            if (!missingPatches.empty())
                return "Needs executable patches the recompiled game doesn't include: " + missingPatches + ".";
            const auto base = modIni.parent_path();
            if (!ini.contains("Details") && !ini.contains("Filesystem"))
            {
                auto count = ini.get<size_t>("Main", "IncludeDirCount", 0);
                if (count == 0 || count > 256)
                    return "Missing or invalid include directories.";
                for (size_t i = 0; i < count; ++i)
                {
                    auto value = ini.getString("Main", "IncludeDir" + std::to_string(i), "");
                    auto path = ResolveRelative(base, value);
                    std::error_code ec;
                    if (value.empty() || path.empty() || !std::filesystem::is_directory(path, ec))
                        return "Include directory is missing or outside the mod folder.";
                }
            }
            // Reject symlinks, including those leading outside the imported folder.
            std::unordered_map<std::string, std::filesystem::path> names;
            for (const auto& file : std::filesystem::recursive_directory_iterator(base))
            {
                if (file.is_symlink() || (!file.is_regular_file() && !file.is_directory()))
                    return "Symbolic links are not supported in iOS mods.";
                const auto key = FoldPath(file.path().lexically_relative(base).generic_string());
                if (!names.emplace(key, file.path()).second)
                    return "Two mod paths differ only in letter case.";
                const auto name = FoldPath(file.path().filename().string());
                if (ini.getBool("Details", "Merge", ini.getBool("Filesystem", "Merge", ini.getBool("Main", "Merge", false))) &&
                    (name.ends_with(".ar") || name.ends_with(".arl") || name.find(".ar.") != std::string::npos))
                    return "This archive uses Unleashed's .ar/.arl format. Sonic '06 asset merging requires U8 .arc archives.";
                if (name.starts_with('+') && (name.ends_with(".arl") || name.ends_with(".ar") || name.find(".ar.") != std::string::npos))
                    return "Unleashed append archives are not supported. Use Sonic '06 replacement .arc files.";
                if (name.ends_with(".xex") || name.ends_with(".xexp") || name.ends_with(".dll") || name.ends_with(".dylib"))
                    return "Executable patches and native plugins need an individual source port.";
                if (name.ends_with(".mlua") && file.is_regular_file())
                    if (auto issue = ScriptPatchIssue(file.path()); !issue.empty())
                        return issue;
            }
            const auto save = ini.contains("Details") || ini.contains("Filesystem")
                ? ini.getString("Details", "Save", ini.getString("Filesystem", "Save", ""))
                : ini.getString("Main", "SaveFile", "");
            if (!save.empty() && ResolveRelative(base, save).empty())
                return "Save redirection must stay inside the mod folder.";
            if (!save.empty())
            {
                const auto path = ResolveRelative(base, save);
                const bool umm = ini.contains("Details") || ini.contains("Filesystem");
                if ((umm && std::filesystem::is_directory(path)) || (!umm && std::filesystem::is_regular_file(path)))
                    return "HMM saves need a directory; UMM saves need a filename.";
            }
            return {};
        }
        catch (const std::exception& ex) { return std::string("Cannot inspect mod: ") + ex.what(); }
    }

    std::vector<Entry> Scan(const std::filesystem::path& userPath)
    {
        const auto modsPath = userPath / "mods";
        if (std::filesystem::is_symlink(modsPath))
            throw std::runtime_error("The mods folder must not be a symbolic link.");
        std::filesystem::create_directories(modsPath);
        IniFile database;
        database.read(DatabasePath(userPath));
        std::vector<Entry> entries;
        for (const auto& folder : std::filesystem::directory_iterator(modsPath))
        {
            if (folder.is_symlink() || !folder.is_directory())
                continue;
            const auto iniPath = folder.path() / "mod.ini";
            if (!std::filesystem::is_regular_file(iniPath) || std::filesystem::is_symlink(iniPath))
                continue;
            IniFile ini;
            ini.read(iniPath);
            Entry entry;
            entry.iniPath = iniPath;
            entry.title = ini.getString("Desc", "Title", ini.getString("Details", "Title", folder.path().filename().string()));
            entry.author = ini.getString("Desc", "Author", ini.getString("Details", "Author", ""));
            entry.issue = CompatibilityIssue(iniPath);
            entries.emplace_back(std::move(entry));
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.iniPath < b.iniPath; });
        // Restore order including disabled mods; newly discovered mods start disabled.
        std::vector<Entry> ordered;
        auto count = std::min(database.get<size_t>("Installed", "Count", 0), size_t(10000));
        for (size_t i = 0; i < count; ++i)
        {
            auto path = ResolveRelative(userPath, database.getString("Installed", "Mod" + std::to_string(i), ""));
            auto found = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.iniPath == path; });
            if (found == entries.end())
                continue;
            found->enabled = found->issue.empty() && database.getBool("Installed", "Enabled" + std::to_string(i), false);
            ordered.emplace_back(std::move(*found));
            entries.erase(found);
        }
        ordered.insert(ordered.end(), entries.begin(), entries.end());
        return ordered;
    }

    static bool VersionAtLeast(std::string actual, std::string required)
    {
        if (required.empty() || actual == required)
            return true;
        auto parse = [](const std::string& text, std::vector<unsigned>& result)
        {
            size_t start = 0;
            while (start < text.size())
            {
                const auto end = text.find('.', start);
                const auto length = end == std::string::npos ? text.size() - start : end - start;
                unsigned value = 0;
                const auto conversion = std::from_chars(text.data() + start, text.data() + start + length, value);
                if (conversion.ec != std::errc{} || conversion.ptr != text.data() + start + length)
                    return false;
                result.push_back(value);
                if (end == std::string::npos) break;
                start = end + 1;
                if (start == text.size()) return false;
            }
            return !result.empty();
        };
        std::vector<unsigned> a, b;
        if (!parse(actual, a) || !parse(required, b)) return false;
        a.resize(std::max(a.size(), b.size()));
        b.resize(a.size());
        return a >= b;
    }

    std::string SelectionIssue(const std::vector<Entry>& entries)
    {
        for (const auto& entry : entries)
        {
            if (!entry.enabled) continue;
            IniFile ini;
            if (!ini.read(entry.iniPath)) return "Cannot read " + entry.title + ".";
            const auto count = ini.get<size_t>("Main", "DependsCount", 0);
            if (count > 256) return "Invalid dependency count in " + entry.title + ".";
            for (size_t i = 0; i < count; ++i)
            {
                const auto value = ini.getString("Main", "Depends" + std::to_string(i), "");
                std::vector<std::string> fields;
                size_t start = 0;
                for (;;)
                {
                    const auto end = value.find('|', start);
                    fields.push_back(value.substr(start, end == std::string::npos ? end : end - start));
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                if (fields.size() < 3 || fields[0].empty()) return "Invalid dependency declaration in " + entry.title + ".";
                bool satisfied = false;
                for (const auto& candidate : entries)
                {
                    if (!candidate.enabled || candidate.iniPath == entry.iniPath || !candidate.issue.empty()) continue;
                    IniFile dependency;
                    if (dependency.read(candidate.iniPath) && dependency.getString("Main", "ID", "") == fields[0]
                        && VersionAtLeast(dependency.getString("Desc", "Version", ""), fields.size() > 3 ? fields[3] : ""))
                    {
                        satisfied = true;
                        break;
                    }
                }
                if (!satisfied) return entry.title + " requires an enabled compatible dependency: " + fields[1]
                    + (fields.size() > 3 && !fields[3].empty() ? " (version " + fields[3] + " or newer)." : ".");
            }
        }
        return {};
    }

    bool Save(const std::filesystem::path& userPath, const std::vector<Entry>& entries, std::string& error)
    {
        try
        {
            for (const auto& entry : entries)
                if (entry.enabled)
                {
                    const auto issue = CompatibilityIssue(entry.iniPath);
                    if (!issue.empty()) throw std::runtime_error(entry.title + ": " + issue);
                }
            const auto selectionIssue = SelectionIssue(entries);
            if (!selectionIssue.empty()) throw std::runtime_error(selectionIssue);
            auto temporary = DatabasePath(userPath);
            temporary += ".tmp";
            if (std::filesystem::is_symlink(temporary))
                throw std::runtime_error("The mod settings temporary file must not be a symbolic link.");
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream << "[Installed]\nCount=" << entries.size() << '\n';
            std::vector<size_t> active;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                auto path = entries[i].iniPath.lexically_relative(userPath).generic_string();
                if (path.find_first_of("\"\r\n") != std::string::npos || ResolveRelative(userPath, path) != entries[i].iniPath)
                    throw std::runtime_error("Invalid mod path.");
                const bool enabled = entries[i].enabled && CompatibilityIssue(entries[i].iniPath).empty();
                stream << "Mod" << i << "=\"" << path << "\"\nEnabled" << i << '=' << enabled << '\n';
                if (enabled)
                    active.push_back(i);
            }
            stream << "[Main]\nActiveModCount=" << active.size() << '\n';
            for (size_t i = 0; i < active.size(); ++i)
                stream << "ActiveMod" << i << "=ios" << active[i] << '\n';
            stream << "[Mods]\n";
            for (size_t i : active)
                stream << "ios" << i << "=\"" << entries[i].iniPath.lexically_relative(userPath).generic_string() << "\"\n";
            stream.close();
            if (!stream)
                throw std::runtime_error("Could not write mod settings.");
            std::filesystem::rename(temporary, DatabasePath(userPath));
            return true;
        }
        catch (const std::exception& ex) { error = ex.what(); return false; }
    }

    bool Import(const std::filesystem::path& userPath, const std::filesystem::path& folder, std::string& error)
    {
        std::filesystem::path staging;
        bool ownsStaging = false;
        try
        {
            if (!std::filesystem::is_regular_file(folder / "mod.ini") || std::filesystem::is_symlink(folder))
                throw std::runtime_error("Select the extracted folder containing mod.ini.");
            for (const auto& file : std::filesystem::recursive_directory_iterator(folder))
                if (file.is_symlink() || (!file.is_regular_file() && !file.is_directory()))
                    throw std::runtime_error("Mod folders must contain ordinary files and folders.");
            const auto destination = userPath / "mods" / folder.filename();
            if (folder.filename().empty() || folder.filename().string().find_first_of("\"\r\n") != std::string::npos)
                throw std::runtime_error("Invalid mod folder name.");
            if (std::filesystem::is_symlink(destination.parent_path()))
                throw std::runtime_error("The mods folder must not be a symbolic link.");
            std::filesystem::create_directories(destination.parent_path());
            if (std::filesystem::exists(destination))
                throw std::runtime_error("A mod with this folder name is already installed. Rename the source folder first.");
            staging = userPath / "mod-import.tmp";
            const auto source = std::filesystem::weakly_canonical(folder);
            const auto relative = std::filesystem::weakly_canonical(staging).lexically_relative(source);
            if (!relative.empty() && *relative.begin() != "..")
                throw std::runtime_error("Select a mod folder, not a folder containing the app's storage.");
            if (std::filesystem::exists(staging) || std::filesystem::is_symlink(staging))
                throw std::runtime_error("An unfinished import exists at mod-import.tmp. Remove it using Files and try again.");
            ownsStaging = true;
            std::filesystem::copy(folder, staging, std::filesystem::copy_options::recursive);
            std::filesystem::rename(staging, destination);
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            if (ownsStaging)
            {
                std::error_code ec;
                std::filesystem::remove_all(staging, ec);
            }
            return false;
        }
    }

    static bool AssetKey(std::string_view guestPath, std::string& key, bool& save)
    {
        auto value = FoldPath(std::string(guestPath));
        save = false;
        const auto separator = value.find(":/");
        if (separator != std::string::npos)
        {
            const auto root = value.substr(0, separator);
            save = root == "save";
            if (!save && root != "d" && root != "game") return false;
            value.erase(0, separator + 2);
        }
        if (value.find(':') != std::string::npos || value.find('\0') != std::string::npos) return false;
        const std::filesystem::path path(value);
        if (path.is_absolute()) return false;
        for (const auto& component : path)
            if (component == "..") return false;
        key = path.lexically_normal().generic_string();
        if (key == ".") key.clear();
        while (!key.empty() && key.back() == '/') key.pop_back();
        return true;
    }

    void FileOverlay::Load(const std::filesystem::path& userPath)
    {
        const auto entries = Scan(userPath);
        const auto issue = SelectionIssue(entries);
        if (!issue.empty()) throw std::runtime_error(issue);
        FileOverlay next;
        std::vector<arc::Layer> archiveLayers;
        for (const auto& entry : entries)
        {
            if (!entry.enabled) continue;
            const auto compatibility = CompatibilityIssue(entry.iniPath);
            if (!compatibility.empty()) throw std::runtime_error(entry.title + ": " + compatibility);
            IniFile ini;
            if (!ini.read(entry.iniPath)) throw std::runtime_error("Cannot read " + entry.title + ".");
            const auto base = entry.iniPath.parent_path();
            const bool umm = ini.contains("Details") || ini.contains("Filesystem");
            std::vector<std::filesystem::path> includes;
            if (umm) includes.push_back(base);
            else
                for (size_t i = 0; i < ini.get<size_t>("Main", "IncludeDirCount", 0); ++i)
                    includes.push_back(ResolveRelative(base, ini.getString("Main", "IncludeDir" + std::to_string(i), "")));
            for (const auto& include : includes)
            {
                arc::Layer layer{include, ini.getBool("Details", "Merge",
                    ini.getBool("Filesystem", "Merge", ini.getBool("Main", "Merge", false))), {}};
                auto readOnly = ini.getString("Details", "Read-only", ini.getString("Filesystem", "Read-only", ""));
                for (size_t begin = 0; begin < readOnly.size(); )
                {
                    const auto end = readOnly.find(',', begin);
                    auto name = readOnly.substr(begin, end == std::string::npos ? end : end - begin);
                    const auto first = name.find_first_not_of(" \t"), last = name.find_last_not_of(" \t");
                    if (first != std::string::npos) layer.replacements.insert(FoldPath(name.substr(first, last - first + 1)));
                    if (end == std::string::npos) break;
                    begin = end + 1;
                }
                archiveLayers.push_back(std::move(layer));
                next.m_paths.try_emplace("", include);
                next.m_directories[""].push_back(include);
                for (const auto& file : std::filesystem::recursive_directory_iterator(include))
                {
                    const auto key = FoldPath(file.path().lexically_relative(include).generic_string());
                    next.m_paths.try_emplace(key, file.path()); // First enabled mod/include wins.
                    if (file.is_directory()) next.m_directories[key].push_back(file.path());
                }
            }
            next.m_includeDirs.push_back(std::move(includes));
            const auto save = umm ? ini.getString("Details", "Save", ini.getString("Filesystem", "Save", ""))
                                  : ini.getString("Main", "SaveFile", "");
            if (next.m_saveFile.empty() && !save.empty())
            {
                next.m_saveFile = ResolveRelative(base, save);
                if (!umm) next.m_saveFile /= "SonicNextSaveData.bin";
            }
        }
        for (const auto& [key, file] : arc::BuildOverlays(userPath / "game", userPath / "mod-cache", archiveLayers))
        {
            next.m_paths[key] = file;
            // A generated archive must win enumeration too, including its size
            // and file type when its source was an extracted .arc directory.
            auto relative = std::filesystem::path(key).parent_path();
            auto physical = file.parent_path();
            while (true)
            {
                auto& directories = next.m_directories[relative.generic_string()];
                directories.insert(directories.begin(), physical);
                next.m_paths.try_emplace(relative.generic_string(), physical);
                if (relative.empty()) break;
                relative = relative.parent_path();
                physical = physical.parent_path();
            }
        }
        *this = std::move(next);
    }

    std::filesystem::path FileOverlay::Resolve(std::string_view guestPath) const
    {
        std::string key;
        bool save;
        if (!AssetKey(guestPath, key, save)) return {};
        if (save)
        {
            if (m_saveFile.empty() || key.empty()) return {};
            if (key == "sonicnextsavedata.bin") return m_saveFile;
            return ResolveRelative(m_saveFile.parent_path(), key);
        }
        const auto found = m_paths.find(key);
        return found == m_paths.end() ? std::filesystem::path{} : found->second;
    }

    std::vector<std::filesystem::path> FileOverlay::SearchDirectories(std::string_view guestPath) const
    {
        std::string key;
        bool save;
        if (!AssetKey(guestPath, key, save) || save) return {};
        const auto found = m_directories.find(key);
        return found == m_directories.end() ? std::vector<std::filesystem::path>{} : found->second;
    }

    std::vector<std::filesystem::path>* FileOverlay::IncludeDirectories(size_t index)
    {
        return index < m_includeDirs.size() ? &m_includeDirs[index] : nullptr;
    }
}
