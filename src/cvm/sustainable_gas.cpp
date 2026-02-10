// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/sustainable_gas.h>
#include <cvm/trust_context.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

namespace cvm {

// EVM opcode gas costs (base Ethereum costs / 100)
static const uint64_t GAS_ZERO = 0;
static const uint64_t GAS_BASE = 2;
static const uint64_t GAS_VERYLOW = 3;
static const uint64_t GAS_LOW = 5;
static const uint64_t GAS_MID = 8;
static const uint64_t GAS_HIGH = 10;
static const uint64_t GAS_JUMPDEST = 1;
static const uint64_t GAS_SLOAD = 200;
static const uint64_t GAS_SSTORE_SET = 200;
static const uint64_t GAS_SSTORE_RESET = 50;
static const uint64_t GAS_CREATE = 320;
static const uint64_t GAS_CALL = 7;
static const uint64_t GAS_MEMORY = 3;
static const uint64_t GAS_SHA3 = 30;
static const uint64_t GAS_SHA3_WORD = 6;
static const uint64_t GAS_COPY = 3;
static const uint64_t GAS_BLOCKHASH = 20;
static const uint64_t GAS_EXTCODECOPY = 7;

SustainableGasSystem::SustainableGasSystem()
    : m_db(nullptr)
    , gasParams()
    , m_lastRecordedBlock(0)
{
}

SustainableGasSystem::~SustainableGasSystem()
{
}

// ===== Reputation-Adjusted Gas Costs =====

uint64_t SustainableGasSystem::CalculateGasCost(uint8_t opcode, const CVM::TrustContext& trust)
{
    // Get base cost for opcode (already 100x lower than Ethereum)
    uint64_t baseCost = GetBaseOpcodeCost(opcode);
    
    // Get caller reputation
    uint8_t callerReputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // Apply reputation multiplier (high reputation = lower cost)
    double repMultiplier = CalculateReputationMultiplier(callerReputation);
    
    // Calculate adjusted cost
    uint64_t adjustedCost = static_cast<uint64_t>(baseCost * repMultiplier);
    
    // Free gas for high reputation
    if (IsEligibleForFreeGas(callerReputation)) {
        return 0;
    }
    
    return adjustedCost;
}

uint64_t SustainableGasSystem::CalculateStorageCost(bool isWrite, const CVM::TrustContext& trust)
{
    // Base storage costs (100x lower than Ethereum)
    uint64_t baseCost = isWrite ? GAS_SSTORE_SET : GAS_SLOAD;
    
    // Get caller reputation
    uint8_t callerReputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // Apply reputation multiplier
    double repMultiplier = CalculateReputationMultiplier(callerReputation);
    
    // Calculate adjusted cost
    uint64_t adjustedCost = static_cast<uint64_t>(baseCost * repMultiplier);
    
    // Free gas for high reputation
    if (IsEligibleForFreeGas(callerReputation)) {
        return 0;
    }
    
    return adjustedCost;
}

uint64_t SustainableGasSystem::GetPredictableGasPrice(uint8_t reputation, uint64_t networkLoad)
{
    // Start with base gas price (100x lower than Ethereum)
    uint64_t basePrice = gasParams.baseGasPrice;
    
    // Apply reputation discount (50% to 100% of base)
    double repMultiplier = CalculateReputationMultiplier(reputation);
    uint64_t reputationAdjustedPrice = static_cast<uint64_t>(basePrice * repMultiplier);
    
    // Apply network load factor (maximum 2x variation)
    // Network load is 0-100, we map to 1.0-2.0 multiplier
    double loadMultiplier = 1.0 + (networkLoad / 100.0);
    
    // Ensure we don't exceed maximum 2x variation
    loadMultiplier = std::min(loadMultiplier, static_cast<double>(gasParams.maxPriceVariation));
    
    uint64_t finalPrice = static_cast<uint64_t>(reputationAdjustedPrice * loadMultiplier);
    
    return finalPrice;
}

// ===== Free Gas System =====

uint64_t SustainableGasSystem::GetFreeGasAllowance(uint8_t reputation)
{
    if (!IsEligibleForFreeGas(reputation)) {
        return 0;
    }
    
    // Free gas allowance scales with reputation above threshold
    // 80 reputation = 1M gas, 100 reputation = 5M gas
    uint64_t baseAllowance = 1000000;
    uint64_t bonusAllowance = (reputation - gasParams.freeGasThreshold) * 200000;
    
    return baseAllowance + bonusAllowance;
}

// ===== Anti-Congestion Through Trust =====

bool SustainableGasSystem::ShouldPrioritizeTransaction(const CVM::TrustContext& trust, uint64_t networkLoad)
{
    // Get caller reputation
    uint8_t callerReputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // High reputation addresses get priority during congestion
    if (callerReputation >= 90) {
        return true;  // Always prioritize 90+ reputation
    }
    
    if (callerReputation >= 70 && networkLoad > 50) {
        return true;  // Prioritize 70+ reputation during high load
    }
    
    return false;
}

bool SustainableGasSystem::CheckReputationThreshold(uint8_t reputation, OperationType opType)
{
    switch (opType) {
        case OperationType::STANDARD:
            return reputation >= 0;  // No threshold for standard operations
            
        case OperationType::HIGH_FREQUENCY:
            return reputation >= 50;  // Require 50+ for high-frequency
            
        case OperationType::STORAGE_INTENSIVE:
            return reputation >= 40;  // Require 40+ for storage-intensive
            
        case OperationType::COMPUTE_INTENSIVE:
            return reputation >= 30;  // Require 30+ for compute-intensive
            
        case OperationType::CROSS_CHAIN:
            return reputation >= 60;  // Require 60+ for cross-chain
            
        default:
            return true;
    }
}

void SustainableGasSystem::ImplementTrustBasedRateLimit(const uint160& address, uint8_t reputation)
{
    // Get or create rate limit state
    RateLimitState& state = rateLimits[address];
    state.reputation = reputation;
    
    // Higher reputation = higher rate limits
    // This is enforced by callers checking operation counts
}

// ===== Gas Subsidies and Rebates =====

uint64_t SustainableGasSystem::CalculateSubsidy(const CVM::TrustContext& trust, bool isBeneficialOp)
{
    if (!isBeneficialOp) {
        return 0;
    }
    
    // Get caller reputation
    uint8_t callerReputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // Subsidy scales with reputation
    // 50 reputation = 10% subsidy, 100 reputation = 50% subsidy
    uint64_t subsidyPercent = callerReputation / 2;
    
    return subsidyPercent;  // Return as percentage
}

void SustainableGasSystem::ProcessGasRebate(const uint160& address, uint64_t amount)
{
    // Add to subsidy pool for this address
    gasSubsidyPools[address] += amount;
}

bool SustainableGasSystem::IsNetworkBeneficialOperation(uint8_t opcode, const CVM::TrustContext& trust)
{
    // Get caller reputation
    uint8_t callerReputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // Operations that improve network health
    // For now, consider high-reputation contract calls as beneficial
    return callerReputation >= 70;
}

// ===== Community Gas Pools =====

void SustainableGasSystem::ContributeToGasPool(const uint160& contributor, uint64_t amount, const std::string& poolId)
{
    communityGasPools[poolId] += amount;
}

bool SustainableGasSystem::UseGasPool(const std::string& poolId, uint64_t amount, const CVM::TrustContext& trust)
{
    auto it = communityGasPools.find(poolId);
    if (it == communityGasPools.end() || it->second < amount) {
        return false;
    }
    
    // Get caller reputation
    uint8_t callerReputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // Require minimum reputation to use community pools
    if (callerReputation < 30) {
        return false;
    }
    
    it->second -= amount;
    return true;
}

// ===== Business-Friendly Pricing =====

void SustainableGasSystem::CreatePriceGuarantee(const uint160& businessAddr, uint64_t guaranteedPrice, 
                                               uint64_t duration, uint8_t minReputation)
{
    // Create price guarantee (expiration is current block + duration)
    // Note: We don't have access to current block height here, so caller must provide absolute expiration
    priceGuarantees[businessAddr] = PriceGuarantee(guaranteedPrice, duration, minReputation);
}

bool SustainableGasSystem::HasPriceGuarantee(const uint160& address, uint64_t& guaranteedPrice)
{
    auto it = priceGuarantees.find(address);
    if (it == priceGuarantees.end()) {
        return false;
    }
    
    // Note: Expiration check should be done by caller with current block height
    guaranteedPrice = it->second.guaranteedPrice;
    return true;
}

bool SustainableGasSystem::HasPriceGuarantee(const uint160& address, uint64_t& guaranteedPrice, int64_t currentBlock)
{
    auto it = priceGuarantees.find(address);
    if (it == priceGuarantees.end()) {
        return false;
    }
    
    // Check if expired
    if (currentBlock >= static_cast<int64_t>(it->second.expirationBlock)) {
        // Expired - remove from map
        priceGuarantees.erase(it);
        return false;
    }
    
    guaranteedPrice = it->second.guaranteedPrice;
    return true;
}

bool SustainableGasSystem::GetPriceGuaranteeInfo(const uint160& address, PriceGuarantee& guarantee)
{
    auto it = priceGuarantees.find(address);
    if (it == priceGuarantees.end()) {
        return false;
    }
    
    guarantee = it->second;
    return true;
}

void SustainableGasSystem::UpdateBaseCosts(double networkTrustDensity)
{
    // As network trust density increases, base costs can decrease
    // Trust density of 1.0 = 50% cost reduction
    double costMultiplier = 1.0 - (networkTrustDensity * 0.5);
    
    // Update base gas price
    uint64_t originalBase = 10000000;  // 0.01 gwei
    gasParams.baseGasPrice = static_cast<uint64_t>(originalBase * costMultiplier);
}

double SustainableGasSystem::CalculateNetworkTrustDensity()
{
    // If no database is available, return default trust density
    if (!m_db) {
        return 0.5;  // 50% trust density as default
    }
    
    // Query reputation system for network-wide statistics
    // Trust density is calculated as the ratio of addresses with positive reputation
    // to total addresses with any reputation score
    
    CVM::ReputationSystem repSystem(*m_db);
    
    // Get all reputation keys from database
    std::vector<std::string> reputationKeys = m_db->ListKeysWithPrefix("reputation_");
    
    if (reputationKeys.empty()) {
        return 0.5;  // Default if no reputation data
    }
    
    int64_t totalAddresses = 0;
    int64_t positiveReputationAddresses = 0;
    int64_t totalReputationScore = 0;
    
    for (const auto& key : reputationKeys) {
        std::vector<uint8_t> data;
        if (m_db->ReadGeneric(key, data) && data.size() >= sizeof(int64_t)) {
            // Deserialize reputation score
            int64_t score = 0;
            memcpy(&score, data.data(), sizeof(int64_t));
            
            totalAddresses++;
            totalReputationScore += score;
            
            // Count addresses with positive reputation (score > 0)
            if (score > 0) {
                positiveReputationAddresses++;
            }
        }
    }
    
    if (totalAddresses == 0) {
        return 0.5;  // Default if no valid reputation data
    }
    
    // Calculate trust density as ratio of positive reputation addresses
    // This gives a value between 0.0 and 1.0
    double trustDensity = static_cast<double>(positiveReputationAddresses) / totalAddresses;
    
    // Also factor in average reputation score
    // Normalize average score from [-10000, +10000] to [0, 1]
    double avgScore = static_cast<double>(totalReputationScore) / totalAddresses;
    double normalizedAvgScore = (avgScore + 10000.0) / 20000.0;
    normalizedAvgScore = std::max(0.0, std::min(1.0, normalizedAvgScore));
    
    // Combine both metrics (weighted average)
    // 60% weight on positive address ratio, 40% on average score
    double combinedDensity = (trustDensity * 0.6) + (normalizedAvgScore * 0.4);
    
    return combinedDensity;
}

double SustainableGasSystem::GetCurrentPriceMultiplier()
{
    // If we don't have enough data, return default multiplier
    if (m_recentBlockGas.empty()) {
        return 1.0;
    }
    
    // Calculate average gas usage over tracked blocks
    uint64_t totalGas = std::accumulate(m_recentBlockGas.begin(), m_recentBlockGas.end(), 0ULL);
    double avgGas = static_cast<double>(totalGas) / m_recentBlockGas.size();
    
    // Calculate multiplier based on congestion vs target
    // If avgGas == TARGET_GAS_PER_BLOCK, multiplier = 1.0
    // If avgGas > TARGET_GAS_PER_BLOCK, multiplier > 1.0 (up to 2.0)
    // If avgGas < TARGET_GAS_PER_BLOCK, multiplier < 1.0 (down to 0.5)
    double congestionRatio = avgGas / static_cast<double>(TARGET_GAS_PER_BLOCK);
    
    // Linear interpolation:
    // congestionRatio = 0.0 -> multiplier = 0.5
    // congestionRatio = 1.0 -> multiplier = 1.0
    // congestionRatio = 2.0 -> multiplier = 2.0
    double multiplier = 0.5 + (congestionRatio * 0.5);
    
    // Clamp to [0.5, 2.0] range as per requirements
    multiplier = std::max(0.5, std::min(2.0, multiplier));
    
    return multiplier;
}

void SustainableGasSystem::RecordBlockGasUsage(int64_t blockHeight, uint64_t gasUsed)
{
    // Only record if this is a new block (avoid duplicates)
    if (blockHeight <= m_lastRecordedBlock && m_lastRecordedBlock > 0) {
        return;
    }
    
    m_lastRecordedBlock = blockHeight;
    
    // Add gas usage to tracking deque
    m_recentBlockGas.push_back(gasUsed);
    
    // Maintain maximum of 100 blocks
    while (m_recentBlockGas.size() > MAX_TRACKED_BLOCKS) {
        m_recentBlockGas.pop_front();
    }
}

void SustainableGasSystem::ResetRateLimits()
{
    rateLimits.clear();
}

// ===== Private Helper Methods =====

double SustainableGasSystem::CalculateReputationMultiplier(uint8_t reputation)
{
    // Reputation 0 = 1.0x (full cost)
    // Reputation 50 = 0.75x (25% discount)
    // Reputation 100 = 0.5x (50% discount)
    
    // Linear interpolation from 1.0 to 0.5
    double multiplier = 1.0 - (reputation / 200.0);
    
    // Clamp to reasonable range
    multiplier = std::max(0.5, std::min(1.0, multiplier));
    
    return multiplier;
}

uint64_t SustainableGasSystem::GetBaseOpcodeCost(uint8_t opcode)
{
    // EVM opcode gas costs (already 100x lower than Ethereum)
    // Based on EVM opcode specification
    
    // 0x00-0x0f: Stop and Arithmetic Operations
    if (opcode == 0x00) return GAS_ZERO;  // STOP
    if (opcode >= 0x01 && opcode <= 0x0b) return GAS_VERYLOW;  // ADD, MUL, SUB, DIV, etc.
    if (opcode >= 0x10 && opcode <= 0x1a) return GAS_VERYLOW;  // LT, GT, EQ, etc.
    
    // 0x20: SHA3
    if (opcode == 0x20) return GAS_SHA3;
    
    // 0x30-0x3f: Environmental Information
    if (opcode >= 0x30 && opcode <= 0x3f) return GAS_BASE;
    
    // 0x40: BLOCKHASH
    if (opcode == 0x40) return GAS_BLOCKHASH;
    
    // 0x50-0x5f: Stack, Memory, Storage and Flow Operations
    if (opcode >= 0x50 && opcode <= 0x5f) {
        if (opcode == 0x54) return GAS_SLOAD;  // SLOAD
        if (opcode == 0x55) return GAS_SSTORE_SET;  // SSTORE
        if (opcode == 0x56 || opcode == 0x57) return GAS_MID;  // JUMP, JUMPI
        if (opcode == 0x5b) return GAS_JUMPDEST;  // JUMPDEST
        return GAS_VERYLOW;
    }
    
    // 0x60-0x7f: Push Operations
    if (opcode >= 0x60 && opcode <= 0x7f) return GAS_VERYLOW;
    
    // 0x80-0x8f: Duplication Operations
    if (opcode >= 0x80 && opcode <= 0x8f) return GAS_VERYLOW;
    
    // 0x90-0x9f: Exchange Operations
    if (opcode >= 0x90 && opcode <= 0x9f) return GAS_VERYLOW;
    
    // 0xa0-0xa4: Logging Operations
    if (opcode >= 0xa0 && opcode <= 0xa4) return GAS_LOW;
    
    // 0xf0-0xff: System Operations
    if (opcode == 0xf0) return GAS_CREATE;  // CREATE
    if (opcode == 0xf1 || opcode == 0xf2 || opcode == 0xf4) return GAS_CALL;  // CALL, CALLCODE, DELEGATECALL
    if (opcode == 0xf3) return GAS_ZERO;  // RETURN
    if (opcode == 0xfa) return GAS_CALL;  // STATICCALL
    if (opcode == 0xfd) return GAS_ZERO;  // REVERT
    if (opcode == 0xff) return GAS_HIGH;  // SELFDESTRUCT
    
    // Default
    return GAS_BASE;
}

bool SustainableGasSystem::IsHighFrequencyOperation(uint8_t opcode)
{
    // Operations that are typically called frequently
    return (opcode >= 0x60 && opcode <= 0x7f) ||  // PUSH operations
           (opcode >= 0x80 && opcode <= 0x8f) ||  // DUP operations
           (opcode >= 0x90 && opcode <= 0x9f);    // SWAP operations
}

bool SustainableGasSystem::IsStorageIntensiveOperation(uint8_t opcode)
{
    // Storage operations
    return opcode == 0x54 ||  // SLOAD
           opcode == 0x55;     // SSTORE
}

bool SustainableGasSystem::IsComputeIntensiveOperation(uint8_t opcode)
{
    // Compute-intensive operations
    return opcode == 0x20 ||  // SHA3
           opcode == 0x08 ||  // ADDMOD
           opcode == 0x09 ||  // MULMOD
           opcode == 0x0a ||  // EXP
           opcode == 0x0b;    // SIGNEXTEND
}

} // namespace cvm
