#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ModLoader
{
    struct Code
    {
        std::string name;
        bool enabled;
    };

    static inline bool s_isLogTypeConsole;

    static inline std::filesystem::path s_saveFilePath;
    
    static std::filesystem::path ResolvePath(std::string_view path);

    static std::vector<std::filesystem::path>* GetIncludeDirectories(size_t modIndex);

    static std::vector<std::filesystem::path> GetSearchDirectories(std::string_view path);
    static std::vector<Code> GetBuiltInCodes();
    static void SaveBuiltInCodes(const std::vector<Code>& codes);

    static void Init();
};
