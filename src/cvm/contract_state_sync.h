// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CONTRACT_STATE_SYNC_H
#define CASCOIN_CVM_CONTRACT_STATE_SYNC_H

#include <cvm/cvmdb.h>
#include <cvm/contract.h>
#include <uint256.h>
#include <serialize.h>
#include <net.h>

#include <map>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

namespace CVM {

/**
 * Contract state chunk for efficient synchronization
 * Contracts are split into chunks for parallel download
 */
struct ContractStateChunk {
    uint160 contractAddress;
    uint32_t chunkIndex;
    uint32_t totalChunks;
    std::vector<std::pair<uint256, uint256>> storageEntries;  // key-value pairs
    uint256 chunkHash;  // Hash of this chunk for verification
    
    ContractStateChunk() : chunkIndex(0), totalChunks(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(contractAddress);
        READWRITE(chunkIndex);
        READWRITE(totalChunks);
        READWRITE(storageEntries);
        READWRITE(chunkHash);
    }
    
    // Calculate chunk hash for verification
    uint256 CalculateHash() const;
    bool Verify() const { return chunkHash == CalculateHash(); }
};

/**
 * Contract metadata for synchronization
 */
struct ContractSyncMetadata {
    uint160 contractAddress;
    uint256 codeHash;
    uint64_t storageSize;       // Number of storage entries
    uint32_t chunkCount;        // Number of chunks needed
    uint256 stateRoot;          // Merkle root of all storage
    uint64_t lastModifiedBlock; // Block height of last modification
    
    ContractSyncMetadata() : storageSize(0), chunkCount(0), lastModifiedBlock(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(contractAddress);
        READWRITE(codeHash);
        READWRITE(storageSize);
        READWRITE(chunkCount);
        READWRITE(stateRoot);
        READWRITE(lastModifiedBlock);
    }
};

/**
 * Request for contract state synchronization
 */
struct ContractStateRequest {
    enum class RequestType : uint8_t {
        LIST_CONTRACTS = 0,     // Request list of all contracts
        CONTRACT_METADATA = 1,  // Request metadata for specific contracts
        CONTRACT_CHUNK = 2,     // Request specific chunk of contract state
        STATE_PROOF = 3         // Request storage proof for verification
    };
    
    RequestType type;
    std::vector<uint160> contractAddresses;  // For METADATA and CHUNK requests
    std::vector<uint32_t> chunkIndices;      // For CHUNK requests
    uint64_t fromBlock;                       // For incremental sync
    
    ContractStateRequest() : type(RequestType::LIST_CONTRACTS), fromBlock(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint8_t typeInt = static_cast<uint8_t>(type);
        READWRITE(typeInt);
        if (ser_action.ForRead()) {
            type = static_cast<RequestType>(typeInt);
        }
        READWRITE(contractAddresses);
        READWRITE(chunkIndices);
        READWRITE(fromBlock);
    }
};

/**
 * Response for contract state synchronization
 */
struct ContractStateResponse {
    enum class ResponseType : uint8_t {
        CONTRACT_LIST = 0,      // List of contract addresses
        METADATA = 1,           // Contract metadata
        CHUNK = 2,              // Contract state chunk
        STATE_PROOF = 3,        // Storage proof
        ERROR = 255             // Error response
    };
    
    ResponseType type;
    std::vector<uint160> contractList;
    std::vector<ContractSyncMetadata> metadata;
    std::vector<ContractStateChunk> chunks;
    std::vector<uint256> stateProof;
    std::string errorMessage;
    
    ContractStateResponse() : type(ResponseType::ERROR) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint8_t typeInt = static_cast<uint8_t>(type);
        READWRITE(typeInt);
        if (ser_action.ForRead()) {
            type = static_cast<ResponseType>(typeInt);
        }
        READWRITE(contractList);
        READWRITE(metadata);
        READWRITE(chunks);
        READWRITE(stateProof);
        READWRITE(errorMessage);
    }
};

/**
 * Storage proof for light client verification
 */
struct StorageProof {
    uint160 contractAddress;
    uint256 key;
    uint256 value;
    std::vector<uint256> proof;  // Merkle proof path
    uint256 stateRoot;           // Expected state root
    
    StorageProof() {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(contractAddress);
        READWRITE(key);
        READWRITE(value);
        READWRITE(proof);
        READWRITE(stateRoot);
    }
    
    // Verify the proof against the state root
    bool Verify() const;
};

/**
 * Synchronization progress tracking
 */
struct SyncProgress {
    uint64_t totalContracts;
    uint64_t syncedContracts;
    uint64_t totalChunks;
    uint64_t downloadedChunks;
    uint64_t verifiedChunks;
    uint64_t failedChunks;
    uint64_t startTime;
    uint64_t lastUpdateTime;
    
    SyncProgress() : totalContracts(0), syncedContracts(0), totalChunks(0),
                     downloadedChunks(0), verifiedChunks(0), failedChunks(0),
                     startTime(0), lastUpdateTime(0) {}
    
    double GetProgressPercent() const {
        if (totalChunks == 0) return 0.0;
        return (double)verifiedChunks / totalChunks * 100.0;
    }
    
    bool IsComplete() const {
        return totalChunks > 0 && verifiedChunks == totalChunks;
    }
};

/**
 * Contract State Synchronization Manager
 * 
 * Handles efficient synchronization of contract state for new nodes:
 * - Parallel chunk download from multiple peers
 * - Merkle proof verification for data integrity
 * - Incremental sync for catching up after disconnection
 * - Priority-based download (active contracts first)
 */
class ContractStateSyncManager {
public:
    ContractStateSyncManager(CVMDatabase* db);
    ~ContractStateSyncManager();
    
    // Initialize synchronization
    bool Initialize();
    void Shutdown();
    
    // Start full state synchronization
    bool StartFullSync();
    
    // Start incremental sync from a specific block
    bool StartIncrementalSync(uint64_t fromBlock);
    
    // Process incoming sync messages
    void ProcessContractStateRequest(CNode* pfrom, const ContractStateRequest& request,
                                    CConnman* connman);
    void ProcessContractStateResponse(CNode* pfrom, const ContractStateResponse& response);
    
    // Request state from peers
    bool RequestContractList(CNode* peer, CConnman* connman);
    bool RequestContractMetadata(CNode* peer, const std::vector<uint160>& contracts,
                                CConnman* connman);
    bool RequestContractChunk(CNode* peer, const uint160& contract, uint32_t chunkIndex,
                             CConnman* connman);
    bool RequestStateProof(CNode* peer, const uint160& contract, const uint256& key,
                          CConnman* connman);
    
    // Generate responses for incoming requests
    ContractStateResponse HandleListContractsRequest();
    ContractStateResponse HandleMetadataRequest(const std::vector<uint160>& contracts);
    ContractStateResponse HandleChunkRequest(const std::vector<uint160>& contracts,
                                            const std::vector<uint32_t>& chunkIndices);
    ContractStateResponse HandleStateProofRequest(const uint160& contract, const uint256& key);
    
    // Chunk management
    ContractStateChunk CreateChunk(const uint160& contract, uint32_t chunkIndex);
    bool ApplyChunk(const ContractStateChunk& chunk);
    bool VerifyChunk(const ContractStateChunk& chunk);
    
    // State root calculation
    uint256 CalculateContractStateRoot(const uint160& contract);
    uint256 CalculateGlobalStateRoot();
    
    // Storage proof generation and verification
    StorageProof GenerateStorageProof(const uint160& contract, const uint256& key);
    bool VerifyStorageProof(const StorageProof& proof);
    
    // Progress tracking
    SyncProgress GetProgress() const;
    bool IsSyncing() const { return isSyncing.load(); }
    bool IsSynced() const { return isSynced.load(); }
    
    // Peer management for sync
    void AddSyncPeer(NodeId peerId);
    void RemoveSyncPeer(NodeId peerId);
    void UpdatePeerSyncStatus(NodeId peerId, bool hasContractState);
    
    // Configuration
    static const uint32_t MAX_ENTRIES_PER_CHUNK = 1000;
    static const uint32_t MAX_CHUNKS_PER_REQUEST = 10;
    static const uint32_t MAX_CONTRACTS_PER_REQUEST = 100;
    static const uint32_t SYNC_TIMEOUT_SECONDS = 60;
    
private:
    CVMDatabase* database;
    
    // Synchronization state
    std::atomic<bool> isSyncing;
    std::atomic<bool> isSynced;
    SyncProgress progress;
    mutable std::mutex progressMutex;
    
    // Pending requests tracking
    struct PendingRequest {
        uint160 contractAddress;
        uint32_t chunkIndex;
        NodeId peerId;
        uint64_t requestTime;
    };
    std::map<uint256, PendingRequest> pendingRequests;  // requestId -> request
    std::mutex pendingMutex;
    
    // Downloaded but not yet applied chunks
    std::map<std::pair<uint160, uint32_t>, ContractStateChunk> downloadedChunks;
    std::mutex chunksMutex;
    
    // Sync peers
    std::set<NodeId> syncPeers;
    std::map<NodeId, bool> peerHasContractState;
    std::mutex peersMutex;
    
    // Contract metadata cache
    std::map<uint160, ContractSyncMetadata> contractMetadata;
    std::mutex metadataMutex;
    
    // Helper functions
    uint256 GenerateRequestId(const uint160& contract, uint32_t chunkIndex);
    void CleanupTimedOutRequests();
    NodeId SelectBestPeerForRequest();
    void ScheduleNextChunkDownload();
    
    // Merkle tree helpers
    uint256 CalculateMerkleRoot(const std::vector<std::pair<uint256, uint256>>& entries);
    std::vector<uint256> GenerateMerkleProof(const std::vector<std::pair<uint256, uint256>>& entries,
                                            const uint256& key);
    bool VerifyMerkleProof(const std::vector<uint256>& proof, const uint256& root,
                          const uint256& key, const uint256& value);
};

// Global contract state sync manager
extern std::unique_ptr<ContractStateSyncManager> g_contractStateSyncManager;

// Initialize contract state sync
bool InitContractStateSync(CVMDatabase* db);
void ShutdownContractStateSync();

} // namespace CVM

#endif // CASCOIN_CVM_CONTRACT_STATE_SYNC_H
