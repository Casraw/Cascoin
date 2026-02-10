// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_forced_inclusion_tests.cpp
 * @brief Property-based tests for L2 Forced Inclusion System
 * 
 * **Feature: cascoin-l2-solution, Property 19: Forced Inclusion Guarantee**
 * **Validates: Requirements 17.2, 17.3**
 * 
 * Property 19: Forced Inclusion Guarantee
 * *For any* transaction submitted via L1 forced inclusion, it SHALL be
 * included in an L2 block within 24 hours or the sequencer SHALL be slashed.
 */

#include <l2/forced_inclusion.h>
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
 * Helper function to generate random transaction data
 */
static std::vector<unsigned char> RandomData(size_t maxSize = 100)
{
    size_t size = TestRand32() % maxSize;
    std::vector<unsigned char> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<unsigned char>(TestRand32() % 256);
    }
    return data;
}

/**
 * Helper function to create a valid forced inclusion request
 */
static std::optional<l2::ForcedInclusionRequest> CreateForcedRequest(
    l2::ForcedInclusionSystem& system,
    const uint160& submitter,
    uint64_t timestamp)
{
    return system.SubmitForcedTransaction(
        TestRand256(),                          // l1TxHash
        TestRand64() % 1000000,                 // l1BlockNumber
        submitter,                              // submitter
        RandomAddress160(),                     // target
        (TestRand64() % 1000 + 1) * COIN,       // value
        RandomData(),                           // data
        21000 + (TestRand64() % 100000),        // gasLimit
        TestRand256(),                          // maxGasPrice
        TestRand64() % 1000,                    // nonce
        l2::FORCED_INCLUSION_BOND,              // bondAmount
        timestamp);                             // currentTime
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_forced_inclusion_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_system_has_no_requests)
{
    l2::ForcedInclusionSystem system(1);
    
    BOOST_CHECK_EQUAL(system.GetPendingRequestCount(), 0u);
    BOOST_CHECK_EQUAL(system.GetTotalRequestCount(), 0u);
    BOOST_CHECK_EQUAL(system.GetTotalBondsHeld(), 0);
    BOOST_CHECK_EQUAL(system.GetTotalSlashed(), 0);
}

BOOST_AUTO_TEST_CASE(submit_forced_transaction_success)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    auto request = CreateForcedRequest(system, submitter, timestamp);
    
    BOOST_REQUIRE(request.has_value());
    BOOST_CHECK(request->status == l2::ForcedInclusionStatus::PENDING);
    BOOST_CHECK(request->submitter == submitter);
    BOOST_CHECK_EQUAL(request->submittedAt, timestamp);
    BOOST_CHECK_EQUAL(request->deadline, timestamp + l2::FORCED_INCLUSION_DEADLINE);
    BOOST_CHECK_EQUAL(request->bondAmount, l2::FORCED_INCLUSION_BOND);
    
    BOOST_CHECK_EQUAL(system.GetPendingRequestCount(), 1u);
    BOOST_CHECK_EQUAL(system.GetTotalBondsHeld(), l2::FORCED_INCLUSION_BOND);
}

BOOST_AUTO_TEST_CASE(submit_requires_minimum_bond)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    // Try to submit with insufficient bond
    auto request = system.SubmitForcedTransaction(
        TestRand256(), 100, submitter, RandomAddress160(),
        100 * COIN, {}, 21000, TestRand256(), 0,
        l2::FORCED_INCLUSION_BOND - 1,  // Insufficient bond
        timestamp);
    
    BOOST_CHECK(!request.has_value());
    BOOST_CHECK_EQUAL(system.GetPendingRequestCount(), 0u);
}

BOOST_AUTO_TEST_CASE(submit_requires_gas_limit)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    // Try to submit with zero gas limit
    auto request = system.SubmitForcedTransaction(
        TestRand256(), 100, submitter, RandomAddress160(),
        100 * COIN, {}, 0,  // Zero gas limit
        TestRand256(), 0, l2::FORCED_INCLUSION_BOND, timestamp);
    
    BOOST_CHECK(!request.has_value());
}

BOOST_AUTO_TEST_CASE(per_address_limit_enforced)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    // Submit maximum allowed requests
    for (uint32_t i = 0; i < l2::MAX_FORCED_TX_PER_ADDRESS; ++i) {
        auto request = CreateForcedRequest(system, submitter, timestamp);
        BOOST_CHECK(request.has_value());
        timestamp += 100;
    }
    
    // Next request should fail
    auto extraRequest = CreateForcedRequest(system, submitter, timestamp);
    BOOST_CHECK(!extraRequest.has_value());
    
    BOOST_CHECK_EQUAL(system.GetPendingRequestCount(), l2::MAX_FORCED_TX_PER_ADDRESS);
}

BOOST_AUTO_TEST_CASE(mark_as_included_success)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    auto request = CreateForcedRequest(system, submitter, timestamp);
    BOOST_REQUIRE(request.has_value());
    
    // Mark as included
    uint64_t l2BlockNumber = 500;
    uint256 l2TxHash = TestRand256();
    
    bool success = system.MarkAsIncluded(
        request->requestId, l2BlockNumber, l2TxHash, timestamp + 1000);
    
    BOOST_CHECK(success);
    
    // Verify status updated
    auto updated = system.GetRequest(request->requestId);
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK(updated->status == l2::ForcedInclusionStatus::INCLUDED);
    BOOST_CHECK_EQUAL(updated->includedInBlock, l2BlockNumber);
    BOOST_CHECK(updated->l2TxHash == l2TxHash);
    
    // Bond should be returned
    BOOST_CHECK_EQUAL(system.GetTotalBondsHeld(), 0);
    BOOST_CHECK_EQUAL(system.GetPendingRequestCount(), 0u);
}

BOOST_AUTO_TEST_CASE(expired_request_slashes_sequencer)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint160 sequencer = RandomAddress160();
    uint64_t timestamp = 1000;
    
    // Set sequencer stake
    system.SetSequencerStake(sequencer, 1000 * COIN);
    
    auto request = CreateForcedRequest(system, submitter, timestamp);
    BOOST_REQUIRE(request.has_value());
    
    // Assign sequencer
    bool assigned = system.AssignSequencer(request->requestId, sequencer);
    BOOST_CHECK(assigned);
    
    // Process after deadline
    uint64_t afterDeadline = request->deadline + 1;
    auto results = system.ProcessExpiredRequests(afterDeadline);
    
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].finalStatus == l2::ForcedInclusionStatus::SLASHED);
    BOOST_CHECK(results[0].slashedSequencer == sequencer);
    BOOST_CHECK(results[0].slashedAmount > 0);
    BOOST_CHECK_EQUAL(results[0].bondReturned, l2::FORCED_INCLUSION_BOND);
    
    // Verify sequencer stats
    auto stats = system.GetSequencerStats(sequencer);
    BOOST_CHECK_EQUAL(stats.missedDeadlines, 1u);
    BOOST_CHECK(stats.totalSlashed > 0);
}

BOOST_AUTO_TEST_CASE(expired_without_sequencer_not_slashed)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    auto request = CreateForcedRequest(system, submitter, timestamp);
    BOOST_REQUIRE(request.has_value());
    
    // Don't assign sequencer, process after deadline
    uint64_t afterDeadline = request->deadline + 1;
    auto results = system.ProcessExpiredRequests(afterDeadline);
    
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].finalStatus == l2::ForcedInclusionStatus::EXPIRED);
    BOOST_CHECK(results[0].slashedSequencer.IsNull());
    BOOST_CHECK_EQUAL(results[0].slashedAmount, 0);
    BOOST_CHECK_EQUAL(results[0].bondReturned, l2::FORCED_INCLUSION_BOND);
}

BOOST_AUTO_TEST_CASE(repeat_offender_detection)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint160 sequencer = RandomAddress160();
    uint64_t timestamp = 1000;
    
    system.SetSequencerStake(sequencer, 1000 * COIN);
    
    // Create and expire multiple requests
    for (uint32_t i = 0; i < l2::REPEAT_OFFENDER_THRESHOLD; ++i) {
        auto request = CreateForcedRequest(system, submitter, timestamp);
        BOOST_REQUIRE(request.has_value());
        
        system.AssignSequencer(request->requestId, sequencer);
        
        // Process after deadline
        uint64_t afterDeadline = request->deadline + 1;
        system.ProcessExpiredRequests(afterDeadline);
        
        timestamp = afterDeadline + 100;
        
        // Use different submitter to avoid per-address limit
        submitter = RandomAddress160();
    }
    
    // Sequencer should be repeat offender
    BOOST_CHECK(system.IsRepeatOffender(sequencer));
    
    auto stats = system.GetSequencerStats(sequencer);
    BOOST_CHECK(stats.isRepeatOffender);
    BOOST_CHECK_EQUAL(stats.missedDeadlines, l2::REPEAT_OFFENDER_THRESHOLD);
}

BOOST_AUTO_TEST_CASE(request_serialization_roundtrip)
{
    l2::ForcedInclusionRequest original;
    original.requestId = TestRand256();
    original.l1TxHash = TestRand256();
    original.l1BlockNumber = TestRand64();
    original.submitter = RandomAddress160();
    original.target = RandomAddress160();
    original.value = (TestRand64() % 1000 + 1) * COIN;
    original.data = RandomData(50);
    original.gasLimit = 21000 + TestRand64() % 100000;
    original.maxGasPrice = TestRand256();
    original.nonce = TestRand64() % 1000;
    original.bondAmount = l2::FORCED_INCLUSION_BOND;
    original.submittedAt = 1000;
    original.deadline = 1000 + l2::FORCED_INCLUSION_DEADLINE;
    original.status = l2::ForcedInclusionStatus::PENDING;
    original.l2ChainId = 1;
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::ForcedInclusionRequest restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(time_remaining_calculation)
{
    l2::ForcedInclusionSystem system(1);
    
    uint160 submitter = RandomAddress160();
    uint64_t timestamp = 1000;
    
    auto request = CreateForcedRequest(system, submitter, timestamp);
    BOOST_REQUIRE(request.has_value());
    
    // Check time remaining at various points
    uint64_t remaining = system.GetTimeRemaining(request->requestId, timestamp);
    BOOST_CHECK_EQUAL(remaining, l2::FORCED_INCLUSION_DEADLINE);
    
    remaining = system.GetTimeRemaining(request->requestId, timestamp + 1000);
    BOOST_CHECK_EQUAL(remaining, l2::FORCED_INCLUSION_DEADLINE - 1000);
    
    // After deadline
    remaining = system.GetTimeRemaining(request->requestId, request->deadline + 1);
    BOOST_CHECK_EQUAL(remaining, 0u);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 19: Forced Inclusion Guarantee**
 * 
 * *For any* transaction submitted via L1 forced inclusion, it SHALL be
 * included in an L2 block within 24 hours or the sequencer SHALL be slashed.
 * 
 * **Validates: Requirements 17.2, 17.3**
 */
BOOST_AUTO_TEST_CASE(property_forced_inclusion_guarantee)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::ForcedInclusionSystem system(1);
        
        uint64_t timestamp = 1000;
        
        // Create random number of forced transactions
        int numRequests = 1 + (TestRand32() % 5);
        std::vector<uint256> requestIds;
        std::map<uint256, uint160> assignedSequencers;
        
        for (int i = 0; i < numRequests; ++i) {
            uint160 submitter = RandomAddress160();
            auto request = CreateForcedRequest(system, submitter, timestamp);
            
            if (request.has_value()) {
                requestIds.push_back(request->requestId);
                
                // Randomly assign sequencer
                if (TestRand32() % 2 == 0) {
                    uint160 sequencer = RandomAddress160();
                    system.SetSequencerStake(sequencer, 1000 * COIN);
                    system.AssignSequencer(request->requestId, sequencer);
                    assignedSequencers[request->requestId] = sequencer;
                }
            }
            timestamp += 100;
        }
        
        // Randomly include some requests before deadline
        std::set<uint256> includedIds;
        for (const auto& requestId : requestIds) {
            if (TestRand32() % 2 == 0) {
                auto req = system.GetRequest(requestId);
                if (req.has_value()) {
                    uint64_t includeTime = req->submittedAt + (TestRand64() % l2::FORCED_INCLUSION_DEADLINE);
                    system.MarkAsIncluded(requestId, TestRand64() % 1000000, TestRand256(), includeTime);
                    includedIds.insert(requestId);
                }
            }
        }
        
        // Process expired requests after deadline
        uint64_t afterDeadline = timestamp + l2::FORCED_INCLUSION_DEADLINE + 1000;
        auto results = system.ProcessExpiredRequests(afterDeadline);
        
        // Verify property: for each non-included request with assigned sequencer,
        // the sequencer must be slashed
        for (const auto& result : results) {
            // Skip if was included
            if (includedIds.count(result.requestId) > 0) {
                continue;
            }
            
            auto it = assignedSequencers.find(result.requestId);
            if (it != assignedSequencers.end()) {
                // Had assigned sequencer - must be slashed
                BOOST_CHECK_MESSAGE(
                    result.finalStatus == l2::ForcedInclusionStatus::SLASHED,
                    "Request with assigned sequencer should be SLASHED in iteration " << iteration);
                BOOST_CHECK_MESSAGE(
                    result.slashedSequencer == it->second,
                    "Correct sequencer should be slashed in iteration " << iteration);
                BOOST_CHECK_MESSAGE(
                    result.slashedAmount > 0,
                    "Slashed amount should be positive in iteration " << iteration);
            } else {
                // No assigned sequencer - just expired
                BOOST_CHECK_MESSAGE(
                    result.finalStatus == l2::ForcedInclusionStatus::EXPIRED,
                    "Request without sequencer should be EXPIRED in iteration " << iteration);
            }
            
            // Bond should always be returned
            BOOST_CHECK_MESSAGE(
                result.bondReturned == l2::FORCED_INCLUSION_BOND,
                "Bond should be returned in iteration " << iteration);
        }
        
        // Verify all bonds accounted for
        BOOST_CHECK_MESSAGE(
            system.GetTotalBondsHeld() == 0,
            "All bonds should be released after processing in iteration " << iteration);
    }
}

/**
 * **Property: Deadline Enforcement**
 * 
 * *For any* forced inclusion request, the deadline SHALL be exactly
 * 24 hours (FORCED_INCLUSION_DEADLINE) after submission.
 * 
 * **Validates: Requirements 17.2**
 */
BOOST_AUTO_TEST_CASE(property_deadline_enforcement)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::ForcedInclusionSystem system(1);
        
        // Random submission time
        uint64_t timestamp = TestRand64() % 1000000000;
        
        // Create multiple requests
        int numRequests = 1 + (TestRand32() % 10);
        
        for (int i = 0; i < numRequests; ++i) {
            uint160 submitter = RandomAddress160();
            auto request = CreateForcedRequest(system, submitter, timestamp);
            
            if (request.has_value()) {
                // Verify deadline is exactly 24 hours after submission
                BOOST_CHECK_MESSAGE(
                    request->deadline == timestamp + l2::FORCED_INCLUSION_DEADLINE,
                    "Deadline should be exactly 24 hours after submission in iteration " 
                    << iteration << ", request " << i);
                
                // Verify not expired before deadline
                BOOST_CHECK_MESSAGE(
                    !system.IsRequestExpired(request->requestId, timestamp),
                    "Request should not be expired at submission time in iteration " << iteration);
                
                BOOST_CHECK_MESSAGE(
                    !system.IsRequestExpired(request->requestId, request->deadline - 1),
                    "Request should not be expired 1 second before deadline in iteration " << iteration);
                
                // Verify expired after deadline
                BOOST_CHECK_MESSAGE(
                    system.IsRequestExpired(request->requestId, request->deadline + 1),
                    "Request should be expired 1 second after deadline in iteration " << iteration);
            }
            
            timestamp += 1000;
        }
    }
}

/**
 * **Property: Bond Conservation**
 * 
 * *For any* sequence of forced inclusion submissions and resolutions,
 * the total bonds held plus bonds returned SHALL equal total bonds submitted.
 * 
 * **Validates: Requirements 17.1**
 */
BOOST_AUTO_TEST_CASE(property_bond_conservation)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ForcedInclusionSystem system(1);
        
        uint64_t timestamp = 1000;
        CAmount totalSubmitted = 0;
        CAmount totalReturned = 0;
        
        // Submit requests from multiple submitters
        int numSubmitters = 2 + (TestRand32() % 3);
        std::vector<uint256> requestIds;
        
        for (int s = 0; s < numSubmitters; ++s) {
            uint160 submitter = RandomAddress160();
            int numRequests = 1 + (TestRand32() % 3);
            
            for (int i = 0; i < numRequests; ++i) {
                auto request = CreateForcedRequest(system, submitter, timestamp);
                
                if (request.has_value()) {
                    requestIds.push_back(request->requestId);
                    totalSubmitted += request->bondAmount;
                    
                    // Randomly assign sequencer
                    if (TestRand32() % 2 == 0) {
                        uint160 sequencer = RandomAddress160();
                        system.SetSequencerStake(sequencer, 1000 * COIN);
                        system.AssignSequencer(request->requestId, sequencer);
                    }
                }
                timestamp += 100;
            }
        }
        
        // Verify bonds held equals submitted
        BOOST_CHECK_MESSAGE(
            system.GetTotalBondsHeld() == totalSubmitted,
            "Bonds held should equal submitted before processing in iteration " << iteration);
        
        // Randomly include some, let others expire
        for (const auto& requestId : requestIds) {
            if (TestRand32() % 2 == 0) {
                auto req = system.GetRequest(requestId);
                if (req.has_value() && req->status == l2::ForcedInclusionStatus::PENDING) {
                    system.MarkAsIncluded(requestId, TestRand64() % 1000000, TestRand256(), timestamp);
                    totalReturned += req->bondAmount;
                }
            }
        }
        
        // Process expired
        uint64_t afterDeadline = timestamp + l2::FORCED_INCLUSION_DEADLINE + 1000;
        auto results = system.ProcessExpiredRequests(afterDeadline);
        
        for (const auto& result : results) {
            totalReturned += result.bondReturned;
        }
        
        // Verify conservation
        CAmount totalAccountedFor = system.GetTotalBondsHeld() + totalReturned;
        BOOST_CHECK_MESSAGE(
            totalAccountedFor == totalSubmitted,
            "Bond conservation violated in iteration " << iteration
            << " (submitted=" << totalSubmitted 
            << ", held=" << system.GetTotalBondsHeld()
            << ", returned=" << totalReturned << ")");
    }
}

/**
 * **Property: Sequencer Slashing Proportionality**
 * 
 * *For any* sequencer that misses a forced inclusion deadline, the slashing
 * amount SHALL be at least FORCED_INCLUSION_SLASH_AMOUNT.
 * 
 * **Validates: Requirements 17.3**
 */
BOOST_AUTO_TEST_CASE(property_sequencer_slashing_proportionality)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::ForcedInclusionSystem system(1);
        
        uint160 submitter = RandomAddress160();
        uint160 sequencer = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Set random stake
        CAmount stake = (100 + TestRand64() % 10000) * COIN;
        system.SetSequencerStake(sequencer, stake);
        
        auto request = CreateForcedRequest(system, submitter, timestamp);
        BOOST_REQUIRE(request.has_value());
        
        system.AssignSequencer(request->requestId, sequencer);
        
        // Process after deadline
        uint64_t afterDeadline = request->deadline + 1;
        auto results = system.ProcessExpiredRequests(afterDeadline);
        
        BOOST_REQUIRE_EQUAL(results.size(), 1u);
        
        // Verify slashing amount is at least minimum
        BOOST_CHECK_MESSAGE(
            results[0].slashedAmount >= l2::FORCED_INCLUSION_SLASH_AMOUNT,
            "Slashing amount should be at least minimum in iteration " << iteration
            << " (slashed=" << results[0].slashedAmount 
            << ", minimum=" << l2::FORCED_INCLUSION_SLASH_AMOUNT << ")");
        
        // Verify slashing is proportional to stake (10% or minimum)
        CAmount expectedMinSlash = std::max(l2::FORCED_INCLUSION_SLASH_AMOUNT, stake / 10);
        BOOST_CHECK_MESSAGE(
            results[0].slashedAmount >= expectedMinSlash,
            "Slashing should be proportional to stake in iteration " << iteration);
    }
}

/**
 * **Property: Per-Address Limit Enforcement**
 * 
 * *For any* address, the number of pending forced transactions SHALL not
 * exceed MAX_FORCED_TX_PER_ADDRESS.
 * 
 * **Validates: Requirements 17.1**
 */
BOOST_AUTO_TEST_CASE(property_per_address_limit)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ForcedInclusionSystem system(1);
        
        uint160 submitter = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Try to submit more than limit
        int attemptedRequests = l2::MAX_FORCED_TX_PER_ADDRESS + 5;
        int successfulRequests = 0;
        
        for (int i = 0; i < attemptedRequests; ++i) {
            auto request = CreateForcedRequest(system, submitter, timestamp);
            if (request.has_value()) {
                successfulRequests++;
            }
            timestamp += 100;
        }
        
        // Verify limit enforced
        BOOST_CHECK_MESSAGE(
            static_cast<uint32_t>(successfulRequests) == l2::MAX_FORCED_TX_PER_ADDRESS,
            "Should only allow MAX_FORCED_TX_PER_ADDRESS requests in iteration " << iteration);
        
        // Verify pending count
        auto pending = system.GetPendingRequests(submitter);
        BOOST_CHECK_MESSAGE(
            pending.size() == l2::MAX_FORCED_TX_PER_ADDRESS,
            "Pending count should equal limit in iteration " << iteration);
    }
}

/**
 * **Property: Inclusion Releases Slot**
 * 
 * *For any* forced transaction that is included, the submitter's slot
 * SHALL be released allowing new submissions.
 * 
 * **Validates: Requirements 17.2**
 */
BOOST_AUTO_TEST_CASE(property_inclusion_releases_slot)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ForcedInclusionSystem system(1);
        
        uint160 submitter = RandomAddress160();
        uint64_t timestamp = 1000;
        
        // Fill up to limit
        std::vector<uint256> requestIds;
        for (uint32_t i = 0; i < l2::MAX_FORCED_TX_PER_ADDRESS; ++i) {
            auto request = CreateForcedRequest(system, submitter, timestamp);
            BOOST_REQUIRE(request.has_value());
            requestIds.push_back(request->requestId);
            timestamp += 100;
        }
        
        // Verify at limit
        auto extraRequest = CreateForcedRequest(system, submitter, timestamp);
        BOOST_CHECK(!extraRequest.has_value());
        
        // Include one request
        system.MarkAsIncluded(requestIds[0], 100, TestRand256(), timestamp);
        
        // Should now be able to submit one more
        auto newRequest = CreateForcedRequest(system, submitter, timestamp + 100);
        BOOST_CHECK_MESSAGE(
            newRequest.has_value(),
            "Should be able to submit after inclusion releases slot in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
