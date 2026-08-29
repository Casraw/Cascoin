// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_minter_tests.cpp
 * @brief Property-based tests for L2 Token Minter
 * 
 * **Feature: l2-bridge-security**
 * 
 * **Property 5: 1:1 Mint Ratio**
 * **Validates: Requirements 4.2**
 * 
 * **Property 6: Supply Invariant**
 * **Validates: Requirements 8.1, 8.3**
 */

#include <l2/l2_minter.h>
#include <l2/burn_registry.h>
#include <l2/state_manager.h>
#include <l2/account_state.h>
#include <random.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <set>
#include <vector>

namespace {

// Local random context for tests
FastRandomContext g_test_rand_ctx(true);

uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

/**
 * Helper to generate a random uint256 hash
 */
uint256 RandomHash()
{
    uint256 hash;
    for (int i = 0; i < 8; ++i) {
        ((uint32_t*)hash.begin())[i] = TestRand32();
    }
    return hash;
}

/**
 * Helper to generate a random uint160 address
 */
uint160 RandomAddress()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        ((uint32_t*)addr.begin())[i] = TestRand32();
    }
    return addr;
}

/**
 * Helper to generate a random valid burn amount
 */
CAmount RandomBurnAmount()
{
    // Generate amounts between 1 satoshi and 1000 CAS
    return (TestRand64() % (1000 * COIN)) + 1;
}

/**
 * Test fixture for L2TokenMinter tests
 */
struct L2MinterTestFixture {
    l2::L2StateManager stateManager;
    l2::BurnRegistry burnRegistry;
    std::unique_ptr<l2::L2TokenMinter> minter;
    
    L2MinterTestFixture() 
        : stateManager(1)  // Chain ID 1
    {
        minter = std::make_unique<l2::L2TokenMinter>(stateManager, burnRegistry);
        minter->SetCurrentBlockNumber(100);  // Start at block 100
    }
    
    void Reset() {
        burnRegistry.Clear();
        stateManager.Clear();
        minter->Clear();
        minter->SetCurrentBlockNumber(100);
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(l2_minter_tests, L2MinterTestFixture)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(minter_construction)
{
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), 0);
    BOOST_CHECK_EQUAL(minter->GetTotalMintedL2(), 0);
    BOOST_CHECK_EQUAL(minter->GetTotalBurnedL1(), 0);
}

BOOST_AUTO_TEST_CASE(mint_tokens_success)
{
    uint256 l1TxHash = RandomHash();
    uint160 recipient = RandomAddress();
    CAmount amount = 100 * COIN;
    
    l2::MintResult result = minter->MintTokens(l1TxHash, recipient, amount);
    
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.amountMinted, amount);
    BOOST_CHECK(!result.l2TxHash.IsNull());
    
    // Verify balance was updated
    BOOST_CHECK_EQUAL(minter->GetBalance(recipient), amount);
    
    // Verify supply was updated
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), amount);
}

BOOST_AUTO_TEST_CASE(mint_tokens_null_l1_hash_fails)
{
    uint256 l1TxHash;  // Null hash
    uint160 recipient = RandomAddress();
    CAmount amount = 100 * COIN;
    
    l2::MintResult result = minter->MintTokens(l1TxHash, recipient, amount);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(!result.errorMessage.empty());
}

BOOST_AUTO_TEST_CASE(mint_tokens_null_recipient_fails)
{
    uint256 l1TxHash = RandomHash();
    uint160 recipient;  // Null address
    CAmount amount = 100 * COIN;
    
    l2::MintResult result = minter->MintTokens(l1TxHash, recipient, amount);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(!result.errorMessage.empty());
}

BOOST_AUTO_TEST_CASE(mint_tokens_zero_amount_fails)
{
    uint256 l1TxHash = RandomHash();
    uint160 recipient = RandomAddress();
    CAmount amount = 0;
    
    l2::MintResult result = minter->MintTokens(l1TxHash, recipient, amount);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(!result.errorMessage.empty());
}

BOOST_AUTO_TEST_CASE(mint_tokens_negative_amount_fails)
{
    uint256 l1TxHash = RandomHash();
    uint160 recipient = RandomAddress();
    CAmount amount = -100;
    
    l2::MintResult result = minter->MintTokens(l1TxHash, recipient, amount);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(!result.errorMessage.empty());
}

BOOST_AUTO_TEST_CASE(mint_tokens_double_mint_fails)
{
    uint256 l1TxHash = RandomHash();
    uint160 recipient = RandomAddress();
    CAmount amount = 100 * COIN;
    
    // First mint should succeed
    l2::MintResult result1 = minter->MintTokens(l1TxHash, recipient, amount);
    BOOST_CHECK(result1.success);
    
    // Second mint with same L1 TX hash should fail
    l2::MintResult result2 = minter->MintTokens(l1TxHash, recipient, amount);
    BOOST_CHECK(!result2.success);
    BOOST_CHECK(result2.errorMessage.find("already processed") != std::string::npos);
    
    // Supply should only reflect first mint
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), amount);
}

BOOST_AUTO_TEST_CASE(mint_event_emitted)
{
    uint256 l1TxHash = RandomHash();
    uint160 recipient = RandomAddress();
    CAmount amount = 100 * COIN;
    
    bool callbackCalled = false;
    l2::MintEvent receivedEvent;
    
    minter->RegisterMintEventCallback([&](const l2::MintEvent& event) {
        callbackCalled = true;
        receivedEvent = event;
    });
    
    minter->MintTokens(l1TxHash, recipient, amount);
    
    BOOST_CHECK(callbackCalled);
    BOOST_CHECK(receivedEvent.l1TxHash == l1TxHash);
    BOOST_CHECK(receivedEvent.recipient == recipient);
    BOOST_CHECK_EQUAL(receivedEvent.amount, amount);
}

BOOST_AUTO_TEST_CASE(get_mint_events)
{
    // Mint multiple times
    std::vector<uint256> l1Hashes;
    for (int i = 0; i < 5; ++i) {
        uint256 l1TxHash = RandomHash();
        l1Hashes.push_back(l1TxHash);
        minter->MintTokens(l1TxHash, RandomAddress(), RandomBurnAmount());
    }
    
    auto events = minter->GetMintEvents();
    BOOST_CHECK_EQUAL(events.size(), 5u);
    
    // Verify we can retrieve by L1 TX hash
    for (const auto& hash : l1Hashes) {
        auto event = minter->GetMintEventByL1TxHash(hash);
        BOOST_CHECK(event.has_value());
        BOOST_CHECK(event->l1TxHash == hash);
    }
}

BOOST_AUTO_TEST_CASE(get_mint_events_for_address)
{
    uint160 targetAddress = RandomAddress();
    
    // Mint to target address 3 times
    for (int i = 0; i < 3; ++i) {
        minter->MintTokens(RandomHash(), targetAddress, RandomBurnAmount());
    }
    
    // Mint to other addresses 2 times
    for (int i = 0; i < 2; ++i) {
        minter->MintTokens(RandomHash(), RandomAddress(), RandomBurnAmount());
    }
    
    auto events = minter->GetMintEventsForAddress(targetAddress);
    BOOST_CHECK_EQUAL(events.size(), 3u);
    
    for (const auto& event : events) {
        BOOST_CHECK(event.recipient == targetAddress);
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 5: 1:1 Mint Ratio**
 * 
 * *For any* successful mint operation, the amount of L2 tokens minted SHALL
 * exactly equal the amount of CAS burned on L1 (as encoded in the OP_RETURN).
 * 
 * **Validates: Requirements 4.2**
 */
BOOST_AUTO_TEST_CASE(property_1_to_1_mint_ratio)
{
    // Run 100 iterations as per PBT requirements
    for (int iteration = 0; iteration < 100; ++iteration) {
        Reset();
        
        // Generate random burn amount
        CAmount burnAmount = RandomBurnAmount();
        uint256 l1TxHash = RandomHash();
        uint160 recipient = RandomAddress();
        
        // Mint tokens
        l2::MintResult result = minter->MintTokens(l1TxHash, recipient, burnAmount);
        
        // Property: Mint should succeed
        BOOST_CHECK_MESSAGE(result.success,
            "Mint should succeed in iteration " << iteration);
        
        // Property: Minted amount should exactly equal burn amount (1:1 ratio)
        BOOST_CHECK_MESSAGE(result.amountMinted == burnAmount,
            "Minted amount (" << result.amountMinted << ") should equal burn amount (" 
            << burnAmount << ") in iteration " << iteration);
        
        // Property: Recipient balance should equal burn amount
        CAmount balance = minter->GetBalance(recipient);
        BOOST_CHECK_MESSAGE(balance == burnAmount,
            "Recipient balance (" << balance << ") should equal burn amount (" 
            << burnAmount << ") in iteration " << iteration);
        
        // Property: Total supply should equal burn amount
        CAmount totalSupply = minter->GetTotalSupply();
        BOOST_CHECK_MESSAGE(totalSupply == burnAmount,
            "Total supply (" << totalSupply << ") should equal burn amount (" 
            << burnAmount << ") in iteration " << iteration);
        
        // Property: Total burned on L1 should equal burn amount
        CAmount totalBurned = minter->GetTotalBurnedL1();
        BOOST_CHECK_MESSAGE(totalBurned == burnAmount,
            "Total burned L1 (" << totalBurned << ") should equal burn amount (" 
            << burnAmount << ") in iteration " << iteration);
    }
}

/**
 * **Property 5 (continued): Multiple mints maintain 1:1 ratio**
 * 
 * *For any* sequence of mint operations, each individual mint should
 * maintain the 1:1 ratio, and the cumulative totals should match.
 * 
 * **Validates: Requirements 4.2**
 */
BOOST_AUTO_TEST_CASE(property_1_to_1_mint_ratio_multiple)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        Reset();
        
        // Generate random number of mints (1-20)
        size_t numMints = (TestRand32() % 20) + 1;
        
        CAmount expectedTotalSupply = 0;
        std::map<uint160, CAmount> expectedBalances;
        
        for (size_t i = 0; i < numMints; ++i) {
            CAmount burnAmount = RandomBurnAmount();
            uint256 l1TxHash = RandomHash();
            uint160 recipient = RandomAddress();
            
            l2::MintResult result = minter->MintTokens(l1TxHash, recipient, burnAmount);
            
            if (result.success) {
                // Property: Each mint should be exactly 1:1
                BOOST_CHECK_MESSAGE(result.amountMinted == burnAmount,
                    "Mint " << i << " should be 1:1 in iteration " << iteration);
                
                expectedTotalSupply += burnAmount;
                expectedBalances[recipient] += burnAmount;
            }
        }
        
        // Property: Total supply should equal sum of all burns
        BOOST_CHECK_MESSAGE(minter->GetTotalSupply() == expectedTotalSupply,
            "Total supply should equal sum of burns in iteration " << iteration);
        
        // Property: Each recipient balance should match expected
        for (const auto& pair : expectedBalances) {
            CAmount actualBalance = minter->GetBalance(pair.first);
            BOOST_CHECK_MESSAGE(actualBalance == pair.second,
                "Balance should match expected in iteration " << iteration);
        }
    }
}

/**
 * **Property 6: Supply Invariant**
 * 
 * *For any* L2 chain state, the total L2 token supply SHALL equal the sum
 * of all CAS amounts burned on L1 (as recorded in the burn registry).
 * Additionally, the sum of all L2 balances SHALL equal the total supply.
 * 
 * **Validates: Requirements 8.1, 8.3**
 */
BOOST_AUTO_TEST_CASE(property_supply_invariant)
{
    // Run 100 iterations as per PBT requirements
    for (int iteration = 0; iteration < 100; ++iteration) {
        Reset();
        
        // Generate random number of mints (1-20)
        size_t numMints = (TestRand32() % 20) + 1;
        
        CAmount expectedTotalBurned = 0;
        std::set<uint160> allRecipients;
        
        for (size_t i = 0; i < numMints; ++i) {
            CAmount burnAmount = RandomBurnAmount();
            uint256 l1TxHash = RandomHash();
            uint160 recipient = RandomAddress();
            
            l2::MintResult result = minter->MintTokens(l1TxHash, recipient, burnAmount);
            
            if (result.success) {
                expectedTotalBurned += burnAmount;
                allRecipients.insert(recipient);
            }
        }
        
        // Property 1: Total L2 supply == Total CAS burned on L1
        CAmount totalSupply = minter->GetTotalSupply();
        CAmount totalBurnedL1 = minter->GetTotalBurnedL1();
        
        BOOST_CHECK_MESSAGE(totalSupply == totalBurnedL1,
            "Total supply (" << totalSupply << ") should equal total burned L1 (" 
            << totalBurnedL1 << ") in iteration " << iteration);
        
        BOOST_CHECK_MESSAGE(totalSupply == expectedTotalBurned,
            "Total supply (" << totalSupply << ") should equal expected burned (" 
            << expectedTotalBurned << ") in iteration " << iteration);
        
        // Property 2: Sum of all L2 balances == Total supply
        CAmount sumOfBalances = 0;
        for (const uint160& addr : allRecipients) {
            sumOfBalances += minter->GetBalance(addr);
        }
        
        BOOST_CHECK_MESSAGE(sumOfBalances == totalSupply,
            "Sum of balances (" << sumOfBalances << ") should equal total supply (" 
            << totalSupply << ") in iteration " << iteration);
        
        // Property 3: VerifySupplyInvariant should return true
        BOOST_CHECK_MESSAGE(minter->VerifySupplyInvariant(),
            "Supply invariant should hold in iteration " << iteration);
    }
}

/**
 * **Property 6 (continued): Supply invariant after multiple operations**
 * 
 * *For any* sequence of mint operations to the same recipient,
 * the supply invariant should still hold.
 * 
 * **Validates: Requirements 8.1, 8.3**
 */
BOOST_AUTO_TEST_CASE(property_supply_invariant_same_recipient)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        Reset();
        
        // Use same recipient for all mints
        uint160 recipient = RandomAddress();
        
        // Generate random number of mints (1-20)
        size_t numMints = (TestRand32() % 20) + 1;
        
        CAmount expectedBalance = 0;
        
        for (size_t i = 0; i < numMints; ++i) {
            CAmount burnAmount = RandomBurnAmount();
            uint256 l1TxHash = RandomHash();
            
            l2::MintResult result = minter->MintTokens(l1TxHash, recipient, burnAmount);
            
            if (result.success) {
                expectedBalance += burnAmount;
            }
        }
        
        // Property: Recipient balance should equal sum of all mints
        CAmount actualBalance = minter->GetBalance(recipient);
        BOOST_CHECK_MESSAGE(actualBalance == expectedBalance,
            "Balance (" << actualBalance << ") should equal expected (" 
            << expectedBalance << ") in iteration " << iteration);
        
        // Property: Total supply should equal recipient balance (only one recipient)
        CAmount totalSupply = minter->GetTotalSupply();
        BOOST_CHECK_MESSAGE(totalSupply == actualBalance,
            "Total supply (" << totalSupply << ") should equal balance (" 
            << actualBalance << ") in iteration " << iteration);
        
        // Property: Supply invariant should hold
        BOOST_CHECK_MESSAGE(minter->VerifySupplyInvariant(),
            "Supply invariant should hold in iteration " << iteration);
    }
}

/**
 * **Property: Double-mint prevention maintains supply invariant**
 * 
 * *For any* attempt to double-mint, the supply invariant should still hold
 * and the total supply should not increase.
 * 
 * **Validates: Requirements 4.2, 8.1, 8.3**
 */
BOOST_AUTO_TEST_CASE(property_double_mint_maintains_invariant)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        Reset();
        
        uint256 l1TxHash = RandomHash();
        uint160 recipient = RandomAddress();
        CAmount burnAmount = RandomBurnAmount();
        
        // First mint
        l2::MintResult result1 = minter->MintTokens(l1TxHash, recipient, burnAmount);
        BOOST_REQUIRE(result1.success);
        
        CAmount supplyAfterFirst = minter->GetTotalSupply();
        
        // Attempt double-mint
        l2::MintResult result2 = minter->MintTokens(l1TxHash, recipient, burnAmount);
        
        // Property: Double-mint should fail
        BOOST_CHECK_MESSAGE(!result2.success,
            "Double-mint should fail in iteration " << iteration);
        
        // Property: Supply should not change after failed double-mint
        CAmount supplyAfterSecond = minter->GetTotalSupply();
        BOOST_CHECK_MESSAGE(supplyAfterSecond == supplyAfterFirst,
            "Supply should not change after failed double-mint in iteration " << iteration);
        
        // Property: Supply invariant should still hold
        BOOST_CHECK_MESSAGE(minter->VerifySupplyInvariant(),
            "Supply invariant should hold after failed double-mint in iteration " << iteration);
    }
}

/**
 * **Property: Mint events are consistent with supply**
 * 
 * *For any* sequence of mints, the sum of amounts in mint events
 * should equal the total supply.
 * 
 * **Validates: Requirements 4.4, 8.1**
 */
BOOST_AUTO_TEST_CASE(property_mint_events_consistent)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        Reset();
        
        // Generate random number of mints (1-20)
        size_t numMints = (TestRand32() % 20) + 1;
        
        for (size_t i = 0; i < numMints; ++i) {
            minter->MintTokens(RandomHash(), RandomAddress(), RandomBurnAmount());
        }
        
        // Calculate sum of amounts from mint events
        auto events = minter->GetMintEvents();
        CAmount sumFromEvents = 0;
        for (const auto& event : events) {
            sumFromEvents += event.amount;
        }
        
        // Property: Sum of event amounts should equal total supply
        CAmount totalSupply = minter->GetTotalSupply();
        BOOST_CHECK_MESSAGE(sumFromEvents == totalSupply,
            "Sum from events (" << sumFromEvents << ") should equal total supply (" 
            << totalSupply << ") in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
