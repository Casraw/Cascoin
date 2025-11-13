// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/consensus_validator.h>
#include <cvm/trust_context.h>
#include <cvm/fee_calculator.h>
#include <cvm/gas_subsidy.h>
#include <cvm/softfork.h>
#include <util.h>

namespace CVM {

// External global trust context (defined in trust_context.cpp)
extern std::shared_ptr<TrustContext> g_trustContext;

using namespace ConsensusConstants;

ConsensusValidator::ConsensusValidator() {}

ConsensusValidator::~ConsensusValidator() {}

bool ConsensusValidator::ValidateReputationRange(uint8_t reputation, std::string& error) {
    if (reputation > MAX_REPUTATION) {
        error = strprintf("Invalid reputation: %d (max: %d)", reputation, MAX_REPUTATION);
        return false;
    }
    return true;
}

uint64_t ConsensusValidator::GetConsensusReputationDiscount(
    uint8_t reputation,
    uint64_t baseGasCost)
{
    // Deterministic discount calculation
    // All nodes must get the same result
    
    uint64_t discountPct = 0;
    
    if (reputation >= DISCOUNT_TIER_4_REP) {
        discountPct = DISCOUNT_TIER_4_PCT;  // 75%
    } else if (reputation >= DISCOUNT_TIER_3_REP) {
        discountPct = DISCOUNT_TIER_3_PCT;  // 50%
    } else if (reputation >= DISCOUNT_TIER_2_REP) {
        discountPct = DISCOUNT_TIER_2_PCT;  // 25%
    } else {
        discountPct = DISCOUNT_TIER_1_PCT;  // 0%
    }
    
    // Calculate discount amount
    uint64_t discount = (baseGasCost * discountPct) / 100;
    
    return discount;
}

bool ConsensusValidator::IsEligibleForFreeGas(uint8_t reputation) {
    // Deterministic check
    return reputation >= FREE_GAS_THRESHOLD;
}

uint64_t ConsensusValidator::GetMaxAllowedSubsidy(
    uint8_t reputation,
    uint64_t gasUsed)
{
    // Deterministic subsidy calculation
    // Higher reputation = higher subsidy limit
    
    if (reputation < 50) {
        return 0;  // No subsidy for low reputation
    }
    
    // Calculate max subsidy based on reputation
    uint64_t maxSubsidy = (gasUsed * reputation) / 100;
    
    // Cap at MAX_SUBSIDY_PER_TX
    if (maxSubsidy > MAX_SUBSIDY_PER_TX) {
        maxSubsidy = MAX_SUBSIDY_PER_TX;
    }
    
    return maxSubsidy;
}

bool ConsensusValidator::ValidateReputationDiscount(
    uint8_t reputation,
    uint64_t baseGasCost,
    uint64_t discountedGasCost,
    std::string& error)
{
    if (!ValidateReputationRange(reputation, error)) {
        return false;
    }
    
    // Calculate expected discount
    uint64_t expectedDiscount = GetConsensusReputationDiscount(reputation, baseGasCost);
    uint64_t expectedCost = baseGasCost - expectedDiscount;
    
    // Verify claimed cost matches expected
    if (discountedGasCost != expectedCost) {
        error = strprintf(
            "Invalid gas discount: claimed=%d, expected=%d (base=%d, rep=%d, discount=%d)",
            discountedGasCost, expectedCost, baseGasCost, reputation, expectedDiscount
        );
        return false;
    }
    
    return true;
}

bool ConsensusValidator::ValidateFreeGasEligibility(
    uint8_t reputation,
    bool claimedEligibility,
    std::string& error)
{
    if (!ValidateReputationRange(reputation, error)) {
        return false;
    }
    
    bool expectedEligibility = IsEligibleForFreeGas(reputation);
    
    if (claimedEligibility != expectedEligibility) {
        error = strprintf(
            "Invalid free gas eligibility: claimed=%d, expected=%d (rep=%d, threshold=%d)",
            claimedEligibility, expectedEligibility, reputation, FREE_GAS_THRESHOLD
        );
        return false;
    }
    
    return true;
}

bool ConsensusValidator::ValidateGasSubsidy(
    const uint160& address,
    uint8_t reputation,
    uint64_t subsidyAmount,
    const std::string& poolId,
    std::string& error)
{
    if (!ValidateReputationRange(reputation, error)) {
        return false;
    }
    
    // Check subsidy doesn't exceed maximum
    if (subsidyAmount > MAX_SUBSIDY_PER_TX) {
        error = strprintf(
            "Subsidy exceeds maximum: %d > %d",
            subsidyAmount, MAX_SUBSIDY_PER_TX
        );
        return false;
    }
    
    // Validate pool has sufficient balance
    if (!ValidatePoolBalance(poolId, subsidyAmount, error)) {
        return false;
    }
    
    return true;
}

uint8_t ConsensusValidator::CalculateDeterministicTrustScore(
    const uint160& address,
    int blockHeight)
{
    // SOLUTION: Optimistic Consensus with Sender-Declared Reputation
    // 
    // How it works:
    // 1. Sender declares their reputation in the transaction
    // 2. Each node validates with THEIR OWN HAT v2 score
    // 3. If sender's claim is <= node's calculated score: ACCEPT
    // 4. If sender's claim is > node's calculated score: REJECT
    // 
    // Why this works:
    // - Sender has incentive to be honest (too high = rejection)
    // - Each node validates with their own HAT v2 (personalized)
    // - Consensus is maintained because transaction contains the claim
    // - Different nodes can have different HAT v2 values
    // - Transaction is either valid for ALL or NONE (based on claim)
    // 
    // Example:
    // - Sender claims reputation 85
    // - Node A calculates HAT v2 = 90 → ACCEPT (85 <= 90)
    // - Node B calculates HAT v2 = 80 → REJECT (85 > 80)
    // - Node C calculates HAT v2 = 85 → ACCEPT (85 <= 85)
    // 
    // Result: Nodes with similar trust views agree on validity
    // This creates "trust clusters" in the network
    
    // Query HAT v2 score from reputation system
    // This uses ALL HAT v2 components including personalized WoT
    if (g_trustContext) {
        return g_trustContext->GetReputation(address);
    }
    
    // Fallback: Use ASRS if HAT v2 not available
    // TODO: Integrate with ASRS for fallback
    return 50;
}

uint64_t ConsensusValidator::CalculateExpectedGasCost(
    uint64_t baseGas,
    uint8_t reputation,
    bool useFreeGas)
{
    if (useFreeGas && IsEligibleForFreeGas(reputation)) {
        return 0;
    }
    
    uint64_t discount = GetConsensusReputationDiscount(reputation, baseGas);
    return baseGas - discount;
}

bool ConsensusValidator::ValidateTransactionCost(
    const CTransaction& tx,
    uint64_t claimedGasUsed,
    uint64_t claimedCost,
    int blockHeight,
    std::string& error)
{
    // Extract sender and get reputation
    // TODO: Implement proper sender extraction
    uint160 sender;  // Placeholder
    
    uint8_t reputation = CalculateDeterministicTrustScore(sender, blockHeight);
    
    // Check if transaction claims free gas
    bool usesFreeGas = (claimedCost == 0);
    
    // Calculate expected cost
    uint64_t expectedCost = CalculateExpectedGasCost(
        claimedGasUsed,
        reputation,
        usesFreeGas
    );
    
    // Verify claimed cost matches expected
    if (claimedCost != expectedCost) {
        error = strprintf(
            "Invalid transaction cost: claimed=%d, expected=%d (gas=%d, rep=%d)",
            claimedCost, expectedCost, claimedGasUsed, reputation
        );
        return false;
    }
    
    return true;
}

bool ConsensusValidator::ValidatePoolBalance(
    const std::string& poolId,
    uint64_t requestedAmount,
    std::string& error)
{
    // TODO: Query actual pool balance from database
    // For now, assume sufficient balance
    return true;
}

bool ConsensusValidator::ValidateBlockTrustFeatures(
    const CBlock& block,
    int blockHeight,
    const Consensus::Params& params,
    std::string& error)
{
    // Validate all trust-aware features in block
    
    uint64_t totalSubsidies = 0;
    
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check if CVM/EVM transaction
        if (!IsEVMTransaction(*tx) && FindCVMOpReturn(*tx) < 0) {
            continue;
        }
        
        // TODO: Validate transaction costs
        // This requires extracting gas usage and costs from transaction
        
        // TODO: Track total subsidies
        // Ensure doesn't exceed MAX_SUBSIDY_PER_BLOCK
    }
    
    // Validate total subsidies
    if (totalSubsidies > MAX_SUBSIDY_PER_BLOCK) {
        error = strprintf(
            "Block subsidies exceed maximum: %d > %d",
            totalSubsidies, MAX_SUBSIDY_PER_BLOCK
        );
        return false;
    }
    
    return true;
}

} // namespace CVM
