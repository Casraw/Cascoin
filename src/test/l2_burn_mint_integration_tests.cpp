// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_burn_mint_integration_tests.cpp
 * @brief Integration tests for L2 Burn-and-Mint Token Model
 * 
 * These tests verify the complete burn-and-mint flow including:
 * - Full burn-mint flow (Task 16.1)
 * - Multi-sequencer consensus (Task 16.2)
 * - Double-mint prevention (Task 16.3)
 * - Fee distribution (Task 16.4)
 * - Supply invariant (Task 16.5)
 * - L2 reorg handling (Task 16.6)
 * 
 * Requirements: 1.1-1.6, 3.1, 3.4, 4.1-4.6, 5.3, 5.4, 5.6, 6.1-6.3, 8.1, 8.3, 8.4, 10.3
 */

#include <test/test_bitcoin.h>

#include <l2/burn_parser.h>
#include <l2/burn_validator.h>
#include <l2/burn_registry.h>
#include <l2/mint_consensus.h>
#include <l2/l2_minter.h>
#include <l2/fee_distributor.h>
#include <l2/state_manager.h>
#include <l2/l2_transaction.h>
#include <l2/l2_common.h>

#include <key.h>
#include <pubkey.h>
#include <random.h>
#include <uint256.h>
#include <script/script.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <map>
#include <set>
#include <chrono>

namespace {

// Local random context for tests
static FastRandomContext g_integ_rand_ctx(true);

static uint32_t IntegRand32() { return g_integ_rand_ctx.rand32(); }
static uint64_t IntegRand64() {
    return ((uint64_t)g_integ_rand_ctx.rand32() << 32) | g_integ_rand_ctx.rand32();
}

/**
 * Helper to generate a random uint256 hash
 */
static uint256 RandomHash()
{
    uint256 hash;
    for (int i = 0; i < 8; ++i) {
        ((uint32_t*)hash.begin())[i] = IntegRand32();
    }
    return hash;
}

/**
 * Helper to generate a random uint160 address
 */
static uint160 RandomAddress()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        ((uint32_t*)addr.begin())[i] = IntegRand32();
    }
    return addr;
}

/**
 * Helper to generate a random valid burn amount
 */
static CAmount RandomBurnAmount()
{
    return (IntegRand64() % (1000 * COIN)) + 1;
}

/**
 * Helper to generate a valid compressed public key
 */
static CPubKey GenerateValidCompressedPubKey()
{
    std::vector<unsigned char> pubkeyData(33);
    pubkeyData[0] = (IntegRand32() % 2 == 0) ? 0x02 : 0x03;
    for (size_t i = 1; i < 33; ++i) {
        pubkeyData[i] = static_cast<unsigned char>(IntegRand32() & 0xFF);
    }
    return CPubKey(pubkeyData.begin(), pubkeyData.end());
}

/**
 * Helper to generate a key pair
 */
static std::pair<CKey, CPubKey> GenerateKeyPair()
{
    CKey key;
    key.MakeNewKey(true);
    return {key, key.GetPubKey()};
}

/**
 * Helper to get current timestamp
 */
static uint64_t GetCurrentTimestamp()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/**
 * @brief Integration test fixture for burn-and-mint flow
 * 
 * Sets up all components needed for integration testing:
 * - BurnRegistry for tracking processed burns
 * - L2StateManager for managing L2 state
 * - L2TokenMinter for minting tokens
 * - MintConsensusManager for sequencer consensus
 * - FeeDistributor for fee distribution
 */
struct BurnMintIntegrationFixture : public BasicTestingSetup {
    // Core components
    l2::BurnRegistry burnRegistry;
    l2::L2StateManager stateManager;
    std::unique_ptr<l2::L2TokenMinter> minter;
    std::unique_ptr<l2::MintConsensusManager> consensusManager;
    l2::FeeDistributor feeDistributor;
    
    // Sequencer keys for consensus testing
    std::vector<std::pair<CKey, CPubKey>> sequencers;
    
    // Chain configuration
    uint32_t chainId;
    uint64_t currentL1Block;
    uint64_t currentL2Block;
    
    BurnMintIntegrationFixture()
        : BasicTestingSetup(CBaseChainParams::REGTEST)
        , stateManager(1)  // Chain ID 1
        , chainId(1)
        , currentL1Block(100)
        , currentL2Block(1)
    {
        // Initialize minter
        minter = std::make_unique<l2::L2TokenMinter>(stateManager, burnRegistry);
        minter->SetCurrentBlockNumber(currentL2Block);
        
        // Initialize consensus manager with chain ID
        consensusManager = std::make_unique<l2::MintConsensusManager>(chainId);
        
        // Setup default 5 sequencers
        SetupSequencers(5);
    }
    
    void SetupSequencers(size_t count) {
        consensusManager->ClearTestSequencers();
        sequencers.clear();
        
        for (size_t i = 0; i < count; ++i) {
            auto keyPair = GenerateKeyPair();
            sequencers.push_back(keyPair);
            consensusManager->AddTestSequencer(keyPair.second.GetID(), keyPair.second);
            feeDistributor.RegisterSequencer(keyPair.second.GetID(), 100, 1000 * COIN);
        }
        consensusManager->SetTestSequencerCount(count);
    }

    /**
     * Create a burn transaction with OP_RETURN output
     */
    CTransaction CreateBurnTransaction(const CPubKey& recipientPubKey, CAmount amount)
    {
        CMutableTransaction mtx;
        mtx.nVersion = 2;
        mtx.nLockTime = 0;
        
        // Add dummy input
        CTxIn input;
        input.prevout.hash = RandomHash();
        input.prevout.n = 0;
        mtx.vin.push_back(input);
        
        // Create burn script with OP_RETURN
        CScript burnScript = l2::BurnTransactionParser::CreateBurnScript(
            chainId, recipientPubKey, amount);
        
        // Add burn output (OP_RETURN outputs have 0 value)
        CTxOut burnOutput(0, burnScript);
        mtx.vout.push_back(burnOutput);
        
        return CTransaction(mtx);
    }
    
    /**
     * Simulate L1 confirmations by advancing block count
     */
    void SimulateL1Confirmations(int confirmations)
    {
        currentL1Block += confirmations;
    }
    
    /**
     * Create a signed confirmation from a sequencer
     */
    l2::MintConfirmation CreateSequencerConfirmation(
        size_t sequencerIndex,
        const uint256& l1TxHash,
        const uint160& l2Recipient,
        CAmount amount)
    {
        if (sequencerIndex >= sequencers.size()) {
            throw std::runtime_error("Invalid sequencer index");
        }
        
        l2::MintConfirmation conf;
        conf.l1TxHash = l1TxHash;
        conf.l2Recipient = l2Recipient;
        conf.amount = amount;
        conf.sequencerAddress = sequencers[sequencerIndex].second.GetID();
        conf.timestamp = GetCurrentTimestamp();
        conf.Sign(sequencers[sequencerIndex].first);
        
        return conf;
    }
    
    /**
     * Submit confirmations from multiple sequencers
     */
    size_t SubmitConfirmations(
        size_t count,
        const uint256& l1TxHash,
        const uint160& l2Recipient,
        CAmount amount)
    {
        size_t accepted = 0;
        for (size_t i = 0; i < count && i < sequencers.size(); ++i) {
            auto conf = CreateSequencerConfirmation(i, l1TxHash, l2Recipient, amount);
            if (consensusManager->ProcessConfirmation(conf)) {
                accepted++;
            }
        }
        return accepted;
    }

    /**
     * Execute the full burn-mint flow
     * Returns true if tokens were successfully minted
     */
    bool ExecuteFullBurnMintFlow(
        const uint256& l1TxHash,
        const CPubKey& recipientPubKey,
        CAmount amount,
        size_t confirmationCount)
    {
        uint160 l2Recipient = recipientPubKey.GetID();
        
        // Step 1: Submit confirmations
        SubmitConfirmations(confirmationCount, l1TxHash, l2Recipient, amount);
        
        // Step 2: Check if consensus reached
        if (!consensusManager->HasConsensus(l1TxHash)) {
            return false;
        }
        
        // Step 3: Mint tokens
        l2::MintResult result = minter->MintTokens(l1TxHash, l2Recipient, amount);
        
        return result.success;
    }
    
    /**
     * Create an L2 transaction for fee testing
     */
    l2::L2Transaction CreateL2Transaction(
        const uint160& fromAddr,
        const uint160& toAddr,
        CAmount txValue,
        CAmount txGasPrice,
        uint64_t txGasLimit)
    {
        l2::L2Transaction tx;
        tx.from = fromAddr;
        tx.to = toAddr;
        tx.value = txValue;
        tx.gasPrice = txGasPrice;
        tx.gasLimit = txGasLimit;
        tx.nonce = IntegRand64() % 1000;
        tx.data = {};
        return tx;
    }
    
    /**
     * Reset all state for clean test
     */
    void Reset()
    {
        burnRegistry.Clear();
        stateManager.Clear();
        minter->Clear();
        minter->SetCurrentBlockNumber(1);
        consensusManager->Clear();
        feeDistributor.Clear();
        currentL1Block = 100;
        currentL2Block = 1;
        SetupSequencers(5);
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(l2_burn_mint_integration_tests, BurnMintIntegrationFixture)

// ============================================================================
// Task 16.1: Test Full Burn-Mint Flow
// Requirements: 1.1-1.6, 4.1-4.6
// ============================================================================

/**
 * @brief Test complete burn-mint flow from burn TX creation to token minting
 * 
 * This test verifies:
 * 1. Burn transaction creation with OP_RETURN
 * 2. Burn transaction parsing and validation
 * 3. Sequencer consensus collection
 * 4. Token minting after consensus
 * 5. Balance and supply updates
 * 
 * Requirements: 1.1-1.6, 4.1-4.6
 */
BOOST_AUTO_TEST_CASE(integration_full_burn_mint_flow)
{
    // Step 1: Create burn transaction
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    
    // Step 2: Verify burn transaction is valid
    BOOST_CHECK(l2::BurnTransactionParser::IsBurnTransaction(burnTx));
    
    auto burnData = l2::BurnTransactionParser::ParseBurnTransaction(burnTx);
    BOOST_REQUIRE(burnData.has_value());
    BOOST_CHECK_EQUAL(burnData->chainId, chainId);
    BOOST_CHECK(burnData->recipientPubKey == recipientPubKey);
    BOOST_CHECK_EQUAL(burnData->amount, burnAmount);
    
    // Step 3: Simulate L1 confirmations (need 6)
    SimulateL1Confirmations(6);
    
    // Step 4: Submit sequencer confirmations (need 4/5 for 2/3 consensus)
    uint160 l2Recipient = recipientPubKey.GetID();
    size_t accepted = SubmitConfirmations(4, l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK_EQUAL(accepted, 4u);
    
    // Step 5: Verify consensus reached
    BOOST_CHECK(consensusManager->HasConsensus(l1TxHash));
    
    auto consensusState = consensusManager->GetConsensusState(l1TxHash);
    BOOST_REQUIRE(consensusState.has_value());
    BOOST_CHECK_EQUAL(consensusState->GetConfirmationCount(), 4u);
    
    // Step 6: Mint tokens
    l2::MintResult mintResult = minter->MintTokens(l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK(mintResult.success);
    BOOST_CHECK_EQUAL(mintResult.amountMinted, burnAmount);
    
    // Step 7: Verify balance updated
    CAmount balance = minter->GetBalance(l2Recipient);
    BOOST_CHECK_EQUAL(balance, burnAmount);
    
    // Step 8: Verify supply updated
    CAmount totalSupply = minter->GetTotalSupply();
    BOOST_CHECK_EQUAL(totalSupply, burnAmount);
    
    // Step 9: Verify burn recorded in registry
    BOOST_CHECK(burnRegistry.IsProcessed(l1TxHash));
}

/**
 * @brief Test multiple sequential burn-mint operations
 * 
 * Verifies that multiple burns can be processed correctly
 * and that cumulative totals are accurate.
 * 
 * Requirements: 1.1-1.6, 4.1-4.6
 */
BOOST_AUTO_TEST_CASE(integration_multiple_burn_mint_flows)
{
    const int NUM_BURNS = 5;
    CAmount totalExpectedSupply = 0;
    std::map<uint160, CAmount> expectedBalances;
    
    for (int i = 0; i < NUM_BURNS; ++i) {
        // Create unique burn
        CPubKey recipientPubKey = GenerateValidCompressedPubKey();
        CAmount burnAmount = (i + 1) * 10 * COIN;
        
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        uint160 l2Recipient = recipientPubKey.GetID();
        
        // Execute full flow
        bool success = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
        BOOST_CHECK_MESSAGE(success, "Burn-mint flow " << i << " should succeed");
        
        if (success) {
            totalExpectedSupply += burnAmount;
            expectedBalances[l2Recipient] += burnAmount;
        }
        
        // Clear consensus for next iteration
        consensusManager->Clear();
    }
    
    // Verify total supply
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), totalExpectedSupply);
    
    // Verify individual balances
    for (const auto& pair : expectedBalances) {
        BOOST_CHECK_EQUAL(minter->GetBalance(pair.first), pair.second);
    }
    
    // Verify supply invariant
    BOOST_CHECK(minter->VerifySupplyInvariant());
}

/**
 * @brief Test burn-mint flow with same recipient multiple times
 * 
 * Requirements: 1.1-1.6, 4.1-4.6
 */
BOOST_AUTO_TEST_CASE(integration_burn_mint_same_recipient)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    uint160 l2Recipient = recipientPubKey.GetID();
    CAmount totalExpected = 0;
    
    for (int i = 0; i < 3; ++i) {
        CAmount burnAmount = (i + 1) * 50 * COIN;
        
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        
        bool success = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
        BOOST_CHECK(success);
        
        if (success) {
            totalExpected += burnAmount;
        }
        
        consensusManager->Clear();
    }
    
    // Verify cumulative balance
    BOOST_CHECK_EQUAL(minter->GetBalance(l2Recipient), totalExpected);
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), totalExpected);
}

// ============================================================================
// Task 16.2: Test Multi-Sequencer Consensus
// Requirements: 3.1, 3.4, 10.3
// ============================================================================

/**
 * @brief Test 2/3 consensus threshold with 5 sequencers
 * 
 * With 5 sequencers, need ceil(5 * 2/3) = 4 confirmations.
 * Tests that:
 * - 3 confirmations do NOT reach consensus
 * - 4 confirmations DO reach consensus
 * 
 * Requirements: 3.1, 3.4, 10.3
 */
BOOST_AUTO_TEST_CASE(integration_multi_sequencer_consensus_threshold)
{
    // Setup: 5 sequencers (already done in fixture)
    BOOST_CHECK_EQUAL(sequencers.size(), 5u);
    
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    uint160 l2Recipient = recipientPubKey.GetID();
    
    // Test with 3 confirmations - should NOT reach consensus
    SubmitConfirmations(3, l1TxHash, l2Recipient, burnAmount);
    
    BOOST_CHECK_MESSAGE(!consensusManager->HasConsensus(l1TxHash),
        "3/5 confirmations should NOT reach consensus");
    
    auto state = consensusManager->GetConsensusState(l1TxHash);
    BOOST_REQUIRE(state.has_value());
    BOOST_CHECK_EQUAL(state->GetConfirmationCount(), 3u);
    BOOST_CHECK_CLOSE(state->GetConfirmationRatio(5), 0.6, 0.01);
    
    // Add 4th confirmation - should reach consensus
    auto conf4 = CreateSequencerConfirmation(3, l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK(consensusManager->ProcessConfirmation(conf4));
    
    BOOST_CHECK_MESSAGE(consensusManager->HasConsensus(l1TxHash),
        "4/5 confirmations should reach consensus");
    
    state = consensusManager->GetConsensusState(l1TxHash);
    BOOST_REQUIRE(state.has_value());
    BOOST_CHECK_EQUAL(state->GetConfirmationCount(), 4u);
    BOOST_CHECK_CLOSE(state->GetConfirmationRatio(5), 0.8, 0.01);
}

/**
 * @brief Test consensus with varying sequencer counts
 * 
 * Tests the 2/3 threshold calculation for different network sizes.
 * 
 * Requirements: 3.1, 3.4, 10.3
 */
BOOST_AUTO_TEST_CASE(integration_consensus_varying_sequencer_counts)
{
    // Test cases: (sequencer_count, threshold_needed)
    std::vector<std::pair<size_t, size_t>> testCases = {
        {3, 2},   // 3 sequencers: need 2
        {4, 3},   // 4 sequencers: need 3
        {5, 4},   // 5 sequencers: need 4
        {6, 4},   // 6 sequencers: need 4
        {7, 5},   // 7 sequencers: need 5
        {9, 6},   // 9 sequencers: need 6
        {10, 7},  // 10 sequencers: need 7
    };
    
    for (const auto& tc : testCases) {
        Reset();
        SetupSequencers(tc.first);
        
        CPubKey recipientPubKey = GenerateValidCompressedPubKey();
        CAmount burnAmount = 100 * COIN;
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        uint160 l2Recipient = recipientPubKey.GetID();
        
        // Submit threshold - 1 confirmations (should NOT reach consensus)
        SubmitConfirmations(tc.second - 1, l1TxHash, l2Recipient, burnAmount);
        
        BOOST_CHECK_MESSAGE(!consensusManager->HasConsensus(l1TxHash),
            "With " << tc.first << " sequencers, " << (tc.second - 1) 
            << " confirmations should NOT reach consensus");
        
        // Submit one more (should reach consensus)
        auto conf = CreateSequencerConfirmation(tc.second - 1, l1TxHash, l2Recipient, burnAmount);
        consensusManager->ProcessConfirmation(conf);
        
        BOOST_CHECK_MESSAGE(consensusManager->HasConsensus(l1TxHash),
            "With " << tc.first << " sequencers, " << tc.second 
            << " confirmations should reach consensus");
    }
}

/**
 * @brief Test minimum sequencer requirement (need at least 3)
 * 
 * Requirements: 3.1, 10.3
 */
BOOST_AUTO_TEST_CASE(integration_minimum_sequencer_requirement)
{
    // Test with 1 sequencer
    Reset();
    SetupSequencers(1);
    
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    CTransaction burnTx1 = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx1.GetHash();
    uint160 l2Recipient = recipientPubKey.GetID();
    
    SubmitConfirmations(1, l1TxHash, l2Recipient, burnAmount);
    
    BOOST_CHECK_MESSAGE(!consensusManager->HasConsensus(l1TxHash),
        "Should NOT reach consensus with only 1 sequencer");
    
    // Test with 2 sequencers
    Reset();
    SetupSequencers(2);
    
    CTransaction burnTx2 = CreateBurnTransaction(recipientPubKey, burnAmount);
    l1TxHash = burnTx2.GetHash();
    
    SubmitConfirmations(2, l1TxHash, l2Recipient, burnAmount);
    
    BOOST_CHECK_MESSAGE(!consensusManager->HasConsensus(l1TxHash),
        "Should NOT reach consensus with only 2 sequencers");
    
    // Test with 3 sequencers (minimum)
    Reset();
    SetupSequencers(3);
    
    CTransaction burnTx3 = CreateBurnTransaction(recipientPubKey, burnAmount);
    l1TxHash = burnTx3.GetHash();
    
    SubmitConfirmations(2, l1TxHash, l2Recipient, burnAmount);
    
    BOOST_CHECK_MESSAGE(consensusManager->HasConsensus(l1TxHash),
        "Should reach consensus with 3 sequencers and 2 confirmations");
}

// ============================================================================
// Task 16.3: Test Double-Mint Prevention
// Requirements: 5.3, 5.4
// ============================================================================

/**
 * @brief Test that same burn TX cannot be minted twice
 * 
 * Requirements: 5.3, 5.4
 */
BOOST_AUTO_TEST_CASE(integration_double_mint_prevention)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    uint160 l2Recipient = recipientPubKey.GetID();
    
    // First mint should succeed
    bool firstSuccess = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
    BOOST_CHECK(firstSuccess);
    
    CAmount supplyAfterFirst = minter->GetTotalSupply();
    CAmount balanceAfterFirst = minter->GetBalance(l2Recipient);
    
    // Clear consensus but keep burn registry
    consensusManager->Clear();
    
    // Second mint attempt with same L1 TX hash should fail
    SubmitConfirmations(4, l1TxHash, l2Recipient, burnAmount);
    
    // Even with consensus, minting should fail because already processed
    l2::MintResult secondResult = minter->MintTokens(l1TxHash, l2Recipient, burnAmount);
    
    BOOST_CHECK_MESSAGE(!secondResult.success,
        "Second mint attempt should fail");
    BOOST_CHECK(secondResult.errorMessage.find("already processed") != std::string::npos);
    
    // Supply and balance should not change
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), supplyAfterFirst);
    BOOST_CHECK_EQUAL(minter->GetBalance(l2Recipient), balanceAfterFirst);
    
    // Verify burn is still marked as processed
    BOOST_CHECK(burnRegistry.IsProcessed(l1TxHash));
}

/**
 * @brief Test double-mint prevention with different amounts
 * 
 * Even if attacker tries to mint different amount, should be rejected.
 * 
 * Requirements: 5.3, 5.4
 */
BOOST_AUTO_TEST_CASE(integration_double_mint_different_amount)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount originalAmount = 100 * COIN;
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, originalAmount);
    uint256 l1TxHash = burnTx.GetHash();
    uint160 l2Recipient = recipientPubKey.GetID();
    
    // First mint
    bool firstSuccess = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, originalAmount, 4);
    BOOST_CHECK(firstSuccess);
    
    CAmount supplyAfterFirst = minter->GetTotalSupply();
    
    // Try to mint again with different amount
    consensusManager->Clear();
    CAmount differentAmount = 200 * COIN;
    
    SubmitConfirmations(4, l1TxHash, l2Recipient, differentAmount);
    
    l2::MintResult secondResult = minter->MintTokens(l1TxHash, l2Recipient, differentAmount);
    
    BOOST_CHECK(!secondResult.success);
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), supplyAfterFirst);
}

/**
 * @brief Test double-mint prevention with different recipient
 * 
 * Requirements: 5.3, 5.4
 */
BOOST_AUTO_TEST_CASE(integration_double_mint_different_recipient)
{
    CPubKey recipientPubKey1 = GenerateValidCompressedPubKey();
    CPubKey recipientPubKey2 = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey1, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    uint160 l2Recipient2 = recipientPubKey2.GetID();
    
    // First mint to recipient1
    bool firstSuccess = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey1, burnAmount, 4);
    BOOST_CHECK(firstSuccess);

    
    CAmount supplyAfterFirst = minter->GetTotalSupply();
    
    // Try to mint to different recipient
    consensusManager->Clear();
    
    SubmitConfirmations(4, l1TxHash, l2Recipient2, burnAmount);
    
    l2::MintResult secondResult = minter->MintTokens(l1TxHash, l2Recipient2, burnAmount);
    
    BOOST_CHECK(!secondResult.success);
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), supplyAfterFirst);
    BOOST_CHECK_EQUAL(minter->GetBalance(l2Recipient2), 0);
}

// ============================================================================
// Task 16.4: Test Fee Distribution
// Requirements: 6.1, 6.2, 6.3
// ============================================================================

/**
 * @brief Test that sequencer receives fees from block transactions
 * 
 * Requirements: 6.1, 6.2, 6.3
 */
BOOST_AUTO_TEST_CASE(integration_fee_distribution_basic)
{
    // Get sequencer address (block producer)
    uint160 blockProducer = sequencers[0].second.GetID();
    
    // Create transactions with fees
    std::vector<l2::L2Transaction> transactions;
    CAmount totalExpectedFees = 0;
    
    for (int i = 0; i < 5; ++i) {
        l2::L2Transaction tx = CreateL2Transaction(
            RandomAddress(),  // from
            RandomAddress(),  // to
            10 * COIN,        // value
            1000,             // gasPrice (satoshis per gas unit)
            21000             // gasLimit
        );
        tx.gasUsed = 21000;  // Simulate execution
        transactions.push_back(tx);
        totalExpectedFees += tx.gasUsed * tx.gasPrice;
    }
    
    // Calculate expected fees
    CAmount calculatedFees = feeDistributor.CalculateBlockFees(transactions);
    BOOST_CHECK_EQUAL(calculatedFees, totalExpectedFees);
    
    // Distribute fees
    bool distributed = feeDistributor.DistributeBlockFees(
        currentL2Block, blockProducer, transactions);
    BOOST_CHECK(distributed);
    
    // Verify sequencer received fees
    CAmount feesEarned = feeDistributor.GetTotalFeesEarned(blockProducer);
    BOOST_CHECK_EQUAL(feesEarned, totalExpectedFees);
}

/**
 * @brief Test that no new tokens are minted as block rewards
 * 
 * Requirements: 6.1, 6.2
 */
BOOST_AUTO_TEST_CASE(integration_no_minting_for_block_rewards)
{
    // First, mint some tokens via burn-and-mint
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 1000 * COIN;
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    
    bool mintSuccess = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
    BOOST_CHECK(mintSuccess);
    
    CAmount supplyBeforeBlock = minter->GetTotalSupply();
    
    // Now produce a block with transactions
    uint160 blockProducer = sequencers[0].second.GetID();
    
    std::vector<l2::L2Transaction> transactions;
    for (int i = 0; i < 3; ++i) {
        l2::L2Transaction tx = CreateL2Transaction(
            RandomAddress(), RandomAddress(), 1 * COIN, 1000, 21000);
        tx.gasUsed = 21000;
        transactions.push_back(tx);
    }
    
    // Distribute fees (this should NOT mint new tokens)
    feeDistributor.DistributeBlockFees(currentL2Block, blockProducer, transactions);
    
    // Verify total supply has NOT changed
    CAmount supplyAfterBlock = minter->GetTotalSupply();
    BOOST_CHECK_EQUAL(supplyAfterBlock, supplyBeforeBlock);
    
    // Verify supply invariant still holds
    BOOST_CHECK(minter->VerifySupplyInvariant());
}

/**
 * @brief Test fee distribution with empty block
 * 
 * Requirements: 6.5
 */
BOOST_AUTO_TEST_CASE(integration_fee_distribution_empty_block)
{
    uint160 blockProducer = sequencers[0].second.GetID();
    
    // Empty transaction list
    std::vector<l2::L2Transaction> emptyTransactions;
    
    // Calculate fees (should be 0)
    CAmount fees = feeDistributor.CalculateBlockFees(emptyTransactions);
    BOOST_CHECK_EQUAL(fees, 0);
    
    // Distribute (should succeed but with 0 fees)
    bool distributed = feeDistributor.DistributeBlockFees(
        currentL2Block, blockProducer, emptyTransactions);
    BOOST_CHECK(distributed);
    
    // Sequencer should have earned 0 fees from this block
    // (may have fees from previous blocks)
}

// ============================================================================
// Task 16.5: Test Supply Invariant
// Requirements: 8.1, 8.3, 8.4
// ============================================================================

/**
 * @brief Test supply invariant after multiple burns and operations
 * 
 * Requirements: 8.1, 8.3, 8.4
 */
BOOST_AUTO_TEST_CASE(integration_supply_invariant_multiple_operations)
{
    CAmount totalBurnedL1 = 0;
    std::map<uint160, CAmount> expectedBalances;
    
    // Perform multiple burn-mint operations
    for (int i = 0; i < 10; ++i) {
        CPubKey recipientPubKey = GenerateValidCompressedPubKey();
        CAmount burnAmount = (i + 1) * 25 * COIN;
        
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        
        bool success = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
        
        if (success) {
            totalBurnedL1 += burnAmount;
            expectedBalances[recipientPubKey.GetID()] += burnAmount;
        }
        
        consensusManager->Clear();
    }
    
    // Invariant 1: total_supply == total_burned_l1
    CAmount totalSupply = minter->GetTotalSupply();
    CAmount totalBurned = minter->GetTotalBurnedL1();
    
    BOOST_CHECK_EQUAL(totalSupply, totalBurnedL1);
    BOOST_CHECK_EQUAL(totalBurned, totalBurnedL1);
    BOOST_CHECK_EQUAL(totalSupply, totalBurned);
    
    // Invariant 2: total_supply == sum(balances)
    CAmount sumOfBalances = 0;
    for (const auto& pair : expectedBalances) {
        CAmount balance = minter->GetBalance(pair.first);
        BOOST_CHECK_EQUAL(balance, pair.second);
        sumOfBalances += balance;
    }
    
    BOOST_CHECK_EQUAL(totalSupply, sumOfBalances);
    
    // Invariant 3: VerifySupplyInvariant should return true
    BOOST_CHECK(minter->VerifySupplyInvariant());
}

/**
 * @brief Test supply invariant with same recipient multiple times
 * 
 * Requirements: 8.1, 8.3
 */
BOOST_AUTO_TEST_CASE(integration_supply_invariant_same_recipient)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    uint160 l2Recipient = recipientPubKey.GetID();
    CAmount totalExpected = 0;
    
    for (int i = 0; i < 5; ++i) {
        CAmount burnAmount = 100 * COIN;
        
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        
        bool success = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
        
        if (success) {
            totalExpected += burnAmount;
        }
        
        consensusManager->Clear();
    }
    
    // All invariants should hold
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), totalExpected);
    BOOST_CHECK_EQUAL(minter->GetBalance(l2Recipient), totalExpected);
    BOOST_CHECK_EQUAL(minter->GetTotalBurnedL1(), totalExpected);
    BOOST_CHECK(minter->VerifySupplyInvariant());
}

/**
 * @brief Test supply invariant is maintained after failed operations
 * 
 * Requirements: 8.1, 8.3
 */
BOOST_AUTO_TEST_CASE(integration_supply_invariant_after_failures)
{
    // First, do a successful mint
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    
    bool success = ExecuteFullBurnMintFlow(l1TxHash, recipientPubKey, burnAmount, 4);
    BOOST_CHECK(success);
    
    CAmount supplyAfterSuccess = minter->GetTotalSupply();
    
    // Now try various failed operations
    
    // 1. Double-mint attempt
    consensusManager->Clear();
    SubmitConfirmations(4, l1TxHash, recipientPubKey.GetID(), burnAmount);
    l2::MintResult doubleMint = minter->MintTokens(l1TxHash, recipientPubKey.GetID(), burnAmount);
    BOOST_CHECK(!doubleMint.success);
    
    // 2. Mint with null hash
    l2::MintResult nullHash = minter->MintTokens(uint256(), recipientPubKey.GetID(), burnAmount);
    BOOST_CHECK(!nullHash.success);
    
    // 3. Mint with zero amount
    l2::MintResult zeroAmount = minter->MintTokens(RandomHash(), recipientPubKey.GetID(), 0);
    BOOST_CHECK(!zeroAmount.success);
    
    // Supply should not have changed
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), supplyAfterSuccess);
    
    // Invariant should still hold
    BOOST_CHECK(minter->VerifySupplyInvariant());
}

// ============================================================================
// Task 16.6: Test L2 Reorg Handling
// Requirements: 5.6
// ============================================================================

/**
 * @brief Test burn registry handles L2 reorg correctly
 * 
 * Requirements: 5.6
 */
BOOST_AUTO_TEST_CASE(integration_l2_reorg_handling)
{
    // Create burns at different L2 blocks
    std::vector<uint256> burnHashes;
    std::vector<uint64_t> burnBlocks;
    
    for (int i = 0; i < 5; ++i) {
        CPubKey recipientPubKey = GenerateValidCompressedPubKey();
        CAmount burnAmount = 100 * COIN;
        
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        uint160 l2Recipient = recipientPubKey.GetID();
        
        // Set different L2 block for each burn
        uint64_t l2Block = 100 + (i * 10);  // 100, 110, 120, 130, 140
        minter->SetCurrentBlockNumber(l2Block);
        
        // Execute burn-mint flow
        SubmitConfirmations(4, l1TxHash, l2Recipient, burnAmount);
        
        if (consensusManager->HasConsensus(l1TxHash)) {
            l2::MintResult result = minter->MintTokens(l1TxHash, l2Recipient, burnAmount);
            if (result.success) {
                burnHashes.push_back(l1TxHash);
                burnBlocks.push_back(l2Block);
                
                // Also record in burn registry with block info
                l2::BurnRecord record;
                record.l1TxHash = l1TxHash;
                record.l1BlockNumber = currentL1Block;
                record.l1BlockHash = RandomHash();
                record.l2Recipient = l2Recipient;
                record.amount = burnAmount;
                record.l2MintBlock = l2Block;
                record.l2MintTxHash = result.l2TxHash;
                record.timestamp = GetCurrentTimestamp();
                burnRegistry.RecordBurn(record);
            }
        }
        
        consensusManager->Clear();
    }
    
    BOOST_REQUIRE_EQUAL(burnHashes.size(), 5u);
    
    // All burns should be processed
    for (const auto& hash : burnHashes) {
        BOOST_CHECK(burnRegistry.IsProcessed(hash));
    }
    
    size_t countBefore = burnRegistry.GetBurnCount();
    BOOST_CHECK_EQUAL(countBefore, 5u);
    
    // Simulate L2 reorg from block 125
    // This should remove burns at blocks 130 and 140
    uint64_t reorgBlock = 125;
    size_t removed = burnRegistry.HandleReorg(reorgBlock);
    
    BOOST_CHECK_EQUAL(removed, 2u);  // Burns at 130 and 140
    BOOST_CHECK_EQUAL(burnRegistry.GetBurnCount(), 3u);
    
    // Burns at 100, 110, 120 should still be processed
    BOOST_CHECK(burnRegistry.IsProcessed(burnHashes[0]));  // Block 100
    BOOST_CHECK(burnRegistry.IsProcessed(burnHashes[1]));  // Block 110
    BOOST_CHECK(burnRegistry.IsProcessed(burnHashes[2]));  // Block 120
    
    // Burns at 130, 140 should no longer be processed
    BOOST_CHECK(!burnRegistry.IsProcessed(burnHashes[3]));  // Block 130
    BOOST_CHECK(!burnRegistry.IsProcessed(burnHashes[4]));  // Block 140
}

/**
 * @brief Test that reorg allows re-processing of removed burns
 * 
 * Requirements: 5.6
 */
BOOST_AUTO_TEST_CASE(integration_reorg_allows_reprocessing)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    uint160 l2Recipient = recipientPubKey.GetID();
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    
    // Set L2 block
    uint64_t l2Block = 200;
    minter->SetCurrentBlockNumber(l2Block);
    
    // First mint
    SubmitConfirmations(4, l1TxHash, l2Recipient, burnAmount);
    l2::MintResult result1 = minter->MintTokens(l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK(result1.success);
    
    // Record in registry
    l2::BurnRecord record;
    record.l1TxHash = l1TxHash;
    record.l1BlockNumber = currentL1Block;
    record.l1BlockHash = RandomHash();
    record.l2Recipient = l2Recipient;
    record.amount = burnAmount;
    record.l2MintBlock = l2Block;
    record.l2MintTxHash = result1.l2TxHash;
    record.timestamp = GetCurrentTimestamp();
    burnRegistry.RecordBurn(record);
    
    BOOST_CHECK(burnRegistry.IsProcessed(l1TxHash));
    
    // Simulate reorg that removes this burn
    burnRegistry.HandleReorg(l2Block - 10);
    
    BOOST_CHECK(!burnRegistry.IsProcessed(l1TxHash));
    
    // Should be able to record the same burn again
    BOOST_CHECK(burnRegistry.RecordBurn(record));
    BOOST_CHECK(burnRegistry.IsProcessed(l1TxHash));
}

/**
 * @brief Test reorg with no affected burns
 * 
 * Requirements: 5.6
 */
BOOST_AUTO_TEST_CASE(integration_reorg_no_affected_burns)
{
    // Create burns at early blocks
    for (int i = 0; i < 3; ++i) {
        CPubKey recipientPubKey = GenerateValidCompressedPubKey();
        CAmount burnAmount = 50 * COIN;
        uint160 l2Recipient = recipientPubKey.GetID();
        
        CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
        uint256 l1TxHash = burnTx.GetHash();
        
        uint64_t l2Block = 10 + i;  // Blocks 10, 11, 12
        minter->SetCurrentBlockNumber(l2Block);
        
        SubmitConfirmations(4, l1TxHash, l2Recipient, burnAmount);
        minter->MintTokens(l1TxHash, l2Recipient, burnAmount);
        
        l2::BurnRecord record;
        record.l1TxHash = l1TxHash;
        record.l1BlockNumber = currentL1Block;
        record.l1BlockHash = RandomHash();
        record.l2Recipient = l2Recipient;
        record.amount = burnAmount;
        record.l2MintBlock = l2Block;
        record.l2MintTxHash = RandomHash();
        record.timestamp = GetCurrentTimestamp();
        burnRegistry.RecordBurn(record);
        
        consensusManager->Clear();
    }
    
    size_t countBefore = burnRegistry.GetBurnCount();
    BOOST_CHECK_EQUAL(countBefore, 3u);
    
    // Reorg from block 100 (after all burns)
    size_t removed = burnRegistry.HandleReorg(100);
    
    BOOST_CHECK_EQUAL(removed, 0u);
    BOOST_CHECK_EQUAL(burnRegistry.GetBurnCount(), 3u);
}

// ============================================================================
// Additional Integration Tests
// ============================================================================

/**
 * @brief Test confirmation uniqueness in integration context
 * 
 * Requirements: 3.6
 */
BOOST_AUTO_TEST_CASE(integration_confirmation_uniqueness)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    uint160 l2Recipient = recipientPubKey.GetID();
    
    CTransaction burnTx = CreateBurnTransaction(recipientPubKey, burnAmount);
    uint256 l1TxHash = burnTx.GetHash();
    
    // Submit first confirmation from sequencer 0
    auto conf1 = CreateSequencerConfirmation(0, l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK(consensusManager->ProcessConfirmation(conf1));
    
    auto state = consensusManager->GetConsensusState(l1TxHash);
    BOOST_REQUIRE(state.has_value());
    BOOST_CHECK_EQUAL(state->GetConfirmationCount(), 1u);
    
    // Try duplicate from same sequencer
    auto conf1_dup = CreateSequencerConfirmation(0, l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK(!consensusManager->ProcessConfirmation(conf1_dup));
    
    // Count should still be 1
    state = consensusManager->GetConsensusState(l1TxHash);
    BOOST_CHECK_EQUAL(state->GetConfirmationCount(), 1u);
    
    // Confirmation from different sequencer should work
    auto conf2 = CreateSequencerConfirmation(1, l1TxHash, l2Recipient, burnAmount);
    BOOST_CHECK(consensusManager->ProcessConfirmation(conf2));
    
    state = consensusManager->GetConsensusState(l1TxHash);
    BOOST_CHECK_EQUAL(state->GetConfirmationCount(), 2u);
}

/**
 * @brief Test chain ID validation in burn parsing
 * 
 * Requirements: 2.3
 */
BOOST_AUTO_TEST_CASE(integration_chain_id_validation)
{
    CPubKey recipientPubKey = GenerateValidCompressedPubKey();
    CAmount burnAmount = 100 * COIN;
    
    // Create burn with correct chain ID
    CScript correctScript = l2::BurnTransactionParser::CreateBurnScript(
        chainId, recipientPubKey, burnAmount);
    BOOST_CHECK(!correctScript.empty());
    
    auto burnData = l2::BurnData::Parse(correctScript);
    BOOST_REQUIRE(burnData.has_value());
    BOOST_CHECK_EQUAL(burnData->chainId, chainId);
    
    // Create burn with wrong chain ID
    uint32_t wrongChainId = chainId + 1;
    CScript wrongScript = l2::BurnTransactionParser::CreateBurnScript(
        wrongChainId, recipientPubKey, burnAmount);
    BOOST_CHECK(!wrongScript.empty());
    
    auto wrongBurnData = l2::BurnData::Parse(wrongScript);
    BOOST_REQUIRE(wrongBurnData.has_value());
    BOOST_CHECK_EQUAL(wrongBurnData->chainId, wrongChainId);
    BOOST_CHECK(wrongBurnData->chainId != chainId);
}

/**
 * @brief Test complete flow with all components working together
 * 
 * This is the main integration test that exercises all components.
 */
BOOST_AUTO_TEST_CASE(integration_complete_system_flow)
{
    // 1. Create multiple users with burns
    struct UserBurn {
        CPubKey pubkey;
        CAmount amount;
        uint256 l1TxHash;
        bool minted;
    };
    
    std::vector<UserBurn> users;
    CAmount totalExpectedSupply = 0;
    
    for (int i = 0; i < 5; ++i) {
        UserBurn user;
        user.pubkey = GenerateValidCompressedPubKey();
        user.amount = (i + 1) * 100 * COIN;
        user.minted = false;
        
        CTransaction burnTx = CreateBurnTransaction(user.pubkey, user.amount);
        user.l1TxHash = burnTx.GetHash();
        
        users.push_back(user);
    }
    
    // 2. Process each burn through the full flow
    for (auto& user : users) {
        uint160 l2Recipient = user.pubkey.GetID();
        
        // Submit confirmations
        size_t accepted = SubmitConfirmations(4, user.l1TxHash, l2Recipient, user.amount);
        BOOST_CHECK_EQUAL(accepted, 4u);
        
        // Check consensus
        BOOST_CHECK(consensusManager->HasConsensus(user.l1TxHash));
        
        // Mint tokens
        l2::MintResult result = minter->MintTokens(user.l1TxHash, l2Recipient, user.amount);
        
        if (result.success) {
            user.minted = true;
            totalExpectedSupply += user.amount;
            
            // Verify balance
            BOOST_CHECK_EQUAL(minter->GetBalance(l2Recipient), user.amount);
        }
        
        consensusManager->Clear();
    }
    
    // 3. Verify all users were minted
    for (const auto& user : users) {
        BOOST_CHECK(user.minted);
    }
    
    // 4. Verify supply invariant
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), totalExpectedSupply);
    BOOST_CHECK(minter->VerifySupplyInvariant());
    
    // 5. Verify no double-mints possible
    for (const auto& user : users) {
        l2::MintResult doubleMint = minter->MintTokens(
            user.l1TxHash, user.pubkey.GetID(), user.amount);
        BOOST_CHECK(!doubleMint.success);
    }
    
    // 6. Supply should not have changed
    BOOST_CHECK_EQUAL(minter->GetTotalSupply(), totalExpectedSupply);
}

BOOST_AUTO_TEST_SUITE_END()
