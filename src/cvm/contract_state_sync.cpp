// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/contract_state_sync.h>
#include <hash.h>
#include <util.h>
#include <netmessagemaker.h>
#include <protocol.h>
#include <validation.h>

#include <algorithm>
#include <chrono>

namespace CVM {

// Global instance
std::unique_ptr<ContractStateSyncManager> g_contractStateSyncManager;

// Calculate chunk hash for verification
uint256 ContractStateChunk::CalculateHash() const {
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << contractAddress;
    hasher << chunkIndex;
    hasher << totalChunks;
    for (const auto& entry : storageEntries) {
        hasher << entry.first;
        hasher << entry.second;
    }
    return hasher.GetHash();
}

// Verify storage proof against state root
bool StorageProof::Verify() const {
    if (proof.empty()) {
        return false;
    }
    
    // Calculate leaf hash
    CHashWriter leafHasher(SER_GETHASH, 0);
    leafHasher << key;
    leafHasher << value;
    uint256 currentHash = leafHasher.GetHash();
    
    // Walk up the Merkle tree
    for (const uint256& sibling : proof) {
        CHashWriter nodeHasher(SER_GETHASH, 0);
        if (currentHash < sibling) {
            nodeHasher << currentHash;
            nodeHasher << sibling;
        } else {
            nodeHasher << sibling;
            nodeHasher << currentHash;
        }
        currentHash = nodeHasher.GetHash();
    }
    
    return currentHash == stateRoot;
}

ContractStateSyncManager::ContractStateSyncManager(CVMDatabase* db)
    : database(db), isSyncing(false), isSynced(false) {
}

ContractStateSyncManager::~ContractStateSyncManager() {
    Shutdown();
}

bool ContractStateSyncManager::Initialize() {
    LogPrintf("ContractStateSyncManager: Initializing...\n");
    
    if (!database) {
        LogPrintf("ContractStateSyncManager: Database not available\n");
        return false;
    }
    
    // Check if we already have contract state
    std::vector<uint160> contracts = database->ListContracts();
    if (!contracts.empty()) {
        LogPrintf("ContractStateSyncManager: Found %d existing contracts\n", contracts.size());
        isSynced.store(true);
    }
    
    return true;
}

void ContractStateSyncManager::Shutdown() {
    LogPrintf("ContractStateSyncManager: Shutting down...\n");
    isSyncing.store(false);
    
    // Clear pending requests
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingRequests.clear();
    }
    
    // Clear downloaded chunks
    {
        std::lock_guard<std::mutex> lock(chunksMutex);
        downloadedChunks.clear();
    }
}

bool ContractStateSyncManager::StartFullSync() {
    if (isSyncing.load()) {
        LogPrintf("ContractStateSyncManager: Already syncing\n");
        return false;
    }
    
    LogPrintf("ContractStateSyncManager: Starting full state sync\n");
    
    isSyncing.store(true);
    isSynced.store(false);
    
    // Reset progress
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress = SyncProgress();
        progress.startTime = GetTime();
    }
    
    return true;
}

bool ContractStateSyncManager::StartIncrementalSync(uint64_t fromBlock) {
    if (isSyncing.load()) {
        LogPrintf("ContractStateSyncManager: Already syncing\n");
        return false;
    }
    
    LogPrintf("ContractStateSyncManager: Starting incremental sync from block %lu\n", fromBlock);
    
    isSyncing.store(true);
    
    // Reset progress
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress = SyncProgress();
        progress.startTime = GetTime();
    }
    
    return true;
}

void ContractStateSyncManager::ProcessContractStateRequest(CNode* pfrom, 
    const ContractStateRequest& request, CConnman* connman) {
    
    if (!pfrom || !connman) return;
    
    ContractStateResponse response;
    
    switch (request.type) {
        case ContractStateRequest::RequestType::LIST_CONTRACTS:
            response = HandleListContractsRequest();
            break;
            
        case ContractStateRequest::RequestType::CONTRACT_METADATA:
            response = HandleMetadataRequest(request.contractAddresses);
            break;
            
        case ContractStateRequest::RequestType::CONTRACT_CHUNK:
            response = HandleChunkRequest(request.contractAddresses, request.chunkIndices);
            break;
            
        case ContractStateRequest::RequestType::STATE_PROOF:
            if (!request.contractAddresses.empty()) {
                // Use first contract address and first chunk index as key
                uint256 key;
                if (!request.chunkIndices.empty()) {
                    // Interpret chunk index as part of key (simplified)
                    key = uint256S(std::to_string(request.chunkIndices[0]));
                }
                response = HandleStateProofRequest(request.contractAddresses[0], key);
            } else {
                response.type = ContractStateResponse::ResponseType::ERR_STATE;
                response.errorMessage = "No contract address specified";
            }
            break;
            
        default:
            response.type = ContractStateResponse::ResponseType::ERR_STATE;
            response.errorMessage = "Unknown request type";
            break;
    }
    
    // Send response
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::CONTRACTSTATERESPONSE, response));
}

void ContractStateSyncManager::ProcessContractStateResponse(CNode* pfrom, 
    const ContractStateResponse& response) {
    
    if (!pfrom) return;
    
    switch (response.type) {
        case ContractStateResponse::ResponseType::CONTRACT_LIST:
            LogPrint(BCLog::NET, "ContractStateSync: Received contract list with %d contracts\n",
                     response.contractList.size());
            
            // Update progress
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress.totalContracts = response.contractList.size();
                progress.lastUpdateTime = GetTime();
            }
            break;
            
        case ContractStateResponse::ResponseType::METADATA:
            LogPrint(BCLog::NET, "ContractStateSync: Received metadata for %d contracts\n",
                     response.metadata.size());
            
            // Cache metadata
            {
                std::lock_guard<std::mutex> lock(metadataMutex);
                for (const auto& meta : response.metadata) {
                    contractMetadata[meta.contractAddress] = meta;
                }
            }
            
            // Update total chunks
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                uint64_t totalChunks = 0;
                for (const auto& meta : response.metadata) {
                    totalChunks += meta.chunkCount;
                }
                progress.totalChunks = totalChunks;
                progress.lastUpdateTime = GetTime();
            }
            break;
            
        case ContractStateResponse::ResponseType::CHUNK:
            LogPrint(BCLog::NET, "ContractStateSync: Received %d chunks\n",
                     response.chunks.size());
            
            for (const auto& chunk : response.chunks) {
                // Verify chunk
                if (VerifyChunk(chunk)) {
                    // Apply chunk to database
                    if (ApplyChunk(chunk)) {
                        std::lock_guard<std::mutex> lock(progressMutex);
                        progress.downloadedChunks++;
                        progress.verifiedChunks++;
                        progress.lastUpdateTime = GetTime();
                    } else {
                        std::lock_guard<std::mutex> lock(progressMutex);
                        progress.failedChunks++;
                    }
                } else {
                    LogPrintf("ContractStateSync: Chunk verification failed for contract %s chunk %d\n",
                             chunk.contractAddress.ToString(), chunk.chunkIndex);
                    std::lock_guard<std::mutex> lock(progressMutex);
                    progress.failedChunks++;
                }
            }
            
            // Check if sync is complete
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                if (progress.IsComplete()) {
                    isSyncing.store(false);
                    isSynced.store(true);
                    LogPrintf("ContractStateSync: Synchronization complete!\n");
                }
            }
            break;
            
        case ContractStateResponse::ResponseType::STATE_PROOF:
            LogPrint(BCLog::NET, "ContractStateSync: Received state proof\n");
            // State proofs are typically requested for verification, not sync
            break;
            
        case ContractStateResponse::ResponseType::ERR_STATE:
            LogPrintf("ContractStateSync: Error response: %s\n", response.errorMessage);
            break;
    }
}

bool ContractStateSyncManager::RequestContractList(CNode* peer, CConnman* connman) {
    if (!peer || !connman) return false;
    
    ContractStateRequest request;
    request.type = ContractStateRequest::RequestType::LIST_CONTRACTS;
    
    const CNetMsgMaker msgMaker(peer->GetSendVersion());
    connman->PushMessage(peer, msgMaker.Make(NetMsgType::CONTRACTSTATEREQUEST, request));
    
    LogPrint(BCLog::NET, "ContractStateSync: Requested contract list from peer=%d\n", peer->GetId());
    return true;
}

bool ContractStateSyncManager::RequestContractMetadata(CNode* peer, 
    const std::vector<uint160>& contracts, CConnman* connman) {
    
    if (!peer || !connman || contracts.empty()) return false;
    
    ContractStateRequest request;
    request.type = ContractStateRequest::RequestType::CONTRACT_METADATA;
    request.contractAddresses = contracts;
    
    const CNetMsgMaker msgMaker(peer->GetSendVersion());
    connman->PushMessage(peer, msgMaker.Make(NetMsgType::CONTRACTSTATEREQUEST, request));
    
    LogPrint(BCLog::NET, "ContractStateSync: Requested metadata for %d contracts from peer=%d\n",
             contracts.size(), peer->GetId());
    return true;
}

bool ContractStateSyncManager::RequestContractChunk(CNode* peer, const uint160& contract,
    uint32_t chunkIndex, CConnman* connman) {
    
    if (!peer || !connman) return false;
    
    ContractStateRequest request;
    request.type = ContractStateRequest::RequestType::CONTRACT_CHUNK;
    request.contractAddresses.push_back(contract);
    request.chunkIndices.push_back(chunkIndex);
    
    const CNetMsgMaker msgMaker(peer->GetSendVersion());
    connman->PushMessage(peer, msgMaker.Make(NetMsgType::CONTRACTSTATEREQUEST, request));
    
    // Track pending request
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        uint256 requestId = GenerateRequestId(contract, chunkIndex);
        PendingRequest pending;
        pending.contractAddress = contract;
        pending.chunkIndex = chunkIndex;
        pending.peerId = peer->GetId();
        pending.requestTime = GetTime();
        pendingRequests[requestId] = pending;
    }
    
    LogPrint(BCLog::NET, "ContractStateSync: Requested chunk %d for contract %s from peer=%d\n",
             chunkIndex, contract.ToString(), peer->GetId());
    return true;
}

bool ContractStateSyncManager::RequestStateProof(CNode* peer, const uint160& contract,
    const uint256& key, CConnman* connman) {
    
    if (!peer || !connman) return false;
    
    ContractStateRequest request;
    request.type = ContractStateRequest::RequestType::STATE_PROOF;
    request.contractAddresses.push_back(contract);
    // Encode key in chunk indices (simplified - in production would use proper encoding)
    
    const CNetMsgMaker msgMaker(peer->GetSendVersion());
    connman->PushMessage(peer, msgMaker.Make(NetMsgType::CONTRACTSTATEREQUEST, request));
    
    LogPrint(BCLog::NET, "ContractStateSync: Requested state proof for contract %s from peer=%d\n",
             contract.ToString(), peer->GetId());
    return true;
}


ContractStateResponse ContractStateSyncManager::HandleListContractsRequest() {
    ContractStateResponse response;
    response.type = ContractStateResponse::ResponseType::CONTRACT_LIST;
    
    if (database) {
        response.contractList = database->ListContracts();
    }
    
    LogPrint(BCLog::NET, "ContractStateSync: Responding with %d contracts\n",
             response.contractList.size());
    return response;
}

ContractStateResponse ContractStateSyncManager::HandleMetadataRequest(
    const std::vector<uint160>& contracts) {
    
    ContractStateResponse response;
    response.type = ContractStateResponse::ResponseType::METADATA;
    
    if (!database) {
        response.type = ContractStateResponse::ResponseType::ERR_STATE;
        response.errorMessage = "Database not available";
        return response;
    }
    
    for (const uint160& addr : contracts) {
        if (contracts.size() > MAX_CONTRACTS_PER_REQUEST) break;
        
        Contract contract;
        if (database->ReadContract(addr, contract)) {
            ContractSyncMetadata meta;
            meta.contractAddress = addr;
            meta.codeHash = Hash(contract.code.begin(), contract.code.end());
            meta.storageSize = 0;  // Would need to count storage entries
            meta.chunkCount = 1;   // Simplified - would calculate based on storage size
            meta.stateRoot = CalculateContractStateRoot(addr);
            meta.lastModifiedBlock = contract.deploymentHeight;
            
            response.metadata.push_back(meta);
        }
    }
    
    LogPrint(BCLog::NET, "ContractStateSync: Responding with metadata for %d contracts\n",
             response.metadata.size());
    return response;
}

ContractStateResponse ContractStateSyncManager::HandleChunkRequest(
    const std::vector<uint160>& contracts, const std::vector<uint32_t>& chunkIndices) {
    
    ContractStateResponse response;
    response.type = ContractStateResponse::ResponseType::CHUNK;
    
    if (!database) {
        response.type = ContractStateResponse::ResponseType::ERR_STATE;
        response.errorMessage = "Database not available";
        return response;
    }
    
    size_t numChunks = std::min(contracts.size(), chunkIndices.size());
    numChunks = std::min(numChunks, (size_t)MAX_CHUNKS_PER_REQUEST);
    
    for (size_t i = 0; i < numChunks; i++) {
        ContractStateChunk chunk = CreateChunk(contracts[i], chunkIndices[i]);
        if (!chunk.storageEntries.empty() || chunk.chunkIndex == 0) {
            response.chunks.push_back(chunk);
        }
    }
    
    LogPrint(BCLog::NET, "ContractStateSync: Responding with %d chunks\n",
             response.chunks.size());
    return response;
}

ContractStateResponse ContractStateSyncManager::HandleStateProofRequest(
    const uint160& contract, const uint256& key) {
    
    ContractStateResponse response;
    response.type = ContractStateResponse::ResponseType::STATE_PROOF;
    
    StorageProof proof = GenerateStorageProof(contract, key);
    response.stateProof = proof.proof;
    
    LogPrint(BCLog::NET, "ContractStateSync: Responding with state proof for contract %s\n",
             contract.ToString());
    return response;
}

ContractStateChunk ContractStateSyncManager::CreateChunk(const uint160& contract, 
    uint32_t chunkIndex) {
    
    ContractStateChunk chunk;
    chunk.contractAddress = contract;
    chunk.chunkIndex = chunkIndex;
    chunk.totalChunks = 1;  // Simplified - would calculate based on storage size
    
    if (!database) {
        return chunk;
    }
    
    // Get all storage keys for this contract
    std::string prefix = "S" + contract.ToString();
    std::vector<std::string> keys;
    database->GetAllKeys(prefix, keys);
    
    // Calculate which entries belong to this chunk
    uint32_t startIdx = chunkIndex * MAX_ENTRIES_PER_CHUNK;
    uint32_t endIdx = startIdx + MAX_ENTRIES_PER_CHUNK;
    
    chunk.totalChunks = (keys.size() + MAX_ENTRIES_PER_CHUNK - 1) / MAX_ENTRIES_PER_CHUNK;
    if (chunk.totalChunks == 0) chunk.totalChunks = 1;
    
    for (uint32_t i = startIdx; i < endIdx && i < keys.size(); i++) {
        // Parse key and load value
        uint256 storageKey;
        uint256 storageValue;
        
        if (database->Load(contract, storageKey, storageValue)) {
            chunk.storageEntries.push_back(std::make_pair(storageKey, storageValue));
        }
    }
    
    // Calculate chunk hash
    chunk.chunkHash = chunk.CalculateHash();
    
    return chunk;
}

bool ContractStateSyncManager::ApplyChunk(const ContractStateChunk& chunk) {
    if (!database) return false;
    
    // Apply all storage entries from the chunk
    for (const auto& entry : chunk.storageEntries) {
        if (!database->Store(chunk.contractAddress, entry.first, entry.second)) {
            LogPrintf("ContractStateSync: Failed to store entry for contract %s\n",
                     chunk.contractAddress.ToString());
            return false;
        }
    }
    
    LogPrint(BCLog::NET, "ContractStateSync: Applied chunk %d/%d for contract %s (%d entries)\n",
             chunk.chunkIndex + 1, chunk.totalChunks, chunk.contractAddress.ToString(),
             chunk.storageEntries.size());
    return true;
}

bool ContractStateSyncManager::VerifyChunk(const ContractStateChunk& chunk) {
    return chunk.Verify();
}

uint256 ContractStateSyncManager::CalculateContractStateRoot(const uint160& contract) {
    if (!database) return uint256();
    
    // Get all storage entries for this contract
    std::vector<std::pair<uint256, uint256>> entries;
    
    std::string prefix = "S" + contract.ToString();
    std::vector<std::string> keys;
    database->GetAllKeys(prefix, keys);
    
    for (const std::string& keyStr : keys) {
        uint256 key;
        uint256 value;
        if (database->Load(contract, key, value)) {
            entries.push_back(std::make_pair(key, value));
        }
    }
    
    return CalculateMerkleRoot(entries);
}

uint256 ContractStateSyncManager::CalculateGlobalStateRoot() {
    if (!database) return uint256();
    
    std::vector<uint160> contracts = database->ListContracts();
    
    CHashWriter hasher(SER_GETHASH, 0);
    for (const uint160& contract : contracts) {
        uint256 contractRoot = CalculateContractStateRoot(contract);
        hasher << contract;
        hasher << contractRoot;
    }
    
    return hasher.GetHash();
}

StorageProof ContractStateSyncManager::GenerateStorageProof(const uint160& contract, 
    const uint256& key) {
    
    StorageProof proof;
    proof.contractAddress = contract;
    proof.key = key;
    
    if (!database) return proof;
    
    // Load the value
    if (!database->Load(contract, key, proof.value)) {
        return proof;
    }
    
    // Get all storage entries for Merkle tree
    std::vector<std::pair<uint256, uint256>> entries;
    std::string prefix = "S" + contract.ToString();
    std::vector<std::string> keys;
    database->GetAllKeys(prefix, keys);
    
    for (const std::string& keyStr : keys) {
        uint256 k;
        uint256 v;
        if (database->Load(contract, k, v)) {
            entries.push_back(std::make_pair(k, v));
        }
    }
    
    // Generate Merkle proof
    proof.proof = GenerateMerkleProof(entries, key);
    proof.stateRoot = CalculateMerkleRoot(entries);
    
    return proof;
}

bool ContractStateSyncManager::VerifyStorageProof(const StorageProof& proof) {
    return proof.Verify();
}

SyncProgress ContractStateSyncManager::GetProgress() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return progress;
}

void ContractStateSyncManager::AddSyncPeer(NodeId peerId) {
    std::lock_guard<std::mutex> lock(peersMutex);
    syncPeers.insert(peerId);
    LogPrint(BCLog::NET, "ContractStateSync: Added sync peer %d\n", peerId);
}

void ContractStateSyncManager::RemoveSyncPeer(NodeId peerId) {
    std::lock_guard<std::mutex> lock(peersMutex);
    syncPeers.erase(peerId);
    peerHasContractState.erase(peerId);
    LogPrint(BCLog::NET, "ContractStateSync: Removed sync peer %d\n", peerId);
}

void ContractStateSyncManager::UpdatePeerSyncStatus(NodeId peerId, bool hasContractState) {
    std::lock_guard<std::mutex> lock(peersMutex);
    peerHasContractState[peerId] = hasContractState;
}

uint256 ContractStateSyncManager::GenerateRequestId(const uint160& contract, uint32_t chunkIndex) {
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << contract;
    hasher << chunkIndex;
    hasher << GetTime();
    return hasher.GetHash();
}

void ContractStateSyncManager::CleanupTimedOutRequests() {
    std::lock_guard<std::mutex> lock(pendingMutex);
    
    uint64_t now = GetTime();
    auto it = pendingRequests.begin();
    while (it != pendingRequests.end()) {
        if (now - it->second.requestTime > SYNC_TIMEOUT_SECONDS) {
            LogPrint(BCLog::NET, "ContractStateSync: Request timed out for contract %s chunk %d\n",
                     it->second.contractAddress.ToString(), it->second.chunkIndex);
            it = pendingRequests.erase(it);
        } else {
            ++it;
        }
    }
}

NodeId ContractStateSyncManager::SelectBestPeerForRequest() {
    std::lock_guard<std::mutex> lock(peersMutex);
    
    // Prefer peers that have contract state
    for (const auto& pair : peerHasContractState) {
        if (pair.second && syncPeers.count(pair.first)) {
            return pair.first;
        }
    }
    
    // Fall back to any sync peer
    if (!syncPeers.empty()) {
        return *syncPeers.begin();
    }
    
    return -1;
}

uint256 ContractStateSyncManager::CalculateMerkleRoot(
    const std::vector<std::pair<uint256, uint256>>& entries) {
    
    if (entries.empty()) {
        return uint256();
    }
    
    // Calculate leaf hashes
    std::vector<uint256> hashes;
    for (const auto& entry : entries) {
        CHashWriter hasher(SER_GETHASH, 0);
        hasher << entry.first;
        hasher << entry.second;
        hashes.push_back(hasher.GetHash());
    }
    
    // Build Merkle tree
    while (hashes.size() > 1) {
        std::vector<uint256> newHashes;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            CHashWriter hasher(SER_GETHASH, 0);
            hasher << hashes[i];
            if (i + 1 < hashes.size()) {
                hasher << hashes[i + 1];
            } else {
                hasher << hashes[i];  // Duplicate last hash if odd number
            }
            newHashes.push_back(hasher.GetHash());
        }
        hashes = newHashes;
    }
    
    return hashes[0];
}

std::vector<uint256> ContractStateSyncManager::GenerateMerkleProof(
    const std::vector<std::pair<uint256, uint256>>& entries, const uint256& key) {
    
    std::vector<uint256> proof;
    
    if (entries.empty()) {
        return proof;
    }
    
    // Find the index of the key
    int targetIdx = -1;
    std::vector<uint256> hashes;
    for (size_t i = 0; i < entries.size(); i++) {
        CHashWriter hasher(SER_GETHASH, 0);
        hasher << entries[i].first;
        hasher << entries[i].second;
        hashes.push_back(hasher.GetHash());
        
        if (entries[i].first == key) {
            targetIdx = i;
        }
    }
    
    if (targetIdx < 0) {
        return proof;  // Key not found
    }
    
    // Build proof by walking up the tree
    size_t idx = targetIdx;
    while (hashes.size() > 1) {
        // Add sibling to proof
        size_t siblingIdx = (idx % 2 == 0) ? idx + 1 : idx - 1;
        if (siblingIdx < hashes.size()) {
            proof.push_back(hashes[siblingIdx]);
        } else {
            proof.push_back(hashes[idx]);  // Duplicate if no sibling
        }
        
        // Move to parent level
        std::vector<uint256> newHashes;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            CHashWriter hasher(SER_GETHASH, 0);
            hasher << hashes[i];
            if (i + 1 < hashes.size()) {
                hasher << hashes[i + 1];
            } else {
                hasher << hashes[i];
            }
            newHashes.push_back(hasher.GetHash());
        }
        hashes = newHashes;
        idx = idx / 2;
    }
    
    return proof;
}

bool ContractStateSyncManager::VerifyMerkleProof(const std::vector<uint256>& proof,
    const uint256& root, const uint256& key, const uint256& value) {
    
    // Calculate leaf hash
    CHashWriter leafHasher(SER_GETHASH, 0);
    leafHasher << key;
    leafHasher << value;
    uint256 currentHash = leafHasher.GetHash();
    
    // Walk up the tree
    for (const uint256& sibling : proof) {
        CHashWriter nodeHasher(SER_GETHASH, 0);
        if (currentHash < sibling) {
            nodeHasher << currentHash;
            nodeHasher << sibling;
        } else {
            nodeHasher << sibling;
            nodeHasher << currentHash;
        }
        currentHash = nodeHasher.GetHash();
    }
    
    return currentHash == root;
}

// Global initialization functions
bool InitContractStateSync(CVMDatabase* db) {
    if (g_contractStateSyncManager) {
        return true;  // Already initialized
    }
    
    g_contractStateSyncManager = std::make_unique<ContractStateSyncManager>(db);
    return g_contractStateSyncManager->Initialize();
}

void ShutdownContractStateSync() {
    if (g_contractStateSyncManager) {
        g_contractStateSyncManager->Shutdown();
        g_contractStateSyncManager.reset();
    }
}

} // namespace CVM
