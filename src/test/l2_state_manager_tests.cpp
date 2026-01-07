// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_state_manager_tests.cpp
 * @brief Property-based tests for L2 State Manager
 * 
 * **Feature: cascoin-l2-solution, Property 1: State Root Consistency**
 * **Validates: Requirements 3.1, 5.2, 19.2**
 * 
 * Property 1: State Root Consistency
 * *For any* sequence of L2 transactions applied to a state, re-executing 
 * the same transactions from the same initial state SHALL produce the 
 * identical final state root.
 */

#include <l2/state_manager.h>
#include <l2/account_state.h>
#include <random.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <set>

namespace {

// Local random context for tests
static FastRandomContext g_test_rand_ctx(true);

static uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

static uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

static uint256 TestRand256() { 
    return g_test_rand_ctx.rand256(); 
}

/**
 * Helper function to generate a random account state
 */
static l2::AccountState RandomAccountState()
{
    l2::AccountState state;
    state.balance = TestRand64() % (1000 * COIN);
    state.nonce = TestRand64() % 1000;
    state.hatScore = TestRand32() % 101;  // 0-100
    state.lastActivity = TestRand64() % 1000000;
    
    // 30% chance of being a contract
    if (TestRand32() % 10 < 3) {
        state.codeHash = TestRand256();
        state.storageRoot = TestRand256();
    }
    
    return state;
}

/**
 * Helper function to generate a random address key
 */
static uint256 RandomAddress()
{
    return TestRand256();
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_state_manager_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_state_manager_has_consistent_root)
{
    l2::L2StateManager manager1(1);
    l2::L2StateManager manager2(1);
    
    // Two empty state managers should have the same root
    BOOST_CHECK(manager1.GetStateRoot() == manager2.GetStateRoot());
    BOOST_CHECK(manager1.IsEmpty());
    BOOST_CHECK_EQUAL(manager1.GetAccountCount(), 0u);
}

BOOST_AUTO_TEST_CASE(set_and_get_account_state)
{
    l2::L2StateManager manager(1);
    
    uint256 address = RandomAddress();
    l2::AccountState state = RandomAccountState();
    
    manager.SetAccountState(address, state);
    
    l2::AccountState retrieved = manager.GetAccountState(address);
    BOOST_CHECK(retrieved == state);
    BOOST_CHECK_EQUAL(manager.GetAccountCount(), 1u);
}

BOOST_AUTO_TEST_CASE(empty_account_not_stored)
{
    l2::L2StateManager manager(1);
    
    uint256 address = RandomAddress();
    l2::AccountState emptyState;
    
    manager.SetAccountState(address, emptyState);
    
    // Empty accounts should not be stored
    BOOST_CHECK(manager.IsEmpty());
    BOOST_CHECK_EQUAL(manager.GetAccountCount(), 0u);
}

BOOST_AUTO_TEST_CASE(state_root_changes_on_modification)
{
    l2::L2StateManager manager(1);
    
    uint256 emptyRoot = manager.GetStateRoot();
    
    uint256 address = RandomAddress();
    l2::AccountState state = RandomAccountState();
    manager.SetAccountState(address, state);
    
    uint256 rootAfterSet = manager.GetStateRoot();
    BOOST_CHECK(rootAfterSet != emptyRoot);
    
    // Setting empty state should remove account
    l2::AccountState emptyState;
    manager.SetAccountState(address, emptyState);
    
    uint256 rootAfterDelete = manager.GetStateRoot();
    BOOST_CHECK(rootAfterDelete == emptyRoot);
}

BOOST_AUTO_TEST_CASE(clear_empties_state)
{
    l2::L2StateManager manager(1);
    
    // Add some accounts
    for (int i = 0; i < 5; ++i) {
        manager.SetAccountState(RandomAddress(), RandomAccountState());
    }
    
    BOOST_CHECK(!manager.IsEmpty());
    
    manager.Clear();
    
    BOOST_CHECK(manager.IsEmpty());
    BOOST_CHECK_EQUAL(manager.GetAccountCount(), 0u);
}

BOOST_AUTO_TEST_CASE(snapshot_and_revert)
{
    l2::L2StateManager manager(1);
    
    // Add initial state
    uint256 addr1 = RandomAddress();
    l2::AccountState state1 = RandomAccountState();
    manager.SetAccountState(addr1, state1);
    
    // Create snapshot
    manager.CreateSnapshot(100, 50);
    uint256 snapshotRoot = manager.GetStateRoot();
    
    BOOST_CHECK_EQUAL(manager.GetSnapshotCount(), 1u);
    
    // Modify state
    uint256 addr2 = RandomAddress();
    l2::AccountState state2 = RandomAccountState();
    manager.SetAccountState(addr2, state2);
    
    uint256 modifiedRoot = manager.GetStateRoot();
    BOOST_CHECK(modifiedRoot != snapshotRoot);
    
    // Revert to snapshot
    bool reverted = manager.RevertToStateRoot(snapshotRoot);
    BOOST_CHECK(reverted);
    
    uint256 revertedRoot = manager.GetStateRoot();
    BOOST_CHECK(revertedRoot == snapshotRoot);
    
    // Verify original state is restored
    l2::AccountState retrieved = manager.GetAccountState(addr1);
    BOOST_CHECK(retrieved == state1);
}

BOOST_AUTO_TEST_CASE(revert_to_unknown_root_fails)
{
    l2::L2StateManager manager(1);
    
    uint256 unknownRoot = RandomAddress();
    bool reverted = manager.RevertToStateRoot(unknownRoot);
    
    BOOST_CHECK(!reverted);
}

BOOST_AUTO_TEST_CASE(account_proof_generation_and_verification)
{
    l2::L2StateManager manager(1);
    
    uint256 address = RandomAddress();
    l2::AccountState state = RandomAccountState();
    manager.SetAccountState(address, state);
    
    uint256 root = manager.GetStateRoot();
    l2::MerkleProof proof = manager.GenerateAccountProof(address);
    
    // Verify proof
    bool verified = l2::L2StateManager::VerifyAccountProof(proof, root, address, state);
    BOOST_CHECK(verified);
    
    // Wrong state should fail verification
    l2::AccountState wrongState = RandomAccountState();
    if (wrongState != state) {
        bool wrongVerified = l2::L2StateManager::VerifyAccountProof(proof, root, address, wrongState);
        BOOST_CHECK(!wrongVerified);
    }
}

BOOST_AUTO_TEST_CASE(contract_storage_operations)
{
    l2::L2StateManager manager(1);
    
    uint256 contractAddr = RandomAddress();
    uint256 key = RandomAddress();
    uint256 value = RandomAddress();
    
    // Set storage
    manager.SetContractStorage(contractAddr, key, value);
    
    // Get storage
    uint256 retrieved = manager.GetContractStorage(contractAddr, key);
    BOOST_CHECK(retrieved == value);
    
    // Non-existent key returns zero
    uint256 nonExistentKey = RandomAddress();
    uint256 zeroValue = manager.GetContractStorage(contractAddr, nonExistentKey);
    BOOST_CHECK(zeroValue.IsNull());
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 1: State Root Consistency**
 * 
 * *For any* sequence of account state changes, applying the same changes
 * in the same order to two separate state managers SHALL produce identical
 * state roots.
 * 
 * **Validates: Requirements 3.1, 5.2**
 */
BOOST_AUTO_TEST_CASE(property_state_root_consistency)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Generate random state changes
        int numChanges = 2 + (TestRand32() % 5);
        std::vector<std::pair<uint256, l2::AccountState>> changes;
        
        for (int i = 0; i < numChanges; ++i) {
            changes.emplace_back(RandomAddress(), RandomAccountState());
        }
        
        // Apply to first manager
        l2::L2StateManager manager1(1);
        for (const auto& change : changes) {
            manager1.SetAccountState(change.first, change.second);
        }
        uint256 root1 = manager1.GetStateRoot();
        
        // Apply to second manager
        l2::L2StateManager manager2(1);
        for (const auto& change : changes) {
            manager2.SetAccountState(change.first, change.second);
        }
        uint256 root2 = manager2.GetStateRoot();
        
        // Roots should be identical
        BOOST_CHECK_MESSAGE(root1 == root2,
            "State root consistency failed for iteration " << iteration);
    }
}

/**
 * **Property 1: State Root Consistency (Order Independence)**
 * 
 * *For any* set of account state changes, applying them in any order
 * SHALL produce the same final state root.
 * 
 * **Validates: Requirements 3.1**
 */
BOOST_AUTO_TEST_CASE(property_state_root_order_independence)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Generate random state changes with unique addresses
        int numChanges = 2 + (TestRand32() % 4);
        std::vector<std::pair<uint256, l2::AccountState>> changes;
        std::set<uint256> usedAddresses;
        
        for (int i = 0; i < numChanges; ++i) {
            uint256 addr;
            do {
                addr = RandomAddress();
            } while (usedAddresses.count(addr) > 0);
            usedAddresses.insert(addr);
            changes.emplace_back(addr, RandomAccountState());
        }
        
        // Apply in original order
        l2::L2StateManager manager1(1);
        for (const auto& change : changes) {
            manager1.SetAccountState(change.first, change.second);
        }
        uint256 root1 = manager1.GetStateRoot();
        
        // Apply in reverse order
        l2::L2StateManager manager2(1);
        for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
            manager2.SetAccountState(it->first, it->second);
        }
        uint256 root2 = manager2.GetStateRoot();
        
        // Roots should be identical
        BOOST_CHECK_MESSAGE(root1 == root2,
            "State root order independence failed for iteration " << iteration);
    }
}

/**
 * **Property 1: State Root Consistency (Revert Consistency)**
 * 
 * *For any* state, creating a snapshot and then reverting to it SHALL
 * restore the exact same state root.
 * 
 * **Validates: Requirements 19.2**
 */
BOOST_AUTO_TEST_CASE(property_revert_consistency)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::L2StateManager manager(1);
        
        // Create initial state
        int numInitial = 1 + (TestRand32() % 3);
        for (int i = 0; i < numInitial; ++i) {
            manager.SetAccountState(RandomAddress(), RandomAccountState());
        }
        
        // Create snapshot
        manager.CreateSnapshot(100, 50);
        uint256 snapshotRoot = manager.GetStateRoot();
        
        // Make modifications
        int numMods = 1 + (TestRand32() % 3);
        for (int i = 0; i < numMods; ++i) {
            manager.SetAccountState(RandomAddress(), RandomAccountState());
        }
        
        // Revert
        bool reverted = manager.RevertToStateRoot(snapshotRoot);
        BOOST_CHECK(reverted);
        
        uint256 revertedRoot = manager.GetStateRoot();
        
        // Reverted root should match snapshot
        BOOST_CHECK_MESSAGE(revertedRoot == snapshotRoot,
            "Revert consistency failed for iteration " << iteration);
    }
}

/**
 * **Property: Account State Serialization Round-Trip**
 * 
 * *For any* account state, serializing and deserializing SHALL produce
 * an identical account state.
 * 
 * **Validates: Requirements 3.1**
 */
BOOST_AUTO_TEST_CASE(property_account_state_roundtrip)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::AccountState original = RandomAccountState();
        
        // Serialize
        std::vector<unsigned char> serialized = original.Serialize();
        
        // Deserialize
        l2::AccountState restored;
        bool success = restored.Deserialize(serialized);
        
        BOOST_CHECK(success);
        BOOST_CHECK_MESSAGE(original == restored,
            "Account state round-trip failed for iteration " << iteration);
    }
}

/**
 * **Property: State Manager Proof Consistency**
 * 
 * *For any* account in the state, the generated proof SHALL verify
 * successfully against the current state root.
 * 
 * **Validates: Requirements 3.1, 5.2**
 */
BOOST_AUTO_TEST_CASE(property_proof_consistency)
{
    // Run 5 iterations (proof generation is expensive)
    for (int iteration = 0; iteration < 5; ++iteration) {
        l2::L2StateManager manager(1);
        
        // Add accounts
        int numAccounts = 1 + (TestRand32() % 3);
        std::vector<std::pair<uint256, l2::AccountState>> accounts;
        
        for (int i = 0; i < numAccounts; ++i) {
            uint256 addr = RandomAddress();
            l2::AccountState state = RandomAccountState();
            manager.SetAccountState(addr, state);
            accounts.emplace_back(addr, state);
        }
        
        uint256 root = manager.GetStateRoot();
        
        // Verify proof for first account
        const auto& account = accounts[0];
        l2::MerkleProof proof = manager.GenerateAccountProof(account.first);
        
        bool verified = l2::L2StateManager::VerifyAccountProof(
            proof, root, account.first, account.second);
        
        BOOST_CHECK_MESSAGE(verified,
            "Proof consistency failed for iteration " << iteration);
    }
}

/**
 * **Property: Empty Account Exclusion**
 * 
 * *For any* address not in the state, the account state SHALL be empty
 * and proof verification with empty state SHALL succeed.
 * 
 * **Validates: Requirements 3.1**
 */
BOOST_AUTO_TEST_CASE(property_empty_account_exclusion)
{
    // Run 5 iterations
    for (int iteration = 0; iteration < 5; ++iteration) {
        l2::L2StateManager manager(1);
        
        // Add some accounts
        std::set<uint256> existingAddresses;
        int numAccounts = TestRand32() % 3;
        
        for (int i = 0; i < numAccounts; ++i) {
            uint256 addr = RandomAddress();
            manager.SetAccountState(addr, RandomAccountState());
            existingAddresses.insert(addr);
        }
        
        // Generate non-existent address
        uint256 nonExistent;
        do {
            nonExistent = RandomAddress();
        } while (existingAddresses.count(nonExistent) > 0);
        
        // Get state for non-existent address
        l2::AccountState state = manager.GetAccountState(nonExistent);
        
        // Should be empty
        BOOST_CHECK_MESSAGE(state.IsEmpty(),
            "Non-existent account should be empty for iteration " << iteration);
        
        // Proof should verify with empty state
        uint256 root = manager.GetStateRoot();
        l2::MerkleProof proof = manager.GenerateAccountProof(nonExistent);
        
        bool verified = l2::L2StateManager::VerifyAccountProof(
            proof, root, nonExistent, state);
        
        BOOST_CHECK_MESSAGE(verified,
            "Empty account proof failed for iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
