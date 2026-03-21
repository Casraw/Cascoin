// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_BEENFT_H
#define CASCOIN_BEENFT_H

#include <amount.h>
#include <uint256.h>
#include <script/script.h>
#include <support/allocators/secure.h>
#include <hash.h>
#include <primitives/transaction.h>

#include <string>
#include <vector>
#include <map>

/*
Cascoin: Bee NFT System

Individual Bee Tokenization allows transferring specific bees from BCTs as NFT-like tokens.
Each bee from a BCT can be tokenized and transferred independently, enabling:
- Bee trading markets
- Granular mining rights transfer
- Individual bee ownership tracking
- Integration with DeFi systems

Architecture:
1. BCT creates N bees (existing system)
2. Owner can tokenize individual bees via OP_BEE_TOKEN transaction
3. Bee tokens can be transferred via OP_BEE_TRANSFER transaction
4. Mining system checks bee token ownership for mining rights
5. Rewards go to current bee token owner

Each bee NFT contains:
- Original BCT transaction ID
- Bee index within that BCT (0 to N-1)
- Maturity and expiry heights
- Current owner address
- Mining performance history
*/

// Maximum data size for bee NFT transactions
const int BEE_NFT_MAX_DATA_SIZE = 4096; // 4KB for larger metadata as required

// Cascoin: Memory management for NFT system - prevent memory leaks
const int MAX_NFT_CACHE_ENTRIES = 500; // Maximum NFT objects to keep in memory
const int NFT_CACHE_CLEANUP_THRESHOLD = 600; // Start cleanup when this many entries
const size_t MAX_NFT_MEMORY_POOL_SIZE = 8 * 1024 * 1024; // 8MB max for NFT data pool

// Magic bytes for different NFT types
#define NFT_MAGIC_MICE_TOKEN "CASTOK"
#define NFT_MAGIC_MICE_TRANSFER "CASXFR"
#define NFT_MAGIC_GENERIC "CASNFT"
#define NFT_MAGIC_GENERIC_TRANSFER "CASGEN"

// Bee NFT Token structure
struct BeeNFTToken {
    uint256 originalBCT;        // Original BCT transaction hash
    uint32_t beeIndex;          // Index of this bee within the BCT (0-based)
    uint32_t maturityHeight;    // Block height when bee matures for mining
    uint32_t expiryHeight;      // Block height when bee expires
    uint32_t tokenizedHeight;   // Block height when bee was tokenized
    std::string currentOwner;   // Current owner's address
    
    // Serialization for storage and network transmission
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(originalBCT);
        READWRITE(beeIndex);
        READWRITE(maturityHeight);
        READWRITE(expiryHeight);
        READWRITE(tokenizedHeight);
        READWRITE(currentOwner);
    }
    
    // Generate unique bee NFT ID
    uint256 GetBeeNFTId() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << originalBCT << beeIndex;
        return ss.GetHash();
    }
    
    // Check if bee is currently mature for mining
    bool IsMature(int currentHeight) const {
        return currentHeight >= maturityHeight && currentHeight < expiryHeight;
    }
    
    // Check if bee has expired
    bool IsExpired(int currentHeight) const {
        return currentHeight >= expiryHeight;
    }
    
    // Cascoin: Memory management - estimate memory usage for this NFT
    size_t EstimateMemoryUsage() const {
        return sizeof(BeeNFTToken) + currentOwner.size() + 64; // Conservative estimate: ~512 bytes per entry
    }
};

// Cascoin: NFT Memory Pool Manager to prevent memory leaks while maintaining 4KB data size
class NFTMemoryManager {
private:
    static std::map<uint256, std::shared_ptr<std::vector<unsigned char>>> nftDataPool;
    static std::mutex poolMutex;
    static size_t currentPoolSize;
    
public:
    // Store NFT data in managed pool
    static bool StoreNFTData(const uint256& nftId, const std::vector<unsigned char>& data) {
        std::lock_guard<std::mutex> lock(poolMutex);
        
        // Check if we would exceed memory pool size
        if (currentPoolSize + data.size() > MAX_NFT_MEMORY_POOL_SIZE) {
            // Cleanup oldest entries to make space
            CleanupOldEntries();
        }
        
        auto dataPtr = std::make_shared<std::vector<unsigned char>>(data);
        nftDataPool[nftId] = dataPtr;
        currentPoolSize += data.size();
        return true;
    }
    
    // Retrieve NFT data from pool
    static std::shared_ptr<std::vector<unsigned char>> GetNFTData(const uint256& nftId) {
        std::lock_guard<std::mutex> lock(poolMutex);
        auto it = nftDataPool.find(nftId);
        return (it != nftDataPool.end()) ? it->second : nullptr;
    }
    
    // Cleanup old entries when memory pool gets too large
    static void CleanupOldEntries() {
        if (nftDataPool.size() <= MAX_NFT_CACHE_ENTRIES) return;
        
        // Remove oldest 25% of entries to make room
        size_t toRemove = nftDataPool.size() / 4;
        auto it = nftDataPool.begin();
        for (size_t i = 0; i < toRemove && it != nftDataPool.end(); ++i) {
            if (it->second) {
                currentPoolSize -= it->second->size();
            }
            it = nftDataPool.erase(it);
        }
    }
    
    // Get current memory usage statistics
    static size_t GetPoolSize() { 
        std::lock_guard<std::mutex> lock(poolMutex);
        return currentPoolSize; 
    }
    
    static size_t GetEntryCount() { 
        std::lock_guard<std::mutex> lock(poolMutex);
        return nftDataPool.size(); 
    }
};

// Bee NFT Transfer record
struct BeeNFTTransfer {
    uint256 beeNFTId;           // Unique bee NFT identifier
    std::string fromOwner;      // Previous owner address
    std::string toOwner;        // New owner address
    uint32_t transferHeight;    // Block height of transfer
    uint256 transferTxId;       // Transaction hash of transfer
    CAmount transferFee;        // Fee paid for transfer
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(beeNFTId);
        READWRITE(fromOwner);
        READWRITE(toOwner);
        READWRITE(transferHeight);
        READWRITE(transferTxId);
        READWRITE(transferFee);
    }
};

// Bee NFT mining performance record
struct BeeNFTMiningRecord {
    uint256 beeNFTId;           // Unique bee NFT identifier
    uint32_t blockHeight;       // Height of mined block
    uint256 blockHash;          // Hash of mined block
    CAmount rewardAmount;       // Mining reward received
    uint32_t difficulty;        // Mining difficulty at time of block
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(beeNFTId);
        READWRITE(blockHeight);
        READWRITE(blockHash);
        READWRITE(rewardAmount);
        READWRITE(difficulty);
    }
};

// Generic NFT structures
struct GenericNFT {
    std::string name;
    std::string description;
    std::string metadata;
    int64_t amount;
    std::string owner_address;
    
    GenericNFT() : amount(0) {}
    GenericNFT(const std::string& n, const std::string& desc, const std::string& meta, int64_t amt, const std::string& owner)
        : name(n), description(desc), metadata(meta), amount(amt), owner_address(owner) {}
};

struct GenericNFTTransfer {
    std::string nft_txid;
    std::string recipient_address;
    int64_t amount;
    
    GenericNFTTransfer() : amount(0) {}
    GenericNFTTransfer(const std::string& txid, const std::string& recipient, int64_t amt)
        : nft_txid(txid), recipient_address(recipient), amount(amt) {}
};

// Validation functions
bool IsValidBeeNFTTokenTransaction(const CTransaction& tx, std::string& error);
bool IsValidBeeNFTTransferTransaction(const CTransaction& tx, std::string& error);

// Parsing functions for bee NFT transactions
bool ParseBeeNFTTokenTransaction(const CTransaction& tx, std::vector<BeeNFTToken>& tokens, std::string& error);
bool ParseBeeNFTTransferTransaction(const CTransaction& tx, std::vector<BeeNFTTransfer>& transfers, std::string& error);

// Utility functions
std::vector<unsigned char> SerializeBeeNFTToken(const BeeNFTToken& token);
bool DeserializeBeeNFTToken(const std::vector<unsigned char>& data, BeeNFTToken& token);

std::vector<unsigned char> SerializeBeeNFTTransfer(const BeeNFTTransfer& transfer);
bool DeserializeBeeNFTTransfer(const std::vector<unsigned char>& data, BeeNFTTransfer& transfer);

// Create bee NFT token transaction script
CScript CreateBeeNFTTokenScript(const std::vector<BeeNFTToken>& tokens);

// Create bee NFT transfer transaction script  
CScript CreateBeeNFTTransferScript(const std::vector<BeeNFTTransfer>& transfers);

// Generic NFT script creation functions
std::vector<unsigned char> CreateGenericNFTScript(const std::vector<GenericNFT>& nfts);
std::vector<unsigned char> CreateGenericNFTTransferScript(const std::vector<GenericNFTTransfer>& transfers);

// Generic NFT parsing functions
bool IsValidGenericNFTTransaction(const CTransaction& tx, std::string& error);
bool ParseGenericNFTTransaction(const CTransaction& tx, std::vector<GenericNFT>& nfts, std::string& error);
bool IsValidGenericNFTTransferTransaction(const CTransaction& tx, std::string& error);
bool ParseGenericNFTTransferTransaction(const CTransaction& tx, std::vector<GenericNFTTransfer>& transfers, std::string& error);

#endif // CASCOIN_BEENFT_H
