#include "arc_merge.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <xxhash.h>

namespace ios_mod_manager::arc
{
    namespace fs = std::filesystem;
    static constexpr uint32_t Magic = 0x55AA382D;
    static constexpr size_t MaxEntries = 250000, MaxTable = 32 * 1024 * 1024;

    std::string Key(std::string value)
    {
        for (char& c : value)
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    }

    static void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    static void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    static uint32_t U32(const uint8_t* p)
    {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }

    static void Put(std::ostream& out, uint32_t value)
    {
        const char bytes[] = { char(value >> 24), char(value >> 16), char(value >> 8), char(value) };
        out.write(bytes, 4);
    }

    static bool SafeName(std::string_view name)
    {
        return !name.empty() && name != "." && name != ".." &&
            name.find_first_of("/\\:\0", 0, 4) == std::string_view::npos;
    }

    Members Read(const fs::path& file)
    {
        Require(fs::is_regular_file(file) && !fs::is_symlink(file), "Archive is not an ordinary file.");
        const auto length = fs::file_size(file);
        Require(length >= 45 && length <= UINT32_MAX, "Invalid U8 archive size.");
        std::ifstream in(file, std::ios::binary);
        std::array<uint8_t, 32> header{};
        Require(bool(in.read(reinterpret_cast<char*>(header.data()), header.size())), "Cannot read archive header.");
        Require(U32(header.data()) == Magic, "Merging requires a U8 .arc archive.");
        const uint32_t start = U32(header.data() + 4), tableSize = U32(header.data() + 8), data = U32(header.data() + 12);
        Require(start >= 32 && tableSize >= 13 && tableSize <= MaxTable &&
            uint64_t(start) + tableSize <= data && data <= length, "Invalid archive table bounds.");
        std::vector<uint8_t> table(tableSize);
        in.seekg(start);
        Require(bool(in.read(reinterpret_cast<char*>(table.data()), table.size())), "Truncated archive table.");
        const uint32_t count = U32(table.data() + 8);
        Require(count > 0 && count <= MaxEntries && (U32(table.data()) >> 24) == 1,
            "Invalid U8 root type or entry count (flags=" + std::to_string(U32(table.data())) +
            ", count=" + std::to_string(count) + ").");
        // Toolchains emit both extended Sonic '06 nodes and ordinary raw U8
        // nodes. Validate the entire table for each layout instead of assuming
        // that root padding, parent metadata or its label must be zero.
        const auto parse = [&](size_t stride) -> Members
        {
            Require(uint64_t(count) * stride < tableSize, "Entry table exceeds its declared size.");
            const size_t strings = size_t(count) * stride;
            const auto nameAt = [&](uint32_t flags)
            {
                const size_t nameOffset = strings + (flags & 0xFFFFFF);
                Require(nameOffset < table.size(), "Invalid archive name offset.");
                size_t end = nameOffset;
                while (end < table.size() && table[end]) ++end;
                Require(end < table.size(), "Unterminated archive name.");
                return std::string(reinterpret_cast<const char*>(table.data() + nameOffset), end - nameOffset);
            };
            // The root label is metadata, not a path component. Several packers
            // name it, and the game's lookup starts with its children.
            nameAt(U32(table.data()));
            struct Directory { uint32_t index, end; std::string path; };
            std::vector<Directory> stack{{0, count, ""}};
            Members result;
            for (uint32_t i = 1; i < count; ++i)
            {
                while (i >= stack.back().end) stack.pop_back();
                const uint8_t* node = table.data() + size_t(i) * stride;
                const uint32_t flags = U32(node), offset = U32(node + 4), size = U32(node + 8);
                const uint32_t expanded = stride == 16 ? U32(node + 12) : 0;
                Require((flags >> 24) <= 1, "Unknown archive entry type.");
                const auto name = nameAt(flags);
                Require(SafeName(name), "Unsafe archive member name.");
                const std::string path = stack.back().path.empty() ? name : stack.back().path + '/' + name;
                Require(path.size() <= 4096, "Archive member path is too long.");
                Member member{path, (flags >> 24) == 1, file, offset, size, expanded};
                if (member.directory)
                {
                    Require(offset == stack.back().index && size > i && size <= stack.back().end,
                        "Invalid archive directory hierarchy.");
                    // The extended word is only a decompressed size for files.
                    member.expandedSize = 0;
                    stack.push_back({i, size, path});
                }
                else
                    Require(offset >= data && uint64_t(offset) + size <= length && (expanded == 0 || size != 0),
                        "Invalid archive member bounds.");
                Require(result.emplace(Key(path), std::move(member)).second, "Duplicate case-insensitive archive member.");
            }
            return result;
        };
        std::string extendedError;
        try { return parse(16); }
        catch (const std::runtime_error& ex) { extendedError = ex.what(); }
        try { return parse(12); }
        catch (const std::runtime_error& ex)
        {
            throw std::runtime_error("Invalid U8 table (16-byte: " + extendedError +
                "; 12-byte: " + ex.what() + ").");
        }
    }

    Members ReadFolder(const fs::path& folder)
    {
        Require(fs::is_directory(folder) && !fs::is_symlink(folder), "Invalid extracted archive folder.");
        Members result;
        for (const auto& entry : fs::recursive_directory_iterator(folder))
        {
            Require(!entry.is_symlink() && (entry.is_directory() || entry.is_regular_file()), "Unsupported extracted archive entry.");
            const std::string name = entry.path().lexically_relative(folder).generic_string();
            Require(name.size() <= 4096 && result.size() < MaxEntries, "Extracted archive is too large.");
            for (const auto& component : entry.path().lexically_relative(folder))
                Require(SafeName(component.string()), "Unsafe extracted archive member.");
            const auto size = entry.is_directory() ? 0 : entry.file_size();
            Require(size <= UINT32_MAX, "Archive member exceeds 4 GiB.");
            Require(result.emplace(Key(name), Member{name, entry.is_directory(), entry.path(), 0, uint32_t(size), 0}).second,
                "Duplicate case-insensitive extracted member.");
        }
        return result;
    }

    void Overlay(Members& base, const Members& patch)
    {
        for (const auto& [key, member] : patch)
        {
            auto found = base.find(key);
            if (found == base.end()) base.emplace(key, member);
            else
            {
                Require(found->second.directory == member.directory, "A mod replaces an archive directory with a file (or vice versa).");
                // Preserve the original member spelling for the game's internal lookup.
                if (!member.directory)
                {
                    auto name = found->second.name;
                    found->second = member;
                    found->second.name = std::move(name);
                }
            }
        }
    }

    static void Payload(const Member& member, const auto& consume)
    {
        std::ifstream in(member.source, std::ios::binary);
        in.seekg(member.offset);
        std::array<char, 65536> buffer;
        uint64_t remaining = member.size;
        while (remaining)
        {
            const auto size = std::min<uint64_t>(remaining, buffer.size());
            Require(bool(in.read(buffer.data(), size)), "Archive input changed or became unreadable.");
            consume(buffer.data(), size);
            remaining -= size;
        }
    }

    std::string Fingerprint(const Members& members)
    {
        std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hash(XXH3_createState(), XXH3_freeState);
        Require(bool(hash), "Cannot allocate archive fingerprint.");
        XXH3_64bits_reset(hash.get());
        const auto update = [&](const void* data, size_t size) { XXH3_64bits_update(hash.get(), data, size); };
        constexpr std::string_view version = "Sonic06-U8-merge-v2";
        update(version.data(), version.size());
        for (const auto& [key, member] : members)
        {
            update(key.c_str(), key.size() + 1);
            const auto spelling = fs::path(member.name).filename().string();
            update(spelling.c_str(), spelling.size() + 1);
            const uint32_t fields[] = { member.directory ? 1u : 0u,
                member.directory ? 0u : member.size, member.directory ? 0u : member.expandedSize };
            update(fields, sizeof(fields));
            if (!member.directory) Payload(member, update);
        }
        return std::to_string(XXH3_64bits_digest(hash.get()));
    }

    void Write(const fs::path& file, const Members& members)
    {
        struct Node
        {
            std::string name;
            const Member* member = nullptr;
            std::map<std::string, std::unique_ptr<Node>> children;
            uint32_t parent = 0, end = 0, nameOffset = 0, dataOffset = 0;
        };
        Node root;
        for (const auto& [key, member] : members)
        {
            Node* node = &root;
            for (const auto& part : fs::path(member.name))
            {
                const auto name = part.string();
                Require(SafeName(name), "Unsafe archive output member.");
                Require(!node->member || node->member->directory, "Archive file has child entries.");
                auto& child = node->children[Key(name)];
                if (!child) { child = std::make_unique<Node>(); child->name = name; }
                node = child.get();
            }
            Require(member.directory || node->children.empty(), "Archive file conflicts with a directory.");
            node->member = &member;
        }
        std::vector<Node*> nodes;
        std::string strings(1, '\0');
        const auto visit = [&](auto&& self, Node& node, uint32_t parent) -> void
        {
            const uint32_t index = uint32_t(nodes.size());
            Require(nodes.size() < MaxEntries && strings.size() <= 0xFFFFFF, "Merged archive table is too large.");
            node.parent = parent;
            node.nameOffset = index ? uint32_t(strings.size()) : 0;
            if (index) { strings += node.name; strings += '\0'; }
            nodes.push_back(&node);
            for (auto& [key, child] : node.children) self(self, *child, index);
            node.end = uint32_t(nodes.size());
        };
        visit(visit, root, 0);
        const uint64_t tableSize = uint64_t(nodes.size()) * 16 + strings.size();
        Require(tableSize <= MaxTable, "Merged archive table is too large.");
        const auto align = [](uint64_t value) { return (value + 31) & ~uint64_t(31); };
        const uint64_t data = align(32 + tableSize);
        uint64_t cursor = data;
        for (auto* node : nodes)
            if (node->member && !node->member->directory)
            {
                node->dataOffset = uint32_t(cursor);
                cursor = align(cursor + node->member->size);
                Require(cursor <= UINT32_MAX, "Merged archive exceeds 4 GiB.");
            }
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        Require(bool(out), "Cannot create merged archive (check free storage).");
        Put(out, Magic); Put(out, 32); Put(out, uint32_t(tableSize)); Put(out, uint32_t(data));
        const std::array<char, 32> zeros{};
        // Identify the extended Sonic '06 layout to U8 tools. All-zero
        // reserved words conventionally identify ordinary 12-byte U8 nodes.
        Put(out, 0xE4F91200); Put(out, 0x00000402);
        out.write(zeros.data(), 8);
        for (const auto* node : nodes)
        {
            const bool directory = !node->member || node->member->directory;
            Put(out, node->nameOffset | (directory ? 0x01000000 : 0));
            Put(out, directory ? node->parent : node->dataOffset);
            Put(out, directory ? node->end : node->member->size);
            Put(out, directory ? 0 : node->member->expandedSize);
        }
        out.write(strings.data(), strings.size());
        const auto pad = [&](uint64_t offset)
        {
            const auto position = out.tellp();
            Require(position >= 0 && uint64_t(position) <= offset, "Cannot write merged archive.");
            uint64_t remaining = offset - uint64_t(position);
            while (remaining) { const auto size = std::min<uint64_t>(remaining, zeros.size()); out.write(zeros.data(), size); remaining -= size; }
        };
        for (const auto* node : nodes)
            if (node->member && !node->member->directory)
            {
                pad(node->dataOffset);
                Payload(*node->member, [&](const void* bytes, size_t size) { out.write(static_cast<const char*>(bytes), size); });
            }
        pad(cursor);
        out.close();
        Require(bool(out), "Could not finish merged archive (check free storage).");
    }

    static void CheckRelative(const std::string& key)
    {
        Require(!fs::path(key).is_absolute(), "Absolute archive overlay path.");
        for (const auto& part : fs::path(key))
            Require(SafeName(part.string()), "Unsafe archive overlay path.");
    }

    std::map<std::string, fs::path> BuildOverlays(const fs::path& gameRoot, const fs::path& cacheRoot,
        const std::vector<Layer>& layers)
    {
        std::map<std::string, fs::path> result;
        if (std::none_of(layers.begin(), layers.end(), [](const Layer& layer) { return layer.merge; })) return result;

        std::map<std::string, fs::path> originals;
        if (fs::is_directory(gameRoot))
            for (const auto& entry : fs::recursive_directory_iterator(gameRoot))
                if (entry.is_regular_file() && !entry.is_symlink() && Key(entry.path().extension().string()) == ".arc")
                {
                    const auto key = Key(entry.path().lexically_relative(gameRoot).generic_string());
                    CheckRelative(key);
                    Require(originals.emplace(key, entry.path()).second, "Original archive paths differ only in case.");
                }

        struct Inputs
        {
            std::map<std::string, fs::path> archives, loose;
            std::set<std::string> affected;
        };
        std::vector<Inputs> inputs(layers.size());
        std::set<std::string> targets;
        bool looseMerging = false;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            for (auto it = fs::recursive_directory_iterator(layers[i].root); it != fs::recursive_directory_iterator(); ++it)
            {
                Require(!it->is_symlink(), "Archive mod paths must not be symbolic links.");
                const auto key = Key(it->path().lexically_relative(layers[i].root).generic_string());
                CheckRelative(key);
                const bool arc = Key(it->path().extension().string()) == ".arc";
                const bool extracted = it->is_directory() && layers[i].merge && originals.contains(key + ".arc");
                if (arc || extracted)
                {
                    const auto target = extracted ? key + ".arc" : key;
                    Require(inputs[i].archives.emplace(target, it->path()).second,
                        "A mod contains both a packed and extracted version of an archive.");
                    if (layers[i].merge) targets.insert(target);
                    if (it->is_directory()) it.disable_recursion_pending();
                }
                else if (layers[i].merge && it->is_regular_file() && key != "mod.ini")
                {
                    inputs[i].loose.emplace(key, it->path());
                    looseMerging = true;
                }
            }
        }

        // Read only node/name tables here. Original payloads are streamed only for
        // affected archives, not unpacked into RAM or into the installed game.
        if (looseMerging)
        {
            std::set<std::string> candidates;
            for (const auto& [key, file] : originals) candidates.insert(key);
            for (const auto& input : inputs)
                for (const auto& [key, file] : input.archives) candidates.insert(key);
            for (const auto& key : candidates)
            {
                std::set<std::string> memberKeys;
                const auto inspect = [&](const fs::path& file)
                {
                    try
                    {
                        auto table = fs::is_directory(file) ? ReadFolder(file) : Read(file);
                        for (const auto& [memberKey, member] : table)
                            if (!member.directory) memberKeys.insert(memberKey);
                    }
                    catch (const std::exception&)
                    {
                        // Non-U8 files cannot supply a member index. A directly
                        // targeted merge is still validated below and fails closed.
                    }
                };
                if (auto original = originals.find(key); original != originals.end()) inspect(original->second);
                for (const auto& input : inputs)
                    if (auto packed = input.archives.find(key); packed != input.archives.end()) inspect(packed->second);
                const auto slash = key.find('/');
                const auto platform = slash == std::string::npos ? "" : key.substr(0, slash + 1);
                for (const auto& memberKey : memberKeys)
                    for (size_t i = 0; i < layers.size(); ++i)
                        if (layers[i].merge && (inputs[i].loose.contains(memberKey) ||
                            (!platform.empty() && inputs[i].loose.contains(platform + memberKey))))
                        { targets.insert(key); inputs[i].affected.insert(key); }
            }
        }

        static std::atomic<uint64_t> sequence{};
        for (const auto& key : targets)
        {
            try
            {
                // A full replacement cuts off originals and all lower layers.
                size_t boundary = layers.size();
                for (size_t i = 0; i < layers.size(); ++i)
                    if (inputs[i].archives.contains(key) && (!layers[i].merge || layers[i].replacements.contains(key)))
                    { boundary = i; break; }
                bool needsMerge = false;
                for (size_t i = 0; i < boundary; ++i)
                    if (layers[i].merge && (inputs[i].archives.contains(key) || inputs[i].affected.contains(key))) needsMerge = true;
                if (!needsMerge) continue; // Keep the normal, unmodified full-file overlay.

                const auto readInput = [](const fs::path& path, const std::string& origin)
                {
                    try { return fs::is_directory(path) ? ReadFolder(path) : Read(path); }
                    catch (const std::exception& ex) { throw std::runtime_error(origin + ": " + ex.what()); }
                };
                Members merged;
                if (boundary < layers.size()) merged = readInput(inputs[boundary].archives.at(key), "Full replacement archive");
                else if (auto found = originals.find(key); found != originals.end()) merged = readInput(found->second, "Installed original archive");

                const auto slash = key.find('/');
                const auto platform = slash == std::string::npos ? "" : key.substr(0, slash + 1);
                for (size_t i = boundary; i-- > 0; )
                {
                    if (auto input = inputs[i].archives.find(key); input != inputs[i].archives.end())
                        Overlay(merged, readInput(input->second, "Mod archive in " + layers[i].root.filename().string()));
                    if (layers[i].merge)
                        for (auto& [memberKey, member] : merged)
                        {
                            if (member.directory) continue;
                            auto loose = inputs[i].loose.find(memberKey);
                            if (loose == inputs[i].loose.end() && !platform.empty()) loose = inputs[i].loose.find(platform + memberKey);
                            if (loose != inputs[i].loose.end())
                            {
                                const auto size = fs::file_size(loose->second);
                                Require(size <= UINT32_MAX, "Loose archive member exceeds 4 GiB.");
                                member.source = loose->second;
                                member.offset = member.expandedSize = 0;
                                member.size = uint32_t(size);
                            }
                        }
                }
                const auto fingerprint = Fingerprint(merged);
                const auto cacheName = fingerprint + '-' + std::to_string(XXH3_64bits(key.data(), key.size()));
                const auto directory = cacheRoot / cacheName;
                const auto output = directory / key;
                Require(!fs::is_symlink(cacheRoot), "Archive cache must not be a symbolic link.");
                fs::create_directories(cacheRoot);
                Require(!fs::is_symlink(directory), "Archive cache entry must not be a symbolic link.");
                if (fs::exists(directory))
                {
                    bool valid = false;
                    try
                    {
                        for (const auto& entry : fs::recursive_directory_iterator(directory))
                            Require(!entry.is_symlink(), "Invalid cache link.");
                        valid = Fingerprint(Read(output)) == fingerprint;
                    }
                    catch (const std::exception&) {}
                    if (valid) { result.emplace(key, output); continue; }
                    fs::remove_all(directory); // Only this content-addressed generated cache.
                }
                fs::path staging;
                for (unsigned attempt = 0; attempt < 64; ++attempt)
                {
                    staging = cacheRoot / (".building-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()) + '-' + std::to_string(sequence++));
                    if (fs::create_directory(staging)) break;
                    staging.clear();
                }
                Require(!staging.empty(), "Cannot create archive cache staging folder.");
                try
                {
                    fs::create_directories((staging / key).parent_path());
                    Write(staging / key, merged);
                    Require(Fingerprint(Read(staging / key)) == fingerprint, "Merged archive failed verification.");
                    fs::rename(staging, directory);
                }
                catch (...)
                {
                    std::error_code ignored;
                    fs::remove_all(staging, ignored);
                    throw;
                }
                result.emplace(key, output);
            }
            catch (const std::exception& ex)
            {
                throw std::runtime_error("Cannot merge " + key + ": " + ex.what());
            }
        }
        return result;
    }
}
