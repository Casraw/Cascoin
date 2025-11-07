// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_MEMPOOL_MANAGER_H
#define CASCOIN_CVM_MEMPOOL_MANAGER_H

#include <primitives/transaction.h>
#include <cvm/tx_priority.h>
#include <cvm/gas_allowance.h>
#include <cvm/gas_subsidy.h>
#include <cvm/sustainable_gas.h>
#include <cvm/softfork.h>
#include <amount.h>
#include <uint256.h>
#include <univalue.h>
#include <map>
#include <set>
#include <mutex>

namespace CVM {

class CVMDatabase;

/**
 * CVM Mempool Manager
 * 
 * Manages CVM/EVM transactions in the mempool with:
 * - Reputation-based transaction prioritization
 * - Free gas allowance tracking
 * - Gas subsidy validation
 * - Anti-congestion mechanisms
 * - Price guarantee enforcement
 */
class MempoolManager {
public:
    /**
     * Transaction validation result
     */
    struct ValidationResult {
        bool isValid;
        bool isFreeGas;
        bool hasSubsidy;
        uint8_t reputation;
        TransactionPriorityManager::PriorityLevel priority;
        CAmount effectiveFee;
        std::string error;
        
        ValidationResult()
            : isValid(false), isFreeGas(false), hasSubsidy(false),
              reputation(0), priority(TransactionPriorityManager::PriorityLevel::LOW),
              effectiveFee(0) {}
    };
    
    MempoolManager();
    ~MempoolManager();
    
    /**
     * Initialize mempool manager
     * 
     * @param db CVM database
     */
    void Initialize(CVMDatabase* db);
    
    // ===== Transaction Validation =====
    
    /**
     * Validate CVM/EVM transaction for mempool acceptance
     * 
     * Checks:
     * - Transaction format validity
     * - Gas limits and costs
     * - Free gas eligibility
     * - Gas subsidy validity
     * - Reputation requirements
     * - Rate limiting
     * 
     * @param tx Transaction to validate
     * @param currentHeight Current block height
     * @return Validation result
     */
    ValidationResult ValidateTransaction(
        const CTransaction& tx,
        int currentHeight
    );
    
    /**
     * Check if transaction is eligible for free gas
     * 
     * @param tx Transaction
     * @param senderAddr Sender address
     * @return true if eligible
     */
    bool CheckFreeGasEligibility(
        const CTransaction& tx,
        const uint160& senderAddr
    );
    
    /**
     * Validate gas subsidy for transaction
     * 
     * @param tx Transaction
     * @param subsidy Subsidy to validate
     * @return true if valid
     */
    bool ValidateGasSubsidy(
        const CTransaction& tx,
        const GasSubsidyTracker::SubsidyRecord& subsidy
    );
    
    // ===== Priority Management =====
    
    /**
     * Get transaction priority for ordering
     * 
     * @param tx Transaction
     * @return Priority information
     */
    TransactionPriorityManager::TransactionPriority GetTransactionPriority(
        const CTransaction& tx
    );
    
    /**
     * Compare two transactions for priority ordering
     * Higher priority transactions should be processed first
     * 
     * @param a First transaction
     * @param b Second transaction
     * @return true if a has higher priority than b
     */
    bool CompareTransactionPriority(
        const CTransaction& a,
        const CTransaction& b
    );
    
    /**
     * Check if transaction should be guaranteed inclusion
     * 
     * @param tx Transaction
     * @return true if guaranteed (90+ reputation)
     */
    bool HasGuaranteedInclusion(const CTransaction& tx);
    
    // ===== Free Gas Management =====
    
    /**
     * Record free gas usage for transaction
     * 
     * @param tx Transaction
     * @param gasUsed Gas consumed
     * @param currentHeight Current block height
     */
    void RecordFreeGasUsage(
        const CTransaction& tx,
        uint64_t gasUsed,
        int currentHeight
    );
    
    /**
     * Get remaining free gas allowance for address
     * 
     * @param address Address to check
     * @return Remaining gas allowance
     */
    uint64_t GetRemainingFreeGas(const uint160& address);
    
    // ===== Rate Limiting =====
    
    /**
     * Check if address is rate limited
     * 
     * @param address Address to check
     * @return true if rate limited
     */
    bool IsRateLimited(const uint160& address);
    
    /**
     * Record transaction submission for rate limiting
     * 
     * @param address Sender address
     */
    void RecordTransactionSubmission(const uint160& address);
    
    // ===== Fee Calculation =====
    
    /**
     * Calculate effective fee for transaction
     * Considers:
     * - Base gas cost
     * - Reputation-based discounts
     * - Free gas allowance
     * - Gas subsidies
     * 
     * @param tx Transaction
     * @param gasLimit Gas limit
     * @param reputation Sender reputation
     * @return Effective fee in satoshis
     */
    CAmount CalculateEffectiveFee(
        const CTransaction& tx,
        uint64_t gasLimit,
        uint8_t reputation
    );
    
    /**
     * Get minimum fee for transaction
     * 
     * @param tx Transaction
     * @param reputation Sender reputation
     * @return Minimum fee
     */
    CAmount GetMinimumFee(
        const CTransaction& tx,
        uint8_t reputation
    );
    
    // ===== Statistics =====
    
    /**
     * Get mempool statistics
     * 
     * @return JSON object with statistics
     */
    UniValue GetMempoolStats();
    
    /**
     * Get transaction count by priority level
     * 
     * @return Map of priority level to count
     */
    std::map<TransactionPriorityManager::PriorityLevel, uint64_t> GetPriorityDistribution();
    
private:
    CVMDatabase* m_db;
    std::unique_ptr<TransactionPriorityManager> m_priorityManager;
    std::unique_ptr<GasAllowanceTracker> m_gasAllowanceManager;
    std::unique_ptr<GasSubsidyTracker> m_gasSubsidyManager;
    std::unique_ptr<cvm::SustainableGasSystem> m_gasSystem;
    
    // Rate limiting
    std::map<uint160, int64_t> m_lastSubmission;
    std::map<uint160, uint32_t> m_submissionCount;
    std::mutex m_rateLimitMutex;
    
    // Statistics
    uint64_t m_totalValidated;
    uint64_t m_totalAccepted;
    uint64_t m_totalRejected;
    uint64_t m_freeGasTransactions;
    uint64_t m_subsidizedTransactions;
    std::mutex m_statsMutex;
    
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
     * Extract gas limit from CVM/EVM transaction
     * 
     * @param tx Transaction
     * @return Gas limit, or 0 if not a CVM/EVM transaction
     */
    uint64_t ExtractGasLimit(const CTransaction& tx);
};

} // namespace CVM

#endif // CASCOIN_CVM_MEMPOOL_MANAGER_H
