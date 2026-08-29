// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file burn_registry.cpp
 * @brief Implementation of BurnRegistry for L2 Burn-and-Mint Token Model
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <l2/burn_registry.h>

namespace l2 {

// ============================================================================
// BurnRegistry Implementation
// ============================================================================

BurnRegistry::BurnRegistry()
    : totalBurned_(0)
{
}

BurnRegistry::~BurnRegistry() = default;

bool BurnRegistry::IsProcessed(const uint256& l1TxHash) const
{
    LOCK(cs_registry_);
    return burnRecords_.count(l1TxHash) > 0;
}

bool BurnRegistry::RecordBurn(const BurnRecord& record)
{
    LOCK(cs_registry_);
    
    // Check if already processed (double-mint prevention)
    if (burnRecords_.count(record.l1TxHash) > 0) {
        return false;
    }
    
    // Validate the record
    if (!record.IsValid()) {
        return false;
    }
    
    // Store the record
    burnRecords_[record.l1TxHash] = record;
    
    // Update indexes
    AddIndexes(record);
    
    // Update total burned
    totalBurned_ += record.amount;
    
    return true;
}

std::optional<BurnRecord> BurnRegistry::GetBurnRecord(const uint256& l1TxHash) const
{
    LOCK(cs_registry_);
    
    auto it = burnRecords_.find(l1TxHash);
    if (it != burnRecords_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<BurnRecord> BurnRegistry::GetBurnsForAddress(const uint160& address) const
{
    LOCK(cs_registry_);
    
    std::vector<BurnRecord> result;
    
    auto it = addressIndex_.find(address);
    if (it != addressIndex_.end()) {
        for (const uint256& txHash : it->second) {
            auto recordIt = burnRecords_.find(txHash);
            if (recordIt != burnRecords_.end()) {
                result.push_back(recordIt->second);
            }
        }
    }
    
    return result;
}

CAmount BurnRegistry::GetTotalBurned() const
{
    LOCK(cs_registry_);
    return totalBurned_;
}

std::vector<BurnRecord> BurnRegistry::GetBurnHistory(uint64_t fromBlock, uint64_t toBlock) const
{
    LOCK(cs_registry_);
    
    std::vector<BurnRecord> result;
    
    // Iterate through block index for the range
    for (auto it = blockIndex_.lower_bound(fromBlock); 
         it != blockIndex_.end() && it->first <= toBlock; 
         ++it) {
        for (const uint256& txHash : it->second) {
            auto recordIt = burnRecords_.find(txHash);
            if (recordIt != burnRecords_.end()) {
                result.push_back(recordIt->second);
            }
        }
    }
    
    return result;
}

size_t BurnRegistry::HandleReorg(uint64_t reorgFromBlock)
{
    LOCK(cs_registry_);
    
    size_t removedCount = 0;
    std::vector<uint256> toRemove;
    
    // Find all burns in blocks >= reorgFromBlock
    for (auto it = blockIndex_.lower_bound(reorgFromBlock); 
         it != blockIndex_.end(); 
         ++it) {
        for (const uint256& txHash : it->second) {
            toRemove.push_back(txHash);
        }
    }
    
    // Remove each burn record
    for (const uint256& txHash : toRemove) {
        auto recordIt = burnRecords_.find(txHash);
        if (recordIt != burnRecords_.end()) {
            // Update total burned
            totalBurned_ -= recordIt->second.amount;
            
            // Remove indexes
            RemoveIndexes(recordIt->second);
            
            // Remove the record
            burnRecords_.erase(recordIt);
            
            ++removedCount;
        }
    }
    
    return removedCount;
}

size_t BurnRegistry::GetBurnCount() const
{
    LOCK(cs_registry_);
    return burnRecords_.size();
}

void BurnRegistry::Clear()
{
    LOCK(cs_registry_);
    
    burnRecords_.clear();
    addressIndex_.clear();
    blockIndex_.clear();
    totalBurned_ = 0;
}

std::vector<BurnRecord> BurnRegistry::GetAllBurns() const
{
    LOCK(cs_registry_);
    
    std::vector<BurnRecord> result;
    result.reserve(burnRecords_.size());
    
    for (const auto& pair : burnRecords_) {
        result.push_back(pair.second);
    }
    
    return result;
}

void BurnRegistry::AddIndexes(const BurnRecord& record)
{
    // Add to address index
    addressIndex_[record.l2Recipient].insert(record.l1TxHash);
    
    // Add to block index (for reorg handling)
    blockIndex_[record.l2MintBlock].insert(record.l1TxHash);
}

void BurnRegistry::RemoveIndexes(const BurnRecord& record)
{
    // Remove from address index
    auto addrIt = addressIndex_.find(record.l2Recipient);
    if (addrIt != addressIndex_.end()) {
        addrIt->second.erase(record.l1TxHash);
        if (addrIt->second.empty()) {
            addressIndex_.erase(addrIt);
        }
    }
    
    // Remove from block index
    auto blockIt = blockIndex_.find(record.l2MintBlock);
    if (blockIt != blockIndex_.end()) {
        blockIt->second.erase(record.l1TxHash);
        if (blockIt->second.empty()) {
            blockIndex_.erase(blockIt);
        }
    }
}

} // namespace l2
