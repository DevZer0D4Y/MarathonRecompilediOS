#include "sampling_profiler.h"
#include <os/logger.h>

#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <pthread.h>

namespace
{
    struct ThreadSamples
    {
        std::string name;
        uint32_t total = 0;
        std::unordered_map<uint64_t, uint32_t> pcs;
    };
}

static void SamplingProfilerThread()
{
    pthread_setname_np("Sampling Profiler");

    const uint64_t imageBase = uint64_t(_dyld_get_image_header(0));
    const mach_port_t selfThread = mach_thread_self();
    std::unordered_map<uint64_t, ThreadSamples> samples;
    auto lastReport = std::chrono::steady_clock::now();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t threadCount = 0;
        if (task_threads(mach_task_self(), &threads, &threadCount) != KERN_SUCCESS)
            continue;

        for (mach_msg_type_number_t i = 0; i < threadCount; i++)
        {
            if (threads[i] != selfThread && thread_suspend(threads[i]) == KERN_SUCCESS)
            {
                arm_thread_state64_t state{};
                mach_msg_type_number_t stateCount = ARM_THREAD_STATE64_COUNT;
                thread_basic_info_data_t basicInfo{};
                mach_msg_type_number_t basicCount = THREAD_BASIC_INFO_COUNT;

                bool gotState = thread_get_state(threads[i], ARM_THREAD_STATE64, (thread_state_t)&state, &stateCount) == KERN_SUCCESS;
                bool gotInfo = thread_info(threads[i], THREAD_BASIC_INFO, (thread_info_t)&basicInfo, &basicCount) == KERN_SUCCESS;
                thread_resume(threads[i]);

                // Only count threads that are running, not ones blocked in the kernel.
                if (gotState && gotInfo && basicInfo.run_state == TH_STATE_RUNNING)
                {
                    thread_identifier_info_data_t identifierInfo{};
                    mach_msg_type_number_t identifierCount = THREAD_IDENTIFIER_INFO_COUNT;
                    thread_info(threads[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&identifierInfo, &identifierCount);

                    auto& thread = samples[identifierInfo.thread_id];
                    if (thread.name.empty())
                    {
                        char name[64]{};
                        if (pthread_t pthread = pthread_from_mach_thread_np(threads[i]))
                            pthread_getname_np(pthread, name, sizeof(name));

                        thread.name = name[0] != '\0' ? name : fmt::format("{:x}", identifierInfo.thread_id);
                    }

                    thread.total++;
                    thread.pcs[arm_thread_state64_get_pc(state) - imageBase]++;
                }
            }

            mach_port_deallocate(mach_task_self(), threads[i]);
        }

        vm_deallocate(mach_task_self(), (vm_address_t)threads, threadCount * sizeof(thread_act_t));

        if (std::chrono::steady_clock::now() - lastReport < std::chrono::seconds(10))
            continue;

        lastReport = std::chrono::steady_clock::now();

        std::vector<ThreadSamples*> sorted;
        for (auto& [id, thread] : samples)
            sorted.push_back(&thread);

        std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a->total > b->total; });

        for (size_t i = 0; i < sorted.size() && i < 4; i++)
        {
            std::vector<std::pair<uint32_t, uint64_t>> pcs;
            for (auto& [pc, count] : sorted[i]->pcs)
                pcs.emplace_back(count, pc);

            std::sort(pcs.begin(), pcs.end(), std::greater<>());

            std::string text = fmt::format("PROFILE {} samples={}", sorted[i]->name, sorted[i]->total);
            for (size_t j = 0; j < pcs.size() && j < 12; j++)
                text += fmt::format(" {:x}:{}", pcs[j].second, pcs[j].first);

            LOGN(text);
        }

        samples.clear();
    }
}

void ios::StartSamplingProfilerIfRequested()
{
    if (getenv("MARATHON_RECOMP_PROFILE") != nullptr)
        std::thread(SamplingProfilerThread).detach();
}
