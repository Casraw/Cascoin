// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/mempool_priority.h>
#include <cvm/cvm.h>
#include <util.h>

namespace CVM {

// ===== CompareTxMemPoolEntryByReputationPriority =====

CompareTxMemPoolEntryByReputationPriority::CompareTxMemPoolEntryByReputationPriority()
{
    // Initialize priority manager and fee calculator
    // TODO: Initialize with CVM database when available
}

bool CompareTxMemPoolEntryByReputationPriority::operator()(
    const CTxMemPoolEntry& a,
    const CTxMemPoolEntry& b) const
{
    // Check if transactions are CVM/EVM
    bool aIsCVM = IsCVMTransaction(a);
    bool bIsCVM = IsCVMTransaction(b);
    
    // CVM/EVM transactions have priority over standard Bitcoin transactions
    if (aIsCVM && !bIsCVM) return true;
    if (!aIsCVM && bIsCVM) return false;
    
    // If both are standard Bitcoin transactions, use standard fee-based comparison
    if (!aIsCVM && !bIsCVM) {
        double aFeeRate = GetFeeRate(a);
        double bFeeRate = GetFeeRate(b);
        if (aFeeRate != bFeeRate) {
            return aFeeRate > bFeeRate;
        }
        return a.GetTime() < b.GetTime();
    }
    
    // Both are CVM/EVM transactions - use reputation-based priority
    TransactionPriorityManager::PriorityLevel aPriority = GetPriorityLevel(a);
    TransactionPriorityManager::PriorityLevel bPriority = GetPriorityLevel(b);
    
    // Higher priority level wins
    if (aPriority != bPriority) {
        return aPriority < bPriority; // Lower enum value = higher priority
    }
    
    // Same priority level - compare by fee rate
    double aFeeRate = GetFeeRate(a);
    double bFeeRate = GetFeeRate(b);
    if (aFeeRate != bFeeRate) {
        return aFeeRate > bFeeRate;
    }
    
    // Same fee rate - older transaction wins
    return a.GetTime() < b.GetTime();
}

TransactionPriorityManager::PriorityLevel 
CompareTxMemPoolEntryByReputationPriority::GetPriorityLevel(const CTxMemPoolEntry& entry) const
{
    uint8_t reputation = GetReputation(entry);
    
    // Map reputation to priority level
    if (reputation >= 90) return TransactionPriorityManager::PriorityLevel::CRITICAL;
    if (reputation >= 70) return TransactionPriorityManager::PriorityLevel::HIGH;
    if (reputation >= 50) return TransactionPriorityManager::PriorityLevel::NORMAL;
    return TransactionPriorityManager::PriorityLevel::LOW;
}

uint8_t CompareTxMemPoolEntryByReputationPriority::GetReputation(const CTxMemPoolEntry& entry) const
{
    // Extract sender address from transaction
    const CTransaction& tx = entry.GetTx();
    
    // Use FeeCalculator to get sender address and reputation
    // This reuses the logic we just implemented in Task 13.5.1
    uint160 senderAddr = m_feeCalculator.GetSenderAddress(tx);
    
    // If we couldn't extract sender address, return default reputation
    if (senderAddr.IsNull()) {
        LogPrint(BCLog::CVM, "CompareTxMemPoolEntry: Could not extract sender address for tx %s\n",
                 tx.GetHash().ToString());
        return 50; // Default medium reputation
    }
    
    // Get reputation from fee calculator
    uint8_t reputation = m_feeCalculator.GetReputation(senderAddr);
    
    LogPrint(BCLog::CVM, "CompareTxMemPoolEntry: tx=%s sender=%s reputation=%d\n",
             tx.GetHash().ToString(), senderAddr.ToString(), reputation);
    
    return reputation;
}

bool CompareTxMemPoolEntryByReputationPriority::IsCVMTransaction(const CTxMemPoolEntry& entry) const
{
    const CTransaction& tx = entry.GetTx();
    return IsEVMTransaction(tx) || FindCVMOpReturn(tx) >= 0;
}

double CompareTxMemPoolEntryByReputationPriority::GetFeeRate(const CTxMemPoolEntry& entry) const
{
    return (double)entry.GetModifiedFee() / entry.GetTxSize();
}

// ===== MempoolPriorityHelper =====

MempoolPriorityHelper::MempoolPriorityHelper()
{
}

bool MempoolPriorityHelper::HasGuaranteedInclusion(const CTxMemPoolEntry& entry)
{
    // Check if CVM/EVM transaction
    if (!IsCVMTransaction(entry)) {
        return false;
    }
    
    // Check reputation threshold (90+)
    uint8_t reputation = GetReputation(entry);
    return reputation >= 90;
}

TransactionPriorityManager::PriorityLevel 
MempoolPriorityHelper::GetPriorityLevel(const CTxMemPoolEntry& entry)
{
    uint8_t reputation = GetReputation(entry);
    
    if (reputation >= 90) return TransactionPriorityManager::PriorityLevel::CRITICAL;
    if (reputation >= 70) return TransactionPriorityManager::PriorityLevel::HIGH;
    if (reputation >= 50) return TransactionPriorityManager::PriorityLevel::NORMAL;
    return TransactionPriorityManager::PriorityLevel::LOW;
}

bool MempoolPriorityHelper::ShouldEvictBefore(
    const CTxMemPoolEntry& a,
    const CTxMemPoolEntry& b)
{
    // Check if CVM/EVM transactions
    bool aIsCVM = IsCVMTransaction(a);
    bool bIsCVM = IsCVMTransaction(b);
    
    // Standard Bitcoin transactions are evicted before CVM/EVM transactions
    if (!aIsCVM && bIsCVM) return true;
    if (aIsCVM && !bIsCVM) return false;
    
    // If both are standard, use fee rate
    if (!aIsCVM && !bIsCVM) {
        double aFeeRate = (double)a.GetModifiedFee() / a.GetTxSize();
        double bFeeRate = (double)b.GetModifiedFee() / b.GetTxSize();
        return aFeeRate < bFeeRate;
    }
    
    // Both are CVM/EVM - use priority
    TransactionPriorityManager::PriorityLevel aPriority = GetPriorityLevel(a);
    TransactionPriorityManager::PriorityLevel bPriority = GetPriorityLevel(b);
    
    // Lower priority is evicted first
    if (aPriority != bPriority) {
        return aPriority > bPriority; // Higher enum value = lower priority
    }
    
    // Same priority - lower fee rate is evicted first
    double aFeeRate = (double)a.GetModifiedFee() / a.GetTxSize();
    double bFeeRate = (double)b.GetModifiedFee() / b.GetTxSize();
    return aFeeRate < bFeeRate;
}

uint8_t MempoolPriorityHelper::GetReputation(const CTxMemPoolEntry& entry)
{
    // Extract sender address from transaction
    const CTransaction& tx = entry.GetTx();
    
    // Use FeeCalculator to get sender address and reputation
    FeeCalculator feeCalc;
    // Note: FeeCalculator needs database initialization for full functionality
    // but GetReputation() can work with TrustContext which may be available
    
    uint160 senderAddr = feeCalc.GetSenderAddress(tx);
    
    // If we couldn't extract sender address, return default reputation
    if (senderAddr.IsNull()) {
        LogPrint(BCLog::CVM, "MempoolPriorityHelper: Could not extract sender address for tx %s\n",
                 tx.GetHash().ToString());
        return 50; // Default medium reputation
    }
    
    // Get reputation
    uint8_t reputation = feeCalc.GetReputation(senderAddr);
    
    LogPrint(BCLog::CVM, "MempoolPriorityHelper: tx=%s sender=%s reputation=%d\n",
             tx.GetHash().ToString(), senderAddr.ToString(), reputation);
    
    return reputation;
}

bool MempoolPriorityHelper::IsCVMTransaction(const CTxMemPoolEntry& entry)
{
    const CTransaction& tx = entry.GetTx();
    return IsEVMTransaction(tx) || FindCVMOpReturn(tx) >= 0;
}

uint160 MempoolPriorityHelper::GetSenderAddress(const CTxMemPoolEntry& entry)
{
    const CTransaction& tx = entry.GetTx();
    
    // Use FeeCalculator to extract sender address
    FeeCalculator feeCalc;
    return feeCalc.GetSenderAddress(tx);
}

std::map<TransactionPriorityManager::PriorityLevel, uint64_t>
MempoolPriorityHelper::GetPriorityDistribution(const CTxMemPool& mempool)
{
    std::map<TransactionPriorityManager::PriorityLevel, uint64_t> distribution;
    
    // Initialize counts
    distribution[TransactionPriorityManager::PriorityLevel::CRITICAL] = 0;
    distribution[TransactionPriorityManager::PriorityLevel::HIGH] = 0;
    distribution[TransactionPriorityManager::PriorityLevel::NORMAL] = 0;
    distribution[TransactionPriorityManager::PriorityLevel::LOW] = 0;
    
    // Count transactions by priority
    LOCK(mempool.cs);
    for (const auto& entry : mempool.mapTx) {
        if (IsCVMTransaction(entry)) {
            TransactionPriorityManager::PriorityLevel priority = GetPriorityLevel(entry);
            distribution[priority]++;
        }
    }
    
    return distribution;
}

uint64_t MempoolPriorityHelper::CountGuaranteedInclusion(const CTxMemPool& mempool)
{
    uint64_t count = 0;
    
    LOCK(mempool.cs);
    for (const auto& entry : mempool.mapTx) {
        if (HasGuaranteedInclusion(entry)) {
            count++;
        }
    }
    
    return count;
}

// ===== BlockAssemblerPriorityHelper =====

CTxMemPool::setEntries BlockAssemblerPriorityHelper::SelectTransactionsForBlock(
    CTxMemPool& mempool,
    uint64_t maxWeight,
    int64_t maxSigOpsCost)
{
    CTxMemPool::setEntries selected;
    uint64_t currentWeight = 0;
    int64_t currentSigOpsCost = 0;
    
    LOCK(mempool.cs);
    
    // First, get all guaranteed inclusion transactions (90+ reputation)
    CTxMemPool::setEntries guaranteed = GetGuaranteedInclusionTransactions(mempool);
    
    // Add guaranteed transactions first
    for (auto it : guaranteed) {
        uint64_t txWeight = it->GetTxWeight();
        int64_t txSigOpsCost = it->GetSigOpCost();
        
        // Check if transaction fits
        if (currentWeight + txWeight <= maxWeight &&
            currentSigOpsCost + txSigOpsCost <= maxSigOpsCost) {
            selected.insert(it);
            currentWeight += txWeight;
            currentSigOpsCost += txSigOpsCost;
        }
    }
    
    // Then fill remaining space with highest priority transactions
    std::vector<CTxMemPool::txiter> sortedTxs;
    for (auto it = mempool.mapTx.begin(); it != mempool.mapTx.end(); ++it) {
        // Skip if already selected
        if (selected.count(it) > 0) continue;
        
        // Only consider CVM/EVM transactions for priority selection
        if (MempoolPriorityHelper::IsCVMTransaction(*it)) {
            sortedTxs.push_back(it);
        }
    }
    
    // Sort by priority
    std::sort(sortedTxs.begin(), sortedTxs.end(),
        [](const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) {
            CompareTxMemPoolEntryByReputationPriority comp;
            return comp(*a, *b);
        });
    
    // Add transactions in priority order
    for (auto it : sortedTxs) {
        uint64_t txWeight = it->GetTxWeight();
        int64_t txSigOpsCost = it->GetSigOpCost();
        
        // Check if transaction fits
        if (currentWeight + txWeight <= maxWeight &&
            currentSigOpsCost + txSigOpsCost <= maxSigOpsCost) {
            selected.insert(it);
            currentWeight += txWeight;
            currentSigOpsCost += txSigOpsCost;
        }
    }
    
    LogPrint(BCLog::CVM, "BlockAssembler: Selected %d transactions (%d guaranteed, weight=%d/%d)\n",
             selected.size(), guaranteed.size(), currentWeight, maxWeight);
    
    return selected;
}

CTxMemPool::setEntries BlockAssemblerPriorityHelper::GetGuaranteedInclusionTransactions(
    CTxMemPool& mempool)
{
    CTxMemPool::setEntries guaranteed;
    
    LOCK(mempool.cs);
    for (auto it = mempool.mapTx.begin(); it != mempool.mapTx.end(); ++it) {
        if (MempoolPriorityHelper::HasGuaranteedInclusion(*it)) {
            guaranteed.insert(it);
        }
    }
    
    return guaranteed;
}

std::vector<CTxMemPool::txiter> BlockAssemblerPriorityHelper::SortByPriority(
    const CTxMemPool::setEntries& transactions)
{
    std::vector<CTxMemPool::txiter> sorted(transactions.begin(), transactions.end());
    
    std::sort(sorted.begin(), sorted.end(),
        [](const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) {
            CompareTxMemPoolEntryByReputationPriority comp;
            return comp(*a, *b);
        });
    
    return sorted;
}

bool BlockAssemblerPriorityHelper::ShouldIncludeInBlock(
    const CTxMemPoolEntry& entry,
    uint64_t networkLoad,
    uint64_t blockSpaceRemaining)
{
    // Guaranteed inclusion transactions always included
    if (MempoolPriorityHelper::HasGuaranteedInclusion(entry)) {
        return true;
    }
    
    // Check if CVM/EVM transaction
    if (!MempoolPriorityHelper::IsCVMTransaction(entry)) {
        return false; // Let standard Bitcoin logic handle non-CVM transactions
    }
    
    // Get priority level
    TransactionPriorityManager::PriorityLevel priority = 
        MempoolPriorityHelper::GetPriorityLevel(entry);
    
    // During high network load, only include high priority transactions
    if (networkLoad > 80) {
        return priority <= TransactionPriorityManager::PriorityLevel::HIGH;
    }
    
    // During moderate load, include normal and above
    if (networkLoad > 50) {
        return priority <= TransactionPriorityManager::PriorityLevel::NORMAL;
    }
    
    // During low load, include all transactions
    return true;
}

} // namespace CVM
