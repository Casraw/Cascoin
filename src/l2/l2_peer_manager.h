// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_PEER_MANAGER_H
#define CASCOIN_L2_PEER_MANAGER_H

/**
 * @file l2_peer_manager.h
 * @brief L2 Peer Management for Cascoin Layer 2
 * 
 * This file implements L2-specific peer management including:
 * - Tracking L2 capabilities of peers
 * - L2-specific peer scoring
 * - L2 sync logic
 * 
 * Requirements: 11.5, 11.6
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <sync.h>
#include <protocol.h>

#include <cstdint>
#include <map>
#include <vector>
#include <set>
#include <chrono>
#include <optional>

// Forward declarations
class CNode;
class CConnman;

namespace l2 {

/**
 * @brief L2 capabilities of a peer
 */
struct L2PeerCapabilities {
    /** Whether peer supports L2 */
    bool supportsL2 = false;
    
    /** Whether peer is an L2 sequencer */
    bool isSequencer = false;
    
    /** L2 chain IDs this peer supports */
    std::set<uint64_t> supportedChainIds;
    
    /** Highest L2 block number known by this peer */
    uint64_t highestL2Block = 0;
    
    /** L2 state root hash at highest block */
    uint256 latestStateRoot;
    
    /** Protocol version for L2 */
    uint32_t l2ProtocolVersion = 0;
    
    /** Timestamp of last L2 activity */
    uint64_t lastL2Activity = 0;

    L2PeerCapabilities() = default;
};

/**
 * @brief L2 peer scoring information
 */
struct L2PeerScore {
    /** Base score (0-100) */
    int32_t baseScore = 50;
    
    /** Bonus for being a sequencer */
    int32_t sequencerBonus = 0;
    
    /** Bonus for having recent L2 data */
    int32_t dataFreshnessBonus = 0;
    
    /** Penalty for slow responses */
    int32_t latencyPenalty = 0;
    
    /** Penalty for invalid data */
    int32_t invalidDataPenalty = 0;
    
    /** Number of valid L2 blocks received */
    uint64_t validBlocksReceived = 0;
    
    /** Number of invalid L2 blocks received */
    uint64_t invalidBlocksReceived = 0;
    
    /** Average response time in milliseconds */
    uint64_t avgResponseTimeMs = 0;
    
    /** Last score update timestamp */
    uint64_t lastUpdate = 0;

    /**
     * @brief Calculate total score
     * @return Total score (clamped to 0-100)
     */
    int32_t GetTotalScore() const {
        int32_t total = baseScore + sequencerBonus + dataFreshnessBonus - latencyPenalty - invalidDataPenalty;
        if (total < 0) return 0;
        if (total > 100) return 100;
        return total;
    }
};

/**
 * @brief L2 sync state for a peer
 */
struct L2SyncState {
    /** Whether we're syncing L2 from this peer */
    bool isSyncing = false;
    
    /** L2 block we're syncing from */
    uint64_t syncFromBlock = 0;
    
    /** L2 block we're syncing to */
    uint64_t syncToBlock = 0;
    
    /** Current sync progress (blocks downloaded) */
    uint64_t blocksDownloaded = 0;
    
    /** Timestamp when sync started */
    uint64_t syncStartTime = 0;
    
    /** Last block request timestamp */
    uint64_t lastBlockRequest = 0;
    
    /** Number of pending block requests */
    uint32_t pendingRequests = 0;
    
    /** Headers we've received but not yet validated */
    std::vector<uint256> pendingHeaders;

    /**
     * @brief Get sync progress as percentage
     * @return Progress 0.0 - 100.0
     */
    double GetProgress() const {
        if (syncToBlock <= syncFromBlock) return 100.0;
        uint64_t total = syncToBlock - syncFromBlock;
        return (static_cast<double>(blocksDownloaded) / total) * 100.0;
    }
};

/**
 * @brief Complete L2 peer information
 */
struct L2PeerInfo {
    /** Node ID */
    int64_t nodeId = -1;
    
    /** L2 capabilities */
    L2PeerCapabilities capabilities;
    
    /** L2 peer score */
    L2PeerScore score;
    
    /** L2 sync state */
    L2SyncState syncState;
    
    /** Sequencer address (if peer is sequencer) */
    uint160 sequencerAddress;
    
    /** When peer was first seen as L2-capable */
    uint64_t firstSeen = 0;
    
    /** When peer was last active for L2 */
    uint64_t lastActive = 0;

    L2PeerInfo() = default;
    explicit L2PeerInfo(int64_t id) : nodeId(id) {}
};


/**
 * @brief L2 Peer Manager
 * 
 * Manages L2-specific peer information, scoring, and synchronization.
 * Tracks which peers support L2, their capabilities, and coordinates
 * L2 block synchronization.
 * 
 * Requirements: 11.5, 11.6
 */
class L2PeerManager {
public:
    /**
     * @brief Construct a new L2 Peer Manager
     * @param chainId The L2 chain ID
     */
    explicit L2PeerManager(uint64_t chainId);

    /**
     * @brief Register a peer as L2-capable
     * @param nodeId The peer's node ID
     * @param capabilities The peer's L2 capabilities
     */
    void RegisterL2Peer(int64_t nodeId, const L2PeerCapabilities& capabilities);

    /**
     * @brief Unregister a peer (on disconnect)
     * @param nodeId The peer's node ID
     */
    void UnregisterPeer(int64_t nodeId);

    /**
     * @brief Update peer capabilities
     * @param nodeId The peer's node ID
     * @param capabilities Updated capabilities
     */
    void UpdatePeerCapabilities(int64_t nodeId, const L2PeerCapabilities& capabilities);

    /**
     * @brief Check if a peer supports L2
     * @param nodeId The peer's node ID
     * @return true if peer supports L2
     */
    bool IsL2Peer(int64_t nodeId) const;

    /**
     * @brief Check if a peer is an L2 sequencer
     * @param nodeId The peer's node ID
     * @return true if peer is a sequencer
     */
    bool IsSequencerPeer(int64_t nodeId) const;

    /**
     * @brief Get L2 peer info
     * @param nodeId The peer's node ID
     * @return Peer info or nullopt if not found
     */
    std::optional<L2PeerInfo> GetPeerInfo(int64_t nodeId) const;

    /**
     * @brief Get all L2-capable peers
     * @return Vector of L2 peer info
     */
    std::vector<L2PeerInfo> GetAllL2Peers() const;

    /**
     * @brief Get all sequencer peers
     * @return Vector of sequencer peer info
     */
    std::vector<L2PeerInfo> GetSequencerPeers() const;

    /**
     * @brief Get the number of L2-capable peers
     * @return Count of L2 peers
     */
    size_t GetL2PeerCount() const;

    /**
     * @brief Get the number of sequencer peers
     * @return Count of sequencer peers
     */
    size_t GetSequencerPeerCount() const;

    // =========================================================================
    // Peer Scoring
    // =========================================================================

    /**
     * @brief Update peer score based on valid block received
     * @param nodeId The peer's node ID
     */
    void RecordValidBlock(int64_t nodeId);

    /**
     * @brief Update peer score based on invalid block received
     * @param nodeId The peer's node ID
     */
    void RecordInvalidBlock(int64_t nodeId);

    /**
     * @brief Update peer response time
     * @param nodeId The peer's node ID
     * @param responseTimeMs Response time in milliseconds
     */
    void RecordResponseTime(int64_t nodeId, uint64_t responseTimeMs);

    /**
     * @brief Get peer score
     * @param nodeId The peer's node ID
     * @return Peer score (0-100) or -1 if not found
     */
    int32_t GetPeerScore(int64_t nodeId) const;

    /**
     * @brief Get best peers for L2 sync (sorted by score)
     * @param count Maximum number of peers to return
     * @return Vector of peer info sorted by score (highest first)
     */
    std::vector<L2PeerInfo> GetBestPeersForSync(size_t count) const;

    // =========================================================================
    // L2 Sync Logic
    // =========================================================================

    /**
     * @brief Start L2 sync from a peer
     * @param nodeId The peer's node ID
     * @param fromBlock Starting block number
     * @param toBlock Ending block number
     * @return true if sync started
     */
    bool StartSync(int64_t nodeId, uint64_t fromBlock, uint64_t toBlock);

    /**
     * @brief Stop L2 sync from a peer
     * @param nodeId The peer's node ID
     */
    void StopSync(int64_t nodeId);

    /**
     * @brief Update sync progress
     * @param nodeId The peer's node ID
     * @param blocksDownloaded Number of blocks downloaded
     */
    void UpdateSyncProgress(int64_t nodeId, uint64_t blocksDownloaded);

    /**
     * @brief Get sync state for a peer
     * @param nodeId The peer's node ID
     * @return Sync state or nullopt if not syncing
     */
    std::optional<L2SyncState> GetSyncState(int64_t nodeId) const;

    /**
     * @brief Check if we're syncing from any peer
     * @return true if syncing
     */
    bool IsSyncing() const;

    /**
     * @brief Get the peer we're syncing from
     * @return Node ID of sync peer or -1 if not syncing
     */
    int64_t GetSyncPeer() const;

    /**
     * @brief Select best peer for L2 sync
     * @return Node ID of best peer or -1 if none available
     */
    int64_t SelectSyncPeer() const;

    /**
     * @brief Request L2 blocks from a peer
     * @param nodeId The peer's node ID
     * @param fromBlock Starting block number
     * @param count Number of blocks to request
     * @param connman Connection manager for sending messages
     * @return true if request was sent
     */
    bool RequestBlocks(int64_t nodeId, uint64_t fromBlock, uint32_t count, CConnman* connman);

    /**
     * @brief Request L2 headers from a peer
     * @param nodeId The peer's node ID
     * @param fromBlock Starting block number
     * @param count Number of headers to request
     * @param connman Connection manager for sending messages
     * @return true if request was sent
     */
    bool RequestHeaders(int64_t nodeId, uint64_t fromBlock, uint32_t count, CConnman* connman);

    // =========================================================================
    // Utility
    // =========================================================================

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Clear all peer data (for testing)
     */
    void Clear();

    /**
     * @brief Prune inactive peers
     * @param maxInactiveSeconds Maximum inactivity time before pruning
     * @return Number of peers pruned
     */
    size_t PruneInactivePeers(uint64_t maxInactiveSeconds = 3600);

    /**
     * @brief Update peer's highest known L2 block
     * @param nodeId The peer's node ID
     * @param blockNumber The block number
     * @param stateRoot The state root at that block
     */
    void UpdatePeerHeight(int64_t nodeId, uint64_t blockNumber, const uint256& stateRoot);

    /**
     * @brief Get the highest L2 block known by any peer
     * @return Highest block number
     */
    uint64_t GetNetworkHeight() const;

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Map of node ID to L2 peer info */
    std::map<int64_t, L2PeerInfo> peers_;

    /** Current sync peer (-1 if not syncing) */
    int64_t currentSyncPeer_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_peers_;

    /** Maximum peers to track */
    static constexpr size_t MAX_L2_PEERS = 1000;

    /** Score bonus for sequencers */
    static constexpr int32_t SEQUENCER_BONUS = 20;

    /** Score penalty per invalid block */
    static constexpr int32_t INVALID_BLOCK_PENALTY = 10;

    /** Score bonus for fresh data (within 10 blocks of tip) */
    static constexpr int32_t FRESH_DATA_BONUS = 10;

    /** Maximum pending block requests per peer */
    static constexpr uint32_t MAX_PENDING_REQUESTS = 16;

    /**
     * @brief Update peer score based on current state
     * @param nodeId The peer's node ID
     */
    void UpdatePeerScore(int64_t nodeId);

    /**
     * @brief Get current timestamp
     * @return Unix timestamp in seconds
     */
    uint64_t GetCurrentTime() const;
};

/**
 * @brief Global L2 peer manager instance
 */
L2PeerManager& GetL2PeerManager();

/**
 * @brief Initialize the global L2 peer manager
 * @param chainId The L2 chain ID
 */
void InitL2PeerManager(uint64_t chainId);

/**
 * @brief Check if L2 peer manager is initialized
 * @return true if initialized
 */
bool IsL2PeerManagerInitialized();

} // namespace l2

#endif // CASCOIN_L2_PEER_MANAGER_H
