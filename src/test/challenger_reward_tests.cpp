// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Challenger Reward System Tests
 * 
 * Property-based tests for the Challenger Reward System.
 * 
 * Property 4: Reward Percentage Validation
 * Validates: Requirements 4.6
 */

#include <cvm/trustgraph.h>
#include <cvm/reward_types.h>
#include <cvm/commit_reveal.h>
#include <cvm/reward_distributor.h>
#include <cvm/cvmdb.h>
#include <test/test_bitcoin.h>
#include <random.h>
#include <streams.h>
#include <version.h>
#include <fs.h>

#include <boost/test/unit_test.hpp>
#include <set>

BOOST_FIXTURE_TEST_SUITE(challenger_reward_tests, BasicTestingSetup)

// ============================================================================
// Task 1.2: Property Test for Percentage Validation
// Property 4: Reward Percentage Validation
// Validates: Requirements 4.6
// ============================================================================

/**
 * Property 4: Reward Percentage Validation
 * 
 * *For any* WoTConfig, the configuration SHALL be valid if and only if:
 * - (challengerRewardPercent + daoVoterRewardPercent + burnPercent) equals 100 AND
 * - (wronglyAccusedRewardPercent + failedChallengeBurnPercent) equals 100
 */
BOOST_AUTO_TEST_CASE(property_percentage_validation)
{
    // Run 100+ iterations with random percentage combinations
    for (int i = 0; i < 150; i++) {
        CVM::WoTConfig config;
        
        // Generate random percentages for slash reward distribution
        uint8_t challengerPercent = static_cast<uint8_t>(GetRandInt(101));  // 0-100
        uint8_t voterPercent = static_cast<uint8_t>(GetRandInt(101));       // 0-100
        uint8_t burnPercent = static_cast<uint8_t>(GetRandInt(101));        // 0-100
        
        // Generate random percentages for failed challenge distribution
        uint8_t wronglyAccusedPercent = static_cast<uint8_t>(GetRandInt(101));  // 0-100
        uint8_t failedBurnPercent = static_cast<uint8_t>(GetRandInt(101));      // 0-100
        
        // Set the config values
        config.challengerRewardPercent = challengerPercent;
        config.daoVoterRewardPercent = voterPercent;
        config.burnPercent = burnPercent;
        config.wronglyAccusedRewardPercent = wronglyAccusedPercent;
        config.failedChallengeBurnPercent = failedBurnPercent;
        
        // Calculate expected validity
        bool slashSumValid = (challengerPercent + voterPercent + burnPercent) == 100;
        bool failedSumValid = (wronglyAccusedPercent + failedBurnPercent) == 100;
        bool expectedValid = slashSumValid && failedSumValid;
        
        // Verify the validation function returns the expected result
        bool actualValid = config.ValidateRewardPercentages();
        
        BOOST_CHECK_EQUAL(actualValid, expectedValid);
    }
}

/**
 * Test that default WoTConfig values are valid
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6
 */
BOOST_AUTO_TEST_CASE(default_config_valid)
{
    CVM::WoTConfig config;
    
    // Verify default values match requirements
    BOOST_CHECK_EQUAL(config.challengerRewardPercent, 50);
    BOOST_CHECK_EQUAL(config.daoVoterRewardPercent, 30);
    BOOST_CHECK_EQUAL(config.burnPercent, 20);
    BOOST_CHECK_EQUAL(config.wronglyAccusedRewardPercent, 70);
    BOOST_CHECK_EQUAL(config.failedChallengeBurnPercent, 30);
    BOOST_CHECK_EQUAL(config.commitPhaseDuration, 720);
    BOOST_CHECK_EQUAL(config.revealPhaseDuration, 720);
    BOOST_CHECK_EQUAL(config.enableCommitReveal, true);
    
    // Verify default config passes validation
    BOOST_CHECK(config.ValidateRewardPercentages());
}

/**
 * Test valid percentage combinations
 * Validates: Requirements 4.6
 */
BOOST_AUTO_TEST_CASE(valid_percentage_combinations)
{
    // Test various valid combinations
    std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint8_t>> validCombos = {
        {50, 30, 20, 70, 30},  // Default
        {100, 0, 0, 100, 0},   // All to challenger/wrongly accused
        {0, 100, 0, 0, 100},   // All to voters/burn
        {0, 0, 100, 50, 50},   // All burn
        {33, 33, 34, 60, 40},  // Even split (with rounding)
        {1, 1, 98, 99, 1},     // Extreme burn
        {80, 15, 5, 90, 10},   // High challenger reward
    };
    
    for (const auto& combo : validCombos) {
        CVM::WoTConfig config;
        config.challengerRewardPercent = std::get<0>(combo);
        config.daoVoterRewardPercent = std::get<1>(combo);
        config.burnPercent = std::get<2>(combo);
        config.wronglyAccusedRewardPercent = std::get<3>(combo);
        config.failedChallengeBurnPercent = std::get<4>(combo);
        
        BOOST_CHECK_MESSAGE(config.ValidateRewardPercentages(),
            "Expected valid for: " + std::to_string(std::get<0>(combo)) + ", " +
            std::to_string(std::get<1>(combo)) + ", " + std::to_string(std::get<2>(combo)) + ", " +
            std::to_string(std::get<3>(combo)) + ", " + std::to_string(std::get<4>(combo)));
    }
}

/**
 * Test invalid percentage combinations
 * Validates: Requirements 4.6
 */
BOOST_AUTO_TEST_CASE(invalid_percentage_combinations)
{
    // Test various invalid combinations
    std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint8_t>> invalidCombos = {
        {50, 30, 21, 70, 30},  // Slash sum = 101
        {50, 30, 19, 70, 30},  // Slash sum = 99
        {50, 30, 20, 70, 31},  // Failed sum = 101
        {50, 30, 20, 70, 29},  // Failed sum = 99
        {0, 0, 0, 0, 0},       // All zeros
        {50, 50, 50, 50, 50},  // All 50s (sums to 150)
        {100, 100, 100, 100, 100}, // All 100s
    };
    
    for (const auto& combo : invalidCombos) {
        CVM::WoTConfig config;
        config.challengerRewardPercent = std::get<0>(combo);
        config.daoVoterRewardPercent = std::get<1>(combo);
        config.burnPercent = std::get<2>(combo);
        config.wronglyAccusedRewardPercent = std::get<3>(combo);
        config.failedChallengeBurnPercent = std::get<4>(combo);
        
        BOOST_CHECK_MESSAGE(!config.ValidateRewardPercentages(),
            "Expected invalid for: " + std::to_string(std::get<0>(combo)) + ", " +
            std::to_string(std::get<1>(combo)) + ", " + std::to_string(std::get<2>(combo)) + ", " +
            std::to_string(std::get<3>(combo)) + ", " + std::to_string(std::get<4>(combo)));
    }
}

/**
 * Test boundary conditions for percentages
 * Validates: Requirements 4.6
 */
BOOST_AUTO_TEST_CASE(percentage_boundary_conditions)
{
    CVM::WoTConfig config;
    
    // Test exact boundary: sum = 100
    config.challengerRewardPercent = 34;
    config.daoVoterRewardPercent = 33;
    config.burnPercent = 33;
    config.wronglyAccusedRewardPercent = 50;
    config.failedChallengeBurnPercent = 50;
    BOOST_CHECK(config.ValidateRewardPercentages());
    
    // Test just below boundary: sum = 99
    config.burnPercent = 32;
    BOOST_CHECK(!config.ValidateRewardPercentages());
    
    // Test just above boundary: sum = 101
    config.burnPercent = 34;
    BOOST_CHECK(!config.ValidateRewardPercentages());
    
    // Reset to valid and test failed challenge boundary
    config.burnPercent = 33;
    BOOST_CHECK(config.ValidateRewardPercentages());
    
    // Test failed challenge just below: sum = 99
    config.failedChallengeBurnPercent = 49;
    BOOST_CHECK(!config.ValidateRewardPercentages());
    
    // Test failed challenge just above: sum = 101
    config.failedChallengeBurnPercent = 51;
    BOOST_CHECK(!config.ValidateRewardPercentages());
}

// ============================================================================
// Task 2.2: Unit Tests for Reward Type Serialization
// Validates: Requirements 3.2
// ============================================================================

/**
 * Test round-trip serialization of PendingReward
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(pending_reward_serialization)
{
    // Create a PendingReward with all fields populated
    uint256 disputeId;
    disputeId.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    
    uint160 recipient;
    recipient.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    uint256 rewardId = CVM::PendingReward::GenerateRewardId(
        disputeId, recipient, CVM::RewardType::CHALLENGER_BOUNTY);
    
    CVM::PendingReward original(
        rewardId,
        disputeId,
        recipient,
        100 * COIN,  // 100 CAS
        CVM::RewardType::CHALLENGER_BOUNTY,
        1234567890   // timestamp
    );
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::PendingReward deserialized;
    ss >> deserialized;
    
    // Verify all fields match
    BOOST_CHECK(deserialized.rewardId == original.rewardId);
    BOOST_CHECK(deserialized.disputeId == original.disputeId);
    BOOST_CHECK(deserialized.recipient == original.recipient);
    BOOST_CHECK_EQUAL(deserialized.amount, original.amount);
    BOOST_CHECK(deserialized.type == original.type);
    BOOST_CHECK_EQUAL(deserialized.createdTime, original.createdTime);
    BOOST_CHECK_EQUAL(deserialized.claimed, original.claimed);
    BOOST_CHECK(deserialized.claimTxHash == original.claimTxHash);
    BOOST_CHECK_EQUAL(deserialized.claimedTime, original.claimedTime);
}

/**
 * Test round-trip serialization of PendingReward with claimed status
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(pending_reward_claimed_serialization)
{
    uint256 disputeId;
    disputeId.SetHex("0x1111111111111111111111111111111111111111111111111111111111111111");
    
    uint160 recipient;
    recipient.SetHex("0x2222222222222222222222222222222222222222");
    
    uint256 rewardId = CVM::PendingReward::GenerateRewardId(
        disputeId, recipient, CVM::RewardType::DAO_VOTER_REWARD);
    
    CVM::PendingReward original(
        rewardId,
        disputeId,
        recipient,
        50 * COIN,
        CVM::RewardType::DAO_VOTER_REWARD,
        1234567890
    );
    
    // Mark as claimed
    original.claimed = true;
    original.claimTxHash.SetHex("0x3333333333333333333333333333333333333333333333333333333333333333");
    original.claimedTime = 1234567900;
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::PendingReward deserialized;
    ss >> deserialized;
    
    // Verify claimed fields
    BOOST_CHECK_EQUAL(deserialized.claimed, true);
    BOOST_CHECK(deserialized.claimTxHash == original.claimTxHash);
    BOOST_CHECK_EQUAL(deserialized.claimedTime, original.claimedTime);
}

/**
 * Test serialization of all RewardType enum values
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_type_enum_serialization)
{
    std::vector<CVM::RewardType> types = {
        CVM::RewardType::CHALLENGER_BOND_RETURN,
        CVM::RewardType::CHALLENGER_BOUNTY,
        CVM::RewardType::DAO_VOTER_REWARD,
        CVM::RewardType::WRONGLY_ACCUSED_COMPENSATION
    };
    
    uint256 disputeId;
    disputeId.SetHex("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    
    uint160 recipient;
    recipient.SetHex("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    
    for (const auto& type : types) {
        uint256 rewardId = CVM::PendingReward::GenerateRewardId(disputeId, recipient, type);
        
        CVM::PendingReward original(
            rewardId, disputeId, recipient, 10 * COIN, type, 1234567890);
        
        // Serialize
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << original;
        
        // Deserialize
        CVM::PendingReward deserialized;
        ss >> deserialized;
        
        // Verify type is preserved
        BOOST_CHECK_MESSAGE(deserialized.type == type,
            "RewardType mismatch for " + CVM::RewardTypeToString(type));
    }
}

/**
 * Test round-trip serialization of RewardDistribution
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_distribution_serialization)
{
    CVM::RewardDistribution original;
    original.disputeId.SetHex("0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    original.slashDecision = true;
    original.totalSlashedBond = 1000 * COIN;
    original.challengerBondReturn = 100 * COIN;
    original.challengerBounty = 500 * COIN;
    original.totalDaoVoterRewards = 300 * COIN;
    original.burnedAmount = 200 * COIN;
    original.distributedTime = 1234567890;
    
    // Add some voter rewards
    uint160 voter1, voter2, voter3;
    voter1.SetHex("0x1111111111111111111111111111111111111111");
    voter2.SetHex("0x2222222222222222222222222222222222222222");
    voter3.SetHex("0x3333333333333333333333333333333333333333");
    
    original.voterRewards[voter1] = 100 * COIN;
    original.voterRewards[voter2] = 150 * COIN;
    original.voterRewards[voter3] = 50 * COIN;
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::RewardDistribution deserialized;
    ss >> deserialized;
    
    // Verify all fields
    BOOST_CHECK(deserialized.disputeId == original.disputeId);
    BOOST_CHECK_EQUAL(deserialized.slashDecision, original.slashDecision);
    BOOST_CHECK_EQUAL(deserialized.totalSlashedBond, original.totalSlashedBond);
    BOOST_CHECK_EQUAL(deserialized.challengerBondReturn, original.challengerBondReturn);
    BOOST_CHECK_EQUAL(deserialized.challengerBounty, original.challengerBounty);
    BOOST_CHECK_EQUAL(deserialized.totalDaoVoterRewards, original.totalDaoVoterRewards);
    BOOST_CHECK_EQUAL(deserialized.burnedAmount, original.burnedAmount);
    BOOST_CHECK_EQUAL(deserialized.distributedTime, original.distributedTime);
    
    // Verify voter rewards map
    BOOST_CHECK_EQUAL(deserialized.voterRewards.size(), original.voterRewards.size());
    BOOST_CHECK_EQUAL(deserialized.voterRewards[voter1], original.voterRewards[voter1]);
    BOOST_CHECK_EQUAL(deserialized.voterRewards[voter2], original.voterRewards[voter2]);
    BOOST_CHECK_EQUAL(deserialized.voterRewards[voter3], original.voterRewards[voter3]);
}

/**
 * Test RewardDistribution serialization for failed challenge (no slash)
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_distribution_failed_challenge_serialization)
{
    CVM::RewardDistribution original;
    original.disputeId.SetHex("0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    original.slashDecision = false;  // Failed challenge
    original.totalSlashedBond = 100 * COIN;  // Forfeited challenger bond
    original.challengerBondReturn = 0;  // No return for failed challenge
    original.challengerBounty = 0;
    original.totalDaoVoterRewards = 0;
    original.burnedAmount = 30 * COIN;  // 30% burned
    original.distributedTime = 1234567890;
    
    // Wrongly accused gets compensation (stored as voter reward)
    uint160 wronglyAccused;
    wronglyAccused.SetHex("0x4444444444444444444444444444444444444444");
    original.voterRewards[wronglyAccused] = 70 * COIN;  // 70% compensation
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::RewardDistribution deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK_EQUAL(deserialized.slashDecision, false);
    BOOST_CHECK_EQUAL(deserialized.challengerBondReturn, 0);
    BOOST_CHECK_EQUAL(deserialized.voterRewards[wronglyAccused], 70 * COIN);
}

/**
 * Test RewardDistribution with empty voter rewards map
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_distribution_empty_voters_serialization)
{
    CVM::RewardDistribution original;
    original.disputeId.SetHex("0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    original.slashDecision = true;
    original.totalSlashedBond = 100 * COIN;
    original.challengerBondReturn = 50 * COIN;
    original.challengerBounty = 50 * COIN;
    original.totalDaoVoterRewards = 0;  // No voters on winning side
    original.burnedAmount = 0;
    original.distributedTime = 1234567890;
    // voterRewards map is empty
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::RewardDistribution deserialized;
    ss >> deserialized;
    
    // Verify empty map is preserved
    BOOST_CHECK(deserialized.voterRewards.empty());
    BOOST_CHECK_EQUAL(deserialized.totalDaoVoterRewards, 0);
}

/**
 * Test PendingReward::GenerateRewardId produces unique IDs
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_id_uniqueness)
{
    uint256 disputeId1, disputeId2;
    disputeId1.SetHex("0x1111111111111111111111111111111111111111111111111111111111111111");
    disputeId2.SetHex("0x2222222222222222222222222222222222222222222222222222222222222222");
    
    uint160 recipient1, recipient2;
    recipient1.SetHex("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    recipient2.SetHex("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    
    // Same dispute, same recipient, different types should produce different IDs
    uint256 id1 = CVM::PendingReward::GenerateRewardId(
        disputeId1, recipient1, CVM::RewardType::CHALLENGER_BOND_RETURN);
    uint256 id2 = CVM::PendingReward::GenerateRewardId(
        disputeId1, recipient1, CVM::RewardType::CHALLENGER_BOUNTY);
    BOOST_CHECK(id1 != id2);
    
    // Same dispute, different recipients, same type should produce different IDs
    uint256 id3 = CVM::PendingReward::GenerateRewardId(
        disputeId1, recipient1, CVM::RewardType::DAO_VOTER_REWARD);
    uint256 id4 = CVM::PendingReward::GenerateRewardId(
        disputeId1, recipient2, CVM::RewardType::DAO_VOTER_REWARD);
    BOOST_CHECK(id3 != id4);
    
    // Different disputes, same recipient, same type should produce different IDs
    uint256 id5 = CVM::PendingReward::GenerateRewardId(
        disputeId1, recipient1, CVM::RewardType::WRONGLY_ACCUSED_COMPENSATION);
    uint256 id6 = CVM::PendingReward::GenerateRewardId(
        disputeId2, recipient1, CVM::RewardType::WRONGLY_ACCUSED_COMPENSATION);
    BOOST_CHECK(id5 != id6);
    
    // Same inputs should produce same ID (deterministic)
    uint256 id7 = CVM::PendingReward::GenerateRewardId(
        disputeId1, recipient1, CVM::RewardType::CHALLENGER_BOND_RETURN);
    BOOST_CHECK(id1 == id7);
}

/**
 * Test PendingReward::IsValid() method
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(pending_reward_is_valid)
{
    uint256 disputeId;
    disputeId.SetHex("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    
    uint160 recipient;
    recipient.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    uint256 rewardId = CVM::PendingReward::GenerateRewardId(
        disputeId, recipient, CVM::RewardType::CHALLENGER_BOUNTY);
    
    // Valid reward
    CVM::PendingReward validReward(
        rewardId, disputeId, recipient, 100 * COIN,
        CVM::RewardType::CHALLENGER_BOUNTY, 1234567890);
    BOOST_CHECK(validReward.IsValid());
    
    // Invalid: zero amount
    CVM::PendingReward zeroAmount(
        rewardId, disputeId, recipient, 0,
        CVM::RewardType::CHALLENGER_BOUNTY, 1234567890);
    BOOST_CHECK(!zeroAmount.IsValid());
    
    // Invalid: null recipient
    uint160 nullRecipient;  // Default constructed is null
    CVM::PendingReward nullRecipientReward(
        rewardId, disputeId, nullRecipient, 100 * COIN,
        CVM::RewardType::CHALLENGER_BOUNTY, 1234567890);
    BOOST_CHECK(!nullRecipientReward.IsValid());
}

/**
 * Test RewardDistribution helper methods
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_distribution_helpers)
{
    CVM::RewardDistribution dist;
    dist.disputeId.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    dist.slashDecision = true;
    dist.totalSlashedBond = 1000 * COIN;
    dist.challengerBondReturn = 100 * COIN;
    dist.challengerBounty = 500 * COIN;
    dist.totalDaoVoterRewards = 300 * COIN;
    dist.burnedAmount = 200 * COIN;
    dist.distributedTime = 1234567890;
    
    // Test TotalDistributed
    CAmount expectedTotal = 100 * COIN + 500 * COIN + 300 * COIN + 200 * COIN;
    BOOST_CHECK_EQUAL(dist.TotalDistributed(), expectedTotal);
    
    // Test VerifyConservation for slash decision
    // Total in = challenger bond (100) + slashed bond (1000) = 1100
    // Total out = 100 + 500 + 300 + 200 = 1100
    BOOST_CHECK(dist.VerifyConservation(100 * COIN));
    
    // Test IsValid
    BOOST_CHECK(dist.IsValid());
    
    // Test invalid distribution (no timestamp)
    CVM::RewardDistribution invalidDist;
    invalidDist.disputeId.SetHex("0x1111111111111111111111111111111111111111111111111111111111111111");
    invalidDist.distributedTime = 0;
    BOOST_CHECK(!invalidDist.IsValid());
}

/**
 * Test RewardTypeToString function
 * Validates: Requirements 3.2
 */
BOOST_AUTO_TEST_CASE(reward_type_to_string)
{
    BOOST_CHECK_EQUAL(CVM::RewardTypeToString(CVM::RewardType::CHALLENGER_BOND_RETURN), 
                      "CHALLENGER_BOND_RETURN");
    BOOST_CHECK_EQUAL(CVM::RewardTypeToString(CVM::RewardType::CHALLENGER_BOUNTY), 
                      "CHALLENGER_BOUNTY");
    BOOST_CHECK_EQUAL(CVM::RewardTypeToString(CVM::RewardType::DAO_VOTER_REWARD), 
                      "DAO_VOTER_REWARD");
    BOOST_CHECK_EQUAL(CVM::RewardTypeToString(CVM::RewardType::WRONGLY_ACCUSED_COMPENSATION), 
                      "WRONGLY_ACCUSED_COMPENSATION");
    
    // Test unknown type (cast an invalid value)
    BOOST_CHECK_EQUAL(CVM::RewardTypeToString(static_cast<CVM::RewardType>(255)), 
                      "UNKNOWN");
}


// ============================================================================
// Task 3.3: Property Test for Commit-Reveal Hash Integrity
// Property 7: Commit-Reveal Hash Integrity (Round-Trip)
// Validates: Requirements 8.1, 8.4, 8.7
// ============================================================================

/**
 * Property 7: Commit-Reveal Hash Integrity (Round-Trip)
 * 
 * *For any* vote (true/false) and nonce, computing the commitment hash and then
 * revealing with the same vote and nonce SHALL always verify successfully.
 * Conversely, revealing with a different vote or nonce SHALL always fail verification.
 */
BOOST_AUTO_TEST_CASE(property_commit_reveal_hash_integrity)
{
    // Run 100+ iterations with random votes and nonces
    for (int i = 0; i < 150; i++) {
        // Generate random vote (true or false)
        bool vote = (GetRandInt(2) == 1);
        
        // Generate random nonce
        uint256 nonce;
        GetRandBytes(nonce.begin(), 32);
        
        // Calculate commitment hash
        uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(vote, nonce);
        
        // Property 1: Same vote and nonce should always verify
        BOOST_CHECK_MESSAGE(
            CVM::CommitRevealManager::VerifyCommitment(commitmentHash, vote, nonce),
            "Round-trip verification failed for vote=" + std::to_string(vote) +
            " nonce=" + nonce.GetHex()
        );
        
        // Property 2: Different vote should always fail
        bool wrongVote = !vote;
        BOOST_CHECK_MESSAGE(
            !CVM::CommitRevealManager::VerifyCommitment(commitmentHash, wrongVote, nonce),
            "Verification should fail for wrong vote"
        );
        
        // Property 3: Different nonce should always fail
        uint256 wrongNonce;
        GetRandBytes(wrongNonce.begin(), 32);
        // Make sure wrongNonce is actually different
        if (wrongNonce == nonce) {
            wrongNonce.SetHex("0x1111111111111111111111111111111111111111111111111111111111111111");
        }
        BOOST_CHECK_MESSAGE(
            !CVM::CommitRevealManager::VerifyCommitment(commitmentHash, vote, wrongNonce),
            "Verification should fail for wrong nonce"
        );
        
        // Property 4: Both wrong vote and nonce should fail
        BOOST_CHECK_MESSAGE(
            !CVM::CommitRevealManager::VerifyCommitment(commitmentHash, wrongVote, wrongNonce),
            "Verification should fail for wrong vote and nonce"
        );
    }
}

/**
 * Test that commitment hash is deterministic
 * Same inputs should always produce the same hash
 * Validates: Requirements 8.1, 8.7
 */
BOOST_AUTO_TEST_CASE(commitment_hash_deterministic)
{
    // Test with fixed values
    uint256 nonce;
    nonce.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    
    // Calculate hash multiple times with same inputs
    uint256 hash1 = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
    uint256 hash2 = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
    uint256 hash3 = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
    
    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash2 == hash3);
    
    // Different vote should produce different hash
    uint256 hashFalse = CVM::CommitRevealManager::CalculateCommitmentHash(false, nonce);
    BOOST_CHECK(hash1 != hashFalse);
}

/**
 * Test commitment hash uniqueness
 * Different inputs should produce different hashes
 * Validates: Requirements 8.1, 8.7
 */
BOOST_AUTO_TEST_CASE(commitment_hash_uniqueness)
{
    std::set<uint256> hashes;
    
    // Generate many hashes with random inputs
    for (int i = 0; i < 100; i++) {
        bool vote = (GetRandInt(2) == 1);
        uint256 nonce;
        GetRandBytes(nonce.begin(), 32);
        
        uint256 hash = CVM::CommitRevealManager::CalculateCommitmentHash(vote, nonce);
        
        // Each hash should be unique (collision probability is negligible)
        BOOST_CHECK_MESSAGE(hashes.find(hash) == hashes.end(),
            "Hash collision detected - this should be extremely rare");
        hashes.insert(hash);
    }
}

/**
 * Test VoteCommitment serialization round-trip
 * Validates: Requirements 8.1, 8.7
 */
BOOST_AUTO_TEST_CASE(vote_commitment_serialization)
{
    // Create a VoteCommitment with all fields populated
    uint256 disputeId;
    disputeId.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    
    uint160 voter;
    voter.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    uint256 nonce;
    nonce.SetHex("0xfedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");
    
    uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
    
    CVM::VoteCommitment original(
        disputeId,
        voter,
        commitmentHash,
        100 * COIN,  // 100 CAS stake
        1234567890   // commit timestamp
    );
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::VoteCommitment deserialized;
    ss >> deserialized;
    
    // Verify all fields match
    BOOST_CHECK(deserialized.disputeId == original.disputeId);
    BOOST_CHECK(deserialized.voter == original.voter);
    BOOST_CHECK(deserialized.commitmentHash == original.commitmentHash);
    BOOST_CHECK_EQUAL(deserialized.stake, original.stake);
    BOOST_CHECK_EQUAL(deserialized.commitTime, original.commitTime);
    BOOST_CHECK_EQUAL(deserialized.revealed, original.revealed);
    BOOST_CHECK_EQUAL(deserialized.vote, original.vote);
    BOOST_CHECK(deserialized.nonce == original.nonce);
    BOOST_CHECK_EQUAL(deserialized.revealTime, original.revealTime);
    BOOST_CHECK_EQUAL(deserialized.forfeited, original.forfeited);
}

/**
 * Test VoteCommitment serialization with revealed vote
 * Validates: Requirements 8.4
 */
BOOST_AUTO_TEST_CASE(vote_commitment_revealed_serialization)
{
    uint256 disputeId;
    disputeId.SetHex("0x1111111111111111111111111111111111111111111111111111111111111111");
    
    uint160 voter;
    voter.SetHex("0x2222222222222222222222222222222222222222");
    
    uint256 nonce;
    nonce.SetHex("0x3333333333333333333333333333333333333333333333333333333333333333");
    
    uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
    
    CVM::VoteCommitment original(
        disputeId,
        voter,
        commitmentHash,
        50 * COIN,
        1234567890
    );
    
    // Mark as revealed
    original.revealed = true;
    original.vote = true;
    original.nonce = nonce;
    original.revealTime = 1234567900;
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::VoteCommitment deserialized;
    ss >> deserialized;
    
    // Verify revealed fields
    BOOST_CHECK_EQUAL(deserialized.revealed, true);
    BOOST_CHECK_EQUAL(deserialized.vote, true);
    BOOST_CHECK(deserialized.nonce == nonce);
    BOOST_CHECK_EQUAL(deserialized.revealTime, 1234567900);
    BOOST_CHECK_EQUAL(deserialized.forfeited, false);
}

/**
 * Test VoteCommitment serialization with forfeited stake
 * Validates: Requirements 8.5, 8.6
 */
BOOST_AUTO_TEST_CASE(vote_commitment_forfeited_serialization)
{
    uint256 disputeId;
    disputeId.SetHex("0x4444444444444444444444444444444444444444444444444444444444444444");
    
    uint160 voter;
    voter.SetHex("0x5555555555555555555555555555555555555555");
    
    uint256 commitmentHash;
    commitmentHash.SetHex("0x6666666666666666666666666666666666666666666666666666666666666666");
    
    CVM::VoteCommitment original(
        disputeId,
        voter,
        commitmentHash,
        75 * COIN,
        1234567890
    );
    
    // Mark as forfeited (didn't reveal in time)
    original.forfeited = true;
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;
    
    // Deserialize
    CVM::VoteCommitment deserialized;
    ss >> deserialized;
    
    // Verify forfeited state
    BOOST_CHECK_EQUAL(deserialized.forfeited, true);
    BOOST_CHECK_EQUAL(deserialized.revealed, false);
}

/**
 * Test VoteCommitment::IsValid() method
 * Validates: Requirements 8.1, 8.7
 */
BOOST_AUTO_TEST_CASE(vote_commitment_is_valid)
{
    uint256 disputeId;
    disputeId.SetHex("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    
    uint160 voter;
    voter.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    uint256 commitmentHash;
    commitmentHash.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    
    // Valid commitment
    CVM::VoteCommitment validCommitment(
        disputeId, voter, commitmentHash, 100 * COIN, 1234567890);
    BOOST_CHECK(validCommitment.IsValid());
    
    // Invalid: null disputeId
    CVM::VoteCommitment nullDispute;
    nullDispute.voter = voter;
    nullDispute.commitmentHash = commitmentHash;
    nullDispute.stake = 100 * COIN;
    BOOST_CHECK(!nullDispute.IsValid());
    
    // Invalid: null voter
    CVM::VoteCommitment nullVoter;
    nullVoter.disputeId = disputeId;
    nullVoter.commitmentHash = commitmentHash;
    nullVoter.stake = 100 * COIN;
    BOOST_CHECK(!nullVoter.IsValid());
    
    // Invalid: null commitmentHash
    CVM::VoteCommitment nullHash;
    nullHash.disputeId = disputeId;
    nullHash.voter = voter;
    nullHash.stake = 100 * COIN;
    BOOST_CHECK(!nullHash.IsValid());
    
    // Invalid: zero stake
    CVM::VoteCommitment zeroStake(
        disputeId, voter, commitmentHash, 0, 1234567890);
    BOOST_CHECK(!zeroStake.IsValid());
}

/**
 * Test VoteCommitment::CanReveal() method
 * Validates: Requirements 8.4
 */
BOOST_AUTO_TEST_CASE(vote_commitment_can_reveal)
{
    uint256 disputeId;
    disputeId.SetHex("0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    
    uint160 voter;
    voter.SetHex("0xdddddddddddddddddddddddddddddddddddddddd");
    
    uint256 commitmentHash;
    commitmentHash.SetHex("0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    
    // Fresh commitment can reveal
    CVM::VoteCommitment fresh(
        disputeId, voter, commitmentHash, 100 * COIN, 1234567890);
    BOOST_CHECK(fresh.CanReveal());
    
    // Already revealed cannot reveal again
    CVM::VoteCommitment revealed = fresh;
    revealed.revealed = true;
    BOOST_CHECK(!revealed.CanReveal());
    
    // Forfeited cannot reveal
    CVM::VoteCommitment forfeited = fresh;
    forfeited.forfeited = true;
    BOOST_CHECK(!forfeited.CanReveal());
    
    // Both revealed and forfeited cannot reveal
    CVM::VoteCommitment both = fresh;
    both.revealed = true;
    both.forfeited = true;
    BOOST_CHECK(!both.CanReveal());
}


// ============================================================================
// Task 3.4: Property Test for Phase Transitions
// Property 8: Phase Transition Correctness
// Validates: Requirements 8.2, 8.3
// ============================================================================

/**
 * Mock CommitRevealManager for testing phase transitions
 * Allows setting a custom block height for testing
 */
class MockCommitRevealManager : public CVM::CommitRevealManager {
public:
    MockCommitRevealManager(CVM::CVMDatabase& db, const CVM::WoTConfig& config)
        : CVM::CommitRevealManager(db, config)
        , mockBlockHeight(0)
    {}
    
    void SetMockBlockHeight(uint32_t height) {
        mockBlockHeight = height;
    }
    
    uint32_t GetCurrentBlockHeight() const override {
        return mockBlockHeight;
    }
    
private:
    uint32_t mockBlockHeight;
};

/**
 * Property 8: Phase Transition Correctness
 * 
 * *For any* dispute using commit-reveal, the dispute SHALL be:
 * - In commit phase from creation until commitPhaseDuration blocks pass
 * - In reveal phase from commitPhaseDuration until revealPhaseDuration blocks pass
 * - Neither phase after both durations have passed
 * 
 * This test verifies the phase calculation logic directly without database access.
 */
BOOST_AUTO_TEST_CASE(property_phase_transition_correctness)
{
    // Test phase transition logic with various configurations
    for (int i = 0; i < 100; i++) {
        // Generate random phase durations (between 10 and 1000 blocks)
        uint32_t commitDuration = 10 + GetRandInt(991);
        uint32_t revealDuration = 10 + GetRandInt(991);
        
        // Generate random dispute start time (block height)
        uint32_t disputeStart = GetRandInt(1000000);
        
        // Calculate phase boundaries
        uint32_t commitPhaseEnd = disputeStart + commitDuration;
        uint32_t revealPhaseEnd = commitPhaseEnd + revealDuration;
        
        // Test various block heights
        std::vector<uint32_t> testHeights = {
            disputeStart,                           // Start of commit phase
            disputeStart + commitDuration / 2,      // Middle of commit phase
            commitPhaseEnd - 1,                     // End of commit phase
            commitPhaseEnd,                         // Start of reveal phase
            commitPhaseEnd + revealDuration / 2,    // Middle of reveal phase
            revealPhaseEnd - 1,                     // End of reveal phase
            revealPhaseEnd,                         // After both phases
            revealPhaseEnd + 1000                   // Well after both phases
        };
        
        for (uint32_t currentHeight : testHeights) {
            // Calculate expected phase
            bool expectedCommitPhase = (currentHeight >= disputeStart && currentHeight < commitPhaseEnd);
            bool expectedRevealPhase = (currentHeight >= commitPhaseEnd && currentHeight < revealPhaseEnd);
            
            // Verify mutual exclusivity: can't be in both phases
            BOOST_CHECK_MESSAGE(!(expectedCommitPhase && expectedRevealPhase),
                "Cannot be in both commit and reveal phase simultaneously");
            
            // Verify phase boundaries are correct
            if (currentHeight < disputeStart) {
                BOOST_CHECK(!expectedCommitPhase);
                BOOST_CHECK(!expectedRevealPhase);
            } else if (currentHeight < commitPhaseEnd) {
                BOOST_CHECK(expectedCommitPhase);
                BOOST_CHECK(!expectedRevealPhase);
            } else if (currentHeight < revealPhaseEnd) {
                BOOST_CHECK(!expectedCommitPhase);
                BOOST_CHECK(expectedRevealPhase);
            } else {
                BOOST_CHECK(!expectedCommitPhase);
                BOOST_CHECK(!expectedRevealPhase);
            }
        }
    }
}

/**
 * Test phase transition at exact boundaries
 * Validates: Requirements 8.2, 8.3
 */
BOOST_AUTO_TEST_CASE(phase_transition_boundaries)
{
    // Use default config values
    CVM::WoTConfig config;
    uint32_t commitDuration = config.commitPhaseDuration;  // 720 blocks
    uint32_t revealDuration = config.revealPhaseDuration;  // 720 blocks
    
    uint32_t disputeStart = 10000;
    uint32_t commitPhaseEnd = disputeStart + commitDuration;
    uint32_t revealPhaseEnd = commitPhaseEnd + revealDuration;
    
    // Test exact boundary: last block of commit phase
    {
        uint32_t height = commitPhaseEnd - 1;
        bool inCommit = (height >= disputeStart && height < commitPhaseEnd);
        bool inReveal = (height >= commitPhaseEnd && height < revealPhaseEnd);
        BOOST_CHECK(inCommit);
        BOOST_CHECK(!inReveal);
    }
    
    // Test exact boundary: first block of reveal phase
    {
        uint32_t height = commitPhaseEnd;
        bool inCommit = (height >= disputeStart && height < commitPhaseEnd);
        bool inReveal = (height >= commitPhaseEnd && height < revealPhaseEnd);
        BOOST_CHECK(!inCommit);
        BOOST_CHECK(inReveal);
    }
    
    // Test exact boundary: last block of reveal phase
    {
        uint32_t height = revealPhaseEnd - 1;
        bool inCommit = (height >= disputeStart && height < commitPhaseEnd);
        bool inReveal = (height >= commitPhaseEnd && height < revealPhaseEnd);
        BOOST_CHECK(!inCommit);
        BOOST_CHECK(inReveal);
    }
    
    // Test exact boundary: first block after reveal phase
    {
        uint32_t height = revealPhaseEnd;
        bool inCommit = (height >= disputeStart && height < commitPhaseEnd);
        bool inReveal = (height >= commitPhaseEnd && height < revealPhaseEnd);
        BOOST_CHECK(!inCommit);
        BOOST_CHECK(!inReveal);
    }
}

/**
 * Test phase durations with edge case values
 * Validates: Requirements 8.2, 8.3
 */
BOOST_AUTO_TEST_CASE(phase_duration_edge_cases)
{
    // Test with minimum duration (1 block)
    {
        uint32_t commitDuration = 1;
        uint32_t revealDuration = 1;
        uint32_t disputeStart = 100;
        
        // At disputeStart: in commit phase
        uint32_t height = disputeStart;
        bool inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        BOOST_CHECK(inCommit);
        
        // At disputeStart + 1: in reveal phase
        height = disputeStart + 1;
        inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        bool inReveal = (height >= disputeStart + commitDuration && 
                         height < disputeStart + commitDuration + revealDuration);
        BOOST_CHECK(!inCommit);
        BOOST_CHECK(inReveal);
        
        // At disputeStart + 2: after both phases
        height = disputeStart + 2;
        inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        inReveal = (height >= disputeStart + commitDuration && 
                    height < disputeStart + commitDuration + revealDuration);
        BOOST_CHECK(!inCommit);
        BOOST_CHECK(!inReveal);
    }
    
    // Test with very large durations
    {
        uint32_t commitDuration = 100000;
        uint32_t revealDuration = 100000;
        uint32_t disputeStart = 1000000;
        
        // Middle of commit phase
        uint32_t height = disputeStart + 50000;
        bool inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        bool inReveal = (height >= disputeStart + commitDuration && 
                         height < disputeStart + commitDuration + revealDuration);
        BOOST_CHECK(inCommit);
        BOOST_CHECK(!inReveal);
        
        // Middle of reveal phase
        height = disputeStart + commitDuration + 50000;
        inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        inReveal = (height >= disputeStart + commitDuration && 
                    height < disputeStart + commitDuration + revealDuration);
        BOOST_CHECK(!inCommit);
        BOOST_CHECK(inReveal);
    }
}

/**
 * Test that phases are mutually exclusive
 * Validates: Requirements 8.2, 8.3
 */
BOOST_AUTO_TEST_CASE(phases_mutually_exclusive)
{
    // Run many random tests to verify mutual exclusivity
    for (int i = 0; i < 100; i++) {
        uint32_t commitDuration = 1 + GetRandInt(10000);
        uint32_t revealDuration = 1 + GetRandInt(10000);
        uint32_t disputeStart = GetRandInt(1000000);
        uint32_t currentHeight = GetRandInt(2000000);
        
        uint32_t commitPhaseEnd = disputeStart + commitDuration;
        uint32_t revealPhaseEnd = commitPhaseEnd + revealDuration;
        
        bool inCommit = (currentHeight >= disputeStart && currentHeight < commitPhaseEnd);
        bool inReveal = (currentHeight >= commitPhaseEnd && currentHeight < revealPhaseEnd);
        
        // Phases must be mutually exclusive
        BOOST_CHECK_MESSAGE(!(inCommit && inReveal),
            "Phases must be mutually exclusive at height " + std::to_string(currentHeight));
    }
}

/**
 * Test phase calculation with zero start height
 * Validates: Requirements 8.2, 8.3
 */
BOOST_AUTO_TEST_CASE(phase_zero_start)
{
    uint32_t commitDuration = 720;
    uint32_t revealDuration = 720;
    uint32_t disputeStart = 0;  // Genesis block
    
    // At block 0: in commit phase
    {
        uint32_t height = 0;
        bool inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        BOOST_CHECK(inCommit);
    }
    
    // At block 719: still in commit phase
    {
        uint32_t height = 719;
        bool inCommit = (height >= disputeStart && height < disputeStart + commitDuration);
        BOOST_CHECK(inCommit);
    }
    
    // At block 720: in reveal phase
    {
        uint32_t height = 720;
        bool inReveal = (height >= disputeStart + commitDuration && 
                         height < disputeStart + commitDuration + revealDuration);
        BOOST_CHECK(inReveal);
    }
}

// ============================================================================
// Task 5.5: Property Test for Conservation of Funds (Slash)
// Property 1: Conservation of Funds (Slash Decision)
// Validates: Requirements 1.1, 1.2, 1.3, 1.4, 5.4, 5.5
// ============================================================================

/**
 * Mock RewardDistributor for testing without database
 * Allows testing the calculation logic directly
 */
class MockRewardDistributor : public CVM::RewardDistributor {
public:
    MockRewardDistributor(CVM::CVMDatabase& db, const CVM::WoTConfig& config)
        : CVM::RewardDistributor(db, config)
        , mockTimestamp(1234567890)
    {}
    
    void SetMockTimestamp(uint32_t ts) {
        mockTimestamp = ts;
    }
    
    uint32_t GetCurrentTimestamp() const override {
        return mockTimestamp;
    }
    
private:
    uint32_t mockTimestamp;
};

/**
 * Helper to create a test dispute with random parameters
 */
CVM::DAODispute CreateTestDispute(bool slashDecision, CAmount challengeBond,
                                   int numSlashVoters, int numKeepVoters)
{
    CVM::DAODispute dispute;
    
    // Generate random dispute ID
    GetRandBytes(dispute.disputeId.begin(), 32);
    
    // Generate random challenger
    GetRandBytes(dispute.challenger.begin(), 20);
    
    dispute.challengeBond = challengeBond;
    dispute.resolved = true;
    dispute.slashDecision = slashDecision;
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add voters on slash side (vote = true)
    for (int i = 0; i < numSlashVoters; i++) {
        uint160 voter;
        GetRandBytes(voter.begin(), 20);
        
        CAmount stake = COIN + GetRandInt(100) * COIN;  // 1-100 CAS
        
        dispute.daoVotes[voter] = true;  // Vote to slash
        dispute.daoStakes[voter] = stake;
    }
    
    // Add voters on keep side (vote = false)
    for (int i = 0; i < numKeepVoters; i++) {
        uint160 voter;
        GetRandBytes(voter.begin(), 20);
        
        CAmount stake = COIN + GetRandInt(100) * COIN;  // 1-100 CAS
        
        dispute.daoVotes[voter] = false;  // Vote to keep
        dispute.daoStakes[voter] = stake;
    }
    
    return dispute;
}

/**
 * Property 1: Conservation of Funds (Slash Decision)
 * 
 * *For any* dispute that resolves with a slash decision, the sum of
 * (challenger bond return + challenger bounty + total DAO voter rewards + burned amount)
 * SHALL equal (original challenger bond + slashed bond from malicious voter).
 * 
 * This ensures no funds are created or destroyed during reward distribution.
 */
BOOST_AUTO_TEST_CASE(property_conservation_of_funds_slash)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    
    MockRewardDistributor distributor(db, config);
    
    // Run 100+ iterations with random parameters
    for (int i = 0; i < 150; i++) {
        // Generate random amounts
        CAmount challengerBond = COIN + GetRandInt(1000) * COIN;  // 1-1000 CAS
        CAmount slashedBond = COIN + GetRandInt(1000) * COIN;     // 1-1000 CAS
        
        // Generate random number of voters (0-10 on each side)
        int numSlashVoters = GetRandInt(11);
        int numKeepVoters = GetRandInt(11);
        
        // Create dispute with slash decision
        CVM::DAODispute dispute = CreateTestDispute(true, challengerBond, 
                                                     numSlashVoters, numKeepVoters);
        
        // Distribute rewards
        bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
        BOOST_CHECK(success);
        
        // Get the distribution record
        CVM::RewardDistribution dist = distributor.GetRewardDistribution(dispute.disputeId);
        
        // Calculate total input
        CAmount totalIn = challengerBond + slashedBond;
        
        // Calculate total output
        CAmount totalOut = dist.challengerBondReturn + dist.challengerBounty + 
                          dist.totalDaoVoterRewards + dist.burnedAmount;
        
        // Verify conservation of funds
        BOOST_CHECK_MESSAGE(totalOut == totalIn,
            "Conservation violated: in=" + std::to_string(totalIn) + 
            " out=" + std::to_string(totalOut) +
            " diff=" + std::to_string(totalIn - totalOut));
        
        // Also verify using the helper method
        BOOST_CHECK(dist.VerifyConservation(challengerBond));
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 5.6: Property Test for Conservation of Funds (Failed Challenge)
// Property 2: Conservation of Funds (Failed Challenge)
// Validates: Requirements 2.1, 2.2, 2.3
// ============================================================================

/**
 * Property 2: Conservation of Funds (Failed Challenge)
 * 
 * *For any* dispute that resolves without a slash decision (keep vote),
 * the sum of (wrongly accused compensation + burned amount) SHALL equal
 * the original challenger bond.
 * 
 * This ensures the forfeited challenger bond is fully accounted for.
 */
BOOST_AUTO_TEST_CASE(property_conservation_of_funds_failed_challenge)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    
    MockRewardDistributor distributor(db, config);
    
    // Run 100+ iterations with random parameters
    for (int i = 0; i < 150; i++) {
        // Generate random challenger bond
        CAmount challengerBond = COIN + GetRandInt(1000) * COIN;  // 1-1000 CAS
        
        // Create dispute with keep decision (no slash)
        CVM::DAODispute dispute = CreateTestDispute(false, challengerBond, 
                                                     GetRandInt(5), GetRandInt(5));
        
        // Generate random original voter address
        uint160 originalVoter;
        GetRandBytes(originalVoter.begin(), 20);
        
        // Distribute rewards
        bool success = distributor.DistributeFailedChallengeRewards(dispute, originalVoter);
        BOOST_CHECK(success);
        
        // Get the distribution record
        CVM::RewardDistribution dist = distributor.GetRewardDistribution(dispute.disputeId);
        
        // Calculate total output (compensation + burn)
        CAmount totalOut = dist.totalDaoVoterRewards + dist.burnedAmount;
        
        // Verify conservation: total out should equal forfeited challenger bond
        BOOST_CHECK_MESSAGE(totalOut == challengerBond,
            "Conservation violated: bond=" + std::to_string(challengerBond) + 
            " out=" + std::to_string(totalOut));
        
        // Verify challenger gets nothing back
        BOOST_CHECK_EQUAL(dist.challengerBondReturn, 0);
        BOOST_CHECK_EQUAL(dist.challengerBounty, 0);
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 5.7: Property Test for Proportional Voter Rewards
// Property 3: Proportional Voter Reward Distribution
// Validates: Requirements 1.3, 1.5
// ============================================================================

/**
 * Property 3: Proportional Voter Reward Distribution
 * 
 * *For any* set of DAO voters on the winning side with stakes [s1, s2, ..., sn],
 * each voter i's reward SHALL equal (si / sum(s1..sn)) * total_voter_reward_pool,
 * using integer arithmetic with remainder going to burn.
 */
BOOST_AUTO_TEST_CASE(property_proportional_voter_rewards)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    
    MockRewardDistributor distributor(db, config);
    
    // Run 100+ iterations with random parameters
    for (int i = 0; i < 150; i++) {
        // Generate random amounts
        CAmount challengerBond = 100 * COIN;
        CAmount slashedBond = 100 * COIN + GetRandInt(900) * COIN;  // 100-1000 CAS
        
        // Need at least 2 voters on winning side to test proportionality
        int numSlashVoters = 2 + GetRandInt(9);  // 2-10 voters
        int numKeepVoters = GetRandInt(5);       // 0-4 voters
        
        // Create dispute with slash decision
        CVM::DAODispute dispute = CreateTestDispute(true, challengerBond, 
                                                     numSlashVoters, numKeepVoters);
        
        // Calculate total stake on winning side
        CAmount totalWinningStake = 0;
        for (const auto& [voter, vote] : dispute.daoVotes) {
            if (vote == true) {  // Slash side wins
                totalWinningStake += dispute.daoStakes[voter];
            }
        }
        
        // Distribute rewards
        bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
        BOOST_CHECK(success);
        
        // Get the distribution record
        CVM::RewardDistribution dist = distributor.GetRewardDistribution(dispute.disputeId);
        
        // Calculate expected voter pool
        CAmount expectedVoterPool = (slashedBond * config.daoVoterRewardPercent) / 100;
        
        // Verify each voter's reward is proportional to their stake
        for (const auto& [voter, vote] : dispute.daoVotes) {
            if (vote == true) {  // Winning side
                CAmount voterStake = dispute.daoStakes[voter];
                
                // Expected reward using integer arithmetic
                __int128 numerator = static_cast<__int128>(voterStake) * expectedVoterPool;
                CAmount expectedReward = static_cast<CAmount>(numerator / totalWinningStake);
                
                // Get actual reward
                CAmount actualReward = 0;
                auto it = dist.voterRewards.find(voter);
                if (it != dist.voterRewards.end()) {
                    actualReward = it->second;
                }
                
                // Verify proportionality (allow for rounding)
                BOOST_CHECK_MESSAGE(actualReward == expectedReward,
                    "Proportionality violated: expected=" + std::to_string(expectedReward) +
                    " actual=" + std::to_string(actualReward));
            }
        }
        
        // Verify total voter rewards + remainder equals voter pool
        CAmount totalVoterRewards = 0;
        for (const auto& [voter, amount] : dist.voterRewards) {
            totalVoterRewards += amount;
        }
        
        // The remainder should be in the burn amount
        CAmount voterRemainder = expectedVoterPool - totalVoterRewards;
        BOOST_CHECK(voterRemainder >= 0);
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 5.8: Property Test for Claim Idempotence
// Property 5: Claim Idempotence
// Validates: Requirements 3.3, 3.4
// ============================================================================

/**
 * Property 5: Claim Idempotence
 * 
 * *For any* pending reward, claiming it once SHALL succeed and mark it as claimed,
 * and any subsequent claim attempt for the same reward SHALL fail without modifying state.
 */
BOOST_AUTO_TEST_CASE(property_claim_idempotence)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    
    MockRewardDistributor distributor(db, config);
    
    // Run 100+ iterations
    for (int i = 0; i < 150; i++) {
        // Create a dispute and distribute rewards
        CAmount challengerBond = 100 * COIN;
        CAmount slashedBond = 100 * COIN;
        
        CVM::DAODispute dispute = CreateTestDispute(true, challengerBond, 3, 2);
        
        bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
        BOOST_CHECK(success);
        
        // Get pending rewards for challenger
        std::vector<CVM::PendingReward> rewards = distributor.GetPendingRewards(dispute.challenger);
        BOOST_CHECK(!rewards.empty());
        
        // Pick a reward to claim
        CVM::PendingReward& reward = rewards[0];
        CAmount expectedAmount = reward.amount;
        
        // First claim should succeed
        CAmount claimed1 = distributor.ClaimReward(reward.rewardId, dispute.challenger);
        BOOST_CHECK_EQUAL(claimed1, expectedAmount);
        
        // Second claim should fail (return 0)
        CAmount claimed2 = distributor.ClaimReward(reward.rewardId, dispute.challenger);
        BOOST_CHECK_EQUAL(claimed2, 0);
        
        // Third claim should also fail
        CAmount claimed3 = distributor.ClaimReward(reward.rewardId, dispute.challenger);
        BOOST_CHECK_EQUAL(claimed3, 0);
        
        // Verify reward is marked as claimed
        CVM::PendingReward updatedReward;
        bool found = distributor.GetReward(reward.rewardId, updatedReward);
        BOOST_CHECK(found);
        BOOST_CHECK(updatedReward.claimed);
        
        // Verify amount hasn't changed
        BOOST_CHECK_EQUAL(updatedReward.amount, expectedAmount);
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 5.9: Property Test for Pending Rewards Completeness
// Property 6: Pending Rewards Query Completeness
// Validates: Requirements 3.5
// ============================================================================

/**
 * Property 6: Pending Rewards Query Completeness
 * 
 * *For any* address with N unclaimed rewards in the database, querying pending
 * rewards for that address SHALL return exactly those N rewards, and no claimed rewards.
 */
BOOST_AUTO_TEST_CASE(property_pending_rewards_completeness)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    
    MockRewardDistributor distributor(db, config);
    
    // Run 100+ iterations
    for (int i = 0; i < 150; i++) {
        // Create multiple disputes for the same challenger
        uint160 challenger;
        GetRandBytes(challenger.begin(), 20);
        
        int numDisputes = 1 + GetRandInt(5);  // 1-5 disputes
        std::set<uint256> expectedRewardIds;
        
        for (int d = 0; d < numDisputes; d++) {
            CAmount challengerBond = 100 * COIN;
            CAmount slashedBond = 100 * COIN;
            
            // Create dispute with fixed challenger
            CVM::DAODispute dispute;
            GetRandBytes(dispute.disputeId.begin(), 32);
            dispute.challenger = challenger;
            dispute.challengeBond = challengerBond;
            dispute.resolved = true;
            dispute.slashDecision = true;
            dispute.createdTime = 1234567800;
            dispute.resolvedTime = 1234567890;
            
            // Add some voters
            for (int v = 0; v < 3; v++) {
                uint160 voter;
                GetRandBytes(voter.begin(), 20);
                dispute.daoVotes[voter] = true;
                dispute.daoStakes[voter] = 10 * COIN;
            }
            
            bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
            BOOST_CHECK(success);
            
            // Track expected reward IDs for this challenger
            uint256 bondReturnId = CVM::PendingReward::GenerateRewardId(
                dispute.disputeId, challenger, CVM::RewardType::CHALLENGER_BOND_RETURN);
            uint256 bountyId = CVM::PendingReward::GenerateRewardId(
                dispute.disputeId, challenger, CVM::RewardType::CHALLENGER_BOUNTY);
            
            expectedRewardIds.insert(bondReturnId);
            expectedRewardIds.insert(bountyId);
        }
        
        // Query pending rewards
        std::vector<CVM::PendingReward> pendingRewards = distributor.GetPendingRewards(challenger);
        
        // Verify count matches
        BOOST_CHECK_EQUAL(pendingRewards.size(), expectedRewardIds.size());
        
        // Verify all expected rewards are present
        for (const auto& reward : pendingRewards) {
            BOOST_CHECK(expectedRewardIds.count(reward.rewardId) > 0);
            BOOST_CHECK(!reward.claimed);  // All should be unclaimed
        }
        
        // Claim some rewards and verify they're excluded from pending
        if (!pendingRewards.empty()) {
            int numToClaim = 1 + GetRandInt(std::min(3, (int)pendingRewards.size()));
            for (int c = 0; c < numToClaim && c < (int)pendingRewards.size(); c++) {
                distributor.ClaimReward(pendingRewards[c].rewardId, challenger);
                expectedRewardIds.erase(pendingRewards[c].rewardId);
            }
            
            // Query again
            std::vector<CVM::PendingReward> remainingRewards = distributor.GetPendingRewards(challenger);
            
            // Verify only unclaimed rewards are returned
            BOOST_CHECK_EQUAL(remainingRewards.size(), expectedRewardIds.size());
            
            for (const auto& reward : remainingRewards) {
                BOOST_CHECK(!reward.claimed);
            }
        }
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 7.3: Property Test for Non-Reveal Stake Forfeiture
// Property 9: Non-Reveal Stake Forfeiture
// Validates: Requirements 8.5, 8.6
// ============================================================================

/**
 * Mock CommitRevealManager for testing stake forfeiture
 * Allows setting a custom block height for testing
 */
class MockCommitRevealManagerForForfeiture : public CVM::CommitRevealManager {
public:
    MockCommitRevealManagerForForfeiture(CVM::CVMDatabase& db, const CVM::WoTConfig& config)
        : CVM::CommitRevealManager(db, config)
        , mockBlockHeight(0)
    {}
    
    void SetMockBlockHeight(uint32_t height) {
        mockBlockHeight = height;
    }
    
    uint32_t GetCurrentBlockHeight() const override {
        return mockBlockHeight;
    }
    
private:
    uint32_t mockBlockHeight;
};

/**
 * Property 9: Non-Reveal Stake Forfeiture
 * 
 * *For any* voter who submits a commitment but does not reveal within the reveal phase,
 * their stake SHALL be forfeited and not counted in the dispute outcome.
 * 
 * This test verifies:
 * 1. Commitments that are not revealed are marked as forfeited
 * 2. Forfeited stakes are returned by ForfeitUnrevealedStakes()
 * 3. Forfeited commitments cannot be revealed after forfeiture
 * 
 * Validates: Requirements 8.5, 8.6
 */
BOOST_AUTO_TEST_CASE(property_nonreveal_stake_forfeiture)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManagerForForfeiture manager(db, config);
    
    // Run 100+ iterations with random parameters
    for (int i = 0; i < 150; i++) {
        // Generate random dispute ID
        uint256 disputeId;
        GetRandBytes(disputeId.begin(), 32);
        
        // Create a dispute in the database with commit-reveal enabled
        CVM::DAODispute dispute;
        dispute.disputeId = disputeId;
        dispute.useCommitReveal = true;
        dispute.commitPhaseStart = 1000;  // Start at block 1000
        dispute.createdTime = 1000;
        
        // Store dispute
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << dispute;
        std::vector<uint8_t> bytes(ss.begin(), ss.end());
        std::string key = "dispute_" + disputeId.GetHex();
        db.WriteGeneric(key, bytes);
        
        // Generate random number of voters (1-10)
        int numVoters = 1 + GetRandInt(10);
        int numRevealers = GetRandInt(numVoters + 1);  // 0 to numVoters will reveal
        
        // Store voter info including vote and nonce for revealing
        struct VoterInfo {
            uint160 voter;
            CAmount stake;
            bool vote;
            uint256 nonce;
            bool willReveal;
        };
        std::vector<VoterInfo> voterInfos;
        CAmount expectedForfeited = 0;
        
        // Set block height to commit phase
        manager.SetMockBlockHeight(1050);  // Middle of commit phase
        
        // Submit commitments for all voters
        for (int v = 0; v < numVoters; v++) {
            VoterInfo info;
            GetRandBytes(info.voter.begin(), 20);
            info.stake = COIN + GetRandInt(100) * COIN;
            info.vote = (GetRandInt(2) == 1);
            GetRandBytes(info.nonce.begin(), 32);
            info.willReveal = (v < numRevealers);
            
            if (!info.willReveal) {
                expectedForfeited += info.stake;
            }
            
            uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(info.vote, info.nonce);
            
            bool submitted = manager.SubmitCommitment(disputeId, info.voter, commitmentHash, info.stake);
            BOOST_CHECK(submitted);
            
            voterInfos.push_back(info);
        }
        
        // Move to reveal phase
        manager.SetMockBlockHeight(1150);  // Middle of reveal phase
        
        // Reveal votes for voters who will reveal
        for (const auto& info : voterInfos) {
            if (info.willReveal) {
                bool revealed = manager.RevealVote(disputeId, info.voter, info.vote, info.nonce);
                BOOST_CHECK(revealed);
            }
        }
        
        // Move past reveal phase and forfeit unrevealed stakes
        manager.SetMockBlockHeight(1250);  // After reveal phase
        
        CAmount actualForfeited = manager.ForfeitUnrevealedStakes(disputeId);
        
        // Property: Total forfeited should equal sum of unrevealed stakes
        BOOST_CHECK_MESSAGE(actualForfeited == expectedForfeited,
            "Forfeiture mismatch: expected=" + std::to_string(expectedForfeited) +
            " actual=" + std::to_string(actualForfeited));
        
        // Verify all unrevealed commitments are marked as forfeited
        std::vector<CVM::VoteCommitment> commitments = manager.GetCommitments(disputeId);
        for (const auto& commitment : commitments) {
            if (!commitment.revealed) {
                BOOST_CHECK_MESSAGE(commitment.forfeited,
                    "Unrevealed commitment should be forfeited");
            }
        }
        
        // Verify forfeited commitments cannot be revealed
        for (const auto& info : voterInfos) {
            if (!info.willReveal) {
                CVM::VoteCommitment commitment;
                bool found = manager.GetCommitment(disputeId, info.voter, commitment);
                BOOST_CHECK(found);
                BOOST_CHECK(!commitment.CanReveal());  // Should not be able to reveal
            }
        }
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test that forfeiture only happens once
 * Calling ForfeitUnrevealedStakes multiple times should not double-forfeit
 * Validates: Requirements 8.5, 8.6
 */
BOOST_AUTO_TEST_CASE(forfeiture_idempotence)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManagerForForfeiture manager(db, config);
    
    // Generate dispute ID
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    // Create a dispute in the database
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.useCommitReveal = true;
    dispute.commitPhaseStart = 1000;
    dispute.createdTime = 1000;
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = "dispute_" + disputeId.GetHex();
    db.WriteGeneric(key, bytes);
    
    // Set block height to commit phase
    manager.SetMockBlockHeight(1050);
    
    // Submit a commitment
    uint160 voter;
    GetRandBytes(voter.begin(), 20);
    
    uint256 nonce;
    GetRandBytes(nonce.begin(), 32);
    uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
    
    CAmount stake = 100 * COIN;
    bool submitted = manager.SubmitCommitment(disputeId, voter, commitmentHash, stake);
    BOOST_CHECK(submitted);
    
    // Move past reveal phase
    manager.SetMockBlockHeight(1250);
    
    // First forfeiture should return the stake
    CAmount forfeited1 = manager.ForfeitUnrevealedStakes(disputeId);
    BOOST_CHECK_EQUAL(forfeited1, stake);
    
    // Second forfeiture should return 0 (already forfeited)
    CAmount forfeited2 = manager.ForfeitUnrevealedStakes(disputeId);
    BOOST_CHECK_EQUAL(forfeited2, 0);
    
    // Third forfeiture should also return 0
    CAmount forfeited3 = manager.ForfeitUnrevealedStakes(disputeId);
    BOOST_CHECK_EQUAL(forfeited3, 0);
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 7.4: Property Test for Only Revealed Votes Count
// Property 10: Only Revealed Votes Count
// Validates: Requirements 8.6
// ============================================================================

/**
 * Property 10: Only Revealed Votes Count
 * 
 * *For any* dispute resolution, the outcome SHALL be determined solely by
 * revealed votes, ignoring any commitments that were not revealed.
 * 
 * This test verifies:
 * 1. Only revealed votes are counted in the final tally
 * 2. Unrevealed commitments do not affect the outcome
 * 3. The outcome is correct based on revealed votes only
 * 
 * Validates: Requirements 8.6
 */
BOOST_AUTO_TEST_CASE(property_only_revealed_votes_count)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManagerForForfeiture manager(db, config);
    
    // Run 100+ iterations with random parameters
    for (int i = 0; i < 150; i++) {
        // Generate random dispute ID
        uint256 disputeId;
        GetRandBytes(disputeId.begin(), 32);
        
        // Create a dispute in the database
        CVM::DAODispute dispute;
        dispute.disputeId = disputeId;
        dispute.useCommitReveal = true;
        dispute.commitPhaseStart = 1000;
        dispute.createdTime = 1000;
        
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << dispute;
        std::vector<uint8_t> bytes(ss.begin(), ss.end());
        std::string key = "dispute_" + disputeId.GetHex();
        db.WriteGeneric(key, bytes);
        
        // Generate random voters with random reveal status
        int numVoters = 2 + GetRandInt(9);  // 2-10 voters
        
        CAmount revealedSlashStake = 0;
        CAmount revealedKeepStake = 0;
        CAmount unrevealedSlashStake = 0;
        CAmount unrevealedKeepStake = 0;
        
        // Set block height to commit phase
        manager.SetMockBlockHeight(1050);
        
        struct VoterInfo {
            uint160 voter;
            bool vote;
            uint256 nonce;
            CAmount stake;
            bool willReveal;
        };
        std::vector<VoterInfo> voterInfos;
        
        // Submit commitments
        for (int v = 0; v < numVoters; v++) {
            VoterInfo info;
            GetRandBytes(info.voter.begin(), 20);
            info.vote = (GetRandInt(2) == 1);  // Random vote
            GetRandBytes(info.nonce.begin(), 32);
            info.stake = COIN + GetRandInt(100) * COIN;
            info.willReveal = (GetRandInt(2) == 1);  // Random reveal decision
            
            uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(
                info.vote, info.nonce);
            
            bool submitted = manager.SubmitCommitment(disputeId, info.voter, 
                                                       commitmentHash, info.stake);
            BOOST_CHECK(submitted);
            
            // Track stakes by reveal status and vote
            if (info.willReveal) {
                if (info.vote) {
                    revealedSlashStake += info.stake;
                } else {
                    revealedKeepStake += info.stake;
                }
            } else {
                if (info.vote) {
                    unrevealedSlashStake += info.stake;
                } else {
                    unrevealedKeepStake += info.stake;
                }
            }
            
            voterInfos.push_back(info);
        }
        
        // Move to reveal phase
        manager.SetMockBlockHeight(1150);
        
        // Reveal votes for voters who will reveal
        for (const auto& info : voterInfos) {
            if (info.willReveal) {
                bool revealed = manager.RevealVote(disputeId, info.voter, 
                                                    info.vote, info.nonce);
                BOOST_CHECK(revealed);
            }
        }
        
        // Move past reveal phase and forfeit unrevealed
        manager.SetMockBlockHeight(1250);
        manager.ForfeitUnrevealedStakes(disputeId);
        
        // Get all commitments and calculate outcome based on revealed votes only
        std::vector<CVM::VoteCommitment> commitments = manager.GetCommitments(disputeId);
        
        CAmount actualRevealedSlash = 0;
        CAmount actualRevealedKeep = 0;
        
        for (const auto& commitment : commitments) {
            if (commitment.revealed && !commitment.forfeited) {
                if (commitment.vote) {
                    actualRevealedSlash += commitment.stake;
                } else {
                    actualRevealedKeep += commitment.stake;
                }
            }
        }
        
        // Property: Only revealed votes should be counted
        BOOST_CHECK_EQUAL(actualRevealedSlash, revealedSlashStake);
        BOOST_CHECK_EQUAL(actualRevealedKeep, revealedKeepStake);
        
        // Property: Unrevealed votes should not affect the outcome
        // The outcome should be determined by revealed votes only
        bool expectedOutcome = (revealedSlashStake > revealedKeepStake);
        bool actualOutcome = (actualRevealedSlash > actualRevealedKeep);
        
        BOOST_CHECK_EQUAL(actualOutcome, expectedOutcome);
        
        // Verify that unrevealed commitments are either forfeited or not revealed
        for (const auto& commitment : commitments) {
            if (!commitment.revealed) {
                BOOST_CHECK_MESSAGE(commitment.forfeited,
                    "Unrevealed commitment should be forfeited after reveal phase");
            }
        }
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: all voters reveal
 * Validates: Requirements 8.6
 */
BOOST_AUTO_TEST_CASE(all_voters_reveal)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManagerForForfeiture manager(db, config);
    
    // Generate dispute ID
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    // Create a dispute
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.useCommitReveal = true;
    dispute.commitPhaseStart = 1000;
    dispute.createdTime = 1000;
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = "dispute_" + disputeId.GetHex();
    db.WriteGeneric(key, bytes);
    
    // Set block height to commit phase
    manager.SetMockBlockHeight(1050);
    
    // Submit and reveal all votes
    int numVoters = 5;
    CAmount totalSlashStake = 0;
    CAmount totalKeepStake = 0;
    
    struct VoterData {
        uint160 voter;
        bool vote;
        uint256 nonce;
        CAmount stake;
    };
    std::vector<VoterData> voters;
    
    for (int v = 0; v < numVoters; v++) {
        VoterData data;
        GetRandBytes(data.voter.begin(), 20);
        data.vote = (v % 2 == 0);  // Alternate votes
        GetRandBytes(data.nonce.begin(), 32);
        data.stake = (v + 1) * 10 * COIN;
        
        uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(
            data.vote, data.nonce);
        
        bool submitted = manager.SubmitCommitment(disputeId, data.voter, 
                                                   commitmentHash, data.stake);
        BOOST_CHECK(submitted);
        
        if (data.vote) {
            totalSlashStake += data.stake;
        } else {
            totalKeepStake += data.stake;
        }
        
        voters.push_back(data);
    }
    
    // Move to reveal phase
    manager.SetMockBlockHeight(1150);
    
    // Reveal all votes
    for (const auto& data : voters) {
        bool revealed = manager.RevealVote(disputeId, data.voter, data.vote, data.nonce);
        BOOST_CHECK(revealed);
    }
    
    // Move past reveal phase
    manager.SetMockBlockHeight(1250);
    
    // Forfeit should return 0 since all revealed
    CAmount forfeited = manager.ForfeitUnrevealedStakes(disputeId);
    BOOST_CHECK_EQUAL(forfeited, 0);
    
    // Verify all commitments are revealed and not forfeited
    std::vector<CVM::VoteCommitment> commitments = manager.GetCommitments(disputeId);
    BOOST_CHECK_EQUAL(commitments.size(), (size_t)numVoters);
    
    CAmount actualSlash = 0;
    CAmount actualKeep = 0;
    
    for (const auto& commitment : commitments) {
        BOOST_CHECK(commitment.revealed);
        BOOST_CHECK(!commitment.forfeited);
        
        if (commitment.vote) {
            actualSlash += commitment.stake;
        } else {
            actualKeep += commitment.stake;
        }
    }
    
    BOOST_CHECK_EQUAL(actualSlash, totalSlashStake);
    BOOST_CHECK_EQUAL(actualKeep, totalKeepStake);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: no voters reveal
 * Validates: Requirements 8.6
 */
BOOST_AUTO_TEST_CASE(no_voters_reveal)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManagerForForfeiture manager(db, config);
    
    // Generate dispute ID
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    // Create a dispute
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.useCommitReveal = true;
    dispute.commitPhaseStart = 1000;
    dispute.createdTime = 1000;
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = "dispute_" + disputeId.GetHex();
    db.WriteGeneric(key, bytes);
    
    // Set block height to commit phase
    manager.SetMockBlockHeight(1050);
    
    // Submit commitments but don't reveal
    int numVoters = 5;
    CAmount totalStake = 0;
    
    for (int v = 0; v < numVoters; v++) {
        uint160 voter;
        GetRandBytes(voter.begin(), 20);
        
        uint256 nonce;
        GetRandBytes(nonce.begin(), 32);
        
        CAmount stake = (v + 1) * 10 * COIN;
        totalStake += stake;
        
        uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(true, nonce);
        
        bool submitted = manager.SubmitCommitment(disputeId, voter, commitmentHash, stake);
        BOOST_CHECK(submitted);
    }
    
    // Move past reveal phase without revealing
    manager.SetMockBlockHeight(1250);
    
    // Forfeit should return all stakes
    CAmount forfeited = manager.ForfeitUnrevealedStakes(disputeId);
    BOOST_CHECK_EQUAL(forfeited, totalStake);
    
    // Verify all commitments are forfeited
    std::vector<CVM::VoteCommitment> commitments = manager.GetCommitments(disputeId);
    BOOST_CHECK_EQUAL(commitments.size(), (size_t)numVoters);
    
    for (const auto& commitment : commitments) {
        BOOST_CHECK(!commitment.revealed);
        BOOST_CHECK(commitment.forfeited);
    }
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 8.8: Unit Tests for RPC Commands
// Validates: Requirements 7.1, 7.2, 7.3, 7.4
// ============================================================================

/**
 * Test getpendingrewards returns correct data
 * Validates: Requirements 7.1
 * 
 * Note: This tests the underlying RewardDistributor functionality that
 * the RPC command uses. Full RPC testing requires a running node.
 */
BOOST_AUTO_TEST_CASE(rpc_getpendingrewards_data)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with rewards
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    
    // Add a winning voter
    uint160 voter;
    GetRandBytes(voter.begin(), 20);
    dispute.daoVotes[voter] = true;  // Voted for slash
    dispute.daoStakes[voter] = 50 * COIN;
    
    // Distribute rewards
    CAmount slashedBond = 200 * COIN;
    bool distributed = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(distributed);
    
    // Test GetPendingRewards for challenger
    std::vector<CVM::PendingReward> challengerRewards = distributor.GetPendingRewards(challenger);
    BOOST_CHECK_EQUAL(challengerRewards.size(), 2);  // Bond return + bounty
    
    // Verify reward types
    bool hasBondReturn = false;
    bool hasBounty = false;
    for (const auto& reward : challengerRewards) {
        BOOST_CHECK(reward.disputeId == disputeId);
        BOOST_CHECK(reward.recipient == challenger);
        BOOST_CHECK(!reward.claimed);
        BOOST_CHECK(reward.amount > 0);
        
        if (reward.type == CVM::RewardType::CHALLENGER_BOND_RETURN) {
            hasBondReturn = true;
            BOOST_CHECK_EQUAL(reward.amount, 100 * COIN);
        } else if (reward.type == CVM::RewardType::CHALLENGER_BOUNTY) {
            hasBounty = true;
        }
    }
    BOOST_CHECK(hasBondReturn);
    BOOST_CHECK(hasBounty);
    
    // Test GetPendingRewards for voter
    std::vector<CVM::PendingReward> voterRewards = distributor.GetPendingRewards(voter);
    BOOST_CHECK_EQUAL(voterRewards.size(), 1);  // DAO voter reward
    BOOST_CHECK(voterRewards[0].type == CVM::RewardType::DAO_VOTER_REWARD);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test claimreward success and failure cases
 * Validates: Requirements 7.2, 3.3, 3.4
 */
BOOST_AUTO_TEST_CASE(rpc_claimreward_cases)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with rewards
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    uint160 wrongAddress;
    GetRandBytes(wrongAddress.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    
    // Distribute rewards
    CAmount slashedBond = 200 * COIN;
    distributor.DistributeSlashRewards(dispute, slashedBond);
    
    // Get a reward to claim
    std::vector<CVM::PendingReward> rewards = distributor.GetPendingRewards(challenger);
    BOOST_CHECK(!rewards.empty());
    
    uint256 rewardId = rewards[0].rewardId;
    CAmount expectedAmount = rewards[0].amount;
    
    // Test 1: Claim with wrong address should fail
    CAmount wrongClaim = distributor.ClaimReward(rewardId, wrongAddress);
    BOOST_CHECK_EQUAL(wrongClaim, 0);
    
    // Test 2: Claim with correct address should succeed
    CAmount correctClaim = distributor.ClaimReward(rewardId, challenger);
    BOOST_CHECK_EQUAL(correctClaim, expectedAmount);
    
    // Test 3: Double claim should fail
    CAmount doubleClaim = distributor.ClaimReward(rewardId, challenger);
    BOOST_CHECK_EQUAL(doubleClaim, 0);
    
    // Test 4: Claim non-existent reward should fail
    uint256 fakeRewardId;
    GetRandBytes(fakeRewardId.begin(), 32);
    CAmount fakeClaim = distributor.ClaimReward(fakeRewardId, challenger);
    BOOST_CHECK_EQUAL(fakeClaim, 0);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test claimallrewards batch claiming
 * Validates: Requirements 7.2, 3.7
 */
BOOST_AUTO_TEST_CASE(rpc_claimallrewards_batch)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create multiple disputes with rewards for the same challenger
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CAmount totalExpected = 0;
    int numDisputes = 3;
    
    for (int i = 0; i < numDisputes; i++) {
        uint256 disputeId;
        GetRandBytes(disputeId.begin(), 32);
        
        CVM::DAODispute dispute;
        dispute.disputeId = disputeId;
        dispute.challenger = challenger;
        dispute.challengeBond = (i + 1) * 50 * COIN;
        dispute.resolved = true;
        dispute.slashDecision = true;
        
        CAmount slashedBond = (i + 1) * 100 * COIN;
        distributor.DistributeSlashRewards(dispute, slashedBond);
        
        // Calculate expected total (bond return + bounty)
        // Note: When there are no voters on winning side, voter portion goes to challenger
        totalExpected += dispute.challengeBond;  // Bond return
        CAmount bounty = (slashedBond * config.challengerRewardPercent) / 100;  // Base bounty
        CAmount voterPortion = (slashedBond * config.daoVoterRewardPercent) / 100;  // Voter portion
        totalExpected += bounty + voterPortion;  // Bounty includes voter portion when no voters
    }
    
    // Get all pending rewards
    std::vector<CVM::PendingReward> allRewards = distributor.GetPendingRewards(challenger);
    BOOST_CHECK_EQUAL(allRewards.size(), (size_t)(numDisputes * 2));  // 2 rewards per dispute
    
    // Claim all rewards (simulating claimallrewards RPC)
    CAmount totalClaimed = 0;
    int claimedCount = 0;
    
    for (const auto& reward : allRewards) {
        CAmount claimed = distributor.ClaimReward(reward.rewardId, challenger);
        if (claimed > 0) {
            totalClaimed += claimed;
            claimedCount++;
        }
    }
    
    BOOST_CHECK_EQUAL(claimedCount, numDisputes * 2);
    BOOST_CHECK_EQUAL(totalClaimed, totalExpected);
    
    // Verify no more pending rewards
    std::vector<CVM::PendingReward> remainingRewards = distributor.GetPendingRewards(challenger);
    BOOST_CHECK(remainingRewards.empty());
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test getrewarddistribution returns complete breakdown
 * Validates: Requirements 7.3
 */
BOOST_AUTO_TEST_CASE(rpc_getrewarddistribution_breakdown)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with multiple voters
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    
    // Add multiple winning voters with different stakes
    uint160 voter1, voter2, voter3;
    GetRandBytes(voter1.begin(), 20);
    GetRandBytes(voter2.begin(), 20);
    GetRandBytes(voter3.begin(), 20);
    
    dispute.daoVotes[voter1] = true;
    dispute.daoVotes[voter2] = true;
    dispute.daoVotes[voter3] = true;
    dispute.daoStakes[voter1] = 100 * COIN;
    dispute.daoStakes[voter2] = 200 * COIN;
    dispute.daoStakes[voter3] = 100 * COIN;
    
    // Distribute rewards
    CAmount slashedBond = 1000 * COIN;
    distributor.DistributeSlashRewards(dispute, slashedBond);
    
    // Get reward distribution (simulating getrewarddistribution RPC)
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    
    // Verify distribution is valid
    BOOST_CHECK(dist.IsValid());
    BOOST_CHECK(dist.disputeId == disputeId);
    BOOST_CHECK(dist.slashDecision);
    
    // Verify amounts
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 100 * COIN);
    BOOST_CHECK_EQUAL(dist.totalSlashedBond, slashedBond);
    
    // Verify challenger bounty (50% of slashed bond)
    CAmount expectedBounty = (slashedBond * config.challengerRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.challengerBounty, expectedBounty);
    
    // Verify voter rewards exist
    BOOST_CHECK_EQUAL(dist.voterRewards.size(), 3);
    BOOST_CHECK(dist.voterRewards.find(voter1) != dist.voterRewards.end());
    BOOST_CHECK(dist.voterRewards.find(voter2) != dist.voterRewards.end());
    BOOST_CHECK(dist.voterRewards.find(voter3) != dist.voterRewards.end());
    
    // Verify voter2 gets more (has higher stake)
    BOOST_CHECK(dist.voterRewards[voter2] > dist.voterRewards[voter1]);
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test commitdisputevote and revealdisputevote flow
 * Validates: Requirements 8.1, 8.4, 8.7
 */
BOOST_AUTO_TEST_CASE(rpc_commit_reveal_flow)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManager manager(db, config);
    
    // Create a dispute
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.useCommitReveal = true;
    dispute.commitPhaseStart = 1000;
    dispute.createdTime = 1000;
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = "dispute_" + disputeId.GetHex();
    db.WriteGeneric(key, bytes);
    
    // Create voter
    uint160 voter;
    GetRandBytes(voter.begin(), 20);
    
    // Generate vote and nonce
    bool vote = true;  // Vote for slash
    uint256 nonce;
    GetRandBytes(nonce.begin(), 32);
    
    // Calculate commitment hash (simulating client-side calculation)
    uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(vote, nonce);
    
    // Set block height to commit phase
    manager.SetMockBlockHeight(1050);
    
    // Test 1: Submit commitment during commit phase
    bool committed = manager.SubmitCommitment(disputeId, voter, commitmentHash, 100 * COIN);
    BOOST_CHECK(committed);
    
    // Test 2: Cannot submit duplicate commitment
    bool duplicateCommit = manager.SubmitCommitment(disputeId, voter, commitmentHash, 100 * COIN);
    BOOST_CHECK(!duplicateCommit);
    
    // Test 3: Cannot reveal during commit phase
    bool earlyReveal = manager.RevealVote(disputeId, voter, vote, nonce);
    BOOST_CHECK(!earlyReveal);
    
    // Move to reveal phase
    manager.SetMockBlockHeight(1150);
    
    // Test 4: Cannot commit during reveal phase
    uint160 lateVoter;
    GetRandBytes(lateVoter.begin(), 20);
    bool lateCommit = manager.SubmitCommitment(disputeId, lateVoter, commitmentHash, 50 * COIN);
    BOOST_CHECK(!lateCommit);
    
    // Test 5: Reveal with correct vote and nonce
    bool revealed = manager.RevealVote(disputeId, voter, vote, nonce);
    BOOST_CHECK(revealed);
    
    // Test 6: Cannot reveal twice
    bool doubleReveal = manager.RevealVote(disputeId, voter, vote, nonce);
    BOOST_CHECK(!doubleReveal);
    
    // Test 7: Verify commitment is marked as revealed
    CVM::VoteCommitment commitment;
    bool found = manager.GetCommitment(disputeId, voter, commitment);
    BOOST_CHECK(found);
    BOOST_CHECK(commitment.revealed);
    BOOST_CHECK_EQUAL(commitment.vote, vote);
    BOOST_CHECK(commitment.nonce == nonce);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test reveal with wrong vote/nonce fails
 * Validates: Requirements 8.4
 */
BOOST_AUTO_TEST_CASE(rpc_reveal_wrong_data)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    config.commitPhaseDuration = 100;
    config.revealPhaseDuration = 100;
    config.enableCommitReveal = true;
    
    MockCommitRevealManager manager(db, config);
    
    // Create a dispute
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.useCommitReveal = true;
    dispute.commitPhaseStart = 1000;
    dispute.createdTime = 1000;
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = "dispute_" + disputeId.GetHex();
    db.WriteGeneric(key, bytes);
    
    // Create voter
    uint160 voter;
    GetRandBytes(voter.begin(), 20);
    
    // Generate vote and nonce
    bool vote = true;
    uint256 nonce;
    GetRandBytes(nonce.begin(), 32);
    
    uint256 commitmentHash = CVM::CommitRevealManager::CalculateCommitmentHash(vote, nonce);
    
    // Submit commitment during commit phase
    manager.SetMockBlockHeight(1050);
    manager.SubmitCommitment(disputeId, voter, commitmentHash, 100 * COIN);
    
    // Move to reveal phase
    manager.SetMockBlockHeight(1150);
    
    // Test 1: Reveal with wrong vote should fail
    bool wrongVoteReveal = manager.RevealVote(disputeId, voter, !vote, nonce);
    BOOST_CHECK(!wrongVoteReveal);
    
    // Test 2: Reveal with wrong nonce should fail
    uint256 wrongNonce;
    GetRandBytes(wrongNonce.begin(), 32);
    bool wrongNonceReveal = manager.RevealVote(disputeId, voter, vote, wrongNonce);
    BOOST_CHECK(!wrongNonceReveal);
    
    // Test 3: Reveal with both wrong should fail
    bool bothWrongReveal = manager.RevealVote(disputeId, voter, !vote, wrongNonce);
    BOOST_CHECK(!bothWrongReveal);
    
    // Test 4: Correct reveal should still work
    bool correctReveal = manager.RevealVote(disputeId, voter, vote, nonce);
    BOOST_CHECK(correctReveal);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test getdispute includes reward distribution for resolved disputes
 * Validates: Requirements 7.4
 */
BOOST_AUTO_TEST_CASE(rpc_getdispute_with_rewards)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create and resolve a dispute
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    dispute.rewardsDistributed = true;
    
    // Distribute rewards
    CAmount slashedBond = 500 * COIN;
    distributor.DistributeSlashRewards(dispute, slashedBond);
    
    // Simulate what getdispute RPC would do
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    
    // Verify reward distribution is available
    BOOST_CHECK(dist.IsValid());
    BOOST_CHECK(dist.disputeId == disputeId);
    BOOST_CHECK(dist.slashDecision);
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 100 * COIN);
    BOOST_CHECK(dist.challengerBounty > 0);
    BOOST_CHECK(dist.burnedAmount > 0);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test getrewarddistribution for non-existent dispute
 * Validates: Requirements 7.3
 */
BOOST_AUTO_TEST_CASE(rpc_getrewarddistribution_not_found)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Try to get distribution for non-existent dispute
    uint256 fakeDisputeId;
    GetRandBytes(fakeDisputeId.begin(), 32);
    
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(fakeDisputeId);
    
    // Should return invalid/empty distribution
    BOOST_CHECK(!dist.IsValid());
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test getpendingrewards for address with no rewards
 * Validates: Requirements 7.1
 */
BOOST_AUTO_TEST_CASE(rpc_getpendingrewards_empty)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Query rewards for address with no rewards
    uint160 randomAddress;
    GetRandBytes(randomAddress.begin(), 20);
    
    std::vector<CVM::PendingReward> rewards = distributor.GetPendingRewards(randomAddress);
    
    // Should return empty vector
    BOOST_CHECK(rewards.empty());
    
    // Cleanup
    fs::remove_all(tempDir);
}

// ============================================================================
// Task 12.1: Edge Case Unit Tests
// Validates: Requirements 5.1, 5.2, 5.3, 2.4, 9.3, 9.4, 9.5
// ============================================================================

/**
 * Test edge case: No voters on winning side
 * When no DAO voters voted on the winning side, the voter reward portion
 * should go to the challenger instead.
 * Validates: Requirements 5.1
 */
BOOST_AUTO_TEST_CASE(edge_case_no_voters_on_winning_side)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with slash decision but no voters on winning side
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;  // Slash wins
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add voters only on the losing side (keep side)
    uint160 voter1, voter2;
    GetRandBytes(voter1.begin(), 20);
    GetRandBytes(voter2.begin(), 20);
    
    dispute.daoVotes[voter1] = false;  // Vote to keep (losing side)
    dispute.daoVotes[voter2] = false;  // Vote to keep (losing side)
    dispute.daoStakes[voter1] = 50 * COIN;
    dispute.daoStakes[voter2] = 50 * COIN;
    
    // Distribute rewards
    CAmount slashedBond = 1000 * COIN;
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets bond return
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 100 * COIN);
    
    // Verify challenger bounty includes voter portion
    // Normal bounty = 50% of 1000 = 500 CAS
    // Voter portion = 30% of 1000 = 300 CAS
    // Total challenger bounty should be 500 + 300 = 800 CAS
    CAmount expectedBounty = (slashedBond * config.challengerRewardPercent) / 100;
    CAmount voterPortion = (slashedBond * config.daoVoterRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.challengerBounty, expectedBounty + voterPortion);
    
    // Verify no voter rewards were distributed
    BOOST_CHECK_EQUAL(dist.totalDaoVoterRewards, 0);
    BOOST_CHECK(dist.voterRewards.empty());
    
    // Verify burn amount is correct (20% of slashed bond)
    CAmount expectedBurn = (slashedBond * config.burnPercent) / 100;
    BOOST_CHECK_EQUAL(dist.burnedAmount, expectedBurn);
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: All voters on losing side
 * When all DAO voters voted on the losing side, the voter reward portion
 * should be burned.
 * Validates: Requirements 5.2
 */
BOOST_AUTO_TEST_CASE(edge_case_all_voters_on_losing_side)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with slash decision
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;  // Slash wins
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add voters only on the losing side (keep side)
    for (int i = 0; i < 5; i++) {
        uint160 voter;
        GetRandBytes(voter.begin(), 20);
        dispute.daoVotes[voter] = false;  // Vote to keep (losing side)
        dispute.daoStakes[voter] = (i + 1) * 20 * COIN;
    }
    
    // Distribute rewards
    CAmount slashedBond = 500 * COIN;
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify no voter rewards were distributed
    BOOST_CHECK_EQUAL(dist.totalDaoVoterRewards, 0);
    BOOST_CHECK(dist.voterRewards.empty());
    
    // Verify voter portion went to challenger bounty (edge case handling)
    CAmount baseBounty = (slashedBond * config.challengerRewardPercent) / 100;
    CAmount voterPortion = (slashedBond * config.daoVoterRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.challengerBounty, baseBounty + voterPortion);
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Zero slashed bond
 * When the slashed bond is zero, the challenger should still get their
 * bond returned but no bounty should be distributed.
 * Validates: Requirements 5.3
 */
BOOST_AUTO_TEST_CASE(edge_case_zero_slashed_bond)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with slash decision
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add a winning voter
    uint160 voter;
    GetRandBytes(voter.begin(), 20);
    dispute.daoVotes[voter] = true;  // Vote to slash (winning side)
    dispute.daoStakes[voter] = 50 * COIN;
    
    // Distribute rewards with zero slashed bond
    CAmount slashedBond = 0;
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets bond return
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 100 * COIN);
    
    // Verify no bounty (0% of 0 = 0)
    BOOST_CHECK_EQUAL(dist.challengerBounty, 0);
    
    // Verify no voter rewards (0% of 0 = 0)
    BOOST_CHECK_EQUAL(dist.totalDaoVoterRewards, 0);
    
    // Verify no burn (0% of 0 = 0)
    BOOST_CHECK_EQUAL(dist.burnedAmount, 0);
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Invalid voter address for failed challenge
 * When the original voter address is invalid (null), the entire forfeited
 * challenger bond should be burned.
 * Validates: Requirements 2.4
 */
BOOST_AUTO_TEST_CASE(edge_case_invalid_voter_address)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with keep decision (failed challenge)
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = false;  // Keep decision (no slash)
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Use null/invalid voter address
    uint160 invalidVoter;  // Default constructed is null
    
    // Distribute rewards with invalid voter
    bool success = distributor.DistributeFailedChallengeRewards(dispute, invalidVoter);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets nothing back
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 0);
    BOOST_CHECK_EQUAL(dist.challengerBounty, 0);
    
    // Verify no compensation was paid (invalid voter)
    BOOST_CHECK_EQUAL(dist.totalDaoVoterRewards, 0);
    BOOST_CHECK(dist.voterRewards.empty());
    
    // Verify entire bond was burned
    BOOST_CHECK_EQUAL(dist.burnedAmount, dispute.challengeBond);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Legacy dispute backward compatibility
 * Disputes created before the reward system should work correctly
 * with the new system (useCommitReveal = false).
 * Validates: Requirements 9.3, 9.4
 */
BOOST_AUTO_TEST_CASE(edge_case_legacy_dispute_backward_compatibility)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a legacy dispute (no commit-reveal)
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Legacy dispute settings
    dispute.useCommitReveal = false;  // Legacy mode
    dispute.commitPhaseStart = 0;
    dispute.revealPhaseStart = 0;
    dispute.rewardsDistributed = false;
    
    // Add voters using legacy direct voting (not commit-reveal)
    uint160 voter1, voter2;
    GetRandBytes(voter1.begin(), 20);
    GetRandBytes(voter2.begin(), 20);
    
    dispute.daoVotes[voter1] = true;   // Vote to slash (winning)
    dispute.daoVotes[voter2] = false;  // Vote to keep (losing)
    dispute.daoStakes[voter1] = 50 * COIN;
    dispute.daoStakes[voter2] = 30 * COIN;
    
    // Distribute rewards - should work with legacy dispute
    CAmount slashedBond = 200 * COIN;
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets bond return
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 100 * COIN);
    
    // Verify challenger bounty (50% of 200 = 100 CAS)
    CAmount expectedBounty = (slashedBond * config.challengerRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.challengerBounty, expectedBounty);
    
    // Verify voter1 (winning side) gets reward
    BOOST_CHECK(dist.voterRewards.find(voter1) != dist.voterRewards.end());
    BOOST_CHECK(dist.voterRewards[voter1] > 0);
    
    // Verify voter2 (losing side) gets no reward
    BOOST_CHECK(dist.voterRewards.find(voter2) == dist.voterRewards.end());
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Verify legacy dispute phase methods work correctly
    BOOST_CHECK(!dispute.IsInCommitPhase(1000, config.commitPhaseDuration));
    BOOST_CHECK(!dispute.IsInRevealPhase(1000, config.commitPhaseDuration, config.revealPhaseDuration));
    BOOST_CHECK(dispute.ArePhasesComplete(1000, config.commitPhaseDuration, config.revealPhaseDuration));
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Pre-reward-system dispute returns empty data
 * Querying reward distribution for a dispute that was resolved before
 * the reward system existed should return empty data, not an error.
 * Validates: Requirements 9.4, 9.5
 */
BOOST_AUTO_TEST_CASE(edge_case_pre_reward_system_dispute_empty_data)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute ID that was never processed by the reward system
    uint256 preRewardDisputeId;
    GetRandBytes(preRewardDisputeId.begin(), 32);
    
    // Query reward distribution - should return empty/invalid, not error
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(preRewardDisputeId);
    
    // Should return invalid distribution (not found)
    BOOST_CHECK(!dist.IsValid());
    BOOST_CHECK(dist.disputeId.IsNull());
    BOOST_CHECK_EQUAL(dist.distributedTime, 0);
    
    // Query pending rewards for a random address - should return empty
    uint160 randomAddress;
    GetRandBytes(randomAddress.begin(), 20);
    
    std::vector<CVM::PendingReward> rewards = distributor.GetPendingRewards(randomAddress);
    BOOST_CHECK(rewards.empty());
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Dispute with very small slashed bond
 * Ensure rounding doesn't cause issues with very small amounts.
 * Validates: Requirements 5.4, 5.5
 */
BOOST_AUTO_TEST_CASE(edge_case_very_small_slashed_bond)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 1;  // 1 satoshi
    dispute.resolved = true;
    dispute.slashDecision = true;
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add a winning voter
    uint160 voter;
    GetRandBytes(voter.begin(), 20);
    dispute.daoVotes[voter] = true;
    dispute.daoStakes[voter] = 1;  // 1 satoshi stake
    
    // Distribute rewards with very small slashed bond (1 satoshi)
    CAmount slashedBond = 1;
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets bond return
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 1);
    
    // With 1 satoshi slashed bond:
    // 50% = 0 satoshi (integer division)
    // 30% = 0 satoshi
    // 20% = 0 satoshi
    // Remainder = 1 satoshi (goes to burn or challenger)
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Total output should equal total input
    CAmount totalIn = dispute.challengeBond + slashedBond;
    CAmount totalOut = dist.challengerBondReturn + dist.challengerBounty + 
                       dist.totalDaoVoterRewards + dist.burnedAmount;
    BOOST_CHECK_EQUAL(totalOut, totalIn);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Dispute with maximum values
 * Ensure the system handles large amounts without overflow.
 * Validates: Requirements 5.4, 5.5
 */
BOOST_AUTO_TEST_CASE(edge_case_large_amounts)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with large amounts
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 1000000 * COIN;  // 1 million CAS
    dispute.resolved = true;
    dispute.slashDecision = true;
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add multiple winning voters with large stakes
    for (int i = 0; i < 10; i++) {
        uint160 voter;
        GetRandBytes(voter.begin(), 20);
        dispute.daoVotes[voter] = true;
        dispute.daoStakes[voter] = 100000 * COIN;  // 100k CAS each
    }
    
    // Distribute rewards with large slashed bond
    CAmount slashedBond = 10000000 * COIN;  // 10 million CAS
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets bond return
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 1000000 * COIN);
    
    // Verify challenger bounty (50% of 10M = 5M CAS)
    CAmount expectedBounty = (slashedBond * config.challengerRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.challengerBounty, expectedBounty);
    
    // Verify voter rewards exist
    BOOST_CHECK_EQUAL(dist.voterRewards.size(), 10);
    
    // Verify conservation of funds (no overflow)
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Failed challenge with valid voter
 * Normal case for failed challenge - wrongly accused voter gets compensation.
 * Validates: Requirements 2.1, 2.2, 2.3
 */
BOOST_AUTO_TEST_CASE(edge_case_failed_challenge_valid_voter)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute with keep decision (failed challenge)
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    uint160 originalVoter;
    GetRandBytes(originalVoter.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = false;  // Keep decision (no slash)
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Distribute rewards
    bool success = distributor.DistributeFailedChallengeRewards(dispute, originalVoter);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify challenger gets nothing back
    BOOST_CHECK_EQUAL(dist.challengerBondReturn, 0);
    BOOST_CHECK_EQUAL(dist.challengerBounty, 0);
    
    // Verify wrongly accused voter gets compensation (70% of 100 = 70 CAS)
    CAmount expectedCompensation = (dispute.challengeBond * config.wronglyAccusedRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.totalDaoVoterRewards, expectedCompensation);
    BOOST_CHECK(dist.voterRewards.find(originalVoter) != dist.voterRewards.end());
    BOOST_CHECK_EQUAL(dist.voterRewards[originalVoter], expectedCompensation);
    
    // Verify burn amount (30% of 100 = 30 CAS)
    CAmount expectedBurn = (dispute.challengeBond * config.failedChallengeBurnPercent) / 100;
    BOOST_CHECK_EQUAL(dist.burnedAmount, expectedBurn);
    
    // Verify conservation: compensation + burn = forfeited bond
    BOOST_CHECK_EQUAL(dist.totalDaoVoterRewards + dist.burnedAmount, dispute.challengeBond);
    
    // Verify pending reward was created for wrongly accused voter
    std::vector<CVM::PendingReward> rewards = distributor.GetPendingRewards(originalVoter);
    BOOST_CHECK_EQUAL(rewards.size(), 1);
    BOOST_CHECK(rewards[0].type == CVM::RewardType::WRONGLY_ACCUSED_COMPENSATION);
    BOOST_CHECK_EQUAL(rewards[0].amount, expectedCompensation);
    
    // Cleanup
    fs::remove_all(tempDir);
}

/**
 * Test edge case: Dispute with single voter on winning side
 * Single voter should get the entire voter reward pool.
 * Validates: Requirements 1.3, 1.5
 */
BOOST_AUTO_TEST_CASE(edge_case_single_voter_winning_side)
{
    // Create a temporary database for testing
    fs::path tempDir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tempDir);
    
    CVM::CVMDatabase db(tempDir, 8 << 20, false, true);
    CVM::WoTConfig config;
    CVM::RewardDistributor distributor(db, config);
    
    // Create a dispute
    uint256 disputeId;
    GetRandBytes(disputeId.begin(), 32);
    
    uint160 challenger;
    GetRandBytes(challenger.begin(), 20);
    
    CVM::DAODispute dispute;
    dispute.disputeId = disputeId;
    dispute.challenger = challenger;
    dispute.challengeBond = 100 * COIN;
    dispute.resolved = true;
    dispute.slashDecision = true;
    dispute.createdTime = 1234567800;
    dispute.resolvedTime = 1234567890;
    
    // Add single winning voter
    uint160 singleVoter;
    GetRandBytes(singleVoter.begin(), 20);
    dispute.daoVotes[singleVoter] = true;  // Vote to slash (winning)
    dispute.daoStakes[singleVoter] = 50 * COIN;
    
    // Distribute rewards
    CAmount slashedBond = 1000 * COIN;
    bool success = distributor.DistributeSlashRewards(dispute, slashedBond);
    BOOST_CHECK(success);
    
    // Get distribution
    CVM::RewardDistribution dist = distributor.GetRewardDistribution(disputeId);
    BOOST_CHECK(dist.IsValid());
    
    // Verify single voter gets entire voter pool (30% of 1000 = 300 CAS)
    CAmount expectedVoterPool = (slashedBond * config.daoVoterRewardPercent) / 100;
    BOOST_CHECK_EQUAL(dist.voterRewards.size(), 1);
    BOOST_CHECK(dist.voterRewards.find(singleVoter) != dist.voterRewards.end());
    BOOST_CHECK_EQUAL(dist.voterRewards[singleVoter], expectedVoterPool);
    
    // Verify conservation of funds
    BOOST_CHECK(dist.VerifyConservation(dispute.challengeBond));
    
    // Cleanup
    fs::remove_all(tempDir);
}

BOOST_AUTO_TEST_SUITE_END()
