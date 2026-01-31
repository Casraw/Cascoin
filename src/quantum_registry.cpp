// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_registry.h>
#include <hash.h>
#include <util.h>
#include <pubkey.h>
#include <chain.h>
#include <chainparams.h>
#include <validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <ui_interface.h>

#include <leveldb/db.h>

/**
 * @file quantum_registry.cpp
 * @brief Implementation of FALCON-512 Public Key Registry
 *
 * Requirements: 1.1-1.6, 2.1-2.5, 3.1-3.5, 6.1-6.7, 10.1-10.5
 */

// Global quantum registry instance
std::unique_ptr<QuantumPubKeyRegistry> g_quantumRegistry;

QuantumPubKeyRegistry::QuantumPubKeyRegistry(const fs::path& dbPath, 
                                             size_t nCacheSize,
                                             bool fMemory, 
                                             bool fWipe)
    : m_initialized(false)
{
    try {
        // Initialize LevelDB at the specified path
        // Requirements: 1.1, 1.4
        m_db = std::make_unique<CDBWrapper>(dbPath, nCacheSize, fMemory, fWipe);
        m_initialized = true;
        LogPrint(BCLog::ALL, "Quantum registry initialized at %s\n", dbPath.string());
    } catch (const std::exception& e) {
        // Requirements: 1.6 - Log error and continue with compact mode disabled
        m_lastError = strprintf("Failed to initialize quantum registry: %s", e.what());
        LogPrintf("ERROR: %s\n", m_lastError);
        m_initialized = false;
    }
}

QuantumPubKeyRegistry::~QuantumPubKeyRegistry()
{
    // Requirements: 1.5 - Graceful shutdown
    if (m_initialized && m_db) {
        Flush();
        LogPrint(BCLog::ALL, "Quantum registry shutdown complete\n");
    }
}

bool QuantumPubKeyRegistry::RegisterPubKey(const std::vector<unsigned char>& pubkey)
{
    // Requirements: 2.5 - Validate public key size
    if (pubkey.size() != QUANTUM_PUBKEY_SIZE) {
        m_lastError = strprintf("Invalid quantum public key size: %d (expected %d)", 
                                pubkey.size(), QUANTUM_PUBKEY_SIZE);
        LogPrint(BCLog::ALL, "Quantum registry: %s\n", m_lastError);
        return false;
    }
    
    if (!m_initialized || !m_db) {
        m_lastError = "Quantum registry not initialized";
        return false;
    }
    
    // Requirements: 2.1 - Compute SHA256 hash of the public key
    uint256 hash = Hash(pubkey.begin(), pubkey.end());
    
    // Requirements: 2.2, 2.3 - Check if already registered (idempotent)
    std::pair<char, uint256> key = std::make_pair(DB_QUANTUM_PUBKEY, hash);
    if (m_db->Exists(key)) {
        // Already registered, this is not an error
        LogPrint(BCLog::ALL, "Quantum pubkey already registered: %s\n", hash.ToString());
        return true;
    }
    
    // Requirements: 2.4 - Store with synchronous writes for durability
    try {
        if (!m_db->Write(key, pubkey, true /* fSync */)) {
            m_lastError = "Failed to write public key to database";
            LogPrintf("ERROR: Quantum registry: %s\n", m_lastError);
            return false;
        }
    } catch (const std::exception& e) {
        m_lastError = strprintf("Database write error: %s", e.what());
        LogPrintf("ERROR: Quantum registry: %s\n", m_lastError);
        return false;
    }
    
    // Add to cache
    AddToCache(hash, pubkey);
    
    // Requirements: 10.1 - Log successful registration at debug level
    LogPrint(BCLog::ALL, "Quantum pubkey registered: %s\n", hash.ToString());
    
    return true;
}

bool QuantumPubKeyRegistry::LookupPubKey(const uint256& hash, std::vector<unsigned char>& pubkey)
{
    if (!m_initialized || !m_db) {
        m_lastError = "Quantum registry not initialized";
        return false;
    }
    
    // Requirements: 6.3 - Check cache first
    if (LookupCache(hash, pubkey)) {
        m_cacheHits++;
        return true;
    }
    
    m_cacheMisses++;
    
    // Requirements: 3.1, 3.5 - Look up from LevelDB (O(1) hash-based access)
    std::pair<char, uint256> key = std::make_pair(DB_QUANTUM_PUBKEY, hash);
    
    try {
        if (!m_db->Read(key, pubkey)) {
            // Requirements: 3.2 - Not found
            m_lastError = "Quantum public key not registered";
            LogPrint(BCLog::ALL, "Quantum registry lookup failed: %s\n", hash.ToString());
            return false;
        }
    } catch (const std::exception& e) {
        // Requirements: 8.4 - Handle checksum/read errors
        m_lastError = strprintf("Database read error: %s", e.what());
        LogPrintf("ERROR: Quantum registry: %s\n", m_lastError);
        return false;
    }
    
    // Requirements: 3.3 - Verify hash integrity on retrieval
    uint256 computedHash = Hash(pubkey.begin(), pubkey.end());
    if (computedHash != hash) {
        // Requirements: 3.4 - Log error and return failure on hash mismatch
        m_lastError = "Hash verification failed on retrieval - data corruption detected";
        LogPrintf("ERROR: Quantum registry: %s (expected %s, got %s)\n", 
                  m_lastError, hash.ToString(), computedHash.ToString());
        pubkey.clear();
        return false;
    }
    
    // Requirements: 6.4 - Add to cache after successful database read
    AddToCache(hash, pubkey);
    
    return true;
}

bool QuantumPubKeyRegistry::IsRegistered(const uint256& hash)
{
    if (!m_initialized || !m_db) {
        return false;
    }
    
    // Check cache first
    {
        LOCK(m_cacheLock);
        if (m_cacheMap.find(hash) != m_cacheMap.end()) {
            return true;
        }
    }
    
    // Check database
    std::pair<char, uint256> key = std::make_pair(DB_QUANTUM_PUBKEY, hash);
    return m_db->Exists(key);
}

QuantumRegistryStats QuantumPubKeyRegistry::GetStats()
{
    QuantumRegistryStats stats;
    
    if (!m_initialized || !m_db) {
        return stats;
    }
    
    // Requirements: 7.3 - Total count of registered public keys
    stats.totalKeys = CountKeys();
    
    // Requirements: 7.4 - Database size estimation
    // Use LevelDB's approximate size estimation
    std::pair<char, uint256> keyBegin = std::make_pair(DB_QUANTUM_PUBKEY, uint256());
    uint256 maxHash;
    memset(maxHash.begin(), 0xFF, 32);
    std::pair<char, uint256> keyEnd = std::make_pair(DB_QUANTUM_PUBKEY, maxHash);
    stats.databaseSizeBytes = m_db->EstimateSize(keyBegin, keyEnd);
    
    // Cache statistics
    stats.cacheHits = m_cacheHits.load();
    stats.cacheMisses = m_cacheMisses.load();
    
    return stats;
}

std::string QuantumPubKeyRegistry::GetLastError() const
{
    return m_lastError;
}

bool QuantumPubKeyRegistry::Flush()
{
    if (!m_initialized || !m_db) {
        return false;
    }
    
    return m_db->Sync();
}

void QuantumPubKeyRegistry::AddToCache(const uint256& hash, const std::vector<unsigned char>& pubkey)
{
    LOCK(m_cacheLock);
    
    // Check if already in cache
    auto it = m_cacheMap.find(hash);
    if (it != m_cacheMap.end()) {
        // Move to front (most recently used)
        m_cacheList.splice(m_cacheList.begin(), m_cacheList, it->second);
        return;
    }
    
    // Requirements: 6.5 - Evict LRU entry if at capacity
    if (m_cacheMap.size() >= QUANTUM_REGISTRY_CACHE_SIZE) {
        // Remove least recently used (back of list)
        auto& lru = m_cacheList.back();
        m_cacheMap.erase(lru.first);
        m_cacheList.pop_back();
    }
    
    // Add new entry at front (most recently used)
    m_cacheList.emplace_front(hash, pubkey);
    m_cacheMap[hash] = m_cacheList.begin();
}

bool QuantumPubKeyRegistry::LookupCache(const uint256& hash, std::vector<unsigned char>& pubkey) const
{
    LOCK(m_cacheLock);
    
    auto it = m_cacheMap.find(hash);
    if (it == m_cacheMap.end()) {
        return false;
    }
    
    // Move to front (most recently used) for proper LRU behavior
    // Requirements: 6.3, 6.4 - Cache lookup with LRU update
    // Using const_cast is safe here because m_cacheList and m_cacheMap are mutable
    // and we hold the lock
    auto& mutableList = const_cast<std::list<std::pair<uint256, std::vector<unsigned char>>>&>(m_cacheList);
    auto& mutableMap = const_cast<std::unordered_map<uint256, decltype(m_cacheList)::iterator, Uint256Hasher>&>(m_cacheMap);
    
    mutableList.splice(mutableList.begin(), mutableList, it->second);
    mutableMap[hash] = mutableList.begin();
    
    pubkey = mutableList.begin()->second;
    return true;
}

uint64_t QuantumPubKeyRegistry::CountKeys() const
{
    if (!m_initialized || !m_db) {
        return 0;
    }
    
    uint64_t count = 0;
    std::unique_ptr<CDBIterator> iter(m_db->NewIterator());
    
    // Seek to first quantum pubkey entry
    std::pair<char, uint256> keyStart = std::make_pair(DB_QUANTUM_PUBKEY, uint256());
    iter->Seek(keyStart);
    
    while (iter->Valid()) {
        std::pair<char, uint256> key;
        if (!iter->GetKey(key)) {
            break;
        }
        
        // Stop if we've passed the quantum pubkey prefix
        if (key.first != DB_QUANTUM_PUBKEY) {
            break;
        }
        
        count++;
        iter->Next();
    }
    
    return count;
}

/**
 * Convenience function to lookup a public key from the global registry
 * This is used by interpreter.cpp to avoid direct dependency on the registry class
 * 
 * This strong definition overrides the weak stub in quantum_consensus.cpp
 */
bool LookupQuantumPubKey(const uint256& hash, std::vector<unsigned char>& pubkey)
{
    if (!g_quantumRegistry) {
        return false;
    }
    return g_quantumRegistry->LookupPubKey(hash, pubkey);
}

// Global initialization and shutdown functions

bool InitQuantumRegistry(const fs::path& datadir)
{
    // Requirements: 1.1 - Database path at {datadir}/quantum_pubkeys
    fs::path dbPath = datadir / "quantum_pubkeys";
    
    try {
        g_quantumRegistry = std::make_unique<QuantumPubKeyRegistry>(dbPath);
        
        if (!g_quantumRegistry->IsInitialized()) {
            // Requirements: 1.6 - Log error and continue with compact mode disabled
            LogPrintf("WARNING: Quantum registry failed to initialize, compact mode disabled\n");
            g_quantumRegistry.reset();
            return false;
        }
        
        LogPrintf("Quantum public key registry initialized\n");
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize quantum registry: %s\n", e.what());
        g_quantumRegistry.reset();
        return false;
    }
}

void ShutdownQuantumRegistry()
{
    // Requirements: 1.5 - Flush pending writes before closing
    if (g_quantumRegistry) {
        g_quantumRegistry->Flush();
        g_quantumRegistry.reset();
        LogPrintf("Quantum public key registry shutdown\n");
    }
}

/**
 * Verify a quantum transaction using the registry
 * 
 * For registration transactions (0x51): uses the included public key directly
 * For reference transactions (0x52): looks up the public key from the registry
 * 
 * Verification steps:
 * 1. Obtain the public key (from witness or registry)
 * 2. Verify SHA256(pubkey) matches the quantum address program
 * 3. Verify the FALCON-512 signature
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5
 */
bool VerifyQuantumTransaction(const QuantumWitnessData& witnessData,
                              const uint256& sighash,
                              const uint256& quantumProgram)
{
    // Witness must be valid
    if (!witnessData.isValid) {
        LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Invalid witness data - %s\n", 
                 witnessData.error);
        return false;
    }
    
    std::vector<unsigned char> pubkey;
    
    // Requirements: 5.1, 5.2 - Obtain the public key based on transaction type
    if (witnessData.isRegistration) {
        // Requirements: 5.1 - For registration, use the included public key directly
        pubkey = witnessData.pubkey;
        
        // Validate public key size
        if (pubkey.size() != QUANTUM_PUBKEY_SIZE) {
            LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Invalid public key size %d (expected %d)\n",
                     pubkey.size(), QUANTUM_PUBKEY_SIZE);
            return false;
        }
    } else {
        // Requirements: 5.2 - For reference, lookup public key from registry
        if (!g_quantumRegistry) {
            LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Quantum registry not initialized\n");
            return false;
        }
        
        if (!g_quantumRegistry->LookupPubKey(witnessData.pubkeyHash, pubkey)) {
            // Requirements: 3.2 - Public key not registered
            LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Public key not registered for hash %s\n",
                     witnessData.pubkeyHash.ToString());
            return false;
        }
    }
    
    // Requirements: 5.4 - Verify SHA256(pubkey) matches quantum address program
    uint256 computedProgram = Hash(pubkey.begin(), pubkey.end());
    if (computedProgram != quantumProgram) {
        // Requirements: 5.5 - Address derivation mismatch
        LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Public key does not match quantum address "
                 "(computed %s, expected %s)\n",
                 computedProgram.ToString(), quantumProgram.ToString());
        return false;
    }
    
    // Requirements: 5.3 - Verify the FALCON-512 signature
    // Create a CPubKey from the quantum public key
    CPubKey quantumPubKey;
    quantumPubKey.SetQuantum(pubkey);
    
    if (!quantumPubKey.IsValid()) {
        LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Failed to create quantum public key\n");
        return false;
    }
    
    // Verify the signature using the quantum public key
    if (!quantumPubKey.Verify(sighash, witnessData.signature)) {
        LogPrint(BCLog::ALL, "VerifyQuantumTransaction: FALCON-512 signature verification failed\n");
        return false;
    }
    
    LogPrint(BCLog::ALL, "VerifyQuantumTransaction: Signature verified successfully\n");
    return true;
}

/**
 * Rebuild the quantum registry by scanning the blockchain
 * 
 * This function scans the blockchain from the quantum activation height to the
 * current chain tip, extracting and registering public keys from all 0x51
 * (registration) witnesses.
 * 
 * Requirements: 8.5
 * 
 * @param datadir Base data directory
 * @param activationHeight Block height at which quantum features activated
 * @param chainTip Current chain tip height
 * @return true on success
 */
bool RebuildQuantumRegistry(const fs::path& datadir, int activationHeight, int chainTip)
{
    LogPrintf("Rebuilding quantum public key registry from block %d to %d...\n", 
              activationHeight, chainTip);
    
    // First, wipe and reinitialize the registry
    ShutdownQuantumRegistry();
    
    // Delete existing database directory
    fs::path dbPath = datadir / "quantum_pubkeys";
    if (fs::exists(dbPath)) {
        try {
            fs::remove_all(dbPath);
            LogPrintf("Deleted existing quantum registry database at %s\n", dbPath.string());
        } catch (const fs::filesystem_error& e) {
            LogPrintf("ERROR: Could not delete quantum registry database: %s\n", e.what());
            return false;
        }
    }
    
    // Reinitialize with fresh database
    if (!InitQuantumRegistry(datadir)) {
        LogPrintf("ERROR: Failed to reinitialize quantum registry\n");
        return false;
    }
    
    if (!g_quantumRegistry || !g_quantumRegistry->IsInitialized()) {
        LogPrintf("ERROR: Quantum registry not properly initialized after rebuild\n");
        return false;
    }
    
    const CChainParams& chainparams = Params();
    uint64_t keysRegistered = 0;
    uint64_t blocksScanned = 0;
    
    // Scan blockchain from activation height to chain tip
    for (int height = activationHeight; height <= chainTip; height++) {
        // Get block index for this height
        CBlockIndex* pindex = nullptr;
        {
            LOCK(cs_main);
            pindex = chainActive[height];
        }
        
        if (!pindex) {
            LogPrintf("WARNING: Block index not found for height %d during quantum registry rebuild\n", height);
            continue;
        }
        
        // Read block from disk
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus())) {
            LogPrintf("WARNING: Failed to read block %d during quantum registry rebuild\n", height);
            continue;
        }
        
        // Scan all transactions in the block
        for (const auto& tx : block.vtx) {
            // Check each input for quantum witnesses
            for (size_t i = 0; i < tx->vin.size(); i++) {
                // Check if this input has a witness
                if (tx->HasWitness() && i < tx->vin.size()) {
                    const CScriptWitness& witness = tx->vin[i].scriptWitness;
                    
                    // Skip empty witnesses
                    if (witness.stack.empty()) {
                        continue;
                    }
                    
                    // Check if this is a quantum witness (first byte is 0x51 or 0x52)
                    const std::vector<unsigned char>& witnessData = witness.stack[0];
                    if (witnessData.empty()) {
                        continue;
                    }
                    
                    uint8_t marker = witnessData[0];
                    
                    // Only process registration transactions (0x51)
                    if (marker == QUANTUM_WITNESS_MARKER_REGISTRATION) {
                        // Parse the quantum witness
                        QuantumWitnessData parsedWitness = ParseQuantumWitness(witness.stack);
                        
                        if (parsedWitness.isValid && parsedWitness.isRegistration) {
                            // Register the public key
                            if (g_quantumRegistry->RegisterPubKey(parsedWitness.pubkey)) {
                                keysRegistered++;
                                LogPrint(BCLog::ALL, "Quantum registry rebuild: Registered pubkey %s from block %d\n",
                                         parsedWitness.pubkeyHash.ToString(), height);
                            }
                        }
                    }
                }
            }
        }
        
        blocksScanned++;
        
        // Progress update every 10000 blocks
        if (blocksScanned % 10000 == 0) {
            LogPrintf("Quantum registry rebuild progress: %d/%d blocks scanned, %d keys registered\n",
                      blocksScanned, chainTip - activationHeight + 1, keysRegistered);
            uiInterface.ShowProgress(_("Rebuilding quantum registry..."), 
                                     (int)(100.0 * blocksScanned / (chainTip - activationHeight + 1)),
                                     false);  // resume_possible = false
        }
    }
    
    // Flush to ensure all writes are persisted
    g_quantumRegistry->Flush();
    
    LogPrintf("Quantum registry rebuild complete: %d blocks scanned, %d keys registered\n",
              blocksScanned, keysRegistered);
    
    return true;
}
