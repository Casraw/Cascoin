// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/enhanced_storage.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>

namespace CVM {

// Default storage quota: 1MB base + reputation bonus
static constexpr uint64_t BASE_STORAGE_QUOTA = 1000000; // 1MB
static constexpr uint64_t REPUTATION_QUOTA_MULTIPLIER = 10000; // 10KB per reputation point

// Trust cache expiry: 1 hour (in seconds)
static constexpr uint64_t TRUST_CACHE_EXPIRY = 3600;

// Storage rent: blocks before cleanup
static constexpr uint64_t STORAGE_RENT_PERIOD = 100000; // ~69 days at 1 minute blocks

// Storage costs (in gas units, compatible with EVM)
static constexpr uint64_t BASE_STORAGE_READ_COST = 200;   // EVM SLOAD cost
static constexpr uint64_t BASE_STORAGE_WRITE_COST = 5000; // EVM SSTORE cost (new value)

// Reputation-based cost multipliers
// High reputation (80+): 0.1x cost (90% discount)
// Good reputation (60-79): 0.5x cost (50% discount)
// Average reputation (40-59): 1.0x cost (no discount)
// Low reputation (20-39): 1.5x cost (50% penalty)
// Very low reputation (0-19): 2.0x cost (100% penalty)

EnhancedStorage::EnhancedStorage(CVMDatabase* db)
    : database(db), inAtomicOperation(false)
{
    if (!database) {
        LogPrintf("EnhancedStorage: Warning - initialized with null database\n");
    }
}

EnhancedStorage::~EnhancedStorage()
{
    // Ensure no pending atomic operations
    if (inAtomicOperation) {
        LogPrintf("EnhancedStorage: Warning - destroying with pending atomic operation\n");
        RollbackAtomicOperation();
    }
}

// ContractStorage interface implementation

bool EnhancedStorage::Load(const uint160& contractAddr, const uint256& key, uint256& value)
{
    if (!database) {
        return false;
    }
    
    // Check atomic operation snapshot first
    if (inAtomicOperation) {
        auto it = atomicSnapshot.find(std::make_pair(contractAddr, key));
        if (it != atomicSnapshot.end()) {
            value = it->second;
            stats.cacheHits++;
            return true;
        }
    }
    
    // Load from database
    bool result = database->Load(contractAddr, key, value);
    if (result) {
        stats.cacheHits++;
    } else {
        stats.cacheMisses++;
    }
    
    return result;
}

bool EnhancedStorage::Store(const uint160& contractAddr, const uint256& key, const uint256& value)
{
    if (!database) {
        return false;
    }
    
    // If in atomic operation, add to pending operations
    if (inAtomicOperation) {
        pendingOperations.emplace_back(StorageOperation::Type::STORE, contractAddr, key, value);
        atomicSnapshot[std::make_pair(contractAddr, key)] = value;
        return true;
    }
    
    // Direct store
    bool result = database->Store(contractAddr, key, value);
    if (result) {
        // Update storage usage tracking
        storageUsage[contractAddr] += 32; // 32 bytes per key-value pair
        stats.totalKeys++;
        stats.totalSize += 32;
    }
    
    return result;
}

bool EnhancedStorage::Exists(const uint160& contractAddr)
{
    if (!database) {
        return false;
    }
    
    return database->Exists(contractAddr);
}

// Trust-aware storage operations

bool EnhancedStorage::LoadWithTrust(const uint160& contractAddr, const uint256& key, 
                                   uint256& value, const TrustContext& trust)
{
    // Check reputation requirements
    uint32_t callerRep = trust.GetCallerReputation();
    if (callerRep < 10) {
        LogPrint(BCLog::CVM, "EnhancedStorage: LoadWithTrust denied - insufficient reputation (%d)\n", 
                 callerRep);
        return false;
    }
    
    // Calculate reputation-weighted storage cost
    uint64_t storageCost = CalculateStorageCost(contractAddr, callerRep, false);
    
    // Log the cost for gas accounting (actual gas deduction happens in VM layer)
    LogPrint(BCLog::CVM, "EnhancedStorage: Storage read cost: %d gas (reputation: %d)\n",
             storageCost, callerRep);
    
    // Standard load operation
    return Load(contractAddr, key, value);
}

bool EnhancedStorage::StoreWithTrust(const uint160& contractAddr, const uint256& key, 
                                    const uint256& value, const TrustContext& trust)
{
    // Check storage quota based on reputation
    uint32_t callerRep = trust.GetCallerReputation();
    uint64_t quota = GetStorageQuota(contractAddr, callerRep);
    uint64_t currentUsage = GetCurrentStorageUsage(contractAddr);
    
    if (currentUsage + 32 > quota) {
        LogPrint(BCLog::CVM, "EnhancedStorage: StoreWithTrust denied - quota exceeded (%d/%d)\n", 
                 currentUsage + 32, quota);
        return false;
    }
    
    // Calculate reputation-weighted storage cost
    uint64_t storageCost = CalculateStorageCost(contractAddr, callerRep, true);
    
    // Log the cost for gas accounting (actual gas deduction happens in VM layer)
    LogPrint(BCLog::CVM, "EnhancedStorage: Storage write cost: %d gas (reputation: %d)\n",
             storageCost, callerRep);
    
    // Standard store operation
    return Store(contractAddr, key, value);
}

// Reputation-based storage quotas

uint64_t EnhancedStorage::GetStorageQuota(const uint160& address, uint8_t reputation)
{
    // Check if custom quota is set
    auto it = storageQuotas.find(address);
    if (it != storageQuotas.end()) {
        return it->second;
    }
    
    // Calculate default quota based on reputation
    return CalculateDefaultQuota(reputation);
}

bool EnhancedStorage::CheckStorageLimit(const uint160& address, uint64_t requestedSize)
{
    uint8_t reputation = 50; // Default reputation if not cached
    GetCachedTrustScore(address, reputation);
    
    uint64_t quota = GetStorageQuota(address, reputation);
    uint64_t currentUsage = GetCurrentStorageUsage(address);
    
    return (currentUsage + requestedSize) <= quota;
}

uint64_t EnhancedStorage::GetCurrentStorageUsage(const uint160& address)
{
    auto it = storageUsage.find(address);
    if (it != storageUsage.end()) {
        return it->second;
    }
    return 0;
}

// Trust-tagged memory regions

bool EnhancedStorage::CreateTrustTaggedRegion(const uint160& contractAddr, 
                                             const std::string& regionId, 
                                             uint8_t minReputation)
{
    std::string key = MakeRegionKey(contractAddr, regionId);
    
    // Check if region already exists
    if (trustTaggedRegions.find(key) != trustTaggedRegions.end()) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Trust-tagged region already exists: %s\n", key);
        return false;
    }
    
    // Create new region
    TrustTaggedRegion region(contractAddr, regionId, minReputation);
    trustTaggedRegions[key] = region;
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Created trust-tagged region: %s (minRep: %d)\n", 
             key, minReputation);
    return true;
}

bool EnhancedStorage::AccessTrustTaggedRegion(const uint160& contractAddr, 
                                             const std::string& regionId,
                                             const TrustContext& trust)
{
    std::string key = MakeRegionKey(contractAddr, regionId);
    
    auto it = trustTaggedRegions.find(key);
    if (it == trustTaggedRegions.end()) {
        return false;
    }
    
    // Check reputation requirement
    return HasSufficientReputation(trust, it->second.minReputation);
}

bool EnhancedStorage::StoreTrustTaggedValue(const uint160& contractAddr, 
                                           const std::string& regionId,
                                           const uint256& key, const uint256& value, 
                                           const TrustContext& trust)
{
    if (!AccessTrustTaggedRegion(contractAddr, regionId, trust)) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Access denied to trust-tagged region\n");
        return false;
    }
    
    std::string regionKey = MakeRegionKey(contractAddr, regionId);
    auto it = trustTaggedRegions.find(regionKey);
    if (it != trustTaggedRegions.end()) {
        it->second.data[key] = value;
        return true;
    }
    
    return false;
}

bool EnhancedStorage::LoadTrustTaggedValue(const uint160& contractAddr, 
                                          const std::string& regionId,
                                          const uint256& key, uint256& value, 
                                          const TrustContext& trust)
{
    if (!AccessTrustTaggedRegion(contractAddr, regionId, trust)) {
        return false;
    }
    
    std::string regionKey = MakeRegionKey(contractAddr, regionId);
    auto it = trustTaggedRegions.find(regionKey);
    if (it != trustTaggedRegions.end()) {
        auto dataIt = it->second.data.find(key);
        if (dataIt != it->second.data.end()) {
            value = dataIt->second;
            return true;
        }
    }
    
    return false;
}

// Reputation caching

void EnhancedStorage::CacheTrustScore(const uint160& address, uint8_t score, uint64_t timestamp)
{
    trustCache[address] = TrustCacheEntry(score, timestamp);
    LogPrint(BCLog::CVM, "EnhancedStorage: Cached trust score for %s: %d\n", 
             address.ToString(), score);
}

bool EnhancedStorage::GetCachedTrustScore(const uint160& address, uint8_t& score)
{
    auto it = trustCache.find(address);
    if (it != trustCache.end()) {
        // Check if cache entry is still valid
        uint64_t currentTime = GetTime();
        if (currentTime - it->second.timestamp < TRUST_CACHE_EXPIRY) {
            score = it->second.score;
            stats.trustCacheHits++;
            return true;
        } else {
            // Cache expired
            trustCache.erase(it);
        }
    }
    
    stats.trustCacheMisses++;
    return false;
}

void EnhancedStorage::InvalidateTrustCache(const uint160& address)
{
    trustCache.erase(address);
}

void EnhancedStorage::CleanupTrustCache(uint64_t maxAge)
{
    uint64_t currentTime = GetTime();
    auto it = trustCache.begin();
    while (it != trustCache.end()) {
        if (currentTime - it->second.timestamp > maxAge) {
            it = trustCache.erase(it);
        } else {
            ++it;
        }
    }
}

// Storage rent and cleanup

bool EnhancedStorage::PayStorageRent(const uint160& contractAddr, uint64_t amount)
{
    storageRentBalances[contractAddr] += amount;
    LogPrint(BCLog::CVM, "EnhancedStorage: Paid storage rent for %s: %d (total: %d)\n", 
             contractAddr.ToString(), amount, storageRentBalances[contractAddr]);
    return true;
}

uint64_t EnhancedStorage::GetStorageRentBalance(const uint160& contractAddr)
{
    auto it = storageRentBalances.find(contractAddr);
    if (it != storageRentBalances.end()) {
        return it->second;
    }
    return 0;
}

void EnhancedStorage::CleanupExpiredStorage(uint64_t currentBlock)
{
    if (!database) {
        return;
    }
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Starting expired storage cleanup at block %d\n", currentBlock);
    
    size_t cleanedContracts = 0;
    
    // Iterate through storage rent balances
    auto it = storageRentBalances.begin();
    while (it != storageRentBalances.end()) {
        const uint160& contractAddr = it->first;
        uint64_t rentBalance = it->second;
        
        // Check if rent has expired (balance is 0 or very low)
        if (rentBalance == 0) {
            // Check how long the contract has been without rent
            // For now, we'll just mark it for cleanup
            LogPrint(BCLog::CVM, "EnhancedStorage: Contract %s has expired rent, marking for cleanup\n",
                     contractAddr.ToString());
            
            // Clear storage usage tracking
            storageUsage.erase(contractAddr);
            storageQuotas.erase(contractAddr);
            
            it = storageRentBalances.erase(it);
            cleanedContracts++;
        } else {
            ++it;
        }
    }
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Cleaned up %d contracts with expired storage\n", cleanedContracts);
}

void EnhancedStorage::CleanupLowReputationStorage(uint8_t minReputation, uint64_t currentBlock)
{
    if (!database) {
        return;
    }
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Starting low-reputation storage cleanup (minRep: %d) at block %d\n", 
             minReputation, currentBlock);
    
    size_t cleanedContracts = 0;
    
    // Iterate through storage usage and check reputation
    auto it = storageUsage.begin();
    while (it != storageUsage.end()) {
        const uint160& contractAddr = it->first;
        
        // Get cached reputation for this address
        uint8_t reputation = 0;
        if (GetCachedTrustScore(contractAddr, reputation)) {
            if (reputation < minReputation) {
                LogPrint(BCLog::CVM, "EnhancedStorage: Contract %s has low reputation (%d < %d), marking for cleanup\n",
                         contractAddr.ToString(), reputation, minReputation);
                
                // Clear storage tracking
                storageQuotas.erase(contractAddr);
                storageRentBalances.erase(contractAddr);
                
                it = storageUsage.erase(it);
                cleanedContracts++;
                continue;
            }
        }
        ++it;
    }
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Cleaned up %d low-reputation contracts\n", cleanedContracts);
}

// Storage proofs

std::vector<uint256> EnhancedStorage::GenerateStorageProof(const uint160& contractAddr, 
                                                           const uint256& key)
{
    std::vector<uint256> proof;
    
    if (!database) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Cannot generate proof - no database\n");
        return proof;
    }
    
    // Load the storage value
    uint256 value;
    if (!Load(contractAddr, key, value)) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Cannot generate proof - key not found\n");
        return proof;
    }
    
    // Generate a simple proof structure
    // In a full implementation, this would create a Merkle Patricia Trie proof
    // For now, we create a basic proof with the contract address, key, and value hash
    
    // Proof element 1: Hash of contract address
    uint256 contractHash = Hash(contractAddr.begin(), contractAddr.end());
    proof.push_back(contractHash);
    
    // Proof element 2: Hash of key
    uint256 keyHash = Hash(key.begin(), key.end());
    proof.push_back(keyHash);
    
    // Proof element 3: Hash of value
    uint256 valueHash = Hash(value.begin(), value.end());
    proof.push_back(valueHash);
    
    // Proof element 4: Combined hash for verification
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << contractAddr << key << value;
    uint256 combinedHash = hasher.GetHash();
    proof.push_back(combinedHash);
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Generated storage proof for %s key %s (%d elements)\n", 
             contractAddr.ToString(), key.ToString(), proof.size());
    
    return proof;
}

bool EnhancedStorage::VerifyStorageProof(const std::vector<uint256>& proof, 
                                        const uint256& root,
                                        const uint160& contractAddr, 
                                        const uint256& key, 
                                        const uint256& value)
{
    // Basic proof verification
    // In a full implementation, this would verify a Merkle Patricia Trie proof
    
    if (proof.size() < 4) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Invalid proof - insufficient elements\n");
        return false;
    }
    
    // Verify contract address hash
    uint256 expectedContractHash = Hash(contractAddr.begin(), contractAddr.end());
    if (proof[0] != expectedContractHash) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Proof verification failed - contract hash mismatch\n");
        return false;
    }
    
    // Verify key hash
    uint256 expectedKeyHash = Hash(key.begin(), key.end());
    if (proof[1] != expectedKeyHash) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Proof verification failed - key hash mismatch\n");
        return false;
    }
    
    // Verify value hash
    uint256 expectedValueHash = Hash(value.begin(), value.end());
    if (proof[2] != expectedValueHash) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Proof verification failed - value hash mismatch\n");
        return false;
    }
    
    // Verify combined hash
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << contractAddr << key << value;
    uint256 expectedCombinedHash = hasher.GetHash();
    if (proof[3] != expectedCombinedHash) {
        LogPrint(BCLog::CVM, "EnhancedStorage: Proof verification failed - combined hash mismatch\n");
        return false;
    }
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Storage proof verified successfully for %s\n", 
             contractAddr.ToString());
    
    return true;
}

// Atomic operations

void EnhancedStorage::BeginAtomicOperation()
{
    if (inAtomicOperation) {
        LogPrintf("EnhancedStorage: Warning - nested atomic operation\n");
        return;
    }
    
    inAtomicOperation = true;
    pendingOperations.clear();
    atomicSnapshot.clear();
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Begin atomic operation\n");
}

void EnhancedStorage::CommitAtomicOperation()
{
    if (!inAtomicOperation) {
        LogPrintf("EnhancedStorage: Warning - commit without begin\n");
        return;
    }
    
    // Apply all pending operations
    for (const auto& op : pendingOperations) {
        if (op.type == StorageOperation::Type::STORE) {
            database->Store(op.contractAddr, op.key, op.value);
            storageUsage[op.contractAddr] += 32;
            stats.totalKeys++;
            stats.totalSize += 32;
        }
    }
    
    // Clear atomic state
    inAtomicOperation = false;
    pendingOperations.clear();
    atomicSnapshot.clear();
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Committed atomic operation\n");
}

void EnhancedStorage::RollbackAtomicOperation()
{
    if (!inAtomicOperation) {
        LogPrintf("EnhancedStorage: Warning - rollback without begin\n");
        return;
    }
    
    // Discard all pending operations
    inAtomicOperation = false;
    pendingOperations.clear();
    atomicSnapshot.clear();
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Rolled back atomic operation\n");
}

// Backward compatibility

bool EnhancedStorage::LoadLegacy(const uint160& contractAddr, const uint256& key, uint256& value)
{
    // Legacy CVM storage uses the same format, so just delegate
    return Load(contractAddr, key, value);
}

bool EnhancedStorage::StoreLegacy(const uint160& contractAddr, const uint256& key, const uint256& value)
{
    // Legacy CVM storage uses the same format, so just delegate
    return Store(contractAddr, key, value);
}

// Helper functions

std::string EnhancedStorage::MakeRegionKey(const uint160& contractAddr, const std::string& regionId) const
{
    return contractAddr.ToString() + ":" + regionId;
}

uint64_t EnhancedStorage::CalculateDefaultQuota(uint8_t reputation) const
{
    // Base quota + reputation bonus
    return BASE_STORAGE_QUOTA + (reputation * REPUTATION_QUOTA_MULTIPLIER);
}

bool EnhancedStorage::HasSufficientReputation(const TrustContext& trust, uint8_t required) const
{
    return trust.GetCallerReputation() >= required;
}

std::string EnhancedStorage::MakeStorageKey(const uint160& contractAddr, const uint256& key) const
{
    return contractAddr.ToString() + ":" + key.ToString();
}

std::string EnhancedStorage::MakeTrustTaggedKey(const uint160& contractAddr, 
                                               const std::string& regionId,
                                               const uint256& key) const
{
    return contractAddr.ToString() + ":" + regionId + ":" + key.ToString();
}

// Reputation-weighted storage costs

uint64_t EnhancedStorage::CalculateStorageCost(const uint160& address, uint8_t reputation, bool isWrite)
{
    uint64_t baseCost = GetBaseStorageCost(isWrite);
    double multiplier = GetReputationCostMultiplier(reputation);
    
    uint64_t adjustedCost = static_cast<uint64_t>(baseCost * multiplier);
    
    LogPrint(BCLog::CVM, "EnhancedStorage: Storage cost for %s (rep: %d): %d (base: %d, multiplier: %.2f)\n",
             address.ToString(), reputation, adjustedCost, baseCost, multiplier);
    
    return adjustedCost;
}

uint64_t EnhancedStorage::GetBaseStorageCost(bool isWrite) const
{
    return isWrite ? BASE_STORAGE_WRITE_COST : BASE_STORAGE_READ_COST;
}

double EnhancedStorage::GetReputationCostMultiplier(uint8_t reputation) const
{
    // High reputation gets significant discounts
    if (reputation >= 80) {
        return 0.1; // 90% discount
    } else if (reputation >= 60) {
        return 0.5; // 50% discount
    } else if (reputation >= 40) {
        return 1.0; // No discount
    } else if (reputation >= 20) {
        return 1.5; // 50% penalty
    } else {
        return 2.0; // 100% penalty
    }
}

} // namespace CVM
