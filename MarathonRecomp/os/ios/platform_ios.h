#pragma once

#include <filesystem>
#include <SDL_events.h>

namespace ios
{
    std::filesystem::path UserPath();
    void Initialize();
    void HandleLifecycle(const SDL_Event& event);
    void OpenURL(const char* url);
    void ShowRestartRequired();

    // 0 = nominal, 1 = fair, 2 = serious, 3 = critical (NSProcessInfoThermalState).
    int GetThermalState();
}
