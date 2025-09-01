// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CPUMANAGER_H
#define BITCOIN_QT_CPUMANAGER_H

#include <thread>
#include <chrono>

/**
 * CPU Management utilities to prevent GUI freezing
 */
class CPUManager
{
public:
    /**
     * Set thread priority to preserve GUI responsiveness
     * @param priority Lower values = lower priority (0 = lowest, 1 = below normal, 2 = normal)
     */
    static void SetThreadPriority(int priority);
    
    /**
     * Yield CPU time to other threads, especially GUI thread
     * @param yieldDurationMs How long to yield in milliseconds
     */
    static void YieldToGUI(int yieldDurationMs = 1);
    
    /**
     * Get recommended number of worker threads, reserving cores for GUI
     * @param reservedCores Number of cores to reserve for GUI (default 1)
     */
    static int GetRecommendedWorkerThreads(int reservedCores = 1);
    
private:
    CPUManager() = default;
};

#endif // BITCOIN_QT_CPUMANAGER_H