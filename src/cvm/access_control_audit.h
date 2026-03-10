// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ACCESS_CONTROL_AUDIT_H
#define CASCOIN_CVM_ACCESS_CONTROL_AUDIT_H

#include <uint256.h>
#include <serialize.h>
#include <sync.h>
#include <vector>
#include <map>
#include <deque>
#include <string>
#include <memory>
#include <atomic>

namespace CVM {

// Forward declarations
class CVMDatabase;
class SecurityAuditLogger;

/**
 * Access Control Operation Type
 */
enum class AccessOperationType {
    TRUST_SCORE_QUERY,              // Query trust score
    TRUST_SCORE_MODIFICATION,       // Modify trust score
    REPUTATION_GATED_CALL,          // Reputation-gated function call
    GAS_DISCOUNT_CLAIM,             // Claim gas discount based on reputation
    FREE_GAS_CLAIM,                 // Claim free gas allowance
    VALIDATOR_REGISTRATION,         // Register as validator
    VALIDATOR_RESPONSE,             // Submit validator response
    CONTRACT_DEPLOYMENT,            // Deploy contract
    CONTRACT_CALL,                  // Call contract function
    STORAGE_ACCESS,                 // Access contract storage
    CROSS_CHAIN_ATTESTATION,        // Cross-chain trust attestation
    DAO_VOTE,                       // DAO voting
    DISPUTE_CREATION,               // Create dispute
    DISPUTE_RESOLUTION              // Resolve dispute
};

/**
 * Access Control Decision
 */
enum class AccessDecision {
    GRANTED,                        // Access granted
    DENIED_INSUFFICIENT_REPUTATION, // Denied due to low reputation
    DENIED_RATE_LIMITED,            // Denied due to rate limiting
    DENIED_BLACKLISTED,             // Denied due to blacklist
    DENIED_INVALID_SIGNATURE,       // Denied due to invalid signature
    DENIED_INSUFFICIENT_STAKE,      // Denied due to insufficient stake
    DENIED_COOLDOWN,                // Denied due to cooldown period
    DENIED_OTHER                    // Denied for other reasons
};

/**
 * Access Control Audit Entry
 * 
 * Detailed record of an access control decision.
 */
struct AccessControlAuditEntry {
    uint64_t entryId;
    AccessOperationType operationType;
    AccessDecision decision;
    
    // Addresses involved
    uint160 requesterAddress;
    uint160 targetAddress;
    uint160 contractAddress;
    
    // Context
    std::string operationName;
    std::string resourceId;
    uint256 txHash;
    int blockHeight;
    int64_t timestamp;
    
    // Reputation data
    int16_t requiredReputation;
    int16_t actualReputation;
    int16_t reputationDeficit;
    
    // Additional context
    std::string denialReason;
    std::map<std::string, std::string> metadata;
    
    // Rate limiting info
    uint32_t requestsInWindow;
    uint32_t maxRequestsAllowed;
    int64_t windowStartTime;
    
    AccessControlAuditEntry() : entryId(0), operationType(AccessOperationType::TRUST_SCORE_QUERY),
                               decision(AccessDecision::GRANTED), blockHeight(0), timestamp(0),
                               requiredReputation(0), actualReputation(0), reputationDeficit(0),
                               requestsInWindow(0), maxRequestsAllowed(0), windowStartTime(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(entryId);
        uint8_t opType = static_cast<uint8_t>(operationType);
        READWRITE(opType);
        operationType = static_cast<AccessOperationType>(opType);
        uint8_t dec = static_cast<uint8_t>(decision);
        READWRITE(dec);
        decision = static_cast<AccessDecision>(dec);
        READWRITE(requesterAddress);
        READWRITE(targetAddress);
        READWRITE(contractAddress);
        READWRITE(operationName);
        READWRITE(resourceId);
        READWRITE(txHash);
        READWRITE(blockHeight);
        READWRITE(timestamp);
        READWRITE(requiredReputation);
        READWRITE(actualReputation);
        READWRITE(reputationDeficit);
        READWRITE(denialReason);
        READWRITE(metadata);
        READWRITE(requestsInWindow);
        READWRITE(maxRequestsAllowed);
        READWRITE(windowStartTime);
    }
    
    std::string GetOperationTypeString() const;
    std::string GetDecisionString() const;
};

/**
 * Rate Limit State
 */
struct RateLimitState {
    uint160 address;
    AccessOperationType operationType;
    uint32_t requestCount;
    int64_t windowStart;
    int64_t lastRequest;
    
    RateLimitState() : operationType(AccessOperationType::TRUST_SCORE_QUERY),
                      requestCount(0), windowStart(0), lastRequest(0) {}
};

/**
 * Access Control Statistics
 */
struct AccessControlStats {
    // Per-operation type stats
    std::map<AccessOperationType, uint64_t> totalRequests;
    std::map<AccessOperationType, uint64_t> grantedRequests;
    std::map<AccessOperationType, uint64_t> deniedRequests;
    
    // Per-decision type stats
    std::map<AccessDecision, uint64_t> decisionCounts;
    
    // Time-based stats
    int64_t windowStart;
    int64_t windowEnd;
    int startBlockHeight;
    int endBlockHeight;
    
    // Aggregate stats
    uint64_t totalAccessAttempts;
    uint64_t totalGranted;
    uint64_t totalDenied;
    double overallGrantRate;
    double averageReputationDeficit;
    
    AccessControlStats() : windowStart(0), windowEnd(0), startBlockHeight(0),
                          endBlockHeight(0), totalAccessAttempts(0), totalGranted(0),
                          totalDenied(0), overallGrantRate(0), averageReputationDeficit(0) {}
    
    void CalculateRates() {
        if (totalAccessAttempts > 0) {
            overallGrantRate = (double)totalGranted / totalAccessAttempts;
        }
    }
};

/**
 * Access Control Auditor
 * 
 * Comprehensive access control auditing for the CVM system.
 * Implements requirement 10.4:
 * - Log all trust score queries and modifications
 * - Record all reputation-gated operation attempts
 */
class AccessControlAuditor {
public:
    explicit AccessControlAuditor(CVMDatabase& db, SecurityAuditLogger* auditLogger = nullptr);
    ~AccessControlAuditor();
    
    /**
     * Initialize the access control auditor
     */
    bool Initialize(int currentBlockHeight);
    
    /**
     * Update current block height
     */
    void SetBlockHeight(int height) { m_currentBlockHeight = height; }
    
    // ========== Access Control Logging ==========
    
    /**
     * Log a trust score query
     */
    void LogTrustScoreQuery(const uint160& requester, const uint160& target,
                           int16_t score, const std::string& context = "");
    
    /**
     * Log a trust score modification
     */
    void LogTrustScoreModification(const uint160& modifier, const uint160& target,
                                  int16_t oldScore, int16_t newScore,
                                  const std::string& reason);
    
    /**
     * Log a reputation-gated operation attempt
     */
    AccessDecision LogReputationGatedOperation(
        const uint160& requester,
        AccessOperationType operationType,
        const std::string& operationName,
        int16_t requiredReputation,
        int16_t actualReputation,
        const std::string& resourceId = "",
        const uint256& txHash = uint256());
    
    /**
     * Log a generic access control decision
     */
    void LogAccessDecision(const AccessControlAuditEntry& entry);
    
    // ========== Rate Limiting ==========
    
    /**
     * Check and update rate limit for an address
     */
    bool CheckRateLimit(const uint160& address, AccessOperationType operationType,
                       uint32_t& requestsInWindow, uint32_t& maxAllowed);
    
    /**
     * Set rate limit configuration
     */
    void SetRateLimit(AccessOperationType operationType, uint32_t maxRequests,
                     int64_t windowSeconds);
    
    /**
     * Get rate limit state for an address
     */
    RateLimitState GetRateLimitState(const uint160& address, AccessOperationType operationType);
    
    // ========== Blacklist Management ==========
    
    /**
     * Add address to blacklist
     */
    void AddToBlacklist(const uint160& address, const std::string& reason,
                       int64_t duration = -1);  // -1 = permanent
    
    /**
     * Remove address from blacklist
     */
    void RemoveFromBlacklist(const uint160& address);
    
    /**
     * Check if address is blacklisted
     */
    bool IsBlacklisted(const uint160& address);
    
    /**
     * Get blacklist entries
     */
    std::vector<std::pair<uint160, std::string>> GetBlacklistEntries();
    
    // ========== Statistics and Reporting ==========
    
    /**
     * Get access control statistics
     */
    AccessControlStats GetStatistics();
    
    /**
     * Get statistics for a specific time window
     */
    AccessControlStats GetStatisticsForWindow(int64_t startTime, int64_t endTime);
    
    /**
     * Get statistics for a specific block range
     */
    AccessControlStats GetStatisticsForBlockRange(int startBlock, int endBlock);
    
    /**
     * Get recent audit entries
     */
    std::vector<AccessControlAuditEntry> GetRecentEntries(size_t count = 100);
    
    /**
     * Get entries for a specific address
     */
    std::vector<AccessControlAuditEntry> GetEntriesForAddress(const uint160& address,
                                                              size_t count = 100);
    
    /**
     * Get entries by operation type
     */
    std::vector<AccessControlAuditEntry> GetEntriesByOperationType(
        AccessOperationType operationType, size_t count = 100);
    
    /**
     * Get denied entries
     */
    std::vector<AccessControlAuditEntry> GetDeniedEntries(size_t count = 100);
    
    // ========== Configuration ==========
    
    /**
     * Set minimum reputation requirements for operations
     */
    void SetMinimumReputation(AccessOperationType operationType, int16_t minReputation);
    
    /**
     * Get minimum reputation requirement
     */
    int16_t GetMinimumReputation(AccessOperationType operationType);
    
    /**
     * Enable/disable logging for specific operation types
     */
    void EnableLogging(AccessOperationType operationType, bool enabled);
    
    /**
     * Set maximum entries to keep in memory
     */
    void SetMaxEntriesInMemory(size_t maxEntries);
    
private:
    CVMDatabase& m_db;
    SecurityAuditLogger* m_auditLogger;
    mutable CCriticalSection m_cs;
    
    // State
    int m_currentBlockHeight;
    std::atomic<uint64_t> m_nextEntryId;
    
    // Audit entries
    std::deque<AccessControlAuditEntry> m_recentEntries;
    size_t m_maxEntriesInMemory;
    
    // Rate limiting
    std::map<std::pair<uint160, AccessOperationType>, RateLimitState> m_rateLimitStates;
    std::map<AccessOperationType, std::pair<uint32_t, int64_t>> m_rateLimitConfigs;  // max requests, window seconds
    
    // Blacklist
    std::map<uint160, std::pair<std::string, int64_t>> m_blacklist;  // address -> (reason, expiry)
    
    // Configuration
    std::map<AccessOperationType, int16_t> m_minReputationRequirements;
    std::map<AccessOperationType, bool> m_loggingEnabled;
    
    // Statistics
    AccessControlStats m_currentStats;
    
    // Internal methods
    void AddEntry(const AccessControlAuditEntry& entry);
    void PersistEntry(const AccessControlAuditEntry& entry);
    void UpdateStatistics(const AccessControlAuditEntry& entry);
    int64_t GetCurrentTimestamp();
    void CleanupExpiredBlacklistEntries();
    void LoadBlacklist();
};

/**
 * Global access control auditor instance
 */
extern std::unique_ptr<AccessControlAuditor> g_accessControlAuditor;

/**
 * Initialize access control auditor
 */
bool InitAccessControlAuditor(CVMDatabase& db, SecurityAuditLogger* auditLogger, int currentBlockHeight);

/**
 * Shutdown access control auditor
 */
void ShutdownAccessControlAuditor();

} // namespace CVM

#endif // CASCOIN_CVM_ACCESS_CONTROL_AUDIT_H
