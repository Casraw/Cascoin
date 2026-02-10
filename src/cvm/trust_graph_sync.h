// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUST_GRAPH_SYNC_H
#define CASCOIN_CVM_TRUST_GRAPH_SYNC_H

#include <uint256.h>
#include <serialize.h>
#include <net.h>
#include <cvm/trustgraph.h>
#include <cvm/consensus_safety.h>
#include <vector>
#include <map>
#include <mutex>
#include <memory>

class CNode;
class CConnman;

namespace CVM {

// Forward declarations
class CVMDatabase;
class TrustGraph;
class ConsensusSafetyValidator;

/**
 * Trust Graph State Request
 * 
 * Request for trust graph state hash from a peer.
 */
struct TrustGraphStateRequest {
    uint64_t requestId;                 // Unique request ID
    int64_t timestamp;                  // Request timestamp
    
    TrustGraphStateRequest() : requestId(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(requestId);
        READWRITE(timestamp);
    }
};

/**
 * Trust Graph State Response
 * 
 * Response containing trust graph state information.
 */
struct TrustGraphStateResponse {
    uint64_t requestId;                 // Request ID being responded to
    TrustGraphSyncState state;          // Trust graph state
    int64_t timestamp;                  // Response timestamp
    
    TrustGraphStateResponse() : requestId(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(requestId);
        READWRITE(state);
        READWRITE(timestamp);
    }
};

/**
 * Trust Graph Delta Request
 * 
 * Request for trust graph changes since a specific block.
 */
struct TrustGraphDeltaRequest {
    uint64_t requestId;                 // Unique request ID
    int sinceBlock;                     // Block height to get delta from
    uint256 sinceStateHash;             // State hash at sinceBlock
    int64_t timestamp;                  // Request timestamp
    
    TrustGraphDeltaRequest() : requestId(0), sinceBlock(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(requestId);
        READWRITE(sinceBlock);
        READWRITE(sinceStateHash);
        READWRITE(timestamp);
    }
};

/**
 * Trust Graph Delta Response
 * 
 * Response containing trust graph changes.
 */
struct TrustGraphDeltaResponse {
    uint64_t requestId;                 // Request ID being responded to
    int fromBlock;                      // Starting block height
    int toBlock;                        // Ending block height
    std::vector<TrustEdge> edges;       // Trust edge changes
    uint256 newStateHash;               // State hash after applying delta
    int64_t timestamp;                  // Response timestamp
    
    TrustGraphDeltaResponse() : requestId(0), fromBlock(0), toBlock(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(requestId);
        READWRITE(fromBlock);
        READWRITE(toBlock);
        READWRITE(edges);
        READWRITE(newStateHash);
        READWRITE(timestamp);
    }
};

/**
 * Peer Sync State
 * 
 * Tracks synchronization state with a specific peer.
 */
struct PeerSyncState {
    NodeId nodeId;                      // Peer node ID
    uint256 lastKnownStateHash;         // Last known state hash from peer
    int lastKnownBlock;                 // Last known block height from peer
    int64_t lastSyncTime;               // Last synchronization time
    bool isSyncing;                     // Currently syncing with this peer
    int pendingRequests;                // Number of pending requests
    
    PeerSyncState() : nodeId(-1), lastKnownBlock(0), lastSyncTime(0), 
                     isSyncing(false), pendingRequests(0) {}
};

/**
 * Trust Graph Synchronization Manager
 * 
 * Manages trust graph state synchronization across the P2P network.
 * Ensures all nodes have consistent trust graph state for consensus.
 * 
 * Requirements: 10.1, 10.2
 */
class TrustGraphSyncManager {
public:
    TrustGraphSyncManager();
    explicit TrustGraphSyncManager(CVMDatabase* db, TrustGraph* graph, 
                                   ConsensusSafetyValidator* validator);
    ~TrustGraphSyncManager();
    
    // ========== P2P Message Handlers ==========
    
    /**
     * Process trust graph state request from peer
     * 
     * @param request State request
     * @param pfrom Peer that sent the request
     * @param connman Connection manager
     */
    void ProcessStateRequest(
        const TrustGraphStateRequest& request,
        CNode* pfrom,
        CConnman* connman
    );
    
    /**
     * Process trust graph state response from peer
     * 
     * @param response State response
     * @param pfrom Peer that sent the response
     */
    void ProcessStateResponse(
        const TrustGraphStateResponse& response,
        CNode* pfrom
    );
    
    /**
     * Process trust graph delta request from peer
     * 
     * @param request Delta request
     * @param pfrom Peer that sent the request
     * @param connman Connection manager
     */
    void ProcessDeltaRequest(
        const TrustGraphDeltaRequest& request,
        CNode* pfrom,
        CConnman* connman
    );
    
    /**
     * Process trust graph delta response from peer
     * 
     * @param response Delta response
     * @param pfrom Peer that sent the response
     */
    void ProcessDeltaResponse(
        const TrustGraphDeltaResponse& response,
        CNode* pfrom
    );
    
    // ========== Synchronization Methods ==========
    
    /**
     * Request trust graph state from peer
     * 
     * @param pfrom Peer to request from
     * @param connman Connection manager
     * @return Request ID
     */
    uint64_t RequestStateFromPeer(CNode* pfrom, CConnman* connman);
    
    /**
     * Request trust graph delta from peer
     * 
     * @param pfrom Peer to request from
     * @param sinceBlock Block height to get delta from
     * @param connman Connection manager
     * @return Request ID
     */
    uint64_t RequestDeltaFromPeer(
        CNode* pfrom,
        int sinceBlock,
        CConnman* connman
    );
    
    /**
     * Start synchronization with all connected peers
     * 
     * @param connman Connection manager
     */
    void StartSync(CConnman* connman);
    
    /**
     * Check if synchronization is needed
     * 
     * @return true if sync is needed
     */
    bool NeedsSync() const;
    
    /**
     * Get current sync progress
     * 
     * @return Sync progress (0.0-1.0)
     */
    double GetSyncProgress() const;
    
    // ========== State Management ==========
    
    /**
     * Get current trust graph state
     * 
     * @return Trust graph sync state
     */
    TrustGraphSyncState GetCurrentState() const;
    
    /**
     * Verify state matches expected hash
     * 
     * @param expectedHash Expected state hash
     * @return true if state matches
     */
    bool VerifyState(const uint256& expectedHash) const;
    
    /**
     * Apply delta to local trust graph
     * 
     * @param delta Trust edge changes
     * @return true if delta applied successfully
     */
    bool ApplyDelta(const std::vector<TrustEdge>& delta);
    
    /**
     * Get delta since block
     * 
     * @param sinceBlock Block height to get delta from
     * @return Vector of trust edge changes
     */
    std::vector<TrustEdge> GetDeltaSinceBlock(int sinceBlock) const;
    
    // ========== Peer Management ==========
    
    /**
     * Register peer for sync
     * 
     * @param nodeId Peer node ID
     */
    void RegisterPeer(NodeId nodeId);
    
    /**
     * Unregister peer
     * 
     * @param nodeId Peer node ID
     */
    void UnregisterPeer(NodeId nodeId);
    
    /**
     * Get peer sync state
     * 
     * @param nodeId Peer node ID
     * @return Peer sync state (nullptr if not found)
     */
    const PeerSyncState* GetPeerState(NodeId nodeId) const;
    
    /**
     * Get number of synced peers
     * 
     * @return Number of peers with matching state
     */
    int GetSyncedPeerCount() const;
    
private:
    CVMDatabase* database;
    TrustGraph* trustGraph;
    ConsensusSafetyValidator* consensusValidator;
    
    // Peer tracking
    std::map<NodeId, PeerSyncState> peerStates;
    mutable std::mutex peerMutex;
    
    // Request tracking
    std::map<uint64_t, NodeId> pendingRequests;  // requestId -> nodeId
    uint64_t nextRequestId;
    mutable std::mutex requestMutex;
    
    // Sync state
    bool isSyncing;
    int64_t lastSyncTime;
    
    // Constants
    static constexpr int SYNC_INTERVAL = 60;        // Sync every 60 seconds
    static constexpr int MAX_DELTA_BLOCKS = 1000;   // Max blocks in delta request
    static constexpr int REQUEST_TIMEOUT = 30;      // Request timeout in seconds
    
    /**
     * Generate unique request ID
     */
    uint64_t GenerateRequestId();
    
    /**
     * Send state response to peer
     */
    void SendStateResponse(
        const TrustGraphStateRequest& request,
        CNode* pfrom,
        CConnman* connman
    );
    
    /**
     * Send delta response to peer
     */
    void SendDeltaResponse(
        const TrustGraphDeltaRequest& request,
        CNode* pfrom,
        CConnman* connman
    );
    
    /**
     * Update peer state after response
     */
    void UpdatePeerState(NodeId nodeId, const TrustGraphSyncState& state);
    
    /**
     * Check if peer state is stale
     */
    bool IsPeerStateStale(NodeId nodeId) const;
};

// Global trust graph sync manager instance
extern std::unique_ptr<TrustGraphSyncManager> g_trustGraphSyncManager;

/**
 * Initialize the global trust graph sync manager
 */
void InitializeTrustGraphSyncManager(CVMDatabase* db, TrustGraph* graph, 
                                     ConsensusSafetyValidator* validator);

/**
 * Shutdown the global trust graph sync manager
 */
void ShutdownTrustGraphSyncManager();

} // namespace CVM

#endif // CASCOIN_CVM_TRUST_GRAPH_SYNC_H
