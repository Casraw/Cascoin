// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bctdb.h"
#include "util.h"
#include "hash.h"
#include "utilstrencodings.h"
#include "primitives/block.h"
#include "chain.h"
#include "chainparams.h"
#include "validation.h"
#include "base58.h"
#include "script/standard.h"
#include "consensus/params.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <sqlite3.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <cstdio>

// Global BCT block handler instance is defined inline in bctdb.h

// Static member definition
const int BCTDatabaseSQLite::SCHEMA_VERSION;

// SQL column list for BCT queries (used in multiple places to avoid duplication)
static const char* BCT_SELECT_COLUMNS = 
    "txid, honey_address, status, bee_count, creation_height, "
    "maturity_height, expiration_height, timestamp, cost, blocks_found, "
    "rewards_paid, profit, checksum, updated_at";

// BCTRecord implementation

int BCTRecord::getBlocksLeft(int currentHeight) const {
    if (currentHeight >= expirationHeight) return 0;
    return expirationHeight - currentHeight;
}

void BCTRecord::updateStatus(int currentHeight) {
    // Don't update status if heights are not set (e.g., from JSON migration)
    // Heights of 0 indicate the record needs a rescan to get proper values
    if (creationHeight == 0 || maturityHeight == 0 || expirationHeight == 0) {
        // Keep existing status if heights are unknown
        return;
    }
    
    if (currentHeight >= expirationHeight) {
        status = "expired";
    } else if (currentHeight >= maturityHeight) {
        status = "mature";
    } else {
        status = "immature";
    }
}

std::string BCTRecord::calculateChecksum() const {
    // Note: checksum is calculated from immutable fields only
    // updatedAt is excluded as it changes on every update
    std::stringstream ss;
    ss << txid << honeyAddress << beeCount << creationHeight
       << maturityHeight << expirationHeight << timestamp << cost;
    
    std::string data = ss.str();
    uint256 hash = Hash(data.begin(), data.end());
    return hash.ToString().substr(0, 16);
}

bool BCTRecord::validateChecksum() const {
    return checksum == calculateChecksum();
}

bool BCTRecord::operator==(const BCTRecord& other) const {
    return txid == other.txid &&
           honeyAddress == other.honeyAddress &&
           status == other.status &&
           beeCount == other.beeCount &&
           creationHeight == other.creationHeight &&
           maturityHeight == other.maturityHeight &&
           expirationHeight == other.expirationHeight &&
           timestamp == other.timestamp &&
           cost == other.cost &&
           blocksFound == other.blocksFound &&
           rewardsPaid == other.rewardsPaid &&
           profit == other.profit;
}

// BCTDatabaseSQLite implementation

BCTDatabaseSQLite::BCTDatabaseSQLite() {}

BCTDatabaseSQLite::~BCTDatabaseSQLite() {
    shutdown();
}

BCTDatabaseSQLite* BCTDatabaseSQLite::instance() {
    static BCTDatabaseSQLite instance;
    return &instance;
}

bool BCTDatabaseSQLite::initialize(const std::string& dataDir) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db != nullptr) {
        return true; // Already initialized
    }

    dbPath = dataDir + "/bct_database.sqlite";
    
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        LogPrintf("BCTDatabase: Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrency
    executeSQL("PRAGMA journal_mode=WAL;");
    executeSQL("PRAGMA synchronous=NORMAL;");
    executeSQL("PRAGMA foreign_keys=ON;");

    // Create or upgrade schema
    int currentVersion = getSchemaVersion();
    if (currentVersion < 0) {
        // New database, create schema
        if (!createSchema()) {
            LogPrintf("BCTDatabase: Failed to create schema\n");
            sqlite3_close(db);
            db = nullptr;
            return false;
        }
    } else if (currentVersion < SCHEMA_VERSION) {
        // Upgrade needed
        if (!upgradeSchema(currentVersion, SCHEMA_VERSION)) {
            LogPrintf("BCTDatabase: Failed to upgrade schema from %d to %d\n", 
                     currentVersion, SCHEMA_VERSION);
            sqlite3_close(db);
            db = nullptr;
            return false;
        }
    }

    LogPrintf("BCTDatabase: Initialized at %s\n", dbPath);
    return true;
}

void BCTDatabaseSQLite::shutdown() {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
        LogPrintf("BCTDatabase: Shutdown complete\n");
    }
    
    cache.clear();
    cacheValid = false;
}

bool BCTDatabaseSQLite::createSchema() {
    const char* schema = R"(
        -- Version tracking
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY,
            applied_at INTEGER NOT NULL
        );

        -- BCT records
        CREATE TABLE IF NOT EXISTS bcts (
            txid TEXT PRIMARY KEY,
            honey_address TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'immature',
            bee_count INTEGER NOT NULL,
            creation_height INTEGER NOT NULL,
            maturity_height INTEGER NOT NULL,
            expiration_height INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            cost INTEGER NOT NULL,
            blocks_found INTEGER DEFAULT 0,
            rewards_paid INTEGER DEFAULT 0,
            profit INTEGER DEFAULT 0,
            checksum TEXT,
            updated_at INTEGER NOT NULL
        );

        -- Indexes for fast queries
        CREATE INDEX IF NOT EXISTS idx_bcts_status ON bcts(status);
        CREATE INDEX IF NOT EXISTS idx_bcts_creation_height ON bcts(creation_height);
        CREATE INDEX IF NOT EXISTS idx_bcts_honey_address ON bcts(honey_address);

        -- Metadata for sync status
        CREATE TABLE IF NOT EXISTS sync_state (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        -- Rewards tracking (for incremental updates)
        CREATE TABLE IF NOT EXISTS rewards (
            coinbase_txid TEXT PRIMARY KEY,
            bct_txid TEXT NOT NULL,
            amount INTEGER NOT NULL,
            height INTEGER NOT NULL,
            FOREIGN KEY (bct_txid) REFERENCES bcts(txid) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_rewards_bct ON rewards(bct_txid);
        CREATE INDEX IF NOT EXISTS idx_rewards_height ON rewards(height);
    )";

    if (!executeSQL(schema)) {
        return false;
    }

    return setSchemaVersion(SCHEMA_VERSION);
}

bool BCTDatabaseSQLite::upgradeSchema(int fromVersion, int toVersion) {
    LogPrintf("BCTDatabase: Upgrading schema from version %d to %d\n", fromVersion, toVersion);
    
    // Execute migrations sequentially
    for (int version = fromVersion + 1; version <= toVersion; version++) {
        LogPrintf("BCTDatabase: Applying migration to version %d\n", version);
        
        bool success = false;
        
        switch (version) {
            case 1:
                // Version 1 is the initial schema - should be created by createSchema()
                // This case handles upgrading from version 0 (pre-versioned database)
                success = true;
                break;
                
            // Future migrations go here:
            // case 2:
            //     success = migrateToVersion2();
            //     break;
            // case 3:
            //     success = migrateToVersion3();
            //     break;
            
            default:
                LogPrintf("BCTDatabase: Unknown migration version %d\n", version);
                success = false;
                break;
        }
        
        if (!success) {
            LogPrintf("BCTDatabase: Migration to version %d failed\n", version);
            return false;
        }
        
        // Update schema version after each successful migration
        if (!setSchemaVersion(version)) {
            LogPrintf("BCTDatabase: Failed to update schema version to %d\n", version);
            return false;
        }
        
        LogPrintf("BCTDatabase: Successfully migrated to version %d\n", version);
    }
    
    return true;
}

int BCTDatabaseSQLite::getSchemaVersion() {
    if (db == nullptr) return -1;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT version FROM schema_version ORDER BY version DESC LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    int version = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return version;
}

bool BCTDatabaseSQLite::setSchemaVersion(int version) {
    std::stringstream ss;
    ss << "INSERT OR REPLACE INTO schema_version (version, applied_at) VALUES ("
       << version << ", " << std::time(nullptr) << ");";
    return executeSQL(ss.str());
}

bool BCTDatabaseSQLite::executeSQL(const std::string& sql) {
    if (db == nullptr) return false;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        LogPrintf("BCTDatabase: SQL error: %s\n", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool BCTDatabaseSQLite::beginTransaction() {
    return executeSQL("BEGIN TRANSACTION;");
}

bool BCTDatabaseSQLite::commitTransaction() {
    return executeSQL("COMMIT;");
}

bool BCTDatabaseSQLite::rollbackTransaction() {
    return executeSQL("ROLLBACK;");
}


// CRUD Operations

bool BCTDatabaseSQLite::insertBCT(const BCTRecord& bct) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = R"(
        INSERT INTO bcts (txid, honey_address, status, bee_count, creation_height,
                         maturity_height, expiration_height, timestamp, cost,
                         blocks_found, rewards_paid, profit, checksum, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LogPrintf("BCTDatabase: Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    BCTRecord record = bct;
    record.checksum = record.calculateChecksum();
    record.updatedAt = std::time(nullptr);

    sqlite3_bind_text(stmt, 1, record.txid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.honeyAddress.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, record.beeCount);
    sqlite3_bind_int(stmt, 5, record.creationHeight);
    sqlite3_bind_int(stmt, 6, record.maturityHeight);
    sqlite3_bind_int(stmt, 7, record.expirationHeight);
    sqlite3_bind_int64(stmt, 8, record.timestamp);
    sqlite3_bind_int64(stmt, 9, record.cost);
    sqlite3_bind_int(stmt, 10, record.blocksFound);
    sqlite3_bind_int64(stmt, 11, record.rewardsPaid);
    sqlite3_bind_int64(stmt, 12, record.profit);
    sqlite3_bind_text(stmt, 13, record.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 14, record.updatedAt);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LogPrintf("BCTDatabase: Failed to insert BCT %s: %s\n", bct.txid, sqlite3_errmsg(db));
        return false;
    }

    invalidateCache();
    return true;
}

bool BCTDatabaseSQLite::updateBCT(const std::string& txid, const BCTRecord& bct) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = R"(
        UPDATE bcts SET 
            honey_address = ?, status = ?, bee_count = ?, creation_height = ?,
            maturity_height = ?, expiration_height = ?, timestamp = ?, cost = ?,
            blocks_found = ?, rewards_paid = ?, profit = ?, checksum = ?, updated_at = ?
        WHERE txid = ?;
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LogPrintf("BCTDatabase: Failed to prepare update statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    BCTRecord record = bct;
    record.checksum = record.calculateChecksum();
    record.updatedAt = std::time(nullptr);

    sqlite3_bind_text(stmt, 1, record.honeyAddress.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, record.beeCount);
    sqlite3_bind_int(stmt, 4, record.creationHeight);
    sqlite3_bind_int(stmt, 5, record.maturityHeight);
    sqlite3_bind_int(stmt, 6, record.expirationHeight);
    sqlite3_bind_int64(stmt, 7, record.timestamp);
    sqlite3_bind_int64(stmt, 8, record.cost);
    sqlite3_bind_int(stmt, 9, record.blocksFound);
    sqlite3_bind_int64(stmt, 10, record.rewardsPaid);
    sqlite3_bind_int64(stmt, 11, record.profit);
    sqlite3_bind_text(stmt, 12, record.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 13, record.updatedAt);
    sqlite3_bind_text(stmt, 14, txid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LogPrintf("BCTDatabase: Failed to update BCT %s: %s\n", txid, sqlite3_errmsg(db));
        return false;
    }

    invalidateCache();
    return true;
}

bool BCTDatabaseSQLite::deleteBCT(const std::string& txid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = "DELETE FROM bcts WHERE txid = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, txid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return false;
    }

    invalidateCache();
    return true;
}

// Query Operations

BCTRecord BCTDatabaseSQLite::recordFromStatement(sqlite3_stmt* stmt) {
    BCTRecord record;
    
    record.txid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    record.honeyAddress = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    record.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.beeCount = sqlite3_column_int(stmt, 3);
    record.creationHeight = sqlite3_column_int(stmt, 4);
    record.maturityHeight = sqlite3_column_int(stmt, 5);
    record.expirationHeight = sqlite3_column_int(stmt, 6);
    record.timestamp = sqlite3_column_int64(stmt, 7);
    record.cost = sqlite3_column_int64(stmt, 8);
    record.blocksFound = sqlite3_column_int(stmt, 9);
    record.rewardsPaid = sqlite3_column_int64(stmt, 10);
    record.profit = sqlite3_column_int64(stmt, 11);
    
    const char* checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    if (checksum) record.checksum = checksum;
    
    record.updatedAt = sqlite3_column_int64(stmt, 13);
    
    return record;
}

std::vector<BCTRecord> BCTDatabaseSQLite::getAllBCTs(bool includeExpired) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    std::vector<BCTRecord> results;
    if (db == nullptr) return results;

    std::string sql = std::string("SELECT ") + BCT_SELECT_COLUMNS + " FROM bcts";
    
    if (!includeExpired) {
        sql += " WHERE status != 'expired'";
    }
    
    sql += " ORDER BY creation_height DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(recordFromStatement(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<BCTRecord> BCTDatabaseSQLite::getBCTsByStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    std::vector<BCTRecord> results;
    if (db == nullptr) return results;

    std::string sql = std::string("SELECT ") + BCT_SELECT_COLUMNS + 
                      " FROM bcts WHERE status = ? ORDER BY creation_height DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(recordFromStatement(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

BCTRecord BCTDatabaseSQLite::getBCT(const std::string& txid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    BCTRecord record;
    if (db == nullptr) return record;

    std::string sql = std::string("SELECT ") + BCT_SELECT_COLUMNS + " FROM bcts WHERE txid = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return record;
    }

    sqlite3_bind_text(stmt, 1, txid.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        record = recordFromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return record;
}

bool BCTDatabaseSQLite::bctExists(const std::string& txid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = "SELECT 1 FROM bcts WHERE txid = ? LIMIT 1;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, txid.c_str(), -1, SQLITE_TRANSIENT);

    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

BCTSummary BCTDatabaseSQLite::getSummary() {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    BCTSummary summary;
    if (db == nullptr) return summary;

    const char* sql = R"(
        SELECT 
            SUM(CASE WHEN status = 'immature' THEN 1 ELSE 0 END) as immature_count,
            SUM(CASE WHEN status = 'mature' THEN 1 ELSE 0 END) as mature_count,
            SUM(CASE WHEN status = 'expired' THEN 1 ELSE 0 END) as expired_count,
            SUM(bee_count) as total_bees,
            SUM(blocks_found) as total_blocks,
            SUM(cost) as total_cost,
            SUM(rewards_paid) as total_rewards,
            SUM(profit) as total_profit
        FROM bcts;
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return summary;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        summary.immatureCount = sqlite3_column_int(stmt, 0);
        summary.matureCount = sqlite3_column_int(stmt, 1);
        summary.expiredCount = sqlite3_column_int(stmt, 2);
        summary.totalBeeCount = sqlite3_column_int(stmt, 3);
        summary.blocksFound = sqlite3_column_int(stmt, 4);
        summary.totalCost = sqlite3_column_int64(stmt, 5);
        summary.totalRewards = sqlite3_column_int64(stmt, 6);
        summary.totalProfit = sqlite3_column_int64(stmt, 7);
    }

    sqlite3_finalize(stmt);
    return summary;
}

// Sync state management

int BCTDatabaseSQLite::getLastProcessedHeight() {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return -1;

    const char* sql = "SELECT value FROM sync_state WHERE key = 'last_processed_height';";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    int height = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        height = std::stoi(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return height;
}

bool BCTDatabaseSQLite::setLastProcessedHeight(int height) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    std::stringstream ss;
    ss << "INSERT OR REPLACE INTO sync_state (key, value) VALUES ('last_processed_height', '"
       << height << "');";
    
    return executeSQL(ss.str());
}

// Reward tracking

bool BCTDatabaseSQLite::insertReward(const std::string& coinbaseTxid, const std::string& bctTxid,
                                      int64_t amount, int height) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = "INSERT OR REPLACE INTO rewards (coinbase_txid, bct_txid, amount, height) "
                      "VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, coinbaseTxid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bctTxid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, amount);
    sqlite3_bind_int(stmt, 4, height);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

int64_t BCTDatabaseSQLite::getTotalRewardsForBCT(const std::string& bctTxid) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return 0;

    const char* sql = "SELECT SUM(amount) FROM rewards WHERE bct_txid = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, bctTxid.c_str(), -1, SQLITE_TRANSIENT);

    int64_t total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return total;
}

// Data management

bool BCTDatabaseSQLite::clearAllData() {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    bool success = executeSQL("DELETE FROM rewards;") &&
                   executeSQL("DELETE FROM bcts;") &&
                   executeSQL("DELETE FROM sync_state;");
    
    if (success) {
        invalidateCache();
    }
    
    return success;
}

int BCTDatabaseSQLite::rescanFromHeight(int startHeight, int stopHeight) {
    LogPrintf("BCTDatabase: Rescanning BCT data from height %d to %d\n", startHeight, stopHeight);
    
    if (!isInitialized()) {
        LogPrintf("BCTDatabase: Cannot rescan - database not initialized\n");
        return -1;
    }
    
    // If starting from 0, clear all data first
    if (startHeight == 0) {
        clearAllData();
    } else {
        // Delete BCTs and rewards created at or after startHeight
        deleteBCTsAfterHeight(startHeight - 1);
        deleteRewardsAfterHeight(startHeight - 1);
    }
    
    // Get consensus params
    const Consensus::Params& consensusParams = Params().GetConsensus();
    
    // Get BCT creation address script
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.beeCreationAddress));
    CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.hiveCommunityAddress));
    
    int bctCount = 0;
    
    {
        LOCK(cs_main);
        
        // Determine stop height
        if (stopHeight < 0 || stopHeight > chainActive.Height()) {
            stopHeight = chainActive.Height();
        }
        
        beginTransaction();
        
        // Iterate through blocks from startHeight to stopHeight
        for (int height = startHeight; height <= stopHeight; height++) {
            const CBlockIndex* pindex = chainActive[height];
            if (!pindex) continue;
            
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, consensusParams)) {
                LogPrintf("BCTDatabase: Failed to read block at height %d\n", height);
                continue;
            }
            
            // Scan transactions in this block
            for (const auto& tx : block.vtx) {
                // Check if this is a BCT (Bee Creation Transaction)
                CAmount beeFeePaid = 0;
                CScript scriptPubKeyHoney;
                
                if (tx->IsBCT(consensusParams, scriptPubKeyBCF, &beeFeePaid, &scriptPubKeyHoney)) {
                    // Extract honey address
                    CTxDestination honeyDestination;
                    if (!ExtractDestination(scriptPubKeyHoney, honeyDestination)) {
                        continue;
                    }
                    std::string honeyAddress = EncodeDestination(honeyDestination);
                    
                    // Check for community contribution
                    if (tx->vout.size() > 1 && tx->vout[1].scriptPubKey == scriptPubKeyCF) {
                        beeFeePaid += tx->vout[1].nValue;
                    }
                    
                    // Calculate bee count
                    CAmount beeCost = GetBeeCost(height, consensusParams);
                    int beeCount = beeCost > 0 ? beeFeePaid / beeCost : 0;
                    
                    // Create BCT record
                    BCTRecord record;
                    record.txid = tx->GetHash().GetHex();
                    record.honeyAddress = honeyAddress;
                    record.beeCount = beeCount;
                    record.creationHeight = height;
                    record.maturityHeight = height + consensusParams.beeGestationBlocks;
                    record.expirationHeight = height + consensusParams.beeGestationBlocks + consensusParams.beeLifespanBlocks;
                    record.timestamp = pindex->GetBlockTime();
                    record.cost = beeFeePaid;
                    record.blocksFound = 0;
                    record.rewardsPaid = 0;
                    record.profit = -beeFeePaid;
                    
                    // Update status based on current chain height
                    record.updateStatus(stopHeight);
                    
                    // Insert if not exists
                    if (!bctExists(record.txid)) {
                        insertBCT(record);
                        bctCount++;
                    }
                }
                
                // Check if this is a Hive coinbase (reward transaction)
                // Must be coinbase with OP_RETURN OP_BEE marker
                if (tx->IsHiveCoinBase() && tx->vout[0].scriptPubKey.size() >= 78) {
                    // Extract BCT txid from the proof script (bytes 14-78 contain the 64-char hex txid)
                    std::vector<unsigned char> bctTxidBytes(&tx->vout[0].scriptPubKey[14], 
                                                            &tx->vout[0].scriptPubKey[14 + 64]);
                    std::string bctTxid(bctTxidBytes.begin(), bctTxidBytes.end());
                    
                    if (bctExists(bctTxid)) {
                        CAmount rewardAmount = tx->vout[1].nValue;
                        std::string coinbaseTxid = tx->GetHash().GetHex();
                        
                        insertReward(coinbaseTxid, bctTxid, rewardAmount, height);
                        
                        // Update BCT record
                        BCTRecord bct = getBCT(bctTxid);
                        bct.blocksFound++;
                        bct.rewardsPaid += rewardAmount;
                        bct.profit = bct.rewardsPaid - bct.cost;
                        updateBCT(bctTxid, bct);
                    }
                }
            }
            
            // Log progress every 10000 blocks
            if (height % 10000 == 0) {
                LogPrintf("BCTDatabase: Rescan progress - height %d, BCTs found: %d\n", height, bctCount);
            }
        }
        
        // Update last processed height
        setLastProcessedHeight(stopHeight);
        
        commitTransaction();
    }
    
    // Refresh cache
    loadIntoCache();
    
    LogPrintf("BCTDatabase: Rescan complete - scanned heights %d to %d, found %d BCTs\n", 
              startHeight, stopHeight, bctCount);
    
    return bctCount;
}

int BCTDatabaseSQLite::getBCTCount() {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM bcts;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

bool BCTDatabaseSQLite::migrateFromJSON(const std::string& jsonPath) {
    LogPrintf("BCTDatabase: Attempting JSON migration from %s\n", jsonPath);
    
    // Check if JSON file exists
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LogPrintf("BCTDatabase: No JSON cache file found at %s, skipping migration\n", jsonPath);
        return true; // Not an error - file just doesn't exist
    }
    
    // Read the entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    std::string jsonContent = buffer.str();
    
    if (jsonContent.empty()) {
        LogPrintf("BCTDatabase: JSON cache file is empty, skipping migration\n");
        return true;
    }
    
    // Parse JSON manually (simple parser for our known format)
    // Format: {"version":"1.0","timestamp":123456,"bcts":[{...},{...}]}
    
    // Find the bcts array
    size_t bctsStart = jsonContent.find("\"bcts\"");
    if (bctsStart == std::string::npos) {
        LogPrintf("BCTDatabase: Invalid JSON format - no bcts array found\n");
        return false;
    }
    
    size_t arrayStart = jsonContent.find('[', bctsStart);
    if (arrayStart == std::string::npos) {
        LogPrintf("BCTDatabase: Invalid JSON format - no array start found\n");
        return false;
    }
    
    // Find matching closing bracket
    int bracketCount = 1;
    size_t arrayEnd = arrayStart + 1;
    while (arrayEnd < jsonContent.size() && bracketCount > 0) {
        if (jsonContent[arrayEnd] == '[') bracketCount++;
        else if (jsonContent[arrayEnd] == ']') bracketCount--;
        arrayEnd++;
    }
    
    if (bracketCount != 0) {
        LogPrintf("BCTDatabase: Invalid JSON format - unmatched brackets\n");
        return false;
    }
    
    std::string bctsArray = jsonContent.substr(arrayStart, arrayEnd - arrayStart);
    
    // Parse individual BCT objects
    int migratedCount = 0;
    size_t pos = 0;
    
    beginTransaction();
    
    while ((pos = bctsArray.find('{', pos)) != std::string::npos) {
        size_t objEnd = bctsArray.find('}', pos);
        if (objEnd == std::string::npos) break;
        
        std::string objStr = bctsArray.substr(pos, objEnd - pos + 1);
        
        // Extract fields from JSON object
        BCTRecord record;
        
        // Helper lambda to extract string value
        auto extractString = [&objStr](const std::string& key) -> std::string {
            std::string searchKey = "\"" + key + "\"";
            size_t keyPos = objStr.find(searchKey);
            if (keyPos == std::string::npos) return "";
            
            size_t colonPos = objStr.find(':', keyPos);
            if (colonPos == std::string::npos) return "";
            
            size_t valueStart = objStr.find('"', colonPos);
            if (valueStart == std::string::npos) return "";
            
            size_t valueEnd = objStr.find('"', valueStart + 1);
            if (valueEnd == std::string::npos) return "";
            
            return objStr.substr(valueStart + 1, valueEnd - valueStart - 1);
        };
        
        // Helper lambda to extract int value
        auto extractInt = [&objStr](const std::string& key) -> int {
            std::string searchKey = "\"" + key + "\"";
            size_t keyPos = objStr.find(searchKey);
            if (keyPos == std::string::npos) return 0;
            
            size_t colonPos = objStr.find(':', keyPos);
            if (colonPos == std::string::npos) return 0;
            
            // Skip whitespace
            size_t valueStart = colonPos + 1;
            while (valueStart < objStr.size() && (objStr[valueStart] == ' ' || objStr[valueStart] == '\t'))
                valueStart++;
            
            // Read number
            std::string numStr;
            while (valueStart < objStr.size() && (isdigit(objStr[valueStart]) || objStr[valueStart] == '-')) {
                numStr += objStr[valueStart++];
            }
            
            return numStr.empty() ? 0 : std::stoi(numStr);
        };
        
        // Helper lambda to extract int64 value
        auto extractInt64 = [&objStr](const std::string& key) -> int64_t {
            std::string searchKey = "\"" + key + "\"";
            size_t keyPos = objStr.find(searchKey);
            if (keyPos == std::string::npos) return 0;
            
            size_t colonPos = objStr.find(':', keyPos);
            if (colonPos == std::string::npos) return 0;
            
            size_t valueStart = colonPos + 1;
            while (valueStart < objStr.size() && (objStr[valueStart] == ' ' || objStr[valueStart] == '\t'))
                valueStart++;
            
            std::string numStr;
            while (valueStart < objStr.size() && (isdigit(objStr[valueStart]) || objStr[valueStart] == '-')) {
                numStr += objStr[valueStart++];
            }
            
            return numStr.empty() ? 0 : std::stoll(numStr);
        };
        
        record.txid = extractString("txid");
        record.status = extractString("status");
        record.honeyAddress = extractString("honey_address");
        record.beeCount = extractInt("total_mice");
        record.blocksFound = extractInt("blocks_found");
        record.timestamp = extractInt64("timestamp");
        record.cost = extractInt64("cost");
        record.rewardsPaid = extractInt64("rewards_paid");
        record.profit = extractInt64("profit");
        
        // The old JSON format doesn't have height fields
        // Set to 0 - they will be filled in by a rescan if needed
        // But keep the status from JSON so records are still visible
        record.creationHeight = 0;
        record.maturityHeight = 0;
        record.expirationHeight = 0;
        
        if (!record.txid.empty()) {
            // Calculate checksum
            record.checksum = record.calculateChecksum();
            record.updatedAt = std::time(nullptr);
            
            // Insert into database (without lock since we're in a transaction)
            if (!bctExists(record.txid)) {
                insertBCT(record);
                migratedCount++;
            }
        }
        
        pos = objEnd + 1;
    }
    
    commitTransaction();
    
    LogPrintf("BCTDatabase: Migrated %d BCT records from JSON cache\n", migratedCount);
    
    // Delete the old JSON file after successful migration
    if (migratedCount > 0 || bctsArray.find('{') == std::string::npos) {
        if (std::remove(jsonPath.c_str()) == 0) {
            LogPrintf("BCTDatabase: Deleted old JSON cache file %s\n", jsonPath);
        } else {
            LogPrintf("BCTDatabase: Warning - could not delete old JSON cache file %s\n", jsonPath);
        }
    }
    
    return true;
}

bool BCTDatabaseSQLite::databaseExists() const {
    std::ifstream file(dbPath);
    return file.good();
}

bool BCTDatabaseSQLite::loadIntoCache() {
    LogPrintf("BCTDatabase: Loading data into memory cache\n");
    
    if (!isInitialized()) {
        LogPrintf("BCTDatabase: Cannot load cache - database not initialized\n");
        return false;
    }
    
    auto records = getAllBCTs(true); // Include expired
    
    std::lock_guard<std::mutex> lock(dbMutex);
    cache.clear();
    
    // Load all records into cache without checksum validation
    // Checksum validation is only used for detecting corruption, not for filtering
    for (const auto& record : records) {
        cache[record.txid] = record;
    }
    
    cacheValid = true;
    
    LogPrintf("BCTDatabase: Loaded %zu records into cache\n", records.size());
    return true;
}

bool BCTDatabaseSQLite::performInitialScan(CWallet* pwallet) {
#ifdef ENABLE_WALLET
    if (pwallet == nullptr) {
        LogPrintf("BCTDatabase: No wallet available for initial scan\n");
        return false;
    }
    
    LogPrintf("BCTDatabase: Performing initial full scan from wallet (mapWallet size: %zu)\n", pwallet->mapWallet.size());
    
    LOCK2(cs_main, pwallet->cs_wallet);
    
    const Consensus::Params& consensusParams = Params().GetConsensus();
    int currentHeight = chainActive.Height();
    
    // Get BCT creation address script
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.beeCreationAddress));
    CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.hiveCommunityAddress));
    
    beginTransaction();
    
    int scannedCount = 0;
    int bctCount = 0;
    
    // Iterate through all wallet transactions
    for (const auto& item : pwallet->mapWallet) {
        const CWalletTx& wtx = item.second;
        scannedCount++;
        
        // Check if this is a BCT
        CAmount beeFeePaid = 0;
        CScript scriptPubKeyHoney;
        
        if (wtx.tx->IsBCT(consensusParams, scriptPubKeyBCF, &beeFeePaid, &scriptPubKeyHoney)) {
            // Extract honey address
            CTxDestination honeyDestination;
            if (!ExtractDestination(scriptPubKeyHoney, honeyDestination)) {
                continue;
            }
            std::string honeyAddress = EncodeDestination(honeyDestination);
            
            // Check for community contribution
            if (wtx.tx->vout.size() > 1 && wtx.tx->vout[1].scriptPubKey == scriptPubKeyCF) {
                beeFeePaid += wtx.tx->vout[1].nValue;
            }
            
            // Get block height
            int height = 0;
            if (wtx.hashBlock != uint256()) {
                BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
                if (mi != mapBlockIndex.end() && mi->second) {
                    height = mi->second->nHeight;
                }
            }
            
            // Calculate bee count
            CAmount beeCost = GetBeeCost(height, consensusParams);
            int beeCount = beeCost > 0 ? beeFeePaid / beeCost : 0;
            
            // Create BCT record
            BCTRecord record;
            record.txid = wtx.GetHash().GetHex();
            record.honeyAddress = honeyAddress;
            record.beeCount = beeCount;
            record.creationHeight = height;
            record.maturityHeight = height + consensusParams.beeGestationBlocks;
            record.expirationHeight = height + consensusParams.beeGestationBlocks + consensusParams.beeLifespanBlocks;
            record.timestamp = wtx.GetTxTime();
            record.cost = beeFeePaid;
            record.blocksFound = 0;
            record.rewardsPaid = 0;
            record.profit = -beeFeePaid;
            
            // Update status based on current height
            record.updateStatus(currentHeight);
            
            // Calculate checksum
            record.checksum = record.calculateChecksum();
            record.updatedAt = std::time(nullptr);
            
            if (!bctExists(record.txid)) {
                insertBCT(record);
                bctCount++;
            }
        }
    }
    
    // Now scan for Hive coinbase transactions to get rewards
    // Build a set of our BCT txids for quick lookup
    std::set<std::string> myBctIds;
    {
        auto allBcts = getAllBCTs(true);
        for (const auto& bct : allBcts) {
            myBctIds.insert(bct.txid);
        }
    }
    
    // Build rewards map from Hive coinbase transactions
    std::map<std::string, std::pair<int, CAmount>> rewardsMap; // txid -> (blocksFound, rewardsPaid)
    
    for (const auto& item : pwallet->mapWallet) {
        const CWalletTx& wtx = item.second;
        
        // Only process hive coinbase transactions
        if (!wtx.IsHiveCoinBase()) continue;
        
        // Skip unconfirmed transactions
        if (wtx.GetDepthInMainChain() < 1) continue;
        
        // Extract the BCT txid from the coinbase transaction
        if (wtx.tx->vout.size() > 0 && wtx.tx->vout[0].scriptPubKey.size() >= 78) {
            std::vector<unsigned char> blockTxid(&wtx.tx->vout[0].scriptPubKey[14], &wtx.tx->vout[0].scriptPubKey[14 + 64]);
            std::string blockTxidStr(blockTxid.begin(), blockTxid.end());
            
            // Only accumulate if this coinbase references one of our BCTs
            if (myBctIds.find(blockTxidStr) == myBctIds.end()) continue;
            
            auto &entry = rewardsMap[blockTxidStr];
            entry.first++;  // blocks found
            if (wtx.tx->vout.size() > 1) {
                entry.second += wtx.tx->vout[1].nValue;  // rewards
            }
        }
    }
    
    // Update BCT records with rewards
    int rewardsUpdated = 0;
    for (const auto& reward : rewardsMap) {
        BCTRecord bct = getBCT(reward.first);
        if (!bct.txid.empty()) {
            bct.blocksFound = reward.second.first;
            bct.rewardsPaid = reward.second.second;
            bct.profit = bct.rewardsPaid - bct.cost;
            bct.checksum = bct.calculateChecksum();
            bct.updatedAt = std::time(nullptr);
            updateBCT(bct.txid, bct);
            rewardsUpdated++;
        }
    }
    
    // Set last processed height
    setLastProcessedHeight(currentHeight);
    
    commitTransaction();
    
    LogPrintf("BCTDatabase: Initial scan complete - scanned %d transactions, found %d BCTs, updated %d with rewards\n", 
              scannedCount, bctCount, rewardsUpdated);
    
    // Load into cache
    loadIntoCache();
    
    return true;
#else
    LogPrintf("BCTDatabase: Wallet support not enabled, cannot perform initial scan\n");
    return false;
#endif
}

bool BCTDatabaseSQLite::initializeOnStartup(CWallet* pwallet) {
    if (!isInitialized()) {
        LogPrintf("BCTDatabase: Database not initialized, cannot perform startup initialization\n");
        return false;
    }
    
    // Check for JSON cache migration first
    std::string jsonCachePath = dbPath;
    // Replace bct_database.sqlite with bct_cache.json
    size_t pos = jsonCachePath.rfind("bct_database.sqlite");
    if (pos != std::string::npos) {
        jsonCachePath.replace(pos, 19, "bct_cache.json");
        migrateFromJSON(jsonCachePath);
    }
    
    // Check if we have existing data
    int recordCount = 0;
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        if (db != nullptr) {
            sqlite3_stmt* stmt;
            const char* sql = "SELECT COUNT(*) FROM bcts;";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    recordCount = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    
    if (recordCount > 0) {
        // Database has data - load into cache and skip full scan
        LogPrintf("BCTDatabase: Found %d existing BCT records, loading into cache\n", recordCount);
        
        // Load data into cache
        if (!loadIntoCache()) {
            LogPrintf("BCTDatabase: Failed to load cache, will perform full scan\n");
            clearAllData();
            return performInitialScan(pwallet);
        }
        
        // Check for records with missing height data (from JSON migration)
        // These need a rescan to get proper height values
        int recordsWithMissingHeights = 0;
        {
            std::lock_guard<std::mutex> lock(dbMutex);
            if (db != nullptr) {
                sqlite3_stmt* stmt;
                const char* sql = "SELECT COUNT(*) FROM bcts WHERE creation_height = 0 OR maturity_height = 0 OR expiration_height = 0;";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        recordsWithMissingHeights = sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);
                }
            }
        }
        
        if (recordsWithMissingHeights > 0) {
            LogPrintf("BCTDatabase: Found %d records with missing height data (from JSON migration). "
                      "Performing full rescan to populate heights.\n", recordsWithMissingHeights);
            clearAllData();
            return performInitialScan(pwallet);
        }
        
        // Note: We no longer delete records with invalid checksums
        // Invalid checksums are logged but records are kept
        // A full rescan can be triggered manually with -rescanbct if needed
        auto invalidRecords = getInvalidChecksumRecords();
        if (!invalidRecords.empty()) {
            LogPrintf("BCTDatabase: Warning - found %zu records with invalid checksums. "
                      "Consider running with -rescanbct to rebuild the database.\n", 
                      invalidRecords.size());
        }
        
        return true;
    } else {
        // No existing data - perform initial full scan
        LogPrintf("BCTDatabase: No existing BCT data, performing initial full scan\n");
        return performInitialScan(pwallet);
    }
}

std::vector<std::string> BCTDatabaseSQLite::getInvalidChecksumRecords() {
    std::vector<std::string> invalidTxids;
    
    auto allRecords = getAllBCTs(true);
    for (const auto& record : allRecords) {
        if (!record.validateChecksum()) {
            invalidTxids.push_back(record.txid);
        }
    }
    
    return invalidTxids;
}

bool BCTDatabaseSQLite::markRecordsForRescan(const std::vector<std::string>& txids) {
    // For now, we just delete the invalid records so they get rescanned
    // A more sophisticated approach would mark them and rescan incrementally
    
    beginTransaction();
    
    for (const auto& txid : txids) {
        deleteBCT(txid);
    }
    
    commitTransaction();
    
    LogPrintf("BCTDatabase: Marked %zu records for rescan by deletion\n", txids.size());
    return true;
}

bool BCTDatabaseSQLite::validateAllChecksums() {
    auto allRecords = getAllBCTs(true);
    
    int validCount = 0;
    int invalidCount = 0;
    
    for (const auto& record : allRecords) {
        if (record.validateChecksum()) {
            validCount++;
        } else {
            invalidCount++;
            LogPrintf("BCTDatabase: Invalid checksum for BCT %s\n", record.txid);
        }
    }
    
    LogPrintf("BCTDatabase: Checksum validation complete - %d valid, %d invalid\n", 
              validCount, invalidCount);
    
    return invalidCount == 0;
}

bool BCTDatabaseSQLite::updateRecordChecksum(const std::string& txid) {
    BCTRecord record = getBCT(txid);
    if (record.txid.empty()) {
        return false;
    }
    
    record.checksum = record.calculateChecksum();
    return updateBCT(txid, record);
}

// Cache management

void BCTDatabaseSQLite::invalidateCache() {
    cache.clear();
    cacheValid = false;
}

void BCTDatabaseSQLite::refreshCache() {
    // Note: This method should be called without holding dbMutex
    // to avoid deadlock with getAllBCTs
    auto records = getAllBCTs(true);
    
    std::lock_guard<std::mutex> lock(dbMutex);
    cache.clear();
    for (const auto& record : records) {
        cache[record.txid] = record;
    }
    cacheValid = true;
}

// Delete rewards after a specific height (for reorg handling)
bool BCTDatabaseSQLite::deleteRewardsAfterHeight(int height) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = "DELETE FROM rewards WHERE height > ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LogPrintf("BCTDatabase: Failed to prepare deleteRewardsAfterHeight statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, height);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LogPrintf("BCTDatabase: Failed to delete rewards after height %d: %s\n", height, sqlite3_errmsg(db));
        return false;
    }

    return true;
}

// Delete BCTs created after a specific height (for reorg handling)
bool BCTDatabaseSQLite::deleteBCTsAfterHeight(int height) {
    std::lock_guard<std::mutex> lock(dbMutex);
    
    if (db == nullptr) return false;

    const char* sql = "DELETE FROM bcts WHERE creation_height > ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LogPrintf("BCTDatabase: Failed to prepare deleteBCTsAfterHeight statement: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, height);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LogPrintf("BCTDatabase: Failed to delete BCTs after height %d: %s\n", height, sqlite3_errmsg(db));
        return false;
    }

    invalidateCache();
    return true;
}

// Update all BCT statuses based on current height
void BCTDatabaseSQLite::updateAllStatuses(int currentHeight) {
    if (db == nullptr) return;

    // Get consensus params for maturity/expiration calculations
    const Consensus::Params& consensusParams = Params().GetConsensus();
    int gestationBlocks = consensusParams.beeGestationBlocks;
    int lifespanBlocks = consensusParams.beeLifespanBlocks;

    // Update immature -> mature
    // Only update records with valid heights (creation_height > 0)
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        std::stringstream ss;
        ss << "UPDATE bcts SET status = 'mature', updated_at = " << std::time(nullptr)
           << " WHERE status = 'immature' AND creation_height > 0"
           << " AND (creation_height + " << gestationBlocks << ") <= " << currentHeight
           << " AND (creation_height + " << gestationBlocks << " + " << lifespanBlocks << ") > " << currentHeight << ";";
        executeSQL(ss.str());
    }

    // Update mature -> expired
    // Only update records with valid heights (creation_height > 0)
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        std::stringstream ss;
        ss << "UPDATE bcts SET status = 'expired', updated_at = " << std::time(nullptr)
           << " WHERE status != 'expired' AND creation_height > 0"
           << " AND (creation_height + " << gestationBlocks << " + " << lifespanBlocks << ") <= " << currentHeight << ";";
        executeSQL(ss.str());
    }

    invalidateCache();
}

// Process a block for BCT updates
void BCTDatabaseSQLite::processBlock(const CBlock& block, const CBlockIndex* pindex, CWallet* pwallet) {
    if (!isInitialized() || pindex == nullptr) return;

    const Consensus::Params& consensusParams = Params().GetConsensus();
    int height = pindex->nHeight;
    
    LogPrint(BCLog::ALL, "BCTDatabase: Processing block %d for BCT updates\n", height);

    // Get the BCT creation address script
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.beeCreationAddress));
    CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.hiveCommunityAddress));

    // Begin transaction for atomicity
    beginTransaction();

    try {
        // Scan transactions in this block
        for (const auto& tx : block.vtx) {
            // Check if this is a BCT (Bee Creation Transaction)
            CAmount beeFeePaid = 0;
            CScript scriptPubKeyHoney;
            
            if (tx->IsBCT(consensusParams, scriptPubKeyBCF, &beeFeePaid, &scriptPubKeyHoney)) {
                // Extract honey address
                CTxDestination honeyDestination;
                if (!ExtractDestination(scriptPubKeyHoney, honeyDestination)) {
                    LogPrintf("BCTDatabase: Couldn't extract destination from BCT %s\n", tx->GetHash().GetHex());
                    continue;
                }
                std::string honeyAddress = EncodeDestination(honeyDestination);

                // Check for community contribution
                if (tx->vout.size() > 1 && tx->vout[1].scriptPubKey == scriptPubKeyCF) {
                    beeFeePaid += tx->vout[1].nValue;
                }

                // Calculate bee count
                CAmount beeCost = GetBeeCost(height, consensusParams);
                int beeCount = beeFeePaid / beeCost;

                // Create BCT record
                BCTRecord record;
                record.txid = tx->GetHash().GetHex();
                record.honeyAddress = honeyAddress;
                record.status = "immature";
                record.beeCount = beeCount;
                record.creationHeight = height;
                record.maturityHeight = height + consensusParams.beeGestationBlocks;
                record.expirationHeight = height + consensusParams.beeGestationBlocks + consensusParams.beeLifespanBlocks;
                record.timestamp = pindex->GetBlockTime();
                record.cost = beeFeePaid;
                record.blocksFound = 0;
                record.rewardsPaid = 0;
                record.profit = -beeFeePaid;

                // Insert or update the record
                if (!bctExists(record.txid)) {
                    insertBCT(record);
                    LogPrint(BCLog::ALL, "BCTDatabase: Added new BCT %s with %d bees at height %d\n", 
                             record.txid, beeCount, height);
                }
            }

            // Check if this is a Hive coinbase (reward transaction)
            // Must be coinbase with OP_RETURN OP_BEE marker
            if (tx->IsHiveCoinBase() && tx->vout[0].scriptPubKey.size() >= 78) {
                // Extract BCT txid from the proof script (bytes 14-78 contain the 64-char hex txid)
                std::vector<unsigned char> bctTxidBytes(&tx->vout[0].scriptPubKey[14], 
                                                        &tx->vout[0].scriptPubKey[14 + 64]);
                std::string bctTxid(bctTxidBytes.begin(), bctTxidBytes.end());

                // Check if this BCT exists in our database
                if (bctExists(bctTxid)) {
                    // Record the reward
                    CAmount rewardAmount = tx->vout[1].nValue;
                    std::string coinbaseTxid = tx->GetHash().GetHex();
                    
                    insertReward(coinbaseTxid, bctTxid, rewardAmount, height);

                    // Update the BCT record with new reward info
                    BCTRecord bct = getBCT(bctTxid);
                    bct.blocksFound++;
                    bct.rewardsPaid += rewardAmount;
                    bct.profit = bct.rewardsPaid - bct.cost;
                    updateBCT(bctTxid, bct);

                    LogPrint(BCLog::ALL, "BCTDatabase: Recorded reward %lld for BCT %s at height %d\n",
                             rewardAmount, bctTxid, height);
                }
            }
        }

        // Update BCT statuses based on current height
        updateAllStatuses(height);

        // Update last processed height
        setLastProcessedHeight(height);

        commitTransaction();
    } catch (const std::exception& e) {
        LogPrintf("BCTDatabase: Error processing block %d: %s\n", height, e.what());
        rollbackTransaction();
    }
}

// Handle blockchain reorganization
void BCTDatabaseSQLite::handleReorg(int forkHeight) {
    if (!isInitialized()) return;

    LogPrintf("BCTDatabase: Handling reorg at fork height %d\n", forkHeight);

    beginTransaction();

    try {
        // Delete BCT records created after fork height
        deleteBCTsAfterHeight(forkHeight);

        // Delete reward records after fork height
        deleteRewardsAfterHeight(forkHeight);

        // Update sync_state to fork height
        setLastProcessedHeight(forkHeight);

        // Recalculate rewards for remaining BCTs
        // (rewards table entries after fork height were deleted, so totals need recalculation)
        auto allBCTs = getAllBCTs(true);
        for (auto& bct : allBCTs) {
            int64_t totalRewards = getTotalRewardsForBCT(bct.txid);
            if (bct.rewardsPaid != totalRewards) {
                bct.rewardsPaid = totalRewards;
                // Count blocks found from rewards table
                // For simplicity, we'll need to query the count
                bct.profit = bct.rewardsPaid - bct.cost;
                updateBCT(bct.txid, bct);
            }
        }

        commitTransaction();

        LogPrintf("BCTDatabase: Reorg handling complete, reset to height %d\n", forkHeight);
    } catch (const std::exception& e) {
        LogPrintf("BCTDatabase: Error handling reorg: %s\n", e.what());
        rollbackTransaction();
    }
}

// BCTBlockHandler implementation

BCTBlockHandler::BCTBlockHandler() : fRegistered(false) {}

BCTBlockHandler::~BCTBlockHandler() {
    if (fRegistered) {
        UnregisterValidationInterface();
    }
}

void BCTBlockHandler::RegisterValidationInterface() {
    if (!fRegistered) {
        ::RegisterValidationInterface(this);
        fRegistered = true;
        LogPrintf("BCTBlockHandler: Registered with validation interface\n");
    }
}

void BCTBlockHandler::UnregisterValidationInterface() {
    if (fRegistered) {
        ::UnregisterValidationInterface(this);
        fRegistered = false;
        LogPrintf("BCTBlockHandler: Unregistered from validation interface\n");
    }
}

void BCTBlockHandler::BlockConnected(const std::shared_ptr<const CBlock>& block,
                                      const CBlockIndex* pindex,
                                      const std::vector<CTransactionRef>& txnConflicted) {
    if (!block || !pindex) return;

    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    if (!db->isInitialized()) return;

#ifdef ENABLE_WALLET
    // Get the first wallet (if available) for ownership checks
    CWallet* pwallet = nullptr;
    if (!vpwallets.empty()) {
        pwallet = vpwallets[0];
    }
    
    db->processBlock(*block, pindex, pwallet);
#else
    db->processBlock(*block, pindex, nullptr);
#endif
}

void BCTBlockHandler::BlockDisconnected(const std::shared_ptr<const CBlock>& block) {
    if (!block) return;

    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    if (!db->isInitialized()) return;

    // Get the height of the disconnected block
    // We need to find the block index for this block
    LOCK(cs_main);
    BlockMap::iterator mi = mapBlockIndex.find(block->GetHash());
    if (mi != mapBlockIndex.end()) {
        int disconnectedHeight = mi->second->nHeight;
        // Handle reorg by rolling back to the block before the disconnected one
        db->handleReorg(disconnectedHeight - 1);
    }
}

// Global initialization functions

void InitBCTBlockHandler() {
    if (!g_bct_block_handler) {
        g_bct_block_handler.reset(new BCTBlockHandler());
        g_bct_block_handler->RegisterValidationInterface();
    }
}

void ShutdownBCTBlockHandler() {
    if (g_bct_block_handler) {
        g_bct_block_handler->UnregisterValidationInterface();
        g_bct_block_handler.reset();
    }
}
