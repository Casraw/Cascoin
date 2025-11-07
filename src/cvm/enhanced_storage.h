// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ENHANCED_STORAGE_H
#define CASCOIN_CVM_ENHANCED_STORAGE_H

#include <cvm/vmstate.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <uint256.h>
#include <dbwrapper.h>
#include <map>
#include <memory>
#include <vector>
#include <string>

namespace CVM {

/**
 * Storage operation for atomic transactions
 */
struct StorageOperation {
    enum class Type {
        STORE,
        DELETE
    };
    
    Type type;
    uint160 contractAddr;
    uint256 key;
    uint256 value;
    
    StorageOperation(Type t, const uint160& addr, const uint256& k, const uint256& v = uint256())
        : type(t), contractAddr(addr), key(k), value(v) {}
};

/**
 * Trust-tagged storage region for reputation-aware data structures
 */
struct TrustTaggedRegion {
    uint160 contractAddr;
    std::string regionId;
    uint8_t minReputation;
    std::map<uint256, uint256> data;
    uint64_t createdBlock;
    uint64_t lastAccessBlock;
    
    TrustTaggedRegion() : minReputation(0), createdBlock(0), lastAccessBlock(0) {}
    TrustTaggedRegion(const uint160& addr, const std::string& id, uint8_t minRep)
        : contractAddr(addr), regionId(id), minReputation(minRep), 
          createdBlock(0), lastAccessBlock(0) {}
};

/**
 * EnhancedStorage - EVM-compatible storage layer with trust-aware features
 * 
 * Provides:
 * - EVM-compatible 32-byte key/value storage
 * - Backward compatibility with existing CVM storage
 * - Atomic storage operations across contract calls
 * - Trust-score caching for reputation-aware operations
 * - Storage quotas based on reputation levels
 * - Trust-tagged memory regions
 * - Storage rent and cleanup mechanisms
 * - Storage proofs for light clients
 */
class EnhancedStorage : public ContractStorage {
public:
    EnhancedStorage(CVMDatabase* db);
    ~EnhancedStorage();
    
    // ContractStorage interface (EVM-compatible 32-byte keys/values)
    bool Load(const uint160& contractAddr, const uint256& key, uint256& value) override;
    bool Store(const uint160& contractAddr, const uint256& key, const uint256& value) override;
    bool Exists(const uint160& contractAddr) override;
    
    // Trust-aware storage operations with reputation-weighted costs
    bool LoadWithTrust(const uint160& contractAddr, const uint256& key, uint256& value, 
                      const TrustContext& trust);
    bool StoreWithTrust(const uint160& contractAddr, const uint256& key, const uint256& value,
                       const TrustContext& trust);
    
    // Reputation-based storage quotas and limits
    uint64_t GetStorageQuota(const uint160& address, uint8_t reputation);
    bool CheckStorageLimit(const uint160& address, uint64_t requestedSize);
    uint64_t GetCurrentStorageUsage(const uint160& address);
    
    // Reputation-weighted storage costs
    uint64_t CalculateStorageCost(const uint160& address, uint8_t reputation, bool isWrite);
    uint64_t GetBaseStorageCost(bool isWrite) const;
    double GetReputationCostMultiplier(uint8_t reputation) const;
    
    // Trust-tagged memory regions for reputation-aware data structures
    bool CreateTrustTaggedRegion(const uint160& contractAddr, const std::string& regionId, 
                                uint8_t minReputation);
    bool AccessTrustTaggedRegion(const uint160& contractAddr, const std::string& regionId,
                                const TrustContext& trust);
    bool StoreTrustTaggedValue(const uint160& contractAddr, const std::string& regionId,
                              const uint256& key, const uint256& value, const TrustContext& trust);
    bool LoadTrustTaggedValue(const uint160& contractAddr, const std::string& regionId,
                             const uint256& key, uint256& value, const TrustContext& trust);
    
    // Reputation caching and trust score management
    void CacheTrustScore(const uint160& address, uint8_t score, uint64_t timestamp);
    bool GetCachedTrustScore(const uint160& address, uint8_t& score);
    void InvalidateTrustCache(const uint160& address);
    void CleanupTrustCache(uint64_t maxAge);
    
    // Storage rent and reputation-based cleanup
    bool PayStorageRent(const uint160& contractAddr, uint64_t amount);
    uint64_t GetStorageRentBalance(const uint160& contractAddr);
    void CleanupExpiredStorage(uint64_t currentBlock);
    void CleanupLowReputationStorage(uint8_t minReputation, uint64_t currentBlock);
    
    // Storage proofs for light clients
    std::vector<uint256> GenerateStorageProof(const uint160& contractAddr, const uint256& key);
    bool VerifyStorageProof(const std::vector<uint256>& proof, const uint256& root, 
                           const uint160& contractAddr, const uint256& key, const uint256& value);
    
    // Atomic operations across contract calls
    void BeginAtomicOperation();
    void CommitAtomicOperation();
    void RollbackAtomicOperation();
    bool IsInAtomicOperation() const { return inAtomicOperation; }
    
    // Backward compatibility with existing CVM storage
    bool LoadLegacy(const uint160& contractAddr, const uint256& key, uint256& value);
    bool StoreLegacy(const uint160& contractAddr, const uint256& key, const uint256& value);
    
    // Statistics and monitoring
    struct StorageStats {
        uint64_t totalKeys;
        uint64_t totalSize;
        uint64_t cacheHits;
        uint64_t cacheMisses;
        uint64_t trustCacheHits;
        uint64_t trustCacheMisses;
        
        StorageStats() : totalKeys(0), totalSize(0), cacheHits(0), 
                        cacheMisses(0), trustCacheHits(0), trustCacheMisses(0) {}
    };
    
    StorageStats GetStats() const { return stats; }
    void ResetStats() { stats = StorageStats(); }
    
private:
    CVMDatabase* database;
    
    // Trust score cache
    struct TrustCacheEntry {
        uint8_t score;
        uint64_t timestamp;
        
        TrustCacheEntry() : score(0), timestamp(0) {}
        TrustCacheEntry(uint8_t s, uint64_t ts) : score(s), timestamp(ts) {}
    };
    std::map<uint160, TrustCacheEntry> trustCache;
    
    // Storage quotas and usage tracking
    std::map<uint160, uint64_t> storageQuotas;
    std::map<uint160, uint64_t> storageUsage;
    
    // Storage rent balances
    std::map<uint160, uint64_t> storageRentBalances;
    
    // Trust-tagged regions
    std::map<std::string, TrustTaggedRegion> trustTaggedRegions;
    
    // Atomic operation state
    bool inAtomicOperation;
    std::vector<StorageOperation> pendingOperations;
    std::map<std::pair<uint160, uint256>, uint256> atomicSnapshot;
    
    // Statistics
    mutable StorageStats stats;
    
    // Helper functions
    std::string MakeRegionKey(const uint160& contractAddr, const std::string& regionId) const;
    uint64_t CalculateDefaultQuota(uint8_t reputation) const;
    bool HasSufficientReputation(const TrustContext& trust, uint8_t required) const;
    
    // Storage key generation
    std::string MakeStorageKey(const uint160& contractAddr, const uint256& key) const;
    std::string MakeTrustTaggedKey(const uint160& contractAddr, const std::string& regionId, 
                                  const uint256& key) const;
};

} // namespace CVM

#endif // CASCOIN_CVM_ENHANCED_STORAGE_H
