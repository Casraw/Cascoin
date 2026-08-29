// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/dos_protection.h>
#include <cvm/cvmdb.h>
#include <cvm/bytecode_detector.h>
#include <util.h>
#include <utiltime.h>
#include <univalue.h>
#include <hash.h>
#include <algorithm>
#include <set>

namespace CVM {

// Global instance
std::unique_ptr<DoSProtectionManager> g_dosProtection;

// Constants
static const int64_t RATE_LIMIT_WINDOW_SECONDS = 60;
static const int64_t DEPLOY_RATE_LIMIT_WINDOW_SECONDS = 3600;
static const int64_t CLEANUP_INTERVAL_SECONDS = 300;
static const uint32_t MAX_VIOLATION_COUNT = 10;
static const uint32_t BAN_DURATION_BASE_SECONDS = 300;
static const uint32_t MAX_VALIDATOR_TIMEOUTS = 5;

// ===== RateLimitConfig =====

RateLimitConfig::RateLimitConfig()
    : lowRepTxPerMinute(10)
    , normalRepTxPerMinute(60)
    , highRepTxPerMinute(300)
    , criticalRepTxPerMinute(1000)
    , lowRepDeploysPerHour(1)
    , normalRepDeploysPerHour(5)
    , highRepDeploysPerHour(20)
    , criticalRepDeploysPerHour(100)
    , validationRequestsPerMinute(100)
    , lowRepRPCPerMinute(30)
    , normalRepRPCPerMinute(120)
    , highRepRPCPerMinute(600)
    , criticalRepRPCPerMinute(3000)
    , maxBandwidthPerPeer(1024 * 1024)  // 1 MB/s
    , maxMessagesPerMinute(1000)
    , validatorResponseTimeout(30)
{
}

RateLimitConfig RateLimitConfig::Default()
{
    return RateLimitConfig();
}

// ===== DoSProtectionManager =====

DoSProtectionManager::DoSProtectionManager()
    : m_db(nullptr)
    , m_totalTransactionsChecked(0)
    , m_transactionsRateLimited(0)
    , m_deploymentsRateLimited(0)
    , m_maliciousContractsDetected(0)
    , m_validationRequestsRateLimited(0)
    , m_validatorTimeouts(0)
    , m_p2pMessagesRateLimited(0)
    , m_rpcCallsRateLimited(0)
{
}

DoSProtectionManager::~DoSProtectionManager()
{
}

void DoSProtectionManager::Initialize(CVMDatabase* db, const RateLimitConfig& config)
{
    m_db = db;
    m_config = config;
    InitializeMaliciousPatterns();
    LogPrintf("CVM: DoS protection manager initialized\n");
}

// ===== Transaction Flooding Protection (26.1) =====

bool DoSProtectionManager::IsTransactionRateLimited(
    const CTransaction& tx,
    const uint160& senderAddr,
    uint8_t reputation)
{
    LOCK(m_rateLimitLock);
    m_totalTransactionsChecked++;
    
    // Check if address is banned
    if (IsAddressBanned(senderAddr)) {
        m_transactionsRateLimited++;
        return true;
    }
    
    int64_t currentTime = GetTime();
    auto& entry = m_rateLimits[senderAddr];
    entry.address = senderAddr;
    
    // Clean old timestamps
    while (!entry.txTimestamps.empty() && 
           currentTime - entry.txTimestamps.front() > RATE_LIMIT_WINDOW_SECONDS) {
        entry.txTimestamps.pop_front();
    }
    
    // Get rate limit based on reputation
    uint32_t rateLimit = GetTxRateLimit(reputation);
    
    // Check if over limit
    if (entry.txTimestamps.size() >= rateLimit) {
        entry.violationCount++;
        entry.lastViolationTime = currentTime;
        m_transactionsRateLimited++;
        
        // Auto-ban after too many violations
        if (entry.violationCount >= MAX_VIOLATION_COUNT) {
            uint32_t banDuration = BAN_DURATION_BASE_SECONDS * entry.violationCount;
            BanAddress(senderAddr, banDuration, "Repeated rate limit violations");
        }
        
        LogPrint(BCLog::CVM, "DoS: Transaction rate limited for %s (rep=%d, count=%zu, limit=%u)\n",
                 senderAddr.ToString(), reputation, entry.txTimestamps.size(), rateLimit);
        return true;
    }
    
    return false;
}

bool DoSProtectionManager::IsDeploymentRateLimited(
    const uint160& senderAddr,
    uint8_t reputation)
{
    LOCK(m_rateLimitLock);
    
    // Check if address is banned
    if (IsAddressBanned(senderAddr)) {
        m_deploymentsRateLimited++;
        return true;
    }
    
    int64_t currentTime = GetTime();
    auto& entry = m_rateLimits[senderAddr];
    entry.address = senderAddr;
    
    // Clean old timestamps (hourly window for deployments)
    while (!entry.deployTimestamps.empty() && 
           currentTime - entry.deployTimestamps.front() > DEPLOY_RATE_LIMIT_WINDOW_SECONDS) {
        entry.deployTimestamps.pop_front();
    }
    
    // Get rate limit based on reputation
    uint32_t rateLimit = GetDeployRateLimit(reputation);
    
    // Check if over limit
    if (entry.deployTimestamps.size() >= rateLimit) {
        entry.violationCount++;
        entry.lastViolationTime = currentTime;
        m_deploymentsRateLimited++;
        
        LogPrint(BCLog::CVM, "DoS: Deployment rate limited for %s (rep=%d, count=%zu, limit=%u)\n",
                 senderAddr.ToString(), reputation, entry.deployTimestamps.size(), rateLimit);
        return true;
    }
    
    return false;
}

void DoSProtectionManager::RecordTransaction(
    const uint160& senderAddr,
    bool isDeployment)
{
    LOCK(m_rateLimitLock);
    
    int64_t currentTime = GetTime();
    auto& entry = m_rateLimits[senderAddr];
    entry.address = senderAddr;
    
    entry.txTimestamps.push_back(currentTime);
    
    if (isDeployment) {
        entry.deployTimestamps.push_back(currentTime);
    }
}

bool DoSProtectionManager::CheckMempoolAdmission(
    const CTransaction& tx,
    const uint160& senderAddr,
    uint8_t reputation,
    CAmount fee)
{
    // Check if banned
    if (IsAddressBanned(senderAddr)) {
        return false;
    }
    
    // Low reputation addresses need higher fees
    CAmount minFee = 1000;  // Base minimum fee (satoshis)
    
    if (reputation < 30) {
        minFee = 10000;  // 10x fee for very low reputation
    } else if (reputation < 50) {
        minFee = 5000;   // 5x fee for low reputation
    } else if (reputation < 70) {
        minFee = 2000;   // 2x fee for normal reputation
    }
    // High reputation (70+) uses base fee
    
    if (fee < minFee) {
        LogPrint(BCLog::CVM, "DoS: Mempool admission rejected for %s (rep=%d, fee=%lld, minFee=%lld)\n",
                 senderAddr.ToString(), reputation, fee, minFee);
        return false;
    }
    
    return true;
}

RateLimitEntry DoSProtectionManager::GetRateLimitStatus(const uint160& address)
{
    LOCK(m_rateLimitLock);
    
    auto it = m_rateLimits.find(address);
    if (it != m_rateLimits.end()) {
        return it->second;
    }
    
    return RateLimitEntry();
}

void DoSProtectionManager::BanAddress(
    const uint160& address,
    uint32_t durationSeconds,
    const std::string& reason)
{
    LOCK(m_rateLimitLock);
    
    int64_t currentTime = GetTime();
    auto& entry = m_rateLimits[address];
    entry.address = address;
    entry.banUntil = currentTime + durationSeconds;
    
    LogPrintf("CVM DoS: Banned address %s for %u seconds: %s\n",
              address.ToString(), durationSeconds, reason);
}

bool DoSProtectionManager::IsAddressBanned(const uint160& address)
{
    // Note: Caller should hold m_rateLimitLock
    auto it = m_rateLimits.find(address);
    if (it == m_rateLimits.end()) {
        return false;
    }
    
    int64_t currentTime = GetTime();
    return it->second.banUntil > currentTime;
}

// ===== Malicious Contract Detection (26.2) =====

void DoSProtectionManager::InitializeMaliciousPatterns()
{
    LOCK(m_patternLock);
    m_maliciousPatterns.clear();
    
    // Pattern 1: SELFDESTRUCT without access control
    MaliciousPattern selfDestruct;
    selfDestruct.name = "Unprotected SELFDESTRUCT";
    selfDestruct.description = "SELFDESTRUCT opcode without proper access control";
    selfDestruct.pattern = {0xff};  // SELFDESTRUCT opcode
    selfDestruct.severity = 0.9;
    selfDestruct.blockDeploy = false;  // Warning only
    m_maliciousPatterns.push_back(selfDestruct);
    
    // Pattern 2: Reentrancy pattern (CALL followed by SSTORE)
    MaliciousPattern reentrancy;
    reentrancy.name = "Potential Reentrancy";
    reentrancy.description = "CALL opcode followed by state change (SSTORE)";
    reentrancy.pattern = {0xf1};  // CALL opcode
    reentrancy.severity = 0.7;
    reentrancy.blockDeploy = false;
    m_maliciousPatterns.push_back(reentrancy);
    
    // Pattern 3: Delegatecall to user-controlled address
    MaliciousPattern delegateCall;
    delegateCall.name = "DELEGATECALL Risk";
    delegateCall.description = "DELEGATECALL opcode detected";
    delegateCall.pattern = {0xf4};  // DELEGATECALL opcode
    delegateCall.severity = 0.6;
    delegateCall.blockDeploy = false;
    m_maliciousPatterns.push_back(delegateCall);
    
    // Pattern 4: tx.origin usage (ORIGIN opcode)
    MaliciousPattern txOrigin;
    txOrigin.name = "tx.origin Usage";
    txOrigin.description = "ORIGIN opcode used (potential phishing vulnerability)";
    txOrigin.pattern = {0x32};  // ORIGIN opcode
    txOrigin.severity = 0.5;
    txOrigin.blockDeploy = false;
    m_maliciousPatterns.push_back(txOrigin);
    
    // Pattern 5: Infinite loop pattern (JUMP to same location)
    MaliciousPattern infiniteLoop;
    infiniteLoop.name = "Potential Infinite Loop";
    infiniteLoop.description = "Backward jump detected (potential infinite loop)";
    infiniteLoop.pattern = {0x56};  // JUMP opcode
    infiniteLoop.severity = 0.8;
    infiniteLoop.blockDeploy = false;
    m_maliciousPatterns.push_back(infiniteLoop);
    
    LogPrintf("CVM DoS: Initialized %zu malicious patterns\n", m_maliciousPatterns.size());
}

BytecodeAnalysisResult DoSProtectionManager::AnalyzeBytecode(const std::vector<uint8_t>& bytecode)
{
    BytecodeAnalysisResult result;
    
    if (bytecode.empty()) {
        result.analysisReport = "Empty bytecode";
        return result;
    }
    
    LOCK(m_patternLock);
    
    // Check for known malicious patterns
    for (const auto& pattern : m_maliciousPatterns) {
        if (MatchesPattern(bytecode, pattern.pattern)) {
            result.detectedPatterns.push_back(pattern.name);
            result.riskScore = std::max(result.riskScore, pattern.severity);
            
            if (pattern.blockDeploy) {
                result.isMalicious = true;
            }
        }
    }
    
    // Check for infinite loops
    result.hasInfiniteLoop = DetectInfiniteLoop(bytecode);
    if (result.hasInfiniteLoop) {
        result.riskScore = std::max(result.riskScore, 0.9);
        result.detectedPatterns.push_back("Infinite Loop Pattern");
    }
    
    // Check for resource exhaustion
    result.hasResourceExhaustion = DetectResourceExhaustion(bytecode);
    if (result.hasResourceExhaustion) {
        result.riskScore = std::max(result.riskScore, 0.8);
        result.detectedPatterns.push_back("Resource Exhaustion Pattern");
    }
    
    // Check for reentrancy
    result.hasReentrancy = DetectReentrancy(bytecode);
    if (result.hasReentrancy) {
        result.riskScore = std::max(result.riskScore, 0.7);
        result.detectedPatterns.push_back("Reentrancy Vulnerability");
    }
    
    // Check for unbounded loops
    result.hasUnboundedLoop = DetectUnboundedLoop(bytecode);
    if (result.hasUnboundedLoop) {
        result.riskScore = std::max(result.riskScore, 0.6);
        result.detectedPatterns.push_back("Unbounded Loop");
    }
    
    // Check for SELFDESTRUCT
    for (uint8_t byte : bytecode) {
        if (byte == 0xff) {
            result.hasSelfDestruct = true;
            break;
        }
    }
    
    // Mark as malicious if risk score is very high
    if (result.riskScore >= 0.9) {
        result.isMalicious = true;
        m_maliciousContractsDetected++;
    }
    
    // Generate report
    result.analysisReport = "Bytecode Analysis Report\n";
    result.analysisReport += "Size: " + std::to_string(bytecode.size()) + " bytes\n";
    result.analysisReport += "Risk Score: " + std::to_string(result.riskScore) + "\n";
    result.analysisReport += "Detected Patterns: " + std::to_string(result.detectedPatterns.size()) + "\n";
    
    for (const auto& pattern : result.detectedPatterns) {
        result.analysisReport += "  - " + pattern + "\n";
    }
    
    return result;
}

bool DoSProtectionManager::DetectInfiniteLoop(const std::vector<uint8_t>& bytecode)
{
    // Find all jump targets
    std::vector<size_t> jumpTargets = FindJumpTargets(bytecode);
    
    // Check for backward jumps (potential infinite loops)
    return HasBackwardJump(bytecode, jumpTargets);
}

bool DoSProtectionManager::DetectResourceExhaustion(const std::vector<uint8_t>& bytecode)
{
    // Count expensive operations
    size_t expensiveOps = 0;
    size_t loopCount = 0;
    
    for (size_t i = 0; i < bytecode.size(); ++i) {
        uint8_t opcode = bytecode[i];
        
        // Count expensive operations
        if (opcode == 0x55) expensiveOps++;  // SSTORE
        if (opcode == 0xf0) expensiveOps++;  // CREATE
        if (opcode == 0xf5) expensiveOps++;  // CREATE2
        if (opcode == 0xf1) expensiveOps++;  // CALL
        if (opcode == 0xf2) expensiveOps++;  // CALLCODE
        if (opcode == 0xf4) expensiveOps++;  // DELEGATECALL
        if (opcode == 0xfa) expensiveOps++;  // STATICCALL
        if (opcode == 0xa0 || opcode == 0xa1 || opcode == 0xa2 || 
            opcode == 0xa3 || opcode == 0xa4) expensiveOps++;  // LOG0-LOG4
        
        // Count potential loops
        if (opcode == 0x56 || opcode == 0x57) loopCount++;  // JUMP, JUMPI
        
        // Skip PUSH data
        if (opcode >= 0x60 && opcode <= 0x7f) {
            i += (opcode - 0x5f);
        }
    }
    
    // High ratio of expensive ops to code size is suspicious
    double expensiveRatio = (double)expensiveOps / bytecode.size();
    
    // Many loops with expensive operations is suspicious
    return (expensiveRatio > 0.1 && loopCount > 5) || expensiveOps > 50;
}

bool DoSProtectionManager::DetectReentrancy(const std::vector<uint8_t>& bytecode)
{
    // Look for CALL followed by SSTORE pattern
    // This is a common reentrancy vulnerability pattern
    
    bool foundCall = false;
    
    for (size_t i = 0; i < bytecode.size(); ++i) {
        uint8_t opcode = bytecode[i];
        
        // Check for CALL, CALLCODE, or DELEGATECALL
        if (opcode == 0xf1 || opcode == 0xf2 || opcode == 0xf4) {
            foundCall = true;
        }
        
        // If we found a CALL and then SSTORE, potential reentrancy
        if (foundCall && opcode == 0x55) {
            return true;
        }
        
        // Reset on JUMPDEST (new code block)
        if (opcode == 0x5b) {
            foundCall = false;
        }
        
        // Skip PUSH data
        if (opcode >= 0x60 && opcode <= 0x7f) {
            i += (opcode - 0x5f);
        }
    }
    
    return false;
}

bool DoSProtectionManager::DetectUnboundedLoop(const std::vector<uint8_t>& bytecode)
{
    // Look for loops without gas checks or iteration limits
    // This is a simplified heuristic
    
    std::set<size_t> jumpDests;
    std::vector<std::pair<size_t, size_t>> jumps;  // (position, target)
    
    // First pass: find all JUMPDESTs and JUMPs
    for (size_t i = 0; i < bytecode.size(); ++i) {
        uint8_t opcode = bytecode[i];
        
        if (opcode == 0x5b) {  // JUMPDEST
            jumpDests.insert(i);
        }
        
        // Skip PUSH data
        if (opcode >= 0x60 && opcode <= 0x7f) {
            uint8_t pushSize = opcode - 0x5f;
            
            // Check if next opcode is JUMP or JUMPI
            if (i + pushSize + 1 < bytecode.size()) {
                uint8_t nextOp = bytecode[i + pushSize + 1];
                if (nextOp == 0x56 || nextOp == 0x57) {
                    // Extract jump target
                    size_t target = 0;
                    for (uint8_t j = 0; j < pushSize && i + 1 + j < bytecode.size(); ++j) {
                        target = (target << 8) | bytecode[i + 1 + j];
                    }
                    jumps.push_back({i + pushSize + 1, target});
                }
            }
            
            i += pushSize;
        }
    }
    
    // Check for backward jumps to valid JUMPDESTs
    for (const auto& jump : jumps) {
        if (jump.second < jump.first && jumpDests.count(jump.second)) {
            // Found a backward jump - check if there's a GAS check nearby
            bool hasGasCheck = false;
            
            // Look for GAS opcode (0x5a) in the loop body
            for (size_t i = jump.second; i < jump.first && i < bytecode.size(); ++i) {
                if (bytecode[i] == 0x5a) {
                    hasGasCheck = true;
                    break;
                }
            }
            
            if (!hasGasCheck) {
                return true;  // Unbounded loop without gas check
            }
        }
    }
    
    return false;
}

void DoSProtectionManager::AddMaliciousPattern(const MaliciousPattern& pattern)
{
    LOCK(m_patternLock);
    m_maliciousPatterns.push_back(pattern);
}

std::vector<MaliciousPattern> DoSProtectionManager::GetMaliciousPatterns() const
{
    LOCK(m_patternLock);
    return m_maliciousPatterns;
}

// ===== Validator DoS Protection (26.3) =====

bool DoSProtectionManager::IsValidationRequestRateLimited(const uint160& validatorAddr)
{
    LOCK(m_validatorLock);
    
    int64_t currentTime = GetTime();
    auto& entry = m_validatorRequests[validatorAddr];
    entry.validatorAddress = validatorAddr;
    
    // Clean old timestamps
    while (!entry.requestTimestamps.empty() && 
           currentTime - entry.requestTimestamps.front() > RATE_LIMIT_WINDOW_SECONDS) {
        entry.requestTimestamps.pop_front();
    }
    
    // Check if over limit
    if (entry.requestTimestamps.size() >= m_config.validationRequestsPerMinute) {
        m_validationRequestsRateLimited++;
        LogPrint(BCLog::CVM, "DoS: Validation request rate limited for %s (count=%zu, limit=%u)\n",
                 validatorAddr.ToString(), entry.requestTimestamps.size(), 
                 m_config.validationRequestsPerMinute);
        return true;
    }
    
    return false;
}

void DoSProtectionManager::RecordValidationRequest(
    const uint160& validatorAddr,
    const uint256& txHash)
{
    LOCK(m_validatorLock);
    
    int64_t currentTime = GetTime();
    auto& entry = m_validatorRequests[validatorAddr];
    entry.validatorAddress = validatorAddr;
    
    entry.requestTimestamps.push_back(currentTime);
    
    // Set deadline for response
    int64_t deadline = currentTime + m_config.validatorResponseTimeout;
    entry.pendingResponses[txHash] = deadline;
}

void DoSProtectionManager::RecordValidationResponse(
    const uint160& validatorAddr,
    const uint256& txHash,
    bool timedOut)
{
    LOCK(m_validatorLock);
    
    auto it = m_validatorRequests.find(validatorAddr);
    if (it == m_validatorRequests.end()) {
        return;
    }
    
    auto& entry = it->second;
    entry.pendingResponses.erase(txHash);
    
    if (timedOut) {
        entry.timeoutCount++;
        entry.lastTimeoutTime = GetTime();
        m_validatorTimeouts++;
        
        // Penalize validator for timeout
        if (entry.timeoutCount >= MAX_VALIDATOR_TIMEOUTS) {
            PenalizeValidatorTimeout(validatorAddr);
        }
    }
}

std::vector<std::pair<uint160, uint256>> DoSProtectionManager::CheckValidationTimeouts()
{
    LOCK(m_validatorLock);
    
    std::vector<std::pair<uint160, uint256>> timedOut;
    int64_t currentTime = GetTime();
    
    for (auto& [validatorAddr, entry] : m_validatorRequests) {
        std::vector<uint256> toRemove;
        
        for (const auto& [txHash, deadline] : entry.pendingResponses) {
            if (currentTime > deadline) {
                timedOut.push_back({validatorAddr, txHash});
                toRemove.push_back(txHash);
            }
        }
        
        for (const auto& txHash : toRemove) {
            entry.pendingResponses.erase(txHash);
            entry.timeoutCount++;
            entry.lastTimeoutTime = currentTime;
        }
    }
    
    m_validatorTimeouts += timedOut.size();
    return timedOut;
}

ValidatorRequestEntry DoSProtectionManager::GetValidatorRequestStats(const uint160& validatorAddr)
{
    LOCK(m_validatorLock);
    
    auto it = m_validatorRequests.find(validatorAddr);
    if (it != m_validatorRequests.end()) {
        return it->second;
    }
    
    return ValidatorRequestEntry();
}

void DoSProtectionManager::PenalizeValidatorTimeout(const uint160& validatorAddr)
{
    // Log the penalty
    LogPrintf("CVM DoS: Penalizing validator %s for excessive timeouts\n",
              validatorAddr.ToString());
    
    // The actual reputation penalty should be applied through the HAT consensus system
    // This is just a notification that the validator should be penalized
    
    // Reset timeout count after penalty
    LOCK(m_validatorLock);
    auto it = m_validatorRequests.find(validatorAddr);
    if (it != m_validatorRequests.end()) {
        it->second.timeoutCount = 0;
    }
}

// ===== Network Resource Protection (26.4) =====

bool DoSProtectionManager::IsP2PMessageRateLimited(
    const CNetAddr& peerAddr,
    size_t messageSize)
{
    LOCK(m_p2pLock);
    
    int64_t currentTime = GetTime();
    auto& stats = m_p2pStats[peerAddr];
    
    // Reset window if expired
    if (currentTime - stats.windowStart > RATE_LIMIT_WINDOW_SECONDS) {
        stats = P2PMessageStats();
        stats.windowStart = currentTime;
    }
    
    // Check bandwidth limit
    if (stats.bytesReceived + messageSize > m_config.maxBandwidthPerPeer * RATE_LIMIT_WINDOW_SECONDS) {
        m_p2pMessagesRateLimited++;
        LogPrint(BCLog::CVM, "DoS: P2P bandwidth limit exceeded for peer %s\n",
                 peerAddr.ToString());
        return true;
    }
    
    // Check message count limit
    if (stats.messagesReceived >= m_config.maxMessagesPerMinute) {
        m_p2pMessagesRateLimited++;
        LogPrint(BCLog::CVM, "DoS: P2P message count limit exceeded for peer %s\n",
                 peerAddr.ToString());
        return true;
    }
    
    return false;
}

void DoSProtectionManager::RecordP2PMessage(
    const CNetAddr& peerAddr,
    size_t messageSize,
    bool isIncoming)
{
    LOCK(m_p2pLock);
    
    int64_t currentTime = GetTime();
    auto& stats = m_p2pStats[peerAddr];
    
    // Reset window if expired
    if (currentTime - stats.windowStart > RATE_LIMIT_WINDOW_SECONDS) {
        stats = P2PMessageStats();
        stats.windowStart = currentTime;
    }
    
    if (isIncoming) {
        stats.bytesReceived += messageSize;
        stats.messagesReceived++;
    } else {
        stats.bytesSent += messageSize;
        stats.messagesSent++;
    }
}

bool DoSProtectionManager::IsRPCRateLimited(
    const uint160& callerAddr,
    uint8_t reputation)
{
    LOCK(m_rateLimitLock);
    
    int64_t currentTime = GetTime();
    auto& entry = m_rateLimits[callerAddr];
    entry.address = callerAddr;
    
    // Clean old timestamps
    while (!entry.rpcTimestamps.empty() && 
           currentTime - entry.rpcTimestamps.front() > RATE_LIMIT_WINDOW_SECONDS) {
        entry.rpcTimestamps.pop_front();
    }
    
    // Get rate limit based on reputation
    uint32_t rateLimit = GetRPCRateLimit(reputation);
    
    // Check if over limit
    if (entry.rpcTimestamps.size() >= rateLimit) {
        m_rpcCallsRateLimited++;
        LogPrint(BCLog::CVM, "DoS: RPC rate limited for %s (rep=%d, count=%zu, limit=%u)\n",
                 callerAddr.ToString(), reputation, entry.rpcTimestamps.size(), rateLimit);
        return true;
    }
    
    return false;
}

void DoSProtectionManager::RecordRPCCall(const uint160& callerAddr)
{
    LOCK(m_rateLimitLock);
    
    int64_t currentTime = GetTime();
    auto& entry = m_rateLimits[callerAddr];
    entry.address = callerAddr;
    
    entry.rpcTimestamps.push_back(currentTime);
}

P2PMessageStats DoSProtectionManager::GetP2PStats(const CNetAddr& peerAddr)
{
    LOCK(m_p2pLock);
    
    auto it = m_p2pStats.find(peerAddr);
    if (it != m_p2pStats.end()) {
        return it->second;
    }
    
    return P2PMessageStats();
}

uint64_t DoSProtectionManager::GetCurrentBandwidthUsage()
{
    LOCK(m_p2pLock);
    
    uint64_t totalBandwidth = 0;
    int64_t currentTime = GetTime();
    
    for (const auto& [peerAddr, stats] : m_p2pStats) {
        if (currentTime - stats.windowStart <= RATE_LIMIT_WINDOW_SECONDS) {
            totalBandwidth += stats.bytesReceived + stats.bytesSent;
        }
    }
    
    return totalBandwidth;
}

// ===== Statistics and Monitoring =====

UniValue DoSProtectionManager::GetStatistics()
{
    UniValue result(UniValue::VOBJ);
    
    result.pushKV("total_transactions_checked", (uint64_t)m_totalTransactionsChecked);
    result.pushKV("transactions_rate_limited", (uint64_t)m_transactionsRateLimited);
    result.pushKV("deployments_rate_limited", (uint64_t)m_deploymentsRateLimited);
    result.pushKV("malicious_contracts_detected", (uint64_t)m_maliciousContractsDetected);
    result.pushKV("validation_requests_rate_limited", (uint64_t)m_validationRequestsRateLimited);
    result.pushKV("validator_timeouts", (uint64_t)m_validatorTimeouts);
    result.pushKV("p2p_messages_rate_limited", (uint64_t)m_p2pMessagesRateLimited);
    result.pushKV("rpc_calls_rate_limited", (uint64_t)m_rpcCallsRateLimited);
    
    // Rate limit stats
    {
        LOCK(m_rateLimitLock);
        result.pushKV("tracked_addresses", (uint64_t)m_rateLimits.size());
        
        uint64_t bannedCount = 0;
        int64_t currentTime = GetTime();
        for (const auto& [addr, entry] : m_rateLimits) {
            if (entry.banUntil > currentTime) {
                bannedCount++;
            }
        }
        result.pushKV("banned_addresses", bannedCount);
    }
    
    // Validator stats
    {
        LOCK(m_validatorLock);
        result.pushKV("tracked_validators", (uint64_t)m_validatorRequests.size());
        
        uint64_t pendingResponses = 0;
        for (const auto& [addr, entry] : m_validatorRequests) {
            pendingResponses += entry.pendingResponses.size();
        }
        result.pushKV("pending_validator_responses", pendingResponses);
    }
    
    // P2P stats
    {
        LOCK(m_p2pLock);
        result.pushKV("tracked_peers", (uint64_t)m_p2pStats.size());
        result.pushKV("current_bandwidth_usage", GetCurrentBandwidthUsage());
    }
    
    // Pattern stats
    {
        LOCK(m_patternLock);
        result.pushKV("malicious_patterns_registered", (uint64_t)m_maliciousPatterns.size());
    }
    
    return result;
}

void DoSProtectionManager::ResetStatistics()
{
    m_totalTransactionsChecked = 0;
    m_transactionsRateLimited = 0;
    m_deploymentsRateLimited = 0;
    m_maliciousContractsDetected = 0;
    m_validationRequestsRateLimited = 0;
    m_validatorTimeouts = 0;
    m_p2pMessagesRateLimited = 0;
    m_rpcCallsRateLimited = 0;
}

std::vector<std::pair<uint160, int64_t>> DoSProtectionManager::GetBannedAddresses()
{
    LOCK(m_rateLimitLock);
    
    std::vector<std::pair<uint160, int64_t>> banned;
    int64_t currentTime = GetTime();
    
    for (const auto& [addr, entry] : m_rateLimits) {
        if (entry.banUntil > currentTime) {
            banned.push_back({addr, entry.banUntil});
        }
    }
    
    return banned;
}

void DoSProtectionManager::ClearExpiredBans()
{
    LOCK(m_rateLimitLock);
    
    int64_t currentTime = GetTime();
    
    for (auto& [addr, entry] : m_rateLimits) {
        if (entry.banUntil > 0 && entry.banUntil <= currentTime) {
            entry.banUntil = 0;
            entry.violationCount = 0;
        }
    }
}

// ===== Helper Methods =====

uint32_t DoSProtectionManager::GetTxRateLimit(uint8_t reputation) const
{
    if (reputation >= 90) {
        return m_config.criticalRepTxPerMinute;
    } else if (reputation >= 70) {
        return m_config.highRepTxPerMinute;
    } else if (reputation >= 50) {
        return m_config.normalRepTxPerMinute;
    } else {
        return m_config.lowRepTxPerMinute;
    }
}

uint32_t DoSProtectionManager::GetDeployRateLimit(uint8_t reputation) const
{
    if (reputation >= 90) {
        return m_config.criticalRepDeploysPerHour;
    } else if (reputation >= 70) {
        return m_config.highRepDeploysPerHour;
    } else if (reputation >= 50) {
        return m_config.normalRepDeploysPerHour;
    } else {
        return m_config.lowRepDeploysPerHour;
    }
}

uint32_t DoSProtectionManager::GetRPCRateLimit(uint8_t reputation) const
{
    if (reputation >= 90) {
        return m_config.criticalRepRPCPerMinute;
    } else if (reputation >= 70) {
        return m_config.highRepRPCPerMinute;
    } else if (reputation >= 50) {
        return m_config.normalRepRPCPerMinute;
    } else {
        return m_config.lowRepRPCPerMinute;
    }
}

void DoSProtectionManager::CleanupOldEntries()
{
    int64_t currentTime = GetTime();
    int64_t cleanupThreshold = currentTime - CLEANUP_INTERVAL_SECONDS;
    
    // Cleanup rate limit entries
    {
        LOCK(m_rateLimitLock);
        for (auto it = m_rateLimits.begin(); it != m_rateLimits.end(); ) {
            auto& entry = it->second;
            
            // Remove if no recent activity and not banned
            if (entry.txTimestamps.empty() && 
                entry.deployTimestamps.empty() && 
                entry.rpcTimestamps.empty() &&
                entry.banUntil <= currentTime &&
                entry.lastViolationTime < cleanupThreshold) {
                it = m_rateLimits.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Cleanup validator entries
    {
        LOCK(m_validatorLock);
        for (auto it = m_validatorRequests.begin(); it != m_validatorRequests.end(); ) {
            auto& entry = it->second;
            
            if (entry.requestTimestamps.empty() && 
                entry.pendingResponses.empty() &&
                entry.lastTimeoutTime < cleanupThreshold) {
                it = m_validatorRequests.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Cleanup P2P stats
    {
        LOCK(m_p2pLock);
        for (auto it = m_p2pStats.begin(); it != m_p2pStats.end(); ) {
            if (currentTime - it->second.windowStart > RATE_LIMIT_WINDOW_SECONDS * 2) {
                it = m_p2pStats.erase(it);
            } else {
                ++it;
            }
        }
    }
}

bool DoSProtectionManager::MatchesPattern(
    const std::vector<uint8_t>& bytecode,
    const std::vector<uint8_t>& pattern)
{
    if (pattern.empty() || bytecode.size() < pattern.size()) {
        return false;
    }
    
    for (size_t i = 0; i <= bytecode.size() - pattern.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < pattern.size(); ++j) {
            if (bytecode[i + j] != pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    
    return false;
}

std::vector<size_t> DoSProtectionManager::FindJumpTargets(const std::vector<uint8_t>& bytecode)
{
    std::vector<size_t> targets;
    
    for (size_t i = 0; i < bytecode.size(); ++i) {
        uint8_t opcode = bytecode[i];
        
        // JUMPDEST
        if (opcode == 0x5b) {
            targets.push_back(i);
        }
        
        // Skip PUSH data
        if (opcode >= 0x60 && opcode <= 0x7f) {
            i += (opcode - 0x5f);
        }
    }
    
    return targets;
}

bool DoSProtectionManager::HasBackwardJump(
    const std::vector<uint8_t>& bytecode,
    const std::vector<size_t>& jumpTargets)
{
    for (size_t i = 0; i < bytecode.size(); ++i) {
        uint8_t opcode = bytecode[i];
        
        // Check PUSH followed by JUMP/JUMPI
        if (opcode >= 0x60 && opcode <= 0x7f) {
            uint8_t pushSize = opcode - 0x5f;
            
            if (i + pushSize + 1 < bytecode.size()) {
                uint8_t nextOp = bytecode[i + pushSize + 1];
                
                if (nextOp == 0x56 || nextOp == 0x57) {  // JUMP or JUMPI
                    // Extract target
                    size_t target = 0;
                    for (uint8_t j = 0; j < pushSize && i + 1 + j < bytecode.size(); ++j) {
                        target = (target << 8) | bytecode[i + 1 + j];
                    }
                    
                    // Check if backward jump
                    if (target < i + pushSize + 1) {
                        // Verify target is a valid JUMPDEST
                        if (std::find(jumpTargets.begin(), jumpTargets.end(), target) != jumpTargets.end()) {
                            return true;
                        }
                    }
                }
            }
            
            i += pushSize;
        }
    }
    
    return false;
}

// ===== Global Functions =====

void InitializeDoSProtection(CVMDatabase* db)
{
    g_dosProtection = std::make_unique<DoSProtectionManager>();
    g_dosProtection->Initialize(db);
}

void ShutdownDoSProtection()
{
    g_dosProtection.reset();
}

} // namespace CVM
