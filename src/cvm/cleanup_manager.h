// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CLEANUP_MANAGER_H
#define CASCOIN_CVM_CLEANUP_MANAGER_H

#include <uint256.h>
#include <cvm/trust_context.h>
#include <map>
#include <set>
#include <memory>
#include <mutex>

namespace CVM {

class CVMDatabase;
class EnhancedStorage;

/**
 * Cleanup Manager
 * 
 * Manages automatic resource cleanup for the CVM system:
 * - Cleanup for low-reputation contract deployments
 * - Resource reclamation based on reputation thresholds
 * - Automatic storage cleanup for inactive contracts
 * - Reputation-based garbage collection
 * - Periodic cleanup scheduling
 */
class CleanupManager {
public:
    /**
     * Contract cleanup information
     */
    struct ContractCleanupInfo {
        uint160 contractAddr;
        uint160 deployer;
        uint8_t deployerReputation;
        uint64_t deploymentBlock;
        uint64_t lastAccessBlock;
        uint64_t storageSize;
        bool isMarkedForCleanup;
        
        ContractCleanupInfo()
            : deployerReputation(0), deploymentBlock(0), 
              lastAccessBlock(0), storageSize(0), isMarkedForCleanup(false) {}
    };
    
    /**
     * Cleanup statistics
     */
    struct CleanupStats {
        uint64_t totalContractsCleaned;
        uint64_t totalStorageCleaned;
        uint64_t totalBytesReclaimed;
        uint64_t lastCleanupBlock;
        uint64_t lowReputationCleanups;
        uint64_t inactiveCleanups;
        
        CleanupStats()
            : totalContractsCleaned(0), totalStorageCleaned(0), 
              totalBytesReclaimed(0), lastCleanupBlock(0),
              lowReputationCleanups(0), inactiveCleanups(0) {}
    };
    
    CleanupManager();
    ~CleanupManager();
    
    /**
     * Initialize cleanup manager
     * 
     * @param db CVM database
     * @param storage Enhanced storage
     */
    void Initialize(CVMDatabase* db, EnhancedStorage* storage);
    
    // ===== Contract Cleanup =====
    
    /**
     * Mark contract for cleanup based on deployer reputation
     * 
     * @param contractAddr Contract address
     * @param deployer Deployer address
     * @param deploymentBlock Block height of deployment
     * @return true if marked for cleanup
     */
    bool MarkLowReputationContract(
        const uint160& contractAddr,
        const uint160& deployer,
        uint64_t deploymentBlock
    );
    
    /**
     * Check if contract should be cleaned up
     * 
     * @param contractAddr Contract address
     * @param currentBlock Current block height
     * @return true if should be cleaned up
     */
    bool ShouldCleanupContract(
        const uint160& contractAddr,
        uint64_t currentBlock
    );
    
    /**
     * Cleanup contract and reclaim resources
     * 
     * @param contractAddr Contract address
     * @param currentBlock Current block height
     * @return true if cleaned up successfully
     */
    bool CleanupContract(
        const uint160& contractAddr,
        uint64_t currentBlock
    );
    
    /**
     * Get minimum reputation threshold for contract retention
     * 
     * @return Minimum reputation (default: 30)
     */
    static uint8_t GetMinReputationThreshold();
    
    /**
     * Get inactivity period before cleanup (in blocks)
     * 
     * @param deployerReputation Deployer's reputation
     * @return Blocks of inactivity before cleanup
     */
    static uint64_t GetInactivityThreshold(uint8_t deployerReputation);
    
    // ===== Storage Cleanup =====
    
    /**
     * Cleanup expired storage for all contracts
     * 
     * @param currentBlock Current block height
     * @return Number of storage entries cleaned
     */
    uint64_t CleanupExpiredStorage(uint64_t currentBlock);
    
    /**
     * Cleanup storage for low-reputation contracts
     * 
     * @param minReputation Minimum reputation threshold
     * @param currentBlock Current block height
     * @return Number of storage entries cleaned
     */
    uint64_t CleanupLowReputationStorage(
        uint8_t minReputation,
        uint64_t currentBlock
    );
    
    /**
     * Cleanup inactive contract storage
     * 
     * @param inactivityBlocks Blocks of inactivity
     * @param currentBlock Current block height
     * @return Number of storage entries cleaned
     */
    uint64_t CleanupInactiveStorage(
        uint64_t inactivityBlocks,
        uint64_t currentBlock
    );
    
    // ===== Garbage Collection =====
    
    /**
     * Run reputation-based garbage collection
     * 
     * @param currentBlock Current block height
     * @return Cleanup statistics
     */
    CleanupStats RunGarbageCollection(uint64_t currentBlock);
    
    /**
     * Schedule periodic cleanup
     * 
     * @param intervalBlocks Blocks between cleanups
     */
    void SchedulePeriodicCleanup(uint64_t intervalBlocks);
    
    /**
     * Check if periodic cleanup is due
     * 
     * @param currentBlock Current block height
     * @return true if cleanup should run
     */
    bool IsCleanupDue(uint64_t currentBlock);
    
    /**
     * Run periodic cleanup if due
     * 
     * @param currentBlock Current block height
     * @return true if cleanup was performed
     */
    bool RunPeriodicCleanup(uint64_t currentBlock);
    
    // ===== Contract Tracking =====
    
    /**
     * Track contract deployment
     * 
     * @param contractAddr Contract address
     * @param deployer Deployer address
     * @param deploymentBlock Block height
     */
    void TrackContractDeployment(
        const uint160& contractAddr,
        const uint160& deployer,
        uint64_t deploymentBlock
    );
    
    /**
     * Update contract access time
     * 
     * @param contractAddr Contract address
     * @param accessBlock Block height of access
     */
    void UpdateContractAccess(
        const uint160& contractAddr,
        uint64_t accessBlock
    );
    
    /**
     * Get contract cleanup info
     * 
     * @param contractAddr Contract address
     * @return Cleanup information
     */
    ContractCleanupInfo GetContractInfo(const uint160& contractAddr);
    
    // ===== Statistics =====
    
    /**
     * Get cleanup statistics
     * 
     * @return Cleanup statistics
     */
    CleanupStats GetStats() const { return m_stats; }
    
    /**
     * Reset cleanup statistics
     */
    void ResetStats() { m_stats = CleanupStats(); }
    
    /**
     * Get contracts marked for cleanup
     * 
     * @return Set of contract addresses
     */
    std::set<uint160> GetMarkedContracts() const;
    
private:
    CVMDatabase* m_db;
    EnhancedStorage* m_storage;
    
    // Contract tracking
    std::map<uint160, ContractCleanupInfo> m_contracts;
    std::mutex m_contractsMutex;
    
    // Cleanup scheduling
    uint64_t m_cleanupInterval;  // Blocks between cleanups
    uint64_t m_lastCleanupBlock;
    
    // Statistics
    CleanupStats m_stats;
    std::mutex m_statsMutex;
    
    /**
     * Get reputation score for address
     * 
     * @param address Address to check
     * @return Reputation score (0-100)
     */
    uint8_t GetReputation(const uint160& address);
    
    /**
     * Calculate storage size for contract
     * 
     * @param contractAddr Contract address
     * @return Storage size in bytes
     */
    uint64_t CalculateStorageSize(const uint160& contractAddr);
    
    /**
     * Reclaim contract resources
     * 
     * @param contractAddr Contract address
     * @return Bytes reclaimed
     */
    uint64_t ReclaimResources(const uint160& contractAddr);
};

} // namespace CVM

#endif // CASCOIN_CVM_CLEANUP_MANAGER_H
