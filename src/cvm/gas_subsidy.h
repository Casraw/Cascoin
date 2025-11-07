// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_GAS_SUBSIDY_H
#define CASCOIN_CVM_GAS_SUBSIDY_H

#include <uint256.h>
#include <amount.h>
#include <map>
#include <string>
#include <cstdint>

namespace CVM {

class CVMDatabase;
class TrustContext;

/**
 * Gas Subsidy Tracker
 * 
 * Tracks gas subsidies and rebates for beneficial operations.
 * Subsidizes operations that improve network trust and health.
 */
class GasSubsidyTracker {
public:
    /**
     * Subsidy record for an operation
     */
    struct SubsidyRecord {
        uint256 txid;
        uint160 address;
        uint64_t gasUsed;
        uint64_t subsidyAmount;
        uint8_t reputation;
        bool isBeneficial;
        int64_t blockHeight;
        
        SubsidyRecord()
            : gasUsed(0), subsidyAmount(0), reputation(0), 
              isBeneficial(false), blockHeight(0) {}
    };
    
    /**
     * Community gas pool
     */
    struct GasPool {
        std::string poolId;
        uint64_t totalContributed;
        uint64_t totalUsed;
        uint64_t remaining;
        uint8_t minReputation;
        int64_t createdHeight;
        
        GasPool()
            : totalContributed(0), totalUsed(0), remaining(0),
              minReputation(0), createdHeight(0) {}
    };
    
    /**
     * Rebate pending distribution
     */
    struct PendingRebate {
        uint160 address;
        uint64_t amount;
        int64_t blockHeight;
        std::string reason;
        
        PendingRebate()
            : amount(0), blockHeight(0) {}
    };
    
    GasSubsidyTracker();
    ~GasSubsidyTracker();
    
    /**
     * Calculate subsidy for an operation
     * 
     * @param gasUsed Gas used by operation
     * @param trust Trust context
     * @param isBeneficial Whether operation is beneficial to network
     * @return Subsidy amount (in gas units)
     */
    uint64_t CalculateSubsidy(
        uint64_t gasUsed,
        const TrustContext& trust,
        bool isBeneficial
    );
    
    /**
     * Apply subsidy to transaction
     * 
     * @param txid Transaction ID
     * @param address Address receiving subsidy
     * @param gasUsed Gas used
     * @param subsidy Subsidy amount
     * @param trust Trust context
     * @param blockHeight Current block height
     */
    void ApplySubsidy(
        const uint256& txid,
        const uint160& address,
        uint64_t gasUsed,
        uint64_t subsidy,
        const TrustContext& trust,
        int64_t blockHeight
    );
    
    /**
     * Queue rebate for distribution
     * 
     * @param address Address to receive rebate
     * @param amount Rebate amount
     * @param blockHeight Current block height
     * @param reason Reason for rebate
     */
    void QueueRebate(
        const uint160& address,
        uint64_t amount,
        int64_t blockHeight,
        const std::string& reason
    );
    
    /**
     * Distribute pending rebates
     * 
     * @param currentHeight Current block height
     * @return Number of rebates distributed
     */
    int DistributePendingRebates(int64_t currentHeight);
    
    /**
     * Create community gas pool
     * 
     * @param poolId Pool identifier
     * @param initialAmount Initial contribution
     * @param minReputation Minimum reputation to use pool
     * @param blockHeight Current block height
     */
    void CreateGasPool(
        const std::string& poolId,
        uint64_t initialAmount,
        uint8_t minReputation,
        int64_t blockHeight
    );
    
    /**
     * Contribute to gas pool
     * 
     * @param poolId Pool identifier
     * @param amount Amount to contribute
     * @return true if contribution successful
     */
    bool ContributeToPool(
        const std::string& poolId,
        uint64_t amount
    );
    
    /**
     * Use gas from pool
     * 
     * @param poolId Pool identifier
     * @param amount Amount to use
     * @param trust Trust context for eligibility check
     * @return true if usage successful
     */
    bool UseFromPool(
        const std::string& poolId,
        uint64_t amount,
        const TrustContext& trust
    );
    
    /**
     * Get pool information
     * 
     * @param poolId Pool identifier
     * @param pool Output pool information
     * @return true if pool exists
     */
    bool GetPoolInfo(
        const std::string& poolId,
        GasPool& pool
    );
    
    /**
     * Get total subsidies for address
     * 
     * @param address Address to query
     * @return Total subsidy amount received
     */
    uint64_t GetTotalSubsidies(const uint160& address);
    
    /**
     * Get pending rebates for address
     * 
     * @param address Address to query
     * @return Total pending rebate amount
     */
    uint64_t GetPendingRebates(const uint160& address);
    
    /**
     * Load subsidy data from database
     * 
     * @param db Database to load from
     */
    void LoadFromDatabase(CVMDatabase& db);
    
    /**
     * Save subsidy data to database
     * 
     * @param db Database to save to
     */
    void SaveToDatabase(CVMDatabase& db);
    
    /**
     * Clear all subsidy data (for testing)
     */
    void Clear();
    
private:
    // Subsidy records by address
    std::map<uint160, std::vector<SubsidyRecord>> subsidyRecords;
    
    // Community gas pools
    std::map<std::string, GasPool> gasPools;
    
    // Pending rebates
    std::vector<PendingRebate> pendingRebates;
    
    // Total subsidies distributed
    uint64_t totalSubsidiesDistributed;
    
    // Total rebates distributed
    uint64_t totalRebatesDistributed;
    
    /**
     * Check if operation is beneficial to network
     * 
     * @param trust Trust context
     * @return true if beneficial
     */
    bool IsBeneficialOperation(const TrustContext& trust);
};

} // namespace CVM

#endif // CASCOIN_CVM_GAS_SUBSIDY_H
