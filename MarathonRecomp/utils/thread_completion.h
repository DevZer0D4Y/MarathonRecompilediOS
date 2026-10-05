#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

class ThreadCompletion
{
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
public:
    void Finish()
    {
        {
            std::lock_guard lock(mutex);
            finished = true;
        }
        condition.notify_all();
    }

    bool Wait(uint32_t timeout)
    {
        std::unique_lock lock(mutex);
        if (timeout == UINT32_MAX)
        {
            condition.wait(lock, [&] { return finished; });
            return true;
        }
        return condition.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return finished; });
    }
};
