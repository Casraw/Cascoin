// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_challenge_handler_tests.cpp
 * @brief Property-based tests for L2 Challenge Handler
 * 
 * **Feature: cascoin-l2-solution, Property 16: Challenge Bond Slashing**
 * **Validates: Requirements 29.1, 29.2**
 * 
 * Property 16: Challenge Bond Slashing
 * *For any* invalid challenge, the challenger's bond SHALL be slashed
 * and distributed to the challenged party.
 */

#include <l2/challenge_handler.h>
#include <l2/bridge_contract.h>
#include <l2/fraud_proof.h>
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
 * Helper function to create a random withdrawal request
 */
static l2::WithdrawalRequest CreateRandomWithdrawal(uint64_t timestamp)
{
    l2::WithdrawalRequest request;
    request.withdrawalId = TestRand256();
    request.l2Sender = RandomAddress160();
    request.l1Recipient = RandomAddress160();
    request.amount = (TestRand64() % 1000 + 1) * COIN;
    request.l2BlockNumber = TestRand64() % 1000000;
    request.stateRoot = TestRand256();
    request.initiatedAt = timestamp;
    request.challengeDeadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
    request.status = l2::WithdrawalStatus::PENDING;
    request.hatScore = TestRand32() % 101;
    return request;
}

/**
 * Helper function to create a valid fraud proof
 */
static std::vector<unsigned char> CreateValidFraudProof(const uint256& stateRoot)
{
    l2::FraudProof proof;
    proof.type = l2::FraudProofType::INVALID_STATE_TRANSITION;
    proof.disputedStateRoot = stateRoot;
    proof.disputedBlockNumber = TestRand64() % 1000000;
    proof.previousStateRoot = TestRand256();
    proof.challengerAddress = RandomAddress160();
    proof.sequencerAddress = RandomAddress160();
    proof.challengeBond = l2::FRAUD_PROOF_CHALLENGE_BOND;
    proof.submittedAt = TestRand64();
    return proof.Serialize();
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_challenge_handler_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_handler_has_no_challenges)
{
    l2::ChallengeHandler handler(1);
    
    BOOST_CHECK_EQUAL(handler.GetTotalChallengeCount(), 0u);
    BOOST_CHECK_EQUAL(handler.GetActiveChallengeCount(), 0u);
    BOOST_CHECK_EQUAL(handler.GetTotalBondsHeld(), 0);
}

BOOST_AUTO_TEST_CASE(challenge_requires_registered_withdrawal)
{
    l2::ChallengeHandler handler(1);
    
    uint256 withdrawalId = TestRand256();
    uint160 challenger = RandomAddress160();
    
    // Challenge should fail - withdrawal not registered
    auto result = handler.ChallengeWithdrawal(
        withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", 1000, 50);
    
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(challenge_requires_minimum_bond)
{
    l2::ChallengeHandler handler(1);
    
    uint256 withdrawalId = TestRand256();
    uint160 challenger = RandomAddress160();
    uint64_t deadline = 1000 + l2::STANDARD_CHALLENGE_PERIOD;
    
    // Register withdrawal
    handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
    
    // Challenge with insufficient bond should fail
    auto result = handler.ChallengeWithdrawal(
        withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND - 1,
        {}, "Test challenge", 1000, 50);
    
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(valid_challenge_submission)
{
    l2::ChallengeHandler handler(1);
    
    uint256 withdrawalId = TestRand256();
    uint160 challenger = RandomAddress160();
    uint64_t timestamp = 1000;
    uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
    
    // Register withdrawal
    handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
    
    // Submit valid challenge
    auto result = handler.ChallengeWithdrawal(
        withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", timestamp, 50);
    
    BOOST_CHECK(result.has_value());
    BOOST_CHECK(result->status == l2::ChallengeStatus::PENDING);
    BOOST_CHECK_EQUAL(result->bondAmount, l2::WITHDRAWAL_CHALLENGE_BOND);
    BOOST_CHECK(result->challengerAddress == challenger);
    
    // Verify handler state
    BOOST_CHECK_EQUAL(handler.GetTotalChallengeCount(), 1u);
    BOOST_CHECK_EQUAL(handler.GetActiveChallengeCount(), 1u);
    BOOST_CHECK_EQUAL(handler.GetTotalBondsHeld(), l2::WITHDRAWAL_CHALLENGE_BOND);
}

BOOST_AUTO_TEST_CASE(challenge_limit_per_address)
{
    l2::ChallengeHandler handler(1);
    
    uint160 challenger = RandomAddress160();
    uint64_t timestamp = 1000;
    
    // Submit maximum allowed challenges
    for (uint32_t i = 0; i < l2::MAX_CHALLENGES_PER_ADDRESS; ++i) {
        uint256 withdrawalId = TestRand256();
        uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
        handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
        
        auto result = handler.ChallengeWithdrawal(
            withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
            {}, "Test challenge", timestamp, 50);
        
        BOOST_CHECK(result.has_value());
    }
    
    // Next challenge should fail due to limit
    uint256 extraWithdrawalId = TestRand256();
    handler.RegisterChallengeableWithdrawal(extraWithdrawalId, 
        timestamp + l2::STANDARD_CHALLENGE_PERIOD);
    
    auto result = handler.ChallengeWithdrawal(
        extraWithdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", timestamp, 50);
    
    BOOST_CHECK(!result.has_value());
    BOOST_CHECK_EQUAL(handler.GetActiveChallengeCount(challenger), 
                      l2::MAX_CHALLENGES_PER_ADDRESS);
}

BOOST_AUTO_TEST_CASE(invalid_challenge_slashes_bond)
{
    l2::ChallengeHandler handler(1);
    
    uint256 withdrawalId = TestRand256();
    uint160 challenger = RandomAddress160();
    uint64_t timestamp = 1000;
    uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
    
    // Register withdrawal and submit challenge
    handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
    
    auto challenge = handler.ChallengeWithdrawal(
        withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", timestamp, 50);
    
    BOOST_REQUIRE(challenge.has_value());
    
    // Process as invalid challenge
    auto result = handler.ProcessChallengeResult(
        challenge->challengeId, false, timestamp + 1000);
    
    BOOST_CHECK(result.finalStatus == l2::ChallengeStatus::INVALID);
    BOOST_CHECK(result.bondSlashed);
    BOOST_CHECK_EQUAL(result.bondAmount, l2::WITHDRAWAL_CHALLENGE_BOND);
    
    // Bond should be released from handler
    BOOST_CHECK_EQUAL(handler.GetTotalBondsHeld(), 0);
    
    // Challenger stats should reflect invalid challenge
    auto stats = handler.GetChallengerStats(challenger);
    BOOST_CHECK_EQUAL(stats.invalidChallenges, 1u);
    BOOST_CHECK_EQUAL(stats.totalBondsLost, l2::WITHDRAWAL_CHALLENGE_BOND);
}

BOOST_AUTO_TEST_CASE(valid_challenge_returns_bond)
{
    l2::ChallengeHandler handler(1);
    
    uint256 withdrawalId = TestRand256();
    uint160 challenger = RandomAddress160();
    uint64_t timestamp = 1000;
    uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
    
    // Register withdrawal and submit challenge
    handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
    
    auto challenge = handler.ChallengeWithdrawal(
        withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", timestamp, 50);
    
    BOOST_REQUIRE(challenge.has_value());
    
    // Process as valid challenge
    auto result = handler.ProcessChallengeResult(
        challenge->challengeId, true, timestamp + 1000);
    
    BOOST_CHECK(result.finalStatus == l2::ChallengeStatus::VALID);
    BOOST_CHECK(!result.bondSlashed);
    BOOST_CHECK(result.bondRecipient == challenger);
    
    // Bond should be released from handler
    BOOST_CHECK_EQUAL(handler.GetTotalBondsHeld(), 0);
    
    // Challenger stats should reflect valid challenge
    auto stats = handler.GetChallengerStats(challenger);
    BOOST_CHECK_EQUAL(stats.validChallenges, 1u);
    BOOST_CHECK_EQUAL(stats.totalBondsReturned, l2::WITHDRAWAL_CHALLENGE_BOND);
}

BOOST_AUTO_TEST_CASE(challenger_banned_after_threshold)
{
    l2::ChallengeHandler handler(1);
    
    uint160 challenger = RandomAddress160();
    uint64_t timestamp = 1000;
    
    // Submit and invalidate challenges up to ban threshold
    for (uint32_t i = 0; i < l2::INVALID_CHALLENGE_BAN_THRESHOLD; ++i) {
        uint256 withdrawalId = TestRand256();
        uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
        handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
        
        auto challenge = handler.ChallengeWithdrawal(
            withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
            {}, "Test challenge", timestamp, 50);
        
        BOOST_REQUIRE(challenge.has_value());
        
        // Process as invalid
        handler.ProcessChallengeResult(challenge->challengeId, false, timestamp + 1000);
        timestamp += 100;
    }
    
    // Challenger should now be banned
    BOOST_CHECK(handler.IsChallengerBanned(challenger, timestamp));
    
    // New challenge should fail
    uint256 newWithdrawalId = TestRand256();
    handler.RegisterChallengeableWithdrawal(newWithdrawalId, 
        timestamp + l2::STANDARD_CHALLENGE_PERIOD);
    
    auto result = handler.ChallengeWithdrawal(
        newWithdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", timestamp, 50);
    
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(expired_challenges_processed)
{
    l2::ChallengeHandler handler(1);
    
    uint256 withdrawalId = TestRand256();
    uint160 challenger = RandomAddress160();
    uint64_t timestamp = 1000;
    uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
    
    // Register withdrawal and submit challenge
    handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
    
    auto challenge = handler.ChallengeWithdrawal(
        withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
        {}, "Test challenge", timestamp, 50);
    
    BOOST_REQUIRE(challenge.has_value());
    BOOST_CHECK_EQUAL(handler.GetActiveChallengeCount(), 1u);
    
    // Process expired challenges after deadline
    uint64_t afterDeadline = challenge->deadline + 1;
    size_t expired = handler.ProcessExpiredChallenges(afterDeadline);
    
    BOOST_CHECK_EQUAL(expired, 1u);
    BOOST_CHECK_EQUAL(handler.GetActiveChallengeCount(), 0u);
    
    // Bond should be returned (expired challenges don't lose bond)
    BOOST_CHECK_EQUAL(handler.GetTotalBondsHeld(), 0);
    
    auto stats = handler.GetChallengerStats(challenger);
    BOOST_CHECK_EQUAL(stats.expiredChallenges, 1u);
    BOOST_CHECK_EQUAL(stats.totalBondsReturned, l2::WITHDRAWAL_CHALLENGE_BOND);
}

BOOST_AUTO_TEST_CASE(challenge_serialization_roundtrip)
{
    l2::WithdrawalChallenge original;
    original.challengeId = TestRand256();
    original.withdrawalId = TestRand256();
    original.challengerAddress = RandomAddress160();
    original.bondAmount = l2::WITHDRAWAL_CHALLENGE_BOND;
    original.reason = "Test challenge reason";
    original.status = l2::ChallengeStatus::PENDING;
    original.submittedAt = 1000;
    original.deadline = 2000;
    original.l2ChainId = 1;
    original.challengerHatScore = 75;
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::WithdrawalChallenge restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 16: Challenge Bond Slashing**
 * 
 * *For any* invalid challenge, the challenger's bond SHALL be slashed
 * and distributed to the challenged party.
 * 
 * **Validates: Requirements 29.1, 29.2**
 */
BOOST_AUTO_TEST_CASE(property_challenge_bond_slashing)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::ChallengeHandler handler(1);
        
        uint160 challenger = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Submit random number of challenges
        int numChallenges = 1 + (TestRand32() % 5);
        std::vector<uint256> challengeIds;
        CAmount totalBondsSubmitted = 0;
        
        for (int i = 0; i < numChallenges; ++i) {
            uint256 withdrawalId = TestRand256();
            uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
            handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
            
            CAmount bondAmount = l2::WITHDRAWAL_CHALLENGE_BOND + 
                                 (TestRand64() % 100) * COIN;
            
            auto challenge = handler.ChallengeWithdrawal(
                withdrawalId, challenger, bondAmount,
                {}, "Test challenge", timestamp, 50);
            
            if (challenge.has_value()) {
                challengeIds.push_back(challenge->challengeId);
                totalBondsSubmitted += bondAmount;
            }
            timestamp += 100;
        }
        
        // Verify total bonds held
        BOOST_CHECK_MESSAGE(handler.GetTotalBondsHeld() == totalBondsSubmitted,
            "Total bonds held should equal submitted bonds in iteration " << iteration);
        
        // Process challenges as invalid
        CAmount totalSlashed = 0;
        for (const auto& challengeId : challengeIds) {
            auto challenge = handler.GetChallenge(challengeId);
            BOOST_REQUIRE(challenge.has_value());
            
            auto result = handler.ProcessChallengeResult(
                challengeId, false, timestamp + 1000);
            
            // Verify bond was slashed
            BOOST_CHECK_MESSAGE(result.bondSlashed,
                "Bond should be slashed for invalid challenge in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.finalStatus == l2::ChallengeStatus::INVALID,
                "Status should be INVALID for invalid challenge in iteration " << iteration);
            
            totalSlashed += result.bondAmount;
        }
        
        // Verify all bonds were slashed
        BOOST_CHECK_MESSAGE(totalSlashed == totalBondsSubmitted,
            "Total slashed should equal total submitted in iteration " << iteration);
        
        // Verify no bonds remain held
        BOOST_CHECK_MESSAGE(handler.GetTotalBondsHeld() == 0,
            "No bonds should remain after processing in iteration " << iteration);
        
        // Verify challenger stats
        auto stats = handler.GetChallengerStats(challenger);
        BOOST_CHECK_MESSAGE(stats.totalBondsLost == totalSlashed,
            "Challenger stats should reflect slashed bonds in iteration " << iteration);
    }
}

/**
 * **Property: Valid Challenge Bond Return**
 * 
 * *For any* valid challenge, the challenger's bond SHALL be returned
 * to the challenger.
 * 
 * **Validates: Requirements 29.1**
 */
BOOST_AUTO_TEST_CASE(property_valid_challenge_bond_return)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::ChallengeHandler handler(1);
        
        uint160 challenger = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Submit random number of challenges
        int numChallenges = 1 + (TestRand32() % 5);
        std::vector<uint256> challengeIds;
        CAmount totalBondsSubmitted = 0;
        
        for (int i = 0; i < numChallenges; ++i) {
            uint256 withdrawalId = TestRand256();
            uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
            handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
            
            CAmount bondAmount = l2::WITHDRAWAL_CHALLENGE_BOND + 
                                 (TestRand64() % 100) * COIN;
            
            auto challenge = handler.ChallengeWithdrawal(
                withdrawalId, challenger, bondAmount,
                {}, "Test challenge", timestamp, 50);
            
            if (challenge.has_value()) {
                challengeIds.push_back(challenge->challengeId);
                totalBondsSubmitted += bondAmount;
            }
            timestamp += 100;
        }
        
        // Process challenges as valid
        CAmount totalReturned = 0;
        for (const auto& challengeId : challengeIds) {
            auto result = handler.ProcessChallengeResult(
                challengeId, true, timestamp + 1000);
            
            // Verify bond was returned
            BOOST_CHECK_MESSAGE(!result.bondSlashed,
                "Bond should not be slashed for valid challenge in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.bondRecipient == challenger,
                "Bond should be returned to challenger in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.finalStatus == l2::ChallengeStatus::VALID,
                "Status should be VALID for valid challenge in iteration " << iteration);
            
            totalReturned += result.bondAmount;
        }
        
        // Verify all bonds were returned
        BOOST_CHECK_MESSAGE(totalReturned == totalBondsSubmitted,
            "Total returned should equal total submitted in iteration " << iteration);
        
        // Verify challenger stats
        auto stats = handler.GetChallengerStats(challenger);
        BOOST_CHECK_MESSAGE(stats.totalBondsReturned == totalReturned,
            "Challenger stats should reflect returned bonds in iteration " << iteration);
    }
}

/**
 * **Property: Challenge Limit Enforcement**
 * 
 * *For any* challenger, the number of active challenges SHALL not exceed
 * the maximum limit (MAX_CHALLENGES_PER_ADDRESS).
 * 
 * **Validates: Requirements 29.3**
 */
BOOST_AUTO_TEST_CASE(property_challenge_limit_enforcement)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ChallengeHandler handler(1);
        
        uint160 challenger = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Try to submit more than the limit
        int attemptedChallenges = l2::MAX_CHALLENGES_PER_ADDRESS + 5;
        int successfulChallenges = 0;
        
        for (int i = 0; i < attemptedChallenges; ++i) {
            uint256 withdrawalId = TestRand256();
            uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
            handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
            
            auto challenge = handler.ChallengeWithdrawal(
                withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
                {}, "Test challenge", timestamp, 50);
            
            if (challenge.has_value()) {
                successfulChallenges++;
            }
            timestamp += 100;
        }
        
        // Verify limit was enforced
        BOOST_CHECK_MESSAGE(
            static_cast<uint32_t>(successfulChallenges) == l2::MAX_CHALLENGES_PER_ADDRESS,
            "Should only allow MAX_CHALLENGES_PER_ADDRESS challenges in iteration " << iteration);
        
        BOOST_CHECK_MESSAGE(
            handler.GetActiveChallengeCount(challenger) == l2::MAX_CHALLENGES_PER_ADDRESS,
            "Active challenge count should equal limit in iteration " << iteration);
    }
}

/**
 * **Property: Ban Threshold Enforcement**
 * 
 * *For any* challenger who submits INVALID_CHALLENGE_BAN_THRESHOLD invalid
 * challenges, they SHALL be banned from submitting new challenges.
 * 
 * **Validates: Requirements 29.6**
 */
BOOST_AUTO_TEST_CASE(property_ban_threshold_enforcement)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ChallengeHandler handler(1);
        
        uint160 challenger = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Submit and invalidate challenges
        for (uint32_t i = 0; i < l2::INVALID_CHALLENGE_BAN_THRESHOLD; ++i) {
            uint256 withdrawalId = TestRand256();
            uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
            handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
            
            auto challenge = handler.ChallengeWithdrawal(
                withdrawalId, challenger, l2::WITHDRAWAL_CHALLENGE_BOND,
                {}, "Test challenge", timestamp, 50);
            
            BOOST_REQUIRE(challenge.has_value());
            
            // Process as invalid
            handler.ProcessChallengeResult(challenge->challengeId, false, timestamp + 1000);
            
            // Check ban status
            bool shouldBeBanned = (i + 1) >= l2::INVALID_CHALLENGE_BAN_THRESHOLD;
            BOOST_CHECK_MESSAGE(
                handler.IsChallengerBanned(challenger, timestamp + 1000) == shouldBeBanned,
                "Ban status should match threshold in iteration " << iteration 
                << ", challenge " << (i + 1));
            
            timestamp += 100;
        }
        
        // Verify final ban status
        auto stats = handler.GetChallengerStats(challenger);
        BOOST_CHECK_MESSAGE(stats.isBanned,
            "Challenger should be banned after threshold in iteration " << iteration);
        BOOST_CHECK_MESSAGE(
            stats.invalidChallenges == l2::INVALID_CHALLENGE_BAN_THRESHOLD,
            "Invalid challenge count should match threshold in iteration " << iteration);
    }
}

/**
 * **Property: Bond Conservation**
 * 
 * *For any* sequence of challenge submissions and resolutions, the total
 * bonds held plus bonds returned plus bonds slashed SHALL equal total
 * bonds submitted.
 * 
 * **Validates: Requirements 29.1, 29.2**
 */
BOOST_AUTO_TEST_CASE(property_bond_conservation)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ChallengeHandler handler(1);
        
        uint64_t timestamp = 1000;
        CAmount totalSubmitted = 0;
        CAmount totalSlashed = 0;
        CAmount totalReturned = 0;
        
        // Submit challenges from multiple challengers
        int numChallengers = 2 + (TestRand32() % 3);
        std::vector<std::pair<uint256, uint160>> challenges;  // (challengeId, challenger)
        
        for (int c = 0; c < numChallengers; ++c) {
            uint160 challenger = RandomAddress160();
            int numChallenges = 1 + (TestRand32() % 3);
            
            for (int i = 0; i < numChallenges; ++i) {
                uint256 withdrawalId = TestRand256();
                uint64_t deadline = timestamp + l2::STANDARD_CHALLENGE_PERIOD;
                handler.RegisterChallengeableWithdrawal(withdrawalId, deadline);
                
                CAmount bondAmount = l2::WITHDRAWAL_CHALLENGE_BOND;
                
                auto challenge = handler.ChallengeWithdrawal(
                    withdrawalId, challenger, bondAmount,
                    {}, "Test challenge", timestamp, 50);
                
                if (challenge.has_value()) {
                    challenges.push_back({challenge->challengeId, challenger});
                    totalSubmitted += bondAmount;
                }
                timestamp += 100;
            }
        }
        
        // Randomly resolve challenges
        for (const auto& [challengeId, challenger] : challenges) {
            bool isValid = (TestRand32() % 2 == 0);
            auto result = handler.ProcessChallengeResult(
                challengeId, isValid, timestamp + 1000);
            
            if (result.bondSlashed) {
                totalSlashed += result.bondAmount;
            } else {
                totalReturned += result.bondAmount;
            }
        }
        
        // Verify conservation
        CAmount totalAccountedFor = handler.GetTotalBondsHeld() + totalSlashed + totalReturned;
        BOOST_CHECK_MESSAGE(totalAccountedFor == totalSubmitted,
            "Bond conservation violated in iteration " << iteration
            << " (submitted=" << totalSubmitted 
            << ", held=" << handler.GetTotalBondsHeld()
            << ", slashed=" << totalSlashed 
            << ", returned=" << totalReturned << ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
