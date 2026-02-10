# Implementation Plan

- [x] 1. Set up SQLite integration and core database infrastructure
  - [x] 1.1 Add SQLite dependency to build system
    - Update configure.ac to check for SQLite3
    - Update Makefile.am to link against SQLite3
    - Add SQLite3 to depends/packages if needed for cross-compilation
    - _Requirements: 4.1_

  - [x] 1.2 Create BCTDatabaseSQLite class with schema creation
    - Create src/bctdb.h with BCTRecord struct and BCTDatabaseSQLite class declaration
    - Create src/bctdb.cpp with SQLite initialization and schema creation
    - Implement createSchema() with bcts, rewards, sync_state, and schema_version tables
    - Add proper indexes for status, creation_height, and honey_address
    - _Requirements: 7.1_

  - [x] 1.3 Write property test for serialization round-trip
    - **Property 1: Serialization Round-Trip Consistency**
    - **Validates: Requirements 1.4, 4.2, 7.2**
    - Generate random BCTRecord objects
    - Insert into database, read back, verify equivalence

- [x] 2. Implement CRUD operations and caching
  - [x] 2.1 Implement BCTRecord insert/update/delete operations
    - Implement insertBCT() with prepared statements
    - Implement updateBCT() for status and rewards updates
    - Implement deleteBCT() for cleanup
    - Use SQLite transactions for atomicity
    - _Requirements: 3.3_

  - [x] 2.2 Implement query operations with filtering and sorting
    - Implement getAllBCTs(includeExpired) with WHERE clause
    - Implement getBCTsByStatus(status) with indexed query
    - Implement getBCT(txid) for single record lookup
    - Implement getSummary() for aggregated statistics
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 2.3 Write property test for update isolation
    - **Property 2: Update Isolation**
    - **Validates: Requirements 2.2, 2.3, 2.4**
    - Generate database with multiple records
    - Update one record, verify others unchanged

  - [x] 2.4 Write property test for query correctness - sorting
    - **Property 5: Query Correctness - Sorting**
    - **Validates: Requirements 6.2**
    - Generate random BCT records
    - Query with sort, verify correct order

  - [x] 2.5 Write property test for query correctness - filtering
    - **Property 6: Query Correctness - Filtering**
    - **Validates: Requirements 6.3**
    - Generate records with various statuses
    - Filter by status, verify only matching records returned

- [x] 3. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement incremental block processing
  - [x] 4.1 Create ValidationInterface subscriber for block events
    - Create BCTBlockHandler class inheriting from CValidationInterface
    - Implement BlockConnected() to process new blocks for BCTs
    - Implement BlockDisconnected() for reorg handling
    - Register handler in init.cpp
    - _Requirements: 2.1, 3.1_

  - [x] 4.2 Implement processBlock() for incremental BCT updates
    - Scan block transactions for BCT creation transactions
    - Scan block transactions for hive coinbase rewards
    - Update BCT status based on current height (immature→mature→expired)
    - Store last processed height in sync_state table
    - _Requirements: 2.1, 2.2, 2.4_

  - [x] 4.3 Implement handleReorg() for blockchain reorganizations
    - Delete BCT records created after fork height
    - Delete reward records after fork height
    - Update sync_state to fork height
    - Trigger rescan from fork point
    - _Requirements: 3.1_

  - [x] 4.4 Write property test for incremental block processing
    - **Property 3: Incremental Block Processing**
    - **Validates: Requirements 2.1**
    - Process blocks incrementally
    - Compare result to full rescan
    - Verify equivalence

  - [x] 4.5 Write property test for transaction atomicity
    - **Property 4: Transaction Atomicity**
    - **Validates: Requirements 3.3**
    - Simulate interrupted writes
    - Verify no partial data

- [x] 5. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. Implement startup and migration logic
  - [x] 6.1 Implement database initialization on startup
    - Check if bct_database.sqlite exists
    - If exists: load data into memory cache, skip full scan
    - If not exists: perform initial full scan from wallet
    - Store database path in standard Cascoin data directory
    - _Requirements: 1.1, 1.2, 1.3, 4.1_

  - [x] 6.2 Implement JSON cache migration
    - Check for existing bct_cache.json file
    - Parse JSON and convert to BCTRecord objects
    - Insert records into SQLite database
    - Delete old JSON file after successful migration
    - _Requirements: 4.2_

  - [x] 6.3 Implement schema version checking and migration
    - Read schema_version from database
    - Compare with current application version
    - Execute migration scripts if version differs
    - Update schema_version after migration
    - _Requirements: 3.2, 7.3_

  - [x] 6.4 Implement data integrity validation with checksums
    - Calculate checksum for BCTRecord on insert
    - Validate checksum on load
    - Mark invalid records for rescan
    - _Requirements: 1.4_

- [x] 7. Implement manual rescan functionality
  - [x] 7.1 Add -rescanbct startup parameter
    - Add parameter definition in init.cpp help text
    - Check parameter in AppInitMain()
    - Delete existing database and trigger full rescan
    - _Requirements: 8.1_

  - [x] 7.2 Implement rescanbctdatabase RPC command
    - Add RPC command registration in rpcwallet.cpp
    - Parse start_height and stop_height parameters
    - Call BCTDatabaseSQLite::rescanFromHeight()
    - Return result with start_height, stop_height, bcts_found
    - _Requirements: 8.2, 8.3, 8.5_

  - [x] 7.3 Integrate BCT rescan with existing rescan triggers
    - Hook into -reindex processing in init.cpp
    - Hook into rescanblockchain RPC completion
    - Trigger BCT rescan when wallet rescan completes
    - _Requirements: 8.4_

- [x] 8. Integrate with GUI layer
  - [x] 8.1 Update HiveTableModel to use BCTDatabaseSQLite
    - Replace walletModel->getBCTs() calls with BCTDatabaseSQLite queries
    - Use in-memory cache for immediate GUI response
    - Subscribe to database update signals for refresh
    - _Requirements: 5.2_

  - [x] 8.2 Implement asynchronous database operations
    - Move database queries to background thread
    - Use Qt signals/slots for result delivery
    - Provide progress feedback for long operations
    - _Requirements: 5.1, 5.3_

  - [x] 8.3 Remove old BCTDatabase JSON implementation
    - Remove bctdatabase.cpp and bctdatabase.h
    - Update Makefile.am to remove old files
    - Update all includes to use new bctdb.h
    - _Requirements: 4.2_

- [x] 9. Final Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.
