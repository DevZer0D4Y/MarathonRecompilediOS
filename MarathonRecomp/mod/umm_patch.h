#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// Runs Sonic '06 Mod Manager (.mlua) patches, such as those from
// Knuxfan24/Sonic-06-Mod-Manager-Patches, without modifying the installed game.
// Archive, package and file edits are written to the mod cache and served by the
// file overlay. Writes to the executable become guest memory writes applied after
// the game image is loaded. Only writes to data can work: the game's code is
// recompiled ahead of time, so patches that rewrite instructions are rejected, as
// are Lua script edits, which need decompiled scripts.
namespace ios_mod_manager::patch
{
    // Xbox 360 default.xex layout: the PE image is mapped at 0x82000000, and the
    // Sonic '06 Mod Manager places it at file offset 0x3000 of the decrypted XEX.
    constexpr uint32_t ImageBase = 0x82000000, ImageSize = 0xD98000;
    constexpr uint32_t CodeBase = 0x82160000, CodeSize = 0x9AABB8;
    constexpr uint32_t ExecutableHeaderSize = 0x3000;

    struct Info
    {
        std::filesystem::path file;
        std::string title, author, blurb;
        std::string issue; // Why the patch can't run here; empty when supported.
    };

    struct MemoryWrite
    {
        uint32_t address;
        std::vector<uint8_t> bytes;
    };

    struct Result
    {
        // Overlay keys (lowercase, relative to the game folder) to generated files.
        // A path that doesn't exist hides the original file, like a rename did.
        std::map<std::string, std::filesystem::path> files;
        std::vector<MemoryWrite> memory;
    };

    std::filesystem::path Directory(const std::filesystem::path& userPath);
    Info Inspect(const std::filesystem::path& mlua);
    std::vector<Info> Scan(const std::filesystem::path& userPath);
    std::set<std::string> LoadSelection(const std::filesystem::path& userPath);
    bool SaveSelection(const std::filesystem::path& userPath, const std::set<std::string>& names, std::string& error);
    // Copies every .mlua below folder into the patches directory; returns how many.
    size_t Import(const std::filesystem::path& userPath, const std::filesystem::path& folder, std::string& error);

    // Applies patches in order. resolve maps an overlay key to the file the game
    // would currently load (a mod's or the original), or an empty path.
    // listDirectory lists the overlay keys of the files directly inside a key.
    Result Apply(const std::vector<std::filesystem::path>& patches, const std::filesystem::path& cacheRoot,
        const std::function<std::filesystem::path(const std::string&)>& resolve,
        const std::function<std::vector<std::string>(const std::string&)>& listDirectory);

    // Exposed for tests: the Sonic '06 *.pkg asset package format.
    struct Package
    {
        struct File { std::string name, path; };
        struct Category { std::string name; std::vector<File> files; };
        std::vector<Category> categories;
        static Package Parse(const std::vector<uint8_t>& data);
        std::vector<uint8_t> Serialise() const;
    };
}
