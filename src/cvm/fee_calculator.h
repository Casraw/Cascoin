// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_FEE_CALCULATOR_H
#define CASCOIN_CVM_FEE_CALCULATOR_H

#include <amount.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <cvm/softfork.h>
#include <cvm/sustainable_gas.h>
#include <cvm/gas_allowance.h>
#include <cvm/gas_subsidy.h>

namespace CVM {

// Forward declarations
class CVMDatabase;
class TrustContext;

/**
 * Fee calculation result for CVM/EVM transactions
 */
struct FeeCalculationResult {
    CAmount baseFee;                    // Base fee without adjustments
    CAmount reputationDiscount;         // Discount from reputation
    CAmount gasSubsidy;                 // Subsidy from gas pool
    CAmount effectiveFee;               // Final fee to pay
    bool isFreeGas;                     // True if eligible for free gas
    bool hasSubsidy;                    // True if subsidy applied
    bool hasPriceGuarantee;             // True if price guarantee active
    uint8_t reputation;                 // Caller reputation score
    uint64_t gasLimit;                  // Gas limit for transaction
    uint64_t gasPrice;                  // Gas price per unit
    std::string error;                  // Error message if calculation failed
    
    FeeCalculationResult()
        : baseFee(0), reputationDiscount(0), gasSubsidy(0), effectiveFee(0),
          isFreeGas(false), hasSubsidy(false), hasPriceGuarantee(false),
          reputation(0), gasLimit(0), gasPrice(0) {}
    
    bool IsValid() const { return error.empty(); }
};

/**
 * CVM/EVM Fee Calculator
 * 
 * Calculates transaction fees for CVM/EVM contracts with:
 * - Reputation-based gas discounts (50% for 80+ reputation)
 * - Free gas for high-reputation addresses (80+)
 * - Gas subsidies from community pools
 * - Price guarantees for business accounts
 * - Integration with existing Bitcoin fee system
 * 
 * Requirements: 6.1, 6.2, 6.3, 17.1, 18.1, 18.2
 */
class FeeCalculator {
public:
    FeeCalculator();
    ~FeeCalculator();
    
    /**
     * Initialize fee calculator
     * 
     * @param db CVM database
     */
    void Initialize(CVMDatabase* db);
    
    // ===== Main Fee Calculation =====
    
    /**
     * Calculate fee for CVM/EVM transaction
     * 
     * This is the main entry point for fee calculation.
     * Considers:
     * - Base gas cost (100x lower than Ethereum)
     * - Reputation-based discounts
     * - Free gas eligibility (80+ reputation)
     * - Gas subsidies
     * - Price guarantees
     * 
     * @param tx Transaction
     * @param currentHeight Current block height
     * @return Fee calculation result
     */
    FeeCalculationResult CalculateFee(
        const CTransaction& tx,
        int currentHeight
    );
    
    /**
     * Calculate minimum fee required for transaction
     * 
     * This is used by validation.cpp to check if transaction
     * meets minimum fee requirements.
     * 
     * @param tx Transaction
     * @param reputation Sender reputation (0-100)
     * @param currentHeight Current block height
     * @return Minimum fee in satoshis
     */
    CAmount GetMinimumFee(
        const CTransaction& tx,
        uint8_t reputation,
        int currentHeight
    );
    
    /**
     * Calculate required fee for mempool acceptance
     * 
     * Integrates with existing Bitcoin fee system.
     * Returns fee adjusted for reputation and subsidies.
     * 
     * @param tx Transaction
     * @param nTxSize Transaction size in bytes
     * @param reputation Sender reputation
     * @param currentHeight Current block height
     * @return Required fee
     */
    CAmount GetRequiredFee(
        const CTransaction& tx,
        unsigned int nTxSize,
        uint8_t reputation,
        int currentHeight
    );
    
    // ===== Gas Price Estimation =====
    
    /**
     * Estimate gas price with trust-based discounts
     * 
     * @param reputation Caller reputation
     * @param networkLoad Current network load (0-100)
     * @return Estimated gas price in wei
     */
    uint64_t EstimateGasPrice(
        uint8_t reputation,
        uint64_t networkLoad
    );
    
    /**
     * Estimate gas price for transaction
     * 
     * @param tx Transaction
     * @return Estimated gas price
     */
    uint64_t EstimateGasPriceForTransaction(const CTransaction& tx);
    
    // ===== Free Gas Handling =====
    
    /**
     * Check if transaction is eligible for free gas
     * 
     * Requirements:
     * - Reputation >= 80
     * - Has remaining free gas allowance
     * - Within daily quota
     * 
     * @param tx Transaction
     * @param senderAddr Sender address
     * @param gasLimit Gas limit
     * @param currentHeight Current block height
     * @return true if eligible
     */
    bool IsEligibleForFreeGas(
        const CTransaction& tx,
        const uint160& senderAddr,
        uint64_t gasLimit,
        int currentHeight
    );
    
    /**
     * Get remaining free gas allowance
     * 
     * @param address Address to check
     * @param currentHeight Current block height
     * @return Remaining gas allowance
     */
    uint64_t GetRemainingFreeGas(
        const uint160& address,
        int currentHeight
    );
    
    // ===== Subsidy Handling =====
    
    /**
     * Calculate gas subsidy for transaction
     * 
     * @param tx Transaction
     * @param senderAddr Sender address
     * @param gasLimit Gas limit
     * @param reputation Sender reputation
     * @return Subsidy amount in satoshis
     */
    CAmount CalculateGasSubsidy(
        const CTransaction& tx,
        const uint160& senderAddr,
        uint64_t gasLimit,
        uint8_t reputation
    );
    
    /**
     * Check if transaction has valid gas subsidy
     * 
     * @param tx Transaction
     * @param subsidy Subsidy record
     * @return true if valid
     */
    bool ValidateGasSubsidy(
        const CTransaction& tx,
        const GasSubsidyTracker::SubsidyRecord& subsidy
    );
    
    // ===== Price Guarantee Handling =====
    
    /**
     * Check if address has active price guarantee
     * 
     * @param address Business address
     * @param guaranteedPrice Output guaranteed price
     * @param currentHeight Current block height
     * @return true if has active guarantee
     */
    bool HasPriceGuarantee(
        const uint160& address,
        uint64_t& guaranteedPrice,
        int currentHeight
    );
    
    /**
     * Apply price guarantee to fee calculation
     * 
     * @param baseFee Base fee before guarantee
     * @param guaranteedPrice Guaranteed gas price
     * @param gasLimit Gas limit
     * @return Adjusted fee
     */
    CAmount ApplyPriceGuarantee(
        CAmount baseFee,
        uint64_t guaranteedPrice,
        uint64_t gasLimit
    );
    
    // ===== Reputation-Based Adjustments =====
    
    /**
     * Calculate reputation-based fee discount
     * 
     * Discount tiers:
     * - 90-100: 50% discount
     * - 80-89:  40% discount
     * - 70-79:  30% discount
     * - 60-69:  20% discount
     * - 50-59:  10% discount
     * - <50:    No discount
     * 
     * @param baseFee Base fee
     * @param reputation Reputation score (0-100)
     * @return Discount amount
     */
    CAmount CalculateReputationDiscount(
        CAmount baseFee,
        uint8_t reputation
    );
    
    /**
     * Get reputation multiplier for gas costs
     * 
     * @param reputation Reputation score
     * @return Multiplier (0.5 to 1.0)
     */
    double GetReputationMultiplier(uint8_t reputation);
    
    // ===== Utility Methods =====
    
    /**
     * Convert gas units to CAS satoshis
     * 
     * @param gasAmount Gas amount
     * @param gasPrice Gas price per unit
     * @return Amount in satoshis
     */
    CAmount GasToSatoshis(uint64_t gasAmount, uint64_t gasPrice);
    
    /**
     * Convert CAS satoshis to gas units
     * 
     * @param satoshis Amount in satoshis
     * @param gasPrice Gas price per unit
     * @return Gas amount
     */
    uint64_t SatoshisToGas(CAmount satoshis, uint64_t gasPrice);
    
    /**
     * Extract gas limit from transaction
     * 
     * @param tx Transaction
     * @return Gas limit, or 0 if not CVM/EVM transaction
     */
    uint64_t ExtractGasLimit(const CTransaction& tx);
    
    /**
     * Get sender address from transaction
     * 
     * @param tx Transaction
     * @return Sender address
     */
    uint160 GetSenderAddress(const CTransaction& tx);
    
    /**
     * Get reputation for address
     * 
     * @param address Address
     * @return Reputation score (0-100)
     */
    uint8_t GetReputation(const uint160& address);
    
    /**
     * Get current network load
     * 
     * @return Network load (0-100)
     */
    uint64_t GetNetworkLoad();
    
private:
    CVMDatabase* m_db;
    std::shared_ptr<TrustContext> m_trustContext;
    std::unique_ptr<cvm::SustainableGasSystem> m_gasSystem;
    std::unique_ptr<GasAllowanceTracker> m_gasAllowanceTracker;
    std::unique_ptr<GasSubsidyTracker> m_gasSubsidyTracker;
    
    // Conversion rate: 1 gas unit = X satoshis
    // This is dynamically adjusted based on CAS/USD price
    static constexpr uint64_t DEFAULT_GAS_TO_SATOSHI_RATE = 100; // 100 satoshis per gas unit
    
    /**
     * Get current gas to satoshi conversion rate
     */
    uint64_t GetGasToSatoshiRate();
};

} // namespace CVM

#endif // CASCOIN_CVM_FEE_CALCULATOR_H
