#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace ios_mod_manager
{
    struct Entry
    {
        std::filesystem::path iniPath;
        std::string title;
        std::string author;
        std::string issue;
        bool enabled = false;
    };

    // The database stores paths relative to the user directory, surviving app updates.
    std::filesystem::path DatabasePath(const std::filesystem::path& userPath);
    std::filesystem::path ResolveRelative(const std::filesystem::path& base, std::string value);
    std::string CompatibilityIssue(const std::filesystem::path& modIni);
    std::vector<Entry> Scan(const std::filesystem::path& userPath);
    std::string SelectionIssue(const std::vector<Entry>& entries);
    bool Save(const std::filesystem::path& userPath, const std::vector<Entry>& entries, std::string& error);
    bool Import(const std::filesystem::path& userPath, const std::filesystem::path& folder, std::string& error);

    // Built once before guest startup, then read concurrently by game threads.
    // Only assets opened through the virtual filesystem can be replaced.
    class FileOverlay
    {
        std::vector<std::vector<std::filesystem::path>> m_includeDirs;
        std::unordered_map<std::string, std::filesystem::path> m_paths;
        std::unordered_map<std::string, std::vector<std::filesystem::path>> m_directories;
        std::filesystem::path m_saveFile;

    public:
        void Load(const std::filesystem::path& userPath);
        std::filesystem::path Resolve(std::string_view guestPath) const;
        std::vector<std::filesystem::path> SearchDirectories(std::string_view guestPath) const;
        std::vector<std::filesystem::path>* IncludeDirectories(size_t index);
        const std::filesystem::path& SaveFile() const { return m_saveFile; }
    };
}
