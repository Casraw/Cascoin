// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_sequencer_consensus_tests.cpp
 * @brief Property-based tests for L2 Sequencer Consensus Protocol
 * 
 * **Feature: cascoin-l2-solution, Property 11: Consensus Threshold Safety**
 * **Validates: Requirements 2a.5, 22.1**
 * 
 * Property 11: Consensus Threshold Safety
 * *For any* set of sequencer votes, consensus SHALL only be reached when
 * 2/3+ of weighted votes are ACCEPT. No block SHALL be finalized with
 * less than the threshold.
 */

#include <l2/sequencer_consensus.h>
#include <l2/sequencer_discovery.h>
#include <l2/leader_election.h>
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
 * Generate a random uint256
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
 * Generate a random block proposal
 */
l2::L2BlockProposal RandomProposal(const CKey& proposerKey)
{
    l2::L2BlockProposal proposal;
    proposal.blockNumber = TestRand64() % 1000000;
    proposal.parentHash = RandomUint256();
    proposal.stateRoot = RandomUint256();
    proposal.transactionsRoot = RandomUint256();
    proposal.proposerAddress = proposerKey.GetPubKey().GetID();
    proposal.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    proposal.l2ChainId = 1;
    proposal.gasLimit = 30000000;
    proposal.gasUsed = TestRand64() % 30000000;
    proposal.slotNumber = proposal.blockNumber / 10;
    
    proposal.Sign(proposerKey);
    
    return proposal;
}

/**
 * Generate a vote for a proposal
 */
l2::SequencerVote CreateVote(
    const uint256& blockHash,
    const CKey& voterKey,
    l2::VoteType voteType,
    uint64_t slotNumber)
{
    l2::SequencerVote vote;
    vote.blockHash = blockHash;
    vote.voterAddress = voterKey.GetPubKey().GetID();
    vote.vote = voteType;
    vote.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    vote.slotNumber = slotNumber;
    
    if (voteType == l2::VoteType::REJECT) {
        vote.rejectReason = "Test rejection";
    }
    
    vote.Sign(voterKey);
    
    return vote;
}

/**
 * Helper struct for test sequencer
 */
struct TestSequencer {
    CKey key;
    CPubKey pubkey;
    uint160 address;
    uint64_t weight;
    
    TestSequencer() : weight(100) {
        key.MakeNewKey(true);
        pubkey = key.GetPubKey();
        address = pubkey.GetID();
    }
    
    TestSequencer(uint64_t w) : weight(w) {
        key.MakeNewKey(true);
        pubkey = key.GetPubKey();
        address = pubkey.GetID();
    }
};

} // anonymous namespace

// Use BasicTestingSetup to initialize ECC and random subsystems
BOOST_FIXTURE_TEST_SUITE(l2_sequencer_consensus_tests, BasicTestingSetup)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(block_proposal_serialization)
{
    CKey key = RandomKey();
    l2::L2BlockProposal proposal = RandomProposal(key);
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << proposal;
    
    // Deserialize
    l2::L2BlockProposal restored;
    ss >> restored;
    
    // Verify all fields match
    BOOST_CHECK_EQUAL(proposal.blockNumber, restored.blockNumber);
    BOOST_CHECK(proposal.parentHash == restored.parentHash);
    BOOST_CHECK(proposal.stateRoot == restored.stateRoot);
    BOOST_CHECK(proposal.transactionsRoot == restored.transactionsRoot);
    BOOST_CHECK(proposal.proposerAddress == restored.proposerAddress);
    BOOST_CHECK_EQUAL(proposal.timestamp, restored.timestamp);
    BOOST_CHECK_EQUAL(proposal.l2ChainId, restored.l2ChainId);
    BOOST_CHECK_EQUAL(proposal.gasLimit, restored.gasLimit);
    BOOST_CHECK_EQUAL(proposal.gasUsed, restored.gasUsed);
    BOOST_CHECK(proposal.proposerSignature == restored.proposerSignature);
}

BOOST_AUTO_TEST_CASE(sequencer_vote_serialization)
{
    CKey key = RandomKey();
    l2::SequencerVote vote = CreateVote(RandomUint256(), key, l2::VoteType::ACCEPT, 100);
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << vote;
    
    // Deserialize
    l2::SequencerVote restored;
    ss >> restored;
    
    // Verify all fields match
    BOOST_CHECK(vote.blockHash == restored.blockHash);
    BOOST_CHECK(vote.voterAddress == restored.voterAddress);
    BOOST_CHECK(vote.vote == restored.vote);
    BOOST_CHECK_EQUAL(vote.rejectReason, restored.rejectReason);
    BOOST_CHECK_EQUAL(vote.timestamp, restored.timestamp);
    BOOST_CHECK_EQUAL(vote.slotNumber, restored.slotNumber);
    BOOST_CHECK(vote.signature == restored.signature);
}

BOOST_AUTO_TEST_CASE(proposal_signature_verification)
{
    CKey key = RandomKey();
    l2::L2BlockProposal proposal = RandomProposal(key);
    
    // Verify with correct key
    BOOST_CHECK(proposal.VerifySignature(key.GetPubKey()));
    
    // Verify with wrong key should fail
    CKey wrongKey = RandomKey();
    BOOST_CHECK(!proposal.VerifySignature(wrongKey.GetPubKey()));
}

BOOST_AUTO_TEST_CASE(vote_signature_verification)
{
    CKey key = RandomKey();
    l2::SequencerVote vote = CreateVote(RandomUint256(), key, l2::VoteType::ACCEPT, 100);
    
    // Verify with correct key
    BOOST_CHECK(vote.VerifySignature(key.GetPubKey()));
    
    // Verify with wrong key should fail
    CKey wrongKey = RandomKey();
    BOOST_CHECK(!vote.VerifySignature(wrongKey.GetPubKey()));
}

BOOST_AUTO_TEST_CASE(proposal_structure_validation)
{
    CKey key = RandomKey();
    
    // Valid proposal
    l2::L2BlockProposal validProposal = RandomProposal(key);
    BOOST_CHECK(validProposal.ValidateStructure());
    
    // Invalid: gas used > gas limit
    l2::L2BlockProposal invalidGas = validProposal;
    invalidGas.gasUsed = invalidGas.gasLimit + 1;
    BOOST_CHECK(!invalidGas.ValidateStructure());
    
    // Invalid: null proposer address
    l2::L2BlockProposal invalidProposer = validProposal;
    invalidProposer.proposerAddress = uint160();
    BOOST_CHECK(!invalidProposer.ValidateStructure());
    
    // Invalid: timestamp too far in future
    l2::L2BlockProposal invalidTimestamp = validProposal;
    invalidTimestamp.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 120;
    BOOST_CHECK(!invalidTimestamp.ValidateStructure());
}

BOOST_AUTO_TEST_CASE(vote_type_helpers)
{
    CKey key = RandomKey();
    uint256 blockHash = RandomUint256();
    
    l2::SequencerVote acceptVote = CreateVote(blockHash, key, l2::VoteType::ACCEPT, 100);
    BOOST_CHECK(acceptVote.IsAccept());
    BOOST_CHECK(!acceptVote.IsReject());
    BOOST_CHECK(!acceptVote.IsAbstain());
    
    l2::SequencerVote rejectVote = CreateVote(blockHash, key, l2::VoteType::REJECT, 100);
    BOOST_CHECK(!rejectVote.IsAccept());
    BOOST_CHECK(rejectVote.IsReject());
    BOOST_CHECK(!rejectVote.IsAbstain());
    
    l2::SequencerVote abstainVote = CreateVote(blockHash, key, l2::VoteType::ABSTAIN, 100);
    BOOST_CHECK(!abstainVote.IsAccept());
    BOOST_CHECK(!abstainVote.IsReject());
    BOOST_CHECK(abstainVote.IsAbstain());
}

BOOST_AUTO_TEST_CASE(consensus_result_serialization)
{
    l2::ConsensusResult result;
    result.blockHash = RandomUint256();
    result.consensusReached = true;
    result.totalVoters = 10;
    result.acceptVotes = 7;
    result.rejectVotes = 2;
    result.abstainVotes = 1;
    result.weightedAcceptPercent = 0.75;
    result.weightedRejectPercent = 0.20;
    result.timestamp = TestRand64();
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << result;
    
    // Deserialize
    l2::ConsensusResult restored;
    ss >> restored;
    
    // Verify all fields match
    BOOST_CHECK(result.blockHash == restored.blockHash);
    BOOST_CHECK_EQUAL(result.consensusReached, restored.consensusReached);
    BOOST_CHECK_EQUAL(result.totalVoters, restored.totalVoters);
    BOOST_CHECK_EQUAL(result.acceptVotes, restored.acceptVotes);
    BOOST_CHECK_EQUAL(result.rejectVotes, restored.rejectVotes);
    BOOST_CHECK_EQUAL(result.abstainVotes, restored.abstainVotes);
    // Check percentages with tolerance for serialization precision
    BOOST_CHECK_CLOSE(result.weightedAcceptPercent, restored.weightedAcceptPercent, 0.001);
    BOOST_CHECK_CLOSE(result.weightedRejectPercent, restored.weightedRejectPercent, 0.001);
    BOOST_CHECK_EQUAL(result.timestamp, restored.timestamp);
}

BOOST_AUTO_TEST_CASE(sequencer_consensus_basic_operations)
{
    l2::SequencerConsensus consensus(1);
    
    // Initial state should be waiting for proposal
    BOOST_CHECK(consensus.GetState() == l2::ConsensusState::WAITING_FOR_PROPOSAL);
    
    // No current proposal
    BOOST_CHECK(!consensus.GetCurrentProposal().has_value());
    
    // Create and propose a block
    CKey proposerKey = RandomKey();
    l2::L2BlockProposal proposal = RandomProposal(proposerKey);
    
    bool proposed = consensus.ProposeBlock(proposal);
    BOOST_CHECK(proposed);
    
    // State should now be collecting votes
    BOOST_CHECK(consensus.GetState() == l2::ConsensusState::COLLECTING_VOTES);
    
    // Current proposal should be set
    auto currentProposal = consensus.GetCurrentProposal();
    BOOST_CHECK(currentProposal.has_value());
    BOOST_CHECK(currentProposal->GetHash() == proposal.GetHash());
}

BOOST_AUTO_TEST_CASE(sequencer_consensus_clear)
{
    l2::SequencerConsensus consensus(1);
    
    // Propose a block
    CKey proposerKey = RandomKey();
    l2::L2BlockProposal proposal = RandomProposal(proposerKey);
    consensus.ProposeBlock(proposal);
    
    // Clear should reset state
    consensus.Clear();
    
    BOOST_CHECK(consensus.GetState() == l2::ConsensusState::WAITING_FOR_PROPOSAL);
    BOOST_CHECK(!consensus.GetCurrentProposal().has_value());
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 11: Consensus Threshold Safety**
 * 
 * *For any* set of sequencer votes, consensus SHALL only be reached when
 * 2/3+ of weighted votes are ACCEPT. No block SHALL be finalized with
 * less than the threshold.
 * 
 * **Validates: Requirements 2a.5, 22.1**
 */
BOOST_AUTO_TEST_CASE(property_consensus_threshold_safety)
{
    size_t consensusReachedCount = 0;
    size_t consensusFailedCount = 0;
    size_t stillCollectingCount = 0;
    size_t waitingForProposalCount = 0;
    
    // Run 100 iterations as required for property-based tests
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        // Generate random number of sequencers (3-20)
        size_t numSequencers = 3 + (TestRand32() % 18);
        std::vector<TestSequencer> sequencers;
        for (size_t i = 0; i < numSequencers; ++i) {
            // Random weight between 50 and 200
            sequencers.emplace_back(50 + (TestRand32() % 151));
        }
        
        // Register test sequencer weights
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        
        // Calculate total weight
        uint64_t totalWeight = 0;
        for (const auto& seq : sequencers) {
            totalWeight += seq.weight;
        }
        
        // Create a proposal from the first sequencer
        l2::L2BlockProposal proposal = RandomProposal(sequencers[0].key);
        consensus.ProposeBlock(proposal);
        
        uint256 blockHash = proposal.GetHash();
        
        // Randomly decide how many will vote ACCEPT (first N sequencers)
        size_t numAccept = TestRand32() % (numSequencers + 1);
        
        // Calculate expected accept weight at each step
        uint64_t runningAcceptWeight = 0;
        uint64_t runningRejectWeight = 0;
        bool consensusReachedDuringVoting = false;
        double acceptPercentWhenConsensusReached = 0.0;
        
        // Submit votes one by one and track when consensus is reached
        for (size_t i = 0; i < sequencers.size(); ++i) {
            l2::VoteType voteType;
            if (i < numAccept) {
                voteType = l2::VoteType::ACCEPT;
            } else {
                voteType = l2::VoteType::REJECT;
            }
            
            // Calculate what the accept percentage would be after this vote
            if (voteType == l2::VoteType::ACCEPT) {
                runningAcceptWeight += sequencers[i].weight;
            } else {
                runningRejectWeight += sequencers[i].weight;
            }
            
            l2::SequencerVote vote = CreateVote(blockHash, sequencers[i].key, voteType, proposal.slotNumber);
            consensus.ProcessVote(vote);
            
            // Check if consensus was just reached
            l2::ConsensusState state = consensus.GetState();
            if (state == l2::ConsensusState::CONSENSUS_REACHED && !consensusReachedDuringVoting) {
                consensusReachedDuringVoting = true;
                acceptPercentWhenConsensusReached = static_cast<double>(runningAcceptWeight) / totalWeight;
                
                // PROPERTY CHECK: Consensus should only be reached when accept >= 2/3
                BOOST_CHECK_GE(acceptPercentWhenConsensusReached, consensus.GetConsensusThreshold());
            }
            
            // Check if consensus failed (too many rejects)
            if (state == l2::ConsensusState::CONSENSUS_FAILED) {
                double rejectPercent = static_cast<double>(runningRejectWeight) / totalWeight;
                // Consensus fails when reject > 1/3 (making 2/3 accept impossible)
                BOOST_CHECK_GT(rejectPercent, 1.0 - consensus.GetConsensusThreshold());
                break;  // No more votes will be accepted
            }
        }
        
        // Final state check
        l2::ConsensusState finalState = consensus.GetState();
        
        // Calculate final expected percentages
        double finalAcceptPercent = static_cast<double>(runningAcceptWeight) / totalWeight;
        double finalRejectPercent = static_cast<double>(runningRejectWeight) / totalWeight;
        
        if (finalState == l2::ConsensusState::CONSENSUS_REACHED) {
            consensusReachedCount++;
            // Verify consensus was valid
            BOOST_CHECK_GE(acceptPercentWhenConsensusReached, consensus.GetConsensusThreshold());
        } else if (finalState == l2::ConsensusState::CONSENSUS_FAILED) {
            consensusFailedCount++;
            // Verify failure was valid (reject > 1/3)
            BOOST_CHECK_GT(finalRejectPercent, 1.0 - consensus.GetConsensusThreshold());
        } else if (finalState == l2::ConsensusState::COLLECTING_VOTES) {
            stillCollectingCount++;
            // Still collecting - neither threshold met
            BOOST_CHECK_LT(finalAcceptPercent, consensus.GetConsensusThreshold());
            BOOST_CHECK_LE(finalRejectPercent, 1.0 - consensus.GetConsensusThreshold());
        } else if (finalState == l2::ConsensusState::WAITING_FOR_PROPOSAL) {
            // This happens after consensus is reached and state is reset
            waitingForProposalCount++;
            // If we reached consensus during voting, this is expected
            if (consensusReachedDuringVoting) {
                BOOST_CHECK_GE(acceptPercentWhenConsensusReached, consensus.GetConsensusThreshold());
            }
        }
    }
    
    // Verify we tested a variety of outcomes
    size_t totalOutcomes = consensusReachedCount + consensusFailedCount + stillCollectingCount + waitingForProposalCount;
    BOOST_CHECK_EQUAL(totalOutcomes, 100u);
    
    // Log statistics
    BOOST_TEST_MESSAGE("Consensus reached: " << consensusReachedCount << 
                       ", Failed: " << consensusFailedCount << 
                       ", Still collecting: " << stillCollectingCount <<
                       ", Waiting (after consensus): " << waitingForProposalCount);
}

/**
 * **Property: Exactly 2/3 Threshold Boundary**
 * 
 * *For any* vote distribution, the consensus threshold of exactly 2/3
 * (66.67%) SHALL be the minimum required for consensus.
 * 
 * **Validates: Requirements 2a.5**
 */
BOOST_AUTO_TEST_CASE(property_exact_threshold_boundary)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        // Use exactly 3 sequencers with equal weight for clear 2/3 testing
        std::vector<TestSequencer> sequencers;
        for (int i = 0; i < 3; ++i) {
            sequencers.emplace_back(100);  // Equal weight
        }
        
        // Register test sequencer weights
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        
        CKey proposerKey = RandomKey();
        l2::L2BlockProposal proposal = RandomProposal(proposerKey);
        consensus.ProposeBlock(proposal);
        uint256 blockHash = proposal.GetHash();
        
        // Test case 1: 1 out of 3 ACCEPT (33.3%) - should NOT reach consensus
        consensus.Clear();
        // Re-register weights after Clear()
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        consensus.ProposeBlock(proposal);
        
        consensus.ProcessVote(CreateVote(blockHash, sequencers[0].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[1].key, l2::VoteType::REJECT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[2].key, l2::VoteType::REJECT, proposal.slotNumber));
        
        // Consensus should NOT be reached, so we can call CalculateWeightedVotes
        l2::ConsensusResult result1 = consensus.CalculateWeightedVotes(blockHash);
        BOOST_CHECK_MESSAGE(!result1.consensusReached,
            "1/3 ACCEPT should NOT reach consensus (iteration " << iteration << ")");
        
        // Test case 2: 2 out of 3 ACCEPT (66.7%) - should reach consensus
        consensus.Clear();
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        consensus.ProposeBlock(proposal);
        
        consensus.ProcessVote(CreateVote(blockHash, sequencers[0].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[1].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[2].key, l2::VoteType::REJECT, proposal.slotNumber));
        
        // Consensus is reached (66.67% >= 66.67%), get result from finalized block
        auto finalizedBlock2 = consensus.GetFinalizedBlock(blockHash);
        BOOST_CHECK_MESSAGE(finalizedBlock2.has_value(),
            "2/3 ACCEPT should reach consensus (iteration " << iteration << ")");
        
        // Test case 3: 3 out of 3 ACCEPT (100%) - should reach consensus
        consensus.Clear();
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        consensus.ProposeBlock(proposal);
        
        consensus.ProcessVote(CreateVote(blockHash, sequencers[0].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[1].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[2].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        
        // Consensus is reached (100% >= 66.67%), get result from finalized block
        auto finalizedBlock3 = consensus.GetFinalizedBlock(blockHash);
        BOOST_CHECK_MESSAGE(finalizedBlock3.has_value(),
            "3/3 ACCEPT should reach consensus (iteration " << iteration << ")");
    }
}

/**
 * **Property: Weighted Voting Respects Stake**
 * 
 * *For any* set of sequencers with different weights, the consensus
 * calculation SHALL use weighted votes, not simple vote counts.
 * 
 * **Validates: Requirements 2a.5, 22.1**
 */
BOOST_AUTO_TEST_CASE(property_weighted_voting_respects_stake)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        // Create sequencers with very different weights
        std::vector<TestSequencer> sequencers;
        sequencers.emplace_back(1000);  // High weight
        sequencers.emplace_back(100);   // Low weight
        sequencers.emplace_back(100);   // Low weight
        
        // Register test sequencer weights
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        
        // Total weight = 1200
        // High weight = 1000 (83.3%)
        // Low weights = 200 (16.7%)
        
        CKey proposerKey = RandomKey();
        l2::L2BlockProposal proposal = RandomProposal(proposerKey);
        consensus.ProposeBlock(proposal);
        uint256 blockHash = proposal.GetHash();
        
        // Test: High weight ACCEPT, low weights REJECT
        // 1000/1200 = 83.3% > 67% threshold - should reach consensus after first vote
        consensus.ProcessVote(CreateVote(blockHash, sequencers[0].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[1].key, l2::VoteType::REJECT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[2].key, l2::VoteType::REJECT, proposal.slotNumber));
        
        // Consensus is reached after first vote (83.3% > 66.67%), so get result from finalized block
        auto finalizedBlock1 = consensus.GetFinalizedBlock(blockHash);
        
        // By simple count: 1/3 ACCEPT = 33% - would NOT reach consensus
        // By weighted: 1000/1200 = 83.3% - SHOULD reach consensus
        BOOST_CHECK_MESSAGE(finalizedBlock1.has_value(),
            "High-weight ACCEPT should reach consensus despite minority count (iteration " << iteration << ")");
        if (finalizedBlock1) {
            BOOST_CHECK_MESSAGE(finalizedBlock1->consensusResult.weightedAcceptPercent > 0.8,
                "Weighted accept should be > 80% (iteration " << iteration << ")");
        }
        
        // Test: Low weights ACCEPT, high weight REJECT
        consensus.Clear();
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        consensus.ProposeBlock(proposal);
        
        consensus.ProcessVote(CreateVote(blockHash, sequencers[0].key, l2::VoteType::REJECT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[1].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[2].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        
        // Consensus should NOT be reached (only 16.7% weighted accept)
        // Block should NOT be finalized, so we can still call CalculateWeightedVotes
        l2::ConsensusResult result2 = consensus.CalculateWeightedVotes(blockHash);
        
        // By simple count: 2/3 ACCEPT = 67% - would reach consensus
        // By weighted: 200/1200 = 16.7% - should NOT reach consensus
        BOOST_CHECK_MESSAGE(!result2.consensusReached,
            "Low-weight ACCEPT should NOT reach consensus despite majority count (iteration " << iteration << ")");
        BOOST_CHECK_MESSAGE(result2.weightedAcceptPercent < 0.2,
            "Weighted accept should be < 20% (iteration " << iteration << ")");
    }
}

/**
 * **Property: No Duplicate Votes**
 * 
 * *For any* sequencer, only one vote per block SHALL be counted.
 * Duplicate votes from the same address SHALL be rejected.
 * 
 * **Validates: Requirements 2a.5**
 */
BOOST_AUTO_TEST_CASE(property_no_duplicate_votes)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        TestSequencer seq1(100);
        TestSequencer seq2(100);
        
        // Register test sequencer weights
        consensus.SetTestSequencerWeight(seq1.address, seq1.weight);
        consensus.SetTestSequencerWeight(seq2.address, seq2.weight);
        
        CKey proposerKey = RandomKey();
        l2::L2BlockProposal proposal = RandomProposal(proposerKey);
        consensus.ProposeBlock(proposal);
        uint256 blockHash = proposal.GetHash();
        
        // First vote should be accepted
        l2::SequencerVote vote1 = CreateVote(blockHash, seq1.key, l2::VoteType::ACCEPT, proposal.slotNumber);
        bool accepted1 = consensus.ProcessVote(vote1);
        BOOST_CHECK_MESSAGE(accepted1, "First vote should be accepted (iteration " << iteration << ")");
        
        // Duplicate vote from same sequencer should be rejected
        l2::SequencerVote vote2 = CreateVote(blockHash, seq1.key, l2::VoteType::REJECT, proposal.slotNumber);
        bool accepted2 = consensus.ProcessVote(vote2);
        BOOST_CHECK_MESSAGE(!accepted2, "Duplicate vote should be rejected (iteration " << iteration << ")");
        
        // Vote from different sequencer should be accepted
        l2::SequencerVote vote3 = CreateVote(blockHash, seq2.key, l2::VoteType::ACCEPT, proposal.slotNumber);
        bool accepted3 = consensus.ProcessVote(vote3);
        BOOST_CHECK_MESSAGE(accepted3, "Vote from different sequencer should be accepted (iteration " << iteration << ")");
        
        // After 2 ACCEPT votes (100% weighted), consensus is reached and block is finalized.
        // Get the result from the finalized block instead of currentVotes_ (which is cleared).
        auto finalizedBlock = consensus.GetFinalizedBlock(blockHash);
        BOOST_CHECK_MESSAGE(finalizedBlock.has_value(), "Block should be finalized (iteration " << iteration << ")");
        if (finalizedBlock) {
            BOOST_CHECK_EQUAL(finalizedBlock->consensusResult.totalVoters, 2u);  // Only 2 unique voters
            BOOST_CHECK_EQUAL(finalizedBlock->consensusResult.acceptVotes, 2u);  // Both ACCEPT
        }
    }
}

/**
 * **Property: Vote for Wrong Block Rejected**
 * 
 * *For any* vote that references a different block hash than the
 * current proposal, the vote SHALL be rejected.
 * 
 * **Validates: Requirements 2a.5**
 */
BOOST_AUTO_TEST_CASE(property_vote_for_wrong_block_rejected)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        TestSequencer seq(100);
        
        // Register test sequencer weight
        consensus.SetTestSequencerWeight(seq.address, seq.weight);
        
        CKey proposerKey = RandomKey();
        l2::L2BlockProposal proposal = RandomProposal(proposerKey);
        consensus.ProposeBlock(proposal);
        
        // Create vote for a different block
        uint256 wrongBlockHash = RandomUint256();
        l2::SequencerVote vote = CreateVote(wrongBlockHash, seq.key, l2::VoteType::ACCEPT, proposal.slotNumber);
        
        bool accepted = consensus.ProcessVote(vote);
        BOOST_CHECK_MESSAGE(!accepted, "Vote for wrong block should be rejected (iteration " << iteration << ")");
    }
}

/**
 * **Property: Consensus State Transitions**
 * 
 * *For any* consensus process, the state SHALL transition correctly:
 * WAITING_FOR_PROPOSAL -> COLLECTING_VOTES -> CONSENSUS_REACHED/FAILED
 * 
 * **Validates: Requirements 2a.5, 2a.6**
 */
BOOST_AUTO_TEST_CASE(property_consensus_state_transitions)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        // Initial state
        BOOST_CHECK(consensus.GetState() == l2::ConsensusState::WAITING_FOR_PROPOSAL);
        
        // After proposal
        CKey proposerKey = RandomKey();
        l2::L2BlockProposal proposal = RandomProposal(proposerKey);
        consensus.ProposeBlock(proposal);
        
        BOOST_CHECK(consensus.GetState() == l2::ConsensusState::COLLECTING_VOTES);
        
        // After clear
        consensus.Clear();
        BOOST_CHECK(consensus.GetState() == l2::ConsensusState::WAITING_FOR_PROPOSAL);
    }
}

/**
 * **Property: Abstain Votes Don't Count Toward Threshold**
 * 
 * *For any* set of votes including ABSTAIN, the abstain votes SHALL
 * not count toward either ACCEPT or REJECT percentages.
 * 
 * **Validates: Requirements 2a.5**
 */
BOOST_AUTO_TEST_CASE(property_abstain_votes_neutral)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::SequencerConsensus consensus(1);
        
        // Create 4 sequencers with equal weight
        std::vector<TestSequencer> sequencers;
        for (int i = 0; i < 4; ++i) {
            sequencers.emplace_back(100);
        }
        
        // Register test sequencer weights
        for (const auto& seq : sequencers) {
            consensus.SetTestSequencerWeight(seq.address, seq.weight);
        }
        
        CKey proposerKey = RandomKey();
        l2::L2BlockProposal proposal = RandomProposal(proposerKey);
        consensus.ProposeBlock(proposal);
        uint256 blockHash = proposal.GetHash();
        
        // 2 ACCEPT, 1 REJECT, 1 ABSTAIN
        // Total weight = 400
        // Accept weight = 200 (50%)
        // Reject weight = 100 (25%)
        // Abstain weight = 100 (25%)
        consensus.ProcessVote(CreateVote(blockHash, sequencers[0].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[1].key, l2::VoteType::ACCEPT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[2].key, l2::VoteType::REJECT, proposal.slotNumber));
        consensus.ProcessVote(CreateVote(blockHash, sequencers[3].key, l2::VoteType::ABSTAIN, proposal.slotNumber));
        
        l2::ConsensusResult result = consensus.CalculateWeightedVotes(blockHash);
        
        // Verify counts
        BOOST_CHECK_EQUAL(result.acceptVotes, 2u);
        BOOST_CHECK_EQUAL(result.rejectVotes, 1u);
        BOOST_CHECK_EQUAL(result.abstainVotes, 1u);
        BOOST_CHECK_EQUAL(result.totalVoters, 4u);
        
        // Accept percentage should be 50% (200/400)
        BOOST_CHECK_CLOSE(result.weightedAcceptPercent, 0.5, 0.01);
        
        // Should NOT reach consensus (50% < 67%)
        BOOST_CHECK_MESSAGE(!result.consensusReached,
            "50% ACCEPT should not reach consensus (iteration " << iteration << ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
