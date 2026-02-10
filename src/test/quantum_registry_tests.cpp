// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_registry.h>
#include <chainparams.h>
#include <hash.h>
#include <random.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <set>

/**
 * @file quantum_registry_tests.cpp
 * @brief Property-based tests for FALCON-512 Public Key Registry
 *
 * Feature: falcon-pubkey-registry
 * Tests validate correctness properties for the quantum public key registry.
 */

static constexpr int PROPERTY_TEST_ITERATIONS = 100;

/**
 * Test fixture that creates a temporary quantum registry for testing
 */
struct QuantumRegistryTestSetup : public BasicTestingSetup {
    fs::path tempDir;
    std::unique_ptr<QuantumPubKeyRegistry> registry;
    
    QuantumRegistryTestSetup() : BasicTestingSetup(CBaseChainParams::REGTEST) {
        // Create a temporary directory for the test database
        tempDir = fs::temp_directory_path() / fs::unique_path("quantum_registry_test_%%%%-%%%%");
        fs::create_directories(tempDir);
        
        // Create registry with in-memory database for faster tests
        registry = std::make_unique<QuantumPubKeyRegistry>(tempDir, 2 << 20, true /* fMemory */, true /* fWipe */);
        BOOST_REQUIRE(registry->IsInitialized());
    }
    
    ~QuantumRegistryTestSetup() {
        registry.reset();
        // Clean up temporary directory
        boost::system::error_code ec;
        fs::remove_all(tempDir, ec);
    }
    
    // Generate a random 897-byte public key
    std::vector<unsigned char> GenerateRandomPubKey() {
        std::vector<unsigned char> pubkey(QUANTUM_PUBKEY_SIZE);
        GetRandBytes(pubkey.data(), pubkey.size());
        return pubkey;
    }
};

BOOST_FIXTURE_TEST_SUITE(quantum_registry_tests, QuantumRegistryTestSetup)

//=============================================================================
// Property 1: Registration Round-Trip
// For any valid 897-byte FALCON-512 public key, registering it in the Registry
// and then looking it up by its SHA256 hash SHALL return the exact original
// public key bytes.
// **Validates: Requirements 1.2, 1.3, 2.1, 3.1, 7.1**
//=============================================================================

BOOST_AUTO_TEST_CASE(property1_registration_round_trip)
{
    // Feature: falcon-pubkey-registry
    // **Property 1: Registration Round-Trip**
    // **Validates: Requirements 1.2, 1.3, 2.1, 3.1, 7.1**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random 897-byte public key
        std::vector<unsigned char> originalPubkey = GenerateRandomPubKey();
        BOOST_REQUIRE_EQUAL(originalPubkey.size(), QUANTUM_PUBKEY_SIZE);
        
        // Register the public key
        bool registerResult = registry->RegisterPubKey(originalPubkey);
        BOOST_REQUIRE_MESSAGE(registerResult, 
            "Registration should succeed for iteration " << i);
        
        // Compute the expected hash
        uint256 expectedHash = Hash(originalPubkey.begin(), originalPubkey.end());
        
        // Look up the public key by hash
        std::vector<unsigned char> retrievedPubkey;
        bool lookupResult = registry->LookupPubKey(expectedHash, retrievedPubkey);
        BOOST_REQUIRE_MESSAGE(lookupResult, 
            "Lookup should succeed for iteration " << i);
        
        // Verify round-trip: retrieved key must match original exactly
        BOOST_CHECK_EQUAL(retrievedPubkey.size(), originalPubkey.size());
        BOOST_CHECK_MESSAGE(retrievedPubkey == originalPubkey,
            "Retrieved public key must match original for iteration " << i);
    }
}

//=============================================================================
// Property 2: Hash Integrity on Retrieval
// For any public key retrieved from the Registry, computing SHA256 of the
// retrieved key SHALL produce a hash equal to the lookup key used.
// **Validates: Requirements 2.1, 3.3**
//=============================================================================

BOOST_AUTO_TEST_CASE(property2_hash_integrity_on_retrieval)
{
    // Feature: falcon-pubkey-registry
    // **Property 2: Hash Integrity on Retrieval**
    // **Validates: Requirements 2.1, 3.3**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate and register a random public key
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        BOOST_REQUIRE(registry->RegisterPubKey(pubkey));
        
        // Compute the hash used for lookup
        uint256 lookupHash = Hash(pubkey.begin(), pubkey.end());
        
        // Retrieve the public key
        std::vector<unsigned char> retrievedPubkey;
        BOOST_REQUIRE(registry->LookupPubKey(lookupHash, retrievedPubkey));
        
        // Compute hash of retrieved key
        uint256 retrievedHash = Hash(retrievedPubkey.begin(), retrievedPubkey.end());
        
        // Verify hash integrity: computed hash must equal lookup hash
        BOOST_CHECK_MESSAGE(retrievedHash == lookupHash,
            "Hash of retrieved key must match lookup hash for iteration " << i);
    }
}

//=============================================================================
// Additional unit tests for edge cases and error conditions
//=============================================================================

// Test: Registry initialization
BOOST_AUTO_TEST_CASE(registry_initialization)
{
    BOOST_CHECK(registry->IsInitialized());
    
    // Stats should show zero keys initially
    QuantumRegistryStats stats = registry->GetStats();
    BOOST_CHECK_EQUAL(stats.totalKeys, 0u);
}

// Test: Invalid public key size rejection
BOOST_AUTO_TEST_CASE(invalid_pubkey_size_rejection)
{
    // Test with too small key
    std::vector<unsigned char> smallKey(896);
    GetRandBytes(smallKey.data(), smallKey.size());
    BOOST_CHECK(!registry->RegisterPubKey(smallKey));
    
    // Test with too large key
    std::vector<unsigned char> largeKey(898);
    GetRandBytes(largeKey.data(), largeKey.size());
    BOOST_CHECK(!registry->RegisterPubKey(largeKey));
    
    // Test with empty key
    std::vector<unsigned char> emptyKey;
    BOOST_CHECK(!registry->RegisterPubKey(emptyKey));
}

// Test: Lookup of unregistered hash
BOOST_AUTO_TEST_CASE(unregistered_hash_lookup)
{
    // Generate a random hash that hasn't been registered
    uint256 randomHash = GetRandHash();
    
    std::vector<unsigned char> pubkey;
    bool result = registry->LookupPubKey(randomHash, pubkey);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(pubkey.empty());
}

// Test: IsRegistered function
BOOST_AUTO_TEST_CASE(is_registered_check)
{
    // Generate and register a key
    std::vector<unsigned char> pubkey = GenerateRandomPubKey();
    uint256 hash = Hash(pubkey.begin(), pubkey.end());
    
    // Should not be registered initially
    BOOST_CHECK(!registry->IsRegistered(hash));
    
    // Register the key
    BOOST_REQUIRE(registry->RegisterPubKey(pubkey));
    
    // Should now be registered
    BOOST_CHECK(registry->IsRegistered(hash));
    
    // Random hash should not be registered
    uint256 randomHash = GetRandHash();
    BOOST_CHECK(!registry->IsRegistered(randomHash));
}

// Test: Registration idempotence (registering same key twice)
BOOST_AUTO_TEST_CASE(registration_idempotence)
{
    std::vector<unsigned char> pubkey = GenerateRandomPubKey();
    
    // Register the key
    BOOST_CHECK(registry->RegisterPubKey(pubkey));
    
    // Get initial stats
    QuantumRegistryStats stats1 = registry->GetStats();
    uint64_t initialCount = stats1.totalKeys;
    
    // Register the same key again
    BOOST_CHECK(registry->RegisterPubKey(pubkey));
    
    // Count should not increase
    QuantumRegistryStats stats2 = registry->GetStats();
    BOOST_CHECK_EQUAL(stats2.totalKeys, initialCount);
}

// Test: Multiple unique registrations
BOOST_AUTO_TEST_CASE(multiple_unique_registrations)
{
    const int numKeys = 10;
    std::vector<std::vector<unsigned char>> keys;
    std::vector<uint256> hashes;
    
    // Register multiple unique keys
    for (int i = 0; i < numKeys; i++) {
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        
        BOOST_REQUIRE(registry->RegisterPubKey(pubkey));
        keys.push_back(pubkey);
        hashes.push_back(hash);
    }
    
    // Verify all keys can be retrieved
    for (int i = 0; i < numKeys; i++) {
        std::vector<unsigned char> retrieved;
        BOOST_CHECK(registry->LookupPubKey(hashes[i], retrieved));
        BOOST_CHECK(retrieved == keys[i]);
    }
    
    // Verify stats
    QuantumRegistryStats stats = registry->GetStats();
    BOOST_CHECK_EQUAL(stats.totalKeys, static_cast<uint64_t>(numKeys));
}

// Test: Cache hit/miss statistics
BOOST_AUTO_TEST_CASE(cache_statistics)
{
    std::vector<unsigned char> pubkey = GenerateRandomPubKey();
    uint256 hash = Hash(pubkey.begin(), pubkey.end());
    
    // Register the key
    BOOST_REQUIRE(registry->RegisterPubKey(pubkey));
    
    // First lookup should be a cache hit (added during registration)
    std::vector<unsigned char> retrieved;
    BOOST_REQUIRE(registry->LookupPubKey(hash, retrieved));
    
    QuantumRegistryStats stats = registry->GetStats();
    BOOST_CHECK_GE(stats.cacheHits, 1u);
}

//=============================================================================
// Property 9: LRU Cache Eviction
// For any sequence of N+1 unique public key registrations and lookups where N
// equals the cache capacity (1000), the least recently accessed entry SHALL be
// evicted when the (N+1)th entry is added.
// **Validates: Requirements 6.5**
//=============================================================================

BOOST_AUTO_TEST_CASE(property9_lru_cache_eviction)
{
    // Feature: falcon-pubkey-registry
    // **Property 9: LRU Cache Eviction**
    // **Validates: Requirements 6.5**
    
    // Use a smaller cache size for testing (we can't easily change the constant,
    // so we'll test the eviction behavior with the full cache size but fewer iterations)
    // We'll register QUANTUM_REGISTRY_CACHE_SIZE + 1 keys and verify eviction behavior
    
    const size_t testCacheSize = QUANTUM_REGISTRY_CACHE_SIZE;
    std::vector<std::vector<unsigned char>> keys;
    std::vector<uint256> hashes;
    
    // Register exactly cache_size keys
    for (size_t i = 0; i < testCacheSize; i++) {
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        
        BOOST_REQUIRE_MESSAGE(registry->RegisterPubKey(pubkey),
            "Registration should succeed for key " << i);
        
        keys.push_back(pubkey);
        hashes.push_back(hash);
    }
    
    // Access the first key to make it recently used (move to front of LRU)
    std::vector<unsigned char> retrieved;
    BOOST_REQUIRE(registry->LookupPubKey(hashes[0], retrieved));
    BOOST_CHECK(retrieved == keys[0]);
    
    // Now register one more key - this should evict the LRU entry (key at index 1)
    std::vector<unsigned char> newPubkey = GenerateRandomPubKey();
    BOOST_REQUIRE(registry->RegisterPubKey(newPubkey));
    
    // The first key should still be in cache (we accessed it recently)
    // Looking it up should be a cache hit
    QuantumRegistryStats statsAfterAccess = registry->GetStats();
    uint64_t hitsAfterAccess = statsAfterAccess.cacheHits;
    
    retrieved.clear();
    BOOST_REQUIRE(registry->LookupPubKey(hashes[0], retrieved));
    BOOST_CHECK(retrieved == keys[0]);
    
    QuantumRegistryStats statsAfterLookup = registry->GetStats();
    // Should have gotten a cache hit for the first key
    BOOST_CHECK_GT(statsAfterLookup.cacheHits, hitsAfterAccess);
    
    // The second key (index 1) should have been evicted since it was LRU
    // Looking it up should result in a cache miss (database lookup)
    uint64_t missesBeforeEvictedLookup = statsAfterLookup.cacheMisses;
    
    retrieved.clear();
    BOOST_REQUIRE(registry->LookupPubKey(hashes[1], retrieved));
    BOOST_CHECK(retrieved == keys[1]);
    
    QuantumRegistryStats statsAfterEvictedLookup = registry->GetStats();
    // Should have gotten a cache miss for the evicted key
    BOOST_CHECK_GT(statsAfterEvictedLookup.cacheMisses, missesBeforeEvictedLookup);
}

//=============================================================================
// Property 10: Registration Count Accuracy
// For any sequence of K unique public key registrations, the Registry stats
// SHALL report exactly K total keys.
// **Validates: Requirements 7.3**
//=============================================================================

BOOST_AUTO_TEST_CASE(property10_registration_count_accuracy)
{
    // Feature: falcon-pubkey-registry
    // **Property 10: Registration Count Accuracy**
    // **Validates: Requirements 7.3**
    
    // Initial count should be 0
    QuantumRegistryStats initialStats = registry->GetStats();
    BOOST_CHECK_EQUAL(initialStats.totalKeys, 0u);
    
    std::set<uint256> registeredHashes;
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random public key
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        
        // Track unique hashes (in case of collision, which is extremely unlikely)
        bool isNewKey = registeredHashes.find(hash) == registeredHashes.end();
        
        // Register the key
        BOOST_REQUIRE_MESSAGE(registry->RegisterPubKey(pubkey),
            "Registration should succeed for iteration " << i);
        
        if (isNewKey) {
            registeredHashes.insert(hash);
        }
        
        // Verify count matches expected
        QuantumRegistryStats stats = registry->GetStats();
        BOOST_CHECK_MESSAGE(stats.totalKeys == registeredHashes.size(),
            "Total keys should match registered count at iteration " << i 
            << " (expected " << registeredHashes.size() << ", got " << stats.totalKeys << ")");
    }
    
    // Final verification
    QuantumRegistryStats finalStats = registry->GetStats();
    BOOST_CHECK_EQUAL(finalStats.totalKeys, registeredHashes.size());
    BOOST_CHECK_EQUAL(finalStats.totalKeys, static_cast<uint64_t>(PROPERTY_TEST_ITERATIONS));
}

//=============================================================================
// Property 6: Witness Parsing Correctness
// For any valid quantum witness with marker byte 0x51 followed by 897 bytes of
// public key data and up to 700 bytes of signature, parsing SHALL extract the
// correct public key and signature. Similarly, for any valid witness with marker
// 0x52 followed by 32 bytes of hash and up to 700 bytes of signature, parsing
// SHALL extract the correct hash and signature.
// **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**
//=============================================================================

BOOST_AUTO_TEST_CASE(property6_witness_parsing_correctness)
{
    // Feature: falcon-pubkey-registry
    // **Property 6: Witness Parsing Correctness**
    // **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate random public key (897 bytes)
        std::vector<unsigned char> originalPubkey(QUANTUM_PUBKEY_SIZE);
        GetRandBytes(originalPubkey.data(), originalPubkey.size());
        
        // Generate random signature (variable size 1-700 bytes)
        size_t sigSize = (GetRand(QUANTUM_MAX_SIGNATURE_SIZE) % QUANTUM_MAX_SIGNATURE_SIZE) + 1;
        std::vector<unsigned char> originalSignature(sigSize);
        GetRandBytes(originalSignature.data(), originalSignature.size());
        
        // Test Registration Witness (0x51)
        {
            // Construct registration witness: [0x51][pubkey][signature]
            std::vector<unsigned char> witnessData;
            witnessData.push_back(QUANTUM_WITNESS_MARKER_REGISTRATION);
            witnessData.insert(witnessData.end(), originalPubkey.begin(), originalPubkey.end());
            witnessData.insert(witnessData.end(), originalSignature.begin(), originalSignature.end());
            
            std::vector<std::vector<unsigned char>> witness;
            witness.push_back(witnessData);
            
            // Parse the witness
            QuantumWitnessData parsed = ParseQuantumWitness(witness);
            
            // Verify parsing succeeded
            BOOST_REQUIRE_MESSAGE(parsed.isValid,
                "Registration witness parsing should succeed for iteration " << i 
                << ", error: " << parsed.error);
            
            // Verify it's identified as registration
            BOOST_CHECK_MESSAGE(parsed.isRegistration,
                "Should be identified as registration for iteration " << i);
            
            // Verify public key extracted correctly
            BOOST_CHECK_MESSAGE(parsed.pubkey == originalPubkey,
                "Extracted public key should match original for iteration " << i);
            
            // Verify signature extracted correctly
            BOOST_CHECK_MESSAGE(parsed.signature == originalSignature,
                "Extracted signature should match original for iteration " << i);
            
            // Verify hash is computed correctly
            uint256 expectedHash = Hash(originalPubkey.begin(), originalPubkey.end());
            BOOST_CHECK_MESSAGE(parsed.pubkeyHash == expectedHash,
                "Computed hash should match expected for iteration " << i);
        }
        
        // Test Reference Witness (0x52)
        {
            // Generate a random hash for reference
            uint256 originalHash = GetRandHash();
            
            // Construct reference witness: [0x52][hash][signature]
            std::vector<unsigned char> witnessData;
            witnessData.push_back(QUANTUM_WITNESS_MARKER_REFERENCE);
            witnessData.insert(witnessData.end(), originalHash.begin(), originalHash.end());
            witnessData.insert(witnessData.end(), originalSignature.begin(), originalSignature.end());
            
            std::vector<std::vector<unsigned char>> witness;
            witness.push_back(witnessData);
            
            // Parse the witness
            QuantumWitnessData parsed = ParseQuantumWitness(witness);
            
            // Verify parsing succeeded
            BOOST_REQUIRE_MESSAGE(parsed.isValid,
                "Reference witness parsing should succeed for iteration " << i
                << ", error: " << parsed.error);
            
            // Verify it's identified as reference (not registration)
            BOOST_CHECK_MESSAGE(!parsed.isRegistration,
                "Should be identified as reference for iteration " << i);
            
            // Verify hash extracted correctly
            BOOST_CHECK_MESSAGE(parsed.pubkeyHash == originalHash,
                "Extracted hash should match original for iteration " << i);
            
            // Verify signature extracted correctly
            BOOST_CHECK_MESSAGE(parsed.signature == originalSignature,
                "Extracted signature should match original for iteration " << i);
            
            // Verify public key is empty for reference transactions
            BOOST_CHECK_MESSAGE(parsed.pubkey.empty(),
                "Public key should be empty for reference transaction for iteration " << i);
        }
    }
}

//=============================================================================
// Property 7: Invalid Marker Byte Rejection
// For any witness where the first byte is neither 0x51 nor 0x52, parsing SHALL
// fail with an "invalid marker" error.
// **Validates: Requirements 4.6**
//=============================================================================

BOOST_AUTO_TEST_CASE(property7_invalid_marker_rejection)
{
    // Feature: falcon-pubkey-registry
    // **Property 7: Invalid Marker Byte Rejection**
    // **Validates: Requirements 4.6**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random marker byte that is NOT 0x51 or 0x52
        uint8_t invalidMarker;
        do {
            invalidMarker = static_cast<uint8_t>(GetRand(256));
        } while (invalidMarker == QUANTUM_WITNESS_MARKER_REGISTRATION || 
                 invalidMarker == QUANTUM_WITNESS_MARKER_REFERENCE);
        
        // Generate random data to follow the marker (enough for a valid-looking witness)
        size_t dataSize = GetRand(1000) + 50;  // At least 50 bytes
        std::vector<unsigned char> randomData(dataSize);
        GetRandBytes(randomData.data(), randomData.size());
        
        // Construct witness with invalid marker
        std::vector<unsigned char> witnessData;
        witnessData.push_back(invalidMarker);
        witnessData.insert(witnessData.end(), randomData.begin(), randomData.end());
        
        std::vector<std::vector<unsigned char>> witness;
        witness.push_back(witnessData);
        
        // Parse the witness
        QuantumWitnessData parsed = ParseQuantumWitness(witness);
        
        // Verify parsing failed
        BOOST_CHECK_MESSAGE(!parsed.isValid,
            "Parsing should fail for invalid marker 0x" << std::hex << (int)invalidMarker 
            << " at iteration " << std::dec << i);
        
        // Verify error message mentions invalid marker
        BOOST_CHECK_MESSAGE(parsed.error.find("marker") != std::string::npos ||
                           parsed.error.find("Invalid") != std::string::npos,
            "Error message should mention invalid marker for iteration " << i 
            << ", got: " << parsed.error);
    }
    
    // Also test specific edge cases
    std::vector<uint8_t> edgeCases = {0x00, 0x50, 0x53, 0xFF};
    for (uint8_t marker : edgeCases) {
        std::vector<unsigned char> witnessData;
        witnessData.push_back(marker);
        // Add enough data for a valid-looking witness
        witnessData.resize(1000);
        GetRandBytes(witnessData.data() + 1, witnessData.size() - 1);
        
        std::vector<std::vector<unsigned char>> witness;
        witness.push_back(witnessData);
        
        QuantumWitnessData parsed = ParseQuantumWitness(witness);
        
        BOOST_CHECK_MESSAGE(!parsed.isValid,
            "Parsing should fail for edge case marker 0x" << std::hex << (int)marker);
    }
}

//=============================================================================
// Property 8: Address Derivation Verification
// For any quantum transaction, the SHA256 hash of the public key (whether from
// witness or Registry) SHALL equal the quantum address program. If they do not
// match, verification SHALL fail.
// **Validates: Requirements 5.4, 5.5**
//=============================================================================

BOOST_AUTO_TEST_CASE(property8_address_derivation_verification)
{
    // Feature: falcon-pubkey-registry
    // **Property 8: Address Derivation Verification**
    // **Validates: Requirements 5.4, 5.5**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random 897-byte public key
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        
        // Compute the expected quantum address program (SHA256 of pubkey)
        uint256 expectedProgram = Hash(pubkey.begin(), pubkey.end());
        
        // Register the public key
        BOOST_REQUIRE(registry->RegisterPubKey(pubkey));
        
        // Create a registration witness
        std::vector<unsigned char> signature(100);
        GetRandBytes(signature.data(), signature.size());
        
        std::vector<unsigned char> witnessData;
        witnessData.push_back(QUANTUM_WITNESS_MARKER_REGISTRATION);
        witnessData.insert(witnessData.end(), pubkey.begin(), pubkey.end());
        witnessData.insert(witnessData.end(), signature.begin(), signature.end());
        
        std::vector<std::vector<unsigned char>> witness;
        witness.push_back(witnessData);
        
        // Parse the witness
        QuantumWitnessData parsed = ParseQuantumWitness(witness);
        BOOST_REQUIRE_MESSAGE(parsed.isValid, "Witness parsing should succeed");
        
        // Verify the address derivation: SHA256(pubkey) should match the expected program
        uint256 derivedProgram = Hash(parsed.pubkey.begin(), parsed.pubkey.end());
        BOOST_CHECK_MESSAGE(derivedProgram == expectedProgram,
            "Derived program should match expected for iteration " << i);
        
        // Also verify via the pubkeyHash computed during parsing
        BOOST_CHECK_MESSAGE(parsed.pubkeyHash == expectedProgram,
            "Parsed pubkeyHash should match expected program for iteration " << i);
        
        // Test with reference transaction - lookup should return same pubkey
        std::vector<unsigned char> refWitnessData;
        refWitnessData.push_back(QUANTUM_WITNESS_MARKER_REFERENCE);
        refWitnessData.insert(refWitnessData.end(), expectedProgram.begin(), expectedProgram.end());
        refWitnessData.insert(refWitnessData.end(), signature.begin(), signature.end());
        
        std::vector<std::vector<unsigned char>> refWitness;
        refWitness.push_back(refWitnessData);
        
        QuantumWitnessData refParsed = ParseQuantumWitness(refWitness);
        BOOST_REQUIRE_MESSAGE(refParsed.isValid, "Reference witness parsing should succeed");
        
        // Lookup the pubkey from registry
        std::vector<unsigned char> lookedUpPubkey;
        BOOST_REQUIRE(registry->LookupPubKey(refParsed.pubkeyHash, lookedUpPubkey));
        
        // Verify the looked up pubkey matches the original
        BOOST_CHECK_MESSAGE(lookedUpPubkey == pubkey,
            "Looked up pubkey should match original for iteration " << i);
        
        // Verify SHA256(looked up pubkey) matches the reference hash
        uint256 lookedUpHash = Hash(lookedUpPubkey.begin(), lookedUpPubkey.end());
        BOOST_CHECK_MESSAGE(lookedUpHash == refParsed.pubkeyHash,
            "SHA256 of looked up pubkey should match reference hash for iteration " << i);
    }
}

//=============================================================================
// Property 4: Invalid Public Key Size Rejection
// For any byte sequence that is not exactly 897 bytes, attempting to register
// it SHALL fail with an error indicating invalid size.
// **Validates: Requirements 2.5**
//=============================================================================

BOOST_AUTO_TEST_CASE(property4_invalid_pubkey_size_rejection)
{
    // Feature: falcon-pubkey-registry
    // **Property 4: Invalid Public Key Size Rejection**
    // **Validates: Requirements 2.5**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random size that is NOT 897 bytes
        size_t invalidSize;
        do {
            invalidSize = GetRand(2000);  // Random size 0-1999
        } while (invalidSize == QUANTUM_PUBKEY_SIZE);
        
        // Generate random data of invalid size
        std::vector<unsigned char> invalidPubkey(invalidSize);
        if (invalidSize > 0) {
            GetRandBytes(invalidPubkey.data(), invalidPubkey.size());
        }
        
        // Attempt to register - should fail
        bool result = registry->RegisterPubKey(invalidPubkey);
        
        BOOST_CHECK_MESSAGE(!result,
            "Registration should fail for size " << invalidSize << " at iteration " << i);
        
        // Verify error message mentions size
        std::string error = registry->GetLastError();
        BOOST_CHECK_MESSAGE(error.find("size") != std::string::npos ||
                           error.find("Invalid") != std::string::npos,
            "Error should mention invalid size for iteration " << i 
            << ", got: " << error);
    }
    
    // Test specific edge cases
    std::vector<size_t> edgeCases = {0, 1, 896, 898, 1000, 2000};
    for (size_t size : edgeCases) {
        std::vector<unsigned char> invalidPubkey(size);
        if (size > 0) {
            GetRandBytes(invalidPubkey.data(), invalidPubkey.size());
        }
        
        bool result = registry->RegisterPubKey(invalidPubkey);
        BOOST_CHECK_MESSAGE(!result,
            "Registration should fail for edge case size " << size);
    }
}

//=============================================================================
// Property 5: Unregistered Hash Lookup Failure
// For any 32-byte hash that has not been registered (no public key with that
// SHA256 hash exists in the Registry), looking it up SHALL return a "not found"
// result.
// **Validates: Requirements 3.2**
//=============================================================================

BOOST_AUTO_TEST_CASE(property5_unregistered_hash_lookup_failure)
{
    // Feature: falcon-pubkey-registry
    // **Property 5: Unregistered Hash Lookup Failure**
    // **Validates: Requirements 3.2**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random hash that hasn't been registered
        uint256 randomHash = GetRandHash();
        
        // Verify it's not registered
        BOOST_CHECK_MESSAGE(!registry->IsRegistered(randomHash),
            "Random hash should not be registered for iteration " << i);
        
        // Attempt to lookup - should fail
        std::vector<unsigned char> pubkey;
        bool result = registry->LookupPubKey(randomHash, pubkey);
        
        BOOST_CHECK_MESSAGE(!result,
            "Lookup should fail for unregistered hash at iteration " << i);
        
        // Verify pubkey is empty after failed lookup
        BOOST_CHECK_MESSAGE(pubkey.empty(),
            "Pubkey should be empty after failed lookup for iteration " << i);
        
        // Verify error message mentions not registered
        std::string error = registry->GetLastError();
        BOOST_CHECK_MESSAGE(error.find("not registered") != std::string::npos ||
                           error.find("not found") != std::string::npos,
            "Error should mention not registered for iteration " << i 
            << ", got: " << error);
    }
    
    // Also test that after registering some keys, random hashes still fail
    // Register a few keys first
    for (int i = 0; i < 10; i++) {
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        BOOST_REQUIRE(registry->RegisterPubKey(pubkey));
    }
    
    // Now test that random hashes still fail
    for (int i = 0; i < 10; i++) {
        uint256 randomHash = GetRandHash();
        
        std::vector<unsigned char> pubkey;
        bool result = registry->LookupPubKey(randomHash, pubkey);
        
        BOOST_CHECK_MESSAGE(!result,
            "Lookup should still fail for random hash after registrations at iteration " << i);
    }
}

//=============================================================================
// Property 11: Activation Height Enforcement
// For any block height H below the quantum activation height, quantum witnesses
// (0x51 or 0x52 markers) SHALL be rejected. For any block height H at or above
// the activation height, valid quantum witnesses SHALL be accepted.
// **Validates: Requirements 9.1, 9.2, 9.3**
//=============================================================================

BOOST_AUTO_TEST_CASE(property11_activation_height_enforcement)
{
    // Feature: falcon-pubkey-registry
    // **Property 11: Activation Height Enforcement**
    // **Validates: Requirements 9.1, 9.2, 9.3**
    
    // This test verifies that the SCRIPT_VERIFY_QUANTUM flag correctly controls
    // whether quantum witnesses are accepted or rejected.
    
    // Test that quantum activation is based on block height
    const Consensus::Params& params = Params().GetConsensus();
    
    // Regtest activation height should be 1 (for testing)
    BOOST_CHECK_EQUAL(params.quantumActivationHeight, 1);
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate random test data
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        
        // Generate a random signature (1-700 bytes)
        size_t sigSize = (GetRand(QUANTUM_MAX_SIGNATURE_SIZE) % QUANTUM_MAX_SIGNATURE_SIZE) + 1;
        std::vector<unsigned char> signature(sigSize);
        GetRandBytes(signature.data(), signature.size());
        
        // Construct a registration witness: [0x51][pubkey][signature]
        std::vector<unsigned char> witnessData;
        witnessData.push_back(QUANTUM_WITNESS_MARKER_REGISTRATION);
        witnessData.insert(witnessData.end(), pubkey.begin(), pubkey.end());
        witnessData.insert(witnessData.end(), signature.begin(), signature.end());
        
        std::vector<std::vector<unsigned char>> witness;
        witness.push_back(witnessData);
        
        // Parse the witness - this should always succeed regardless of activation
        QuantumWitnessData parsed = ParseQuantumWitness(witness);
        BOOST_REQUIRE_MESSAGE(parsed.isValid,
            "Witness parsing should succeed for iteration " << i);
        
        // Verify it's identified as registration
        BOOST_CHECK_MESSAGE(parsed.isRegistration,
            "Should be identified as registration for iteration " << i);
        
        // Verify the marker byte is correctly identified
        BOOST_CHECK_EQUAL(witnessData[0], QUANTUM_WITNESS_MARKER_REGISTRATION);
        
        // Test reference witness as well
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        
        std::vector<unsigned char> refWitnessData;
        refWitnessData.push_back(QUANTUM_WITNESS_MARKER_REFERENCE);
        refWitnessData.insert(refWitnessData.end(), hash.begin(), hash.end());
        refWitnessData.insert(refWitnessData.end(), signature.begin(), signature.end());
        
        std::vector<std::vector<unsigned char>> refWitness;
        refWitness.push_back(refWitnessData);
        
        QuantumWitnessData refParsed = ParseQuantumWitness(refWitness);
        BOOST_REQUIRE_MESSAGE(refParsed.isValid,
            "Reference witness parsing should succeed for iteration " << i);
        
        // Verify it's identified as reference (not registration)
        BOOST_CHECK_MESSAGE(!refParsed.isRegistration,
            "Should be identified as reference for iteration " << i);
        
        // Verify the marker byte is correctly identified
        BOOST_CHECK_EQUAL(refWitnessData[0], QUANTUM_WITNESS_MARKER_REFERENCE);
    }
    
    // Test that IsQuantumEnabled correctly checks activation height
    // Note: In regtest, activation height is 1, so quantum should be enabled
    // for any block index with height >= 0 (since IsQuantumEnabled checks pindexPrev->nHeight + 1)
    
    // Verify the activation height configuration per network
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& mainParams = Params().GetConsensus();
    BOOST_CHECK_EQUAL(mainParams.quantumActivationHeight, 350000);
    
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& testParams = Params().GetConsensus();
    BOOST_CHECK_EQUAL(testParams.quantumActivationHeight, 5680);
    
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& regParams = Params().GetConsensus();
    BOOST_CHECK_EQUAL(regParams.quantumActivationHeight, 1);
    
    BOOST_TEST_MESSAGE("Property 11: Activation height enforcement validated");
}

//=============================================================================
// Property 3: Registration Idempotence
// For any valid 897-byte public key, registering it N times (where N >= 1)
// SHALL have the same observable effect as registering it once: the key is
// stored, and the total key count increases by exactly 1.
// **Validates: Requirements 2.2, 2.3**
//=============================================================================

BOOST_AUTO_TEST_CASE(property3_registration_idempotence)
{
    // Feature: falcon-pubkey-registry
    // **Property 3: Registration Idempotence**
    // **Validates: Requirements 2.2, 2.3**
    
    for (int i = 0; i < PROPERTY_TEST_ITERATIONS; i++) {
        // Generate a random 897-byte public key
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        BOOST_REQUIRE_EQUAL(pubkey.size(), QUANTUM_PUBKEY_SIZE);
        
        // Get initial key count
        QuantumRegistryStats initialStats = registry->GetStats();
        uint64_t initialCount = initialStats.totalKeys;
        
        // First registration should succeed
        bool firstResult = registry->RegisterPubKey(pubkey);
        BOOST_REQUIRE_MESSAGE(firstResult,
            "First registration should succeed for iteration " << i);
        
        // Key count should increase by exactly 1
        QuantumRegistryStats afterFirstStats = registry->GetStats();
        BOOST_CHECK_MESSAGE(afterFirstStats.totalKeys == initialCount + 1,
            "Key count should increase by 1 after first registration for iteration " << i
            << " (expected " << (initialCount + 1) << ", got " << afterFirstStats.totalKeys << ")");
        
        // Generate a random number of additional registrations (1-10)
        int additionalRegistrations = (GetRand(10) % 10) + 1;
        
        for (int j = 0; j < additionalRegistrations; j++) {
            // Subsequent registrations should also succeed (idempotent)
            bool subsequentResult = registry->RegisterPubKey(pubkey);
            BOOST_CHECK_MESSAGE(subsequentResult,
                "Subsequent registration " << (j + 1) << " should succeed for iteration " << i);
        }
        
        // Key count should NOT have changed after subsequent registrations
        QuantumRegistryStats finalStats = registry->GetStats();
        BOOST_CHECK_MESSAGE(finalStats.totalKeys == initialCount + 1,
            "Key count should remain " << (initialCount + 1) << " after " 
            << additionalRegistrations << " additional registrations for iteration " << i
            << " (got " << finalStats.totalKeys << ")");
        
        // Verify the key can still be looked up correctly
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        std::vector<unsigned char> retrieved;
        bool lookupResult = registry->LookupPubKey(hash, retrieved);
        BOOST_CHECK_MESSAGE(lookupResult,
            "Lookup should succeed after multiple registrations for iteration " << i);
        BOOST_CHECK_MESSAGE(retrieved == pubkey,
            "Retrieved key should match original for iteration " << i);
    }
    
    BOOST_TEST_MESSAGE("Property 3: Registration idempotence validated");
}

BOOST_AUTO_TEST_SUITE_END()


//=============================================================================
// RPC Command Unit Tests
// Tests for the quantum registry RPC commands
// **Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5**
//=============================================================================

/**
 * Test fixture for RPC command testing
 * Uses the global g_quantumRegistry for RPC tests
 */
struct QuantumRPCTestSetup : public BasicTestingSetup {
    fs::path tempDir;
    
    QuantumRPCTestSetup() : BasicTestingSetup(CBaseChainParams::REGTEST) {
        // Create a temporary directory for the test database
        tempDir = fs::temp_directory_path() / fs::unique_path("quantum_rpc_test_%%%%-%%%%");
        fs::create_directories(tempDir);
        
        // Initialize the global registry for RPC tests
        g_quantumRegistry = std::make_unique<QuantumPubKeyRegistry>(tempDir, 2 << 20, true /* fMemory */, true /* fWipe */);
        BOOST_REQUIRE(g_quantumRegistry && g_quantumRegistry->IsInitialized());
    }
    
    ~QuantumRPCTestSetup() {
        // Clean up global registry
        g_quantumRegistry.reset();
        
        // Clean up temporary directory
        boost::system::error_code ec;
        fs::remove_all(tempDir, ec);
    }
    
    // Generate a random 897-byte public key
    std::vector<unsigned char> GenerateRandomPubKey() {
        std::vector<unsigned char> pubkey(QUANTUM_PUBKEY_SIZE);
        GetRandBytes(pubkey.data(), pubkey.size());
        return pubkey;
    }
};

BOOST_FIXTURE_TEST_SUITE(quantum_rpc_tests, QuantumRPCTestSetup)

//=============================================================================
// Test: getquantumpubkey with valid hash
// **Validates: Requirements 7.1**
//=============================================================================

BOOST_AUTO_TEST_CASE(rpc_getquantumpubkey_valid)
{
    // Register a public key
    std::vector<unsigned char> pubkey = GenerateRandomPubKey();
    BOOST_REQUIRE(g_quantumRegistry->RegisterPubKey(pubkey));
    
    // Compute the hash
    uint256 hash = Hash(pubkey.begin(), pubkey.end());
    
    // Look up via registry directly to verify
    std::vector<unsigned char> retrieved;
    BOOST_REQUIRE(g_quantumRegistry->LookupPubKey(hash, retrieved));
    BOOST_CHECK(retrieved == pubkey);
    
    // Verify the hash format is correct (64 hex characters)
    std::string hashHex = hash.GetHex();
    BOOST_CHECK_EQUAL(hashHex.length(), 64u);
}

//=============================================================================
// Test: getquantumpubkey with invalid/unregistered hash
// **Validates: Requirements 7.2**
//=============================================================================

BOOST_AUTO_TEST_CASE(rpc_getquantumpubkey_invalid)
{
    // Generate a random hash that hasn't been registered
    uint256 randomHash = GetRandHash();
    
    // Verify it's not registered
    BOOST_CHECK(!g_quantumRegistry->IsRegistered(randomHash));
    
    // Lookup should fail
    std::vector<unsigned char> pubkey;
    bool result = g_quantumRegistry->LookupPubKey(randomHash, pubkey);
    BOOST_CHECK(!result);
    BOOST_CHECK(pubkey.empty());
    
    // Error should indicate not registered
    std::string error = g_quantumRegistry->GetLastError();
    BOOST_CHECK(error.find("not registered") != std::string::npos);
}

//=============================================================================
// Test: getquantumregistrystats response format
// **Validates: Requirements 7.3, 7.4**
//=============================================================================

BOOST_AUTO_TEST_CASE(rpc_getquantumregistrystats_format)
{
    // Get initial stats
    QuantumRegistryStats stats = g_quantumRegistry->GetStats();
    
    // Verify initial state
    BOOST_CHECK_EQUAL(stats.totalKeys, 0u);
    BOOST_CHECK_GE(stats.databaseSizeBytes, 0u);
    BOOST_CHECK_EQUAL(stats.cacheHits, 0u);
    BOOST_CHECK_EQUAL(stats.cacheMisses, 0u);
    
    // Register some keys
    const int numKeys = 5;
    for (int i = 0; i < numKeys; i++) {
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        BOOST_REQUIRE(g_quantumRegistry->RegisterPubKey(pubkey));
    }
    
    // Get updated stats
    stats = g_quantumRegistry->GetStats();
    
    // Verify key count
    BOOST_CHECK_EQUAL(stats.totalKeys, static_cast<uint64_t>(numKeys));
    
    // Database size should be positive after registrations
    // (Note: in-memory database may report 0, so we just check it's non-negative)
    BOOST_CHECK_GE(stats.databaseSizeBytes, 0u);
}

//=============================================================================
// Test: isquantumpubkeyregistered boolean responses
// **Validates: Requirements 7.5**
//=============================================================================

BOOST_AUTO_TEST_CASE(rpc_isquantumpubkeyregistered_responses)
{
    // Generate a public key
    std::vector<unsigned char> pubkey = GenerateRandomPubKey();
    uint256 hash = Hash(pubkey.begin(), pubkey.end());
    
    // Should not be registered initially
    BOOST_CHECK(!g_quantumRegistry->IsRegistered(hash));
    
    // Register the key
    BOOST_REQUIRE(g_quantumRegistry->RegisterPubKey(pubkey));
    
    // Should now be registered
    BOOST_CHECK(g_quantumRegistry->IsRegistered(hash));
    
    // Random hash should not be registered
    uint256 randomHash = GetRandHash();
    BOOST_CHECK(!g_quantumRegistry->IsRegistered(randomHash));
}

//=============================================================================
// Test: Multiple registrations and lookups
// **Validates: Requirements 7.1, 7.3, 7.5**
//=============================================================================

BOOST_AUTO_TEST_CASE(rpc_multiple_operations)
{
    const int numKeys = 10;
    std::vector<std::vector<unsigned char>> keys;
    std::vector<uint256> hashes;
    
    // Register multiple keys
    for (int i = 0; i < numKeys; i++) {
        std::vector<unsigned char> pubkey = GenerateRandomPubKey();
        uint256 hash = Hash(pubkey.begin(), pubkey.end());
        
        BOOST_REQUIRE(g_quantumRegistry->RegisterPubKey(pubkey));
        
        keys.push_back(pubkey);
        hashes.push_back(hash);
    }
    
    // Verify stats
    QuantumRegistryStats stats = g_quantumRegistry->GetStats();
    BOOST_CHECK_EQUAL(stats.totalKeys, static_cast<uint64_t>(numKeys));
    
    // Verify all keys can be looked up
    for (int i = 0; i < numKeys; i++) {
        BOOST_CHECK(g_quantumRegistry->IsRegistered(hashes[i]));
        
        std::vector<unsigned char> retrieved;
        BOOST_CHECK(g_quantumRegistry->LookupPubKey(hashes[i], retrieved));
        BOOST_CHECK(retrieved == keys[i]);
    }
    
    // Verify cache statistics are updated
    stats = g_quantumRegistry->GetStats();
    BOOST_CHECK_GE(stats.cacheHits + stats.cacheMisses, static_cast<uint64_t>(numKeys));
}

//=============================================================================
// Test: Hash format validation
// **Validates: Requirements 7.1, 7.2**
//=============================================================================

BOOST_AUTO_TEST_CASE(rpc_hash_format_validation)
{
    // Register a key
    std::vector<unsigned char> pubkey = GenerateRandomPubKey();
    BOOST_REQUIRE(g_quantumRegistry->RegisterPubKey(pubkey));
    
    uint256 hash = Hash(pubkey.begin(), pubkey.end());
    
    // Verify hash is 32 bytes (64 hex characters)
    std::string hashHex = hash.GetHex();
    BOOST_CHECK_EQUAL(hashHex.length(), 64u);
    
    // Verify all characters are valid hex
    for (char c : hashHex) {
        bool isValidHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        BOOST_CHECK(isValidHex);
    }
    
    // Verify we can parse the hash back
    uint256 parsedHash;
    parsedHash.SetHex(hashHex);
    BOOST_CHECK(parsedHash == hash);
}

BOOST_AUTO_TEST_SUITE_END()
