// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_leader_election_tests.cpp
 * @brief Property-based tests for L2 Leader Election
 * 
 * **Feature: cascoin-l2-solution, Property 2: Sequencer Election Determinism**
 * **Validates: Requirements 2a.1, 2a.2**
 * 
 * Property 2: Sequencer Election Determinism
 * *For any* given slot number and set of eligible sequencers, the leader 
 * election algorithm SHALL always select the same leader when using the 
 * same random seed.
 * 
 * **Feature: cascoin-l2-solution, Property 3: Failover Consistency**
 * **Validates: Requirements 2b.2, 2b.3, 2b.5**
 * 
 * Property 3: Failover Consistency
 * *For any* sequencer failure, the failover order SHALL be deterministic 
 * and all nodes SHALL agree on the next leader within the timeout period.
 */

#include <l2/leader_election.h>
#include <l2/sequencer_discovery.h>
#include <l2/l2_chainparams.h>
#include <key.h>
#include <random.h>
#include <uint256.h>
#include <hash.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <set>
#include <algorithm>

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
 * Generate a random uint256 for use as election seed
 */
uint256 RandomUint256()
{
    uint256 result;
    for (int i = 0; i < 8; ++i) {
        uint32_t val = TestRand32();
        memcpy(result.begin() + i * 4, &val, 4);
    }
    return result;
}

/**
 * Generate a random key
 */
CKey RandomKey()
{
    CKey key;
    key.MakeNewKey(true);
    return key;
}

/**
 * Generate a random eligible sequencer info
 */
l2::SequencerInfo RandomEligibleSequencer()
{
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::SequencerInfo info;
    info.address = pubkey.GetID();
    info.pubkey = pubkey;
    info.verifiedStake = (100 + TestRand64() % 900) * COIN;  // 100-1000 CAS
    info.verifiedHatScore = 70 + (TestRand32() % 31);  // 70-100
    info.peerCount = 1 + (TestRand32() % 20);  // 1-20 peers
    info.lastAnnouncement = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info.isVerified = true;
    info.isEligible = true;
    info.l2ChainId = 1;
    
    return info;
}

/**
 * Generate a list of random eligible sequencers
 */
std::vector<l2::SequencerInfo> RandomSequencerList(size_t count)
{
    std::vector<l2::SequencerInfo> sequencers;
    sequencers.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        sequencers.push_back(RandomEligibleSequencer());
    }
    
    return sequencers;
}

} // anonymous namespace

// Use BasicTestingSetup to initialize ECC and random subsystems
BOOST_FIXTURE_TEST_SUITE(l2_leader_election_tests, BasicTestingSetup)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(leader_election_result_serialization)
{
    l2::LeaderElectionResult result;
    result.leaderAddress = RandomEligibleSequencer().address;
    result.slotNumber = TestRand64();
    result.validUntilBlock = TestRand64();
    result.electionSeed = RandomUint256();
    result.electionTimestamp = TestRand64();
    result.isValid = true;
    
    // Add some backup sequencers
    for (int i = 0; i < 3; ++i) {
        result.backupSequencers.push_back(RandomEligibleSequencer().address);
    }
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << result;
    
    // Deserialize
    l2::LeaderElectionResult restored;
    ss >> restored;
    
    // Verify all fields match
    BOOST_CHECK(result.leaderAddress == restored.leaderAddress);
    BOOST_CHECK_EQUAL(result.slotNumber, restored.slotNumber);
    BOOST_CHECK_EQUAL(result.validUntilBlock, restored.validUntilBlock);
    BOOST_CHECK(result.electionSeed == restored.electionSeed);
    BOOST_CHECK_EQUAL(result.electionTimestamp, restored.electionTimestamp);
    BOOST_CHECK_EQUAL(result.isValid, restored.isValid);
    BOOST_CHECK_EQUAL(result.backupSequencers.size(), restored.backupSequencers.size());
}

BOOST_AUTO_TEST_CASE(leadership_claim_serialization)
{
    CKey key = RandomKey();
    CPubKey pubkey = key.GetPubKey();
    
    l2::LeadershipClaim claim;
    claim.claimantAddress = pubkey.GetID();
    claim.slotNumber = TestRand64();
    claim.failoverPosition = TestRand32() % 10;
    claim.claimTimestamp = TestRand64();
    claim.previousLeader = RandomEligibleSequencer().address;
    claim.claimReason = "timeout";
    
    // Sign
    uint256 hash = claim.GetSigningHash();
    key.Sign(hash, claim.signature);
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << claim;
    
    // Deserialize
    l2::LeadershipClaim restored;
    ss >> restored;
    
    // Verify all fields match
    BOOST_CHECK(claim.claimantAddress == restored.claimantAddress);
    BOOST_CHECK_EQUAL(claim.slotNumber, restored.slotNumber);
    BOOST_CHECK_EQUAL(claim.failoverPosition, restored.failoverPosition);
    BOOST_CHECK_EQUAL(claim.claimTimestamp, restored.claimTimestamp);
    BOOST_CHECK(claim.previousLeader == restored.previousLeader);
    BOOST_CHECK_EQUAL(claim.claimReason, restored.claimReason);
    BOOST_CHECK(claim.signature == restored.signature);
}

BOOST_AUTO_TEST_CASE(leader_election_empty_sequencers)
{
    l2::LeaderElection election(1);
    
    std::vector<l2::SequencerInfo> emptyList;
    uint256 seed = RandomUint256();
    
    l2::LeaderElectionResult result = election.ElectLeader(0, emptyList, seed);
    
    // Should return invalid result
    BOOST_CHECK(!result.isValid);
}

BOOST_AUTO_TEST_CASE(leader_election_single_sequencer)
{
    l2::LeaderElection election(1);
    
    std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(1);
    uint256 seed = RandomUint256();
    
    l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
    
    // Should return valid result with the only sequencer as leader
    BOOST_CHECK(result.isValid);
    BOOST_CHECK(result.leaderAddress == sequencers[0].address);
    BOOST_CHECK(result.backupSequencers.empty());
}

BOOST_AUTO_TEST_CASE(leader_election_multiple_sequencers)
{
    l2::LeaderElection election(1);
    
    std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(5);
    uint256 seed = RandomUint256();
    
    l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
    
    // Should return valid result
    BOOST_CHECK(result.isValid);
    
    // Leader should be one of the sequencers
    bool leaderFound = false;
    for (const auto& seq : sequencers) {
        if (seq.address == result.leaderAddress) {
            leaderFound = true;
            break;
        }
    }
    BOOST_CHECK(leaderFound);
    
    // Should have backup sequencers (all except leader)
    BOOST_CHECK_EQUAL(result.backupSequencers.size(), 4u);
    
    // Leader should not be in backup list
    for (const auto& backup : result.backupSequencers) {
        BOOST_CHECK(backup != result.leaderAddress);
    }
}

BOOST_AUTO_TEST_CASE(leader_election_slot_calculation)
{
    l2::LeaderElection election(1);
    election.SetBlocksPerLeader(10);
    
    // Blocks 0-9 should be slot 0
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(0), 0u);
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(5), 0u);
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(9), 0u);
    
    // Blocks 10-19 should be slot 1
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(10), 1u);
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(15), 1u);
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(19), 1u);
    
    // Block 100 should be slot 10
    BOOST_CHECK_EQUAL(election.GetSlotForBlock(100), 10u);
}

BOOST_AUTO_TEST_CASE(leader_election_failover_position)
{
    l2::LeaderElection election(1);
    
    std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(5);
    uint256 seed = RandomUint256();
    
    l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
    
    // The GetFailoverPosition method checks against currentElection_ which is
    // set internally. For this test, we verify the result structure directly.
    
    // Leader should be in the result
    BOOST_CHECK(result.isValid);
    BOOST_CHECK(!result.leaderAddress.IsNull());
    
    // Backup sequencers should be at positions 1, 2, 3, 4
    BOOST_CHECK_EQUAL(result.backupSequencers.size(), 4u);
    
    // Leader should not be in backup list
    for (const auto& backup : result.backupSequencers) {
        BOOST_CHECK(backup != result.leaderAddress);
    }
    
    // All backups should be unique
    std::set<uint160> backupSet(result.backupSequencers.begin(), result.backupSequencers.end());
    BOOST_CHECK_EQUAL(backupSet.size(), result.backupSequencers.size());
}

BOOST_AUTO_TEST_CASE(weighted_random_select_basic)
{
    l2::LeaderElection election(1);
    
    // Create sequencers with different weights
    std::vector<l2::SequencerInfo> sequencers;
    
    l2::SequencerInfo seq1;
    seq1.address = RandomEligibleSequencer().address;
    seq1.verifiedHatScore = 100;
    seq1.verifiedStake = 1000 * COIN;  // High weight
    sequencers.push_back(seq1);
    
    l2::SequencerInfo seq2;
    seq2.address = RandomEligibleSequencer().address;
    seq2.verifiedHatScore = 70;
    seq2.verifiedStake = 100 * COIN;  // Lower weight
    sequencers.push_back(seq2);
    
    uint256 seed = RandomUint256();
    
    // Selection should return one of the sequencers
    uint160 selected = election.WeightedRandomSelect(sequencers, seed);
    
    BOOST_CHECK(selected == seq1.address || selected == seq2.address);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 2: Sequencer Election Determinism**
 * 
 * *For any* given slot number and set of eligible sequencers, the leader 
 * election algorithm SHALL always select the same leader when using the 
 * same random seed.
 * 
 * **Validates: Requirements 2a.1, 2a.2**
 */
BOOST_AUTO_TEST_CASE(property_leader_election_determinism)
{
    // Run 100 iterations as required for property-based tests
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate random inputs
        uint64_t slotNumber = TestRand64() % 1000000;
        size_t numSequencers = 2 + (TestRand32() % 10);  // 2-11 sequencers
        std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(numSequencers);
        uint256 seed = RandomUint256();
        
        // Create two independent election instances
        l2::LeaderElection election1(1);
        l2::LeaderElection election2(1);
        
        // Perform election with same inputs
        l2::LeaderElectionResult result1 = election1.ElectLeader(slotNumber, sequencers, seed);
        l2::LeaderElectionResult result2 = election2.ElectLeader(slotNumber, sequencers, seed);
        
        // Both should be valid
        BOOST_CHECK_MESSAGE(result1.isValid && result2.isValid,
            "Election results should be valid for iteration " << iteration);
        
        // Leaders should be identical
        BOOST_CHECK_MESSAGE(result1.leaderAddress == result2.leaderAddress,
            "Leader election determinism failed for iteration " << iteration <<
            " (slot=" << slotNumber << ", sequencers=" << numSequencers << ")");
        
        // Backup lists should be identical
        BOOST_CHECK_MESSAGE(result1.backupSequencers == result2.backupSequencers,
            "Backup sequencer list determinism failed for iteration " << iteration);
        
        // Slot numbers should match
        BOOST_CHECK_EQUAL(result1.slotNumber, result2.slotNumber);
        
        // Election seeds should match
        BOOST_CHECK(result1.electionSeed == result2.electionSeed);
    }
}


/**
 * **Property: Different Seeds Produce Different Results**
 * 
 * *For any* set of eligible sequencers with more than one member,
 * different random seeds SHOULD produce different leader selections
 * (with high probability).
 * 
 * **Validates: Requirements 2a.1**
 */
BOOST_AUTO_TEST_CASE(property_different_seeds_different_results)
{
    l2::LeaderElection election(1);
    
    // Use a fixed set of sequencers with varying weights
    std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(10);
    
    // Track unique leaders selected
    std::set<uint160> uniqueLeaders;
    
    // Run 100 elections with different seeds
    for (int i = 0; i < 100; ++i) {
        uint256 seed = RandomUint256();
        l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
        
        BOOST_CHECK(result.isValid);
        uniqueLeaders.insert(result.leaderAddress);
    }
    
    // With 10 sequencers and 100 random seeds, we should see multiple different leaders
    // (probability of always selecting the same leader is extremely low)
    BOOST_CHECK_MESSAGE(uniqueLeaders.size() > 1,
        "Expected multiple different leaders with different seeds, got " << uniqueLeaders.size());
}

/**
 * **Property: Weighted Selection Favors Higher Weight**
 * 
 * *For any* set of sequencers with significantly different weights,
 * the higher-weight sequencer SHOULD be selected more frequently.
 * 
 * **Validates: Requirements 2a.2**
 */
BOOST_AUTO_TEST_CASE(property_weighted_selection_distribution)
{
    l2::LeaderElection election(1);
    
    // Create two sequencers with very different weights
    std::vector<l2::SequencerInfo> sequencers;
    
    l2::SequencerInfo highWeight;
    highWeight.address = RandomEligibleSequencer().address;
    highWeight.verifiedHatScore = 100;
    highWeight.verifiedStake = 10000 * COIN;  // Very high weight
    sequencers.push_back(highWeight);
    
    l2::SequencerInfo lowWeight;
    lowWeight.address = RandomEligibleSequencer().address;
    lowWeight.verifiedHatScore = 70;
    lowWeight.verifiedStake = 100 * COIN;  // Much lower weight
    sequencers.push_back(lowWeight);
    
    // Count selections
    int highWeightCount = 0;
    int lowWeightCount = 0;
    
    // Run 100 elections
    for (int i = 0; i < 100; ++i) {
        uint256 seed = RandomUint256();
        l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
        
        if (result.leaderAddress == highWeight.address) {
            highWeightCount++;
        } else {
            lowWeightCount++;
        }
    }
    
    // High weight sequencer should be selected more often
    // (with 100x weight difference, should be selected much more frequently)
    BOOST_CHECK_MESSAGE(highWeightCount > lowWeightCount,
        "High weight sequencer should be selected more often: high=" << highWeightCount << 
        ", low=" << lowWeightCount);
}

/**
 * **Property: Backup List Excludes Leader**
 * 
 * *For any* election result, the backup sequencer list SHALL NOT
 * contain the elected leader.
 * 
 * **Validates: Requirements 2b.2**
 */
BOOST_AUTO_TEST_CASE(property_backup_list_excludes_leader)
{
    l2::LeaderElection election(1);
    
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        size_t numSequencers = 2 + (TestRand32() % 10);
        std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(numSequencers);
        uint256 seed = RandomUint256();
        
        l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
        
        BOOST_CHECK(result.isValid);
        
        // Leader should not be in backup list
        for (const auto& backup : result.backupSequencers) {
            BOOST_CHECK_MESSAGE(backup != result.leaderAddress,
                "Leader found in backup list for iteration " << iteration);
        }
    }
}

/**
 * **Property: Backup List Contains All Non-Leaders**
 * 
 * *For any* election with N sequencers, the backup list SHALL contain
 * exactly N-1 sequencers (all except the leader), up to the maximum limit.
 * 
 * **Validates: Requirements 2b.2, 2b.3**
 */
BOOST_AUTO_TEST_CASE(property_backup_list_completeness)
{
    l2::LeaderElection election(1);
    
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        size_t numSequencers = 2 + (TestRand32() % 8);  // 2-9 sequencers (within max backup limit)
        std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(numSequencers);
        uint256 seed = RandomUint256();
        
        l2::LeaderElectionResult result = election.ElectLeader(0, sequencers, seed);
        
        BOOST_CHECK(result.isValid);
        
        // Backup list should have N-1 sequencers
        BOOST_CHECK_MESSAGE(result.backupSequencers.size() == numSequencers - 1,
            "Expected " << (numSequencers - 1) << " backups, got " << 
            result.backupSequencers.size() << " for iteration " << iteration);
        
        // All backups should be from the original sequencer list
        std::set<uint160> originalAddresses;
        for (const auto& seq : sequencers) {
            originalAddresses.insert(seq.address);
        }
        
        for (const auto& backup : result.backupSequencers) {
            BOOST_CHECK_MESSAGE(originalAddresses.count(backup) > 0,
                "Backup not from original list for iteration " << iteration);
        }
    }
}

/**
 * **Property: Backup List Ordering is Deterministic**
 * 
 * *For any* election with the same inputs, the backup sequencer ordering
 * SHALL be identical across multiple executions.
 * 
 * **Validates: Requirements 2b.3, 2b.5**
 */
BOOST_AUTO_TEST_CASE(property_backup_ordering_determinism)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint64_t slotNumber = TestRand64() % 1000000;
        size_t numSequencers = 3 + (TestRand32() % 8);
        std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(numSequencers);
        uint256 seed = RandomUint256();
        
        // Create two independent election instances
        l2::LeaderElection election1(1);
        l2::LeaderElection election2(1);
        
        l2::LeaderElectionResult result1 = election1.ElectLeader(slotNumber, sequencers, seed);
        l2::LeaderElectionResult result2 = election2.ElectLeader(slotNumber, sequencers, seed);
        
        // Backup ordering should be identical
        BOOST_CHECK_MESSAGE(result1.backupSequencers.size() == result2.backupSequencers.size(),
            "Backup list sizes differ for iteration " << iteration);
        
        for (size_t i = 0; i < result1.backupSequencers.size(); ++i) {
            BOOST_CHECK_MESSAGE(result1.backupSequencers[i] == result2.backupSequencers[i],
                "Backup ordering differs at position " << i << " for iteration " << iteration);
        }
    }
}

/**
 * **Property: Slot Number Affects Election**
 * 
 * *For any* set of sequencers, different slot numbers with the same
 * seed generation method SHOULD produce different election results
 * (with high probability).
 * 
 * **Validates: Requirements 2a.1, 2a.2**
 */
BOOST_AUTO_TEST_CASE(property_slot_affects_election)
{
    l2::LeaderElection election(1);
    
    std::vector<l2::SequencerInfo> sequencers = RandomSequencerList(10);
    
    // Track unique leaders for different slots
    std::set<uint160> uniqueLeaders;
    
    // Run elections for different slots
    for (uint64_t slot = 0; slot < 100; ++slot) {
        uint256 seed = election.GenerateElectionSeed(slot);
        l2::LeaderElectionResult result = election.ElectLeader(slot, sequencers, seed);
        
        BOOST_CHECK(result.isValid);
        uniqueLeaders.insert(result.leaderAddress);
    }
    
    // Should see multiple different leaders across slots
    BOOST_CHECK_MESSAGE(uniqueLeaders.size() > 1,
        "Expected different leaders for different slots, got " << uniqueLeaders.size());
}

/**
 * **Property: Conflicting Claims Resolution is Deterministic**
 * 
 * *For any* set of conflicting leadership claims, the resolution
 * SHALL produce the same winner regardless of claim order.
 * 
 * **Validates: Requirements 2b.5, 2b.7**
 */
BOOST_AUTO_TEST_CASE(property_conflict_resolution_determinism)
{
    l2::LeaderElection election(1);
    
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Create conflicting claims
        std::vector<l2::LeadershipClaim> claims;
        
        for (int i = 0; i < 3; ++i) {
            CKey key = RandomKey();
            CPubKey pubkey = key.GetPubKey();
            
            l2::LeadershipClaim claim;
            claim.claimantAddress = pubkey.GetID();
            claim.slotNumber = 100;
            claim.failoverPosition = TestRand32() % 5;
            claim.claimTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() - (TestRand32() % 60);
            claim.claimReason = "timeout";
            
            claims.push_back(claim);
        }
        
        // Resolve in original order
        l2::LeadershipClaim winner1 = election.ResolveConflictingClaims(claims);
        
        // Shuffle and resolve again
        std::vector<l2::LeadershipClaim> shuffled = claims;
        std::reverse(shuffled.begin(), shuffled.end());
        l2::LeadershipClaim winner2 = election.ResolveConflictingClaims(shuffled);
        
        // Winners should be the same
        BOOST_CHECK_MESSAGE(winner1.claimantAddress == winner2.claimantAddress,
            "Conflict resolution not deterministic for iteration " << iteration);
    }
}

/**
 * **Property: Lower Failover Position Wins Conflicts**
 * 
 * *For any* two conflicting claims with different failover positions,
 * the claim with the lower position SHALL win.
 * 
 * **Validates: Requirements 2b.5**
 */
BOOST_AUTO_TEST_CASE(property_lower_failover_position_wins)
{
    l2::LeaderElection election(1);
    
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        CKey key1 = RandomKey();
        CKey key2 = RandomKey();
        
        l2::LeadershipClaim claim1;
        claim1.claimantAddress = key1.GetPubKey().GetID();
        claim1.slotNumber = 100;
        claim1.failoverPosition = 1;  // Lower position
        claim1.claimTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        l2::LeadershipClaim claim2;
        claim2.claimantAddress = key2.GetPubKey().GetID();
        claim2.slotNumber = 100;
        claim2.failoverPosition = 3;  // Higher position
        claim2.claimTimestamp = claim1.claimTimestamp;  // Same timestamp
        
        std::vector<l2::LeadershipClaim> claims = {claim1, claim2};
        l2::LeadershipClaim winner = election.ResolveConflictingClaims(claims);
        
        // Lower failover position should win
        BOOST_CHECK_MESSAGE(winner.claimantAddress == claim1.claimantAddress,
            "Lower failover position should win for iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
