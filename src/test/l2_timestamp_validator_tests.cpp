// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_timestamp_validator_tests.cpp
 * @brief Property-based tests for L2 Timestamp Validator
 * 
 * **Feature: cascoin-l2-solution, Property 15: Timestamp Monotonicity**
 * **Validates: Requirements 27.2, 27.3**
 * 
 * Property 15: Timestamp Monotonicity
 * *For any* sequence of L2 blocks, timestamps SHALL be strictly monotonically increasing.
 */

#include <l2/timestamp_validator.h>
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
 * Helper function to generate a random uint256
 */
static uint256 RandomHash()
{
    uint256 hash;
    for (int i = 0; i < 8; ++i) {
        uint32_t val = TestRand32();
        memcpy(hash.begin() + i * 4, &val, 4);
    }
    return hash;
}

/**
 * Helper function to generate a random timestamp within a range
 */
static uint64_t RandomTimestamp(uint64_t min, uint64_t max)
{
    if (max <= min) return min;
    return min + (TestRand64() % (max - min));
}

// Test time for deterministic testing
static uint64_t g_test_time = 1700000000;  // Fixed test time

static uint64_t GetTestTime() {
    return g_test_time;
}

static void SetTestTime(uint64_t time) {
    g_test_time = time;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_timestamp_validator_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_validator)
{
    l2::TimestampValidator validator;
    
    BOOST_CHECK_EQUAL(validator.GetTrackedSequencerCount(), 0u);
    BOOST_CHECK_EQUAL(validator.GetHistorySize(), 0u);
    BOOST_CHECK_EQUAL(validator.GetLastTimestamp(), 0u);
    BOOST_CHECK_EQUAL(validator.GetLastBlockNumber(), 0u);
}

BOOST_AUTO_TEST_CASE(l1_reference_update)
{
    l2::TimestampValidator validator;
    
    uint64_t blockNumber = 1000;
    uint64_t timestamp = 1700000000;
    uint256 blockHash = RandomHash();
    
    validator.UpdateL1Reference(blockNumber, timestamp, blockHash);
    
    l2::L1TimestampReference ref = validator.GetL1Reference();
    BOOST_CHECK_EQUAL(ref.blockNumber, blockNumber);
    BOOST_CHECK_EQUAL(ref.timestamp, timestamp);
    BOOST_CHECK(ref.blockHash == blockHash);
    BOOST_CHECK(ref.IsValid());
}

BOOST_AUTO_TEST_CASE(l1_timestamp_oracle)
{
    l2::TimestampValidator validator;
    
    // No reference set
    BOOST_CHECK_EQUAL(validator.GetL1TimestampOracle(), 0u);
    
    // Set reference
    validator.UpdateL1Reference(1000, 1700000000, RandomHash());
    BOOST_CHECK_EQUAL(validator.GetL1TimestampOracle(), 1700000000u);
}

BOOST_AUTO_TEST_CASE(monotonicity_check)
{
    l2::TimestampValidator validator;
    
    // Increasing timestamps should be valid
    BOOST_CHECK(validator.IsMonotonicallyIncreasing(1000, 999));
    BOOST_CHECK(validator.IsMonotonicallyIncreasing(1001, 1000));
    
    // Equal timestamps should be invalid
    BOOST_CHECK(!validator.IsMonotonicallyIncreasing(1000, 1000));
    
    // Decreasing timestamps should be invalid
    BOOST_CHECK(!validator.IsMonotonicallyIncreasing(999, 1000));
}

BOOST_AUTO_TEST_CASE(minimum_next_timestamp)
{
    l2::TimestampValidator validator;
    
    uint64_t previous = 1700000000;
    uint64_t minNext = validator.GetMinimumNextTimestamp(previous);
    
    BOOST_CHECK_EQUAL(minNext, previous + l2::MIN_TIMESTAMP_INCREMENT);
}

BOOST_AUTO_TEST_CASE(future_timestamp_check)
{
    l2::TimestampValidator validator;
    
    // Set a fixed time source for testing
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    // Timestamp within allowed future window
    BOOST_CHECK(!validator.IsFutureTimestamp(g_test_time + 10));
    BOOST_CHECK(!validator.IsFutureTimestamp(g_test_time + l2::MAX_FUTURE_TIMESTAMP_SECONDS));
    
    // Timestamp beyond allowed future window
    BOOST_CHECK(validator.IsFutureTimestamp(g_test_time + l2::MAX_FUTURE_TIMESTAMP_SECONDS + 1));
    BOOST_CHECK(validator.IsFutureTimestamp(g_test_time + 100));
}

BOOST_AUTO_TEST_CASE(max_allowed_timestamp)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint64_t maxAllowed = validator.GetMaxAllowedTimestamp();
    BOOST_CHECK_EQUAL(maxAllowed, g_test_time + l2::MAX_FUTURE_TIMESTAMP_SECONDS);
}

BOOST_AUTO_TEST_CASE(l1_drift_calculation)
{
    l2::TimestampValidator validator;
    
    uint64_t l1Timestamp = 1700000000;
    validator.UpdateL1Reference(1000, l1Timestamp, RandomHash());
    
    // L2 timestamp ahead of L1
    BOOST_CHECK_EQUAL(validator.CalculateL1Drift(l1Timestamp + 100), 100);
    
    // L2 timestamp behind L1
    BOOST_CHECK_EQUAL(validator.CalculateL1Drift(l1Timestamp - 100), -100);
    
    // L2 timestamp equal to L1
    BOOST_CHECK_EQUAL(validator.CalculateL1Drift(l1Timestamp), 0);
}

BOOST_AUTO_TEST_CASE(l1_drift_within_bounds)
{
    l2::TimestampValidator validator;
    
    uint64_t l1Timestamp = 1700000000;
    validator.UpdateL1Reference(1000, l1Timestamp, RandomHash());
    
    // Within bounds
    BOOST_CHECK(validator.IsWithinL1Drift(l1Timestamp));
    BOOST_CHECK(validator.IsWithinL1Drift(l1Timestamp + l2::MAX_L1_TIMESTAMP_DRIFT));
    BOOST_CHECK(validator.IsWithinL1Drift(l1Timestamp - l2::MAX_L1_TIMESTAMP_DRIFT));
    
    // Outside bounds
    BOOST_CHECK(!validator.IsWithinL1Drift(l1Timestamp + l2::MAX_L1_TIMESTAMP_DRIFT + 1));
    BOOST_CHECK(!validator.IsWithinL1Drift(l1Timestamp - l2::MAX_L1_TIMESTAMP_DRIFT - 1));
}

BOOST_AUTO_TEST_CASE(validate_timestamp_success)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint64_t l1Timestamp = g_test_time - 60;  // L1 is 60 seconds behind
    validator.UpdateL1Reference(1000, l1Timestamp, RandomHash());
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 10;
    uint64_t newTimestamp = g_test_time;
    
    l2::TimestampValidationResult result = validator.ValidateTimestamp(
        newTimestamp, previousTimestamp, sequencer, 100);
    
    BOOST_CHECK(result.valid);
    BOOST_CHECK(result.reason.empty());
    BOOST_CHECK(!result.manipulationDetected);
}

BOOST_AUTO_TEST_CASE(validate_timestamp_not_monotonic)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time;
    uint64_t newTimestamp = g_test_time - 10;  // Going backwards
    
    l2::TimestampValidationResult result = validator.ValidateTimestamp(
        newTimestamp, previousTimestamp, sequencer, 100);
    
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.reason.find("monotonically") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_timestamp_future)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 10;
    uint64_t newTimestamp = g_test_time + 100;  // Too far in future
    
    l2::TimestampValidationResult result = validator.ValidateTimestamp(
        newTimestamp, previousTimestamp, sequencer, 100);
    
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.reason.find("future") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_timestamp_l1_drift_exceeded)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    // Set L1 reference far in the past
    uint64_t l1Timestamp = g_test_time - l2::MAX_L1_TIMESTAMP_DRIFT - 100;
    validator.UpdateL1Reference(1000, l1Timestamp, RandomHash());
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 10;
    uint64_t newTimestamp = g_test_time;
    
    l2::TimestampValidationResult result = validator.ValidateTimestamp(
        newTimestamp, previousTimestamp, sequencer, 100);
    
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.reason.find("drift") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(record_timestamp)
{
    l2::TimestampValidator validator;
    
    uint160 sequencer = RandomAddress();
    
    validator.RecordTimestamp(100, 1700000000, sequencer, 10, 5);
    
    BOOST_CHECK_EQUAL(validator.GetHistorySize(), 1u);
    BOOST_CHECK_EQUAL(validator.GetLastTimestamp(), 1700000000u);
    BOOST_CHECK_EQUAL(validator.GetLastBlockNumber(), 100u);
    
    std::vector<l2::TimestampHistoryEntry> history = validator.GetHistory();
    BOOST_CHECK_EQUAL(history.size(), 1u);
    BOOST_CHECK_EQUAL(history[0].blockNumber, 100u);
    BOOST_CHECK_EQUAL(history[0].timestamp, 1700000000u);
    BOOST_CHECK(history[0].sequencer == sequencer);
}

BOOST_AUTO_TEST_CASE(sequencer_behavior_tracking)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint64_t l1Timestamp = g_test_time;
    validator.UpdateL1Reference(1000, l1Timestamp, RandomHash());
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 10;
    
    // Validate a few timestamps
    for (int i = 0; i < 5; ++i) {
        uint64_t newTimestamp = previousTimestamp + 1;
        validator.ValidateTimestamp(newTimestamp, previousTimestamp, sequencer, 100 + i);
        previousTimestamp = newTimestamp;
    }
    
    auto behavior = validator.GetSequencerBehavior(sequencer);
    BOOST_CHECK(behavior.has_value());
    BOOST_CHECK_EQUAL(behavior->blocksProduced, 5u);
    BOOST_CHECK_EQUAL(behavior->violationCount, 0u);
    BOOST_CHECK(!behavior->flaggedForManipulation);
}

BOOST_AUTO_TEST_CASE(manipulation_detection_consecutive_violations)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 100;
    
    // Cause consecutive violations (non-monotonic timestamps)
    for (uint32_t i = 0; i < l2::MANIPULATION_VIOLATION_THRESHOLD; ++i) {
        // Try to validate a timestamp that goes backwards
        validator.ValidateTimestamp(previousTimestamp - 1, previousTimestamp, sequencer, 100 + i);
    }
    
    // Should be flagged for manipulation
    BOOST_CHECK(validator.DetectManipulation(sequencer));
    
    auto behavior = validator.GetSequencerBehavior(sequencer);
    BOOST_CHECK(behavior.has_value());
    BOOST_CHECK(behavior->flaggedForManipulation);
}

BOOST_AUTO_TEST_CASE(flagged_sequencers)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint160 sequencer1 = RandomAddress();
    uint160 sequencer2 = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 100;
    
    // Flag sequencer1 through violations
    for (uint32_t i = 0; i < l2::MANIPULATION_VIOLATION_THRESHOLD; ++i) {
        validator.ValidateTimestamp(previousTimestamp - 1, previousTimestamp, sequencer1, 100 + i);
    }
    
    // sequencer2 has no violations
    validator.ValidateTimestamp(g_test_time, g_test_time - 10, sequencer2, 200);
    
    std::vector<uint160> flagged = validator.GetFlaggedSequencers();
    BOOST_CHECK_EQUAL(flagged.size(), 1u);
    BOOST_CHECK(flagged[0] == sequencer1);
}

BOOST_AUTO_TEST_CASE(clear_manipulation_flag)
{
    l2::TimestampValidator validator;
    
    SetTestTime(1700000000);
    validator.SetTimeSource(GetTestTime);
    
    uint160 sequencer = RandomAddress();
    uint64_t previousTimestamp = g_test_time - 100;
    
    // Flag sequencer through violations
    for (uint32_t i = 0; i < l2::MANIPULATION_VIOLATION_THRESHOLD; ++i) {
        validator.ValidateTimestamp(previousTimestamp - 1, previousTimestamp, sequencer, 100 + i);
    }
    
    BOOST_CHECK(validator.DetectManipulation(sequencer));
    
    // Clear the flag
    validator.ClearManipulationFlag(sequencer);
    
    BOOST_CHECK(!validator.DetectManipulation(sequencer));
    BOOST_CHECK(validator.GetFlaggedSequencers().empty());
}

BOOST_AUTO_TEST_CASE(history_cleanup)
{
    l2::TimestampValidator validator;
    
    uint160 sequencer = RandomAddress();
    
    // Add more entries than the history size
    for (uint32_t i = 0; i < l2::TIMESTAMP_HISTORY_SIZE + 50; ++i) {
        validator.RecordTimestamp(i, 1700000000 + i, sequencer, 0, 1);
    }
    
    // History should be capped at TIMESTAMP_HISTORY_SIZE
    BOOST_CHECK_EQUAL(validator.GetHistorySize(), l2::TIMESTAMP_HISTORY_SIZE);
    
    // Most recent entries should be kept
    BOOST_CHECK_EQUAL(validator.GetLastBlockNumber(), l2::TIMESTAMP_HISTORY_SIZE + 49);
}

BOOST_AUTO_TEST_CASE(average_l1_drift)
{
    l2::TimestampValidator validator;
    
    uint160 sequencer = RandomAddress();
    
    // Record timestamps with known drifts
    validator.RecordTimestamp(1, 1700000000, sequencer, 10, 1);
    validator.RecordTimestamp(2, 1700000001, sequencer, 20, 1);
    validator.RecordTimestamp(3, 1700000002, sequencer, 30, 1);
    
    // Average should be (10 + 20 + 30) / 3 = 20
    BOOST_CHECK_EQUAL(validator.GetAverageL1Drift(), 20u);
}

BOOST_AUTO_TEST_CASE(clear_validator)
{
    l2::TimestampValidator validator;
    
    // Add some data
    validator.UpdateL1Reference(1000, 1700000000, RandomHash());
    validator.RecordTimestamp(100, 1700000000, RandomAddress(), 10, 5);
    
    BOOST_CHECK(validator.GetL1Reference().IsValid());
    BOOST_CHECK_EQUAL(validator.GetHistorySize(), 1u);
    
    // Clear
    validator.Clear();
    
    BOOST_CHECK(!validator.GetL1Reference().IsValid());
    BOOST_CHECK_EQUAL(validator.GetHistorySize(), 0u);
    BOOST_CHECK_EQUAL(validator.GetTrackedSequencerCount(), 0u);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 15: Timestamp Monotonicity**
 * 
 * *For any* sequence of L2 blocks, timestamps SHALL be strictly monotonically increasing.
 * 
 * **Validates: Requirements 27.2, 27.3**
 */
BOOST_AUTO_TEST_CASE(property_timestamp_monotonicity)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::TimestampValidator validator;
        
        // Set a fixed time source
        uint64_t baseTime = 1700000000 + (TestRand32() % 1000000);
        SetTestTime(baseTime + 1000);  // Current time is ahead
        validator.SetTimeSource(GetTestTime);
        
        // Set L1 reference
        validator.UpdateL1Reference(1000, baseTime, RandomHash());
        
        uint160 sequencer = RandomAddress();
        
        // Generate a sequence of valid timestamps
        uint64_t previousTimestamp = baseTime;
        std::vector<uint64_t> timestamps;
        timestamps.push_back(previousTimestamp);
        
        uint32_t numBlocks = 10 + (TestRand32() % 20);
        
        for (uint32_t i = 0; i < numBlocks; ++i) {
            // Generate a valid next timestamp (monotonically increasing)
            uint64_t increment = 1 + (TestRand32() % 10);
            uint64_t newTimestamp = previousTimestamp + increment;
            
            // Validate the timestamp
            l2::TimestampValidationResult result = validator.ValidateTimestamp(
                newTimestamp, previousTimestamp, sequencer, i);
            
            BOOST_CHECK_MESSAGE(result.valid,
                "Valid monotonic timestamp rejected at iteration " << iteration <<
                ", block " << i << ": " << result.reason);
            
            timestamps.push_back(newTimestamp);
            previousTimestamp = newTimestamp;
        }
        
        // Verify all timestamps are strictly increasing
        for (size_t i = 1; i < timestamps.size(); ++i) {
            BOOST_CHECK_MESSAGE(timestamps[i] > timestamps[i-1],
                "Timestamps not monotonically increasing at iteration " << iteration <<
                ", index " << i);
        }
    }
}

/**
 * **Property: Non-Monotonic Timestamps Are Rejected**
 * 
 * *For any* timestamp that is not strictly greater than the previous timestamp,
 * the validator SHALL reject it.
 * 
 * **Validates: Requirements 27.2**
 */
BOOST_AUTO_TEST_CASE(property_non_monotonic_rejected)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::TimestampValidator validator;
        
        uint64_t baseTime = 1700000000 + (TestRand32() % 1000000);
        SetTestTime(baseTime + 1000);
        validator.SetTimeSource(GetTestTime);
        
        uint160 sequencer = RandomAddress();
        uint64_t previousTimestamp = baseTime + (TestRand32() % 100);
        
        // Generate a non-monotonic timestamp (equal or less)
        uint64_t newTimestamp;
        if (TestRand32() % 2 == 0) {
            // Equal timestamp
            newTimestamp = previousTimestamp;
        } else {
            // Decreasing timestamp
            uint64_t decrease = 1 + (TestRand32() % 100);
            newTimestamp = (previousTimestamp > decrease) ? previousTimestamp - decrease : 0;
        }
        
        l2::TimestampValidationResult result = validator.ValidateTimestamp(
            newTimestamp, previousTimestamp, sequencer, 100);
        
        BOOST_CHECK_MESSAGE(!result.valid,
            "Non-monotonic timestamp accepted at iteration " << iteration <<
            " (prev=" << previousTimestamp << ", new=" << newTimestamp << ")");
    }
}

/**
 * **Property: Future Timestamps Are Rejected**
 * 
 * *For any* timestamp more than MAX_FUTURE_TIMESTAMP_SECONDS ahead of current time,
 * the validator SHALL reject it.
 * 
 * **Validates: Requirements 27.3**
 */
BOOST_AUTO_TEST_CASE(property_future_timestamps_rejected)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::TimestampValidator validator;
        
        uint64_t currentTime = 1700000000 + (TestRand32() % 1000000);
        SetTestTime(currentTime);
        validator.SetTimeSource(GetTestTime);
        
        uint160 sequencer = RandomAddress();
        uint64_t previousTimestamp = currentTime - 100;
        
        // Generate a future timestamp beyond the allowed window
        uint64_t futureOffset = l2::MAX_FUTURE_TIMESTAMP_SECONDS + 1 + (TestRand32() % 1000);
        uint64_t futureTimestamp = currentTime + futureOffset;
        
        l2::TimestampValidationResult result = validator.ValidateTimestamp(
            futureTimestamp, previousTimestamp, sequencer, 100);
        
        BOOST_CHECK_MESSAGE(!result.valid,
            "Future timestamp accepted at iteration " << iteration <<
            " (current=" << currentTime << ", timestamp=" << futureTimestamp << ")");
        BOOST_CHECK_MESSAGE(result.reason.find("future") != std::string::npos,
            "Wrong rejection reason at iteration " << iteration << ": " << result.reason);
    }
}

/**
 * **Property: L1 Drift Bounds**
 * 
 * *For any* timestamp within MAX_L1_TIMESTAMP_DRIFT of the L1 reference,
 * the validator SHALL accept it (assuming other conditions are met).
 * 
 * **Validates: Requirements 27.1**
 */
BOOST_AUTO_TEST_CASE(property_l1_drift_bounds)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::TimestampValidator validator;
        
        uint64_t l1Timestamp = 1700000000 + (TestRand32() % 1000000);
        uint64_t currentTime = l1Timestamp + 100;  // Current time slightly ahead
        SetTestTime(currentTime);
        validator.SetTimeSource(GetTestTime);
        validator.UpdateL1Reference(1000, l1Timestamp, RandomHash());
        
        uint160 sequencer = RandomAddress();
        uint64_t previousTimestamp = l1Timestamp - 200;
        
        // Generate a timestamp within L1 drift bounds
        int64_t drift = (TestRand32() % (2 * l2::MAX_L1_TIMESTAMP_DRIFT + 1)) - l2::MAX_L1_TIMESTAMP_DRIFT;
        uint64_t newTimestamp = l1Timestamp + drift;
        
        // Ensure monotonicity
        if (newTimestamp <= previousTimestamp) {
            newTimestamp = previousTimestamp + 1;
        }
        
        // Ensure not in future
        if (newTimestamp > currentTime + l2::MAX_FUTURE_TIMESTAMP_SECONDS) {
            newTimestamp = currentTime;
        }
        
        l2::TimestampValidationResult result = validator.ValidateTimestamp(
            newTimestamp, previousTimestamp, sequencer, 100);
        
        // Calculate actual drift
        int64_t actualDrift = static_cast<int64_t>(newTimestamp) - static_cast<int64_t>(l1Timestamp);
        uint64_t absDrift = static_cast<uint64_t>(actualDrift < 0 ? -actualDrift : actualDrift);
        
        if (absDrift <= l2::MAX_L1_TIMESTAMP_DRIFT) {
            BOOST_CHECK_MESSAGE(result.valid,
                "Valid L1 drift rejected at iteration " << iteration <<
                " (drift=" << actualDrift << "): " << result.reason);
        }
    }
}

/**
 * **Property: Manipulation Detection After Violations**
 * 
 * *For any* sequencer with MANIPULATION_VIOLATION_THRESHOLD consecutive violations,
 * the validator SHALL flag them for manipulation.
 * 
 * **Validates: Requirements 27.4, 27.6**
 */
BOOST_AUTO_TEST_CASE(property_manipulation_detection)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::TimestampValidator validator;
        
        uint64_t currentTime = 1700000000 + (TestRand32() % 1000000);
        SetTestTime(currentTime);
        validator.SetTimeSource(GetTestTime);
        
        uint160 sequencer = RandomAddress();
        uint64_t previousTimestamp = currentTime - 100;
        
        // Cause exactly MANIPULATION_VIOLATION_THRESHOLD consecutive violations
        for (uint32_t i = 0; i < l2::MANIPULATION_VIOLATION_THRESHOLD; ++i) {
            // Non-monotonic timestamp (violation)
            validator.ValidateTimestamp(previousTimestamp - 1, previousTimestamp, sequencer, i);
        }
        
        // Should be flagged for manipulation
        BOOST_CHECK_MESSAGE(validator.DetectManipulation(sequencer),
            "Sequencer not flagged after " << l2::MANIPULATION_VIOLATION_THRESHOLD <<
            " violations at iteration " << iteration);
        
        auto behavior = validator.GetSequencerBehavior(sequencer);
        BOOST_CHECK_MESSAGE(behavior.has_value() && behavior->flaggedForManipulation,
            "Sequencer behavior not flagged at iteration " << iteration);
    }
}

/**
 * **Property: Valid Timestamps Reset Consecutive Violations**
 * 
 * *For any* sequencer, a valid timestamp SHALL reset the consecutive violation counter.
 * 
 * **Validates: Requirements 27.4**
 */
BOOST_AUTO_TEST_CASE(property_valid_resets_violations)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::TimestampValidator validator;
        
        uint64_t currentTime = 1700000000 + (TestRand32() % 1000000);
        SetTestTime(currentTime);
        validator.SetTimeSource(GetTestTime);
        validator.UpdateL1Reference(1000, currentTime - 60, RandomHash());
        
        uint160 sequencer = RandomAddress();
        uint64_t previousTimestamp = currentTime - 100;
        
        // Cause some violations (but not enough to flag)
        uint32_t numViolations = TestRand32() % (l2::MANIPULATION_VIOLATION_THRESHOLD - 1);
        for (uint32_t i = 0; i < numViolations; ++i) {
            validator.ValidateTimestamp(previousTimestamp - 1, previousTimestamp, sequencer, i);
        }
        
        // Now submit a valid timestamp
        uint64_t validTimestamp = previousTimestamp + 1;
        l2::TimestampValidationResult result = validator.ValidateTimestamp(
            validTimestamp, previousTimestamp, sequencer, numViolations);
        
        BOOST_CHECK_MESSAGE(result.valid,
            "Valid timestamp rejected at iteration " << iteration);
        
        auto behavior = validator.GetSequencerBehavior(sequencer);
        BOOST_CHECK_MESSAGE(behavior.has_value() && behavior->consecutiveViolations == 0,
            "Consecutive violations not reset at iteration " << iteration);
    }
}

/**
 * **Property: History Size Bounded**
 * 
 * *For any* number of recorded timestamps, the history size SHALL not exceed
 * TIMESTAMP_HISTORY_SIZE.
 * 
 * **Validates: Requirements 27.6**
 */
BOOST_AUTO_TEST_CASE(property_history_bounded)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::TimestampValidator validator;
        
        uint160 sequencer = RandomAddress();
        
        // Record many timestamps
        uint32_t numRecords = l2::TIMESTAMP_HISTORY_SIZE + (TestRand32() % 100);
        for (uint32_t i = 0; i < numRecords; ++i) {
            validator.RecordTimestamp(i, 1700000000 + i, sequencer, 0, 1);
        }
        
        BOOST_CHECK_MESSAGE(validator.GetHistorySize() <= l2::TIMESTAMP_HISTORY_SIZE,
            "History size exceeded at iteration " << iteration <<
            " (size=" << validator.GetHistorySize() << ")");
    }
}

/**
 * **Property: Minimum Timestamp Increment**
 * 
 * *For any* previous timestamp, the minimum next timestamp SHALL be
 * previous + MIN_TIMESTAMP_INCREMENT.
 * 
 * **Validates: Requirements 27.2**
 */
BOOST_AUTO_TEST_CASE(property_minimum_increment)
{
    l2::TimestampValidator validator;
    
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint64_t previousTimestamp = TestRand64() % 2000000000;
        uint64_t minNext = validator.GetMinimumNextTimestamp(previousTimestamp);
        
        BOOST_CHECK_MESSAGE(minNext == previousTimestamp + l2::MIN_TIMESTAMP_INCREMENT,
            "Minimum increment wrong at iteration " << iteration);
        
        // Verify that minNext is valid and minNext-1 is not
        BOOST_CHECK_MESSAGE(validator.IsMonotonicallyIncreasing(minNext, previousTimestamp),
            "Minimum next timestamp not valid at iteration " << iteration);
        
        if (minNext > 0) {
            BOOST_CHECK_MESSAGE(!validator.IsMonotonicallyIncreasing(minNext - 1, previousTimestamp),
                "Below minimum timestamp accepted at iteration " << iteration);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
