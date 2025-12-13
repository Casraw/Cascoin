## Cascoin Core v3.2.0 – BCT Persistent Database

### Overview
- **Major Feature**: SQLite-based persistent database for BCT (Bee Creation Transaction) data
- **Performance**: Drastically faster startup times by avoiding full wallet scans
- **Incremental Updates**: New blocks are processed efficiently without complete rescan
- **Reorg Handling**: Automatic handling of blockchain reorganizations
- **GUI Integration**: Asynchronous database queries for smooth user interface

### Highlights
- **SQLite Integration**: New `bct_database.sqlite` file in Cascoin data directory
- **Fast Startup**: BCT data is loaded from database instead of scanning wallet
- **Incremental Block Processing**: Only new blocks are analyzed
- **JSON Migration**: Automatic migration from old `bct_cache.json` file
- **New RPC Commands**: `rescanbctdatabase` for manual rescans
- **Startup Parameters**: `-rescanbct` for full database rescan

### New Features

#### SQLite-based BCT Database
- **Persistent Storage**: BCT data survives restarts without re-scanning
- **Efficient Queries**: Indexed search by status, height, and honey address
- **Atomic Transactions**: Data integrity guaranteed through SQLite transactions
- **Checksum Validation**: Automatic detection of corrupted records

#### Incremental Block Processing
- **ValidationInterface**: Automatic processing of new blocks
- **BlockConnected**: Detection of new BCTs and Hive rewards
- **BlockDisconnected**: Correct handling of reorgs
- **Status Updates**: Automatic updates (immature → mature → expired)

#### Rescan Functionality
- **`-rescanbct` Parameter**: Full rescan at startup
- **`rescanbctdatabase` RPC**: Manual rescan with optional height limits
  ```
  rescanbctdatabase [start_height] [stop_height]
  ```
- **Integration with `-reindex`**: BCT rescan is automatically triggered
- **Integration with `rescanblockchain`**: BCT rescan after wallet rescan

#### GUI Improvements
- **Asynchronous Queries**: Database operations in background thread
- **No GUI Freeze**: User interface remains responsive during queries
- **Instant Display**: In-memory cache for fast rendering
- **Automatic Updates**: GUI responds to database updates

### Technical Details

#### Database Schema
```sql
-- BCT main table
CREATE TABLE bcts (
    txid TEXT PRIMARY KEY,
    honey_address TEXT,
    status TEXT,
    bee_count INTEGER,
    creation_height INTEGER,
    maturity_height INTEGER,
    expiration_height INTEGER,
    timestamp INTEGER,
    cost INTEGER,
    blocks_found INTEGER,
    rewards_paid INTEGER,
    profit INTEGER,
    checksum TEXT,
    updated_at INTEGER
);

-- Rewards table
CREATE TABLE rewards (
    coinbase_txid TEXT PRIMARY KEY,
    bct_txid TEXT,
    amount INTEGER,
    height INTEGER
);

-- Sync status
CREATE TABLE sync_state (
    key TEXT PRIMARY KEY,
    value TEXT
);

-- Schema version for migrations
CREATE TABLE schema_version (
    version INTEGER
);
```

#### Indexes for Performance
- `idx_bcts_status` - Fast filtering by status
- `idx_bcts_creation_height` - Efficient height queries
- `idx_bcts_honey_address` - Search by honey address
- `idx_rewards_bct_txid` - Reward assignment
- `idx_rewards_height` - Height-based reward queries

#### New Files
- `src/bctdb.h` - BCTDatabaseSQLite class and BCTRecord structure
- `src/bctdb.cpp` - SQLite implementation
- `src/test/bctdb_tests.cpp` - Unit tests and property tests

#### Removed Files
- `src/qt/bctdatabase.h` - Old JSON-based implementation
- `src/qt/bctdatabase.cpp` - Old JSON-based implementation

### RPC Changes

#### New Command: `rescanbctdatabase`
```json
rescanbctdatabase [start_height] [stop_height]

Arguments:
  start_height (optional): Start height for rescan (default: 0)
  stop_height (optional): End height for rescan (default: current height)

Returns:
{
  "start_height": 0,
  "stop_height": 500000,
  "bcts_found": 42
}
```

### Startup Parameters

#### `-rescanbct`
Deletes the BCT database and performs a full rescan.
```bash
./cascoind -rescanbct
```

### Migration from v3.1.x

**Automatic Migration**:
- On first startup, `bct_cache.json` is automatically migrated to SQLite
- After successful migration, the JSON file is deleted
- No manual action required

**Fallback Behavior**:
- If database doesn't exist: Full scan from wallet
- If database is corrupt: Automatic rescan of affected entries

### Performance Comparison

| Operation | v3.1.x (JSON) | v3.2.0 (SQLite) |
|-----------|---------------|-----------------|
| Startup with 1000 BCTs | ~30s | <1s |
| New block | Full Rescan | Incremental |
| GUI refresh | Blocking | Asynchronous |
| Reorg handling | Manual | Automatic |

### Build Changes

**New Dependency**: SQLite3
- Linux: `sudo apt-get install libsqlite3-dev`
- macOS: `brew install sqlite3`
- Windows: Automatic via depends

**Makefile Changes**:
- `$(SQLITE3_LIBS)` added to all relevant binaries
- New source files in `src/Makefile.am`

### Changelog

- [Feature][Database] SQLite-based persistent BCT database
- [Feature][Database] Incremental block processing via ValidationInterface
- [Feature][Database] Automatic reorg handling
- [Feature][Database] Checksum validation for data integrity
- [Feature][RPC] New `rescanbctdatabase` command
- [Feature][Startup] New `-rescanbct` parameter
- [Feature][Migration] Automatic JSON-to-SQLite migration
- [Enhancement][GUI] Asynchronous database queries
- [Enhancement][GUI] In-memory cache for fast display
- [Enhancement][Performance] Indexed database queries
- [Enhancement][Integration] BCT rescan on `-reindex` and `rescanblockchain`
- [Cleanup] Removal of old JSON-based BCTDatabase

### Bug Fixes (Post-Release)

- [Bugfix][Database] `updateStatus()` now checks for valid heights - Records with height 0 (e.g., from JSON migration) are no longer incorrectly marked as "expired"
- [Bugfix][Database] `updateAllStatuses()` ignores records with `creation_height = 0` - SQL queries only update records with valid height values
- [Bugfix][Database] `initializeOnStartup()` detects records with missing heights and automatically triggers a full rescan
- [Bugfix][Database] `performInitialScan()` now also scans Hive coinbase transactions for rewards - previously only BCTs were scanned but no rewards were captured
- [Bugfix][Database] Correct detection of Hive coinbase transactions with `IsHiveCoinBase()` instead of `IsCoinBase()`
- [Bugfix][GUI] Fallback to wallet scan when SQLite database is empty

---

### Upgrade Notes

**Recommended for all users**:
- Significantly faster startup times
- No more GUI freezes during BCT queries
- Automatic handling of blockchain reorgs

**No data migration required**:
- Old JSON data is automatically migrated
- Wallet data remains unchanged

**In case of problems**:
- `-rescanbct` for full database rebuild
- `rescanbctdatabase` RPC for partial rescan

**Known Issue (fixed)**:
If no bees are displayed in the GUI after upgrading to v3.2.0, this is due to records with missing height data from JSON migration. The current version automatically detects this and performs a rescan. If the problem persists:
```bash
rm ~/.cascoin/bct_database.sqlite
./cascoin-qt
```

### Credits

Thanks to everyone who directly contributed to this release:

- Casraw
