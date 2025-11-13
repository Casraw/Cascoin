// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/mempool_manager.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/cvm.h>
#include <cvm/trust_context.h>
#include <cvm/hat_consensus.h>
#include <chain.h>
#include <util.h>
#include <utiltime.h>
#include <univalue.h>

extern CChain chainActive;

namespace CVM {

// Rate limiting constants
static const int64_t RATE_LIMIT_WINDOW = 60;  // 60 seconds
static const uint32_t MAX_SUBMISSIONS_PER_WINDOW = 10;  // For low reputation

MempoolManager::MempoolManager()
    : m_db(nullptr)
    , m_priorityManager(std::make_unique<TransactionPriorityManager>())
    , m_gasAllowanceManager(std::make_unique<GasAllowanceTracker>())
    , m_gasSubsidyManager(std::make_unique<GasSubsidyTracker>())
    , m_gasSystem(std::make_unique<cvm::SustainableGasSystem>())
    , m_hatValidator(nullptr)
    , m_totalValidated(0)
    , m_totalAccepted(0)
    , m_totalRejected(0)
    , m_freeGasTransactions(0)
    , m_subsidizedTransactions(0)
{
}

MempoolManager::~MempoolManager()
{
}

void MempoolManager::Initialize(CVMDatabase* db)
{
    m_db = db;
    
    // Initialize managers (they don't have Initialize methods, just set db)
    // TODO: Add Initialize methods to GasAllowanceTracker and GasSubsidyTracker if needed
    
    LogPrintf("CVM: Mempool manager initialized\n");
}

// ===== Transaction Validation =====

MempoolManager::ValidationResult MempoolManager::ValidateTransaction(
    const CTransaction& tx,
    int currentHeight)
{
    ValidationResult result;
    
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_totalValidated++;
    
    // Check if this is a CVM/EVM transaction
    int cvmOutputIndex = FindCVMOpReturn(tx);
    if (cvmOutputIndex < 0) {
        // Not a CVM transaction, accept
        result.isValid = true;
        m_totalAccepted++;
        return result;
    }
    
    // Parse CVM data
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[cvmOutputIndex], opType, data)) {
        result.error = "Invalid CVM OP_RETURN format";
        m_totalRejected++;
        return result;
    }
    
    // Get sender address
    uint160 senderAddr = GetSenderAddress(tx);
    result.reputation = GetReputation(senderAddr);
    
    // Check rate limiting
    if (IsRateLimited(senderAddr)) {
        result.error = "Rate limit exceeded";
        m_totalRejected++;
        return result;
    }
    
    // Extract gas limit
    uint64_t gasLimit = ExtractGasLimit(tx);
    if (gasLimit == 0) {
        result.error = "Invalid gas limit";
        m_totalRejected++;
        return result;
    }
    
    // Check gas limit bounds
    if (gasLimit > MAX_GAS_PER_TX) {
        result.error = "Gas limit exceeds maximum";
        m_totalRejected++;
        return result;
    }
    
    // Get transaction priority
    auto priority = m_priorityManager->CalculatePriority(tx, *m_db);
    result.priority = priority.level;
    
    // Check free gas eligibility
    result.isFreeGas = CheckFreeGasEligibility(tx, senderAddr);
    
    if (result.isFreeGas) {
        // Check remaining allowance
        uint64_t remaining = GetRemainingFreeGas(senderAddr);
        if (remaining < gasLimit) {
            result.isFreeGas = false;
            result.error = "Insufficient free gas allowance";
        } else {
            result.effectiveFee = 0;
            m_freeGasTransactions++;
        }
    }
    
    // Check for gas subsidy
    if (!result.isFreeGas) {
        // TODO: Check for applicable gas subsidies
        // For now, calculate normal fee with reputation discount
        result.effectiveFee = CalculateEffectiveFee(tx, gasLimit, result.reputation);
    }
    
    // Validate minimum fee
    CAmount minFee = GetMinimumFee(tx, result.reputation);
    if (result.effectiveFee < minFee && !result.isFreeGas) {
        result.error = "Fee below minimum";
        m_totalRejected++;
        return result;
    }
    
    // Record submission for rate limiting
    RecordTransactionSubmission(senderAddr);
    
    result.isValid = true;
    m_totalAccepted++;
    
    return result;
}

bool MempoolManager::CheckFreeGasEligibility(
    const CTransaction& tx,
    const uint160& senderAddr)
{
    if (!m_gasAllowanceManager) {
        return false;
    }
    
    uint8_t reputation = GetReputation(senderAddr);
    // Check if reputation is high enough (80+)
    return reputation >= 80;
}

bool MempoolManager::ValidateGasSubsidy(
    const CTransaction& tx,
    const GasSubsidyTracker::SubsidyRecord& subsidy)
{
    if (!m_gasSubsidyManager) {
        return false;
    }
    
    // Validate subsidy exists and is active
    // TODO: Implement subsidy validation
    return true;
}

// ===== Priority Management =====

TransactionPriorityManager::TransactionPriority MempoolManager::GetTransactionPriority(
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
    
    return m_priorityManager->CalculatePriority(tx, *m_db);
}

bool MempoolManager::CompareTransactionPriority(
    const CTransaction& a,
    const CTransaction& b)
{
    auto priorityA = GetTransactionPriority(a);
    auto priorityB = GetTransactionPriority(b);
    
    return TransactionPriorityManager::CompareTransactionPriority(priorityA, priorityB);
}

bool MempoolManager::HasGuaranteedInclusion(const CTransaction& tx)
{
    auto priority = GetTransactionPriority(tx);
    return priority.guaranteedInclusion;
}

// ===== Free Gas Management =====

void MempoolManager::RecordFreeGasUsage(
    const CTransaction& tx,
    uint64_t gasUsed,
    int currentHeight)
{
    if (!m_gasAllowanceManager) {
        return;
    }
    
    uint160 senderAddr = GetSenderAddress(tx);
    // Deduct gas from allowance
    m_gasAllowanceManager->DeductGas(senderAddr, gasUsed, currentHeight);
}

uint64_t MempoolManager::GetRemainingFreeGas(const uint160& address)
{
    if (!m_gasAllowanceManager) {
        return 0;
    }
    
    // Create trust context for reputation lookup
    TrustContext trust;
    int64_t currentBlock = 0;  // TODO: Get actual current block height
    
    auto state = m_gasAllowanceManager->GetAllowanceState(address, trust, currentBlock);
    return state.dailyAllowance > state.usedToday ? 
           state.dailyAllowance - state.usedToday : 0;
}

// ===== Rate Limiting =====

bool MempoolManager::IsRateLimited(const uint160& address)
{
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    int64_t currentTime = GetTime();
    
    auto it = m_lastSubmission.find(address);
    if (it == m_lastSubmission.end()) {
        return false;  // First submission
    }
    
    // Check if window has expired
    if (currentTime - it->second >= RATE_LIMIT_WINDOW) {
        m_submissionCount[address] = 0;
        m_lastSubmission[address] = currentTime;
        return false;
    }
    
    // Check submission count
    uint8_t reputation = GetReputation(address);
    uint32_t maxSubmissions = MAX_SUBMISSIONS_PER_WINDOW;
    
    // Adjust based on reputation
    if (reputation >= 90) {
        maxSubmissions = 1000;  // Critical: 1000 per minute
    } else if (reputation >= 70) {
        maxSubmissions = 300;   // High: 300 per minute
    } else if (reputation >= 50) {
        maxSubmissions = 60;    // Normal: 60 per minute
    }
    
    return m_submissionCount[address] >= maxSubmissions;
}

void MempoolManager::RecordTransactionSubmission(const uint160& address)
{
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    int64_t currentTime = GetTime();
    
    auto it = m_lastSubmission.find(address);
    if (it == m_lastSubmission.end() || currentTime - it->second >= RATE_LIMIT_WINDOW) {
        m_lastSubmission[address] = currentTime;
        m_submissionCount[address] = 1;
    } else {
        m_submissionCount[address]++;
    }
}

// ===== Fee Calculation =====

CAmount MempoolManager::CalculateEffectiveFee(
    const CTransaction& tx,
    uint64_t gasLimit,
    uint8_t reputation)
{
    if (!m_gasSystem) {
        return 0;
    }
    
    // Calculate base gas cost with trust context
    // Create a minimal trust context for gas calculation
    TrustContext trust(m_db);
    trust.SetCallerReputation(reputation);
    uint64_t baseGasCost = m_gasSystem->CalculateGasCost(gasLimit, trust);
    
    // Convert to CAS (satoshis)
    // Assuming 1 gas = 1 satoshi for simplicity
    // In production, this would use a gas price oracle
    return static_cast<CAmount>(baseGasCost);
}

CAmount MempoolManager::GetMinimumFee(
    const CTransaction& tx,
    uint8_t reputation)
{
    // Minimum fee based on transaction size and reputation
    size_t txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    
    // Base: 1 satoshi per byte
    CAmount baseFee = static_cast<CAmount>(txSize);
    
    // Reputation discount
    if (reputation >= 90) {
        baseFee = baseFee / 2;  // 50% discount for critical reputation
    } else if (reputation >= 70) {
        baseFee = baseFee * 3 / 4;  // 25% discount for high reputation
    }
    
    return baseFee;
}

// ===== Statistics =====

UniValue MempoolManager::GetMempoolStats()
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("total_validated", m_totalValidated);
    result.pushKV("total_accepted", m_totalAccepted);
    result.pushKV("total_rejected", m_totalRejected);
    result.pushKV("free_gas_transactions", m_freeGasTransactions);
    result.pushKV("subsidized_transactions", m_subsidizedTransactions);
    
    double acceptanceRate = m_totalValidated > 0 ? 
        (double)m_totalAccepted / m_totalValidated * 100.0 : 0.0;
    result.pushKV("acceptance_rate_percent", acceptanceRate);
    
    return result;
}

std::map<TransactionPriorityManager::PriorityLevel, uint64_t> 
MempoolManager::GetPriorityDistribution()
{
    std::map<TransactionPriorityManager::PriorityLevel, uint64_t> distribution;
    
    // Initialize all levels to 0
    distribution[TransactionPriorityManager::PriorityLevel::CRITICAL] = 0;
    distribution[TransactionPriorityManager::PriorityLevel::HIGH] = 0;
    distribution[TransactionPriorityManager::PriorityLevel::NORMAL] = 0;
    distribution[TransactionPriorityManager::PriorityLevel::LOW] = 0;
    
    // TODO: Iterate through mempool and count by priority
    // This would require access to the actual mempool
    
    return distribution;
}

// ===== Private Methods =====

uint160 MempoolManager::GetSenderAddress(const CTransaction& tx)
{
    // Extract sender from first input
    // This is a simplified implementation
    // In production, would need proper address extraction
    
    if (tx.vin.empty()) {
        return uint160();
    }
    
    // TODO: Extract actual sender address from input script
    // For now, return a placeholder
    return uint160();
}

uint8_t MempoolManager::GetReputation(const uint160& address)
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

uint64_t MempoolManager::ExtractGasLimit(const CTransaction& tx)
{
    int cvmOutputIndex = FindCVMOpReturn(tx);
    if (cvmOutputIndex < 0) {
        return 0;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[cvmOutputIndex], opType, data)) {
        return 0;
    }
    
    if (opType == CVMOpType::CONTRACT_DEPLOY || opType == CVMOpType::EVM_DEPLOY) {
        CVMDeployData deployData;
        if (deployData.Deserialize(data)) {
            return deployData.gasLimit;
        }
    } else if (opType == CVMOpType::CONTRACT_CALL || opType == CVMOpType::EVM_CALL) {
        CVMCallData callData;
        if (callData.Deserialize(data)) {
            return callData.gasLimit;
        }
    }
    
    return 0;
}

// ===== HAT v2 Consensus Integration =====

void MempoolManager::SetHATConsensusValidator(HATConsensusValidator* validator) {
    m_hatValidator = validator;
}

bool MempoolManager::InitiateHATValidation(
    const CTransaction& tx,
    const HATv2Score& selfReportedScore)
{
    if (!m_hatValidator) {
        LogPrint(BCLog::CVM, "MempoolManager: HAT validator not set\n");
        return false;
    }
    
    // Initiate validation
    ValidationRequest request = m_hatValidator->InitiateValidation(tx, selfReportedScore);
    
    // Select random validators
    std::vector<uint160> validators = m_hatValidator->SelectRandomValidators(
        tx.GetHash(), chainActive.Height());
    
    // Send challenges to validators
    for (const auto& validator : validators) {
        m_hatValidator->SendValidationChallenge(validator, request);
    }
    
    LogPrint(BCLog::CVM, "MempoolManager: Initiated HAT validation for tx %s with %zu validators\n",
             tx.GetHash().ToString(), validators.size());
    
    return true;
}

bool MempoolManager::ProcessValidatorResponse(const ValidationResponse& response) {
    if (!m_hatValidator) {
        return false;
    }
    
    // Process response through HAT validator
    if (!m_hatValidator->ProcessValidatorResponse(response)) {
        return false;
    }
    
    // Check if we have enough responses to determine consensus
    // This would require accessing the validation session
    // For now, just log the response
    LogPrint(BCLog::CVM, "MempoolManager: Processed validator response for tx %s from %s\n",
             response.txHash.ToString(), response.validatorAddress.ToString());
    
    return true;
}

bool MempoolManager::IsHATValidationComplete(const uint256& txHash) {
    if (!m_hatValidator) {
        return true;  // If no HAT validator, consider complete
    }
    
    TransactionState state = m_hatValidator->GetTransactionState(txHash);
    return (state == TransactionState::VALIDATED || state == TransactionState::REJECTED);
}

TransactionState MempoolManager::GetHATValidationState(const uint256& txHash) {
    if (!m_hatValidator) {
        return TransactionState::VALIDATED;  // Default to validated if no HAT validator
    }
    
    return m_hatValidator->GetTransactionState(txHash);
}

bool MempoolManager::HandleDAOResolution(const uint256& txHash, bool approved) {
    if (!m_hatValidator) {
        return false;
    }
    
    // Update transaction state based on DAO decision
    if (approved) {
        m_hatValidator->UpdateMempoolState(txHash, TransactionState::VALIDATED);
        LogPrint(BCLog::CVM, "MempoolManager: DAO approved tx %s\n", txHash.ToString());
    } else {
        m_hatValidator->UpdateMempoolState(txHash, TransactionState::REJECTED);
        LogPrint(BCLog::CVM, "MempoolManager: DAO rejected tx %s\n", txHash.ToString());
    }
    
    return true;
}

} // namespace CVM
