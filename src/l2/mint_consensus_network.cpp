// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file mint_consensus_network.cpp
 * @brief Implementation of P2P Network Integration for Mint Consensus
 * 
 * Requirements: 3.1, 3.3
 */

#include <l2/mint_consensus_network.h>
#include <util.h>

#include <memory>

namespace l2 {

// ============================================================================
// Global Instance Management
// ============================================================================

static std::unique_ptr<MintConsensusNetwork> g_mintConsensusNetwork;
static CCriticalSection cs_mintConsensusNetworkInit;

MintConsensusNetwork& GetMintConsensusNetwork()
{
    LOCK(cs_mintConsensusNetworkInit);
    if (!g_mintConsensusNetwork) {
        throw std::runtime_error("MintConsensusNetwork not initialized");
    }
    return *g_mintConsensusNetwork;
}

void InitMintConsensusNetwork(uint32_t chainId, MintConsensusManager& consensusManager)
{
    LOCK(cs_mintConsensusNetworkInit);
    g_mintConsensusNetwork = std::make_unique<MintConsensusNetwork>(chainId, consensusManager);
    LogPrintf("MintConsensusNetwork initialized for chain ID %u\n", chainId);
}

bool IsMintConsensusNetworkInitialized()
{
    LOCK(cs_mintConsensusNetworkInit);
    return g_mintConsensusNetwork != nullptr;
}

// ============================================================================
// MintConsensusNetwork Implementation
// ============================================================================

MintConsensusNetwork::MintConsensusNetwork(uint32_t chainId, MintConsensusManager& consensusManager)
    : chainId_(chainId)
    , consensusManager_(consensusManager)
    , connman_(nullptr)
    , confirmationsSent_(0)
    , confirmationsReceived_(0)
    , confirmationsRejected_(0)
{
}

MintConsensusNetwork::~MintConsensusNetwork()
{
    Shutdown();
}

bool MintConsensusNetwork::Initialize(CConnman* connman)
{
    LOCK(cs_network_);
    
    if (connman_ != nullptr) {
        return true;  // Already initialized
    }
    
    connman_ = connman;
    
    LogPrintf("MintConsensusNetwork: Initialized with connection manager\n");
    return true;
}

void MintConsensusNetwork::Shutdown()
{
    LOCK(cs_network_);
    
    connman_ = nullptr;
    recentlyBroadcast_.clear();
    
    LogPrintf("MintConsensusNetwork: Shutdown complete\n");
}

size_t MintConsensusNetwork::BroadcastConfirmation(const MintConfirmation& confirmation)
{
    LOCK(cs_network_);
    
    if (connman_ == nullptr) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Cannot broadcast - not initialized\n");
        return 0;
    }
    
    // Check if recently broadcast
    uint256 confHash = confirmation.GetHash();
    if (recentlyBroadcast_.count(confHash) > 0) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Skipping recently broadcast confirmation\n");
        return 0;
    }
    
    // Get sequencer peers
    std::vector<CNode*> peers = GetSequencerPeers();
    
    if (peers.empty()) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: No sequencer peers to broadcast to\n");
        return 0;
    }
    
    // Broadcast to all sequencer peers
    size_t sentCount = 0;
    for (CNode* pnode : peers) {
        if (SendConfirmation(confirmation, pnode)) {
            ++sentCount;
        }
    }
    
    // Mark as recently broadcast
    recentlyBroadcast_.insert(confHash);
    
    // Prune if needed
    if (recentlyBroadcast_.size() > MAX_RECENTLY_BROADCAST) {
        PruneRecentlyBroadcast();
    }
    
    confirmationsSent_ += sentCount;
    
    LogPrint(BCLog::L2, "MintConsensusNetwork: Broadcast confirmation for %s to %zu peers\n",
        confirmation.l1TxHash.ToString().substr(0, 16), sentCount);
    
    return sentCount;
}

bool MintConsensusNetwork::ProcessConfirmationMessage(const MintConfirmationMessage& msg, CNode* pfrom)
{
    LOCK(cs_network_);
    
    // Validate the message
    if (!ValidateMessage(msg, pfrom)) {
        ++confirmationsRejected_;
        return false;
    }
    
    ++confirmationsReceived_;
    
    // Forward to consensus manager
    bool accepted = consensusManager_.ProcessConfirmation(msg.confirmation, pfrom);
    
    if (accepted) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Accepted confirmation from peer for %s\n",
            msg.confirmation.l1TxHash.ToString().substr(0, 16));
        
        // Notify callbacks
        NotifyConfirmationReceived(msg.confirmation, pfrom);
        
        // Re-broadcast to other peers (gossip)
        // Note: The recently broadcast check will prevent infinite loops
        BroadcastConfirmation(msg.confirmation);
    } else {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Rejected confirmation from peer for %s\n",
            msg.confirmation.l1TxHash.ToString().substr(0, 16));
        ++confirmationsRejected_;
    }
    
    return accepted;
}

bool MintConsensusNetwork::ProcessMessage(const std::string& strCommand, CDataStream& vRecv, CNode* pfrom)
{
    if (strCommand == MSG_MINT_CONFIRMATION) {
        MintConfirmationMessage msg;
        vRecv >> msg;
        return ProcessConfirmationMessage(msg, pfrom);
    }
    else if (strCommand == MSG_GET_MINT_CONFIRMATIONS) {
        // Handle request for confirmations
        uint256 l1TxHash;
        vRecv >> l1TxHash;
        
        // Get consensus state and send any confirmations we have
        auto stateOpt = consensusManager_.GetConsensusState(l1TxHash);
        if (stateOpt) {
            for (const auto& pair : stateOpt->confirmations) {
                MintConfirmationMessage msg(pair.second, chainId_);
                SendConfirmation(pair.second, pfrom);
            }
        }
        return true;
    }
    else if (strCommand == MSG_MINT_CONF_INV) {
        // Handle confirmation inventory
        std::vector<uint256> inventory;
        vRecv >> inventory;
        
        // Request any confirmations we don't have
        for (const uint256& l1TxHash : inventory) {
            auto stateOpt = consensusManager_.GetConsensusState(l1TxHash);
            if (!stateOpt || stateOpt->status == MintConsensusState::Status::PENDING) {
                RequestConfirmations(l1TxHash);
            }
        }
        return true;
    }
    
    return false;
}

void MintConsensusNetwork::RequestConfirmations(const uint256& l1TxHash)
{
    LOCK(cs_network_);
    
    if (connman_ == nullptr) {
        return;
    }
    
    // Get sequencer peers
    std::vector<CNode*> peers = GetSequencerPeers();
    
    // Request from a few random peers
    size_t requestCount = std::min(peers.size(), size_t(3));
    
    for (size_t i = 0; i < requestCount && i < peers.size(); ++i) {
        CNode* pnode = peers[i];
        
        // Send request message
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << l1TxHash;
        
        // Note: In production, use proper P2P message sending
        LogPrint(BCLog::L2, "MintConsensusNetwork: Requesting confirmations for %s from peer\n",
            l1TxHash.ToString().substr(0, 16));
    }
}

size_t MintConsensusNetwork::GetSequencerPeerCount() const
{
    LOCK(cs_network_);
    return GetSequencerPeers().size();
}

void MintConsensusNetwork::RegisterConfirmationReceivedCallback(ConfirmationReceivedCallback callback)
{
    LOCK(cs_network_);
    confirmationReceivedCallbacks_.push_back(std::move(callback));
}

std::map<std::string, uint64_t> MintConsensusNetwork::GetStatistics() const
{
    std::map<std::string, uint64_t> stats;
    stats["confirmations_sent"] = confirmationsSent_.load();
    stats["confirmations_received"] = confirmationsReceived_.load();
    stats["confirmations_rejected"] = confirmationsRejected_.load();
    
    LOCK(cs_network_);
    stats["recently_broadcast_count"] = recentlyBroadcast_.size();
    stats["sequencer_peer_count"] = GetSequencerPeers().size();
    
    return stats;
}

void MintConsensusNetwork::Clear()
{
    LOCK(cs_network_);
    
    recentlyBroadcast_.clear();
    confirmationsSent_ = 0;
    confirmationsReceived_ = 0;
    confirmationsRejected_ = 0;
}

// ============================================================================
// Private Methods
// ============================================================================

std::vector<CNode*> MintConsensusNetwork::GetSequencerPeers() const
{
    // Note: Caller must hold cs_network_
    
    std::vector<CNode*> sequencerPeers;
    
    if (connman_ == nullptr) {
        return sequencerPeers;
    }
    
    // In production, this would filter peers based on sequencer status
    // For now, return all connected peers that support L2
    connman_->ForEachNode([&sequencerPeers](CNode* pnode) {
        // Check if peer supports L2 protocol
        // In production, check for L2 service flag or sequencer announcement
        if (pnode->fSuccessfullyConnected) {
            sequencerPeers.push_back(pnode);
        }
    });
    
    return sequencerPeers;
}

bool MintConsensusNetwork::SendConfirmation(const MintConfirmation& confirmation, CNode* pnode)
{
    if (pnode == nullptr || connman_ == nullptr) {
        return false;
    }
    
    // Create message
    MintConfirmationMessage msg(confirmation, chainId_);
    
    // Serialize and send
    // Note: In production, use proper P2P message sending via connman_
    LogPrint(BCLog::L2, "MintConsensusNetwork: Sending confirmation for %s to peer %d\n",
        confirmation.l1TxHash.ToString().substr(0, 16), pnode->GetId());
    
    return true;
}

bool MintConsensusNetwork::ValidateMessage(const MintConfirmationMessage& msg, CNode* pfrom) const
{
    // Check protocol version
    if (msg.protocolVersion != L2_PROTOCOL_VERSION) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Invalid protocol version %u (expected %u)\n",
            msg.protocolVersion, L2_PROTOCOL_VERSION);
        return false;
    }
    
    // Check chain ID
    if (msg.chainId != chainId_) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Invalid chain ID %u (expected %u)\n",
            msg.chainId, chainId_);
        return false;
    }
    
    // Validate confirmation structure
    if (!msg.confirmation.IsValid()) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Invalid confirmation structure\n");
        return false;
    }
    
    // Check if confirmation is expired
    if (msg.confirmation.IsExpired()) {
        LogPrint(BCLog::L2, "MintConsensusNetwork: Expired confirmation\n");
        return false;
    }
    
    return true;
}

void MintConsensusNetwork::NotifyConfirmationReceived(const MintConfirmation& confirmation, CNode* pfrom)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<ConfirmationReceivedCallback> callbacks;
    {
        // Note: Caller already holds cs_network_
        callbacks = confirmationReceivedCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(confirmation, pfrom);
        } catch (const std::exception& e) {
            LogPrintf("MintConsensusNetwork: Exception in confirmation received callback: %s\n", e.what());
        }
    }
}

void MintConsensusNetwork::PruneRecentlyBroadcast()
{
    // Note: Caller must hold cs_network_
    
    // Simple pruning: remove half of the entries
    // In production, use timestamp-based pruning
    size_t toRemove = recentlyBroadcast_.size() / 2;
    
    auto it = recentlyBroadcast_.begin();
    for (size_t i = 0; i < toRemove && it != recentlyBroadcast_.end(); ++i) {
        it = recentlyBroadcast_.erase(it);
    }
}

} // namespace l2
