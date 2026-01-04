// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_DOS_PROTECTION_H
#define CASCOIN_CVM_DOS_PROTECTION_H

#include <uint256.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <net.h>
#include <sync.h>
#include <univalue.h>
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <atomic>
#include <chrono>

namespace CVM {

// Forward declarations
class CVMDatabase;
class BytecodeDetector;

/**
 * Rate Limit Configuration
 * 
 * Configurable rate limits based on reputation levels.
 */
struct RateLimitConfig {
    // Transaction rate limits (per minute)
    uint32_t lowRepTxPerMinute;         // Reputation < 50
    uint32_t normalRepTxPerMinute;      // Reputation 50-69
    uint32_t highRepTxPerMinute;        // Reputation 70-89
    uint32_t criticalRepTxPerMinute;    // Reputation 90+
    
    // Contract deployment limits (per hour)
    uint32_t lowRepDeploysPerHour;
    uint32_t normalRepDeploysPerHour;
    uint32_t highRepDeploysPerHour;
    uint32_t criticalRepDeploysPerHour;
    
    // Validation request limits (per minute)
    uint32_t validationRequestsPerMinute;
    
    // RPC call limits (per minute)
    uint32_t lowRepRPCPerMinute;
    uint32_t normalRepRPCPerMinute;
    uint32_t highRepRPCPerMinute;
    uint32_t criticalRepRPCPerMinute;
    
    // P2P message limits
    uint64_t maxBandwidthPerPeer;       // Bytes per second
    uint32_t maxMessagesPerMinute;
    
    // Timeouts
    uint32_t validatorResponseTimeout;  // Seconds
    
    RateLimitConfig();
    static RateLimitConfig Default();
};

/**
 * Rate Limit Entry
 * 
 * Tracks rate limiting state for an address.
 */
struct RateLimitEntry {
    uint160 address;
    std::deque<int64_t> txTimestamps;
    std::deque<int64_t> deployTimestamps;
    std::deque<int64_t> rpcTimestamps;
    uint32_t violationCount;
    int64_t lastViolationTime;
    int64_t banUntil;
    
    RateLimitEntry() : violationCount(0), lastViolationTime(0), banUntil(0) {}
};

/**
 * Malicious Pattern
 * 
 * Known malicious bytecode patterns.
 */
struct MaliciousPattern {
    std::string name;
    std::string description;
    std::vector<uint8_t> pattern;
    double severity;                    // 0.0-1.0
    bool blockDeploy;                   // Should deployment be blocked?
    
    MaliciousPattern() : severity(0), blockDeploy(false) {}
};

/**
 * Bytecode Analysis Result
 * 
 * Result of malicious contract detection.
 */
struct BytecodeAnalysisResult {
    bool isMalicious;
    bool hasInfiniteLoop;
    bool hasResourceExhaustion;
    bool hasReentrancy;
    bool hasSelfDestruct;
    bool hasUnboundedLoop;
    double riskScore;                   // 0.0-1.0
    std::vector<std::string> detectedPatterns;
    std::string analysisReport;
    
    BytecodeAnalysisResult() 
        : isMalicious(false), hasInfiniteLoop(false), hasResourceExhaustion(false),
          hasReentrancy(false), hasSelfDestruct(false), hasUnboundedLoop(false),
          riskScore(0) {}
};

/**
 * Validator Request Entry
 * 
 * Tracks validation requests for rate limiting.
 */
struct ValidatorRequestEntry {
    uint160 validatorAddress;
    std::deque<int64_t> requestTimestamps;
    std::map<uint256, int64_t> pendingResponses;  // txHash -> deadline
    uint32_t timeoutCount;
    int64_t lastTimeoutTime;
    
    ValidatorRequestEntry() : timeoutCount(0), lastTimeoutTime(0) {}
};

/**
 * P2P Message Stats
 * 
 * Tracks P2P message statistics for bandwidth limiting.
 */
struct P2PMessageStats {
    uint64_t bytesReceived;
    uint64_t bytesSent;
    uint32_t messagesReceived;
    uint32_t messagesSent;
    int64_t windowStart;
    
    P2PMessageStats() : bytesReceived(0), bytesSent(0), 
                       messagesReceived(0), messagesSent(0), windowStart(0) {}
};

/**
 * DoS Protection Manager
 * 
 * Comprehensive DoS protection for the CVM system including:
 * - Transaction flooding protection
 * - Malicious contract detection
 * - Validator DoS protection
 * - Network resource protection
 */
class DoSProtectionManager {
public:
    DoSProtectionManager();
    ~DoSProtectionManager();
    
    /**
     * Initialize with database and configuration
     */
    void Initialize(CVMDatabase* db, const RateLimitConfig& config = RateLimitConfig::Default());
    
    // ===== Transaction Flooding Protection (26.1) =====
    
    /**
     * Check if transaction should be rate limited
     * 
     * @param tx Transaction to check
     * @param senderAddr Sender address
     * @param reputation Sender reputation (0-100)
     * @return true if transaction should be rejected
     */
    bool IsTransactionRateLimited(
        const CTransaction& tx,
        const uint160& senderAddr,
        uint8_t reputation
    );
    
    /**
     * Check if contract deployment should be rate limited
     * 
     * @param senderAddr Deployer address
     * @param reputation Deployer reputation (0-100)
     * @return true if deployment should be rejected
     */
    bool IsDeploymentRateLimited(
        const uint160& senderAddr,
        uint8_t reputation
    );
    
    /**
     * Record transaction submission
     * 
     * @param senderAddr Sender address
     * @param isDeployment Is this a contract deployment?
     */
    void RecordTransaction(
        const uint160& senderAddr,
        bool isDeployment
    );
    
    /**
     * Check mempool admission policy
     * 
     * Reputation-based admission:
     * - Low reputation: Stricter limits, higher fees required
     * - High reputation: Relaxed limits, lower fees
     * 
     * @param tx Transaction
     * @param senderAddr Sender address
     * @param reputation Sender reputation
     * @param fee Transaction fee
     * @return true if transaction should be admitted
     */
    bool CheckMempoolAdmission(
        const CTransaction& tx,
        const uint160& senderAddr,
        uint8_t reputation,
        CAmount fee
    );
    
    /**
     * Get rate limit status for address
     * 
     * @param address Address to check
     * @return Rate limit entry
     */
    RateLimitEntry GetRateLimitStatus(const uint160& address);
    
    /**
     * Ban address temporarily
     * 
     * @param address Address to ban
     * @param durationSeconds Ban duration
     * @param reason Ban reason
     */
    void BanAddress(
        const uint160& address,
        uint32_t durationSeconds,
        const std::string& reason
    );
    
    /**
     * Check if address is banned
     * 
     * @param address Address to check
     * @return true if banned
     */
    bool IsAddressBanned(const uint160& address);
    
    // ===== Malicious Contract Detection (26.2) =====
    
    /**
     * Analyze bytecode for malicious patterns
     * 
     * Detects:
     * - Known exploit patterns
     * - Infinite loops
     * - Resource exhaustion patterns
     * - Reentrancy vulnerabilities
     * - Unbounded loops
     * 
     * @param bytecode Contract bytecode
     * @return Analysis result
     */
    BytecodeAnalysisResult AnalyzeBytecode(const std::vector<uint8_t>& bytecode);
    
    /**
     * Check for infinite loop patterns
     * 
     * @param bytecode Contract bytecode
     * @return true if infinite loop detected
     */
    bool DetectInfiniteLoop(const std::vector<uint8_t>& bytecode);
    
    /**
     * Check for resource exhaustion patterns
     * 
     * @param bytecode Contract bytecode
     * @return true if resource exhaustion pattern detected
     */
    bool DetectResourceExhaustion(const std::vector<uint8_t>& bytecode);
    
    /**
     * Check for reentrancy vulnerability
     * 
     * @param bytecode Contract bytecode
     * @return true if reentrancy pattern detected
     */
    bool DetectReentrancy(const std::vector<uint8_t>& bytecode);
    
    /**
     * Check for unbounded loop patterns
     * 
     * @param bytecode Contract bytecode
     * @return true if unbounded loop detected
     */
    bool DetectUnboundedLoop(const std::vector<uint8_t>& bytecode);
    
    /**
     * Add custom malicious pattern
     * 
     * @param pattern Pattern to add
     */
    void AddMaliciousPattern(const MaliciousPattern& pattern);
    
    /**
     * Get all registered malicious patterns
     * 
     * @return Vector of patterns
     */
    std::vector<MaliciousPattern> GetMaliciousPatterns() const;
    
    // ===== Validator DoS Protection (26.3) =====
    
    /**
     * Check if validation request should be rate limited
     * 
     * @param validatorAddr Validator address
     * @return true if request should be rejected
     */
    bool IsValidationRequestRateLimited(const uint160& validatorAddr);
    
    /**
     * Record validation request
     * 
     * @param validatorAddr Validator address
     * @param txHash Transaction hash
     */
    void RecordValidationRequest(
        const uint160& validatorAddr,
        const uint256& txHash
    );
    
    /**
     * Record validation response
     * 
     * @param validatorAddr Validator address
     * @param txHash Transaction hash
     * @param timedOut Did the response time out?
     */
    void RecordValidationResponse(
        const uint160& validatorAddr,
        const uint256& txHash,
        bool timedOut
    );
    
    /**
     * Check for timed out validation requests
     * 
     * @return Vector of (validator, txHash) pairs that timed out
     */
    std::vector<std::pair<uint160, uint256>> CheckValidationTimeouts();
    
    /**
     * Get validator request stats
     * 
     * @param validatorAddr Validator address
     * @return Request entry
     */
    ValidatorRequestEntry GetValidatorRequestStats(const uint160& validatorAddr);
    
    /**
     * Penalize validator for timeout
     * 
     * @param validatorAddr Validator address
     */
    void PenalizeValidatorTimeout(const uint160& validatorAddr);
    
    // ===== Network Resource Protection (26.4) =====
    
    /**
     * Check if P2P message should be rate limited
     * 
     * @param peerAddr Peer address
     * @param messageSize Message size in bytes
     * @return true if message should be rejected
     */
    bool IsP2PMessageRateLimited(
        const CNetAddr& peerAddr,
        size_t messageSize
    );
    
    /**
     * Record P2P message
     * 
     * @param peerAddr Peer address
     * @param messageSize Message size in bytes
     * @param isIncoming Is this an incoming message?
     */
    void RecordP2PMessage(
        const CNetAddr& peerAddr,
        size_t messageSize,
        bool isIncoming
    );
    
    /**
     * Check if RPC call should be rate limited
     * 
     * @param callerAddr Caller address (from auth or IP)
     * @param reputation Caller reputation
     * @return true if call should be rejected
     */
    bool IsRPCRateLimited(
        const uint160& callerAddr,
        uint8_t reputation
    );
    
    /**
     * Record RPC call
     * 
     * @param callerAddr Caller address
     */
    void RecordRPCCall(const uint160& callerAddr);
    
    /**
     * Get P2P stats for peer
     * 
     * @param peerAddr Peer address
     * @return Message stats
     */
    P2PMessageStats GetP2PStats(const CNetAddr& peerAddr);
    
    /**
     * Get bandwidth usage
     * 
     * @return Total bandwidth used in current window
     */
    uint64_t GetCurrentBandwidthUsage();
    
    // ===== Statistics and Monitoring =====
    
    /**
     * Get DoS protection statistics
     * 
     * @return JSON object with statistics
     */
    UniValue GetStatistics();
    
    /**
     * Reset statistics
     */
    void ResetStatistics();
    
    /**
     * Get banned addresses
     * 
     * @return Vector of banned addresses with ban info
     */
    std::vector<std::pair<uint160, int64_t>> GetBannedAddresses();
    
    /**
     * Clear expired bans
     */
    void ClearExpiredBans();
    
private:
    CVMDatabase* m_db;
    RateLimitConfig m_config;
    
    // Rate limiting state
    std::map<uint160, RateLimitEntry> m_rateLimits;
    mutable CCriticalSection m_rateLimitLock;
    
    // Malicious patterns
    std::vector<MaliciousPattern> m_maliciousPatterns;
    mutable CCriticalSection m_patternLock;
    
    // Validator request tracking
    std::map<uint160, ValidatorRequestEntry> m_validatorRequests;
    mutable CCriticalSection m_validatorLock;
    
    // P2P message tracking
    std::map<CNetAddr, P2PMessageStats> m_p2pStats;
    mutable CCriticalSection m_p2pLock;
    
    // Statistics
    std::atomic<uint64_t> m_totalTransactionsChecked;
    std::atomic<uint64_t> m_transactionsRateLimited;
    std::atomic<uint64_t> m_deploymentsRateLimited;
    std::atomic<uint64_t> m_maliciousContractsDetected;
    std::atomic<uint64_t> m_validationRequestsRateLimited;
    std::atomic<uint64_t> m_validatorTimeouts;
    std::atomic<uint64_t> m_p2pMessagesRateLimited;
    std::atomic<uint64_t> m_rpcCallsRateLimited;
    
    // Helper methods
    void InitializeMaliciousPatterns();
    uint32_t GetTxRateLimit(uint8_t reputation) const;
    uint32_t GetDeployRateLimit(uint8_t reputation) const;
    uint32_t GetRPCRateLimit(uint8_t reputation) const;
    void CleanupOldEntries();
    bool MatchesPattern(const std::vector<uint8_t>& bytecode, const std::vector<uint8_t>& pattern);
    std::vector<size_t> FindJumpTargets(const std::vector<uint8_t>& bytecode);
    bool HasBackwardJump(const std::vector<uint8_t>& bytecode, const std::vector<size_t>& jumpTargets);
};

/**
 * Global DoS protection manager instance
 */
extern std::unique_ptr<DoSProtectionManager> g_dosProtection;

/**
 * Initialize global DoS protection
 */
void InitializeDoSProtection(CVMDatabase* db);

/**
 * Shutdown DoS protection
 */
void ShutdownDoSProtection();

} // namespace CVM

#endif // CASCOIN_CVM_DOS_PROTECTION_H
