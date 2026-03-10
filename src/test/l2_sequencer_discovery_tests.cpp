// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_sequencer_discovery_tests.cpp
 * @brief Property-based tests for L2 Sequencer Discovery
 * 
 * **Feature: cascoin-l2-solution, Property: Sequencer Eligibility Determinism**
 * **Validates: Requirements 2.3, 2.4**
 * 
 * Property: Sequencer Eligibility Determinism
 * *For any* sequencer announcement with the same parameters (HAT score, stake, 
 * peer count), the eligibility determination SHALL produce the same result
 * regardless of when or how many times it is evaluated.
 */

#include <l2/sequencer_discovery.h>
#include <l2/l2_chainparams.h>
#include <key.h>
#include <random.h>
#include <uint256.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <set>

namespace {

// Local random context for tests (deterministic for reproducibility)
FastRandomContext g_test_rand_ctx(true);

uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

/**
 * Helper function to generate a random sequencer announcement
 */
l2::SeqAnnounceMsg RandomSeqAnnounceMsg(const CKey& key, uint64_t chainId)
{
    l2::SeqAnnounceMsg msg;
    
    CPubKey pubkey = key.GetPubKey();
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = (TestRand64() % 1000 + 1) * COIN;  // 1-1000 CAS
    msg.hatScore = TestRand32() % 101;  // 0-100
    msg.blockHeight = TestRand64() % 1000000;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.publicEndpoint = "";
    msg.peerCount = TestRand32() % 20;  // 0-19 peers
    msg.l2ChainId = chainId;
    msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
    
    // Sign the message
    msg.Sign(key);
    
    return msg;
}

/**
 * Helper function to generate a random key using the test framework's random
 */
CKey RandomKey()
{
    CKey key;
    key.MakeNewKey(true);
    return key;
}

/**
 * Helper function to check if a sequencer meets eligibility requirements
 * based on the L2 parameters
 */
static bool MeetsEligibilityRequirements(
    uint32_t hatScore,
    CAmount stake,
    uint32_t peerCount,
    const l2::L2Params& params)
{
    return hatScore >= params.nMinSequencerHATScore &&
           stake >= params.nMinSequencerStake &&
           peerCount >= params.nMinSequencerPeerCount;
}

} // anonymous namespace

// Use BasicTestingSetup to initialize ECC and random subsystems
BOOST_FIXTURE_TEST_SUITE(l2_sequencer_discovery_tests, BasicTestingSetup)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(seq_announce_msg_serialization)
{
    CKey key = RandomKey();
    l2::SeqAnnounceMsg msg = RandomSeqAnnounceMsg(key, 1);
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << msg;
    
    // Deserialize
    l2::SeqAnnounceMsg restored;
    ss >> restored;
    
    // Verify all fields match
    BOOST_CHECK(msg.sequencerAddress == restored.sequencerAddress);
    BOOST_CHECK_EQUAL(msg.stakeAmount, restored.stakeAmount);
    BOOST_CHECK_EQUAL(msg.hatScore, restored.hatScore);
    BOOST_CHECK_EQUAL(msg.blockHeight, restored.blockHeight);
    BOOST_CHECK_EQUAL(msg.timestamp, restored.timestamp);
    BOOST_CHECK_EQUAL(msg.peerCount, restored.peerCount);
    BOOST_CHECK_EQUAL(msg.l2ChainId, restored.l2ChainId);
}

BOOST_AUTO_TEST_CASE(seq_announce_msg_signing)
{
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::SeqAnnounceMsg msg;
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = 100 * COIN;
    msg.hatScore = 80;
    msg.blockHeight = 1000;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.peerCount = 5;
    msg.l2ChainId = 1;
    msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
    
    // Sign
    BOOST_CHECK(msg.Sign(key));
    BOOST_CHECK(!msg.signature.empty());
    
    // Verify
    BOOST_CHECK(msg.VerifySignature(pubkey));
    
    // Modify message and verify fails
    msg.hatScore = 90;
    BOOST_CHECK(!msg.VerifySignature(pubkey));
}

BOOST_AUTO_TEST_CASE(seq_announce_msg_expiry)
{
    l2::SeqAnnounceMsg msg;
    
    // Set timestamp to now
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Should not be expired
    BOOST_CHECK(!msg.IsExpired(3600));
    
    // Set timestamp to 2 hours ago
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 7200;
    
    // Should be expired with 1 hour max age
    BOOST_CHECK(msg.IsExpired(3600));
}

BOOST_AUTO_TEST_CASE(sequencer_discovery_basic_operations)
{
    l2::SequencerDiscovery discovery(1);
    
    // Initially empty
    BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 0u);
    BOOST_CHECK_EQUAL(discovery.GetEligibleCount(), 0u);
    
    // Create and process announcement
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::SeqAnnounceMsg msg;
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = 200 * COIN;  // Above minimum
    msg.hatScore = 80;  // Above minimum
    msg.blockHeight = 1000;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.peerCount = 5;  // Above minimum
    msg.l2ChainId = 1;
    msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
    msg.Sign(key);
    
    // Process announcement
    BOOST_CHECK(discovery.ProcessSeqAnnounce(msg, nullptr));
    
    // Should have one sequencer
    BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 1u);
    
    // Check if eligible
    BOOST_CHECK(discovery.IsEligibleSequencer(msg.sequencerAddress));
}

BOOST_AUTO_TEST_CASE(sequencer_discovery_ineligible_low_hat)
{
    l2::SequencerDiscovery discovery(1);
    
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::SeqAnnounceMsg msg;
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = 200 * COIN;
    msg.hatScore = 50;  // Below minimum (70)
    msg.blockHeight = 1000;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.peerCount = 5;
    msg.l2ChainId = 1;
    msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
    msg.Sign(key);
    
    BOOST_CHECK(discovery.ProcessSeqAnnounce(msg, nullptr));
    BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 1u);
    
    // Should NOT be eligible due to low HAT score
    BOOST_CHECK(!discovery.IsEligibleSequencer(msg.sequencerAddress));
}

BOOST_AUTO_TEST_CASE(sequencer_discovery_ineligible_low_stake)
{
    l2::SequencerDiscovery discovery(1);
    
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::SeqAnnounceMsg msg;
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = 50 * COIN;  // Below minimum (100 CAS)
    msg.hatScore = 80;
    msg.blockHeight = 1000;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.peerCount = 5;
    msg.l2ChainId = 1;
    msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
    msg.Sign(key);
    
    BOOST_CHECK(discovery.ProcessSeqAnnounce(msg, nullptr));
    
    // Should NOT be eligible due to low stake
    BOOST_CHECK(!discovery.IsEligibleSequencer(msg.sequencerAddress));
}

BOOST_AUTO_TEST_CASE(sequencer_discovery_wrong_chain)
{
    l2::SequencerDiscovery discovery(1);  // Chain ID 1
    
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::SeqAnnounceMsg msg;
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = 200 * COIN;
    msg.hatScore = 80;
    msg.blockHeight = 1000;
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.peerCount = 5;
    msg.l2ChainId = 2;  // Different chain ID
    msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
    msg.Sign(key);
    
    // Should reject announcement for different chain
    BOOST_CHECK(!discovery.ProcessSeqAnnounce(msg, nullptr));
    BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 0u);
}

BOOST_AUTO_TEST_CASE(sequencer_discovery_clear)
{
    l2::SequencerDiscovery discovery(1);
    
    // Add some sequencers
    for (int i = 0; i < 5; ++i) {
        CKey key = RandomKey();
        CPubKey pubkey = key.GetPubKey();
        
        l2::SeqAnnounceMsg msg;
        msg.sequencerAddress = pubkey.GetID();
        msg.stakeAmount = 200 * COIN;
        msg.hatScore = 80;
        msg.blockHeight = 1000;
        msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg.peerCount = 5;
        msg.l2ChainId = 1;
        msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
        msg.Sign(key);
        
        discovery.ProcessSeqAnnounce(msg, nullptr);
    }
    
    BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 5u);
    
    discovery.Clear();
    
    BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 0u);
}

BOOST_AUTO_TEST_CASE(sequencer_info_weight_calculation)
{
    l2::SequencerInfo info1;
    info1.verifiedHatScore = 80;
    info1.verifiedStake = 100 * COIN;
    
    l2::SequencerInfo info2;
    info2.verifiedHatScore = 80;
    info2.verifiedStake = 400 * COIN;  // 4x stake
    
    // Higher stake should give higher weight (but not 4x due to sqrt)
    uint64_t weight1 = info1.GetWeight();
    uint64_t weight2 = info2.GetWeight();
    
    BOOST_CHECK(weight2 > weight1);
    BOOST_CHECK(weight2 < weight1 * 4);  // Should be ~2x due to sqrt
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property: Sequencer Eligibility Determinism**
 * 
 * *For any* sequencer announcement with the same parameters,
 * the eligibility determination SHALL produce the same result
 * regardless of when or how many times it is evaluated.
 * 
 * **Validates: Requirements 2.3, 2.4**
 */
BOOST_AUTO_TEST_CASE(property_sequencer_eligibility_determinism)
{
    // Use mainnet params since GetL2Params() returns mainnet by default
    const l2::L2Params& params = l2::MainnetL2Params();
    
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        // Generate random parameters within ranges that can be both eligible and ineligible
        // Mainnet: HAT >= 70, stake >= 100 CAS, peers >= 3
        uint32_t hatScore = 50 + (TestRand32() % 51);  // 50-100 (some below 70)
        CAmount stake = (50 + TestRand64() % 150) * COIN;  // 50-200 CAS (some below 100)
        uint32_t peerCount = TestRand32() % 10;  // 0-9 (some below 3)
        
        // Calculate expected eligibility
        bool expectedEligible = MeetsEligibilityRequirements(
            hatScore, stake, peerCount, params);
        
        // Create two separate discovery instances
        l2::SequencerDiscovery discovery1(1);
        l2::SequencerDiscovery discovery2(1);
        
        // Create announcement with same parameters
        CKey key = RandomKey();
        CPubKey pubkey = key.GetPubKey();
        
        l2::SeqAnnounceMsg msg;
        msg.sequencerAddress = pubkey.GetID();
        msg.stakeAmount = stake;
        msg.hatScore = hatScore;
        msg.blockHeight = 1000;
        msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg.peerCount = peerCount;
        msg.l2ChainId = 1;
        msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
        msg.Sign(key);
        
        // Process in both instances
        discovery1.ProcessSeqAnnounce(msg, nullptr);
        discovery2.ProcessSeqAnnounce(msg, nullptr);
        
        // Check eligibility in both
        bool eligible1 = discovery1.IsEligibleSequencer(msg.sequencerAddress);
        bool eligible2 = discovery2.IsEligibleSequencer(msg.sequencerAddress);
        
        // Both should match expected
        BOOST_CHECK_MESSAGE(eligible1 == expectedEligible,
            "Eligibility mismatch in discovery1 for iteration " << iteration <<
            " (HAT=" << hatScore << ", stake=" << stake/COIN << ", peers=" << peerCount << ")");
        
        BOOST_CHECK_MESSAGE(eligible2 == expectedEligible,
            "Eligibility mismatch in discovery2 for iteration " << iteration);
        
        // Both instances should agree
        BOOST_CHECK_MESSAGE(eligible1 == eligible2,
            "Eligibility determinism failed for iteration " << iteration);
    }
}

/**
 * **Property: Eligible Sequencer List Consistency**
 * 
 * *For any* set of sequencer announcements, the list of eligible
 * sequencers SHALL contain exactly those that meet all requirements.
 * 
 * **Validates: Requirements 2.3**
 */
BOOST_AUTO_TEST_CASE(property_eligible_sequencer_list_consistency)
{
    // Use mainnet params since GetL2Params() returns mainnet by default
    const l2::L2Params& params = l2::MainnetL2Params();
    
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::SequencerDiscovery discovery(1);
        
        // Track expected eligible addresses
        std::set<uint160> expectedEligible;
        
        // Add random sequencers
        int numSequencers = 3 + (TestRand32() % 5);
        for (int i = 0; i < numSequencers; ++i) {
            CKey key = RandomKey();
            CPubKey pubkey = key.GetPubKey();
            
            // Generate parameters that can be both eligible and ineligible
            uint32_t hatScore = 50 + (TestRand32() % 51);  // 50-100
            CAmount stake = (50 + TestRand64() % 150) * COIN;  // 50-200 CAS
            uint32_t peerCount = TestRand32() % 10;  // 0-9
            
            l2::SeqAnnounceMsg msg;
            msg.sequencerAddress = pubkey.GetID();
            msg.stakeAmount = stake;
            msg.hatScore = hatScore;
            msg.blockHeight = 1000;
            msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            msg.peerCount = peerCount;
            msg.l2ChainId = 1;
            msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
            msg.Sign(key);
            
            discovery.ProcessSeqAnnounce(msg, nullptr);
            
            if (MeetsEligibilityRequirements(hatScore, stake, peerCount, params)) {
                expectedEligible.insert(msg.sequencerAddress);
            }
        }
        
        // Get eligible sequencers
        std::vector<l2::SequencerInfo> eligible = discovery.GetEligibleSequencers();
        
        // Convert to set for comparison
        std::set<uint160> actualEligible;
        for (const auto& info : eligible) {
            actualEligible.insert(info.address);
        }
        
        // Should match expected
        BOOST_CHECK_MESSAGE(actualEligible == expectedEligible,
            "Eligible sequencer list mismatch for iteration " << iteration <<
            " (expected " << expectedEligible.size() << ", got " << actualEligible.size() << ")");
    }
}

/**
 * **Property: Sequencer Weight Ordering**
 * 
 * *For any* list of eligible sequencers, they SHALL be ordered
 * by weight (descending) for consistent leader election.
 * 
 * **Validates: Requirements 2.3**
 */
BOOST_AUTO_TEST_CASE(property_sequencer_weight_ordering)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::SequencerDiscovery discovery(1);
        
        // Add eligible sequencers with varying weights
        int numSequencers = 3 + (TestRand32() % 5);
        for (int i = 0; i < numSequencers; ++i) {
            CKey key = RandomKey();
            CPubKey pubkey = key.GetPubKey();
            
            // Ensure all are eligible
            uint32_t hatScore = 70 + (TestRand32() % 31);  // 70-100
            CAmount stake = (100 + TestRand64() % 400) * COIN;  // 100-500 CAS
            uint32_t peerCount = 1 + (TestRand32() % 10);  // 1-10 peers
            
            l2::SeqAnnounceMsg msg;
            msg.sequencerAddress = pubkey.GetID();
            msg.stakeAmount = stake;
            msg.hatScore = hatScore;
            msg.blockHeight = 1000;
            msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            msg.peerCount = peerCount;
            msg.l2ChainId = 1;
            msg.protocolVersion = l2::L2_PROTOCOL_VERSION;
            msg.Sign(key);
            
            discovery.ProcessSeqAnnounce(msg, nullptr);
        }
        
        // Get eligible sequencers
        std::vector<l2::SequencerInfo> eligible = discovery.GetEligibleSequencers();
        
        // Verify ordering by weight (descending)
        for (size_t i = 1; i < eligible.size(); ++i) {
            BOOST_CHECK_MESSAGE(eligible[i-1].GetWeight() >= eligible[i].GetWeight(),
                "Weight ordering violated at position " << i << " for iteration " << iteration);
        }
    }
}

/**
 * **Property: Announcement Update Consistency**
 * 
 * *For any* sequencer that sends multiple announcements, only the
 * most recent announcement SHALL be used for eligibility.
 * 
 * **Validates: Requirements 2.5**
 */
BOOST_AUTO_TEST_CASE(property_announcement_update_consistency)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::SequencerDiscovery discovery(1);
        
        CKey key = RandomKey();
        CPubKey pubkey = key.GetPubKey();
        
        // First announcement - eligible
        l2::SeqAnnounceMsg msg1;
        msg1.sequencerAddress = pubkey.GetID();
        msg1.stakeAmount = 200 * COIN;
        msg1.hatScore = 80;
        msg1.blockHeight = 1000;
        msg1.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - 100;  // 100 seconds ago
        msg1.peerCount = 5;
        msg1.l2ChainId = 1;
        msg1.protocolVersion = l2::L2_PROTOCOL_VERSION;
        msg1.Sign(key);
        
        discovery.ProcessSeqAnnounce(msg1, nullptr);
        BOOST_CHECK(discovery.IsEligibleSequencer(msg1.sequencerAddress));
        
        // Second announcement - newer, with different peer count
        l2::SeqAnnounceMsg msg2;
        msg2.sequencerAddress = pubkey.GetID();
        msg2.stakeAmount = 200 * COIN;
        msg2.hatScore = 80;
        msg2.blockHeight = 1001;
        msg2.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();  // Now
        msg2.peerCount = 10;  // Updated peer count
        msg2.l2ChainId = 1;
        msg2.protocolVersion = l2::L2_PROTOCOL_VERSION;
        msg2.Sign(key);
        
        discovery.ProcessSeqAnnounce(msg2, nullptr);
        
        // Should still have only one sequencer
        BOOST_CHECK_EQUAL(discovery.GetSequencerCount(), 1u);
        
        // Get info and verify it was updated
        auto info = discovery.GetSequencerInfo(msg2.sequencerAddress);
        BOOST_CHECK(info.has_value());
        BOOST_CHECK_EQUAL(info->peerCount, 10u);  // Should have updated peer count
        BOOST_CHECK_EQUAL(info->lastAnnouncement, msg2.timestamp);
    }
}

BOOST_AUTO_TEST_SUITE_END()
