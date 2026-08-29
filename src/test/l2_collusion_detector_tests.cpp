// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_collusion_detector_tests.cpp
 * @brief Unit tests for L2 Anti-Collusion Detection System
 * 
 * Tests for timing correlation detection, voting pattern analysis,
 * wallet cluster integration, and stake concentration monitoring.
 * 
 * Requirements: 22.1, 22.2, 22.4
 */

#include <l2/collusion_detector.h>
#include <l2/l2_chainparams.h>
#include <key.h>
#include <random.h>
#include <uint256.h>
#include <hash.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <chrono>

namespace {

// Local random context for tests
FastRandomContext g_test_rand_ctx(true);

uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

uint256 RandomUint256() {
    uint256 result;
    for (int i = 0; i < 8; ++i) {
        uint32_t val = TestRand32();
        memcpy(result.begin() + i * 4, &val, 4);
    }
    return result;
}

CKey RandomKey() {
    CKey key;
    key.MakeNewKey(true);
    return key;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(l2_collusion_detector_tests, BasicTestingSetup)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(collusion_detector_construction)
{
    l2::CollusionDetector detector(1);
    
    BOOST_CHECK_EQUAL(detector.GetChainId(), 1u);
    BOOST_CHECK_CLOSE(detector.GetTimingCorrelationThreshold(), 0.8, 0.001);
    BOOST_CHECK_CLOSE(detector.GetVotingCorrelationThreshold(), 0.9, 0.001);
    BOOST_CHECK_CLOSE(detector.GetStakeConcentrationLimit(), 0.2, 0.001);
}

BOOST_AUTO_TEST_CASE(sequencer_action_serialization)
{
    l2::SequencerAction action;
    action.sequencerAddress = RandomKey().GetPubKey().GetID();
    action.timestamp = TestRand64();
    action.blockHash = RandomUint256();
    action.voteType = l2::VoteType::ACCEPT;
    action.isBlockProposal = true;
    action.slotNumber = TestRand64() % 1000;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << action;
    
    // Deserialize
    l2::SequencerAction restored;
    ss >> restored;
    
    BOOST_CHECK(action.sequencerAddress == restored.sequencerAddress);
    BOOST_CHECK_EQUAL(action.timestamp, restored.timestamp);
    BOOST_CHECK(action.blockHash == restored.blockHash);
    BOOST_CHECK(action.voteType == restored.voteType);
    BOOST_CHECK_EQUAL(action.isBlockProposal, restored.isBlockProposal);
    BOOST_CHECK_EQUAL(action.slotNumber, restored.slotNumber);
}

BOOST_AUTO_TEST_CASE(voting_pattern_stats_serialization)
{
    l2::VotingPatternStats stats;
    stats.sequencer1 = RandomKey().GetPubKey().GetID();
    stats.sequencer2 = RandomKey().GetPubKey().GetID();
    stats.totalVotesCounted = 100;
    stats.matchingVotes = 80;
    stats.opposingVotes = 20;
    stats.UpdateCorrelation();
    stats.lastUpdated = TestRand64();
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << stats;
    
    // Deserialize
    l2::VotingPatternStats restored;
    ss >> restored;
    
    BOOST_CHECK(stats.sequencer1 == restored.sequencer1);
    BOOST_CHECK(stats.sequencer2 == restored.sequencer2);
    BOOST_CHECK_EQUAL(stats.totalVotesCounted, restored.totalVotesCounted);
    BOOST_CHECK_EQUAL(stats.matchingVotes, restored.matchingVotes);
    BOOST_CHECK_EQUAL(stats.opposingVotes, restored.opposingVotes);
    BOOST_CHECK_CLOSE(stats.correlationScore, restored.correlationScore, 0.001);
    BOOST_CHECK_EQUAL(stats.lastUpdated, restored.lastUpdated);
}

BOOST_AUTO_TEST_CASE(timing_correlation_stats_serialization)
{
    l2::TimingCorrelationStats stats;
    stats.sequencer1 = RandomKey().GetPubKey().GetID();
    stats.sequencer2 = RandomKey().GetPubKey().GetID();
    stats.sampleCount = 50;
    stats.avgTimeDelta = 123.456;
    stats.stdDevTimeDelta = 45.678;
    stats.correlationScore = 0.85;
    stats.lastUpdated = TestRand64();
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << stats;
    
    // Deserialize
    l2::TimingCorrelationStats restored;
    ss >> restored;
    
    BOOST_CHECK(stats.sequencer1 == restored.sequencer1);
    BOOST_CHECK(stats.sequencer2 == restored.sequencer2);
    BOOST_CHECK_EQUAL(stats.sampleCount, restored.sampleCount);
    BOOST_CHECK_CLOSE(stats.avgTimeDelta, restored.avgTimeDelta, 0.01);
    BOOST_CHECK_CLOSE(stats.stdDevTimeDelta, restored.stdDevTimeDelta, 0.01);
    BOOST_CHECK_CLOSE(stats.correlationScore, restored.correlationScore, 0.001);
    BOOST_CHECK_EQUAL(stats.lastUpdated, restored.lastUpdated);
}

BOOST_AUTO_TEST_CASE(collusion_detection_result_serialization)
{
    l2::CollusionDetectionResult result;
    result.type = l2::CollusionType::VOTING_PATTERN;
    result.severity = l2::CollusionSeverity::HIGH;
    result.involvedSequencers.push_back(RandomKey().GetPubKey().GetID());
    result.involvedSequencers.push_back(RandomKey().GetPubKey().GetID());
    result.confidenceScore = 0.85;
    result.description = "Test collusion";
    result.detectionTimestamp = TestRand64();
    result.evidenceHash = RandomUint256();
    result.timingCorrelation = 0.75;
    result.votingCorrelation = 0.92;
    result.sameWalletCluster = false;
    result.stakeConcentration = 0.15;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << result;
    
    // Deserialize
    l2::CollusionDetectionResult restored;
    ss >> restored;
    
    BOOST_CHECK(result.type == restored.type);
    BOOST_CHECK(result.severity == restored.severity);
    BOOST_CHECK_EQUAL(result.involvedSequencers.size(), restored.involvedSequencers.size());
    BOOST_CHECK_CLOSE(result.confidenceScore, restored.confidenceScore, 0.001);
    BOOST_CHECK_EQUAL(result.description, restored.description);
    BOOST_CHECK_EQUAL(result.detectionTimestamp, restored.detectionTimestamp);
    BOOST_CHECK(result.evidenceHash == restored.evidenceHash);
    BOOST_CHECK_CLOSE(result.timingCorrelation, restored.timingCorrelation, 0.001);
    BOOST_CHECK_CLOSE(result.votingCorrelation, restored.votingCorrelation, 0.001);
    BOOST_CHECK_EQUAL(result.sameWalletCluster, restored.sameWalletCluster);
    BOOST_CHECK_CLOSE(result.stakeConcentration, restored.stakeConcentration, 0.001);
}

BOOST_AUTO_TEST_CASE(whistleblower_report_serialization)
{
    l2::WhistleblowerReport report;
    report.reporterAddress = RandomKey().GetPubKey().GetID();
    report.accusedSequencers.push_back(RandomKey().GetPubKey().GetID());
    report.accusedSequencers.push_back(RandomKey().GetPubKey().GetID());
    report.accusedType = l2::CollusionType::WALLET_CLUSTER;
    report.evidence = "Evidence data";
    report.evidenceHash = RandomUint256();
    report.reportTimestamp = TestRand64();
    report.bondAmount = 10 * COIN;
    report.isValidated = true;
    report.isRewarded = false;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << report;
    
    // Deserialize
    l2::WhistleblowerReport restored;
    ss >> restored;
    
    BOOST_CHECK(report.reporterAddress == restored.reporterAddress);
    BOOST_CHECK_EQUAL(report.accusedSequencers.size(), restored.accusedSequencers.size());
    BOOST_CHECK(report.accusedType == restored.accusedType);
    BOOST_CHECK_EQUAL(report.evidence, restored.evidence);
    BOOST_CHECK(report.evidenceHash == restored.evidenceHash);
    BOOST_CHECK_EQUAL(report.reportTimestamp, restored.reportTimestamp);
    BOOST_CHECK_EQUAL(report.bondAmount, restored.bondAmount);
    BOOST_CHECK_EQUAL(report.isValidated, restored.isValidated);
    BOOST_CHECK_EQUAL(report.isRewarded, restored.isRewarded);
}


// ============================================================================
// Timing Correlation Detection Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(record_sequencer_action)
{
    l2::CollusionDetector detector(1);
    
    CKey key = RandomKey();
    uint160 address = key.GetPubKey().GetID();
    
    l2::SequencerAction action;
    action.sequencerAddress = address;
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    action.blockHash = RandomUint256();
    action.voteType = l2::VoteType::ACCEPT;
    action.isBlockProposal = false;
    action.slotNumber = 1;
    
    detector.RecordSequencerAction(action);
    
    // Record another action
    action.timestamp += 100;
    action.blockHash = RandomUint256();
    action.slotNumber = 2;
    detector.RecordSequencerAction(action);
    
    // Actions should be recorded (no direct way to verify, but no crash)
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(timing_correlation_insufficient_samples)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    
    // Record only a few actions (less than MIN_SAMPLES_FOR_CORRELATION)
    for (int i = 0; i < 5; ++i) {
        l2::SequencerAction action1;
        action1.sequencerAddress = addr1;
        action1.timestamp = 1000 + i * 100;
        action1.blockHash = RandomUint256();
        action1.slotNumber = i;
        detector.RecordSequencerAction(action1);
        
        l2::SequencerAction action2;
        action2.sequencerAddress = addr2;
        action2.timestamp = 1000 + i * 100 + 10;
        action2.blockHash = action1.blockHash;  // Same block
        action2.slotNumber = i;
        detector.RecordSequencerAction(action2);
    }
    
    // Should return empty stats due to insufficient samples
    l2::TimingCorrelationStats stats = detector.AnalyzeTimingCorrelation(addr1, addr2);
    BOOST_CHECK_EQUAL(stats.sampleCount, 0u);
}

BOOST_AUTO_TEST_CASE(timing_correlation_high_correlation)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    
    // Record many actions with highly correlated timing
    for (int i = 0; i < 20; ++i) {
        uint256 blockHash = RandomUint256();
        uint64_t baseTime = 1000000 + i * 10000;
        
        l2::SequencerAction action1;
        action1.sequencerAddress = addr1;
        action1.timestamp = baseTime;
        action1.blockHash = blockHash;
        action1.slotNumber = i;
        detector.RecordSequencerAction(action1);
        
        l2::SequencerAction action2;
        action2.sequencerAddress = addr2;
        action2.timestamp = baseTime + 5;  // Very close timing
        action2.blockHash = blockHash;
        action2.slotNumber = i;
        detector.RecordSequencerAction(action2);
    }
    
    l2::TimingCorrelationStats stats = detector.AnalyzeTimingCorrelation(addr1, addr2);
    
    // Should have high correlation due to synchronized timing
    BOOST_CHECK_GE(stats.sampleCount, 10u);
    // Correlation should be high (close to 1.0)
    BOOST_CHECK_GE(stats.correlationScore, 0.9);
}

// ============================================================================
// Voting Pattern Analysis Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(record_vote)
{
    l2::CollusionDetector detector(1);
    
    CKey key = RandomKey();
    uint160 voter = key.GetPubKey().GetID();
    uint256 blockHash = RandomUint256();
    
    detector.RecordVote(blockHash, voter, l2::VoteType::ACCEPT);
    
    // Record another vote
    uint256 blockHash2 = RandomUint256();
    detector.RecordVote(blockHash2, voter, l2::VoteType::REJECT);
    
    BOOST_CHECK(true);  // No crash
}

BOOST_AUTO_TEST_CASE(voting_pattern_perfect_correlation)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 voter1 = key1.GetPubKey().GetID();
    uint160 voter2 = key2.GetPubKey().GetID();
    
    // Record identical voting patterns
    for (int i = 0; i < 20; ++i) {
        uint256 blockHash = RandomUint256();
        l2::VoteType vote = (i % 2 == 0) ? l2::VoteType::ACCEPT : l2::VoteType::REJECT;
        
        detector.RecordVote(blockHash, voter1, vote);
        detector.RecordVote(blockHash, voter2, vote);  // Same vote
    }
    
    l2::VotingPatternStats stats = detector.AnalyzeVotingPattern(voter1, voter2);
    
    BOOST_CHECK_EQUAL(stats.totalVotesCounted, 20u);
    BOOST_CHECK_EQUAL(stats.matchingVotes, 20u);
    BOOST_CHECK_EQUAL(stats.opposingVotes, 0u);
    BOOST_CHECK_CLOSE(stats.correlationScore, 1.0, 0.001);  // Perfect correlation
}

BOOST_AUTO_TEST_CASE(voting_pattern_no_correlation)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 voter1 = key1.GetPubKey().GetID();
    uint160 voter2 = key2.GetPubKey().GetID();
    
    // Record opposite voting patterns
    for (int i = 0; i < 20; ++i) {
        uint256 blockHash = RandomUint256();
        
        detector.RecordVote(blockHash, voter1, l2::VoteType::ACCEPT);
        detector.RecordVote(blockHash, voter2, l2::VoteType::REJECT);  // Opposite vote
    }
    
    l2::VotingPatternStats stats = detector.AnalyzeVotingPattern(voter1, voter2);
    
    BOOST_CHECK_EQUAL(stats.totalVotesCounted, 20u);
    BOOST_CHECK_EQUAL(stats.matchingVotes, 0u);
    BOOST_CHECK_EQUAL(stats.opposingVotes, 20u);
    BOOST_CHECK_CLOSE(stats.correlationScore, -1.0, 0.001);  // Perfect anti-correlation
}

BOOST_AUTO_TEST_CASE(detect_voting_pattern_collusion)
{
    l2::CollusionDetector detector(1);
    detector.SetVotingCorrelationThreshold(0.9);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 voter1 = key1.GetPubKey().GetID();
    uint160 voter2 = key2.GetPubKey().GetID();
    uint160 voter3 = key3.GetPubKey().GetID();
    
    // voter1 and voter2 vote identically (colluding)
    // voter3 votes independently
    for (int i = 0; i < 20; ++i) {
        uint256 blockHash = RandomUint256();
        l2::VoteType colludingVote = (i % 2 == 0) ? l2::VoteType::ACCEPT : l2::VoteType::REJECT;
        l2::VoteType independentVote = (TestRand32() % 2 == 0) ? l2::VoteType::ACCEPT : l2::VoteType::REJECT;
        
        detector.RecordVote(blockHash, voter1, colludingVote);
        detector.RecordVote(blockHash, voter2, colludingVote);
        detector.RecordVote(blockHash, voter3, independentVote);
    }
    
    auto colludingPairs = detector.DetectVotingPatternCollusion();
    
    // Should detect voter1-voter2 as colluding
    bool foundCollusion = false;
    for (const auto& pair : colludingPairs) {
        if ((pair.first == voter1 && pair.second == voter2) ||
            (pair.first == voter2 && pair.second == voter1)) {
            foundCollusion = true;
            break;
        }
    }
    BOOST_CHECK(foundCollusion);
}


// ============================================================================
// Wallet Cluster Integration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(wallet_cluster_same_cluster)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 clusterId = RandomKey().GetPubKey().GetID();
    
    // Set both addresses to same cluster
    detector.SetTestWalletCluster(addr1, clusterId);
    detector.SetTestWalletCluster(addr2, clusterId);
    
    BOOST_CHECK(detector.AreInSameWalletCluster(addr1, addr2));
    BOOST_CHECK(detector.GetWalletCluster(addr1) == clusterId);
    BOOST_CHECK(detector.GetWalletCluster(addr2) == clusterId);
}

BOOST_AUTO_TEST_CASE(wallet_cluster_different_clusters)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 cluster1 = RandomKey().GetPubKey().GetID();
    uint160 cluster2 = RandomKey().GetPubKey().GetID();
    
    // Set addresses to different clusters
    detector.SetTestWalletCluster(addr1, cluster1);
    detector.SetTestWalletCluster(addr2, cluster2);
    
    BOOST_CHECK(!detector.AreInSameWalletCluster(addr1, addr2));
}

BOOST_AUTO_TEST_CASE(detect_wallet_cluster_violations)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 addr3 = key3.GetPubKey().GetID();
    uint160 sharedCluster = RandomKey().GetPubKey().GetID();
    uint160 uniqueCluster = RandomKey().GetPubKey().GetID();
    
    // addr1 and addr2 in same cluster (violation)
    // addr3 in different cluster (ok)
    detector.SetTestWalletCluster(addr1, sharedCluster);
    detector.SetTestWalletCluster(addr2, sharedCluster);
    detector.SetTestWalletCluster(addr3, uniqueCluster);
    
    // Record actions to register sequencers
    l2::SequencerAction action1, action2, action3;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    action3.sequencerAddress = addr3;
    action3.timestamp = 1002;
    action3.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    detector.RecordSequencerAction(action3);
    
    auto violations = detector.DetectWalletClusterViolations();
    
    // Should find one violation (sharedCluster with 2 members)
    BOOST_CHECK_EQUAL(violations.size(), 1u);
    BOOST_CHECK(violations.find(sharedCluster) != violations.end());
    BOOST_CHECK_EQUAL(violations[sharedCluster].size(), 2u);
}

BOOST_AUTO_TEST_CASE(validate_new_sequencer_cluster)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 existing1 = key1.GetPubKey().GetID();
    uint160 existing2 = key2.GetPubKey().GetID();
    uint160 newSeq = key3.GetPubKey().GetID();
    
    uint160 cluster1 = RandomKey().GetPubKey().GetID();
    uint160 cluster2 = RandomKey().GetPubKey().GetID();
    
    detector.SetTestWalletCluster(existing1, cluster1);
    detector.SetTestWalletCluster(existing2, cluster2);
    
    std::vector<uint160> existingSequencers = {existing1, existing2};
    
    // New sequencer in different cluster - should be valid
    uint160 newCluster = RandomKey().GetPubKey().GetID();
    detector.SetTestWalletCluster(newSeq, newCluster);
    BOOST_CHECK(detector.ValidateNewSequencerCluster(newSeq, existingSequencers));
    
    // New sequencer in same cluster as existing - should be invalid
    detector.SetTestWalletCluster(newSeq, cluster1);
    BOOST_CHECK(!detector.ValidateNewSequencerCluster(newSeq, existingSequencers));
}

// ============================================================================
// Stake Concentration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(stake_concentration_calculation)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 addr3 = key3.GetPubKey().GetID();
    
    // Set stakes: addr1 and addr2 in same cluster with 300 CAS total
    // addr3 in different cluster with 100 CAS
    // Total: 400 CAS, cluster1 has 75% concentration
    uint160 cluster1 = RandomKey().GetPubKey().GetID();
    uint160 cluster2 = RandomKey().GetPubKey().GetID();
    
    detector.SetTestWalletCluster(addr1, cluster1);
    detector.SetTestWalletCluster(addr2, cluster1);
    detector.SetTestWalletCluster(addr3, cluster2);
    
    detector.SetTestSequencerStake(addr1, 200 * COIN);
    detector.SetTestSequencerStake(addr2, 100 * COIN);
    detector.SetTestSequencerStake(addr3, 100 * COIN);
    
    // Record actions to register sequencers
    l2::SequencerAction action1, action2, action3;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    action3.sequencerAddress = addr3;
    action3.timestamp = 1002;
    action3.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    detector.RecordSequencerAction(action3);
    
    // addr1's cluster has 300/400 = 75% concentration
    double concentration = detector.CalculateStakeConcentration(addr1);
    BOOST_CHECK_CLOSE(concentration, 0.75, 1.0);
    
    // addr3's cluster has 100/400 = 25% concentration
    concentration = detector.CalculateStakeConcentration(addr3);
    BOOST_CHECK_CLOSE(concentration, 0.25, 1.0);
}

BOOST_AUTO_TEST_CASE(exceeds_stake_concentration_limit)
{
    l2::CollusionDetector detector(1);
    detector.SetStakeConcentrationLimit(0.2);  // 20% limit
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    
    uint160 cluster1 = RandomKey().GetPubKey().GetID();
    uint160 cluster2 = RandomKey().GetPubKey().GetID();
    
    detector.SetTestWalletCluster(addr1, cluster1);
    detector.SetTestWalletCluster(addr2, cluster2);
    
    // addr1 has 30% stake (exceeds 20% limit)
    // addr2 has 70% stake (exceeds 20% limit)
    detector.SetTestSequencerStake(addr1, 30 * COIN);
    detector.SetTestSequencerStake(addr2, 70 * COIN);
    
    // Record actions
    l2::SequencerAction action1, action2;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    
    BOOST_CHECK(detector.ExceedsStakeConcentrationLimit(addr1));
    BOOST_CHECK(detector.ExceedsStakeConcentrationLimit(addr2));
}

BOOST_AUTO_TEST_CASE(get_stake_concentration_violations)
{
    l2::CollusionDetector detector(1);
    detector.SetStakeConcentrationLimit(0.2);  // 20% limit
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 addr3 = key3.GetPubKey().GetID();
    
    uint160 cluster1 = RandomKey().GetPubKey().GetID();
    uint160 cluster2 = RandomKey().GetPubKey().GetID();
    uint160 cluster3 = RandomKey().GetPubKey().GetID();
    
    detector.SetTestWalletCluster(addr1, cluster1);
    detector.SetTestWalletCluster(addr2, cluster2);
    detector.SetTestWalletCluster(addr3, cluster3);
    
    // cluster1: 50% (violation), cluster2: 30% (violation), cluster3: 20% (ok)
    detector.SetTestSequencerStake(addr1, 50 * COIN);
    detector.SetTestSequencerStake(addr2, 30 * COIN);
    detector.SetTestSequencerStake(addr3, 20 * COIN);
    
    // Record actions
    l2::SequencerAction action1, action2, action3;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    action3.sequencerAddress = addr3;
    action3.timestamp = 1002;
    action3.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    detector.RecordSequencerAction(action3);
    
    auto violations = detector.GetStakeConcentrationViolations();
    
    // Should find 2 violations (cluster1 and cluster2)
    BOOST_CHECK_EQUAL(violations.size(), 2u);
    BOOST_CHECK(violations.find(cluster1) != violations.end());
    BOOST_CHECK(violations.find(cluster2) != violations.end());
    BOOST_CHECK(violations.find(cluster3) == violations.end());
}


// ============================================================================
// Comprehensive Collusion Detection Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(analyze_sequencer_pair_no_collusion)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    
    // Different clusters, low stake
    uint160 cluster1 = RandomKey().GetPubKey().GetID();
    uint160 cluster2 = RandomKey().GetPubKey().GetID();
    detector.SetTestWalletCluster(addr1, cluster1);
    detector.SetTestWalletCluster(addr2, cluster2);
    detector.SetTestSequencerStake(addr1, 10 * COIN);
    detector.SetTestSequencerStake(addr2, 10 * COIN);
    
    // Record some actions with different timing
    for (int i = 0; i < 15; ++i) {
        uint256 blockHash = RandomUint256();
        
        l2::SequencerAction action1;
        action1.sequencerAddress = addr1;
        action1.timestamp = 1000000 + i * 10000;
        action1.blockHash = blockHash;
        action1.slotNumber = i;
        detector.RecordSequencerAction(action1);
        
        l2::SequencerAction action2;
        action2.sequencerAddress = addr2;
        action2.timestamp = 1000000 + i * 10000 + (TestRand32() % 5000);  // Random offset
        action2.blockHash = blockHash;
        action2.slotNumber = i;
        detector.RecordSequencerAction(action2);
        
        // Random voting
        l2::VoteType vote1 = (TestRand32() % 2 == 0) ? l2::VoteType::ACCEPT : l2::VoteType::REJECT;
        l2::VoteType vote2 = (TestRand32() % 2 == 0) ? l2::VoteType::ACCEPT : l2::VoteType::REJECT;
        detector.RecordVote(blockHash, addr1, vote1);
        detector.RecordVote(blockHash, addr2, vote2);
    }
    
    l2::CollusionDetectionResult result = detector.AnalyzeSequencerPair(addr1, addr2);
    
    // Should not detect collusion with random behavior
    BOOST_CHECK(!result.sameWalletCluster);
    // Note: Random voting may or may not trigger correlation detection
}

BOOST_AUTO_TEST_CASE(analyze_sequencer_pair_wallet_cluster_collusion)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    
    // Same cluster - definite collusion
    uint160 sharedCluster = RandomKey().GetPubKey().GetID();
    detector.SetTestWalletCluster(addr1, sharedCluster);
    detector.SetTestWalletCluster(addr2, sharedCluster);
    detector.SetTestSequencerStake(addr1, 10 * COIN);
    detector.SetTestSequencerStake(addr2, 10 * COIN);
    
    // Record minimal actions
    l2::SequencerAction action1, action2;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    
    l2::CollusionDetectionResult result = detector.AnalyzeSequencerPair(addr1, addr2);
    
    BOOST_CHECK(result.IsCollusionDetected());
    BOOST_CHECK(result.sameWalletCluster);
    BOOST_CHECK_GE(result.confidenceScore, 0.9);  // Wallet cluster is strong evidence
}

BOOST_AUTO_TEST_CASE(run_full_detection)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 addr3 = key3.GetPubKey().GetID();
    
    // addr1 and addr2 in same cluster (collusion)
    // addr3 in different cluster
    uint160 sharedCluster = RandomKey().GetPubKey().GetID();
    uint160 uniqueCluster = RandomKey().GetPubKey().GetID();
    
    detector.SetTestWalletCluster(addr1, sharedCluster);
    detector.SetTestWalletCluster(addr2, sharedCluster);
    detector.SetTestWalletCluster(addr3, uniqueCluster);
    
    detector.SetTestSequencerStake(addr1, 10 * COIN);
    detector.SetTestSequencerStake(addr2, 10 * COIN);
    detector.SetTestSequencerStake(addr3, 10 * COIN);
    
    // Record actions
    l2::SequencerAction action1, action2, action3;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    action3.sequencerAddress = addr3;
    action3.timestamp = 1002;
    action3.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    detector.RecordSequencerAction(action3);
    
    auto results = detector.RunFullDetection();
    
    // Should detect at least one collusion (addr1-addr2 wallet cluster)
    BOOST_CHECK_GE(results.size(), 1u);
    
    bool foundWalletClusterCollusion = false;
    for (const auto& result : results) {
        if (result.sameWalletCluster) {
            foundWalletClusterCollusion = true;
            break;
        }
    }
    BOOST_CHECK(foundWalletClusterCollusion);
}

BOOST_AUTO_TEST_CASE(collusion_risk_score)
{
    l2::CollusionDetector detector(1);
    
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    CKey key3 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    uint160 addr3 = key3.GetPubKey().GetID();
    
    // addr1 and addr2 in same cluster (high risk)
    // addr3 alone (low risk)
    uint160 sharedCluster = RandomKey().GetPubKey().GetID();
    uint160 uniqueCluster = RandomKey().GetPubKey().GetID();
    
    detector.SetTestWalletCluster(addr1, sharedCluster);
    detector.SetTestWalletCluster(addr2, sharedCluster);
    detector.SetTestWalletCluster(addr3, uniqueCluster);
    
    // Record actions
    l2::SequencerAction action1, action2, action3;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    action3.sequencerAddress = addr3;
    action3.timestamp = 1002;
    action3.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    detector.RecordSequencerAction(action3);
    
    double risk1 = detector.GetCollusionRiskScore(addr1);
    double risk3 = detector.GetCollusionRiskScore(addr3);
    
    // addr1 should have higher risk due to shared cluster
    BOOST_CHECK_GT(risk1, risk3);
    BOOST_CHECK_GE(risk1, 0.9);  // Wallet cluster gives high risk
}

// ============================================================================
// Whistleblower System Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(submit_whistleblower_report)
{
    l2::CollusionDetector detector(1);
    
    CKey reporterKey = RandomKey();
    CKey accused1Key = RandomKey();
    CKey accused2Key = RandomKey();
    
    l2::WhistleblowerReport report;
    report.reporterAddress = reporterKey.GetPubKey().GetID();
    report.accusedSequencers.push_back(accused1Key.GetPubKey().GetID());
    report.accusedSequencers.push_back(accused2Key.GetPubKey().GetID());
    report.accusedType = l2::CollusionType::VOTING_PATTERN;
    report.evidence = "Evidence of coordinated voting";
    report.evidenceHash = RandomUint256();
    report.reportTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    report.bondAmount = 10 * COIN;
    
    BOOST_CHECK(detector.SubmitWhistleblowerReport(report));
    
    // Report with insufficient bond should fail
    report.bondAmount = 1 * COIN;
    BOOST_CHECK(!detector.SubmitWhistleblowerReport(report));
    
    // Report with no accused should fail
    report.bondAmount = 10 * COIN;
    report.accusedSequencers.clear();
    BOOST_CHECK(!detector.SubmitWhistleblowerReport(report));
}

BOOST_AUTO_TEST_CASE(get_pending_reports)
{
    l2::CollusionDetector detector(1);
    
    // Submit a report
    l2::WhistleblowerReport report;
    report.reporterAddress = RandomKey().GetPubKey().GetID();
    report.accusedSequencers.push_back(RandomKey().GetPubKey().GetID());
    report.accusedType = l2::CollusionType::TIMING_CORRELATION;
    report.evidenceHash = RandomUint256();
    report.reportTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    report.bondAmount = 10 * COIN;
    
    detector.SubmitWhistleblowerReport(report);
    
    auto pending = detector.GetPendingReports();
    BOOST_CHECK_EQUAL(pending.size(), 1u);
}

// ============================================================================
// Slashing Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(get_slashing_amount)
{
    l2::CollusionDetector detector(1);
    
    // Test different collusion types and severities
    CAmount amount;
    
    // Timing correlation - low severity
    amount = detector.GetSlashingAmount(l2::CollusionType::TIMING_CORRELATION, l2::CollusionSeverity::LOW);
    BOOST_CHECK_EQUAL(amount, 5 * COIN);  // 10 * 0.5
    
    // Voting pattern - medium severity
    amount = detector.GetSlashingAmount(l2::CollusionType::VOTING_PATTERN, l2::CollusionSeverity::MEDIUM);
    BOOST_CHECK_EQUAL(amount, 20 * COIN);  // 20 * 1.0
    
    // Wallet cluster - high severity
    amount = detector.GetSlashingAmount(l2::CollusionType::WALLET_CLUSTER, l2::CollusionSeverity::HIGH);
    BOOST_CHECK_EQUAL(amount, 100 * COIN);  // 50 * 2.0
    
    // Combined - critical severity
    amount = detector.GetSlashingAmount(l2::CollusionType::COMBINED, l2::CollusionSeverity::CRITICAL);
    BOOST_CHECK_EQUAL(amount, 500 * COIN);  // 100 * 5.0
    
    // None type should return 0
    amount = detector.GetSlashingAmount(l2::CollusionType::NONE, l2::CollusionSeverity::HIGH);
    BOOST_CHECK_EQUAL(amount, 0);
}

// ============================================================================
// Configuration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(configuration_thresholds)
{
    l2::CollusionDetector detector(1);
    
    // Test setting thresholds
    detector.SetTimingCorrelationThreshold(0.7);
    BOOST_CHECK_CLOSE(detector.GetTimingCorrelationThreshold(), 0.7, 0.001);
    
    detector.SetVotingCorrelationThreshold(0.85);
    BOOST_CHECK_CLOSE(detector.GetVotingCorrelationThreshold(), 0.85, 0.001);
    
    detector.SetStakeConcentrationLimit(0.15);
    BOOST_CHECK_CLOSE(detector.GetStakeConcentrationLimit(), 0.15, 0.001);
    
    // Test bounds clamping
    detector.SetTimingCorrelationThreshold(1.5);  // Should clamp to 1.0
    BOOST_CHECK_CLOSE(detector.GetTimingCorrelationThreshold(), 1.0, 0.001);
    
    detector.SetTimingCorrelationThreshold(-0.5);  // Should clamp to 0.0
    BOOST_CHECK_CLOSE(detector.GetTimingCorrelationThreshold(), 0.0, 0.001);
}

BOOST_AUTO_TEST_CASE(clear_detector)
{
    l2::CollusionDetector detector(1);
    
    // Add some data
    CKey key = RandomKey();
    uint160 addr = key.GetPubKey().GetID();
    
    l2::SequencerAction action;
    action.sequencerAddress = addr;
    action.timestamp = 1000;
    action.blockHash = RandomUint256();
    detector.RecordSequencerAction(action);
    
    detector.RecordVote(RandomUint256(), addr, l2::VoteType::ACCEPT);
    
    detector.SetTestSequencerStake(addr, 100 * COIN);
    detector.SetTestWalletCluster(addr, RandomKey().GetPubKey().GetID());
    
    // Clear
    detector.Clear();
    
    // Verify cleared (no crash on operations)
    auto results = detector.RunFullDetection();
    BOOST_CHECK(results.empty());
}

BOOST_AUTO_TEST_CASE(alert_callback)
{
    l2::CollusionDetector detector(1);
    
    bool callbackCalled = false;
    l2::CollusionDetectionResult receivedResult;
    
    detector.RegisterAlertCallback([&](const l2::CollusionDetectionResult& result) {
        callbackCalled = true;
        receivedResult = result;
    });
    
    // Create collusion scenario
    CKey key1 = RandomKey();
    CKey key2 = RandomKey();
    uint160 addr1 = key1.GetPubKey().GetID();
    uint160 addr2 = key2.GetPubKey().GetID();
    
    uint160 sharedCluster = RandomKey().GetPubKey().GetID();
    detector.SetTestWalletCluster(addr1, sharedCluster);
    detector.SetTestWalletCluster(addr2, sharedCluster);
    
    l2::SequencerAction action1, action2;
    action1.sequencerAddress = addr1;
    action1.timestamp = 1000;
    action1.blockHash = RandomUint256();
    action2.sequencerAddress = addr2;
    action2.timestamp = 1001;
    action2.blockHash = action1.blockHash;
    
    detector.RecordSequencerAction(action1);
    detector.RecordSequencerAction(action2);
    
    // Run detection - should trigger callback
    detector.RunFullDetection();
    
    BOOST_CHECK(callbackCalled);
    BOOST_CHECK(receivedResult.IsCollusionDetected());
}

BOOST_AUTO_TEST_SUITE_END()
