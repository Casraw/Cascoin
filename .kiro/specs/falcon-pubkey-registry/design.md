# Design Document: FALCON-512 Public Key Registry

## Overview

The FALCON-512 Public Key Registry provides storage optimization for post-quantum transactions in Cascoin. By storing 897-byte FALCON-512 public keys once on-chain and referencing them by 32-byte SHA256 hash in subsequent transactions, the system reduces transaction size by approximately 55% (from ~1563 bytes to ~698 bytes).

The Registry integrates with the existing quantum address infrastructure (`address_quantum.h/cpp`) and follows established patterns from the CVM database (`cvmdb.h/cpp`) for LevelDB storage. It activates at the same block height as quantum addresses (`consensus.quantumActivationHeight`).

### Key Design Decisions

1. **Separate LevelDB Database**: The Registry uses its own LevelDB instance at `{datadir}/quantum_pubkeys` rather than sharing with CVM or UTXO databases, ensuring isolation and independent lifecycle management.

2. **Marker Byte Protocol**: Transaction type is determined by a single marker byte (0x51 for registration, 0x52 for reference) at the start of the witness, enabling efficient parsing without full witness inspection.

3. **LRU Cache**: An in-memory cache of 1000 entries reduces database lookups for frequently-used public keys, critical for high-throughput validation.

4. **Immutable Storage**: Once registered, public keys cannot be modified or deleted, ensuring cryptographic integrity and preventing replay attacks.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Transaction Validation                        │
│                         (validation.cpp)                             │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Quantum Witness Parser                          │
│                    (quantum_registry.cpp)                            │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐  │
│  │  0x51 Marker    │    │  0x52 Marker    │    │  Invalid        │  │
│  │  Registration   │    │  Reference      │    │  Reject         │  │
│  └────────┬────────┘    └────────┬────────┘    └─────────────────┘  │
└───────────┼──────────────────────┼──────────────────────────────────┘
            │                      │
            ▼                      ▼
┌─────────────────────┐  ┌─────────────────────┐
│  Extract 897-byte   │  │  Extract 32-byte    │
│  Public Key         │  │  Public Key Hash    │
└─────────┬───────────┘  └─────────┬───────────┘
          │                        │
          ▼                        ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    QuantumPubKeyRegistry                             │
│                   (quantum_registry.h/cpp)                           │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    LRU Cache (1000 entries)                  │    │
│  │              Public_Key_Hash → FALCON-512 Public Key         │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │ miss                                  │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              LevelDB (quantum_pubkeys/)                      │    │
│  │              Public_Key_Hash → FALCON-512 Public Key         │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    FALCON-512 Signature Verification                 │
│                      (crypto/quantum/falcon.cpp)                     │
└─────────────────────────────────────────────────────────────────────┘
```

## Components and Interfaces

### QuantumPubKeyRegistry Class

The main Registry class manages public key storage and retrieval.

```cpp
// src/quantum_registry.h

#ifndef BITCOIN_QUANTUM_REGISTRY_H
#define BITCOIN_QUANTUM_REGISTRY_H

#include <dbwrapper.h>
#include <uint256.h>
#include <pubkey.h>
#include <sync.h>

#include <memory>
#include <list>
#include <unordered_map>

/**
 * Marker bytes for quantum witness format
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5
 */
static const uint8_t QUANTUM_WITNESS_MARKER_REGISTRATION = 0x51;
static const uint8_t QUANTUM_WITNESS_MARKER_REFERENCE = 0x52;

/**
 * Size constants for quantum transactions
 */
static const size_t QUANTUM_PUBKEY_HASH_SIZE = 32;
static const size_t QUANTUM_PUBKEY_SIZE = 897;
static const size_t QUANTUM_MAX_SIGNATURE_SIZE = 700;

/**
 * Cache configuration
 * Requirements: 6.1, 6.2, 6.5
 */
static const size_t QUANTUM_REGISTRY_CACHE_SIZE = 1000;

/**
 * Result of parsing a quantum witness
 */
struct QuantumWitnessData {
    bool isValid;
    bool isRegistration;  // true = 0x51, false = 0x52
    uint256 pubkeyHash;
    std::vector<unsigned char> pubkey;  // Only populated for registration
    std::vector<unsigned char> signature;
    std::string error;
    
    QuantumWitnessData() : isValid(false), isRegistration(false) {}
};

/**
 * Statistics for the quantum public key registry
 * Requirements: 7.3, 7.4
 */
struct QuantumRegistryStats {
    uint64_t totalKeys;
    uint64_t databaseSizeBytes;
    uint64_t cacheHits;
    uint64_t cacheMisses;
};

/**
 * QuantumPubKeyRegistry - LevelDB-backed storage for FALCON-512 public keys
 * 
 * Provides O(1) lookup of public keys by their SHA256 hash, with an LRU cache
 * for performance optimization.
 * 
 * Requirements: 1.1-1.6, 2.1-2.5, 3.1-3.5, 6.1-6.7
 */
class QuantumPubKeyRegistry {
public:
    /**
     * Construct registry with specified database path
     * @param dbPath Path to LevelDB database directory
     * @param nCacheSize LevelDB cache size in bytes
     * @param fMemory Use in-memory database (for testing)
     * @param fWipe Wipe existing database on open
     */
    QuantumPubKeyRegistry(const fs::path& dbPath, 
                          size_t nCacheSize = 2 << 20,
                          bool fMemory = false, 
                          bool fWipe = false);
    ~QuantumPubKeyRegistry();
    
    /**
     * Register a public key in the registry
     * @param pubkey The 897-byte FALCON-512 public key
     * @return true if registered (or already exists), false on error
     * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5
     */
    bool RegisterPubKey(const std::vector<unsigned char>& pubkey);
    
    /**
     * Look up a public key by its hash
     * @param hash The 32-byte SHA256 hash of the public key
     * @param pubkey Output: the full public key if found
     * @return true if found, false if not found or error
     * Requirements: 3.1, 3.2, 3.3, 3.4, 3.5
     */
    bool LookupPubKey(const uint256& hash, std::vector<unsigned char>& pubkey);
    
    /**
     * Check if a public key hash is registered
     * @param hash The 32-byte SHA256 hash
     * @return true if registered
     * Requirements: 7.5
     */
    bool IsRegistered(const uint256& hash);
    
    /**
     * Get registry statistics
     * @return QuantumRegistryStats structure
     * Requirements: 7.3, 7.4
     */
    QuantumRegistryStats GetStats();
    
    /**
     * Get the last error message
     * @return Error string
     * Requirements: 10.5
     */
    std::string GetLastError() const;
    
    /**
     * Flush pending writes to disk
     */
    bool Flush();
    
    /**
     * Check if registry is initialized and operational
     */
    bool IsInitialized() const { return m_initialized; }

private:
    // LevelDB database
    std::unique_ptr<CDBWrapper> m_db;
    
    // LRU cache: hash -> pubkey
    mutable CCriticalSection m_cacheLock;
    std::list<std::pair<uint256, std::vector<unsigned char>>> m_cacheList;
    std::unordered_map<uint256, decltype(m_cacheList)::iterator, SaltedUint256Hasher> m_cacheMap;
    
    // Statistics
    mutable std::atomic<uint64_t> m_cacheHits{0};
    mutable std::atomic<uint64_t> m_cacheMisses{0};
    
    // Error state
    mutable std::string m_lastError;
    bool m_initialized;
    
    // Cache operations
    void AddToCache(const uint256& hash, const std::vector<unsigned char>& pubkey);
    bool LookupCache(const uint256& hash, std::vector<unsigned char>& pubkey) const;
};

/**
 * Global quantum registry instance
 */
extern std::unique_ptr<QuantumPubKeyRegistry> g_quantumRegistry;

/**
 * Initialize the global quantum registry
 * @param datadir Base data directory
 * @return true on success
 * Requirements: 1.4
 */
bool InitQuantumRegistry(const fs::path& datadir);

/**
 * Shutdown the global quantum registry
 * Requirements: 1.5
 */
void ShutdownQuantumRegistry();

/**
 * Parse a quantum transaction witness
 * @param witness The witness stack
 * @return QuantumWitnessData with parsed information
 * Requirements: 4.1-4.6
 */
QuantumWitnessData ParseQuantumWitness(const std::vector<std::vector<unsigned char>>& witness);

/**
 * Verify a quantum transaction using the registry
 * @param witnessData Parsed witness data
 * @param sighash The signature hash to verify
 * @param quantumProgram The expected quantum address program (SHA256 of pubkey)
 * @return true if signature is valid
 * Requirements: 5.1-5.5
 */
bool VerifyQuantumTransaction(const QuantumWitnessData& witnessData,
                              const uint256& sighash,
                              const uint256& quantumProgram);

#endif // BITCOIN_QUANTUM_REGISTRY_H
```

### RPC Interface

```cpp
// RPC commands in src/rpc/quantum.cpp

/**
 * getquantumpubkey "hash"
 * Returns the full FALCON-512 public key for a given hash.
 * 
 * Arguments:
 * 1. hash    (string, required) The 32-byte public key hash (hex)
 * 
 * Result:
 * {
 *   "pubkey": "hex",     (string) The 897-byte public key
 *   "hash": "hex"        (string) The hash (for verification)
 * }
 * 
 * Requirements: 7.1, 7.2
 */
UniValue getquantumpubkey(const JSONRPCRequest& request);

/**
 * getquantumregistrystats
 * Returns statistics about the quantum public key registry.
 * 
 * Result:
 * {
 *   "total_keys": n,           (numeric) Total registered public keys
 *   "database_size": n,        (numeric) Database size in bytes
 *   "cache_hits": n,           (numeric) Cache hit count
 *   "cache_misses": n          (numeric) Cache miss count
 * }
 * 
 * Requirements: 7.3, 7.4
 */
UniValue getquantumregistrystats(const JSONRPCRequest& request);

/**
 * isquantumpubkeyregistered "hash"
 * Checks if a public key hash is registered.
 * 
 * Arguments:
 * 1. hash    (string, required) The 32-byte public key hash (hex)
 * 
 * Result:
 * true|false    (boolean) Whether the key is registered
 * 
 * Requirements: 7.5
 */
UniValue isquantumpubkeyregistered(const JSONRPCRequest& request);
```

## Data Models

### Database Schema

The Registry uses a simple key-value schema in LevelDB:

| Key | Value | Description |
|-----|-------|-------------|
| `Q` + `<32-byte hash>` | `<897-byte pubkey>` | Public key storage |

The `Q` prefix distinguishes quantum registry keys from other potential uses of the database.

### Witness Format

**Registration Transaction (0x51)**:
```
┌─────────┬───────────────────────┬─────────────────────────┐
│ 0x51    │ 897-byte Public Key   │ Up to 700-byte Signature│
│ 1 byte  │ 897 bytes             │ Variable                │
└─────────┴───────────────────────┴─────────────────────────┘
Total: 898 - 1598 bytes
```

**Reference Transaction (0x52)**:
```
┌─────────┬───────────────────────┬─────────────────────────┐
│ 0x52    │ 32-byte Hash          │ Up to 700-byte Signature│
│ 1 byte  │ 32 bytes              │ Variable                │
└─────────┴───────────────────────┴─────────────────────────┘
Total: 33 - 733 bytes
```

### LRU Cache Structure

```cpp
// LRU cache implementation using std::list + std::unordered_map
// - std::list maintains insertion order for LRU eviction
// - std::unordered_map provides O(1) lookup by hash

struct CacheEntry {
    uint256 hash;                        // 32 bytes
    std::vector<unsigned char> pubkey;   // 897 bytes
};
// Total per entry: ~929 bytes
// Cache capacity: 1000 entries
// Maximum memory: ~929 KB
```



## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Registration Round-Trip

*For any* valid 897-byte FALCON-512 public key, registering it in the Registry and then looking it up by its SHA256 hash SHALL return the exact original public key bytes.

**Validates: Requirements 1.2, 1.3, 2.1, 3.1, 7.1**

### Property 2: Hash Integrity on Retrieval

*For any* public key retrieved from the Registry, computing SHA256 of the retrieved key SHALL produce a hash equal to the lookup key used.

**Validates: Requirements 2.1, 3.3**

### Property 3: Registration Idempotence

*For any* valid 897-byte public key, registering it N times (where N >= 1) SHALL have the same observable effect as registering it once: the key is stored, and the total key count increases by exactly 1.

**Validates: Requirements 2.2, 2.3**

### Property 4: Invalid Public Key Size Rejection

*For any* byte sequence that is not exactly 897 bytes, attempting to register it SHALL fail with an error indicating invalid size.

**Validates: Requirements 2.5**

### Property 5: Unregistered Hash Lookup Failure

*For any* 32-byte hash that has not been registered (no public key with that SHA256 hash exists in the Registry), looking it up SHALL return a "not found" result.

**Validates: Requirements 3.2**

### Property 6: Witness Parsing Correctness

*For any* valid quantum witness with marker byte 0x51 followed by 897 bytes of public key data and up to 700 bytes of signature, parsing SHALL extract the correct public key and signature. Similarly, *for any* valid witness with marker 0x52 followed by 32 bytes of hash and up to 700 bytes of signature, parsing SHALL extract the correct hash and signature.

**Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**

### Property 7: Invalid Marker Byte Rejection

*For any* witness where the first byte is neither 0x51 nor 0x52, parsing SHALL fail with an "invalid marker" error.

**Validates: Requirements 4.6**

### Property 8: Address Derivation Verification

*For any* quantum transaction, the SHA256 hash of the public key (whether from witness or Registry) SHALL equal the quantum address program. If they do not match, verification SHALL fail.

**Validates: Requirements 5.4, 5.5**

### Property 9: LRU Cache Eviction

*For any* sequence of N+1 unique public key registrations and lookups where N equals the cache capacity (1000), the least recently accessed entry SHALL be evicted when the (N+1)th entry is added.

**Validates: Requirements 6.5**

### Property 10: Registration Count Accuracy

*For any* sequence of K unique public key registrations, the Registry stats SHALL report exactly K total keys.

**Validates: Requirements 7.3**

### Property 11: Activation Height Enforcement

*For any* block height H below the quantum activation height, quantum witnesses (0x51 or 0x52 markers) SHALL be rejected. *For any* block height H at or above the activation height, valid quantum witnesses SHALL be accepted.

**Validates: Requirements 9.1, 9.2, 9.3**

## Error Handling

### Database Errors

| Error Condition | Behavior | Log Level |
|-----------------|----------|-----------|
| Database fails to open | Continue without compact mode, set `m_initialized = false` | Error |
| Write operation fails | Return false, preserve existing data, set `m_lastError` | Error |
| Read operation fails | Return false, set `m_lastError` | Warning |
| Checksum error on read | Return false, log corruption warning | Error |

### Validation Errors

| Error Condition | Error Message | Action |
|-----------------|---------------|--------|
| Public key size != 897 bytes | "Invalid quantum public key size" | Reject transaction |
| Public key hash not found | "Quantum public key not registered" | Reject transaction |
| Invalid witness marker | "Invalid quantum witness marker" | Reject transaction |
| Address derivation mismatch | "Public key does not match quantum address" | Reject transaction |
| Pre-activation quantum witness | "Quantum features not yet active" | Reject transaction |

### Recovery

The `-rebuildquantumregistry` startup option rebuilds the Registry by scanning the blockchain for all Registration_Transactions (0x51 marker) from the quantum activation height to the current tip. This handles database corruption scenarios.

```cpp
// Rebuild process pseudocode
void RebuildQuantumRegistry() {
    // 1. Wipe existing database
    g_quantumRegistry.reset();
    InitQuantumRegistry(datadir, /*fWipe=*/true);
    
    // 2. Scan blockchain from activation height
    for (height = activationHeight; height <= chainTip; height++) {
        CBlock block;
        ReadBlockFromDisk(block, height);
        
        for (const auto& tx : block.vtx) {
            for (const auto& txin : tx->vin) {
                if (IsQuantumRegistrationWitness(txin.scriptWitness)) {
                    auto pubkey = ExtractPubKey(txin.scriptWitness);
                    g_quantumRegistry->RegisterPubKey(pubkey);
                }
            }
        }
    }
}
```

## Testing Strategy

### Unit Tests

Unit tests verify specific examples and edge cases:

1. **Database initialization**: Verify database opens at correct path
2. **Registration with valid key**: Register a known 897-byte key, verify success
3. **Registration with invalid size**: Attempt to register 896, 898, 0 byte keys
4. **Lookup of registered key**: Register key, lookup by hash, verify match
5. **Lookup of unregistered key**: Lookup random hash, verify not found
6. **Witness parsing examples**: Parse known-good 0x51 and 0x52 witnesses
7. **Invalid marker handling**: Parse witness with 0x50, 0x53 markers
8. **RPC command responses**: Verify JSON structure of RPC responses
9. **Error code verification**: Verify -32001 error for missing keys
10. **Activation height boundary**: Test at height-1, height, height+1

### Property-Based Tests

Property-based tests use the Boost.Test framework with random input generation. Each test runs minimum 100 iterations.

**Test Configuration**:
- Framework: Boost.Test (existing in Cascoin)
- Iterations: 100 minimum per property
- Test file: `src/test/quantum_registry_tests.cpp`

**Property Test Implementation Pattern**:
```cpp
BOOST_AUTO_TEST_CASE(property_round_trip)
{
    // Feature: falcon-pubkey-registry, Property 1: Registration Round-Trip
    for (int i = 0; i < 100; i++) {
        // Generate random 897-byte public key
        std::vector<unsigned char> pubkey(897);
        GetRandBytes(pubkey.data(), pubkey.size());
        
        // Register
        BOOST_CHECK(g_quantumRegistry->RegisterPubKey(pubkey));
        
        // Compute hash
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        
        // Lookup
        std::vector<unsigned char> retrieved;
        BOOST_CHECK(g_quantumRegistry->LookupPubKey(hash, retrieved));
        
        // Verify round-trip
        BOOST_CHECK(pubkey == retrieved);
    }
}
```

### Integration Tests

Integration tests verify end-to-end transaction flows:

1. **Registration transaction flow**: Create quantum address, send first transaction (0x51), verify registration
2. **Reference transaction flow**: After registration, send second transaction (0x52), verify lookup and validation
3. **Mixed transaction sequence**: Interleave registration and reference transactions
4. **Activation boundary**: Attempt quantum transactions before and after activation
5. **Node restart persistence**: Register keys, restart node, verify keys persist

### Test File Organization

```
src/test/
├── quantum_registry_tests.cpp    # Unit and property tests for Registry
├── quantum_witness_tests.cpp     # Witness parsing tests
└── quantum_rpc_tests.cpp         # RPC command tests

test/functional/
├── feature_quantum_registry.py   # Integration tests
└── rpc_quantum_registry.py       # RPC functional tests
```
