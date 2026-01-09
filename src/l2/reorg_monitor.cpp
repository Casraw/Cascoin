// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file reorg_monitor.cpp
 * @brief Implementation of L1 Reorganization Monitor for Cascoin L2
 * 
 * Requirements: 19.1, 19.2, 19.3, 19.4, 19.5, 19.6
 */

#include <l2/reorg_monitor.h>
#include <hash.h>
#include <util.h>

#include <algorithm>
#include <sstream>

namespace l2 {

// Maximum L1 blocks to keep in history
static constexpr size_t MAX_L1_HISTORY = 1000;

// Maximum anchor points to keep
static constexpr size_t MAX_ANCHOR_POINTS = 500;

ReorgMonitor::ReorgMonitor(
    uint64_t chainId,
    std::shared_ptr<L2StateManager> stateManager,
    uint32_t finalityDepth)
    : chainId_(chainId)
    , finalityDepth_(finalityDepth)
    , stateManager_(stateManager)
{
}

ReorgMonitor::~ReorgMonitor() = default;

// =============================================================================
// L1 Block Tracking (Requirement 19.1)
// =============================================================================

ReorgDetectionResult ReorgMonitor::ProcessL1Block(const L1BlockInfo& blockInfo)
{
    LOCK(cs_reorg_);

    // Check for reorg if we have a previous tip
    ReorgDetectionResult result = ReorgDetectionResult::NoReorg();
    
    if (currentL1Tip_.blockNumber > 0) {
        // Check if this block extends the current chain
        if (blockInfo.blockNumber == currentL1Tip_.blockNumber + 1 &&
            blockInfo.prevBlockHash == currentL1Tip_.blockHash) {
            // Normal chain extension, no reorg
        } else if (blockInfo.blockNumber <= currentL1Tip_.blockNumber) {
            // Potential reorg - new block at same or lower height
            result = CheckForReorg(blockInfo);
        } else if (blockInfo.prevBlockHash != currentL1Tip_.blockHash) {
            // Gap in blocks or different parent - check for reorg
            result = CheckForReorg(blockInfo);
        }
    }

    // Store the block in history
    l1BlockHistory_[blockInfo.blockNumber] = blockInfo;
    
    // Update current tip if this is the new tip
    if (blockInfo.blockNumber > currentL1Tip_.blockNumber ||
        (result.reorgDetected && blockInfo.blockNumber >= result.forkPoint)) {
        currentL1Tip_ = blockInfo;
    }

    // Update anchor finalization status
    for (auto& [l1Block, anchor] : anchorPoints_) {
        if (!anchor.isFinalized && 
            currentL1Tip_.blockNumber >= l1Block + finalityDepth_) {
            anchor.isFinalized = true;
        }
    }

    // Prune old history
    PruneL1History(MAX_L1_HISTORY);

    return result;
}

ReorgDetectionResult ReorgMonitor::CheckForReorg(const L1BlockInfo& currentTip)
{
    LOCK(cs_reorg_);

    // If no previous tip, no reorg possible
    if (currentL1Tip_.blockNumber == 0) {
        return ReorgDetectionResult::NoReorg();
    }

    // If same block, no reorg
    if (currentTip.blockHash == currentL1Tip_.blockHash) {
        return ReorgDetectionResult::NoReorg();
    }

    // Find the fork point
    uint64_t forkPoint = FindForkPoint(currentL1Tip_, currentTip);
    
    if (forkPoint == 0) {
        // Could not find fork point - might be too deep
        return ReorgDetectionResult::Error("Could not find fork point");
    }

    // Calculate reorg depth
    uint32_t reorgDepth = static_cast<uint32_t>(currentL1Tip_.blockNumber - forkPoint);
    
    if (reorgDepth > MAX_REORG_DEPTH) {
        return ReorgDetectionResult::Error(
            "Reorg depth " + std::to_string(reorgDepth) + 
            " exceeds maximum " + std::to_string(MAX_REORG_DEPTH));
    }

    // Get fork point hash
    uint256 forkPointHash;
    auto it = l1BlockHistory_.find(forkPoint);
    if (it != l1BlockHistory_.end()) {
        forkPointHash = it->second.blockHash;
    }

    return ReorgDetectionResult::Detected(
        reorgDepth, forkPoint, forkPointHash, currentL1Tip_, currentTip);
}

L1BlockInfo ReorgMonitor::GetCurrentL1Tip() const
{
    LOCK(cs_reorg_);
    return currentL1Tip_;
}

std::optional<L1BlockInfo> ReorgMonitor::GetL1Block(uint64_t blockNumber) const
{
    LOCK(cs_reorg_);
    auto it = l1BlockHistory_.find(blockNumber);
    if (it != l1BlockHistory_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// =============================================================================
// State Reversion (Requirement 19.2)
// =============================================================================

bool ReorgMonitor::RevertToLastValidAnchor(uint64_t forkPoint)
{
    LOCK(cs_reorg_);

    // Find the last valid anchor before the fork point
    auto anchor = GetLastValidAnchor(forkPoint);
    if (!anchor) {
        LogPrintf("ReorgMonitor: No valid anchor found before fork point %lu\n", forkPoint);
        return false;
    }

    // Revert state manager to the anchor's state root
    if (stateManager_) {
        bool reverted = stateManager_->RevertToStateRoot(anchor->l2StateRoot);
        if (!reverted) {
            LogPrintf("ReorgMonitor: Failed to revert state to root %s\n",
                     anchor->l2StateRoot.GetHex());
            return false;
        }
        stateManager_->SetBlockNumber(anchor->l2BlockNumber);
    }

    // Remove anchor points after the fork
    auto it = anchorPoints_.upper_bound(forkPoint);
    while (it != anchorPoints_.end()) {
        it = anchorPoints_.erase(it);
    }

    // Remove L1 blocks after the fork from history
    auto blockIt = l1BlockHistory_.upper_bound(forkPoint);
    while (blockIt != l1BlockHistory_.end()) {
        blockIt = l1BlockHistory_.erase(blockIt);
    }

    LogPrintf("ReorgMonitor: Reverted to anchor at L1 block %lu, L2 block %lu, state root %s\n",
             anchor->l1BlockNumber, anchor->l2BlockNumber, anchor->l2StateRoot.GetHex());

    return true;
}

std::optional<L2AnchorPoint> ReorgMonitor::GetLastValidAnchor(uint64_t beforeL1Block) const
{
    LOCK(cs_reorg_);

    // Find the highest anchor point before the given L1 block
    std::optional<L2AnchorPoint> result;
    
    for (auto it = anchorPoints_.rbegin(); it != anchorPoints_.rend(); ++it) {
        if (it->first < beforeL1Block) {
            result = it->second;
            break;
        }
    }

    return result;
}

std::vector<L2AnchorPoint> ReorgMonitor::GetAnchorPoints() const
{
    LOCK(cs_reorg_);
    std::vector<L2AnchorPoint> result;
    result.reserve(anchorPoints_.size());
    for (const auto& [_, anchor] : anchorPoints_) {
        result.push_back(anchor);
    }
    return result;
}

// =============================================================================
// Transaction Replay (Requirement 19.3)
// =============================================================================

size_t ReorgMonitor::ReplayTransactions(uint64_t fromL2Block, uint64_t toL2Block)
{
    LOCK(cs_reorg_);

    if (!stateManager_) {
        LogPrintf("ReorgMonitor: Cannot replay transactions - no state manager\n");
        return 0;
    }

    // Get transactions to replay
    std::vector<L2TxLogEntry> txsToReplay = GetTransactionsInRange(fromL2Block, toL2Block);
    
    if (txsToReplay.empty()) {
        return 0;
    }

    // Sort by block number and original order
    std::sort(txsToReplay.begin(), txsToReplay.end(),
        [](const L2TxLogEntry& a, const L2TxLogEntry& b) {
            if (a.l2BlockNumber != b.l2BlockNumber) {
                return a.l2BlockNumber < b.l2BlockNumber;
            }
            return a.timestamp < b.timestamp;
        });

    size_t replayed = 0;
    uint64_t currentBlock = fromL2Block;

    for (const auto& entry : txsToReplay) {
        // Update block number if needed
        if (entry.l2BlockNumber > currentBlock) {
            currentBlock = entry.l2BlockNumber;
            stateManager_->SetBlockNumber(currentBlock);
        }

        // Deserialize the transaction
        CMutableTransaction mtx;
        if (!entry.GetTransaction(mtx)) {
            LogPrintf("ReorgMonitor: Failed to deserialize tx %s\n", entry.txHash.GetHex());
            continue;
        }
        CTransaction tx(mtx);

        // Replay the transaction
        auto result = stateManager_->ApplyTransaction(tx, currentBlock);
        
        if (result.success) {
            ++replayed;
            
            // Update the log entry with new execution result
            auto logIt = transactionLogs_.find(entry.txHash);
            if (logIt != transactionLogs_.end()) {
                logIt->second.wasSuccessful = true;
                logIt->second.gasUsed = result.gasUsed;
            }
        } else {
            LogPrintf("ReorgMonitor: Failed to replay tx %s: %s\n",
                     entry.txHash.GetHex(), result.error);
        }
    }

    LogPrintf("ReorgMonitor: Replayed %zu/%zu transactions from L2 blocks %lu to %lu\n",
             replayed, txsToReplay.size(), fromL2Block, toL2Block);

    return replayed;
}

std::vector<L2TxLogEntry> ReorgMonitor::GetTransactionsForReplay(uint64_t fromL2Block) const
{
    LOCK(cs_reorg_);
    
    std::vector<L2TxLogEntry> result;
    
    for (auto it = txLogsByBlock_.lower_bound(fromL2Block); 
         it != txLogsByBlock_.end(); ++it) {
        for (const auto& txHash : it->second) {
            auto logIt = transactionLogs_.find(txHash);
            if (logIt != transactionLogs_.end()) {
                result.push_back(logIt->second);
            }
        }
    }

    return result;
}

// =============================================================================
// Full Reorg Recovery
// =============================================================================

ReorgRecoveryResult ReorgMonitor::HandleReorg(const ReorgDetectionResult& detection)
{
    if (!detection.reorgDetected) {
        return ReorgRecoveryResult::Failure("No reorg detected");
    }

    LOCK(cs_reorg_);

    LogPrintf("ReorgMonitor: Handling reorg of depth %u at fork point %lu\n",
             detection.reorgDepth, detection.forkPoint);

    // Step 1: Find last valid anchor (Requirement 19.2)
    auto anchor = GetLastValidAnchor(detection.forkPoint);
    if (!anchor) {
        return ReorgRecoveryResult::Failure("No valid anchor found before fork point");
    }

    // Step 2: Get affected transactions before reverting (Requirement 19.4)
    std::vector<uint256> affectedTxs = GetAffectedTransactions(detection.forkPoint);

    // Step 3: Revert to last valid anchor (Requirement 19.2)
    if (!RevertToLastValidAnchor(detection.forkPoint)) {
        return ReorgRecoveryResult::Failure("Failed to revert to last valid anchor");
    }

    // Step 4: Replay transactions (Requirement 19.3)
    uint64_t replayFromBlock = anchor->l2BlockNumber + 1;
    size_t replayed = 0;
    size_t failed = 0;

    if (stateManager_) {
        auto txsToReplay = GetTransactionsForReplay(replayFromBlock);
        
        for (auto& entry : txsToReplay) {
            CMutableTransaction mtx;
            if (!entry.GetTransaction(mtx)) {
                ++failed;
                continue;
            }
            CTransaction tx(mtx);
            auto result = stateManager_->ApplyTransaction(tx, entry.l2BlockNumber);
            if (result.success) {
                ++replayed;
            } else {
                ++failed;
            }
        }
    }

    // Step 5: Update current L1 tip
    currentL1Tip_ = detection.newTip;
    l1BlockHistory_[detection.newTip.blockNumber] = detection.newTip;

    // Get final state
    uint256 newStateRoot;
    uint64_t newBlockNumber = anchor->l2BlockNumber;
    
    if (stateManager_) {
        newStateRoot = stateManager_->GetStateRoot();
        newBlockNumber = stateManager_->GetBlockNumber();
    }

    auto result = ReorgRecoveryResult::Success(
        newStateRoot, newBlockNumber, replayed, failed, affectedTxs);

    // Step 6: Notify callbacks (Requirement 19.4)
    NotifyCallbacks(detection, result);

    LogPrintf("ReorgMonitor: Recovery complete - replayed %zu txs, %zu failed, %zu affected\n",
             replayed, failed, affectedTxs.size());

    return result;
}

// =============================================================================
// Anchor Point Management
// =============================================================================

void ReorgMonitor::AddAnchorPoint(const L2AnchorPoint& anchor)
{
    LOCK(cs_reorg_);
    
    anchorPoints_[anchor.l1BlockNumber] = anchor;
    
    // Check if already finalized
    if (currentL1Tip_.blockNumber >= anchor.l1BlockNumber + finalityDepth_) {
        anchorPoints_[anchor.l1BlockNumber].isFinalized = true;
    }

    // Create state snapshot if we have a state manager
    if (stateManager_) {
        stateManager_->CreateSnapshot(anchor.l2BlockNumber, anchor.l1BlockNumber);
    }

    // Prune old anchors
    PruneAnchorPoints(MAX_ANCHOR_POINTS);

    LogPrintf("ReorgMonitor: Added anchor at L1 block %lu, L2 block %lu\n",
             anchor.l1BlockNumber, anchor.l2BlockNumber);
}

void ReorgMonitor::UpdateAnchorFinalization(uint64_t l1BlockNumber, uint32_t confirmations)
{
    LOCK(cs_reorg_);
    
    auto it = anchorPoints_.find(l1BlockNumber);
    if (it != anchorPoints_.end()) {
        if (confirmations >= finalityDepth_) {
            it->second.isFinalized = true;
        }
    }
}

bool ReorgMonitor::IsAnchorFinalized(uint64_t l1BlockNumber) const
{
    LOCK(cs_reorg_);
    
    auto it = anchorPoints_.find(l1BlockNumber);
    if (it != anchorPoints_.end()) {
        return it->second.isFinalized;
    }
    return false;
}

std::optional<L2AnchorPoint> ReorgMonitor::GetLatestFinalizedAnchor() const
{
    LOCK(cs_reorg_);
    
    for (auto it = anchorPoints_.rbegin(); it != anchorPoints_.rend(); ++it) {
        if (it->second.isFinalized) {
            return it->second;
        }
    }
    return std::nullopt;
}

// =============================================================================
// Transaction Logging (Requirement 19.6)
// =============================================================================

void ReorgMonitor::LogTransaction(const L2TxLogEntry& entry)
{
    LOCK(cs_reorg_);
    
    transactionLogs_[entry.txHash] = entry;
    txLogsByBlock_[entry.l2BlockNumber].push_back(entry.txHash);

    // Prune if too many logs
    if (transactionLogs_.size() > MAX_TX_LOG_SIZE) {
        // Find oldest block with logs
        if (!txLogsByBlock_.empty()) {
            uint64_t oldestBlock = txLogsByBlock_.begin()->first;
            PruneTransactionLogs(oldestBlock + 1);
        }
    }
}

std::optional<L2TxLogEntry> ReorgMonitor::GetTransactionLog(const uint256& txHash) const
{
    LOCK(cs_reorg_);
    
    auto it = transactionLogs_.find(txHash);
    if (it != transactionLogs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<L2TxLogEntry> ReorgMonitor::GetTransactionsInRange(
    uint64_t fromBlock, uint64_t toBlock) const
{
    LOCK(cs_reorg_);
    
    std::vector<L2TxLogEntry> result;
    
    for (auto it = txLogsByBlock_.lower_bound(fromBlock);
         it != txLogsByBlock_.end() && it->first <= toBlock; ++it) {
        for (const auto& txHash : it->second) {
            auto logIt = transactionLogs_.find(txHash);
            if (logIt != transactionLogs_.end()) {
                result.push_back(logIt->second);
            }
        }
    }

    return result;
}

size_t ReorgMonitor::PruneTransactionLogs(uint64_t beforeL2Block)
{
    LOCK(cs_reorg_);
    
    size_t pruned = 0;
    
    auto it = txLogsByBlock_.begin();
    while (it != txLogsByBlock_.end() && it->first < beforeL2Block) {
        for (const auto& txHash : it->second) {
            transactionLogs_.erase(txHash);
            ++pruned;
        }
        it = txLogsByBlock_.erase(it);
    }

    return pruned;
}

// =============================================================================
// Notification (Requirement 19.4)
// =============================================================================

void ReorgMonitor::RegisterNotificationCallback(ReorgNotificationCallback callback)
{
    LOCK(cs_reorg_);
    notificationCallbacks_.push_back(std::move(callback));
}

std::vector<uint256> ReorgMonitor::GetAffectedTransactions(uint64_t forkPoint) const
{
    LOCK(cs_reorg_);
    
    std::vector<uint256> affected;
    
    // Find anchor at or after fork point
    auto anchorIt = anchorPoints_.lower_bound(forkPoint);
    uint64_t affectedFromL2Block = 0;
    
    if (anchorIt != anchorPoints_.end()) {
        affectedFromL2Block = anchorIt->second.l2BlockNumber;
    } else if (!anchorPoints_.empty()) {
        // Use the last anchor's L2 block
        affectedFromL2Block = anchorPoints_.rbegin()->second.l2BlockNumber;
    }

    // Get all transactions from affected L2 blocks
    for (auto it = txLogsByBlock_.lower_bound(affectedFromL2Block);
         it != txLogsByBlock_.end(); ++it) {
        for (const auto& txHash : it->second) {
            affected.push_back(txHash);
        }
    }

    return affected;
}

// =============================================================================
// Configuration and Status
// =============================================================================

void ReorgMonitor::SetFinalityDepth(uint32_t depth)
{
    LOCK(cs_reorg_);
    finalityDepth_ = depth;
}

uint32_t ReorgMonitor::GetFinalityDepth() const
{
    LOCK(cs_reorg_);
    return finalityDepth_;
}

std::string ReorgMonitor::GetStatistics() const
{
    LOCK(cs_reorg_);
    
    std::ostringstream ss;
    ss << "ReorgMonitor Statistics:\n";
    ss << "  Chain ID: " << chainId_ << "\n";
    ss << "  Finality Depth: " << finalityDepth_ << "\n";
    ss << "  Current L1 Tip: " << currentL1Tip_.blockNumber << "\n";
    ss << "  L1 Blocks Tracked: " << l1BlockHistory_.size() << "\n";
    ss << "  Anchor Points: " << anchorPoints_.size() << "\n";
    ss << "  Transaction Logs: " << transactionLogs_.size() << "\n";
    ss << "  Notification Callbacks: " << notificationCallbacks_.size() << "\n";
    
    // Count finalized anchors
    size_t finalized = 0;
    for (const auto& [_, anchor] : anchorPoints_) {
        if (anchor.isFinalized) ++finalized;
    }
    ss << "  Finalized Anchors: " << finalized << "\n";

    return ss.str();
}

bool ReorgMonitor::IsHealthy() const
{
    LOCK(cs_reorg_);
    
    // Check if we have recent L1 blocks
    if (l1BlockHistory_.empty()) {
        return true; // Just started, no data yet
    }

    // Check if we have at least one anchor point
    // (after initial setup period)
    if (currentL1Tip_.blockNumber > MIN_ANCHOR_INTERVAL && anchorPoints_.empty()) {
        return false;
    }

    return true;
}

void ReorgMonitor::Clear()
{
    LOCK(cs_reorg_);
    
    l1BlockHistory_.clear();
    currentL1Tip_ = L1BlockInfo();
    anchorPoints_.clear();
    transactionLogs_.clear();
    txLogsByBlock_.clear();
}

// =============================================================================
// Private Methods
// =============================================================================

uint64_t ReorgMonitor::FindForkPoint(const L1BlockInfo& oldTip, 
                                      const L1BlockInfo& newTip) const
{
    // Simple approach: walk back from both tips to find common ancestor
    // In practice, this would query the L1 chain for block info
    
    uint64_t oldHeight = oldTip.blockNumber;
    uint64_t newHeight = newTip.blockNumber;
    
    // Start from the lower height
    uint64_t checkHeight = std::min(oldHeight, newHeight);
    
    // Walk back through our history to find matching block
    while (checkHeight > 0) {
        auto it = l1BlockHistory_.find(checkHeight);
        if (it != l1BlockHistory_.end()) {
            // In a real implementation, we'd verify this block is on the new chain
            // For now, assume blocks in history before the reorg are valid
            if (checkHeight < oldHeight && checkHeight < newHeight) {
                return checkHeight;
            }
        }
        --checkHeight;
    }

    // If we have any history, return the oldest block we know
    if (!l1BlockHistory_.empty()) {
        return l1BlockHistory_.begin()->first;
    }

    return 0;
}

void ReorgMonitor::NotifyCallbacks(const ReorgDetectionResult& detection,
                                   const ReorgRecoveryResult& recovery)
{
    // Note: cs_reorg_ should already be held by caller
    for (const auto& callback : notificationCallbacks_) {
        try {
            callback(detection, recovery);
        } catch (const std::exception& e) {
            LogPrintf("ReorgMonitor: Notification callback threw exception: %s\n", e.what());
        }
    }
}

void ReorgMonitor::PruneL1History(size_t keepBlocks)
{
    // Note: cs_reorg_ should already be held by caller
    while (l1BlockHistory_.size() > keepBlocks) {
        l1BlockHistory_.erase(l1BlockHistory_.begin());
    }
}

void ReorgMonitor::PruneAnchorPoints(size_t keepAnchors)
{
    // Note: cs_reorg_ should already be held by caller
    while (anchorPoints_.size() > keepAnchors) {
        // Only prune finalized anchors
        auto it = anchorPoints_.begin();
        if (it->second.isFinalized) {
            anchorPoints_.erase(it);
        } else {
            break; // Don't prune non-finalized anchors
        }
    }
}

} // namespace l2
