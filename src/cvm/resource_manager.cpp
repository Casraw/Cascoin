// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/resource_manager.h>
#include <cvm/cvmdb.h>
#include <cvm/enhanced_storage.h>
#include <cvm/reputation.h>
#include <util.h>
#include <utiltime.h>
#include <univalue.h>

namespace CVM {

ResourceManager::ResourceManager()
    : m_db(nullptr)
    , m_storage(nullptr)
    , m_txPriorityManager(std::make_unique<TransactionPriorityManager>())
{
}

ResourceManager::~ResourceManager()
{
}

void ResourceManager::Initialize(CVMDatabase* db, EnhancedStorage* storage)
{
    m_db = db;
    m_storage = storage;
}

// ===== Execution Priority Management =====

ResourceManager::ExecutionPriority ResourceManager::GetExecutionPriority(
    const uint160& caller,
    const TrustContext& trust)
{
    ExecutionPriority priority;
    uint8_t reputation = trust.GetCallerReputation();
    priority.reputation = reputation;
    priority.priority = CalculatePriorityScore(reputation);
    priority.canPreempt = CanPreemptExecution(reputation);
    priority.maxExecutionTime = GetMaxExecutionTime(reputation);
    
    return priority;
}

int ResourceManager::CalculatePriorityScore(uint8_t reputation)
{
    // Priority score scales with reputation
    // 0-49 reputation: 0-49 priority (low)
    // 50-69 reputation: 50-69 priority (normal)
    // 70-89 reputation: 70-89 priority (high)
    // 90-100 reputation: 90-100 priority (critical)
    
    return static_cast<int>(reputation);
}

bool ResourceManager::CanPreemptExecution(uint8_t reputation)
{
    // Only critical priority (90+) can preempt
    return reputation >= 90;
}

uint64_t ResourceManager::GetMaxExecutionTime(uint8_t reputation)
{
    // Execution time limits based on reputation
    // Low reputation: 500ms
    // Normal reputation: 1000ms
    // High reputation: 2000ms
    // Critical reputation: 5000ms
    
    if (reputation >= 90) {
        return 5000;  // 5 seconds for critical
    } else if (reputation >= 70) {
        return 2000;  // 2 seconds for high
    } else if (reputation >= 50) {
        return 1000;  // 1 second for normal
    } else {
        return 500;   // 500ms for low
    }
}

// ===== Transaction Ordering =====

TransactionPriorityManager::TransactionPriority ResourceManager::GetTransactionPriority(
    const CTransaction& tx)
{
    if (!m_db) {
        TransactionPriorityManager::TransactionPriority priority;
        priority.txid = tx.GetHash();
        priority.reputation = 0;
        priority.level = TransactionPriorityManager::PriorityLevel::LOW;
        priority.timestamp = GetTime();
        priority.guaranteedInclusion = false;
        return priority;
    }
    
    return m_txPriorityManager->CalculatePriority(tx, *m_db);
}

bool ResourceManager::CompareTransactions(
    const CTransaction& a,
    const CTransaction& b)
{
    auto priorityA = GetTransactionPriority(a);
    auto priorityB = GetTransactionPriority(b);
    
    return TransactionPriorityManager::CompareTransactionPriority(priorityA, priorityB);
}

// ===== Rate Limiting =====

bool ResourceManager::CheckRateLimit(const uint160& address, const std::string& method)
{
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    int64_t currentTime = GetTime();
    
    // Get or create rate limit info
    auto it = m_rateLimits.find(address);
    if (it == m_rateLimits.end()) {
        RateLimitInfo info;
        info.address = address;
        info.reputation = GetReputation(address);
        info.callsPerMinute = CalculateRateLimit(info.reputation);
        info.currentCalls = 0;
        info.windowStart = currentTime;
        info.isThrottled = false;
        
        m_rateLimits[address] = info;
        it = m_rateLimits.find(address);
    }
    
    RateLimitInfo& info = it->second;
    
    // Update window if needed
    UpdateRateLimitWindow(info, currentTime);
    
    // Check if throttled
    if (info.currentCalls >= info.callsPerMinute) {
        info.isThrottled = true;
        return false;
    }
    
    info.isThrottled = false;
    return true;
}

void ResourceManager::RecordAPICall(const uint160& address, const std::string& method)
{
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    int64_t currentTime = GetTime();
    
    auto it = m_rateLimits.find(address);
    if (it == m_rateLimits.end()) {
        RateLimitInfo info;
        info.address = address;
        info.reputation = GetReputation(address);
        info.callsPerMinute = CalculateRateLimit(info.reputation);
        info.currentCalls = 1;
        info.windowStart = currentTime;
        info.isThrottled = false;
        
        m_rateLimits[address] = info;
    } else {
        RateLimitInfo& info = it->second;
        UpdateRateLimitWindow(info, currentTime);
        info.currentCalls++;
    }
}

ResourceManager::RateLimitInfo ResourceManager::GetRateLimitInfo(const uint160& address)
{
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    auto it = m_rateLimits.find(address);
    if (it != m_rateLimits.end()) {
        return it->second;
    }
    
    // Create new info
    RateLimitInfo info;
    info.address = address;
    info.reputation = GetReputation(address);
    info.callsPerMinute = CalculateRateLimit(info.reputation);
    info.currentCalls = 0;
    info.windowStart = GetTime();
    info.isThrottled = false;
    
    return info;
}

uint32_t ResourceManager::CalculateRateLimit(uint8_t reputation)
{
    // Rate limits based on reputation:
    // 0-49: 10 calls/minute (low reputation)
    // 50-69: 60 calls/minute (normal reputation)
    // 70-89: 300 calls/minute (high reputation)
    // 90-100: 1000 calls/minute (critical reputation)
    
    if (reputation >= 90) {
        return 1000;  // Critical: 1000 calls/minute
    } else if (reputation >= 70) {
        return 300;   // High: 300 calls/minute
    } else if (reputation >= 50) {
        return 60;    // Normal: 60 calls/minute (1 per second)
    } else {
        return 10;    // Low: 10 calls/minute
    }
}

void ResourceManager::ResetRateLimitWindows()
{
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    int64_t currentTime = GetTime();
    
    for (auto& pair : m_rateLimits) {
        UpdateRateLimitWindow(pair.second, currentTime);
    }
}

void ResourceManager::UpdateRateLimitWindow(RateLimitInfo& info, int64_t currentTime)
{
    // Reset window every 60 seconds
    if (currentTime - info.windowStart >= 60) {
        info.currentCalls = 0;
        info.windowStart = currentTime;
        info.isThrottled = false;
        
        // Update reputation and rate limit
        info.reputation = GetReputation(info.address);
        info.callsPerMinute = CalculateRateLimit(info.reputation);
    }
}

// ===== Storage Quota Management =====

uint64_t ResourceManager::GetStorageQuota(const uint160& address, uint8_t reputation)
{
    if (!m_storage) {
        return 0;
    }
    
    return m_storage->GetStorageQuota(address, reputation);
}

bool ResourceManager::CheckStorageQuota(const uint160& address, uint64_t requestedSize)
{
    if (!m_storage) {
        return false;
    }
    
    return m_storage->CheckStorageLimit(address, requestedSize);
}

// ===== Statistics and Monitoring =====

UniValue ResourceManager::GetResourceStats(const uint160& address)
{
    UniValue result(UniValue::VOBJ);
    
    // Get reputation
    uint8_t reputation = GetReputation(address);
    result.pushKV("reputation", static_cast<int>(reputation));
    
    // Execution priority
    TrustContext trust;
    trust.SetCallerReputation(reputation);
    auto execPriority = GetExecutionPriority(address, trust);
    
    UniValue execInfo(UniValue::VOBJ);
    execInfo.pushKV("priority", execPriority.priority);
    execInfo.pushKV("can_preempt", execPriority.canPreempt);
    execInfo.pushKV("max_execution_time_ms", static_cast<uint64_t>(execPriority.maxExecutionTime));
    result.pushKV("execution_priority", execInfo);
    
    // Rate limiting
    auto rateLimitInfo = GetRateLimitInfo(address);
    UniValue rateInfo(UniValue::VOBJ);
    rateInfo.pushKV("calls_per_minute", static_cast<int>(rateLimitInfo.callsPerMinute));
    rateInfo.pushKV("current_calls", static_cast<int>(rateLimitInfo.currentCalls));
    rateInfo.pushKV("is_throttled", rateLimitInfo.isThrottled);
    result.pushKV("rate_limit", rateInfo);
    
    // Storage quota
    if (m_storage) {
        uint64_t quota = m_storage->GetStorageQuota(address, reputation);
        uint64_t usage = m_storage->GetCurrentStorageUsage(address);
        
        UniValue storageInfo(UniValue::VOBJ);
        storageInfo.pushKV("quota_bytes", quota);
        storageInfo.pushKV("usage_bytes", usage);
        storageInfo.pushKV("available_bytes", quota > usage ? quota - usage : 0);
        storageInfo.pushKV("usage_percent", quota > 0 ? (usage * 100.0 / quota) : 0.0);
        result.pushKV("storage", storageInfo);
    }
    
    // Execution statistics
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        
        auto execCountIt = m_executionCounts.find(address);
        auto execTimeIt = m_totalExecutionTime.find(address);
        
        UniValue statsInfo(UniValue::VOBJ);
        statsInfo.pushKV("total_executions", execCountIt != m_executionCounts.end() ? execCountIt->second : 0);
        statsInfo.pushKV("total_execution_time_ms", execTimeIt != m_totalExecutionTime.end() ? execTimeIt->second : 0);
        
        if (execCountIt != m_executionCounts.end() && execCountIt->second > 0 && execTimeIt != m_totalExecutionTime.end()) {
            statsInfo.pushKV("avg_execution_time_ms", execTimeIt->second / execCountIt->second);
        } else {
            statsInfo.pushKV("avg_execution_time_ms", 0);
        }
        
        result.pushKV("statistics", statsInfo);
    }
    
    return result;
}

UniValue ResourceManager::GetGlobalResourceStats()
{
    UniValue result(UniValue::VOBJ);
    
    // Rate limiting stats
    {
        std::lock_guard<std::mutex> lock(m_rateLimitMutex);
        
        int totalAddresses = m_rateLimits.size();
        int throttledAddresses = 0;
        uint64_t totalCalls = 0;
        
        for (const auto& pair : m_rateLimits) {
            if (pair.second.isThrottled) {
                throttledAddresses++;
            }
            totalCalls += pair.second.currentCalls;
        }
        
        UniValue rateInfo(UniValue::VOBJ);
        rateInfo.pushKV("total_addresses", totalAddresses);
        rateInfo.pushKV("throttled_addresses", throttledAddresses);
        rateInfo.pushKV("total_calls_current_window", totalCalls);
        result.pushKV("rate_limiting", rateInfo);
    }
    
    // Execution stats
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        
        uint64_t totalExecutions = 0;
        uint64_t totalTime = 0;
        
        for (const auto& pair : m_executionCounts) {
            totalExecutions += pair.second;
        }
        
        for (const auto& pair : m_totalExecutionTime) {
            totalTime += pair.second;
        }
        
        UniValue execInfo(UniValue::VOBJ);
        execInfo.pushKV("total_executions", totalExecutions);
        execInfo.pushKV("total_execution_time_ms", totalTime);
        execInfo.pushKV("avg_execution_time_ms", totalExecutions > 0 ? totalTime / totalExecutions : 0);
        result.pushKV("execution", execInfo);
    }
    
    return result;
}

// ===== Private Methods =====

uint8_t ResourceManager::GetReputation(const uint160& address)
{
    if (!m_db) {
        return 0;
    }
    
    // Read reputation from generic storage
    std::string key = "reputation_" + address.ToString();
    std::vector<uint8_t> data;
    
    if (m_db->ReadGeneric(key, data) && data.size() >= sizeof(int64_t)) {
        // Deserialize reputation score
        int64_t score = 0;
        memcpy(&score, data.data(), sizeof(int64_t));
        
        // Convert to 0-100 scale (assuming score is -10000 to +10000)
        // Map: -10000 -> 0, 0 -> 50, +10000 -> 100
        int normalized = 50 + (score / 200);
        if (normalized < 0) normalized = 0;
        if (normalized > 100) normalized = 100;
        
        return static_cast<uint8_t>(normalized);
    }
    
    return 50;  // Default neutral reputation
}

} // namespace CVM
