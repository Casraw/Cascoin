// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_reputation_tests.cpp
 * @brief Property-based tests for L2 Reputation Manager
 * 
 * **Feature: cascoin-l2-solution, Property 10: Reputation Aggregation Consistency**
 * **Validates: Requirements 10.3, 10.5**
 * 
 * Property 10: Reputation Aggregation Consistency
 * *For any* address, the aggregated reputation score SHALL be a deterministic
 * function of L1 and L2 reputation components.
 */

#include <l2/l2_reputation.h>
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

/**
 * Helper function to generate a random address
 */
static uint160 RandomAddress()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        uint32_t val = TestRand32();
        memcpy(addr.begin() + i * 4, &val, 4);
    }
    return addr;
}

/**
 * Helper function to generate a random L2 activity
 */
static l2::L2Activity RandomActivity(uint64_t blockNumber)
{
    l2::L2Activity::Type types[] = {
        l2::L2Activity::Type::TRANSACTION,
        l2::L2Activity::Type::CONTRACT_CALL,
        l2::L2Activity::Type::CONTRACT_DEPLOY,
        l2::L2Activity::Type::DEPOSIT,
        l2::L2Activity::Type::WITHDRAWAL
    };
    
    l2::L2Activity::Type type = types[TestRand32() % 5];
    CAmount value = TestRand64() % (1000 * COIN);
    uint64_t gasUsed = TestRand64() % 1000000;
    bool success = (TestRand32() % 10) < 9;  // 90% success rate
    
    return l2::L2Activity(type, value, gasUsed, blockNumber, success);
}

/**
 * Helper function to generate random reputation data
 */
static l2::L2ReputationData RandomReputationData()
{
    l2::L2ReputationData data;
    data.l1HatScore = TestRand32() % 101;  // 0-100
    data.l2BehaviorScore = TestRand32() % 101;
    data.l2EconomicScore = TestRand32() % 101;
    data.l2TransactionCount = TestRand64() % 10000;
    data.l2VolumeTraded = TestRand64() % (100000 * COIN);
    data.successfulContractCalls = TestRand64() % 1000;
    data.failedTransactions = TestRand64() % 100;
    data.lastL2Activity = TestRand64() % 1000000;
    data.lastL1Sync = TestRand64() % 1000000;
    data.firstSeenOnL2 = TestRand64() % 1000000;
    data.flaggedForReview = (TestRand32() % 20) == 0;  // 5% flagged
    return data;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_reputation_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_reputation_manager)
{
    l2::L2ReputationManager manager(1);
    
    BOOST_CHECK_EQUAL(manager.GetAddressCount(), 0u);
    BOOST_CHECK_EQUAL(manager.GetChainId(), 1u);
    
    uint160 addr = RandomAddress();
    BOOST_CHECK(!manager.HasReputationData(addr));
    BOOST_CHECK(!manager.HasL1Reputation(addr));
    BOOST_CHECK_EQUAL(manager.GetAggregatedReputation(addr), 0u);
    BOOST_CHECK_EQUAL(manager.GetL1HatScore(addr), 0u);
}

BOOST_AUTO_TEST_CASE(import_l1_reputation)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    uint32_t hatScore = 75;
    uint64_t blockNumber = 1000;
    
    bool imported = manager.ImportL1Reputation(addr, hatScore, blockNumber);
    BOOST_CHECK(imported);
    
    BOOST_CHECK(manager.HasL1Reputation(addr));
    BOOST_CHECK(manager.HasReputationData(addr));
    BOOST_CHECK_EQUAL(manager.GetL1HatScore(addr), hatScore);
    BOOST_CHECK_EQUAL(manager.GetAddressCount(), 1u);
    
    // Aggregated score should initially equal L1 score (no L2 activity)
    BOOST_CHECK_EQUAL(manager.GetAggregatedReputation(addr), hatScore);
}

BOOST_AUTO_TEST_CASE(import_invalid_l1_reputation)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    
    // Score > 100 should fail
    bool imported = manager.ImportL1Reputation(addr, 101, 1000);
    BOOST_CHECK(!imported);
    BOOST_CHECK(!manager.HasReputationData(addr));
}

BOOST_AUTO_TEST_CASE(update_l2_reputation)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    
    // Import L1 reputation first
    manager.ImportL1Reputation(addr, 70, 1000);
    
    // Record some L2 activity
    for (int i = 0; i < 20; ++i) {
        l2::L2Activity activity = RandomActivity(1000 + i);
        manager.UpdateL2Reputation(addr, activity);
    }
    
    l2::L2ReputationData data = manager.GetReputationData(addr);
    BOOST_CHECK(data.l2TransactionCount >= 20);
    BOOST_CHECK(data.l2VolumeTraded > 0);
}

BOOST_AUTO_TEST_CASE(record_transaction)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    
    manager.RecordTransaction(addr, 100 * COIN, 21000, 1000);
    
    l2::L2ReputationData data = manager.GetReputationData(addr);
    BOOST_CHECK_EQUAL(data.l2TransactionCount, 1u);
    BOOST_CHECK_EQUAL(data.l2VolumeTraded, 100 * COIN);
    BOOST_CHECK_EQUAL(data.lastL2Activity, 1000u);
}

BOOST_AUTO_TEST_CASE(record_failed_transaction)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    
    manager.RecordTransaction(addr, 100 * COIN, 21000, 1000);
    manager.RecordFailedTransaction(addr, 1001);
    
    l2::L2ReputationData data = manager.GetReputationData(addr);
    BOOST_CHECK_EQUAL(data.l2TransactionCount, 2u);
    BOOST_CHECK_EQUAL(data.failedTransactions, 1u);
    BOOST_CHECK_EQUAL(data.GetSuccessRate(), 50u);  // 1 success, 1 failure
}

BOOST_AUTO_TEST_CASE(record_contract_call)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    
    manager.RecordContractCall(addr, 50 * COIN, 100000, 1000, true);
    
    l2::L2ReputationData data = manager.GetReputationData(addr);
    BOOST_CHECK_EQUAL(data.l2TransactionCount, 1u);
    BOOST_CHECK_EQUAL(data.successfulContractCalls, 1u);
    BOOST_CHECK_EQUAL(data.l2VolumeTraded, 50 * COIN);
}

BOOST_AUTO_TEST_CASE(fast_withdrawal_qualification)
{
    l2::L2ReputationManager manager(1);
    
    uint160 highRepAddr = RandomAddress();
    uint160 lowRepAddr = RandomAddress();
    
    manager.ImportL1Reputation(highRepAddr, 85, 1000);
    manager.ImportL1Reputation(lowRepAddr, 60, 1000);
    
    BOOST_CHECK(manager.QualifiesForFastWithdrawal(highRepAddr));
    BOOST_CHECK(!manager.QualifiesForFastWithdrawal(lowRepAddr));
}

BOOST_AUTO_TEST_CASE(gas_discount_calculation)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr1 = RandomAddress();
    uint160 addr2 = RandomAddress();
    uint160 addr3 = RandomAddress();
    
    manager.ImportL1Reputation(addr1, 100, 1000);  // Max score
    manager.ImportL1Reputation(addr2, 70, 1000);   // Threshold
    manager.ImportL1Reputation(addr3, 50, 1000);   // Below threshold
    
    // Max score should get max discount (50%)
    BOOST_CHECK_EQUAL(manager.GetGasDiscount(addr1), 50u);
    
    // At threshold should get 0%
    BOOST_CHECK_EQUAL(manager.GetGasDiscount(addr2), 0u);
    
    // Below threshold should get 0%
    BOOST_CHECK_EQUAL(manager.GetGasDiscount(addr3), 0u);
}

BOOST_AUTO_TEST_CASE(instant_soft_finality)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr1 = RandomAddress();
    uint160 addr2 = RandomAddress();
    uint160 addr3 = RandomAddress();
    
    manager.ImportL1Reputation(addr1, 85, 1000);  // > 80
    manager.ImportL1Reputation(addr2, 80, 1000);  // = 80 (not > 80)
    manager.ImportL1Reputation(addr3, 75, 1000);  // < 80
    
    BOOST_CHECK(manager.HasInstantSoftFinality(addr1));
    BOOST_CHECK(!manager.HasInstantSoftFinality(addr2));
    BOOST_CHECK(!manager.HasInstantSoftFinality(addr3));
}

BOOST_AUTO_TEST_CASE(rate_limit_multiplier)
{
    l2::L2ReputationManager manager(1);
    
    // Test different score ranges
    struct TestCase {
        uint32_t score;
        uint32_t expectedMultiplier;
    };
    
    TestCase cases[] = {
        {0, 1}, {49, 1},
        {50, 2}, {69, 2},
        {70, 5}, {79, 5},
        {80, 7}, {89, 7},
        {90, 10}, {100, 10}
    };
    
    for (const auto& tc : cases) {
        uint160 addr = RandomAddress();
        manager.ImportL1Reputation(addr, tc.score, 1000);
        BOOST_CHECK_EQUAL(manager.GetRateLimitMultiplier(addr), tc.expectedMultiplier);
    }
}

BOOST_AUTO_TEST_CASE(reputation_benefits_calculation)
{
    // Test static benefits calculation
    l2::ReputationBenefits benefits90 = l2::L2ReputationManager::CalculateBenefits(90);
    BOOST_CHECK(benefits90.qualifiesForFastWithdrawal);
    BOOST_CHECK(benefits90.instantSoftFinality);
    BOOST_CHECK_EQUAL(benefits90.rateLimitMultiplier, 10u);
    BOOST_CHECK_EQUAL(benefits90.priorityLevel, 9u);
    
    l2::ReputationBenefits benefits50 = l2::L2ReputationManager::CalculateBenefits(50);
    BOOST_CHECK(!benefits50.qualifiesForFastWithdrawal);
    BOOST_CHECK(!benefits50.instantSoftFinality);
    BOOST_CHECK_EQUAL(benefits50.rateLimitMultiplier, 2u);
    BOOST_CHECK_EQUAL(benefits50.priorityLevel, 5u);
}

BOOST_AUTO_TEST_CASE(flag_for_review)
{
    l2::L2ReputationManager manager(1);
    
    uint160 addr = RandomAddress();
    manager.ImportL1Reputation(addr, 90, 1000);
    
    // Before flagging
    BOOST_CHECK_EQUAL(manager.GetAggregatedReputation(addr), 90u);
    BOOST_CHECK(!manager.DetectReputationGaming(addr));
    
    // Flag the address
    manager.FlagForReview(addr, "Test reason");
    
    // After flagging, score should be capped at 50
    l2::L2ReputationData data = manager.GetReputationData(addr);
    BOOST_CHECK(data.flaggedForReview);
    BOOST_CHECK(manager.DetectReputationGaming(addr));
    
    // Clear flag
    manager.ClearFlag(addr);
    data = manager.GetReputationData(addr);
    BOOST_CHECK(!data.flaggedForReview);
}

BOOST_AUTO_TEST_CASE(clear_reputation_data)
{
    l2::L2ReputationManager manager(1);
    
    // Add some addresses
    for (int i = 0; i < 5; ++i) {
        manager.ImportL1Reputation(RandomAddress(), 70 + i, 1000);
    }
    
    BOOST_CHECK_EQUAL(manager.GetAddressCount(), 5u);
    
    manager.Clear();
    
    BOOST_CHECK_EQUAL(manager.GetAddressCount(), 0u);
}

// ============================================================================
// Serialization Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(reputation_data_serialization)
{
    l2::L2ReputationData original = RandomReputationData();
    
    std::vector<unsigned char> serialized = original.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    l2::L2ReputationData restored;
    bool success = restored.Deserialize(serialized);
    
    BOOST_CHECK(success);
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(reputation_benefits_serialization)
{
    l2::ReputationBenefits original = l2::L2ReputationManager::CalculateBenefits(85);
    
    std::vector<unsigned char> serialized = original.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    l2::ReputationBenefits restored;
    bool success = restored.Deserialize(serialized);
    
    BOOST_CHECK(success);
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(l2_activity_serialization)
{
    l2::L2Activity original(l2::L2Activity::Type::CONTRACT_CALL, 100 * COIN, 50000, 1000, true);
    
    CDataStream ss(SER_DISK, 0);
    ss << original;
    
    l2::L2Activity restored;
    ss >> restored;
    
    BOOST_CHECK(original.type == restored.type);
    BOOST_CHECK_EQUAL(original.value, restored.value);
    BOOST_CHECK_EQUAL(original.gasUsed, restored.gasUsed);
    BOOST_CHECK_EQUAL(original.blockNumber, restored.blockNumber);
    BOOST_CHECK_EQUAL(original.success, restored.success);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 10: Reputation Aggregation Consistency**
 * 
 * *For any* address, the aggregated reputation score SHALL be a deterministic
 * function of L1 and L2 reputation components.
 * 
 * **Validates: Requirements 10.3, 10.5**
 */
BOOST_AUTO_TEST_CASE(property_reputation_aggregation_consistency)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate random L1 and L2 scores
        uint32_t l1Score = TestRand32() % 101;
        uint32_t l2Behavior = TestRand32() % 101;
        uint32_t l2Economic = TestRand32() % 101;
        
        // Calculate aggregated score twice
        uint32_t score1 = l2::L2ReputationManager::CalculateAggregatedScore(l1Score, l2Behavior, l2Economic);
        uint32_t score2 = l2::L2ReputationManager::CalculateAggregatedScore(l1Score, l2Behavior, l2Economic);
        
        // Scores should be identical (deterministic)
        BOOST_CHECK_MESSAGE(score1 == score2,
            "Aggregation not deterministic for iteration " << iteration <<
            " (l1=" << l1Score << ", l2b=" << l2Behavior << ", l2e=" << l2Economic << ")");
        
        // Score should be in valid range
        BOOST_CHECK_MESSAGE(score1 <= 100,
            "Aggregated score out of range for iteration " << iteration);
    }
}

/**
 * **Property 10: Reputation Aggregation Consistency (Manager Level)**
 * 
 * *For any* address with the same L1 import and L2 activity sequence,
 * two separate managers SHALL produce identical aggregated scores.
 * 
 * **Validates: Requirements 10.3**
 */
BOOST_AUTO_TEST_CASE(property_manager_aggregation_consistency)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        uint160 addr = RandomAddress();
        uint32_t l1Score = TestRand32() % 101;
        uint64_t l1Block = TestRand64() % 1000000;
        
        // Generate random activity sequence
        int numActivities = 5 + (TestRand32() % 20);
        std::vector<l2::L2Activity> activities;
        for (int i = 0; i < numActivities; ++i) {
            activities.push_back(RandomActivity(l1Block + i + 1));
        }
        
        // Apply to first manager
        l2::L2ReputationManager manager1(1);
        manager1.ImportL1Reputation(addr, l1Score, l1Block);
        for (const auto& activity : activities) {
            manager1.UpdateL2Reputation(addr, activity);
        }
        uint32_t score1 = manager1.GetAggregatedReputation(addr);
        
        // Apply to second manager
        l2::L2ReputationManager manager2(1);
        manager2.ImportL1Reputation(addr, l1Score, l1Block);
        for (const auto& activity : activities) {
            manager2.UpdateL2Reputation(addr, activity);
        }
        uint32_t score2 = manager2.GetAggregatedReputation(addr);
        
        // Scores should be identical
        BOOST_CHECK_MESSAGE(score1 == score2,
            "Manager aggregation not consistent for iteration " << iteration);
    }
}

/**
 * **Property: Reputation Data Serialization Round-Trip**
 * 
 * *For any* reputation data, serializing and deserializing SHALL produce
 * identical data.
 * 
 * **Validates: Requirements 10.1**
 */
BOOST_AUTO_TEST_CASE(property_reputation_data_roundtrip)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::L2ReputationData original = RandomReputationData();
        
        std::vector<unsigned char> serialized = original.Serialize();
        
        l2::L2ReputationData restored;
        bool success = restored.Deserialize(serialized);
        
        BOOST_CHECK_MESSAGE(success,
            "Deserialization failed for iteration " << iteration);
        BOOST_CHECK_MESSAGE(original == restored,
            "Round-trip failed for iteration " << iteration);
    }
}

/**
 * **Property: Benefits Monotonicity**
 * 
 * *For any* two reputation scores where score1 > score2, the benefits
 * for score1 SHALL be at least as good as benefits for score2.
 * 
 * **Validates: Requirements 6.1, 6.2, 18.5**
 */
BOOST_AUTO_TEST_CASE(property_benefits_monotonicity)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t score1 = TestRand32() % 101;
        uint32_t score2 = TestRand32() % 101;
        
        if (score1 < score2) {
            std::swap(score1, score2);
        }
        
        l2::ReputationBenefits benefits1 = l2::L2ReputationManager::CalculateBenefits(score1);
        l2::ReputationBenefits benefits2 = l2::L2ReputationManager::CalculateBenefits(score2);
        
        // Higher score should have >= benefits
        BOOST_CHECK_MESSAGE(benefits1.gasDiscountPercent >= benefits2.gasDiscountPercent,
            "Gas discount not monotonic for iteration " << iteration);
        BOOST_CHECK_MESSAGE(benefits1.rateLimitMultiplier >= benefits2.rateLimitMultiplier,
            "Rate limit not monotonic for iteration " << iteration);
        BOOST_CHECK_MESSAGE(benefits1.priorityLevel >= benefits2.priorityLevel,
            "Priority not monotonic for iteration " << iteration);
        BOOST_CHECK_MESSAGE(benefits1.challengePeriodSeconds <= benefits2.challengePeriodSeconds,
            "Challenge period not monotonic for iteration " << iteration);
        BOOST_CHECK_MESSAGE(benefits1.maxWithdrawalWithoutVerification >= benefits2.maxWithdrawalWithoutVerification,
            "Max withdrawal not monotonic for iteration " << iteration);
        
        // If score1 qualifies for fast withdrawal, score2 might not (but not vice versa)
        if (benefits2.qualifiesForFastWithdrawal) {
            BOOST_CHECK_MESSAGE(benefits1.qualifiesForFastWithdrawal,
                "Fast withdrawal qualification not monotonic for iteration " << iteration);
        }
        
        // Same for instant finality
        if (benefits2.instantSoftFinality) {
            BOOST_CHECK_MESSAGE(benefits1.instantSoftFinality,
                "Instant finality not monotonic for iteration " << iteration);
        }
    }
}

/**
 * **Property: Aggregation Weight Sum**
 * 
 * The aggregation weights (L1_REPUTATION_WEIGHT + L2_BEHAVIOR_WEIGHT + L2_ECONOMIC_WEIGHT)
 * SHALL sum to 100 for proper weighted average calculation.
 * 
 * **Validates: Requirements 10.3**
 */
BOOST_AUTO_TEST_CASE(property_aggregation_weights_sum)
{
    uint32_t totalWeight = l2::L1_REPUTATION_WEIGHT + l2::L2_BEHAVIOR_WEIGHT + l2::L2_ECONOMIC_WEIGHT;
    BOOST_CHECK_EQUAL(totalWeight, 100u);
}

/**
 * **Property: Score Bounds**
 * 
 * *For any* input scores, the aggregated score SHALL be in range [0, 100].
 * 
 * **Validates: Requirements 10.3**
 */
BOOST_AUTO_TEST_CASE(property_score_bounds)
{
    // Test boundary cases
    BOOST_CHECK(l2::L2ReputationManager::CalculateAggregatedScore(0, 0, 0) == 0);
    BOOST_CHECK(l2::L2ReputationManager::CalculateAggregatedScore(100, 100, 100) == 100);
    
    // Test random cases
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t l1 = TestRand32() % 101;
        uint32_t l2b = TestRand32() % 101;
        uint32_t l2e = TestRand32() % 101;
        
        uint32_t score = l2::L2ReputationManager::CalculateAggregatedScore(l1, l2b, l2e);
        
        BOOST_CHECK_MESSAGE(score <= 100,
            "Score out of bounds for iteration " << iteration);
    }
}

/**
 * **Property: Gaming Detection Consistency**
 * 
 * *For any* address, gaming detection SHALL be deterministic based on
 * the current reputation data.
 * 
 * **Validates: Requirements 10.5**
 */
BOOST_AUTO_TEST_CASE(property_gaming_detection_consistency)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::L2ReputationManager manager(1);
        
        uint160 addr = RandomAddress();
        manager.ImportL1Reputation(addr, TestRand32() % 101, 1000);
        
        // Add some activity
        int numActivities = TestRand32() % 50;
        for (int i = 0; i < numActivities; ++i) {
            manager.UpdateL2Reputation(addr, RandomActivity(1000 + i));
        }
        
        // Check gaming detection twice
        bool gaming1 = manager.DetectReputationGaming(addr);
        bool gaming2 = manager.DetectReputationGaming(addr);
        
        BOOST_CHECK_MESSAGE(gaming1 == gaming2,
            "Gaming detection not consistent for iteration " << iteration);
    }
}

/**
 * **Property: L1 Sync Request Consistency**
 * 
 * *For any* address, generating a sync request SHALL produce consistent
 * data reflecting the current reputation state.
 * 
 * **Validates: Requirements 10.4**
 */
BOOST_AUTO_TEST_CASE(property_l1_sync_consistency)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::L2ReputationManager manager(1);
        
        uint160 addr = RandomAddress();
        uint32_t l1Score = TestRand32() % 101;
        manager.ImportL1Reputation(addr, l1Score, 1000);
        
        // Add some activity
        int numActivities = 10 + (TestRand32() % 20);
        for (int i = 0; i < numActivities; ++i) {
            manager.UpdateL2Reputation(addr, RandomActivity(1000 + i));
        }
        
        // Generate sync request
        l2::ReputationSyncRequest request = manager.SyncToL1(addr);
        
        // Verify request matches current state
        l2::L2ReputationData data = manager.GetReputationData(addr);
        
        BOOST_CHECK_MESSAGE(request.address == addr,
            "Sync request address mismatch for iteration " << iteration);
        BOOST_CHECK_MESSAGE(request.l2AggregatedScore == data.aggregatedScore,
            "Sync request score mismatch for iteration " << iteration);
        BOOST_CHECK_MESSAGE(request.l2TransactionCount == data.l2TransactionCount,
            "Sync request tx count mismatch for iteration " << iteration);
        BOOST_CHECK_MESSAGE(request.l2VolumeTraded == data.l2VolumeTraded,
            "Sync request volume mismatch for iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
