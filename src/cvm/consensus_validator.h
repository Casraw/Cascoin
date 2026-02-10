// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CONSENSUS_VALIDATOR_H
#define CASCOIN_CVM_CONSENSUS_VALIDATOR_H

#include <uint256.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <consensus/params.h>

namespace CVM {

// Forward declarations
class TrustContext;
class FeeCalculator;

/**
 * Consensus Validator for Trust-Aware Features
 * 
 * Ensures all nodes agree on:
 * - Reputation-based gas discounts
 * - Free gas eligibility
 * - Gas subsidy application
 * - Trust-adjusted transaction costs
 * 
 * Requirements: 10.1, 10.2, 6.1, 6.3
 */
class ConsensusValidator {
public:
    ConsensusValidator();
    ~ConsensusValidator();
    
    /**
     * Validate reputation-based gas discount
     * All nodes must calculate the same discount
     */
    bool ValidateReputationDiscount(
        uint8_t reputation,
        uint64_t baseGasCost,
        uint64_t discountedGasCost,
        std::string& error
    );
    
    /**
     * Validate free gas eligibility
     * Deterministic check based on reputation threshold
     */
    bool ValidateFreeGasEligibility(
        uint8_t reputation,
        bool claimedEligibility,
        std::string& error
    );
    
    /**
     * Validate gas subsidy application
     * Ensure subsidy rules are followed consistently
     */
    bool ValidateGasSubsidy(
        const uint160& address,
        uint8_t reputation,
        uint64_t subsidyAmount,
        const std::string& poolId,
        std::string& error
    );
    
    /**
     * Calculate deterministic trust score for consensus
     * All nodes must get the same result
     */
    uint8_t CalculateDeterministicTrustScore(
        const uint160& address,
        int blockHeight
    );
    
    /**
     * Validate trust-adjusted transaction cost
     * Ensure all nodes agree on final cost
     */
    bool ValidateTransactionCost(
        const CTransaction& tx,
        uint64_t claimedGasUsed,
        uint64_t claimedCost,
        int blockHeight,
        std::string& error
    );
    
    /**
     * Validate entire block's trust-aware features
     * Called from ConnectBlock()
     */
    bool ValidateBlockTrustFeatures(
        const CBlock& block,
        int blockHeight,
        const Consensus::Params& params,
        std::string& error
    );
    
    /**
     * Get consensus-valid reputation discount
     * Deterministic calculation
     */
    static uint64_t GetConsensusReputationDiscount(
        uint8_t reputation,
        uint64_t baseGasCost
    );
    
    /**
     * Check if address is eligible for free gas (consensus)
     * Deterministic check
     */
    static bool IsEligibleForFreeGas(uint8_t reputation);
    
    /**
     * Get maximum allowed subsidy (consensus)
     * Deterministic calculation
     */
    static uint64_t GetMaxAllowedSubsidy(
        uint8_t reputation,
        uint64_t gasUsed
    );
    
    /**
     * Extract sender address from transaction inputs
     * 
     * Parses P2PKH and P2WPKH scripts to extract the sender address.
     * Uses the first input for sender determination.
     * 
     * Requirements: 9.2
     * 
     * @param tx Transaction to extract sender from
     * @param senderOut Output parameter for the extracted sender address
     * @return true if sender was successfully extracted
     */
    static bool ExtractSenderAddress(const CTransaction& tx, uint160& senderOut);
    
    /**
     * Get pool balance from database
     * 
     * Queries LevelDB with key "pool_<id>_balance" and returns cached value on failure.
     * 
     * Requirements: 9.3
     * 
     * @param poolId The pool identifier
     * @return The pool balance, or 0 if not found
     */
    static CAmount GetPoolBalance(const std::string& poolId);
    
    /**
     * Extract gas usage and costs from CVM transaction
     * 
     * Parses OP_RETURN data to extract gas information from CVM transactions.
     * Gas info is encoded in the OP_RETURN data for contract deployments and calls.
     * 
     * Requirements: 9.4
     * 
     * @param tx Transaction to extract gas info from
     * @param gasUsed Output parameter for gas used
     * @param gasCost Output parameter for gas cost
     * @return true if gas info was successfully extracted
     */
    static bool ExtractGasInfo(const CTransaction& tx, uint64_t& gasUsed, CAmount& gasCost);
    
private:
    /**
     * Validate reputation score is in valid range
     */
    bool ValidateReputationRange(uint8_t reputation, std::string& error);
    
    /**
     * Calculate expected gas cost with reputation
     */
    uint64_t CalculateExpectedGasCost(
        uint64_t baseGas,
        uint8_t reputation,
        bool useFreeGas
    );
    
    /**
     * Validate subsidy pool has sufficient balance
     */
    bool ValidatePoolBalance(
        const std::string& poolId,
        uint64_t requestedAmount,
        std::string& error
    );
};

/**
 * Consensus constants for trust-aware features
 */
namespace ConsensusConstants {
    // Reputation thresholds (must be deterministic)
    constexpr uint8_t FREE_GAS_THRESHOLD = 80;
    constexpr uint8_t MIN_REPUTATION = 0;
    constexpr uint8_t MAX_REPUTATION = 100;
    
    // Gas discount tiers (reputation -> discount percentage)
    constexpr uint8_t DISCOUNT_TIER_1_REP = 50;  // 0% discount
    constexpr uint8_t DISCOUNT_TIER_2_REP = 70;  // 25% discount
    constexpr uint8_t DISCOUNT_TIER_3_REP = 80;  // 50% discount
    constexpr uint8_t DISCOUNT_TIER_4_REP = 90;  // 75% discount
    
    constexpr uint64_t DISCOUNT_TIER_1_PCT = 0;
    constexpr uint64_t DISCOUNT_TIER_2_PCT = 25;
    constexpr uint64_t DISCOUNT_TIER_3_PCT = 50;
    constexpr uint64_t DISCOUNT_TIER_4_PCT = 75;
    
    // Free gas limits
    constexpr uint64_t FREE_GAS_ALLOWANCE_MIN = 1000000;   // 1M gas
    constexpr uint64_t FREE_GAS_ALLOWANCE_MAX = 5000000;   // 5M gas
    
    // Subsidy limits
    constexpr uint64_t MAX_SUBSIDY_PER_TX = 100000;        // Max subsidy per transaction
    constexpr uint64_t MAX_SUBSIDY_PER_BLOCK = 10000000;   // Max total subsidies per block
}

} // namespace CVM

#endif // CASCOIN_CVM_CONSENSUS_VALIDATOR_H
