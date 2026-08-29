// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_RESOURCE_MANAGER_H
#define CASCOIN_CVM_RESOURCE_MANAGER_H

#include <uint256.h>
#include <primitives/transaction.h>
#include <cvm/trust_context.h>
#include <cvm/tx_priority.h>
#include <univalue.h>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>

namespace CVM {

class CVMDatabase;
class EnhancedStorage;

/**
 * Resource Manager
 * 
 * Manages reputation-based resource allocation across the CVM system:
 * - Execution priority based on caller reputation
 * - Storage quotas (delegated to EnhancedStorage)
 * - Transaction ordering in mempool
 * - Rate limiting for API calls
 * - RPC server integration
 */
class ResourceManager {
public:
    /**
     * Execution priority information
     */
    struct ExecutionPriority {
        uint8_t reputation;
        int priority;  // Higher = more priority (0-100)
        bool canPreempt;  // Can preempt lower priority executions
        uint64_t maxExecutionTime;  // Max execution time in milliseconds
        
        ExecutionPriority()
            : reputation(0), priority(0), canPreempt(false), maxExecutionTime(1000) {}
    };
    
    /**
     * Rate limit information for API calls
     */
    struct RateLimitInfo {
        uint160 address;
        uint8_t reputation;
        uint32_t callsPerMinute;  // Allowed calls per minute
        uint32_t currentCalls;    // Current calls in window
        int64_t windowStart;      // Start of current time window
        bool isThrottled;         // Currently throttled
        
        RateLimitInfo()
            : reputation(0), callsPerMinute(0), currentCalls(0), 
              windowStart(0), isThrottled(false) {}
    };
    
    ResourceManager();
    ~ResourceManager();
    
    /**
     * Initialize resource manager with database
     * 
     * @param db CVM database for reputation lookups
     * @param storage Enhanced storage for quota management
     */
    void Initialize(CVMDatabase* db, EnhancedStorage* storage);
    
    // ===== Execution Priority Management =====
    
    /**
     * Get execution priority for a contract call
     * 
     * @param caller Caller address
     * @param trust Trust context
     * @return Execution priority information
     */
    ExecutionPriority GetExecutionPriority(
        const uint160& caller,
        const TrustContext& trust
    );
    
    /**
     * Calculate priority score from reputation
     * 
     * @param reputation Reputation score (0-100)
     * @return Priority score (0-100)
     */
    static int CalculatePriorityScore(uint8_t reputation);
    
    /**
     * Check if execution can preempt others
     * 
     * @param reputation Reputation score
     * @return true if can preempt (90+ reputation)
     */
    static bool CanPreemptExecution(uint8_t reputation);
    
    /**
     * Get maximum execution time based on reputation
     * 
     * @param reputation Reputation score
     * @return Max execution time in milliseconds
     */
    static uint64_t GetMaxExecutionTime(uint8_t reputation);
    
    // ===== Transaction Ordering =====
    
    /**
     * Get transaction priority for mempool ordering
     * 
     * @param tx Transaction
     * @return Transaction priority information
     */
    TransactionPriorityManager::TransactionPriority GetTransactionPriority(
        const CTransaction& tx
    );
    
    /**
     * Compare two transactions for ordering
     * 
     * @param a First transaction
     * @param b Second transaction
     * @return true if a should be ordered before b
     */
    bool CompareTransactions(
        const CTransaction& a,
        const CTransaction& b
    );
    
    // ===== Rate Limiting =====
    
    /**
     * Check if API call is allowed (rate limiting)
     * 
     * @param address Caller address
     * @param method RPC method name
     * @return true if call is allowed
     */
    bool CheckRateLimit(const uint160& address, const std::string& method);
    
    /**
     * Record an API call for rate limiting
     * 
     * @param address Caller address
     * @param method RPC method name
     */
    void RecordAPICall(const uint160& address, const std::string& method);
    
    /**
     * Get rate limit for address based on reputation
     * 
     * @param address Address to check
     * @return Rate limit information
     */
    RateLimitInfo GetRateLimitInfo(const uint160& address);
    
    /**
     * Calculate calls per minute allowed based on reputation
     * 
     * @param reputation Reputation score (0-100)
     * @return Calls per minute allowed
     */
    static uint32_t CalculateRateLimit(uint8_t reputation);
    
    /**
     * Reset rate limit window (called periodically)
     */
    void ResetRateLimitWindows();
    
    // ===== Storage Quota Management (delegated to EnhancedStorage) =====
    
    /**
     * Get storage quota for address
     * 
     * @param address Address to check
     * @param reputation Reputation score
     * @return Storage quota in bytes
     */
    uint64_t GetStorageQuota(const uint160& address, uint8_t reputation);
    
    /**
     * Check if storage operation is within quota
     * 
     * @param address Address to check
     * @param requestedSize Requested storage size
     * @return true if within quota
     */
    bool CheckStorageQuota(const uint160& address, uint64_t requestedSize);
    
    // ===== Statistics and Monitoring =====
    
    /**
     * Get resource usage statistics for address
     * 
     * @param address Address to check
     * @return JSON object with statistics
     */
    UniValue GetResourceStats(const uint160& address);
    
    /**
     * Get global resource statistics
     * 
     * @return JSON object with global statistics
     */
    UniValue GetGlobalResourceStats();
    
private:
    CVMDatabase* m_db;
    EnhancedStorage* m_storage;
    std::unique_ptr<TransactionPriorityManager> m_txPriorityManager;
    
    // Rate limiting state
    std::map<uint160, RateLimitInfo> m_rateLimits;
    std::mutex m_rateLimitMutex;
    
    // Statistics
    std::map<uint160, uint64_t> m_executionCounts;
    std::map<uint160, uint64_t> m_totalExecutionTime;
    std::mutex m_statsMutex;
    
    /**
     * Get reputation score for address
     * 
     * @param address Address to check
     * @return Reputation score (0-100)
     */
    uint8_t GetReputation(const uint160& address);
    
    /**
     * Update rate limit window if needed
     * 
     * @param info Rate limit info to update
     * @param currentTime Current timestamp
     */
    void UpdateRateLimitWindow(RateLimitInfo& info, int64_t currentTime);
};

} // namespace CVM

#endif // CASCOIN_CVM_RESOURCE_MANAGER_H
