// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l1_chain_monitor.cpp
 * @brief Implementation of L1 Chain Monitor for detecting burn transactions
 * 
 * Requirements: 2.1, 2.2
 */

#include <l2/l1_chain_monitor.h>
#include <util.h>

#include <chrono>
#include <memory>

namespace l2 {

// ============================================================================
// Global Instance Management
// ============================================================================

static std::unique_ptr<L1ChainMonitor> g_l1ChainMonitor;
static CCriticalSection cs_l1ChainMonitorInit;

L1ChainMonitor& GetL1ChainMonitor()
{
    LOCK(cs_l1ChainMonitorInit);
    if (!g_l1ChainMonitor) {
        throw std::runtime_error("L1ChainMonitor not initialized");
    }
    return *g_l1ChainMonitor;
}

void InitL1ChainMonitor(
    uint32_t chainId,
    BurnValidator& validator,
    MintConsensusManager& consensusManager)
{
    LOCK(cs_l1ChainMonitorInit);
    g_l1ChainMonitor = std::make_unique<L1ChainMonitor>(chainId, validator, consensusManager);
    LogPrintf("L1ChainMonitor initialized for chain ID %u\n", chainId);
}

bool IsL1ChainMonitorInitialized()
{
    LOCK(cs_l1ChainMonitorInit);
    return g_l1ChainMonitor != nullptr;
}

// ============================================================================
// L1ChainMonitor Implementation
// ============================================================================

L1ChainMonitor::L1ChainMonitor(
    uint32_t chainId,
    BurnValidator& validator,
    MintConsensusManager& consensusManager)
    : chainId_(chainId)
    , validator_(validator)
    , consensusManager_(consensusManager)
    , lastProcessedHeight_(0)
    , isRunning_(false)
{
}

L1ChainMonitor::~L1ChainMonitor()
{
    Stop();
}

bool L1ChainMonitor::Start()
{
    if (isRunning_.load()) {
        return true;  // Already running
    }
    
    isRunning_.store(true);
    LogPrintf("L1ChainMonitor: Started monitoring for chain ID %u\n", chainId_);
    return true;
}

void L1ChainMonitor::Stop()
{
    if (!isRunning_.load()) {
        return;  // Already stopped
    }
    
    isRunning_.store(false);
    LogPrintf("L1ChainMonitor: Stopped monitoring\n");
}

void L1ChainMonitor::ProcessNewBlock(const CBlock& block, uint64_t blockHeight, const uint256& blockHash)
{
    if (!isRunning_.load()) {
        return;
    }
    
    LOCK(cs_monitor_);
    
    LogPrint(BCLog::L2, "L1ChainMonitor: Processing block %u (%s)\n",
        blockHeight, blockHash.ToString().substr(0, 16));
    
    // Scan all transactions in the block for burn outputs
    for (const auto& tx : block.vtx) {
        ProcessTransaction(*tx, blockHeight, blockHash);
    }
    
    // Update last processed height
    lastProcessedHeight_ = blockHeight;
    
    // Check pending burns for sufficient confirmations
    CheckPendingBurns();
    
    // Prune old burns if needed
    if (detectedBurns_.size() > MAX_TRACKED_BURNS) {
        PruneOldBurns();
    }
}

bool L1ChainMonitor::ProcessTransaction(const CTransaction& tx, uint64_t blockHeight, const uint256& blockHash)
{
    // Scan for burn output
    auto burnDataOpt = ScanForBurn(tx);
    if (!burnDataOpt) {
        return false;  // Not a burn transaction
    }
    
    const BurnData& burnData = *burnDataOpt;
    
    // Check if this burn is for our chain
    if (burnData.chainId != chainId_) {
        LogPrint(BCLog::L2, "L1ChainMonitor: Ignoring burn for chain %u (we are %u)\n",
            burnData.chainId, chainId_);
        return false;
    }
    
    uint256 txHash = tx.GetHash();
    
    // Check if already detected
    if (detectedBurns_.find(txHash) != detectedBurns_.end()) {
        return false;  // Already tracked
    }
    
    // Create detected burn record
    DetectedBurn burn;
    burn.l1TxHash = txHash;
    burn.l1BlockNumber = blockHeight;
    burn.l1BlockHash = blockHash;
    burn.burnData = burnData;
    burn.confirmations = 1;  // Just included in block
    burn.detectedTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    burn.validationTriggered = false;
    
    // Store the detected burn
    detectedBurns_[txHash] = burn;
    pendingBurns_.insert(txHash);
    
    LogPrintf("L1ChainMonitor: Detected burn TX %s - amount: %lld, recipient: %s\n",
        txHash.ToString().substr(0, 16),
        burnData.amount,
        burnData.GetRecipientAddress().ToString().substr(0, 16));
    
    // Notify callbacks
    NotifyBurnDetected(burn);
    
    return true;
}

void L1ChainMonitor::CheckPendingBurns()
{
    // Note: Caller must hold cs_monitor_
    
    std::vector<uint256> toValidate;
    
    for (const uint256& txHash : pendingBurns_) {
        auto it = detectedBurns_.find(txHash);
        if (it == detectedBurns_.end()) {
            continue;
        }
        
        DetectedBurn& burn = it->second;
        
        // Skip if already validated
        if (burn.validationTriggered) {
            continue;
        }
        
        // Calculate current confirmations
        if (lastProcessedHeight_ >= burn.l1BlockNumber) {
            burn.confirmations = static_cast<int>(lastProcessedHeight_ - burn.l1BlockNumber + 1);
        }
        
        // Check if we have enough confirmations
        if (burn.confirmations >= REQUIRED_CONFIRMATIONS) {
            toValidate.push_back(txHash);
        }
    }
    
    // Trigger validation for burns with sufficient confirmations
    for (const uint256& txHash : toValidate) {
        TriggerValidation(txHash);
    }
}

void L1ChainMonitor::HandleReorg(uint64_t reorgFromHeight)
{
    LOCK(cs_monitor_);
    
    LogPrintf("L1ChainMonitor: Handling reorg from height %u\n", reorgFromHeight);
    
    // Find burns in reverted blocks
    std::vector<uint256> toRemove;
    
    for (const auto& pair : detectedBurns_) {
        if (pair.second.l1BlockNumber >= reorgFromHeight) {
            toRemove.push_back(pair.first);
        }
    }
    
    // Remove reverted burns
    for (const uint256& txHash : toRemove) {
        detectedBurns_.erase(txHash);
        pendingBurns_.erase(txHash);
        
        LogPrint(BCLog::L2, "L1ChainMonitor: Removed burn %s due to reorg\n",
            txHash.ToString().substr(0, 16));
    }
    
    // Update last processed height
    if (reorgFromHeight > 0) {
        lastProcessedHeight_ = reorgFromHeight - 1;
    } else {
        lastProcessedHeight_ = 0;
    }
}

std::vector<DetectedBurn> L1ChainMonitor::GetDetectedBurns() const
{
    LOCK(cs_monitor_);
    
    std::vector<DetectedBurn> result;
    result.reserve(detectedBurns_.size());
    
    for (const auto& pair : detectedBurns_) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<DetectedBurn> L1ChainMonitor::GetPendingBurns() const
{
    LOCK(cs_monitor_);
    
    std::vector<DetectedBurn> result;
    
    for (const uint256& txHash : pendingBurns_) {
        auto it = detectedBurns_.find(txHash);
        if (it != detectedBurns_.end() && !it->second.validationTriggered) {
            result.push_back(it->second);
        }
    }
    
    return result;
}

size_t L1ChainMonitor::GetDetectedCount() const
{
    LOCK(cs_monitor_);
    return detectedBurns_.size();
}

void L1ChainMonitor::RegisterBurnDetectedCallback(BurnDetectedCallback callback)
{
    LOCK(cs_monitor_);
    burnDetectedCallbacks_.push_back(std::move(callback));
}

void L1ChainMonitor::RegisterValidationTriggeredCallback(ValidationTriggeredCallback callback)
{
    LOCK(cs_monitor_);
    validationTriggeredCallbacks_.push_back(std::move(callback));
}

uint64_t L1ChainMonitor::GetLastProcessedHeight() const
{
    LOCK(cs_monitor_);
    return lastProcessedHeight_;
}

void L1ChainMonitor::SetLastProcessedHeight(uint64_t height)
{
    LOCK(cs_monitor_);
    lastProcessedHeight_ = height;
}

void L1ChainMonitor::Clear()
{
    LOCK(cs_monitor_);
    
    detectedBurns_.clear();
    pendingBurns_.clear();
    lastProcessedHeight_ = 0;
}

// ============================================================================
// Private Methods
// ============================================================================

std::optional<BurnData> L1ChainMonitor::ScanForBurn(const CTransaction& tx)
{
    return BurnTransactionParser::ParseBurnTransaction(tx);
}

bool L1ChainMonitor::TriggerValidation(const uint256& l1TxHash)
{
    // Note: Caller must hold cs_monitor_
    
    auto it = detectedBurns_.find(l1TxHash);
    if (it == detectedBurns_.end()) {
        return false;
    }
    
    DetectedBurn& burn = it->second;
    
    // Skip if already validated
    if (burn.validationTriggered) {
        return false;
    }
    
    LogPrint(BCLog::L2, "L1ChainMonitor: Triggering validation for %s (%d confirmations)\n",
        l1TxHash.ToString().substr(0, 16), burn.confirmations);
    
    // Validate the burn
    BurnValidationResult result = validator_.ValidateBurn(l1TxHash);
    
    // Mark as validated
    burn.validationTriggered = true;
    
    // Remove from pending
    pendingBurns_.erase(l1TxHash);
    
    // Notify callbacks
    NotifyValidationTriggered(l1TxHash, result);
    
    if (result.isValid) {
        // Submit confirmation to consensus manager
        return SubmitConfirmation(l1TxHash, result);
    } else {
        LogPrintf("L1ChainMonitor: Validation failed for %s: %s\n",
            l1TxHash.ToString().substr(0, 16), result.errorMessage);
        return false;
    }
}

bool L1ChainMonitor::SubmitConfirmation(const uint256& l1TxHash, const BurnValidationResult& result)
{
    // Create mint confirmation
    MintConfirmation confirmation;
    confirmation.l1TxHash = l1TxHash;
    confirmation.l2Recipient = result.burnData.GetRecipientAddress();
    confirmation.amount = result.burnData.amount;
    // Note: sequencerAddress and signature would be set by the local sequencer
    confirmation.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Submit to consensus manager
    bool submitted = consensusManager_.SubmitConfirmation(confirmation);
    
    if (submitted) {
        LogPrint(BCLog::L2, "L1ChainMonitor: Submitted confirmation for %s\n",
            l1TxHash.ToString().substr(0, 16));
    }
    
    return submitted;
}

void L1ChainMonitor::NotifyBurnDetected(const DetectedBurn& burn)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<BurnDetectedCallback> callbacks;
    {
        // Note: Caller already holds cs_monitor_
        callbacks = burnDetectedCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(burn);
        } catch (const std::exception& e) {
            LogPrintf("L1ChainMonitor: Exception in burn detected callback: %s\n", e.what());
        }
    }
}

void L1ChainMonitor::NotifyValidationTriggered(const uint256& l1TxHash, const BurnValidationResult& result)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<ValidationTriggeredCallback> callbacks;
    {
        // Note: Caller already holds cs_monitor_
        callbacks = validationTriggeredCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(l1TxHash, result);
        } catch (const std::exception& e) {
            LogPrintf("L1ChainMonitor: Exception in validation triggered callback: %s\n", e.what());
        }
    }
}

void L1ChainMonitor::PruneOldBurns()
{
    // Note: Caller must hold cs_monitor_
    
    // Remove validated burns older than 1 hour
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::vector<uint256> toRemove;
    
    for (const auto& pair : detectedBurns_) {
        const DetectedBurn& burn = pair.second;
        
        // Only prune validated burns
        if (burn.validationTriggered && now - burn.detectedTime > 3600) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (const uint256& txHash : toRemove) {
        detectedBurns_.erase(txHash);
    }
    
    if (!toRemove.empty()) {
        LogPrint(BCLog::L2, "L1ChainMonitor: Pruned %zu old burns\n", toRemove.size());
    }
}

} // namespace l2
