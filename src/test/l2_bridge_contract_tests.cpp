// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_bridge_contract_tests.cpp
 * @brief Property-based tests for L2 Bridge Contract
 * 
 * **Feature: cascoin-l2-solution, Property 4: Deposit-Withdrawal Balance**
 * **Validates: Requirements 4.1, 4.2, 4.5**
 * 
 * Property 4: Deposit-Withdrawal Balance
 * *For any* sequence of deposits and withdrawals, the total value locked (TVL)
 * SHALL equal the sum of all deposits minus the sum of all completed withdrawals.
 * 
 * **Feature: cascoin-l2-solution, Property 12: Emergency Exit Completeness**
 * **Validates: Requirements 12.1, 12.2, 12.3**
 * 
 * Property 12: Emergency Exit Completeness
 * *For any* user with a valid balance proof, emergency withdrawal SHALL succeed
 * when emergency mode is active.
 */

#include <l2/bridge_contract.h>
#include <l2/sparse_merkle_tree.h>
#include <l2/account_state.h>
#include <l2/state_manager.h>
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
 * Helper function to generate a random uint160 address
 */
static uint160 RandomAddress160()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        uint32_t val = TestRand32();
        memcpy(addr.begin() + i * 4, &val, 4);
    }
    return addr;
}

/**
 * Helper function to generate a random deposit event
 */
static l2::DepositEvent RandomDeposit(uint64_t timestamp)
{
    l2::DepositEvent deposit;
    deposit.depositId = TestRand256();
    deposit.depositor = RandomAddress160();
    deposit.l2Recipient = RandomAddress160();
    // Keep amounts reasonable to avoid overflow and within limits
    deposit.amount = (TestRand64() % (l2::MAX_DEPOSIT_PER_TX / COIN)) * COIN + COIN;
    deposit.l1BlockNumber = TestRand64() % 1000000;
    deposit.l1TxHash = TestRand256();
    deposit.timestamp = timestamp;
    deposit.processed = false;
    return deposit;
}

/**
 * Helper function to generate a random withdrawal amount
 */
static CAmount RandomWithdrawalAmount()
{
    // Keep amounts reasonable
    return (TestRand64() % 1000 + 1) * COIN;
}

/**
 * Helper to create a valid balance proof using state manager
 */
static std::pair<uint256, std::vector<unsigned char>> CreateValidBalanceProof(
    const uint160& user, CAmount balance)
{
    l2::L2StateManager stateManager(1);
    
    // Set up account state with the balance
    uint256 addressKey = l2::AddressToKey(user);
    l2::AccountState state;
    state.balance = balance;
    state.nonce = 1;
    state.lastActivity = 1000;
    
    stateManager.SetAccountState(addressKey, state);
    
    // Get state root and generate proof
    uint256 stateRoot = stateManager.GetStateRoot();
    l2::MerkleProof proof = stateManager.GenerateAccountProof(addressKey);
    
    return {stateRoot, proof.Serialize()};
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_bridge_contract_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_bridge_has_zero_tvl)
{
    l2::BridgeContract bridge(1);
    
    BOOST_CHECK_EQUAL(bridge.GetTotalValueLocked(), 0);
    BOOST_CHECK_EQUAL(bridge.GetDepositCount(), 0u);
    BOOST_CHECK_EQUAL(bridge.GetWithdrawalCount(), 0u);
}

BOOST_AUTO_TEST_CASE(process_deposit_increases_tvl)
{
    l2::BridgeContract bridge(1);
    
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 100 * COIN;
    
    bool success = bridge.ProcessDeposit(deposit);
    BOOST_CHECK(success);
    
    BOOST_CHECK_EQUAL(bridge.GetTotalValueLocked(), 100 * COIN);
    BOOST_CHECK_EQUAL(bridge.GetDepositCount(), 1u);
    BOOST_CHECK(bridge.IsDepositProcessed(deposit.depositId));
}

BOOST_AUTO_TEST_CASE(duplicate_deposit_rejected)
{
    l2::BridgeContract bridge(1);
    
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 100 * COIN;
    
    BOOST_CHECK(bridge.ProcessDeposit(deposit));
    BOOST_CHECK(!bridge.ProcessDeposit(deposit));  // Duplicate rejected
    
    BOOST_CHECK_EQUAL(bridge.GetTotalValueLocked(), 100 * COIN);
    BOOST_CHECK_EQUAL(bridge.GetDepositCount(), 1u);
}

BOOST_AUTO_TEST_CASE(deposit_exceeding_limit_rejected)
{
    l2::BridgeContract bridge(1);
    
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = l2::MAX_DEPOSIT_PER_TX + COIN;  // Exceeds limit
    
    BOOST_CHECK(!bridge.ProcessDeposit(deposit));
    BOOST_CHECK_EQUAL(bridge.GetTotalValueLocked(), 0);
}

BOOST_AUTO_TEST_CASE(initiate_withdrawal_creates_pending)
{
    l2::BridgeContract bridge(1);
    
    // First deposit some funds
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 1000 * COIN;
    bridge.ProcessDeposit(deposit);
    
    // Initiate withdrawal
    uint160 sender = RandomAddress160();
    uint160 recipient = RandomAddress160();
    uint256 stateRoot = TestRand256();
    
    l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
        sender, recipient, 100 * COIN, 100, stateRoot, 2000, 50);
    
    BOOST_CHECK_EQUAL(request.amount, 100 * COIN);
    BOOST_CHECK(request.status == l2::WithdrawalStatus::PENDING);
    BOOST_CHECK(!request.isFastWithdrawal);  // HAT score 50 < 80
    BOOST_CHECK_EQUAL(bridge.GetWithdrawalCount(), 1u);
}

BOOST_AUTO_TEST_CASE(fast_withdrawal_for_high_reputation)
{
    l2::BridgeContract bridge(1);
    
    // Deposit funds
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 1000 * COIN;
    bridge.ProcessDeposit(deposit);
    
    // Fast withdrawal with high HAT score
    uint160 sender = RandomAddress160();
    uint160 recipient = RandomAddress160();
    uint256 stateRoot = TestRand256();
    
    l2::WithdrawalRequest request = bridge.FastWithdrawal(
        sender, recipient, 100 * COIN, 100, stateRoot, 2000, 85);
    
    BOOST_CHECK(request.isFastWithdrawal);
    BOOST_CHECK_EQUAL(request.hatScore, 85u);
    
    // Challenge period should be 1 day for high reputation
    uint64_t expectedDeadline = 2000 + l2::FAST_CHALLENGE_PERIOD;
    BOOST_CHECK_EQUAL(request.challengeDeadline, expectedDeadline);
}

BOOST_AUTO_TEST_CASE(standard_withdrawal_for_low_reputation)
{
    l2::BridgeContract bridge(1);
    
    // Deposit funds
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 1000 * COIN;
    bridge.ProcessDeposit(deposit);
    
    // Standard withdrawal with low HAT score
    uint160 sender = RandomAddress160();
    uint160 recipient = RandomAddress160();
    uint256 stateRoot = TestRand256();
    
    l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
        sender, recipient, 100 * COIN, 100, stateRoot, 2000, 50);
    
    BOOST_CHECK(!request.isFastWithdrawal);
    
    // Challenge period should be 7 days for low reputation
    uint64_t expectedDeadline = 2000 + l2::STANDARD_CHALLENGE_PERIOD;
    BOOST_CHECK_EQUAL(request.challengeDeadline, expectedDeadline);
}

BOOST_AUTO_TEST_CASE(finalize_withdrawal_after_challenge_period)
{
    l2::BridgeContract bridge(1);
    
    // Deposit funds
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 1000 * COIN;
    bridge.ProcessDeposit(deposit);
    
    CAmount initialTVL = bridge.GetTotalValueLocked();
    
    // Initiate withdrawal
    uint160 sender = RandomAddress160();
    uint160 recipient = RandomAddress160();
    uint256 stateRoot = TestRand256();
    
    l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
        sender, recipient, 100 * COIN, 100, stateRoot, 2000, 50);
    
    // Try to finalize before challenge period - should fail
    BOOST_CHECK(!bridge.FinalizeWithdrawal(request.withdrawalId, 3000));
    
    // Finalize after challenge period
    uint64_t afterChallenge = 2000 + l2::STANDARD_CHALLENGE_PERIOD + 1;
    BOOST_CHECK(bridge.FinalizeWithdrawal(request.withdrawalId, afterChallenge));
    
    // TVL should decrease
    BOOST_CHECK_EQUAL(bridge.GetTotalValueLocked(), initialTVL - 100 * COIN);
    
    // Status should be completed
    BOOST_CHECK(bridge.GetWithdrawalStatus(request.withdrawalId) == l2::WithdrawalStatus::COMPLETED);
}

BOOST_AUTO_TEST_CASE(challenge_withdrawal_changes_status)
{
    l2::BridgeContract bridge(1);
    
    // Deposit funds
    l2::DepositEvent deposit = RandomDeposit(1000);
    deposit.amount = 1000 * COIN;
    bridge.ProcessDeposit(deposit);
    
    // Initiate withdrawal
    uint160 sender = RandomAddress160();
    uint160 recipient = RandomAddress160();
    uint256 stateRoot = TestRand256();
    
    l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
        sender, recipient, 100 * COIN, 100, stateRoot, 2000, 50);
    
    // Challenge the withdrawal
    uint160 challenger = RandomAddress160();
    std::vector<unsigned char> fraudProof;
    
    BOOST_CHECK(bridge.ChallengeWithdrawal(request.withdrawalId, challenger, fraudProof, 3000));
    
    // Status should be challenged
    BOOST_CHECK(bridge.GetWithdrawalStatus(request.withdrawalId) == l2::WithdrawalStatus::CHALLENGED);
}

BOOST_AUTO_TEST_CASE(emergency_mode_detection)
{
    // No activity for 24+ hours should trigger emergency mode
    uint64_t lastActivity = 1000;
    uint64_t currentTime = lastActivity + l2::EMERGENCY_MODE_THRESHOLD + 1;
    
    BOOST_CHECK(l2::BridgeContract::IsEmergencyModeActive(lastActivity, currentTime));
    
    // Recent activity should not trigger emergency mode
    currentTime = lastActivity + 1000;
    BOOST_CHECK(!l2::BridgeContract::IsEmergencyModeActive(lastActivity, currentTime));
}

BOOST_AUTO_TEST_CASE(challenge_period_calculation)
{
    // High reputation (>= 80) gets fast withdrawal
    BOOST_CHECK_EQUAL(l2::BridgeContract::CalculateChallengePeriod(80), l2::FAST_CHALLENGE_PERIOD);
    BOOST_CHECK_EQUAL(l2::BridgeContract::CalculateChallengePeriod(90), l2::FAST_CHALLENGE_PERIOD);
    BOOST_CHECK_EQUAL(l2::BridgeContract::CalculateChallengePeriod(100), l2::FAST_CHALLENGE_PERIOD);
    
    // Low reputation gets standard withdrawal
    BOOST_CHECK_EQUAL(l2::BridgeContract::CalculateChallengePeriod(79), l2::STANDARD_CHALLENGE_PERIOD);
    BOOST_CHECK_EQUAL(l2::BridgeContract::CalculateChallengePeriod(50), l2::STANDARD_CHALLENGE_PERIOD);
    BOOST_CHECK_EQUAL(l2::BridgeContract::CalculateChallengePeriod(0), l2::STANDARD_CHALLENGE_PERIOD);
}

BOOST_AUTO_TEST_CASE(qualifies_for_fast_withdrawal)
{
    BOOST_CHECK(l2::BridgeContract::QualifiesForFastWithdrawal(80));
    BOOST_CHECK(l2::BridgeContract::QualifiesForFastWithdrawal(90));
    BOOST_CHECK(l2::BridgeContract::QualifiesForFastWithdrawal(100));
    
    BOOST_CHECK(!l2::BridgeContract::QualifiesForFastWithdrawal(79));
    BOOST_CHECK(!l2::BridgeContract::QualifiesForFastWithdrawal(50));
    BOOST_CHECK(!l2::BridgeContract::QualifiesForFastWithdrawal(0));
}

BOOST_AUTO_TEST_CASE(deposit_event_serialization_roundtrip)
{
    l2::DepositEvent original = RandomDeposit(1000);
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::DepositEvent restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(withdrawal_request_serialization_roundtrip)
{
    l2::WithdrawalRequest original;
    original.withdrawalId = TestRand256();
    original.l2Sender = RandomAddress160();
    original.l1Recipient = RandomAddress160();
    original.amount = 100 * COIN;
    original.l2BlockNumber = 12345;
    original.stateRoot = TestRand256();
    original.challengeDeadline = 1000000;
    original.initiatedAt = 500000;
    original.status = l2::WithdrawalStatus::PENDING;
    original.hatScore = 75;
    original.isFastWithdrawal = false;
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::WithdrawalRequest restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 4: Deposit-Withdrawal Balance**
 * 
 * *For any* sequence of deposits and completed withdrawals, the total value
 * locked (TVL) SHALL equal the sum of all deposits minus the sum of all
 * completed withdrawals.
 * 
 * **Validates: Requirements 4.1, 4.2, 4.5**
 */
BOOST_AUTO_TEST_CASE(property_deposit_withdrawal_balance)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        CAmount totalDeposited = 0;
        CAmount totalWithdrawn = 0;
        
        // Generate random deposits
        int numDeposits = 2 + (TestRand32() % 4);
        uint64_t timestamp = 1000;
        
        for (int i = 0; i < numDeposits; ++i) {
            l2::DepositEvent deposit = RandomDeposit(timestamp);
            // Use smaller amounts to stay within daily limits
            deposit.amount = (TestRand64() % 1000 + 1) * COIN;
            deposit.depositor = RandomAddress160();  // Different depositors to avoid daily limit
            
            if (bridge.ProcessDeposit(deposit)) {
                totalDeposited += deposit.amount;
            }
            timestamp += 100;
        }
        
        // Verify TVL equals total deposited
        BOOST_CHECK_MESSAGE(bridge.GetTotalValueLocked() == totalDeposited,
            "TVL should equal total deposited after deposits in iteration " << iteration);
        
        // Generate random withdrawals and finalize them
        int numWithdrawals = TestRand32() % 3;
        uint256 stateRoot = TestRand256();
        
        std::vector<uint256> withdrawalIds;
        for (int i = 0; i < numWithdrawals; ++i) {
            CAmount amount = (TestRand64() % 100 + 1) * COIN;
            if (amount <= totalDeposited - totalWithdrawn) {
                l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
                    RandomAddress160(), RandomAddress160(), amount,
                    100 + i, stateRoot, timestamp, 85);  // High HAT for fast withdrawal
                withdrawalIds.push_back(request.withdrawalId);
                timestamp += 100;
            }
        }
        
        // Finalize withdrawals after challenge period
        uint64_t afterChallenge = timestamp + l2::FAST_CHALLENGE_PERIOD + 1;
        for (const auto& id : withdrawalIds) {
            auto withdrawal = bridge.GetWithdrawal(id);
            if (withdrawal && bridge.FinalizeWithdrawal(id, afterChallenge)) {
                totalWithdrawn += withdrawal->amount;
            }
        }
        
        // Verify TVL equals deposits minus withdrawals
        CAmount expectedTVL = totalDeposited - totalWithdrawn;
        BOOST_CHECK_MESSAGE(bridge.GetTotalValueLocked() == expectedTVL,
            "TVL should equal deposits minus withdrawals in iteration " << iteration
            << " (expected " << expectedTVL << ", got " << bridge.GetTotalValueLocked() << ")");
    }
}

/**
 * **Property 4: Deposit-Withdrawal Balance (Invariant)**
 * 
 * *For any* state of the bridge, TVL SHALL never be negative.
 * 
 * **Validates: Requirements 4.5**
 */
BOOST_AUTO_TEST_CASE(property_tvl_never_negative)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        uint64_t timestamp = 1000;
        
        // Random sequence of deposits and withdrawals
        int numOperations = 5 + (TestRand32() % 10);
        
        for (int i = 0; i < numOperations; ++i) {
            bool isDeposit = (TestRand32() % 2 == 0) || bridge.GetTotalValueLocked() == 0;
            
            if (isDeposit) {
                l2::DepositEvent deposit = RandomDeposit(timestamp);
                deposit.amount = (TestRand64() % 100 + 1) * COIN;
                deposit.depositor = RandomAddress160();
                bridge.ProcessDeposit(deposit);
            } else {
                // Withdrawal
                CAmount tvl = bridge.GetTotalValueLocked();
                if (tvl > 0) {
                    CAmount amount = (TestRand64() % (tvl / COIN) + 1) * COIN;
                    if (amount > tvl) amount = tvl;
                    
                    l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
                        RandomAddress160(), RandomAddress160(), amount,
                        100, TestRand256(), timestamp, 85);
                    
                    // Finalize immediately (for testing)
                    uint64_t afterChallenge = timestamp + l2::FAST_CHALLENGE_PERIOD + 1;
                    bridge.FinalizeWithdrawal(request.withdrawalId, afterChallenge);
                }
            }
            
            // TVL should never be negative
            BOOST_CHECK_MESSAGE(bridge.GetTotalValueLocked() >= 0,
                "TVL should never be negative in iteration " << iteration);
            
            timestamp += 100;
        }
    }
}

/**
 * **Property: Challenge Period Consistency**
 * 
 * *For any* withdrawal, the challenge period SHALL be determined solely
 * by the user's HAT score at withdrawal time.
 * 
 * **Validates: Requirements 4.3, 4.4, 6.2**
 */
BOOST_AUTO_TEST_CASE(property_challenge_period_consistency)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::BridgeContract bridge(1);
        
        // Deposit funds
        l2::DepositEvent deposit = RandomDeposit(1000);
        deposit.amount = 10000 * COIN;
        bridge.ProcessDeposit(deposit);
        
        // Random HAT score
        uint32_t hatScore = TestRand32() % 101;
        uint64_t timestamp = 2000;
        
        l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
            RandomAddress160(), RandomAddress160(), 100 * COIN,
            100, TestRand256(), timestamp, hatScore);
        
        // Verify challenge period matches expected
        uint64_t expectedPeriod = l2::BridgeContract::CalculateChallengePeriod(hatScore);
        uint64_t actualPeriod = request.challengeDeadline - request.initiatedAt;
        
        BOOST_CHECK_MESSAGE(actualPeriod == expectedPeriod,
            "Challenge period should match HAT score calculation in iteration " << iteration
            << " (HAT=" << hatScore << ", expected=" << expectedPeriod << ", actual=" << actualPeriod << ")");
        
        // Verify fast withdrawal flag
        bool expectedFast = l2::BridgeContract::QualifiesForFastWithdrawal(hatScore);
        BOOST_CHECK_MESSAGE(request.isFastWithdrawal == expectedFast,
            "Fast withdrawal flag should match HAT score in iteration " << iteration);
    }
}

/**
 * **Property: Withdrawal Finalization Timing**
 * 
 * *For any* withdrawal, finalization SHALL only succeed after the
 * challenge period has passed.
 * 
 * **Validates: Requirements 4.3**
 */
BOOST_AUTO_TEST_CASE(property_withdrawal_finalization_timing)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        // Deposit funds
        l2::DepositEvent deposit = RandomDeposit(1000);
        deposit.amount = 10000 * COIN;
        bridge.ProcessDeposit(deposit);
        
        uint64_t timestamp = 2000;
        uint32_t hatScore = TestRand32() % 101;
        
        l2::WithdrawalRequest request = bridge.InitiateWithdrawal(
            RandomAddress160(), RandomAddress160(), 100 * COIN,
            100, TestRand256(), timestamp, hatScore);
        
        // Try to finalize at various times before deadline
        uint64_t deadline = request.challengeDeadline;
        
        // Before deadline - should fail
        for (int t = 0; t < 3; ++t) {
            uint64_t beforeDeadline = timestamp + (TestRand64() % (deadline - timestamp));
            BOOST_CHECK_MESSAGE(!bridge.FinalizeWithdrawal(request.withdrawalId, beforeDeadline),
                "Finalization should fail before deadline in iteration " << iteration);
        }
        
        // At or after deadline - should succeed
        uint64_t afterDeadline = deadline + (TestRand64() % 1000);
        BOOST_CHECK_MESSAGE(bridge.FinalizeWithdrawal(request.withdrawalId, afterDeadline),
            "Finalization should succeed after deadline in iteration " << iteration);
    }
}

/**
 * **Property: Deposit Idempotence**
 * 
 * *For any* deposit, processing it multiple times SHALL have the same
 * effect as processing it once (idempotent operation).
 * 
 * **Validates: Requirements 4.1**
 */
BOOST_AUTO_TEST_CASE(property_deposit_idempotence)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        l2::DepositEvent deposit = RandomDeposit(1000);
        deposit.amount = 100 * COIN;
        
        // Process deposit first time
        BOOST_CHECK(bridge.ProcessDeposit(deposit));
        CAmount tvlAfterFirst = bridge.GetTotalValueLocked();
        size_t countAfterFirst = bridge.GetDepositCount();
        
        // Try to process same deposit multiple times
        for (int i = 0; i < 3; ++i) {
            BOOST_CHECK(!bridge.ProcessDeposit(deposit));
        }
        
        // TVL and count should not change
        BOOST_CHECK_MESSAGE(bridge.GetTotalValueLocked() == tvlAfterFirst,
            "TVL should not change on duplicate deposits in iteration " << iteration);
        BOOST_CHECK_MESSAGE(bridge.GetDepositCount() == countAfterFirst,
            "Deposit count should not change on duplicates in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================================
// Emergency Exit Tests (Property 12)
// ============================================================================

BOOST_AUTO_TEST_SUITE(l2_bridge_emergency_exit_tests)

BOOST_AUTO_TEST_CASE(emergency_withdrawal_requires_emergency_mode)
{
    l2::BridgeContract bridge(1);
    
    // Deposit funds first (within per-tx limit)
    l2::DepositEvent deposit;
    deposit.depositId = TestRand256();
    deposit.depositor = RandomAddress160();
    deposit.l2Recipient = RandomAddress160();
    deposit.amount = 5000 * COIN;  // Within MAX_DEPOSIT_PER_TX
    deposit.l1BlockNumber = 100;
    deposit.l1TxHash = TestRand256();
    deposit.timestamp = 1000;
    BOOST_CHECK(bridge.ProcessDeposit(deposit));
    
    // Create valid balance proof
    uint160 user = RandomAddress160();
    auto [stateRoot, balanceProof] = CreateValidBalanceProof(user, 100 * COIN);
    
    // Emergency withdrawal should fail when not in emergency mode
    BOOST_CHECK(!bridge.IsInEmergencyMode());
    BOOST_CHECK(!bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, 100 * COIN, 2000));
    
    // Enable emergency mode
    bridge.SetEmergencyMode(true);
    BOOST_CHECK(bridge.IsInEmergencyMode());
    
    // Now emergency withdrawal should succeed
    BOOST_CHECK(bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, 100 * COIN, 2000));
}

BOOST_AUTO_TEST_CASE(emergency_withdrawal_prevents_double_withdrawal)
{
    l2::BridgeContract bridge(1);
    
    // Deposit funds (within per-tx limit)
    l2::DepositEvent deposit;
    deposit.depositId = TestRand256();
    deposit.depositor = RandomAddress160();
    deposit.l2Recipient = RandomAddress160();
    deposit.amount = 5000 * COIN;  // Within MAX_DEPOSIT_PER_TX
    deposit.l1BlockNumber = 100;
    deposit.l1TxHash = TestRand256();
    deposit.timestamp = 1000;
    BOOST_CHECK(bridge.ProcessDeposit(deposit));
    
    // Enable emergency mode
    bridge.SetEmergencyMode(true);
    
    // Create valid balance proof
    uint160 user = RandomAddress160();
    auto [stateRoot, balanceProof] = CreateValidBalanceProof(user, 100 * COIN);
    
    // First emergency withdrawal should succeed
    BOOST_CHECK(bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, 100 * COIN, 2000));
    
    // Second emergency withdrawal for same user should fail
    BOOST_CHECK(!bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, 100 * COIN, 3000));
}

BOOST_AUTO_TEST_CASE(emergency_withdrawal_validates_balance)
{
    l2::BridgeContract bridge(1);
    
    // Deposit limited funds
    l2::DepositEvent deposit;
    deposit.depositId = TestRand256();
    deposit.depositor = RandomAddress160();
    deposit.l2Recipient = RandomAddress160();
    deposit.amount = 100 * COIN;
    deposit.l1BlockNumber = 100;
    deposit.l1TxHash = TestRand256();
    deposit.timestamp = 1000;
    bridge.ProcessDeposit(deposit);
    
    // Enable emergency mode
    bridge.SetEmergencyMode(true);
    
    // Create balance proof for more than TVL
    uint160 user = RandomAddress160();
    auto [stateRoot, balanceProof] = CreateValidBalanceProof(user, 200 * COIN);
    
    // Emergency withdrawal for more than TVL should fail
    BOOST_CHECK(!bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, 200 * COIN, 2000));
    
    // Emergency withdrawal for valid amount should succeed
    auto [stateRoot2, balanceProof2] = CreateValidBalanceProof(user, 50 * COIN);
    BOOST_CHECK(bridge.EmergencyWithdrawal(user, stateRoot2, balanceProof2, 50 * COIN, 2000));
}

BOOST_AUTO_TEST_CASE(emergency_mode_activation_threshold)
{
    // Test the 24-hour threshold
    uint64_t lastActivity = 1000000;
    
    // Just under 24 hours - not emergency
    uint64_t justUnder = lastActivity + l2::EMERGENCY_MODE_THRESHOLD - 1;
    BOOST_CHECK(!l2::BridgeContract::IsEmergencyModeActive(lastActivity, justUnder));
    
    // Exactly 24 hours - emergency
    uint64_t exactly = lastActivity + l2::EMERGENCY_MODE_THRESHOLD;
    BOOST_CHECK(l2::BridgeContract::IsEmergencyModeActive(lastActivity, exactly));
    
    // Over 24 hours - emergency
    uint64_t over = lastActivity + l2::EMERGENCY_MODE_THRESHOLD + 3600;
    BOOST_CHECK(l2::BridgeContract::IsEmergencyModeActive(lastActivity, over));
}

/**
 * **Property 12: Emergency Exit Completeness**
 * 
 * *For any* user with a valid balance proof, emergency withdrawal SHALL succeed
 * when emergency mode is active and the claimed balance is valid.
 * 
 * **Validates: Requirements 12.1, 12.2, 12.3**
 */
BOOST_AUTO_TEST_CASE(property_emergency_exit_completeness)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        // Deposit funds using multiple deposits to stay within per-tx limit
        uint64_t timestamp = 1000;
        for (int d = 0; d < 10; ++d) {
            l2::DepositEvent deposit;
            deposit.depositId = TestRand256();
            deposit.depositor = RandomAddress160();  // Different depositors
            deposit.l2Recipient = RandomAddress160();
            deposit.amount = 9000 * COIN;  // Within MAX_DEPOSIT_PER_TX
            deposit.l1BlockNumber = 100 + d;
            deposit.l1TxHash = TestRand256();
            deposit.timestamp = timestamp + d * 100;
            bridge.ProcessDeposit(deposit);
        }
        
        // Enable emergency mode
        bridge.SetEmergencyMode(true);
        
        // Generate random users with valid balance proofs
        int numUsers = 2 + (TestRand32() % 3);
        
        for (int i = 0; i < numUsers; ++i) {
            uint160 user = RandomAddress160();
            CAmount balance = (TestRand64() % 1000 + 1) * COIN;
            
            // Create valid balance proof
            auto [stateRoot, balanceProof] = CreateValidBalanceProof(user, balance);
            
            // Emergency withdrawal should succeed for valid proof
            bool success = bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, balance, 2000 + i);
            
            BOOST_CHECK_MESSAGE(success,
                "Emergency withdrawal should succeed for valid proof in iteration " 
                << iteration << ", user " << i);
        }
    }
}

/**
 * **Property 12: Emergency Exit - Invalid Proof Rejection**
 * 
 * *For any* user with an invalid balance proof, emergency withdrawal SHALL fail
 * even when emergency mode is active.
 * 
 * **Validates: Requirements 12.2**
 */
BOOST_AUTO_TEST_CASE(property_emergency_exit_invalid_proof_rejection)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        // Deposit funds using multiple deposits
        uint64_t timestamp = 1000;
        for (int d = 0; d < 10; ++d) {
            l2::DepositEvent deposit;
            deposit.depositId = TestRand256();
            deposit.depositor = RandomAddress160();
            deposit.l2Recipient = RandomAddress160();
            deposit.amount = 9000 * COIN;
            deposit.l1BlockNumber = 100 + d;
            deposit.l1TxHash = TestRand256();
            deposit.timestamp = timestamp + d * 100;
            bridge.ProcessDeposit(deposit);
        }
        
        // Enable emergency mode
        bridge.SetEmergencyMode(true);
        
        uint160 user = RandomAddress160();
        CAmount claimedBalance = (TestRand64() % 1000 + 1) * COIN;
        
        // Create proof for different balance (invalid)
        CAmount actualBalance = claimedBalance + 100 * COIN;
        auto [stateRoot, balanceProof] = CreateValidBalanceProof(user, actualBalance);
        
        // Emergency withdrawal with mismatched balance should fail
        bool success = bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, claimedBalance, 2000);
        
        BOOST_CHECK_MESSAGE(!success,
            "Emergency withdrawal should fail for mismatched balance in iteration " << iteration);
    }
}

/**
 * **Property 12: Emergency Exit - TVL Consistency**
 * 
 * *For any* sequence of emergency withdrawals, the TVL SHALL decrease by
 * exactly the sum of withdrawn amounts.
 * 
 * **Validates: Requirements 12.1, 12.3**
 */
BOOST_AUTO_TEST_CASE(property_emergency_exit_tvl_consistency)
{
    // Run 5 iterations
    for (int iteration = 0; iteration < 5; ++iteration) {
        l2::BridgeContract bridge(1);
        
        // Deposit initial funds using multiple deposits
        CAmount totalDeposited = 0;
        uint64_t timestamp = 1000;
        for (int d = 0; d < 10; ++d) {
            l2::DepositEvent deposit;
            deposit.depositId = TestRand256();
            deposit.depositor = RandomAddress160();
            deposit.l2Recipient = RandomAddress160();
            deposit.amount = 9000 * COIN;
            deposit.l1BlockNumber = 100 + d;
            deposit.l1TxHash = TestRand256();
            deposit.timestamp = timestamp + d * 100;
            if (bridge.ProcessDeposit(deposit)) {
                totalDeposited += deposit.amount;
            }
        }
        
        CAmount initialTVL = bridge.GetTotalValueLocked();
        BOOST_CHECK_EQUAL(initialTVL, totalDeposited);
        
        // Enable emergency mode
        bridge.SetEmergencyMode(true);
        
        // Process emergency withdrawals
        CAmount totalWithdrawn = 0;
        int numWithdrawals = 2 + (TestRand32() % 3);
        
        for (int i = 0; i < numWithdrawals; ++i) {
            uint160 user = RandomAddress160();
            CAmount balance = (TestRand64() % 1000 + 1) * COIN;
            
            // Ensure we don't exceed TVL
            if (totalWithdrawn + balance > initialTVL) {
                balance = initialTVL - totalWithdrawn;
                if (balance <= 0) break;
            }
            
            auto [stateRoot, balanceProof] = CreateValidBalanceProof(user, balance);
            
            if (bridge.EmergencyWithdrawal(user, stateRoot, balanceProof, balance, 2000 + i)) {
                totalWithdrawn += balance;
            }
        }
        
        // Verify TVL consistency
        CAmount expectedTVL = initialTVL - totalWithdrawn;
        CAmount actualTVL = bridge.GetTotalValueLocked();
        
        BOOST_CHECK_MESSAGE(actualTVL == expectedTVL,
            "TVL should equal initial minus withdrawn in iteration " << iteration
            << " (expected " << expectedTVL << ", got " << actualTVL << ")");
    }
}

/**
 * **Property 12: Emergency Mode State Consistency**
 * 
 * *For any* bridge state, emergency mode flag SHALL be consistent with
 * the SetEmergencyMode/IsInEmergencyMode operations.
 * 
 * **Validates: Requirements 12.1**
 */
BOOST_AUTO_TEST_CASE(property_emergency_mode_state_consistency)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BridgeContract bridge(1);
        
        // Initially not in emergency mode
        BOOST_CHECK(!bridge.IsInEmergencyMode());
        
        // Random sequence of mode changes
        int numChanges = 3 + (TestRand32() % 5);
        bool expectedMode = false;
        
        for (int i = 0; i < numChanges; ++i) {
            bool newMode = (TestRand32() % 2 == 0);
            bridge.SetEmergencyMode(newMode);
            expectedMode = newMode;
            
            BOOST_CHECK_MESSAGE(bridge.IsInEmergencyMode() == expectedMode,
                "Emergency mode should match set value in iteration " << iteration
                << ", change " << i);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
