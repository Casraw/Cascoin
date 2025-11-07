// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_SUSTAINABLE_GAS_H
#define CASCOIN_CVM_SUSTAINABLE_GAS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <uint256.h>

// Forward declarations
class EnhancedStorage;

namespace CVM {
    class TrustContext;
}

namespace cvm {

/**
 * Operation types for reputation-based gas pricing
 */
enum class OperationType {
    STANDARD,
    HIGH_FREQUENCY,
    STORAGE_INTENSIVE,
    COMPUTE_INTENSIVE,
    CROSS_CHAIN
};

/**
 * Gas parameters for sustainable pricing system
 * Base costs are 100x lower than Ethereum mainnet
 */
struct GasParameters {
    uint64_t baseGasPrice;              // 100x lower than Ethereum (e.g., 0.01 gwei)
    double reputationMultiplier;        // Reputation-based discount (0.5 to 1.0)
    uint64_t maxPriceVariation;         // Maximum 2x variation
    uint8_t freeGasThreshold;           // 80+ reputation gets free gas
    uint64_t subCentTarget;             // Target sub-cent costs in wei
    
    GasParameters() 
        : baseGasPrice(10000000),       // 0.01 gwei (100x lower than typical Ethereum)
          reputationMultiplier(1.0),
          maxPriceVariation(2),
          freeGasThreshold(80),
          subCentTarget(1000000000000000) // ~$0.01 at $2000/ETH
    {}
};

/**
 * Price guarantee for business accounts
 */
struct PriceGuarantee {
    uint64_t guaranteedPrice;
    uint64_t expirationBlock;
    uint8_t minReputation;
    
    PriceGuarantee() : guaranteedPrice(0), expirationBlock(0), minReputation(0) {}
    PriceGuarantee(uint64_t price, uint64_t expiration, uint8_t minRep)
        : guaranteedPrice(price), expirationBlock(expiration), minReputation(minRep) {}
};

/**
 * Rate limiting state for reputation-based throttling
 */
struct RateLimitState {
    uint64_t lastOperationBlock;
    uint32_t operationCount;
    uint8_t reputation;
    
    RateLimitState() : lastOperationBlock(0), operationCount(0), reputation(0) {}
};

/**
 * Sustainable Gas System
 * 
 * Implements reputation-based gas pricing with the following features:
 * - Base gas costs 100x lower than Ethereum mainnet
 * - Reputation-based gas cost multipliers (high reputation = lower costs)
 * - Predictable pricing with maximum 2x variation
 * - Free gas for addresses with 80+ reputation
 * - Anti-congestion through trust instead of high fees
 * 
 * Requirements: 6.1, 6.2, 6.3, 16.1-16.4, 17.1-17.4, 18.1-18.5
 */
class SustainableGasSystem {
public:
    SustainableGasSystem();
    ~SustainableGasSystem();
    
    // ===== Reputation-Adjusted Gas Costs (Requirements 6.1, 6.2, 18.1, 18.3) =====
    
    /**
     * Calculate gas cost for an EVM opcode with reputation adjustment
     * Base costs are 100x lower than Ethereum, further reduced by reputation
     * 
     * @param opcode The EVM opcode being executed
     * @param trust Trust context containing caller reputation
     * @return Adjusted gas cost
     */
    uint64_t CalculateGasCost(uint8_t opcode, const CVM::TrustContext& trust);
    
    /**
     * Calculate storage operation gas cost with reputation adjustment
     * 
     * @param isWrite True for SSTORE, false for SLOAD
     * @param trust Trust context containing caller reputation
     * @return Adjusted storage gas cost
     */
    uint64_t CalculateStorageCost(bool isWrite, const CVM::TrustContext& trust);
    
    /**
     * Get predictable gas price with maximum 2x variation
     * 
     * @param reputation Caller's reputation score (0-100)
     * @param networkLoad Current network load (0-100)
     * @return Predictable gas price
     */
    uint64_t GetPredictableGasPrice(uint8_t reputation, uint64_t networkLoad);
    
    /**
     * Get maximum gas price variation multiplier
     * @return Maximum variation (always 2 for predictability)
     */
    uint64_t GetMaxGasPriceVariation() const { return gasParams.maxPriceVariation; }
    
    // ===== Free Gas System (Requirements 6.3, 17.4) =====
    
    /**
     * Check if address is eligible for free gas (80+ reputation)
     * 
     * @param reputation Caller's reputation score
     * @return True if eligible for free gas
     */
    bool IsEligibleForFreeGas(uint8_t reputation) const { 
        return reputation >= gasParams.freeGasThreshold; 
    }
    
    /**
     * Get free gas allowance for high-reputation address
     * 
     * @param reputation Caller's reputation score
     * @return Free gas allowance in gas units
     */
    uint64_t GetFreeGasAllowance(uint8_t reputation);
    
    // ===== Anti-Congestion Through Trust (Requirements 16.1-16.4) =====
    
    /**
     * Determine if transaction should be prioritized based on reputation
     * 
     * @param trust Trust context
     * @param networkLoad Current network load (0-100)
     * @return True if transaction should be prioritized
     */
    bool ShouldPrioritizeTransaction(const CVM::TrustContext& trust, uint64_t networkLoad);
    
    /**
     * Check if reputation meets threshold for operation type
     * 
     * @param reputation Caller's reputation score
     * @param opType Type of operation being performed
     * @return True if reputation threshold is met
     */
    bool CheckReputationThreshold(uint8_t reputation, OperationType opType);
    
    /**
     * Implement trust-based rate limiting
     * 
     * @param address Address to rate limit
     * @param reputation Caller's reputation score
     */
    void ImplementTrustBasedRateLimit(const uint160& address, uint8_t reputation);
    
    // ===== Gas Subsidies and Rebates (Requirements 17.1-17.4) =====
    
    /**
     * Calculate gas subsidy for beneficial operations
     * 
     * @param trust Trust context
     * @param isBeneficialOp True if operation benefits network
     * @return Subsidy amount in gas units
     */
    uint64_t CalculateSubsidy(const CVM::TrustContext& trust, bool isBeneficialOp);
    
    /**
     * Process gas rebate for positive reputation actions
     * 
     * @param address Address receiving rebate
     * @param amount Rebate amount in gas units
     */
    void ProcessGasRebate(const uint160& address, uint64_t amount);
    
    /**
     * Check if operation is network-beneficial
     * 
     * @param opcode EVM opcode
     * @param trust Trust context
     * @return True if operation benefits network
     */
    bool IsNetworkBeneficialOperation(uint8_t opcode, const CVM::TrustContext& trust);
    
    // ===== Community Gas Pools (Requirement 17.3) =====
    
    /**
     * Contribute to community gas pool
     * 
     * @param contributor Address contributing gas
     * @param amount Amount to contribute
     * @param poolId Pool identifier
     */
    void ContributeToGasPool(const uint160& contributor, uint64_t amount, const std::string& poolId);
    
    /**
     * Use gas from community pool
     * 
     * @param poolId Pool identifier
     * @param amount Amount to use
     * @param trust Trust context
     * @return True if gas was successfully used
     */
    bool UseGasPool(const std::string& poolId, uint64_t amount, const CVM::TrustContext& trust);
    
    // ===== Business-Friendly Pricing (Requirements 18.2, 18.4, 18.5) =====
    
    /**
     * Create long-term price guarantee for business account
     * 
     * @param businessAddr Business address
     * @param guaranteedPrice Guaranteed gas price
     * @param duration Duration in blocks
     * @param minReputation Minimum reputation required
     */
    void CreatePriceGuarantee(const uint160& businessAddr, uint64_t guaranteedPrice, 
                             uint64_t duration, uint8_t minReputation);
    
    /**
     * Check if address has price guarantee
     * 
     * @param address Address to check
     * @param guaranteedPrice Output parameter for guaranteed price
     * @return True if address has active price guarantee
     */
    bool HasPriceGuarantee(const uint160& address, uint64_t& guaranteedPrice);
    
    /**
     * Check if address has active price guarantee (with expiration check)
     * 
     * @param address Business address
     * @param guaranteedPrice Output guaranteed price
     * @param currentBlock Current block height for expiration check
     * @return True if address has active price guarantee
     */
    bool HasPriceGuarantee(const uint160& address, uint64_t& guaranteedPrice, int64_t currentBlock);
    
    /**
     * Get price guarantee info including expiration
     * 
     * @param address Business address
     * @param guarantee Output price guarantee info
     * @return True if address has price guarantee
     */
    bool GetPriceGuaranteeInfo(const uint160& address, PriceGuarantee& guarantee);
    
    /**
     * Update base costs based on network trust density
     * 
     * @param networkTrustDensity Network trust density (0.0 to 1.0)
     */
    void UpdateBaseCosts(double networkTrustDensity);
    
    /**
     * Calculate network trust density
     * 
     * @return Network trust density (0.0 to 1.0)
     */
    double CalculateNetworkTrustDensity();
    
    // ===== Utility Methods =====
    
    /**
     * Get current gas parameters
     */
    const GasParameters& GetGasParameters() const { return gasParams; }
    
    /**
     * Set gas parameters (for testing)
     */
    void SetGasParameters(const GasParameters& params) { gasParams = params; }
    
    /**
     * Reset rate limits (for testing)
     */
    void ResetRateLimits();
    
private:
    // Gas parameters
    GasParameters gasParams;
    
    // Gas subsidy pools
    std::map<uint160, uint64_t> gasSubsidyPools;
    
    // Community gas pools
    std::map<std::string, uint64_t> communityGasPools;
    
    // Price guarantees for businesses
    std::map<uint160, PriceGuarantee> priceGuarantees;
    
    // Rate limiting state
    std::map<uint160, RateLimitState> rateLimits;
    
    // Helper methods
    double CalculateReputationMultiplier(uint8_t reputation);
    uint64_t GetBaseOpcodeCost(uint8_t opcode);
    bool IsHighFrequencyOperation(uint8_t opcode);
    bool IsStorageIntensiveOperation(uint8_t opcode);
    bool IsComputeIntensiveOperation(uint8_t opcode);
};

} // namespace cvm

#endif // CASCOIN_CVM_SUSTAINABLE_GAS_H
