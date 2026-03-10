# Requirements Document

## Introduction

This document specifies the requirements for the FALCON-512 Public Key Registry feature in Cascoin. The Registry provides a storage optimization for post-quantum signatures by storing FALCON-512 public keys once on-chain and referencing them by hash in subsequent transactions. This reduces transaction size from approximately 1563 bytes (897-byte public key + 666-byte signature) to approximately 698 bytes (32-byte hash + 666-byte signature) after the initial registration—a savings of approximately 55%.

## Glossary

- **Registry**: The LevelDB-backed storage system that maps Public_Key_Hash values to full FALCON-512 public keys
- **Public_Key_Hash**: A 32-byte SHA256 hash of a FALCON-512 public key, used as a compact reference identifier
- **Registration_Transaction**: A quantum transaction that includes the full 897-byte FALCON-512 public key, triggering storage in the Registry
- **Reference_Transaction**: A quantum transaction that includes only the 32-byte Public_Key_Hash instead of the full public key
- **Quantum_Address**: A Bech32m-encoded address derived from SHA256(FALCON-512 public key) with witness version 2 (casq/tcasq/rcasq prefix)
- **Validator**: A Cascoin node that validates transactions by verifying signatures against public keys
- **Registry_Database**: The LevelDB instance at `{datadir}/quantum_pubkeys` that stores the Registry data
- **Cache**: An in-memory LRU cache of recently accessed public keys for performance optimization

## Requirements

### Requirement 1: Registry Database Storage

**User Story:** As a node operator, I want FALCON-512 public keys to be stored in a persistent database, so that they can be retrieved efficiently during transaction validation.

#### Acceptance Criteria

1. THE Registry_Database SHALL store data at the filesystem path `{datadir}/quantum_pubkeys`
2. THE Registry_Database SHALL use the 32-byte Public_Key_Hash as the database key
3. THE Registry_Database SHALL store the complete 897-byte FALCON-512 public key as the database value
4. WHEN the node starts, THE Registry_Database SHALL initialize and open the LevelDB instance
5. WHEN the node shuts down, THE Registry_Database SHALL flush pending writes and close gracefully
6. IF the Registry_Database fails to open, THEN THE node SHALL log an error and continue with compact mode disabled

### Requirement 2: Public Key Registration

**User Story:** As a user with a quantum address, I want my public key to be automatically registered when I send my first transaction, so that subsequent transactions can use compact references.

#### Acceptance Criteria

1. WHEN a Registration_Transaction is validated, THE Registry SHALL compute SHA256 of the included public key
2. WHEN a Registration_Transaction is validated, THE Registry SHALL store the public key if the computed hash is not already present
3. WHEN the computed Public_Key_Hash already exists in the Registry, THE Registry SHALL skip storage and proceed with validation
4. WHEN storing a public key, THE Registry SHALL use synchronous writes to ensure durability
5. IF the public key size is not exactly 897 bytes, THEN THE Validator SHALL reject the transaction with error "Invalid quantum public key size"

### Requirement 3: Public Key Lookup

**User Story:** As a validator, I want to retrieve full public keys from the Registry using hash references, so that I can verify signatures on Reference_Transactions.

#### Acceptance Criteria

1. WHEN validating a Reference_Transaction, THE Registry SHALL look up the full public key using the provided Public_Key_Hash
2. IF the Public_Key_Hash is not found in the Registry, THEN THE Validator SHALL reject the transaction with error "Quantum public key not registered"
3. WHEN a public key is retrieved, THE Registry SHALL verify that SHA256 of the retrieved key matches the lookup hash
4. IF the hash verification fails on retrieval, THEN THE Registry SHALL log an error and return lookup failure
5. THE Registry lookup SHALL use LevelDB's O(1) hash-based key access

### Requirement 4: Transaction Witness Format

**User Story:** As a wallet developer, I want clear witness formats for registration and reference transactions, so that I can implement quantum transaction support correctly.

#### Acceptance Criteria

1. THE Registration_Transaction witness SHALL contain: [0x51 marker byte][897-byte FALCON-512 public key][up to 700-byte signature]
2. THE Reference_Transaction witness SHALL contain: [0x52 marker byte][32-byte Public_Key_Hash][up to 700-byte signature]
3. WHEN parsing a quantum witness, THE Validator SHALL read the first byte to determine the transaction mode
4. WHEN the first byte is 0x51, THE Validator SHALL parse the witness as a Registration_Transaction
5. WHEN the first byte is 0x52, THE Validator SHALL parse the witness as a Reference_Transaction
6. IF the marker byte is neither 0x51 nor 0x52, THEN THE Validator SHALL reject the transaction with error "Invalid quantum witness marker"

### Requirement 5: Signature Verification Integration

**User Story:** As a validator, I want signature verification to work seamlessly with both transaction types, so that all quantum transactions are validated correctly.

#### Acceptance Criteria

1. WHEN verifying a Registration_Transaction, THE Validator SHALL use the included public key directly for FALCON-512 verification
2. WHEN verifying a Reference_Transaction, THE Validator SHALL first retrieve the public key from the Registry
3. WHEN the public key is obtained (from witness or Registry), THE Validator SHALL verify the signature using FALCON-512
4. WHEN the public key is obtained, THE Validator SHALL verify that SHA256(public key) matches the transaction's quantum address program
5. IF the address derivation check fails, THEN THE Validator SHALL reject the transaction with error "Public key does not match quantum address"

### Requirement 6: Performance Optimization

**User Story:** As a node operator, I want the Registry to perform efficiently, so that transaction validation throughput is not degraded.

#### Acceptance Criteria

1. THE Registry SHALL maintain an in-memory LRU Cache of recently accessed public keys
2. THE Cache SHALL store at least 1000 public key entries
3. WHEN looking up a public key, THE Registry SHALL check the Cache before accessing the Registry_Database
4. WHEN a public key is retrieved from the Registry_Database, THE Registry SHALL add it to the Cache
5. WHEN the Cache reaches capacity, THE Registry SHALL evict the least recently used entry
6. THE Registry lookup from Cache SHALL complete in under 1 millisecond
7. THE Registry lookup from Registry_Database SHALL complete in under 10 milliseconds

### Requirement 7: RPC Interface

**User Story:** As a node operator, I want RPC commands to query the Registry, so that I can monitor the system and debug issues.

#### Acceptance Criteria

1. THE `getquantumpubkey` command SHALL accept a Public_Key_Hash parameter and return the corresponding full public key
2. THE `getquantumpubkey` command SHALL return error code -32001 when the hash is not found
3. THE `getquantumregistrystats` command SHALL return the total count of registered public keys
4. THE `getquantumregistrystats` command SHALL return the Registry_Database size in bytes
5. THE `isquantumpubkeyregistered` command SHALL accept a Public_Key_Hash and return true if registered, false otherwise
6. THE RPC commands SHALL be categorized under the "quantum" help category

### Requirement 8: Data Integrity

**User Story:** As a security auditor, I want the Registry to maintain data integrity, so that public keys cannot be corrupted or tampered with.

#### Acceptance Criteria

1. THE Registry_Database SHALL enable LevelDB checksums for all stored data
2. THE Registry SHALL NOT provide any mechanism to delete registered public keys
3. THE Registry SHALL NOT provide any mechanism to modify registered public keys
4. WHEN a checksum error is detected during read, THE Registry SHALL log an error and return lookup failure
5. IF the Registry_Database becomes corrupted, THEN THE node SHALL provide a `-rebuildquantumregistry` startup option to rebuild from blockchain data

### Requirement 9: Activation and Configuration

**User Story:** As a network operator, I want the Registry feature to activate together with quantum addresses, so that all quantum features are enabled consistently.

#### Acceptance Criteria

1. THE Registry feature SHALL activate at the same block height as quantum addresses (`consensus.nQuantumActivationHeight`)
2. WHILE the current block height is below the quantum activation height, THE Validator SHALL reject transactions with 0x51 or 0x52 marker bytes
3. WHEN the quantum activation height is reached, THE Validator SHALL begin accepting Registration_Transactions and Reference_Transactions
4. THE Registry SHALL use the existing quantum activation height configuration per network (mainnet, testnet, regtest)

### Requirement 10: Error Handling and Logging

**User Story:** As a developer, I want comprehensive error handling and logging, so that I can diagnose issues quickly.

#### Acceptance Criteria

1. WHEN a public key is successfully registered, THE Registry SHALL log the Public_Key_Hash and block height at debug level
2. WHEN a Registry lookup fails, THE Registry SHALL log the requested Public_Key_Hash at warning level
3. WHEN a transaction is rejected due to Registry errors, THE Validator SHALL include the specific error reason in the rejection message
4. IF a write operation fails, THEN THE Registry SHALL return an error without corrupting existing data
5. THE Registry SHALL expose a `GetLastError()` method that returns the most recent error message

