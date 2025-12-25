// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TX_PRIORITY_H
#define CASCOIN_CVM_TX_PRIORITY_H

#include <uint256.h>
#include <primitives/transaction.h>
#include <map>
#include <cstdint>

namespace CVM {

class CVMDatabase;
class TrustContext;

/**
 * Transaction Priority Manager
 * 
 * Manages reputation-based transaction prioritization for anti-congestion.
 * High-reputation addresses get priority during network congestion.
 */
class TransactionPriorityManager {
public:
    /**
     * Priority levels based on reputation
     */
    enum class PriorityLevel {
        CRITICAL = 1,    // 90+ reputation - Always prioritized, guaranteed inclusion
        HIGH = 2,        // 70-89 reputation - High priority during congestion
        NORMAL = 3,      // 50-69 reputation - Normal priority
        LOW = 4          // <50 reputation - Low priority, may be delayed
    };
    
    /**
     * Transaction priority information
     */
    struct TransactionPriority {
        uint256 txid;
        uint8_t reputation;
        PriorityLevel level;
        int64_t timestamp;
        bool guaranteedInclusion;
        
        TransactionPriority()
            : reputation(0), level(PriorityLevel::LOW), 
              timestamp(0), guaranteedInclusion(false) {}
    };
    
    TransactionPriorityManager();
    ~TransactionPriorityManager();
    
    /**
     * Calculate priority for a transaction
     * 
     * @param tx Transaction to prioritize
     * @param db CVM database for reputation lookup
     * @return Transaction priority information
     */
    TransactionPriority CalculatePriority(
        const CTransaction& tx,
        CVMDatabase& db
    );
    
    /**
     * Get priority level from reputation score
     * 
     * @param reputation Reputation score (0-100)
     * @return Priority level
     */
    static PriorityLevel GetPriorityLevel(uint8_t reputation);
    
    /**
     * Check if transaction should be guaranteed inclusion
     * 
     * @param reputation Reputation score
     * @return true if guaranteed inclusion (90+ reputation)
     */
    static bool HasGuaranteedInclusion(uint8_t reputation);
    
    /**
     * Compare two transactions for priority ordering
     * Higher priority transactions should be processed first
     * 
     * @param a First transaction priority
     * @param b Second transaction priority
     * @return true if a has higher priority than b
     */
    static bool CompareTransactionPriority(
        const TransactionPriority& a,
        const TransactionPriority& b
    );
    
    /**
     * Get priority score for mining/mempool ordering
     * Higher score = higher priority
     * 
     * @param priority Transaction priority
     * @return Priority score (0-1000)
     */
    static int64_t GetPriorityScore(const TransactionPriority& priority);
    
    /**
     * Cache transaction priority
     * 
     * @param txid Transaction ID
     * @param priority Priority information
     */
    void CachePriority(const uint256& txid, const TransactionPriority& priority);
    
    /**
     * Get cached priority
     * 
     * @param txid Transaction ID
     * @param priority Output priority information
     * @return true if found in cache
     */
    bool GetCachedPriority(const uint256& txid, TransactionPriority& priority);
    
    /**
     * Remove from cache
     * 
     * @param txid Transaction ID
     */
    void RemoveFromCache(const uint256& txid);
    
    /**
     * Clear all cached priorities
     */
    void ClearCache();
    
    /**
     * Get network congestion level (0-100)
     * 
     * @return Congestion level
     */
    uint8_t GetNetworkCongestion() const;
    
    /**
     * Update network congestion level
     * 
     * @param mempoolSize Current mempool size
     * @param maxMempoolSize Maximum mempool size
     */
    void UpdateNetworkCongestion(size_t mempoolSize, size_t maxMempoolSize);
    
private:
    // Cache of transaction priorities
    std::map<uint256, TransactionPriority> priorityCache;
    
    // Current network congestion level (0-100)
    uint8_t networkCongestion;
    
    /**
     * Extract sender address from transaction
     * 
     * @param tx Transaction
     * @param senderAddr Output sender address
     * @return true if sender extracted successfully
     */
    bool ExtractSenderAddress(const CTransaction& tx, uint160& senderAddr);
};

} // namespace CVM

#endif // CASCOIN_CVM_TX_PRIORITY_H
