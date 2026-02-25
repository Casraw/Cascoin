// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_GAS_ALLOWANCE_H
#define CASCOIN_CVM_GAS_ALLOWANCE_H

#include <uint256.h>
#include <amount.h>
#include <map>
#include <cstdint>

namespace CVM {

class CVMDatabase;
class TrustContext;

/**
 * Gas Allowance Tracker
 * 
 * Tracks free gas allowances for high-reputation addresses.
 * Addresses with 80+ reputation get daily gas allowances that renew automatically.
 */
class GasAllowanceTracker {
public:
    /**
     * Gas allowance state for an address
     */
    struct AllowanceState {
        uint64_t dailyAllowance;      // Total daily allowance based on reputation
        uint64_t usedToday;            // Gas used today
        int64_t lastRenewalBlock;      // Block height of last renewal
        uint8_t reputation;            // Cached reputation score
        
        AllowanceState() 
            : dailyAllowance(0), usedToday(0), lastRenewalBlock(0), reputation(0) {}
    };
    
    GasAllowanceTracker();
    ~GasAllowanceTracker();
    
    /**
     * Check if address has sufficient free gas allowance
     * 
     * @param address Address to check
     * @param gasNeeded Amount of gas needed
     * @param trust Trust context for reputation lookup
     * @param currentBlock Current block height
     * @return true if address has sufficient allowance
     */
    bool HasSufficientAllowance(
        const uint160& address,
        uint64_t gasNeeded,
        const TrustContext& trust,
        int64_t currentBlock
    );
    
    /**
     * Deduct gas from address allowance
     * 
     * @param address Address to deduct from
     * @param gasUsed Amount of gas to deduct
     * @param currentBlock Current block height
     * @return true if deduction successful
     */
    bool DeductGas(
        const uint160& address,
        uint64_t gasUsed,
        int64_t currentBlock
    );
    
    /**
     * Get current allowance state for address
     * 
     * @param address Address to query
     * @param trust Trust context for reputation lookup
     * @param currentBlock Current block height
     * @return Allowance state
     */
    AllowanceState GetAllowanceState(
        const uint160& address,
        const TrustContext& trust,
        int64_t currentBlock
    );
    
    /**
     * Renew allowance if needed (daily renewal)
     * 
     * @param address Address to renew
     * @param trust Trust context for reputation lookup
     * @param currentBlock Current block height
     */
    void RenewIfNeeded(
        const uint160& address,
        const TrustContext& trust,
        int64_t currentBlock
    );
    
    /**
     * Load allowance state from database
     * 
     * @param db Database to load from
     */
    void LoadFromDatabase(CVMDatabase& db);
    
    /**
     * Save allowance state to database
     * 
     * @param db Database to save to
     */
    void SaveToDatabase(CVMDatabase& db);
    
    /**
     * Clear all allowance state (for testing)
     */
    void Clear();
    
private:
    // In-memory cache of allowance states
    std::map<uint160, AllowanceState> allowanceCache;
    
    /**
     * Calculate daily allowance based on reputation
     * 
     * @param reputation Reputation score (0-100)
     * @return Daily gas allowance
     */
    uint64_t CalculateDailyAllowance(uint8_t reputation);
    
    /**
     * Check if renewal is needed (daily renewal)
     * 
     * @param lastRenewalBlock Last renewal block height
     * @param currentBlock Current block height
     * @return true if renewal needed
     */
    bool NeedsRenewal(int64_t lastRenewalBlock, int64_t currentBlock);
    
    /**
     * Get blocks per day (approximately)
     * 2.5 minute blocks = 576 blocks per day
     */
    static constexpr int64_t BLOCKS_PER_DAY = 576;
};

} // namespace CVM

#endif // CASCOIN_CVM_GAS_ALLOWANCE_H
