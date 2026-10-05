#pragma once
#include <charconv>
#include <string_view>

inline bool ParseReleaseVersion(std::string_view text, int& major, int& minor, int& revision)
{
    if (text.starts_with('v')) text.remove_prefix(1);
    int parts[3]{};
    for (int i = 0; i < 3; ++i)
    {
        size_t separator = text.find('.');
        std::string_view part = i == 2 ? text : text.substr(0, separator);
        if (part.empty() || part.front() < '0' || part.front() > '9') return false;
        auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), parts[i]);
        if (error != std::errc{} || end != part.data() + part.size()) return false;
        if (i < 2)
        {
            if (separator == std::string_view::npos) return false;
            text.remove_prefix(separator + 1);
        }
    }
    major = parts[0]; minor = parts[1]; revision = parts[2];
    return true;
}
