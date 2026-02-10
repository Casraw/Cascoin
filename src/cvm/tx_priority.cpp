// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/tx_priority.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <cvm/reputation.h>
#include <hash.h>
#include <util.h>
#include <timedata.h>

namespace CVM {

TransactionPriorityManager::TransactionPriorityManager()
    : networkCongestion(0)
{
}

TransactionPriorityManager::~TransactionPriorityManager()
{
}

TransactionPriorityManager::TransactionPriority TransactionPriorityManager::CalculatePriority(
    const CTransaction& tx,
    CVMDatabase& db
)
{
    TransactionPriority priority;
    priority.txid = tx.GetHash();
    priority.timestamp = GetTime();
    
    // Extract sender address
    uint160 senderAddr;
    if (!ExtractSenderAddress(tx, senderAddr)) {
        // Default to low priority if can't extract sender
        priority.reputation = 0;
        priority.level = PriorityLevel::LOW;
        priority.guaranteedInclusion = false;
        return priority;
    }
    
    // Get reputation score
    ReputationSystem repSystem(db);
    ReputationScore score;
    repSystem.GetReputation(senderAddr, score);
    
    priority.reputation = static_cast<uint8_t>(std::min(int64_t(100), std::max(int64_t(0), score.score / 100)));
    priority.level = GetPriorityLevel(priority.reputation);
    priority.guaranteedInclusion = HasGuaranteedInclusion(priority.reputation);
    
    // Cache the priority
    CachePriority(priority.txid, priority);
    
    LogPrint(BCLog::CVM, "TxPriority: Calculated priority for tx %s - Reputation: %d, Level: %d, Guaranteed: %s\n",
             priority.txid.ToString(), priority.reputation, static_cast<int>(priority.level),
             priority.guaranteedInclusion ? "yes" : "no");
    
    return priority;
}

TransactionPriorityManager::PriorityLevel TransactionPriorityManager::GetPriorityLevel(uint8_t reputation)
{
    if (reputation >= 90) {
        return PriorityLevel::CRITICAL;  // 90+ = Critical priority
    } else if (reputation >= 70) {
        return PriorityLevel::HIGH;      // 70-89 = High priority
    } else if (reputation >= 50) {
        return PriorityLevel::NORMAL;    // 50-69 = Normal priority
    } else {
        return PriorityLevel::LOW;       // <50 = Low priority
    }
}

bool TransactionPriorityManager::HasGuaranteedInclusion(uint8_t reputation)
{
    // 90+ reputation gets guaranteed inclusion
    return reputation >= 90;
}

bool TransactionPriorityManager::CompareTransactionPriority(
    const TransactionPriority& a,
    const TransactionPriority& b
)
{
    // First, compare by guaranteed inclusion
    if (a.guaranteedInclusion != b.guaranteedInclusion) {
        return a.guaranteedInclusion;  // Guaranteed inclusion comes first
    }
    
    // Then compare by priority level (lower number = higher priority)
    if (a.level != b.level) {
        return static_cast<int>(a.level) < static_cast<int>(b.level);
    }
    
    // Then compare by reputation score
    if (a.reputation != b.reputation) {
        return a.reputation > b.reputation;
    }
    
    // Finally, compare by timestamp (older first)
    return a.timestamp < b.timestamp;
}

int64_t TransactionPriorityManager::GetPriorityScore(const TransactionPriority& priority)
{
    // Calculate priority score (0-1000)
    // Higher score = higher priority
    
    int64_t score = 0;
    
    // Guaranteed inclusion gets massive boost
    if (priority.guaranteedInclusion) {
        score += 500;
    }
    
    // Priority level contribution (400 points max)
    switch (priority.level) {
        case PriorityLevel::CRITICAL:
            score += 400;
            break;
        case PriorityLevel::HIGH:
            score += 300;
            break;
        case PriorityLevel::NORMAL:
            score += 200;
            break;
        case PriorityLevel::LOW:
            score += 100;
            break;
    }
    
    // Reputation contribution (100 points max)
    score += priority.reputation;
    
    return score;
}

void TransactionPriorityManager::CachePriority(const uint256& txid, const TransactionPriority& priority)
{
    priorityCache[txid] = priority;
}

bool TransactionPriorityManager::GetCachedPriority(const uint256& txid, TransactionPriority& priority)
{
    auto it = priorityCache.find(txid);
    if (it == priorityCache.end()) {
        return false;
    }
    
    priority = it->second;
    return true;
}

void TransactionPriorityManager::RemoveFromCache(const uint256& txid)
{
    priorityCache.erase(txid);
}

void TransactionPriorityManager::ClearCache()
{
    priorityCache.clear();
}

uint8_t TransactionPriorityManager::GetNetworkCongestion() const
{
    return networkCongestion;
}

void TransactionPriorityManager::UpdateNetworkCongestion(size_t mempoolSize, size_t maxMempoolSize)
{
    if (maxMempoolSize == 0) {
        networkCongestion = 0;
        return;
    }
    
    // Calculate congestion as percentage of max mempool size
    double congestionRatio = static_cast<double>(mempoolSize) / static_cast<double>(maxMempoolSize);
    networkCongestion = static_cast<uint8_t>(std::min(100.0, congestionRatio * 100.0));
    
    LogPrint(BCLog::CVM, "TxPriority: Network congestion updated - %d%% (%d/%d transactions)\n",
             networkCongestion, mempoolSize, maxMempoolSize);
}

bool TransactionPriorityManager::ExtractSenderAddress(const CTransaction& tx, uint160& senderAddr)
{
    // Extract sender from first input
    // This is simplified - in production would need proper address extraction
    if (tx.vin.empty()) {
        return false;
    }
    
    // Hash the prevout to get a pseudo-address
    // In production, would extract actual address from scriptSig or witness
    CHashWriter hw(SER_GETHASH, 0);
    hw << tx.vin[0].prevout;
    uint256 hash = hw.GetHash();
    memcpy(senderAddr.begin(), hash.begin(), 20);
    
    return true;
}

} // namespace CVM
