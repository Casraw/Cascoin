// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_MEMPOOL_PRIORITY_H
#define CASCOIN_CVM_MEMPOOL_PRIORITY_H

#include <txmempool.h>
#include <cvm/tx_priority.h>
#include <cvm/softfork.h>
#include <cvm/fee_calculator.h>

namespace CVM {

/**
 * Comparator for sorting mempool entries by reputation-based priority
 * 
 * Priority order (highest to lowest):
 * 1. CRITICAL (90+ reputation, guaranteed inclusion)
 * 2. HIGH (70-89 reputation)
 * 3. NORMAL (50-69 reputation)
 * 4. LOW (<50 reputation)
 * 
 * Within same priority level, sort by:
 * - Fee rate (higher is better)
 * - Entry time (older is better)
 * 
 * Requirements: 15.1, 15.3, 16.2, 16.3
 */
class CompareTxMemPoolEntryByReputationPriority
{
public:
    CompareTxMemPoolEntryByReputationPriority();
    
    /**
     * Compare two mempool entries for priority ordering
     * 
     * @param a First entry
     * @param b Second entry
     * @return true if a has higher priority than b
     */
    bool operator()(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b) const;
    
private:
    /**
     * Get priority level for transaction
     * 
     * @param entry Mempool entry
     * @return Priority level
     */
    TransactionPriorityManager::PriorityLevel GetPriorityLevel(const CTxMemPoolEntry& entry) const;
    
    /**
     * Get reputation for transaction
     * 
     * @param entry Mempool entry
     * @return Reputation score (0-100)
     */
    uint8_t GetReputation(const CTxMemPoolEntry& entry) const;
    
    /**
     * Check if transaction is CVM/EVM
     * 
     * @param entry Mempool entry
     * @return true if CVM/EVM transaction
     */
    bool IsCVMTransaction(const CTxMemPoolEntry& entry) const;
    
    /**
     * Get fee rate for comparison
     * 
     * @param entry Mempool entry
     * @return Fee rate
     */
    double GetFeeRate(const CTxMemPoolEntry& entry) const;
    
    mutable TransactionPriorityManager m_priorityManager;
    mutable FeeCalculator m_feeCalculator;
};

/**
 * Helper class for managing reputation-based mempool priority
 * 
 * This class provides utilities for:
 * - Checking guaranteed inclusion eligibility
 * - Getting priority statistics
 * - Managing priority-based eviction
 */
class MempoolPriorityHelper
{
public:
    MempoolPriorityHelper();
    
    /**
     * Check if transaction should have guaranteed inclusion
     * 
     * Requirements:
     * - Reputation >= 90
     * - Valid CVM/EVM transaction
     * 
     * @param entry Mempool entry
     * @return true if guaranteed inclusion
     */
    static bool HasGuaranteedInclusion(const CTxMemPoolEntry& entry);
    
    /**
     * Get priority level for transaction
     * 
     * @param entry Mempool entry
     * @return Priority level
     */
    static TransactionPriorityManager::PriorityLevel GetPriorityLevel(const CTxMemPoolEntry& entry);
    
    /**
     * Check if transaction A should be evicted before transaction B
     * 
     * Lower priority transactions are evicted first.
     * Within same priority, lower fee rate transactions are evicted first.
     * 
     * @param a First entry
     * @param b Second entry
     * @return true if a should be evicted before b
     */
    static bool ShouldEvictBefore(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b);
    
    /**
     * Get reputation for transaction
     * 
     * @param entry Mempool entry
     * @return Reputation score (0-100)
     */
    static uint8_t GetReputation(const CTxMemPoolEntry& entry);
    
    /**
     * Check if transaction is CVM/EVM
     * 
     * @param entry Mempool entry
     * @return true if CVM/EVM transaction
     */
    static bool IsCVMTransaction(const CTxMemPoolEntry& entry);
    
    /**
     * Get sender address from transaction
     * 
     * @param entry Mempool entry
     * @return Sender address (may be null if cannot be extracted)
     */
    static uint160 GetSenderAddress(const CTxMemPoolEntry& entry);
    
    /**
     * Get priority statistics for mempool
     * 
     * @param mempool Mempool to analyze
     * @return Map of priority level to count
     */
    static std::map<TransactionPriorityManager::PriorityLevel, uint64_t> 
        GetPriorityDistribution(const CTxMemPool& mempool);
    
    /**
     * Count guaranteed inclusion transactions in mempool
     * 
     * @param mempool Mempool to analyze
     * @return Count of guaranteed inclusion transactions
     */
    static uint64_t CountGuaranteedInclusion(const CTxMemPool& mempool);
};

/**
 * Block assembler helper for reputation-based transaction selection
 * 
 * This class provides utilities for selecting transactions for blocks
 * based on reputation priority.
 */
class BlockAssemblerPriorityHelper
{
public:
    /**
     * Select transactions for block with reputation-based priority
     * 
     * Algorithm:
     * 1. Include all guaranteed inclusion transactions (90+ reputation)
     * 2. Fill remaining space with highest priority transactions
     * 3. Within same priority, prefer higher fee rate
     * 
     * @param mempool Mempool to select from
     * @param maxWeight Maximum block weight
     * @param maxSigOpsCost Maximum signature operations cost
     * @return Set of transactions to include
     */
    static CTxMemPool::setEntries SelectTransactionsForBlock(
        CTxMemPool& mempool,
        uint64_t maxWeight,
        int64_t maxSigOpsCost
    );
    
    /**
     * Get guaranteed inclusion transactions
     * 
     * @param mempool Mempool to select from
     * @return Set of guaranteed inclusion transactions
     */
    static CTxMemPool::setEntries GetGuaranteedInclusionTransactions(CTxMemPool& mempool);
    
    /**
     * Sort transactions by priority for block inclusion
     * 
     * @param transactions Transactions to sort
     * @return Sorted vector of transactions
     */
    static std::vector<CTxMemPool::txiter> SortByPriority(
        const CTxMemPool::setEntries& transactions
    );
    
    /**
     * Check if transaction should be included in block
     * 
     * Considers:
     * - Priority level
     * - Network congestion
     * - Block space availability
     * 
     * @param entry Transaction entry
     * @param networkLoad Current network load (0-100)
     * @param blockSpaceRemaining Remaining block space
     * @return true if should be included
     */
    static bool ShouldIncludeInBlock(
        const CTxMemPoolEntry& entry,
        uint64_t networkLoad,
        uint64_t blockSpaceRemaining
    );
};

} // namespace CVM

#endif // CASCOIN_CVM_MEMPOOL_PRIORITY_H
