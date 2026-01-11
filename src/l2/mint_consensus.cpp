// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file mint_consensus.cpp
 * @brief Implementation of Mint Consensus Manager for L2 Burn-and-Mint Token Model
 * 
 * Requirements: 3.1, 3.3, 3.4, 3.5, 3.6
 */

#include <l2/mint_consensus.h>
#include <util.h>

#include <algorithm>
#include <memory>

namespace l2 {

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<MintConsensusManager> g_mintConsensusManager;
static CCriticalSection cs_mintConsensusInit;

MintConsensusManager& GetMintConsensusManager()
{
    LOCK(cs_mintConsensusInit);
    if (!g_mintConsensusManager) {
        throw std::runtime_error("MintConsensusManager not initialized");
    }
    return *g_mintConsensusManager;
}

void InitMintConsensusManager(uint32_t chainId)
{
    LOCK(cs_mintConsensusInit);
    g_mintConsensusManager = std::make_unique<MintConsensusManager>(chainId);
    LogPrintf("MintConsensusManager initialized for chain ID %u\n", chainId);
}

bool IsMintConsensusManagerInitialized()
{
    LOCK(cs_mintConsensusInit);
    return g_mintConsensusManager != nullptr;
}

// ============================================================================
// MintConsensusManager Implementation
// ============================================================================

MintConsensusManager::MintConsensusManager(uint32_t chainId)
    : chainId_(chainId)
{
}

MintConsensusManager::~MintConsensusManager()
{
}

bool MintConsensusManager::SubmitConfirmation(const MintConfirmation& confirmation)
{
    // Validate confirmation structure
    if (!confirmation.IsValid()) {
        LogPrint(BCLog::L2, "MintConsensusManager: Invalid confirmation structure\n");
        return false;
    }
    
    // Process the confirmation locally
    if (!ProcessConfirmation(confirmation, nullptr)) {
        return false;
    }
    
    // Broadcast to other sequencers
    BroadcastConfirmation(confirmation);
    
    return true;
}

bool MintConsensusManager::ProcessConfirmation(const MintConfirmation& confirmation, CNode* pfrom)
{
    LOCK(cs_consensus_);
    
    // Validate confirmation structure
    if (!confirmation.IsValid()) {
        LogPrint(BCLog::L2, "MintConsensusManager: Invalid confirmation from %s\n",
            confirmation.sequencerAddress.ToString());
        return false;
    }
    
    // Check if confirmation has expired
    if (confirmation.IsExpired()) {
        LogPrint(BCLog::L2, "MintConsensusManager: Expired confirmation for %s\n",
            confirmation.l1TxHash.ToString());
        return false;
    }
    
    // Verify sequencer is eligible
    if (!IsEligibleSequencer(confirmation.sequencerAddress)) {
        LogPrint(BCLog::L2, "MintConsensusManager: Confirmation from non-eligible sequencer %s\n",
            confirmation.sequencerAddress.ToString());
        return false;
    }
    
    // Verify signature if we have the sequencer's public key
    auto pubkey = GetSequencerPubKey(confirmation.sequencerAddress);
    if (pubkey && !confirmation.VerifySignature(*pubkey)) {
        LogPrint(BCLog::L2, "MintConsensusManager: Invalid signature from sequencer %s\n",
            confirmation.sequencerAddress.ToString());
        return false;
    }
    
    // Get or create consensus state
    auto it = consensusStates_.find(confirmation.l1TxHash);
    if (it == consensusStates_.end()) {
        // Create new consensus state
        MintConsensusState state;
        state.l1TxHash = confirmation.l1TxHash;
        state.burnData.chainId = chainId_;
        state.burnData.amount = confirmation.amount;
        // Note: recipientPubKey would need to be set from the actual burn data
        state.firstSeenTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state.status = MintConsensusState::Status::PENDING;
        
        consensusStates_[confirmation.l1TxHash] = state;
        it = consensusStates_.find(confirmation.l1TxHash);
    }
    
    MintConsensusState& state = it->second;
    
    // Check if already processed (minted or failed)
    if (state.status == MintConsensusState::Status::MINTED ||
        state.status == MintConsensusState::Status::FAILED ||
        state.status == MintConsensusState::Status::REJECTED) {
        LogPrint(BCLog::L2, "MintConsensusManager: Burn %s already processed (status: %s)\n",
            confirmation.l1TxHash.ToString(), state.GetStatusString());
        return false;
    }
    
    // Check for duplicate confirmation from same sequencer (Requirements: 3.6)
    if (state.HasConfirmation(confirmation.sequencerAddress)) {
        LogPrint(BCLog::L2, "MintConsensusManager: Duplicate confirmation from sequencer %s for %s\n",
            confirmation.sequencerAddress.ToString(), confirmation.l1TxHash.ToString());
        return false;
    }
    
    // Verify confirmation matches existing state
    if (state.GetConfirmationCount() > 0) {
        // Check that amount matches
        auto firstConf = state.confirmations.begin()->second;
        if (confirmation.amount != firstConf.amount ||
            confirmation.l2Recipient != firstConf.l2Recipient) {
            LogPrint(BCLog::L2, "MintConsensusManager: Confirmation mismatch for %s\n",
                confirmation.l1TxHash.ToString());
            return false;
        }
    }
    
    // Add confirmation
    if (!state.AddConfirmation(confirmation)) {
        return false;
    }
    
    LogPrint(BCLog::L2, "MintConsensusManager: Added confirmation from %s for %s (%zu/%zu)\n",
        confirmation.sequencerAddress.ToString(),
        confirmation.l1TxHash.ToString(),
        state.GetConfirmationCount(),
        GetActiveSequencerCount());
    
    // Check if consensus reached
    CheckConsensusStatus(confirmation.l1TxHash);
    
    // Prune old states if needed
    if (consensusStates_.size() > MAX_CONSENSUS_STATES) {
        PruneOldStates();
    }
    
    return true;
}

bool MintConsensusManager::HasConsensus(const uint256& l1TxHash) const
{
    LOCK(cs_consensus_);
    
    auto it = consensusStates_.find(l1TxHash);
    if (it == consensusStates_.end()) {
        return false;
    }
    
    const MintConsensusState& state = it->second;
    
    // Check if already marked as reached or minted
    if (state.status == MintConsensusState::Status::REACHED ||
        state.status == MintConsensusState::Status::MINTED) {
        return true;
    }
    
    // Check current confirmation ratio
    size_t totalSequencers = GetActiveSequencerCount();
    return state.HasReachedConsensus(totalSequencers);
}

std::optional<MintConsensusState> MintConsensusManager::GetConsensusState(const uint256& l1TxHash) const
{
    LOCK(cs_consensus_);
    
    auto it = consensusStates_.find(l1TxHash);
    if (it == consensusStates_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

std::vector<MintConsensusState> MintConsensusManager::GetPendingBurns() const
{
    LOCK(cs_consensus_);
    
    std::vector<MintConsensusState> pending;
    for (const auto& pair : consensusStates_) {
        if (pair.second.status == MintConsensusState::Status::PENDING) {
            pending.push_back(pair.second);
        }
    }
    
    return pending;
}

void MintConsensusManager::ProcessTimeouts()
{
    LOCK(cs_consensus_);
    
    std::vector<uint256> timedOut;
    
    for (auto& pair : consensusStates_) {
        MintConsensusState& state = pair.second;
        
        if (state.status == MintConsensusState::Status::PENDING && state.HasTimedOut()) {
            state.status = MintConsensusState::Status::FAILED;
            timedOut.push_back(pair.first);
            
            LogPrint(BCLog::L2, "MintConsensusManager: Consensus timeout for %s (%zu confirmations)\n",
                pair.first.ToString(), state.GetConfirmationCount());
        }
    }
    
    // Notify callbacks for timed out burns
    for (const uint256& txHash : timedOut) {
        NotifyConsensusFailed(txHash, "Consensus timeout");
    }
}

void MintConsensusManager::RegisterConsensusReachedCallback(ConsensusReachedCallback callback)
{
    LOCK(cs_consensus_);
    consensusReachedCallbacks_.push_back(std::move(callback));
}

void MintConsensusManager::RegisterConsensusFailedCallback(ConsensusFailedCallback callback)
{
    LOCK(cs_consensus_);
    consensusFailedCallbacks_.push_back(std::move(callback));
}

bool MintConsensusManager::MarkAsMinted(const uint256& l1TxHash)
{
    LOCK(cs_consensus_);
    
    auto it = consensusStates_.find(l1TxHash);
    if (it == consensusStates_.end()) {
        return false;
    }
    
    it->second.status = MintConsensusState::Status::MINTED;
    LogPrint(BCLog::L2, "MintConsensusManager: Marked %s as minted\n", l1TxHash.ToString());
    
    return true;
}

size_t MintConsensusManager::GetPendingCount() const
{
    LOCK(cs_consensus_);
    
    size_t count = 0;
    for (const auto& pair : consensusStates_) {
        if (pair.second.status == MintConsensusState::Status::PENDING) {
            ++count;
        }
    }
    return count;
}

size_t MintConsensusManager::GetConsensusReachedCount() const
{
    LOCK(cs_consensus_);
    
    size_t count = 0;
    for (const auto& pair : consensusStates_) {
        if (pair.second.status == MintConsensusState::Status::REACHED ||
            pair.second.status == MintConsensusState::Status::MINTED) {
            ++count;
        }
    }
    return count;
}

void MintConsensusManager::Clear()
{
    LOCK(cs_consensus_);
    consensusStates_.clear();
    testSequencerCount_.reset();
    testSequencers_.clear();
}

void MintConsensusManager::SetTestSequencerCount(size_t count)
{
    LOCK(cs_consensus_);
    testSequencerCount_ = count;
}

void MintConsensusManager::AddTestSequencer(const uint160& address, const CPubKey& pubkey)
{
    LOCK(cs_consensus_);
    testSequencers_[address] = pubkey;
}

void MintConsensusManager::ClearTestSequencers()
{
    LOCK(cs_consensus_);
    testSequencers_.clear();
    testSequencerCount_.reset();
}

// ============================================================================
// Private Methods
// ============================================================================

size_t MintConsensusManager::GetActiveSequencerCount() const
{
    // Use test count if set
    if (testSequencerCount_) {
        return *testSequencerCount_;
    }
    
    // Use callback if set
    if (sequencerCountGetter_) {
        return sequencerCountGetter_();
    }
    
    // Default to test sequencers count
    if (!testSequencers_.empty()) {
        return testSequencers_.size();
    }
    
    // Default minimum
    return MIN_SEQUENCERS_FOR_CONSENSUS;
}

bool MintConsensusManager::IsEligibleSequencer(const uint160& address) const
{
    // Check test sequencers first
    if (!testSequencers_.empty()) {
        return testSequencers_.find(address) != testSequencers_.end();
    }
    
    // Use callback if set
    if (sequencerVerifier_) {
        return sequencerVerifier_(address);
    }
    
    // Default: accept all (for testing)
    return true;
}

std::optional<CPubKey> MintConsensusManager::GetSequencerPubKey(const uint160& address) const
{
    // Check test sequencers first
    auto it = testSequencers_.find(address);
    if (it != testSequencers_.end()) {
        return it->second;
    }
    
    // Use callback if set
    if (sequencerPubKeyGetter_) {
        return sequencerPubKeyGetter_(address);
    }
    
    return std::nullopt;
}

void MintConsensusManager::BroadcastConfirmation(const MintConfirmation& confirmation)
{
    // In production, this would broadcast via P2P network
    // For now, just log
    LogPrint(BCLog::L2, "MintConsensusManager: Broadcasting confirmation for %s\n",
        confirmation.l1TxHash.ToString());
}

void MintConsensusManager::NotifyConsensusReached(const MintConsensusState& state)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<ConsensusReachedCallback> callbacks;
    {
        LOCK(cs_consensus_);
        callbacks = consensusReachedCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(state);
        } catch (const std::exception& e) {
            LogPrintf("MintConsensusManager: Exception in consensus reached callback: %s\n", e.what());
        }
    }
}

void MintConsensusManager::NotifyConsensusFailed(const uint256& l1TxHash, const std::string& reason)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<ConsensusFailedCallback> callbacks;
    {
        LOCK(cs_consensus_);
        callbacks = consensusFailedCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(l1TxHash, reason);
        } catch (const std::exception& e) {
            LogPrintf("MintConsensusManager: Exception in consensus failed callback: %s\n", e.what());
        }
    }
}

void MintConsensusManager::CheckConsensusStatus(const uint256& l1TxHash)
{
    // Note: Caller must hold cs_consensus_
    
    auto it = consensusStates_.find(l1TxHash);
    if (it == consensusStates_.end()) {
        return;
    }
    
    MintConsensusState& state = it->second;
    
    // Skip if not pending
    if (state.status != MintConsensusState::Status::PENDING) {
        return;
    }
    
    size_t totalSequencers = GetActiveSequencerCount();
    
    // Check if consensus reached
    if (state.HasReachedConsensus(totalSequencers)) {
        state.status = MintConsensusState::Status::REACHED;
        
        LogPrint(BCLog::L2, "MintConsensusManager: Consensus reached for %s (%zu/%zu = %.1f%%)\n",
            l1TxHash.ToString(),
            state.GetConfirmationCount(),
            totalSequencers,
            state.GetConfirmationRatio(totalSequencers) * 100.0);
        
        // Notify callbacks (outside lock)
        MintConsensusState stateCopy = state;
        
        // Release lock before calling callbacks
        cs_consensus_.unlock();
        NotifyConsensusReached(stateCopy);
        cs_consensus_.lock();
    }
}

void MintConsensusManager::PruneOldStates()
{
    // Note: Caller must hold cs_consensus_
    
    // Remove old minted/failed states
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::vector<uint256> toRemove;
    
    for (const auto& pair : consensusStates_) {
        const MintConsensusState& state = pair.second;
        
        // Remove states older than 1 hour that are not pending
        if (state.status != MintConsensusState::Status::PENDING &&
            now - state.firstSeenTime > 3600) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (const uint256& txHash : toRemove) {
        consensusStates_.erase(txHash);
    }
    
    if (!toRemove.empty()) {
        LogPrint(BCLog::L2, "MintConsensusManager: Pruned %zu old consensus states\n", toRemove.size());
    }
}

} // namespace l2
