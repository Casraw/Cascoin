// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file sequencer_consensus.cpp
 * @brief Implementation of Sequencer Consensus Protocol
 * 
 * Requirements: 2a.3, 2a.4, 2a.5, 2a.6, 22.1
 */

#include <l2/sequencer_consensus.h>
#include <l2/sequencer_discovery.h>
#include <l2/leader_election.h>
#include <l2/l2_chainparams.h>
#include <util.h>
#include <hash.h>

#include <algorithm>
#include <chrono>

namespace l2 {

// Global sequencer consensus instance
static std::unique_ptr<SequencerConsensus> g_sequencer_consensus;
static bool g_sequencer_consensus_initialized = false;

SequencerConsensus::SequencerConsensus(uint64_t chainId)
    : chainId_(chainId)
    , state_(ConsensusState::WAITING_FOR_PROPOSAL)
    , isLocalSequencer_(false)
    , consensusThreshold_(2.0 / 3.0)  // 2/3 threshold (0.6666...)
    , voteTimeoutMs_(5000)       // 5 second timeout
{
}

bool SequencerConsensus::ProposeBlock(const L2BlockProposal& proposal)
{
    LOCK(cs_consensus_);

    LogPrint(BCLog::L2, "SequencerConsensus: Proposing block %lu with hash %s\n",
             proposal.blockNumber, proposal.GetHash().ToString());

    // Validate the proposal
    if (!ValidateProposal(proposal)) {
        LogPrint(BCLog::L2, "SequencerConsensus: Proposal validation failed\n");
        return false;
    }

    // Check if we're already processing a proposal
    if (state_ == ConsensusState::COLLECTING_VOTES && currentProposal_) {
        LogPrint(BCLog::L2, "SequencerConsensus: Already processing proposal %s\n",
                 currentProposal_->GetHash().ToString());
        return false;
    }

    // Store the proposal
    currentProposal_ = proposal;
    currentVotes_.clear();
    state_ = ConsensusState::COLLECTING_VOTES;
    proposalReceivedTime_ = std::chrono::steady_clock::now();

    LogPrint(BCLog::L2, "SequencerConsensus: Proposal accepted, collecting votes\n");

    return true;
}

SequencerVote SequencerConsensus::VoteOnProposal(const L2BlockProposal& proposal, const CKey& signingKey)
{
    LOCK(cs_consensus_);

    SequencerVote vote;
    vote.blockHash = proposal.GetHash();
    vote.voterAddress = signingKey.GetPubKey().GetID();
    vote.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    vote.slotNumber = proposal.slotNumber;

    // Validate the proposal
    if (!ValidateProposal(proposal)) {
        vote.vote = VoteType::REJECT;
        vote.rejectReason = "Invalid proposal structure";
        LogPrint(BCLog::L2, "SequencerConsensus: Voting REJECT - invalid proposal\n");
    } else {
        // Additional validation checks
        bool isValid = true;
        std::string rejectReason;

        // Check proposer is the current leader
        if (IsLeaderElectionInitialized()) {
            auto currentLeader = GetLeaderElection().GetCurrentLeader();
            if (currentLeader && currentLeader->address != proposal.proposerAddress) {
                isValid = false;
                rejectReason = "Proposer is not the current leader";
            }
        }

        // Check timestamp is not too far in the future
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (proposal.timestamp > now + 30) {
            isValid = false;
            rejectReason = "Timestamp too far in future";
        }

        // Check chain ID matches
        if (proposal.l2ChainId != chainId_) {
            isValid = false;
            rejectReason = "Chain ID mismatch";
        }

        if (isValid) {
            vote.vote = VoteType::ACCEPT;
            LogPrint(BCLog::L2, "SequencerConsensus: Voting ACCEPT for block %s\n",
                     vote.blockHash.ToString());
        } else {
            vote.vote = VoteType::REJECT;
            vote.rejectReason = rejectReason;
            LogPrint(BCLog::L2, "SequencerConsensus: Voting REJECT - %s\n", rejectReason);
        }
    }

    // Sign the vote
    if (!vote.Sign(signingKey)) {
        LogPrint(BCLog::L2, "SequencerConsensus: Failed to sign vote\n");
        vote.vote = VoteType::ABSTAIN;
        vote.rejectReason = "Failed to sign";
    }

    // Process our own vote
    ProcessVote(vote);

    // Broadcast to other sequencers
    BroadcastVote(vote);

    return vote;
}

bool SequencerConsensus::ProcessVote(const SequencerVote& vote)
{
    LOCK(cs_consensus_);

    LogPrint(BCLog::L2, "SequencerConsensus: Processing vote from %s for block %s\n",
             vote.voterAddress.ToString(), vote.blockHash.ToString());

    // Validate the vote
    if (!ValidateVote(vote)) {
        LogPrint(BCLog::L2, "SequencerConsensus: Vote validation failed\n");
        return false;
    }

    // Check if we have a current proposal matching this vote
    if (!currentProposal_ || currentProposal_->GetHash() != vote.blockHash) {
        LogPrint(BCLog::L2, "SequencerConsensus: Vote for unknown or different block\n");
        return false;
    }

    // Check if we already have a vote from this sequencer
    if (currentVotes_.count(vote.voterAddress) > 0) {
        LogPrint(BCLog::L2, "SequencerConsensus: Duplicate vote from %s\n",
                 vote.voterAddress.ToString());
        return false;
    }

    // Store the vote
    currentVotes_[vote.voterAddress] = vote;

    LogPrint(BCLog::L2, "SequencerConsensus: Vote recorded (%s), total votes: %zu\n",
             vote.IsAccept() ? "ACCEPT" : (vote.IsReject() ? "REJECT" : "ABSTAIN"),
             currentVotes_.size());

    // Check if consensus is reached
    if (HasConsensus(vote.blockHash)) {
        LogPrint(BCLog::L2, "SequencerConsensus: Consensus reached for block %s\n",
                 vote.blockHash.ToString());
        FinalizeBlock(vote.blockHash);
    } else {
        // Check if consensus is impossible (too many rejects)
        ConsensusResult result = CalculateWeightedVotes(vote.blockHash);
        if (result.weightedRejectPercent > (1.0 - consensusThreshold_)) {
            LogPrint(BCLog::L2, "SequencerConsensus: Consensus impossible, too many rejects\n");
            HandleConsensusFailed(vote.blockHash);
        }
    }

    return true;
}

bool SequencerConsensus::HasConsensus(const uint256& blockHash) const
{
    LOCK(cs_consensus_);

    // Check if already finalized
    if (finalizedBlocks_.count(blockHash) > 0) {
        return true;
    }

    // Calculate weighted votes
    ConsensusResult result = CalculateWeightedVotes(blockHash);

    // Consensus requires 2/3+ weighted ACCEPT votes
    return result.weightedAcceptPercent >= consensusThreshold_;
}

ConsensusResult SequencerConsensus::CalculateWeightedVotes(const uint256& blockHash) const
{
    LOCK(cs_consensus_);

    ConsensusResult result;
    result.blockHash = blockHash;
    result.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Get total weight of all eligible sequencers
    uint64_t totalWeight = GetTotalSequencerWeight();
    if (totalWeight == 0) {
        return result;
    }

    // Count votes and calculate weighted percentages
    uint64_t acceptWeight = 0;
    uint64_t rejectWeight = 0;
    uint64_t abstainWeight = 0;

    for (const auto& [voterAddr, vote] : currentVotes_) {
        if (vote.blockHash != blockHash) {
            continue;
        }

        uint64_t weight = GetSequencerWeight(voterAddr);
        result.totalVoters++;

        switch (vote.vote) {
            case VoteType::ACCEPT:
                result.acceptVotes++;
                acceptWeight += weight;
                break;
            case VoteType::REJECT:
                result.rejectVotes++;
                rejectWeight += weight;
                break;
            case VoteType::ABSTAIN:
                result.abstainVotes++;
                abstainWeight += weight;
                break;
        }
    }

    // Calculate weighted percentages
    result.weightedAcceptPercent = static_cast<double>(acceptWeight) / totalWeight;
    result.weightedRejectPercent = static_cast<double>(rejectWeight) / totalWeight;
    result.consensusReached = result.weightedAcceptPercent >= consensusThreshold_;

    return result;
}

void SequencerConsensus::HandleConsensusFailed(const uint256& blockHash)
{
    LOCK(cs_consensus_);

    LogPrint(BCLog::L2, "SequencerConsensus: Consensus failed for block %s\n",
             blockHash.ToString());

    // Record the failure
    failedProposals_[blockHash] = "Consensus not reached";

    // Reset state
    state_ = ConsensusState::CONSENSUS_FAILED;
    currentProposal_.reset();
    currentVotes_.clear();

    // Notify callbacks
    NotifyConsensusFailed(blockHash, "Consensus not reached");

    // Trigger failover to next sequencer
    if (IsLeaderElectionInitialized() && currentProposal_) {
        GetLeaderElection().HandleLeaderTimeout(currentProposal_->slotNumber);
    }

    // Reset to waiting state
    state_ = ConsensusState::WAITING_FOR_PROPOSAL;
}

std::optional<ConsensusBlock> SequencerConsensus::GetFinalizedBlock(const uint256& blockHash) const
{
    LOCK(cs_consensus_);

    auto it = finalizedBlocks_.find(blockHash);
    if (it != finalizedBlocks_.end()) {
        return it->second;
    }
    return std::nullopt;
}

ConsensusState SequencerConsensus::GetState() const
{
    LOCK(cs_consensus_);
    return state_;
}

std::optional<L2BlockProposal> SequencerConsensus::GetCurrentProposal() const
{
    LOCK(cs_consensus_);
    return currentProposal_;
}

std::map<uint160, SequencerVote> SequencerConsensus::GetVotes(const uint256& blockHash) const
{
    LOCK(cs_consensus_);

    std::map<uint160, SequencerVote> votes;
    for (const auto& [addr, vote] : currentVotes_) {
        if (vote.blockHash == blockHash) {
            votes[addr] = vote;
        }
    }
    return votes;
}

void SequencerConsensus::RegisterConsensusCallback(ConsensusCallback callback)
{
    LOCK(cs_consensus_);
    consensusCallbacks_.push_back(std::move(callback));
}

void SequencerConsensus::RegisterConsensusFailedCallback(ConsensusFailedCallback callback)
{
    LOCK(cs_consensus_);
    consensusFailedCallbacks_.push_back(std::move(callback));
}

void SequencerConsensus::SetLocalSequencerAddress(const uint160& address)
{
    LOCK(cs_consensus_);
    localSequencerAddress_ = address;
    isLocalSequencer_ = !address.IsNull();
}

void SequencerConsensus::Clear()
{
    LOCK(cs_consensus_);
    state_ = ConsensusState::WAITING_FOR_PROPOSAL;
    currentProposal_.reset();
    currentVotes_.clear();
    finalizedBlocks_.clear();
    failedProposals_.clear();
    consensusCallbacks_.clear();
    consensusFailedCallbacks_.clear();
    // Note: testSequencerWeights_ is NOT cleared here to preserve test weights
    // across Clear() calls. Use ClearTestSequencerWeights() to clear them explicitly.
}

void SequencerConsensus::SetTestSequencerWeight(const uint160& address, uint64_t weight)
{
    LOCK(cs_consensus_);
    testSequencerWeights_[address] = weight;
}

void SequencerConsensus::ClearTestSequencerWeights()
{
    LOCK(cs_consensus_);
    testSequencerWeights_.clear();
}

bool SequencerConsensus::ValidateProposal(const L2BlockProposal& proposal) const
{
    // Basic structure validation
    if (!proposal.ValidateStructure()) {
        return false;
    }

    // Verify proposer signature
    if (IsSequencerDiscoveryInitialized()) {
        auto seqInfo = GetSequencerDiscovery().GetSequencerInfo(proposal.proposerAddress);
        if (seqInfo && seqInfo->pubkey.IsValid()) {
            if (!proposal.VerifySignature(seqInfo->pubkey)) {
                LogPrint(BCLog::L2, "SequencerConsensus: Invalid proposer signature\n");
                return false;
            }
        }
    }

    // Check chain ID
    if (proposal.l2ChainId != chainId_) {
        LogPrint(BCLog::L2, "SequencerConsensus: Chain ID mismatch\n");
        return false;
    }

    return true;
}

bool SequencerConsensus::ValidateVote(const SequencerVote& vote) const
{
    // Check voter is an eligible sequencer
    if (IsSequencerDiscoveryInitialized()) {
        if (!GetSequencerDiscovery().IsEligibleSequencer(vote.voterAddress)) {
            LogPrint(BCLog::L2, "SequencerConsensus: Voter is not eligible sequencer\n");
            return false;
        }

        // Verify signature
        auto seqInfo = GetSequencerDiscovery().GetSequencerInfo(vote.voterAddress);
        if (seqInfo && seqInfo->pubkey.IsValid()) {
            if (!vote.VerifySignature(seqInfo->pubkey)) {
                LogPrint(BCLog::L2, "SequencerConsensus: Invalid vote signature\n");
                return false;
            }
        }
    }

    // Check timestamp is reasonable
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (vote.timestamp > now + 60) {
        LogPrint(BCLog::L2, "SequencerConsensus: Vote timestamp too far in future\n");
        return false;
    }

    return true;
}

void SequencerConsensus::BroadcastVote(const SequencerVote& vote)
{
    // TODO: Implement P2P broadcast of vote
    // This will be implemented when P2P integration is done
    LogPrint(BCLog::L2, "SequencerConsensus: Broadcasting vote for block %s\n",
             vote.blockHash.ToString());
}

void SequencerConsensus::NotifyConsensusReached(const ConsensusBlock& block)
{
    // Note: Must be called with cs_consensus_ held
    for (const auto& callback : consensusCallbacks_) {
        try {
            callback(block);
        } catch (const std::exception& e) {
            LogPrint(BCLog::L2, "SequencerConsensus: Callback error: %s\n", e.what());
        }
    }
}

void SequencerConsensus::NotifyConsensusFailed(const uint256& blockHash, const std::string& reason)
{
    // Note: Must be called with cs_consensus_ held
    for (const auto& callback : consensusFailedCallbacks_) {
        try {
            callback(blockHash, reason);
        } catch (const std::exception& e) {
            LogPrint(BCLog::L2, "SequencerConsensus: Callback error: %s\n", e.what());
        }
    }
}

void SequencerConsensus::FinalizeBlock(const uint256& blockHash)
{
    // Note: Must be called with cs_consensus_ held

    if (!currentProposal_ || currentProposal_->GetHash() != blockHash) {
        return;
    }

    LogPrint(BCLog::L2, "SequencerConsensus: Finalizing block %s\n", blockHash.ToString());

    // Create finalized block
    ConsensusBlock block;
    block.proposal = *currentProposal_;
    block.consensusResult = CalculateWeightedVotes(blockHash);
    block.isFinalized = true;

    // Collect accept votes
    for (const auto& [addr, vote] : currentVotes_) {
        if (vote.blockHash == blockHash && vote.IsAccept()) {
            block.acceptVotes.push_back(vote);
        }
    }

    // Store finalized block
    finalizedBlocks_[blockHash] = block;

    // Update state
    state_ = ConsensusState::CONSENSUS_REACHED;

    // Notify callbacks
    NotifyConsensusReached(block);

    // Prune old blocks if needed
    PruneOldBlocks();

    // Reset for next proposal
    currentProposal_.reset();
    currentVotes_.clear();
    state_ = ConsensusState::WAITING_FOR_PROPOSAL;
}

uint64_t SequencerConsensus::GetTotalSequencerWeight() const
{
    // If test weights are set, use them instead of SequencerDiscovery
    if (!testSequencerWeights_.empty()) {
        uint64_t totalWeight = 0;
        for (const auto& [addr, weight] : testSequencerWeights_) {
            totalWeight += weight;
        }
        return totalWeight;
    }

    if (!IsSequencerDiscoveryInitialized()) {
        return 0;
    }

    uint64_t totalWeight = 0;
    auto sequencers = GetSequencerDiscovery().GetEligibleSequencers();
    for (const auto& seq : sequencers) {
        totalWeight += seq.GetWeight();
    }
    return totalWeight;
}

uint64_t SequencerConsensus::GetSequencerWeight(const uint160& address) const
{
    // If test weights are set, use them instead of SequencerDiscovery
    if (!testSequencerWeights_.empty()) {
        auto it = testSequencerWeights_.find(address);
        if (it != testSequencerWeights_.end()) {
            return it->second;
        }
        return 0;  // Unknown sequencer in test mode
    }

    if (!IsSequencerDiscoveryInitialized()) {
        return 1;  // Default weight
    }

    auto seqInfo = GetSequencerDiscovery().GetSequencerInfo(address);
    if (seqInfo) {
        return seqInfo->GetWeight();
    }
    return 0;
}

void SequencerConsensus::PruneOldBlocks()
{
    // Note: Must be called with cs_consensus_ held

    if (finalizedBlocks_.size() <= MAX_FINALIZED_BLOCKS) {
        return;
    }

    // Find oldest blocks and remove them
    std::vector<std::pair<uint64_t, uint256>> blocksByNumber;
    for (const auto& [hash, block] : finalizedBlocks_) {
        blocksByNumber.emplace_back(block.GetBlockNumber(), hash);
    }

    std::sort(blocksByNumber.begin(), blocksByNumber.end());

    size_t toRemove = finalizedBlocks_.size() - MAX_FINALIZED_BLOCKS;
    for (size_t i = 0; i < toRemove && i < blocksByNumber.size(); ++i) {
        finalizedBlocks_.erase(blocksByNumber[i].second);
    }
}

// Global instance management

SequencerConsensus& GetSequencerConsensus()
{
    assert(g_sequencer_consensus_initialized);
    return *g_sequencer_consensus;
}

void InitSequencerConsensus(uint64_t chainId)
{
    g_sequencer_consensus = std::make_unique<SequencerConsensus>(chainId);
    g_sequencer_consensus_initialized = true;
    LogPrintf("SequencerConsensus: Initialized for chain %lu\n", chainId);
}

bool IsSequencerConsensusInitialized()
{
    return g_sequencer_consensus_initialized;
}

} // namespace l2
