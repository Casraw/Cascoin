// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/cpumanager.h>
#include <util.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

void CPUManager::SetThreadPriority(int priority)
{
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    switch (priority) {
        case 0: // Lowest
            ::SetThreadPriority(thread, THREAD_PRIORITY_IDLE);
            break;
        case 1: // Below normal
            ::SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
            break;
        case 2: // Normal
        default:
            ::SetThreadPriority(thread, THREAD_PRIORITY_NORMAL);
            break;
    }
#else
    pthread_t thread = pthread_self();
    int policy;
    struct sched_param param;
    
    if (pthread_getschedparam(thread, &policy, &param) == 0) {
        switch (priority) {
            case 0: // Lowest
                param.sched_priority = std::max(param.sched_priority - 2, 1);
                break;
            case 1: // Below normal
                param.sched_priority = std::max(param.sched_priority - 1, 1);
                break;
            case 2: // Normal
            default:
                // Keep current priority
                break;
        }
        pthread_setschedparam(thread, policy, &param);
    }
#endif
}

void CPUManager::YieldToGUI(int yieldDurationMs)
{
    if (yieldDurationMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(yieldDurationMs));
    } else {
        std::this_thread::yield();
    }
}

int CPUManager::GetRecommendedWorkerThreads(int reservedCores)
{
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    int coreCount = sysinfo.dwNumberOfProcessors;
#else
    int coreCount = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    
    // Reserve cores for GUI and system responsiveness
    int maxWorkerThreads = std::max(1, coreCount - reservedCores);
    
    // Allow override via command line argument
    int requestedThreads = gArgs.GetArg("-hivecheckthreads", maxWorkerThreads);
    
    if (requestedThreads == -2) {
        return maxWorkerThreads;
    } else if (requestedThreads < 0 || requestedThreads > coreCount) {
        return maxWorkerThreads;
    } else if (requestedThreads == 0) {
        return 1;
    }
    
    return requestedThreads;
}