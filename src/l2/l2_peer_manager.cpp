// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <l2/l2_peer_manager.h>
#include <net.h>
#include <netmessagemaker.h>
#include <util.h>

#include <algorithm>
#include <chrono>

namespace l2 {

// Global L2 peer manager instance
static std::unique_ptr<L2PeerManager> g_l2PeerManager;

L2PeerManager::L2PeerManager(uint64_t chainId)
    : chainId_(chainId)
    , currentSyncPeer_(-1)
{
}

void L2PeerManager::RegisterL2Peer(int64_t nodeId, const L2PeerCapabilities& capabilities) {
    LOCK(cs_peers_);
    
    if (peers_.size() >= MAX_L2_PEERS) {
        // Prune oldest inactive peer
        PruneInactivePeers(1800);  // 30 minutes
        if (peers_.size() >= MAX_L2_PEERS) {
            LogPrint(BCLog::NET, "L2 Peer Manager: Cannot register peer %lld, max peers reached\n", nodeId);
            return;
        }
    }
    
    L2PeerInfo info(nodeId);
    info.capabilities = capabilities;
    info.firstSeen = GetCurrentTime();
    info.lastActive = info.firstSeen;
    
    // Set initial score bonuses
    if (capabilities.isSequencer) {
        info.score.sequencerBonus = SEQUENCER_BONUS;
    }
    
    peers_[nodeId] = info;
    
    LogPrint(BCLog::NET, "L2 Peer Manager: Registered L2 peer %lld (sequencer=%d, chains=%zu)\n",
             nodeId, capabilities.isSequencer, capabilities.supportedChainIds.size());
}

void L2PeerManager::UnregisterPeer(int64_t nodeId) {
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        // If this was our sync peer, stop syncing
        if (currentSyncPeer_ == nodeId) {
            currentSyncPeer_ = -1;
        }
        
        peers_.erase(it);
        LogPrint(BCLog::NET, "L2 Peer Manager: Unregistered peer %lld\n", nodeId);
    }
}

void L2PeerManager::UpdatePeerCapabilities(int64_t nodeId, const L2PeerCapabilities& capabilities) {
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        it->second.capabilities = capabilities;
        it->second.lastActive = GetCurrentTime();
        
        // Update sequencer bonus
        if (capabilities.isSequencer) {
            it->second.score.sequencerBonus = SEQUENCER_BONUS;
        } else {
            it->second.score.sequencerBonus = 0;
        }
        
        UpdatePeerScore(nodeId);
    }
}

bool L2PeerManager::IsL2Peer(int64_t nodeId) const {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    return it != peers_.end() && it->second.capabilities.supportsL2;
}

bool L2PeerManager::IsSequencerPeer(int64_t nodeId) const {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    return it != peers_.end() && it->second.capabilities.isSequencer;
}

std::optional<L2PeerInfo> L2PeerManager::GetPeerInfo(int64_t nodeId) const {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<L2PeerInfo> L2PeerManager::GetAllL2Peers() const {
    LOCK(cs_peers_);
    std::vector<L2PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto& pair : peers_) {
        if (pair.second.capabilities.supportsL2) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<L2PeerInfo> L2PeerManager::GetSequencerPeers() const {
    LOCK(cs_peers_);
    std::vector<L2PeerInfo> result;
    for (const auto& pair : peers_) {
        if (pair.second.capabilities.isSequencer) {
            result.push_back(pair.second);
        }
    }
    return result;
}

size_t L2PeerManager::GetL2PeerCount() const {
    LOCK(cs_peers_);
    size_t count = 0;
    for (const auto& pair : peers_) {
        if (pair.second.capabilities.supportsL2) {
            count++;
        }
    }
    return count;
}

size_t L2PeerManager::GetSequencerPeerCount() const {
    LOCK(cs_peers_);
    size_t count = 0;
    for (const auto& pair : peers_) {
        if (pair.second.capabilities.isSequencer) {
            count++;
        }
    }
    return count;
}

// =========================================================================
// Peer Scoring
// =========================================================================

void L2PeerManager::RecordValidBlock(int64_t nodeId) {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        it->second.score.validBlocksReceived++;
        it->second.lastActive = GetCurrentTime();
        UpdatePeerScore(nodeId);
    }
}

void L2PeerManager::RecordInvalidBlock(int64_t nodeId) {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        it->second.score.invalidBlocksReceived++;
        it->second.score.invalidDataPenalty += INVALID_BLOCK_PENALTY;
        it->second.lastActive = GetCurrentTime();
        UpdatePeerScore(nodeId);
        
        LogPrint(BCLog::NET, "L2 Peer Manager: Recorded invalid block from peer %lld, new score=%d\n",
                 nodeId, it->second.score.GetTotalScore());
    }
}

void L2PeerManager::RecordResponseTime(int64_t nodeId, uint64_t responseTimeMs) {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        // Exponential moving average
        if (it->second.score.avgResponseTimeMs == 0) {
            it->second.score.avgResponseTimeMs = responseTimeMs;
        } else {
            it->second.score.avgResponseTimeMs = 
                (it->second.score.avgResponseTimeMs * 7 + responseTimeMs) / 8;
        }
        
        // Apply latency penalty for slow responses (>5 seconds)
        if (it->second.score.avgResponseTimeMs > 5000) {
            it->second.score.latencyPenalty = 
                std::min(30, static_cast<int32_t>((it->second.score.avgResponseTimeMs - 5000) / 1000));
        } else {
            it->second.score.latencyPenalty = 0;
        }
        
        it->second.lastActive = GetCurrentTime();
        UpdatePeerScore(nodeId);
    }
}

int32_t L2PeerManager::GetPeerScore(int64_t nodeId) const {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        return it->second.score.GetTotalScore();
    }
    return -1;
}

std::vector<L2PeerInfo> L2PeerManager::GetBestPeersForSync(size_t count) const {
    LOCK(cs_peers_);
    
    std::vector<L2PeerInfo> result;
    for (const auto& pair : peers_) {
        if (pair.second.capabilities.supportsL2) {
            result.push_back(pair.second);
        }
    }
    
    // Sort by score (highest first)
    std::sort(result.begin(), result.end(), [](const L2PeerInfo& a, const L2PeerInfo& b) {
        return a.score.GetTotalScore() > b.score.GetTotalScore();
    });
    
    // Limit to requested count
    if (result.size() > count) {
        result.resize(count);
    }
    
    return result;
}

void L2PeerManager::UpdatePeerScore(int64_t nodeId) {
    // Called with lock held
    auto it = peers_.find(nodeId);
    if (it == peers_.end()) return;
    
    L2PeerInfo& info = it->second;
    
    // Update data freshness bonus
    uint64_t networkHeight = GetNetworkHeight();
    if (networkHeight > 0 && info.capabilities.highestL2Block > 0) {
        if (networkHeight - info.capabilities.highestL2Block <= 10) {
            info.score.dataFreshnessBonus = FRESH_DATA_BONUS;
        } else {
            info.score.dataFreshnessBonus = 0;
        }
    }
    
    info.score.lastUpdate = GetCurrentTime();
}

// =========================================================================
// L2 Sync Logic
// =========================================================================

bool L2PeerManager::StartSync(int64_t nodeId, uint64_t fromBlock, uint64_t toBlock) {
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it == peers_.end() || !it->second.capabilities.supportsL2) {
        return false;
    }
    
    // Stop any existing sync
    if (currentSyncPeer_ >= 0 && currentSyncPeer_ != nodeId) {
        auto syncIt = peers_.find(currentSyncPeer_);
        if (syncIt != peers_.end()) {
            syncIt->second.syncState.isSyncing = false;
        }
    }
    
    L2SyncState& state = it->second.syncState;
    state.isSyncing = true;
    state.syncFromBlock = fromBlock;
    state.syncToBlock = toBlock;
    state.blocksDownloaded = 0;
    state.syncStartTime = GetCurrentTime();
    state.lastBlockRequest = 0;
    state.pendingRequests = 0;
    state.pendingHeaders.clear();
    
    currentSyncPeer_ = nodeId;
    
    LogPrint(BCLog::NET, "L2 Peer Manager: Started sync from peer %lld, blocks %llu-%llu\n",
             nodeId, fromBlock, toBlock);
    
    return true;
}

void L2PeerManager::StopSync(int64_t nodeId) {
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        it->second.syncState.isSyncing = false;
    }
    
    if (currentSyncPeer_ == nodeId) {
        currentSyncPeer_ = -1;
    }
    
    LogPrint(BCLog::NET, "L2 Peer Manager: Stopped sync from peer %lld\n", nodeId);
}

void L2PeerManager::UpdateSyncProgress(int64_t nodeId, uint64_t blocksDownloaded) {
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it != peers_.end() && it->second.syncState.isSyncing) {
        it->second.syncState.blocksDownloaded = blocksDownloaded;
        it->second.lastActive = GetCurrentTime();
        
        // Check if sync is complete
        if (blocksDownloaded >= (it->second.syncState.syncToBlock - it->second.syncState.syncFromBlock)) {
            it->second.syncState.isSyncing = false;
            if (currentSyncPeer_ == nodeId) {
                currentSyncPeer_ = -1;
            }
            LogPrint(BCLog::NET, "L2 Peer Manager: Sync complete from peer %lld\n", nodeId);
        }
    }
}

std::optional<L2SyncState> L2PeerManager::GetSyncState(int64_t nodeId) const {
    LOCK(cs_peers_);
    auto it = peers_.find(nodeId);
    if (it != peers_.end() && it->second.syncState.isSyncing) {
        return it->second.syncState;
    }
    return std::nullopt;
}

bool L2PeerManager::IsSyncing() const {
    LOCK(cs_peers_);
    return currentSyncPeer_ >= 0;
}

int64_t L2PeerManager::GetSyncPeer() const {
    LOCK(cs_peers_);
    return currentSyncPeer_;
}

int64_t L2PeerManager::SelectSyncPeer() const {
    LOCK(cs_peers_);
    
    int64_t bestPeer = -1;
    int32_t bestScore = -1;
    uint64_t highestBlock = 0;
    
    for (const auto& pair : peers_) {
        if (!pair.second.capabilities.supportsL2) continue;
        if (pair.second.syncState.isSyncing) continue;  // Already syncing
        
        int32_t score = pair.second.score.GetTotalScore();
        uint64_t height = pair.second.capabilities.highestL2Block;
        
        // Prefer peers with higher blocks and better scores
        if (height > highestBlock || (height == highestBlock && score > bestScore)) {
            bestPeer = pair.first;
            bestScore = score;
            highestBlock = height;
        }
    }
    
    return bestPeer;
}

bool L2PeerManager::RequestBlocks(int64_t nodeId, uint64_t fromBlock, uint32_t count, CConnman* connman) {
    if (!connman) return false;
    
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it == peers_.end() || !it->second.capabilities.supportsL2) {
        return false;
    }
    
    if (it->second.syncState.pendingRequests >= MAX_PENDING_REQUESTS) {
        return false;
    }
    
    // Send L2GETBLOCKS message
    bool sent = false;
    connman->ForNode(nodeId, [&](CNode* pnode) {
        if (pnode && pnode->fSuccessfullyConnected) {
            uint64_t endBlock = fromBlock + count - 1;
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(
                    NetMsgType::L2GETBLOCKS, fromBlock, endBlock, chainId_));
            sent = true;
        }
        return true;
    });
    
    if (sent) {
        it->second.syncState.lastBlockRequest = GetCurrentTime();
        it->second.syncState.pendingRequests++;
    }
    
    return sent;
}

bool L2PeerManager::RequestHeaders(int64_t nodeId, uint64_t fromBlock, uint32_t count, CConnman* connman) {
    if (!connman) return false;
    
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it == peers_.end() || !it->second.capabilities.supportsL2) {
        return false;
    }
    
    // Send L2GETHEADERS message
    bool sent = false;
    connman->ForNode(nodeId, [&](CNode* pnode) {
        if (pnode && pnode->fSuccessfullyConnected) {
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(
                    NetMsgType::L2GETHEADERS, fromBlock, static_cast<uint64_t>(count), chainId_));
            sent = true;
        }
        return true;
    });
    
    return sent;
}

// =========================================================================
// Utility
// =========================================================================

void L2PeerManager::Clear() {
    LOCK(cs_peers_);
    peers_.clear();
    currentSyncPeer_ = -1;
}

size_t L2PeerManager::PruneInactivePeers(uint64_t maxInactiveSeconds) {
    LOCK(cs_peers_);
    
    uint64_t now = GetCurrentTime();
    size_t pruned = 0;
    
    for (auto it = peers_.begin(); it != peers_.end(); ) {
        if (now - it->second.lastActive > maxInactiveSeconds) {
            // Don't prune if we're syncing from this peer
            if (currentSyncPeer_ == it->first) {
                ++it;
                continue;
            }
            
            LogPrint(BCLog::NET, "L2 Peer Manager: Pruning inactive peer %lld\n", it->first);
            it = peers_.erase(it);
            pruned++;
        } else {
            ++it;
        }
    }
    
    return pruned;
}

void L2PeerManager::UpdatePeerHeight(int64_t nodeId, uint64_t blockNumber, const uint256& stateRoot) {
    LOCK(cs_peers_);
    
    auto it = peers_.find(nodeId);
    if (it != peers_.end()) {
        if (blockNumber > it->second.capabilities.highestL2Block) {
            it->second.capabilities.highestL2Block = blockNumber;
            it->second.capabilities.latestStateRoot = stateRoot;
            it->second.capabilities.lastL2Activity = GetCurrentTime();
            it->second.lastActive = GetCurrentTime();
            
            UpdatePeerScore(nodeId);
        }
    }
}

uint64_t L2PeerManager::GetNetworkHeight() const {
    LOCK(cs_peers_);
    
    uint64_t maxHeight = 0;
    for (const auto& pair : peers_) {
        if (pair.second.capabilities.highestL2Block > maxHeight) {
            maxHeight = pair.second.capabilities.highestL2Block;
        }
    }
    
    return maxHeight;
}

uint64_t L2PeerManager::GetCurrentTime() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// =========================================================================
// Global Instance Management
// =========================================================================

L2PeerManager& GetL2PeerManager() {
    assert(g_l2PeerManager);
    return *g_l2PeerManager;
}

void InitL2PeerManager(uint64_t chainId) {
    g_l2PeerManager = std::make_unique<L2PeerManager>(chainId);
    LogPrint(BCLog::NET, "L2 Peer Manager: Initialized for chain %llu\n", chainId);
}

bool IsL2PeerManagerInitialized() {
    return g_l2PeerManager != nullptr;
}

} // namespace l2
