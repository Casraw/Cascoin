// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_graph_sync.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/consensus_safety.h>
#include <net.h>
#include <netmessagemaker.h>
#include <protocol.h>
#include <util.h>
#include <validation.h>
#include <chain.h>

namespace CVM {

// Global instance
std::unique_ptr<TrustGraphSyncManager> g_trustGraphSyncManager;

void InitializeTrustGraphSyncManager(CVMDatabase* db, TrustGraph* graph, 
                                     ConsensusSafetyValidator* validator) {
    g_trustGraphSyncManager = std::make_unique<TrustGraphSyncManager>(db, graph, validator);
    LogPrintf("TrustGraphSyncManager: Initialized\n");
}

void ShutdownTrustGraphSyncManager() {
    g_trustGraphSyncManager.reset();
    LogPrintf("TrustGraphSyncManager: Shutdown\n");
}

// TrustGraphSyncManager implementation

TrustGraphSyncManager::TrustGraphSyncManager()
    : database(nullptr), trustGraph(nullptr), consensusValidator(nullptr),
      nextRequestId(1), isSyncing(false), lastSyncTime(0) {
}

TrustGraphSyncManager::TrustGraphSyncManager(CVMDatabase* db, TrustGraph* graph, 
                                             ConsensusSafetyValidator* validator)
    : database(db), trustGraph(graph), consensusValidator(validator),
      nextRequestId(1), isSyncing(false), lastSyncTime(0) {
}

TrustGraphSyncManager::~TrustGraphSyncManager() {
}

// ========== P2P Message Handlers ==========

void TrustGraphSyncManager::ProcessStateRequest(
    const TrustGraphStateRequest& request,
    CNode* pfrom,
    CConnman* connman
) {
    if (!pfrom || !connman) {
        return;
    }
    
    LogPrintf("TrustGraphSyncManager: Received state request %lu from peer %d\n",
              request.requestId, pfrom->GetId());
    
    SendStateResponse(request, pfrom, connman);
}

void TrustGraphSyncManager::ProcessStateResponse(
    const TrustGraphStateResponse& response,
    CNode* pfrom
) {
    if (!pfrom) {
        return;
    }
    
    NodeId nodeId = pfrom->GetId();
    
    LogPrintf("TrustGraphSyncManager: Received state response %lu from peer %d\n",
              response.requestId, nodeId);
    
    // Verify this is a response to our request
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        auto it = pendingRequests.find(response.requestId);
        if (it == pendingRequests.end() || it->second != nodeId) {
            LogPrintf("TrustGraphSyncManager: Unexpected state response %lu from peer %d\n",
                      response.requestId, nodeId);
            return;
        }
        pendingRequests.erase(it);
    }
    
    // Update peer state
    UpdatePeerState(nodeId, response.state);
    
    // Check if we need to sync
    TrustGraphSyncState localState = GetCurrentState();
    
    if (localState.stateHash != response.state.stateHash) {
        LogPrintf("TrustGraphSyncManager: State mismatch with peer %d (local=%s, peer=%s)\n",
                  nodeId, localState.stateHash.ToString(), response.state.stateHash.ToString());
        
        // If peer has newer state, we may need to request delta
        if (response.state.lastSyncBlock > localState.lastSyncBlock) {
            LogPrintf("TrustGraphSyncManager: Peer %d has newer state (peer=%lu, local=%lu)\n",
                      nodeId, response.state.lastSyncBlock, localState.lastSyncBlock);
        }
    } else {
        LogPrintf("TrustGraphSyncManager: State matches with peer %d\n", nodeId);
    }
}

void TrustGraphSyncManager::ProcessDeltaRequest(
    const TrustGraphDeltaRequest& request,
    CNode* pfrom,
    CConnman* connman
) {
    if (!pfrom || !connman) {
        return;
    }
    
    LogPrintf("TrustGraphSyncManager: Received delta request %lu from peer %d (since block %d)\n",
              request.requestId, pfrom->GetId(), request.sinceBlock);
    
    SendDeltaResponse(request, pfrom, connman);
}

void TrustGraphSyncManager::ProcessDeltaResponse(
    const TrustGraphDeltaResponse& response,
    CNode* pfrom
) {
    if (!pfrom) {
        return;
    }
    
    NodeId nodeId = pfrom->GetId();
    
    LogPrintf("TrustGraphSyncManager: Received delta response %lu from peer %d (%zu edges)\n",
              response.requestId, nodeId, response.edges.size());
    
    // Verify this is a response to our request
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        auto it = pendingRequests.find(response.requestId);
        if (it == pendingRequests.end() || it->second != nodeId) {
            LogPrintf("TrustGraphSyncManager: Unexpected delta response %lu from peer %d\n",
                      response.requestId, nodeId);
            return;
        }
        pendingRequests.erase(it);
    }
    
    // Apply delta to local trust graph
    if (!response.edges.empty()) {
        if (ApplyDelta(response.edges)) {
            LogPrintf("TrustGraphSyncManager: Applied %zu trust edge changes from peer %d\n",
                      response.edges.size(), nodeId);
            
            // Verify new state matches expected
            TrustGraphSyncState newState = GetCurrentState();
            if (newState.stateHash != response.newStateHash) {
                LogPrintf("TrustGraphSyncManager: WARNING: State hash mismatch after applying delta\n");
            }
        } else {
            LogPrintf("TrustGraphSyncManager: Failed to apply delta from peer %d\n", nodeId);
        }
    }
}

// ========== Synchronization Methods ==========

uint64_t TrustGraphSyncManager::RequestStateFromPeer(CNode* pfrom, CConnman* connman) {
    if (!pfrom || !connman) {
        return 0;
    }
    
    TrustGraphStateRequest request;
    request.requestId = GenerateRequestId();
    request.timestamp = GetTime();
    
    // Track pending request
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        pendingRequests[request.requestId] = pfrom->GetId();
    }
    
    // Send request
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::TRUSTGRAPHSTATEREQ, request));
    
    LogPrintf("TrustGraphSyncManager: Sent state request %lu to peer %d\n",
              request.requestId, pfrom->GetId());
    
    return request.requestId;
}

uint64_t TrustGraphSyncManager::RequestDeltaFromPeer(
    CNode* pfrom,
    int sinceBlock,
    CConnman* connman
) {
    if (!pfrom || !connman) {
        return 0;
    }
    
    TrustGraphDeltaRequest request;
    request.requestId = GenerateRequestId();
    request.sinceBlock = sinceBlock;
    request.sinceStateHash = consensusValidator ? 
        consensusValidator->CalculateTrustGraphStateHash() : uint256();
    request.timestamp = GetTime();
    
    // Track pending request
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        pendingRequests[request.requestId] = pfrom->GetId();
    }
    
    // Send request
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::TRUSTGRAPHDELTAREQ, request));
    
    LogPrintf("TrustGraphSyncManager: Sent delta request %lu to peer %d (since block %d)\n",
              request.requestId, pfrom->GetId(), sinceBlock);
    
    return request.requestId;
}

void TrustGraphSyncManager::StartSync(CConnman* connman) {
    if (!connman || isSyncing) {
        return;
    }
    
    isSyncing = true;
    lastSyncTime = GetTime();
    
    LogPrintf("TrustGraphSyncManager: Starting trust graph synchronization\n");
    
    // Request state from all connected peers
    connman->ForEachNode([this, connman](CNode* pnode) {
        if (pnode->fSuccessfullyConnected && !pnode->fDisconnect) {
            RequestStateFromPeer(pnode, connman);
        }
    });
}

bool TrustGraphSyncManager::NeedsSync() const {
    int64_t currentTime = GetTime();
    return (currentTime - lastSyncTime) > SYNC_INTERVAL;
}

double TrustGraphSyncManager::GetSyncProgress() const {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    if (peerStates.empty()) {
        return 0.0;
    }
    
    TrustGraphSyncState localState = GetCurrentState();
    int syncedCount = 0;
    
    for (const auto& pair : peerStates) {
        if (pair.second.lastKnownStateHash == localState.stateHash) {
            syncedCount++;
        }
    }
    
    return static_cast<double>(syncedCount) / peerStates.size();
}

// ========== State Management ==========

TrustGraphSyncState TrustGraphSyncManager::GetCurrentState() const {
    if (consensusValidator) {
        return consensusValidator->GetTrustGraphState();
    }
    return TrustGraphSyncState();
}

bool TrustGraphSyncManager::VerifyState(const uint256& expectedHash) const {
    if (consensusValidator) {
        return consensusValidator->VerifyTrustGraphState(expectedHash);
    }
    return false;
}

bool TrustGraphSyncManager::ApplyDelta(const std::vector<TrustEdge>& delta) {
    if (consensusValidator) {
        return consensusValidator->ApplyTrustGraphDelta(delta);
    }
    return false;
}

std::vector<TrustEdge> TrustGraphSyncManager::GetDeltaSinceBlock(int sinceBlock) const {
    if (consensusValidator) {
        return consensusValidator->GetTrustGraphDelta(sinceBlock);
    }
    return std::vector<TrustEdge>();
}

// ========== Peer Management ==========

void TrustGraphSyncManager::RegisterPeer(NodeId nodeId) {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    if (peerStates.find(nodeId) == peerStates.end()) {
        PeerSyncState state;
        state.nodeId = nodeId;
        state.lastSyncTime = GetTime();
        peerStates[nodeId] = state;
        
        LogPrintf("TrustGraphSyncManager: Registered peer %d for sync\n", nodeId);
    }
}

void TrustGraphSyncManager::UnregisterPeer(NodeId nodeId) {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    auto it = peerStates.find(nodeId);
    if (it != peerStates.end()) {
        peerStates.erase(it);
        LogPrintf("TrustGraphSyncManager: Unregistered peer %d\n", nodeId);
    }
}

const PeerSyncState* TrustGraphSyncManager::GetPeerState(NodeId nodeId) const {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    auto it = peerStates.find(nodeId);
    if (it != peerStates.end()) {
        return &it->second;
    }
    return nullptr;
}

int TrustGraphSyncManager::GetSyncedPeerCount() const {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    TrustGraphSyncState localState = GetCurrentState();
    int count = 0;
    
    for (const auto& pair : peerStates) {
        if (pair.second.lastKnownStateHash == localState.stateHash) {
            count++;
        }
    }
    
    return count;
}

// ========== Private Methods ==========

uint64_t TrustGraphSyncManager::GenerateRequestId() {
    std::lock_guard<std::mutex> lock(requestMutex);
    return nextRequestId++;
}

void TrustGraphSyncManager::SendStateResponse(
    const TrustGraphStateRequest& request,
    CNode* pfrom,
    CConnman* connman
) {
    TrustGraphStateResponse response;
    response.requestId = request.requestId;
    response.state = GetCurrentState();
    response.timestamp = GetTime();
    
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::TRUSTGRAPHSTATE, response));
    
    LogPrintf("TrustGraphSyncManager: Sent state response %lu to peer %d\n",
              response.requestId, pfrom->GetId());
}

void TrustGraphSyncManager::SendDeltaResponse(
    const TrustGraphDeltaRequest& request,
    CNode* pfrom,
    CConnman* connman
) {
    TrustGraphDeltaResponse response;
    response.requestId = request.requestId;
    response.fromBlock = request.sinceBlock;
    
    // Get current block height
    {
        LOCK(cs_main);
        response.toBlock = chainActive.Height();
    }
    
    // Limit delta size
    if (response.toBlock - response.fromBlock > MAX_DELTA_BLOCKS) {
        response.toBlock = response.fromBlock + MAX_DELTA_BLOCKS;
    }
    
    // Get delta
    response.edges = GetDeltaSinceBlock(request.sinceBlock);
    response.newStateHash = consensusValidator ? 
        consensusValidator->CalculateTrustGraphStateHash() : uint256();
    response.timestamp = GetTime();
    
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::TRUSTGRAPHDELTA, response));
    
    LogPrintf("TrustGraphSyncManager: Sent delta response %lu to peer %d (%zu edges)\n",
              response.requestId, pfrom->GetId(), response.edges.size());
}

void TrustGraphSyncManager::UpdatePeerState(NodeId nodeId, const TrustGraphSyncState& state) {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    auto it = peerStates.find(nodeId);
    if (it != peerStates.end()) {
        it->second.lastKnownStateHash = state.stateHash;
        it->second.lastKnownBlock = state.lastSyncBlock;
        it->second.lastSyncTime = GetTime();
        it->second.isSyncing = false;
    }
}

bool TrustGraphSyncManager::IsPeerStateStale(NodeId nodeId) const {
    std::lock_guard<std::mutex> lock(peerMutex);
    
    auto it = peerStates.find(nodeId);
    if (it == peerStates.end()) {
        return true;
    }
    
    int64_t currentTime = GetTime();
    return (currentTime - it->second.lastSyncTime) > SYNC_INTERVAL;
}

} // namespace CVM
