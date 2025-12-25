// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cleanup_manager.h>
#include <cvm/cvmdb.h>
#include <cvm/enhanced_storage.h>
#include <cvm/reputation.h>
#include <util.h>

namespace CVM {

// Cleanup thresholds
static const uint8_t MIN_REPUTATION_THRESHOLD = 30;  // Contracts below this may be cleaned
static const uint64_t LOW_REP_INACTIVITY_BLOCKS = 1000;  // ~1.7 days for low rep
static const uint64_t NORMAL_REP_INACTIVITY_BLOCKS = 10000;  // ~17 days for normal rep
static const uint64_t HIGH_REP_INACTIVITY_BLOCKS = 100000;  // ~170 days for high rep
static const uint64_t DEFAULT_CLEANUP_INTERVAL = 1000;  // Run cleanup every ~1.7 days

CleanupManager::CleanupManager()
    : m_db(nullptr)
    , m_storage(nullptr)
    , m_cleanupInterval(DEFAULT_CLEANUP_INTERVAL)
    , m_lastCleanupBlock(0)
{
}

CleanupManager::~CleanupManager()
{
}

void CleanupManager::Initialize(CVMDatabase* db, EnhancedStorage* storage)
{
    m_db = db;
    m_storage = storage;
}

// ===== Contract Cleanup =====

bool CleanupManager::MarkLowReputationContract(
    const uint160& contractAddr,
    const uint160& deployer,
    uint64_t deploymentBlock)
{
    std::lock_guard<std::mutex> lock(m_contractsMutex);
    
    uint8_t reputation = GetReputation(deployer);
    
    // Mark for cleanup if reputation is below threshold
    if (reputation < MIN_REPUTATION_THRESHOLD) {
        ContractCleanupInfo info;
        info.contractAddr = contractAddr;
        info.deployer = deployer;
        info.deployerReputation = reputation;
        info.deploymentBlock = deploymentBlock;
        info.lastAccessBlock = deploymentBlock;
        info.storageSize = 0;
        info.isMarkedForCleanup = true;
        
        m_contracts[contractAddr] = info;
        
        LogPrintf("CVM: Contract %s marked for cleanup (deployer reputation: %d)\n",
                  contractAddr.ToString(), reputation);
        
        return true;
    }
    
    return false;
}

bool CleanupManager::ShouldCleanupContract(
    const uint160& contractAddr,
    uint64_t currentBlock)
{
    std::lock_guard<std::mutex> lock(m_contractsMutex);
    
    auto it = m_contracts.find(contractAddr);
    if (it == m_contracts.end()) {
        return false;
    }
    
    const ContractCleanupInfo& info = it->second;
    
    // Check if marked for cleanup
    if (info.isMarkedForCleanup) {
        return true;
    }
    
    // Check inactivity threshold based on deployer reputation
    uint64_t inactivityThreshold = GetInactivityThreshold(info.deployerReputation);
    uint64_t inactiveBlocks = currentBlock - info.lastAccessBlock;
    
    if (inactiveBlocks >= inactivityThreshold) {
        LogPrintf("CVM: Contract %s inactive for %d blocks (threshold: %d)\n",
                  contractAddr.ToString(), inactiveBlocks, inactivityThreshold);
        return true;
    }
    
    return false;
}

bool CleanupManager::CleanupContract(
    const uint160& contractAddr,
    uint64_t currentBlock)
{
    if (!m_storage) {
        return false;
    }
    
    // Reclaim resources
    uint64_t bytesReclaimed = ReclaimResources(contractAddr);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.totalContractsCleaned++;
        m_stats.totalBytesReclaimed += bytesReclaimed;
        
        // Determine cleanup reason
        std::lock_guard<std::mutex> contractLock(m_contractsMutex);
        auto it = m_contracts.find(contractAddr);
        if (it != m_contracts.end()) {
            if (it->second.deployerReputation < MIN_REPUTATION_THRESHOLD) {
                m_stats.lowReputationCleanups++;
            } else {
                m_stats.inactiveCleanups++;
            }
        }
    }
    
    // Remove from tracking
    {
        std::lock_guard<std::mutex> lock(m_contractsMutex);
        m_contracts.erase(contractAddr);
    }
    
    LogPrintf("CVM: Cleaned up contract %s, reclaimed %d bytes\n",
              contractAddr.ToString(), bytesReclaimed);
    
    return true;
}

uint8_t CleanupManager::GetMinReputationThreshold()
{
    return MIN_REPUTATION_THRESHOLD;
}

uint64_t CleanupManager::GetInactivityThreshold(uint8_t deployerReputation)
{
    // Inactivity thresholds based on deployer reputation:
    // 0-49: 1000 blocks (~1.7 days)
    // 50-69: 10000 blocks (~17 days)
    // 70+: 100000 blocks (~170 days)
    
    if (deployerReputation >= 70) {
        return HIGH_REP_INACTIVITY_BLOCKS;
    } else if (deployerReputation >= 50) {
        return NORMAL_REP_INACTIVITY_BLOCKS;
    } else {
        return LOW_REP_INACTIVITY_BLOCKS;
    }
}

// ===== Storage Cleanup =====

uint64_t CleanupManager::CleanupExpiredStorage(uint64_t currentBlock)
{
    if (!m_storage) {
        return 0;
    }
    
    // Delegate to EnhancedStorage
    m_storage->CleanupExpiredStorage(currentBlock);
    
    // Update statistics
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.totalStorageCleaned++;
    
    return 0;  // EnhancedStorage doesn't return count
}

uint64_t CleanupManager::CleanupLowReputationStorage(
    uint8_t minReputation,
    uint64_t currentBlock)
{
    if (!m_storage) {
        return 0;
    }
    
    // Delegate to EnhancedStorage
    m_storage->CleanupLowReputationStorage(minReputation, currentBlock);
    
    // Update statistics
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.totalStorageCleaned++;
    m_stats.lowReputationCleanups++;
    
    return 0;  // EnhancedStorage doesn't return count
}

uint64_t CleanupManager::CleanupInactiveStorage(
    uint64_t inactivityBlocks,
    uint64_t currentBlock)
{
    if (!m_storage) {
        return 0;
    }
    
    uint64_t cleaned = 0;
    
    // Check all tracked contracts for inactivity
    std::vector<uint160> inactiveContracts;
    {
        std::lock_guard<std::mutex> lock(m_contractsMutex);
        
        for (const auto& pair : m_contracts) {
            const ContractCleanupInfo& info = pair.second;
            uint64_t inactiveFor = currentBlock - info.lastAccessBlock;
            
            if (inactiveFor >= inactivityBlocks) {
                inactiveContracts.push_back(pair.first);
            }
        }
    }
    
    // Cleanup inactive contracts
    for (const uint160& contractAddr : inactiveContracts) {
        if (CleanupContract(contractAddr, currentBlock)) {
            cleaned++;
        }
    }
    
    // Update statistics
    if (cleaned > 0) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.inactiveCleanups += cleaned;
    }
    
    return cleaned;
}

// ===== Garbage Collection =====

CleanupManager::CleanupStats CleanupManager::RunGarbageCollection(uint64_t currentBlock)
{
    LogPrintf("CVM: Running garbage collection at block %d\n", currentBlock);
    
    // 1. Cleanup expired storage
    CleanupExpiredStorage(currentBlock);
    
    // 2. Cleanup low-reputation storage
    CleanupLowReputationStorage(MIN_REPUTATION_THRESHOLD, currentBlock);
    
    // 3. Cleanup inactive contracts
    std::vector<uint160> contractsToCleanup;
    {
        std::lock_guard<std::mutex> lock(m_contractsMutex);
        
        for (const auto& pair : m_contracts) {
            if (ShouldCleanupContract(pair.first, currentBlock)) {
                contractsToCleanup.push_back(pair.first);
            }
        }
    }
    
    for (const uint160& contractAddr : contractsToCleanup) {
        CleanupContract(contractAddr, currentBlock);
    }
    
    // 4. Cleanup trust cache
    if (m_storage) {
        m_storage->CleanupTrustCache(86400);  // 24 hours
    }
    
    // Update last cleanup block
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.lastCleanupBlock = currentBlock;
    }
    m_lastCleanupBlock = currentBlock;
    
    LogPrintf("CVM: Garbage collection complete. Cleaned %d contracts, reclaimed %d bytes\n",
              m_stats.totalContractsCleaned, m_stats.totalBytesReclaimed);
    
    return GetStats();
}

void CleanupManager::SchedulePeriodicCleanup(uint64_t intervalBlocks)
{
    m_cleanupInterval = intervalBlocks;
    LogPrintf("CVM: Scheduled periodic cleanup every %d blocks\n", intervalBlocks);
}

bool CleanupManager::IsCleanupDue(uint64_t currentBlock)
{
    if (m_lastCleanupBlock == 0) {
        return true;  // First cleanup
    }
    
    return (currentBlock - m_lastCleanupBlock) >= m_cleanupInterval;
}

bool CleanupManager::RunPeriodicCleanup(uint64_t currentBlock)
{
    if (!IsCleanupDue(currentBlock)) {
        return false;
    }
    
    RunGarbageCollection(currentBlock);
    return true;
}

// ===== Contract Tracking =====

void CleanupManager::TrackContractDeployment(
    const uint160& contractAddr,
    const uint160& deployer,
    uint64_t deploymentBlock)
{
    std::lock_guard<std::mutex> lock(m_contractsMutex);
    
    ContractCleanupInfo info;
    info.contractAddr = contractAddr;
    info.deployer = deployer;
    info.deployerReputation = GetReputation(deployer);
    info.deploymentBlock = deploymentBlock;
    info.lastAccessBlock = deploymentBlock;
    info.storageSize = 0;
    info.isMarkedForCleanup = false;
    
    m_contracts[contractAddr] = info;
    
    // Mark for cleanup if low reputation
    if (info.deployerReputation < MIN_REPUTATION_THRESHOLD) {
        info.isMarkedForCleanup = true;
        m_contracts[contractAddr] = info;
        
        LogPrintf("CVM: Contract %s deployed by low-reputation address (rep: %d)\n",
                  contractAddr.ToString(), info.deployerReputation);
    }
}

void CleanupManager::UpdateContractAccess(
    const uint160& contractAddr,
    uint64_t accessBlock)
{
    std::lock_guard<std::mutex> lock(m_contractsMutex);
    
    auto it = m_contracts.find(contractAddr);
    if (it != m_contracts.end()) {
        it->second.lastAccessBlock = accessBlock;
    }
}

CleanupManager::ContractCleanupInfo CleanupManager::GetContractInfo(const uint160& contractAddr)
{
    std::lock_guard<std::mutex> lock(m_contractsMutex);
    
    auto it = m_contracts.find(contractAddr);
    if (it != m_contracts.end()) {
        return it->second;
    }
    
    return ContractCleanupInfo();
}

std::set<uint160> CleanupManager::GetMarkedContracts() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_contractsMutex));
    
    std::set<uint160> marked;
    for (const auto& pair : m_contracts) {
        if (pair.second.isMarkedForCleanup) {
            marked.insert(pair.first);
        }
    }
    
    return marked;
}

// ===== Private Methods =====

uint8_t CleanupManager::GetReputation(const uint160& address)
{
    if (!m_db) {
        return 0;
    }
    
    // Read reputation from generic storage
    std::string key = "reputation_" + address.ToString();
    std::vector<uint8_t> data;
    
    if (m_db->ReadGeneric(key, data) && data.size() >= sizeof(int64_t)) {
        // Deserialize reputation score
        int64_t score = 0;
        memcpy(&score, data.data(), sizeof(int64_t));
        
        // Convert to 0-100 scale (assuming score is -10000 to +10000)
        // Map: -10000 -> 0, 0 -> 50, +10000 -> 100
        int normalized = 50 + (score / 200);
        if (normalized < 0) normalized = 0;
        if (normalized > 100) normalized = 100;
        
        return static_cast<uint8_t>(normalized);
    }
    
    return 50;  // Default neutral reputation
}

uint64_t CleanupManager::CalculateStorageSize(const uint160& contractAddr)
{
    if (!m_storage) {
        return 0;
    }
    
    return m_storage->GetCurrentStorageUsage(contractAddr);
}

uint64_t CleanupManager::ReclaimResources(const uint160& contractAddr)
{
    if (!m_storage) {
        return 0;
    }
    
    // Calculate storage size before cleanup
    uint64_t storageSize = CalculateStorageSize(contractAddr);
    
    // TODO: Implement actual resource reclamation
    // This would involve:
    // 1. Deleting all storage entries for the contract
    // 2. Removing contract metadata
    // 3. Cleaning up any associated resources
    
    // For now, just return the calculated size
    return storageSize;
}

} // namespace CVM
