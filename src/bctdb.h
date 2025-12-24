// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BCTDB_H
#define BITCOIN_BCTDB_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <memory>

#include <validationinterface.h>

struct sqlite3;
struct sqlite3_stmt;

class CBlock;
class CBlockIndex;
class CTransaction;
class CWallet;

namespace Consensus {
    struct Params;
}

/**
 * BCTRecord - Data structure representing a Bee Creation Transaction record
 */
struct BCTRecord {
    std::string txid;
    std::string honeyAddress;
    std::string status;          // "immature", "mature", "expired"
    int beeCount;
    int creationHeight;
    int maturityHeight;
    int expirationHeight;
    int64_t timestamp;
    int64_t cost;
    int blocksFound;
    int64_t rewardsPaid;
    int64_t profit;
    std::string checksum;        // For integrity validation
    int64_t updatedAt;

    BCTRecord() : beeCount(0), creationHeight(0), maturityHeight(0),
                  expirationHeight(0), timestamp(0), cost(0), blocksFound(0),
                  rewardsPaid(0), profit(0), updatedAt(0) {}

    // Calculate blocks remaining until expiration
    int getBlocksLeft(int currentHeight) const;

    // Update status based on current height
    void updateStatus(int currentHeight);

    // Calculate checksum for integrity validation
    std::string calculateChecksum() const;

    // Validate checksum
    bool validateChecksum() const;

    // Equality operator for testing
    bool operator==(const BCTRecord& other) const;
    bool operator!=(const BCTRecord& other) const { return !(*this == other); }
};

/**
 * BCTSummary - Aggregated statistics for BCT records
 */
struct BCTSummary {
    int immatureCount;
    int matureCount;
    int expiredCount;
    int totalBeeCount;
    int blocksFound;
    int64_t totalCost;
    int64_t totalRewards;
    int64_t totalProfit;

    BCTSummary() : immatureCount(0), matureCount(0), expiredCount(0),
                   totalBeeCount(0), blocksFound(0), totalCost(0),
                   totalRewards(0), totalProfit(0) {}
};

/**
 * BCTDatabaseSQLite - SQLite-based persistent storage for BCT data
 * 
 * This class provides persistent storage for BCT (Bee Creation Transaction) data
 * using SQLite. It supports incremental updates, caching, and efficient queries.
 */
class BCTDatabaseSQLite {
public:
    // Schema version for migrations
    static const int SCHEMA_VERSION = 1;

    // Get singleton instance
    static BCTDatabaseSQLite* instance();

    // Destructor
    ~BCTDatabaseSQLite();

    // Initialization and shutdown
    bool initialize(const std::string& dataDir);
    void shutdown();
    bool isInitialized() const { return db != nullptr; }

    // CRUD operations
    bool insertBCT(const BCTRecord& bct);
    bool updateBCT(const std::string& txid, const BCTRecord& bct);
    bool deleteBCT(const std::string& txid);

    // Query operations
    std::vector<BCTRecord> getAllBCTs(bool includeExpired = false);
    std::vector<BCTRecord> getBCTsByStatus(const std::string& status);
    BCTRecord getBCT(const std::string& txid);
    bool bctExists(const std::string& txid);

    // Statistics
    BCTSummary getSummary();

    // Sync state management
    int getLastProcessedHeight();
    bool setLastProcessedHeight(int height);

    // Migration and initialization
    bool migrateFromJSON(const std::string& jsonPath);
    
    // Startup initialization - loads cache or triggers full scan
    bool initializeOnStartup(CWallet* pwallet);
    
    // Load data into memory cache from database
    bool loadIntoCache();
    
    // Perform initial full scan from wallet
    bool performInitialScan(CWallet* pwallet);
    
    // Check if database file exists
    bool databaseExists() const;
    
    // Get records with invalid checksums (for rescan)
    std::vector<std::string> getInvalidChecksumRecords();
    
    // Validate that all BCTs in database belong to the wallet
    // Returns number of foreign BCTs found (0 = all valid)
    int validateWalletOwnership(CWallet* pwallet);
    
    // Rescan only rewards (not BCTs) - useful for catching missed rewards
    void rescanRewardsOnly(CWallet* pwallet);
    
    // Mark records for rescan
    bool markRecordsForRescan(const std::vector<std::string>& txids);

    // Rescan support
    bool clearAllData();
    int rescanFromHeight(int startHeight, int stopHeight = -1);  // DEPRECATED: Scans all BCTs, not just wallet BCTs
    int rescanFromWallet(CWallet* pwallet, int startHeight = 0, int stopHeight = -1);  // Recommended: Only scans wallet BCTs
    int getBCTCount();  // Get total count of BCT records

    // Reward tracking
    bool insertReward(const std::string& coinbaseTxid, const std::string& bctTxid, 
                      int64_t amount, int height);
    int64_t getTotalRewardsForBCT(const std::string& bctTxid);
    bool deleteRewardsAfterHeight(int height);

    // Block processing for incremental updates
    void processBlock(const CBlock& block, const CBlockIndex* pindex, CWallet* pwallet);
    void handleReorg(int forkHeight);
    
    // Update all BCT statuses based on current height
    void updateAllStatuses(int currentHeight);
    
    // Delete BCTs created after a specific height (for reorg handling)
    bool deleteBCTsAfterHeight(int height);

    // Get database path
    const std::string& getDatabasePath() const { return dbPath; }

private:
    // Private constructor for singleton
    BCTDatabaseSQLite();

    // Prevent copying
    BCTDatabaseSQLite(const BCTDatabaseSQLite&) = delete;
    BCTDatabaseSQLite& operator=(const BCTDatabaseSQLite&) = delete;

    // Database handle
    sqlite3* db = nullptr;
    std::string dbPath;
    mutable std::mutex dbMutex;

    // In-memory cache
    std::map<std::string, BCTRecord> cache;
    bool cacheValid = false;

    // Schema management
    bool createSchema();
    bool upgradeSchema(int fromVersion, int toVersion);
    int getSchemaVersion();
    bool setSchemaVersion(int version);
    
    // Checksum validation
    bool validateAllChecksums();
    bool updateRecordChecksum(const std::string& txid);

    // Helper methods
    bool executeSQL(const std::string& sql);
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Prepared statement helpers
    BCTRecord recordFromStatement(sqlite3_stmt* stmt);
    bool bindRecordToStatement(sqlite3_stmt* stmt, const BCTRecord& record);

    // Cache management
    void invalidateCache();
    void refreshCache();
};

/**
 * BCTBlockHandler - ValidationInterface subscriber for block events
 * 
 * This class subscribes to blockchain events and triggers incremental
 * updates to the BCT database when blocks are connected or disconnected.
 */
class BCTBlockHandler : public CValidationInterface {
public:
    BCTBlockHandler();
    ~BCTBlockHandler();

    // Register/unregister with validation interface
    void RegisterValidationInterface();
    void UnregisterValidationInterface();

protected:
    // CValidationInterface callbacks
    void BlockConnected(const std::shared_ptr<const CBlock>& block, 
                       const CBlockIndex* pindex,
                       const std::vector<CTransactionRef>& txnConflicted) override;
    
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block) override;

private:
    bool fRegistered;
};

// Global BCT block handler instance
inline std::unique_ptr<BCTBlockHandler> g_bct_block_handler;

// Initialize BCT block handler (call during startup)
void InitBCTBlockHandler();

// Shutdown BCT block handler (call during shutdown)
void ShutdownBCTBlockHandler();

#endif // BITCOIN_BCTDB_H
