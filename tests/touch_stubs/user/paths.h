#pragma once
#include <filesystem>
inline std::filesystem::path touchTestDirectory;
inline const std::filesystem::path& GetUserPath() { return touchTestDirectory; }
