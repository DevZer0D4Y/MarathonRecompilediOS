#pragma once
#include <filesystem>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ios_mod_manager::arc
{
    struct Member
    {
        std::string name;
        bool directory = false;
        std::filesystem::path source;
        uint32_t offset = 0, size = 0, expandedSize = 0;
    };
    using Members = std::map<std::string, Member>;
    std::string Key(std::string value);
    Members Read(const std::filesystem::path& file);
    Members ReadFolder(const std::filesystem::path& folder);
    void Overlay(Members& base, const Members& patch);
    // Payloads are copied in bounded chunks, retaining original compression.
    void Write(const std::filesystem::path& file, const Members& members);
    std::string Fingerprint(const Members& members);
    struct Layer
    {
        std::filesystem::path root;
        bool merge = false;
        std::set<std::string> replacements;
    };
    // Layers are ordered highest priority first. Originals are never modified.
    std::map<std::string, std::filesystem::path> BuildOverlays(
        const std::filesystem::path& gameRoot, const std::filesystem::path& cacheRoot,
        const std::vector<Layer>& layers);
}
