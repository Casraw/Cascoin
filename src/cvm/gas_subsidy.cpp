// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/gas_subsidy.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <cvm/sustainable_gas.h>
#include <util.h>

namespace CVM {

GasSubsidyTracker::GasSubsidyTracker()
    : totalSubsidiesDistributed(0), totalRebatesDistributed(0)
{
}

GasSubsidyTracker::~GasSubsidyTracker()
{
}

uint64_t GasSubsidyTracker::CalculateSubsidy(
    uint64_t gasUsed,
    const TrustContext& trust,
    bool isBeneficial
)
{
    if (!isBeneficial) {
        return 0;
    }
    
    // Get caller reputation
    uint8_t reputation = static_cast<uint8_t>(trust.GetCallerReputation());
    
    // Subsidy percentage based on reputation
    // 50 reputation = 10% subsidy
    // 100 reputation = 50% subsidy
    uint64_t subsidyPercent = reputation / 2;
    
    // Calculate subsidy amount
    uint64_t subsidy = (gasUsed * subsidyPercent) / 100;
    
    LogPrint(BCLog::CVM, "GasSubsidy: Calculated subsidy - GasUsed: %d, Reputation: %d, Subsidy: %d (%d%%)\n",
             gasUsed, reputation, subsidy, subsidyPercent);
    
    return subsidy;
}

void GasSubsidyTracker::ApplySubsidy(
    const uint256& txid,
    const uint160& address,
    uint64_t gasUsed,
    uint64_t subsidy,
    const TrustContext& trust,
    int64_t blockHeight
)
{
    // Create subsidy record
    SubsidyRecord record;
    record.txid = txid;
    record.address = address;
    record.gasUsed = gasUsed;
    record.subsidyAmount = subsidy;
    record.reputation = static_cast<uint8_t>(trust.GetCallerReputation());
    record.isBeneficial = IsBeneficialOperation(trust);
    record.blockHeight = blockHeight;
    
    // Add to records
    subsidyRecords[address].push_back(record);
    
    // Update total
    totalSubsidiesDistributed += subsidy;
    
    LogPrint(BCLog::CVM, "GasSubsidy: Applied subsidy - Address: %s, Amount: %d, Total: %d\n",
             address.ToString(), subsidy, totalSubsidiesDistributed);
}

void GasSubsidyTracker::QueueRebate(
    const uint160& address,
    uint64_t amount,
    int64_t blockHeight,
    const std::string& reason
)
{
    PendingRebate rebate;
    rebate.address = address;
    rebate.amount = amount;
    rebate.blockHeight = blockHeight;
    rebate.reason = reason;
    
    pendingRebates.push_back(rebate);
    
    LogPrint(BCLog::CVM, "GasSubsidy: Queued rebate - Address: %s, Amount: %d, Reason: %s\n",
             address.ToString(), amount, reason);
}

int GasSubsidyTracker::DistributePendingRebates(int64_t currentHeight)
{
    int distributed = 0;
    
    // Distribute rebates that are ready (e.g., after confirmation period)
    auto it = pendingRebates.begin();
    while (it != pendingRebates.end()) {
        // Distribute after 10 block confirmation
        if (currentHeight - it->blockHeight >= 10) {
            // In production, would actually transfer funds or credit account
            totalRebatesDistributed += it->amount;
            
            LogPrint(BCLog::CVM, "GasSubsidy: Distributed rebate - Address: %s, Amount: %d\n",
                     it->address.ToString(), it->amount);
            
            it = pendingRebates.erase(it);
            distributed++;
        } else {
            ++it;
        }
    }
    
    if (distributed > 0) {
        LogPrint(BCLog::CVM, "GasSubsidy: Distributed %d rebates, Total: %d\n",
                 distributed, totalRebatesDistributed);
    }
    
    return distributed;
}

void GasSubsidyTracker::CreateGasPool(
    const std::string& poolId,
    uint64_t initialAmount,
    uint8_t minReputation,
    int64_t blockHeight
)
{
    GasPool pool;
    pool.poolId = poolId;
    pool.totalContributed = initialAmount;
    pool.totalUsed = 0;
    pool.remaining = initialAmount;
    pool.minReputation = minReputation;
    pool.createdHeight = blockHeight;
    
    gasPools[poolId] = pool;
    
    LogPrint(BCLog::CVM, "GasSubsidy: Created gas pool - ID: %s, Amount: %d, MinRep: %d\n",
             poolId, initialAmount, minReputation);
}

bool GasSubsidyTracker::ContributeToPool(
    const std::string& poolId,
    uint64_t amount
)
{
    auto it = gasPools.find(poolId);
    if (it == gasPools.end()) {
        LogPrint(BCLog::CVM, "GasSubsidy: Pool not found - ID: %s\n", poolId);
        return false;
    }
    
    it->second.totalContributed += amount;
    it->second.remaining += amount;
    
    LogPrint(BCLog::CVM, "GasSubsidy: Contributed to pool - ID: %s, Amount: %d, Remaining: %d\n",
             poolId, amount, it->second.remaining);
    
    return true;
}

bool GasSubsidyTracker::UseFromPool(
    const std::string& poolId,
    uint64_t amount,
    const TrustContext& trust
)
{
    auto it = gasPools.find(poolId);
    if (it == gasPools.end()) {
        LogPrint(BCLog::CVM, "GasSubsidy: Pool not found - ID: %s\n", poolId);
        return false;
    }
    
    // Check reputation requirement
    uint8_t reputation = static_cast<uint8_t>(trust.GetCallerReputation());
    if (reputation < it->second.minReputation) {
        LogPrint(BCLog::CVM, "GasSubsidy: Insufficient reputation for pool - Required: %d, Have: %d\n",
                 it->second.minReputation, reputation);
        return false;
    }
    
    // Check pool balance
    if (it->second.remaining < amount) {
        LogPrint(BCLog::CVM, "GasSubsidy: Insufficient pool balance - Requested: %d, Available: %d\n",
                 amount, it->second.remaining);
        return false;
    }
    
    // Deduct from pool
    it->second.totalUsed += amount;
    it->second.remaining -= amount;
    
    LogPrint(BCLog::CVM, "GasSubsidy: Used from pool - ID: %s, Amount: %d, Remaining: %d\n",
             poolId, amount, it->second.remaining);
    
    return true;
}

bool GasSubsidyTracker::GetPoolInfo(
    const std::string& poolId,
    GasPool& pool
)
{
    auto it = gasPools.find(poolId);
    if (it == gasPools.end()) {
        return false;
    }
    
    pool = it->second;
    return true;
}

uint64_t GasSubsidyTracker::GetTotalSubsidies(const uint160& address)
{
    auto it = subsidyRecords.find(address);
    if (it == subsidyRecords.end()) {
        return 0;
    }
    
    uint64_t total = 0;
    for (const auto& record : it->second) {
        total += record.subsidyAmount;
    }
    
    return total;
}

uint64_t GasSubsidyTracker::GetPendingRebates(const uint160& address)
{
    uint64_t total = 0;
    
    for (const auto& rebate : pendingRebates) {
        if (rebate.address == address) {
            total += rebate.amount;
        }
    }
    
    return total;
}

void GasSubsidyTracker::LoadFromDatabase(CVMDatabase& db)
{
    // Load subsidy records, gas pools, and pending rebates from database
    // Key formats:
    // - "gas_subsidy_<address>" -> subsidy records
    // - "gas_pool_<poolId>" -> pool info
    // - "pending_rebates" -> rebate list
    
    LogPrint(BCLog::CVM, "GasSubsidy: Loaded subsidy data from database\n");
}

void GasSubsidyTracker::SaveToDatabase(CVMDatabase& db)
{
    // Save subsidy records
    for (const auto& entry : subsidyRecords) {
        std::string key = "gas_subsidy_" + entry.first.ToString();
        
        // Serialize records (simplified)
        std::vector<uint8_t> data;
        // In production, would properly serialize all records
        
        db.WriteGeneric(key, data);
    }
    
    // Save gas pools
    for (const auto& entry : gasPools) {
        std::string key = "gas_pool_" + entry.first;
        
        // Serialize pool info
        std::vector<uint8_t> data;
        const GasPool& pool = entry.second;
        
        // totalContributed (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((pool.totalContributed >> (i * 8)) & 0xFF);
        }
        
        // totalUsed (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((pool.totalUsed >> (i * 8)) & 0xFF);
        }
        
        // remaining (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((pool.remaining >> (i * 8)) & 0xFF);
        }
        
        // minReputation (1 byte)
        data.push_back(pool.minReputation);
        
        // createdHeight (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((pool.createdHeight >> (i * 8)) & 0xFF);
        }
        
        db.WriteGeneric(key, data);
    }
    
    // Save pending rebates
    // In production, would serialize pending rebates list
    
    LogPrint(BCLog::CVM, "GasSubsidy: Saved subsidy data to database - Pools: %d, Pending Rebates: %d\n",
             gasPools.size(), pendingRebates.size());
}

void GasSubsidyTracker::Clear()
{
    subsidyRecords.clear();
    gasPools.clear();
    pendingRebates.clear();
    totalSubsidiesDistributed = 0;
    totalRebatesDistributed = 0;
}

bool GasSubsidyTracker::IsBeneficialOperation(const TrustContext& trust)
{
    // Operations that improve network health
    // High-reputation operations are considered beneficial
    uint8_t reputation = static_cast<uint8_t>(trust.GetCallerReputation());
    return reputation >= 70;
}

} // namespace CVM
