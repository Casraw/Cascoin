// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QUANTUM_REGISTRY_H
#define BITCOIN_QUANTUM_REGISTRY_H

#include <dbwrapper.h>
#include <uint256.h>
#include <sync.h>
#include <fs.h>

#include <memory>
#include <list>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <string>

/**
 * @file quantum_registry.h
 * @brief FALCON-512 Public Key Registry for post-quantum transaction optimization
 *
 * The Registry provides storage optimization for post-quantum signatures by storing
 * FALCON-512 public keys once on-chain and referencing them by hash in subsequent
 * transactions. This reduces transaction size from approximately 1563 bytes to
 * approximately 698 bytes after the initial registration—a savings of approximately 55%.
 *
 * Requirements: 1.1-1.6, 2.1-2.5, 3.1-3.5, 4.1-4.6, 6.1-6.7, 7.1-7.6
 */

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
 * Database key prefix for quantum registry
 */
static const char DB_QUANTUM_PUBKEY = 'Q';

/**
 * Result of parsing a quantum witness
 * Requirements: 4.1-4.6
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
    
    QuantumRegistryStats() : totalKeys(0), databaseSizeBytes(0), cacheHits(0), cacheMisses(0) {}
};

/**
 * Hasher for uint256 to use in unordered_map
 */
struct Uint256Hasher {
    size_t operator()(const uint256& hash) const {
        return hash.GetCheapHash();
    }
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
     * Requirements: 1.1, 1.4
     */
    explicit QuantumPubKeyRegistry(const fs::path& dbPath, 
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
    // Uses std::list for LRU ordering and std::unordered_map for O(1) lookup
    mutable CCriticalSection m_cacheLock;
    std::list<std::pair<uint256, std::vector<unsigned char>>> m_cacheList;
    std::unordered_map<uint256, decltype(m_cacheList)::iterator, Uint256Hasher> m_cacheMap;
    
    // Statistics
    mutable std::atomic<uint64_t> m_cacheHits{0};
    mutable std::atomic<uint64_t> m_cacheMisses{0};
    
    // Error state
    mutable std::string m_lastError;
    bool m_initialized;
    
    // Cache operations
    void AddToCache(const uint256& hash, const std::vector<unsigned char>& pubkey);
    bool LookupCache(const uint256& hash, std::vector<unsigned char>& pubkey) const;
    
    // Count total keys in database
    uint64_t CountKeys() const;
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
 * Rebuild the quantum registry by scanning the blockchain
 * @param datadir Base data directory
 * @param activationHeight Block height at which quantum features activated
 * @param chainTip Current chain tip height
 * @return true on success
 * Requirements: 8.5
 */
bool RebuildQuantumRegistry(const fs::path& datadir, int activationHeight, int chainTip);

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
