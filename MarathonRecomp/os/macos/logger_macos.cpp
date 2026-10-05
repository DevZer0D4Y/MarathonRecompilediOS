#include <os/logger.h>

#ifdef MARATHON_RECOMP_IOS
#include <user/paths.h>

// Apps started from the home screen have no console, so iOS builds also log to a file the
// player can get through the Files app. The previous session's log is kept next to it.
static std::mutex g_logFileMutex;
static FILE* g_logFile;
#endif

void os::logger::Init()
{
#ifdef MARATHON_RECOMP_IOS
    std::lock_guard lock(g_logFileMutex);

    if (g_logFile != nullptr)
        return;

    auto logPath = GetUserPath() / "marathonrecomp.log";

    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);
    std::filesystem::rename(logPath, GetUserPath() / "marathonrecomp.old.log", ec);

    g_logFile = fopen(logPath.c_str(), "w");

    if (g_logFile != nullptr)
        setvbuf(g_logFile, nullptr, _IOLBF, 0);
#endif
}

void os::logger::Log(const std::string_view str, ELogType type, const char* func)
{
    if (func)
    {
        fmt::println("[{}] {}", func, str);
    }
    else
    {
        fmt::println("{}", str);
    }

#ifdef MARATHON_RECOMP_IOS
    std::lock_guard lock(g_logFileMutex);

    if (g_logFile != nullptr)
    {
        if (func)
            fmt::println(g_logFile, "[{}] {}", func, str);
        else
            fmt::println(g_logFile, "{}", str);
    }
#endif
}
