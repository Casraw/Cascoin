// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_SEQUENCER_CONSENSUS_H
#define CASCOIN_L2_SEQUENCER_CONSENSUS_H

/**
 * @file sequencer_consensus.h
 * @brief Sequencer Consensus Protocol for Cascoin L2
 * 
 * This file implements the consensus protocol for L2 block production.
 * The leader proposes blocks and other sequencers vote to achieve
 * 2/3+ consensus before finalization.
 * 
 * Requirements: 2a.3, 2a.4, 2a.5, 2a.6, 22.1
 */

#include <l2/l2_common.h>
#include <l2/sequencer_discovery.h>
#include <uint256.h>
#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <chrono>

namespace l2 {

/**
 * @brief Block proposal from the leader sequencer
 * 
 * Contains all information needed for other sequencers to validate
 * and vote on a proposed L2 block.
 * 
 * Requirements: 2a.4
 */
struct L2BlockProposal {
    /** L2 block number */
    uint64_t blockNumber = 0;
    
    /** Hash of the parent block */
    uint256 parentHash;
    
    /** State root after applying all transactions */
    uint256 stateRoot;
    
    /** Merkle root of transactions */
    uint256 transactionsRoot;
    
    /** Transaction hashes included in this block */
    std::vector<uint256> transactionHashes;
    
    /** Address of the proposing sequencer (leader) */
    uint160 proposerAddress;
    
    /** Block timestamp (Unix time) */
    uint64_t timestamp = 0;
    
    /** Signature of the proposer */
    std::vector<unsigned char> proposerSignature;
    
    /** L2 chain ID */
    uint64_t l2ChainId = DEFAULT_L2_CHAIN_ID;
    
    /** Gas limit for this block */
    uint64_t gasLimit = 30000000;  // 30M gas default
    
    /** Gas used by transactions */
    uint64_t gasUsed = 0;
    
    /** Slot number this proposal is for */
    uint64_t slotNumber = 0;

    L2BlockProposal() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockNumber);
        READWRITE(parentHash);
        READWRITE(stateRoot);
        READWRITE(transactionsRoot);
        READWRITE(transactionHashes);
        READWRITE(proposerAddress);
        READWRITE(timestamp);
        READWRITE(proposerSignature);
        READWRITE(l2ChainId);
        READWRITE(gasLimit);
        READWRITE(gasUsed);
        READWRITE(slotNumber);
    }

    /**
     * @brief Get the hash of this block proposal
     * @return SHA256 hash of the block header
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << blockNumber;
        ss << parentHash;
        ss << stateRoot;
        ss << transactionsRoot;
        ss << proposerAddress;
        ss << timestamp;
        ss << l2ChainId;
        ss << gasLimit;
        ss << gasUsed;
        ss << slotNumber;
        return ss.GetHash();
    }

    /**
     * @brief Get the hash for signing (excludes signature)
     * @return Hash to be signed by proposer
     */
    uint256 GetSigningHash() const {
        return GetHash();
    }

    /**
     * @brief Sign the proposal with a private key
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key) {
        uint256 hash = GetSigningHash();
        return key.Sign(hash, proposerSignature);
    }

    /**
     * @brief Verify the proposal signature
     * @param pubkey The public key to verify against
     * @return true if signature is valid
     */
    bool VerifySignature(const CPubKey& pubkey) const {
        if (proposerSignature.empty()) {
            return false;
        }
        uint256 hash = GetSigningHash();
        return pubkey.Verify(hash, proposerSignature);
    }

    /**
     * @brief Validate basic structure of the proposal
     * @return true if structure is valid
     */
    bool ValidateStructure() const {
        // Block number must be positive (except genesis)
        if (blockNumber > 0 && parentHash.IsNull()) {
            return false;
        }
        
        // Timestamp must be reasonable
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (timestamp > now + 60) {  // Max 60 seconds in future
            return false;
        }
        
        // Gas used cannot exceed gas limit
        if (gasUsed > gasLimit) {
            return false;
        }
        
        // Proposer address must be set
        if (proposerAddress.IsNull()) {
            return false;
        }
        
        return true;
    }
};

/**
 * @brief Vote on a block proposal from a sequencer
 * 
 * Sequencers vote ACCEPT, REJECT, or ABSTAIN on proposed blocks.
 * 2/3+ ACCEPT votes are required for consensus.
 * 
 * Requirements: 2a.4, 2a.5
 */
struct SequencerVote {
    /** Hash of the block being voted on */
    uint256 blockHash;
    
    /** Address of the voting sequencer */
    uint160 voterAddress;
    
    /** Vote type: ACCEPT, REJECT, or ABSTAIN */
    VoteType vote = VoteType::ABSTAIN;
    
    /** Reason for rejection (if vote is REJECT) */
    std::string rejectReason;
    
    /** Signature of the voter */
    std::vector<unsigned char> signature;
    
    /** Timestamp of the vote */
    uint64_t timestamp = 0;
    
    /** Slot number this vote is for */
    uint64_t slotNumber = 0;

    SequencerVote() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHash);
        READWRITE(voterAddress);
        uint8_t voteVal = static_cast<uint8_t>(vote);
        READWRITE(voteVal);
        if (ser_action.ForRead()) {
            vote = static_cast<VoteType>(voteVal);
        }
        READWRITE(rejectReason);
        READWRITE(signature);
        READWRITE(timestamp);
        READWRITE(slotNumber);
    }

    /**
     * @brief Get the hash for signing
     * @return Hash to be signed by voter
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << blockHash;
        ss << voterAddress;
        ss << static_cast<uint8_t>(vote);
        ss << rejectReason;
        ss << timestamp;
        ss << slotNumber;
        return ss.GetHash();
    }

    /**
     * @brief Sign the vote with a private key
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key) {
        uint256 hash = GetSigningHash();
        return key.Sign(hash, signature);
    }

    /**
     * @brief Verify the vote signature
     * @param pubkey The public key to verify against
     * @return true if signature is valid
     */
    bool VerifySignature(const CPubKey& pubkey) const {
        if (signature.empty()) {
            return false;
        }
        uint256 hash = GetSigningHash();
        return pubkey.Verify(hash, signature);
    }

    /**
     * @brief Check if this is an accept vote
     */
    bool IsAccept() const { return vote == VoteType::ACCEPT; }
    
    /**
     * @brief Check if this is a reject vote
     */
    bool IsReject() const { return vote == VoteType::REJECT; }
    
    /**
     * @brief Check if this is an abstain vote
     */
    bool IsAbstain() const { return vote == VoteType::ABSTAIN; }
};


/**
 * @brief Result of consensus voting
 */
struct ConsensusResult {
    /** Block hash that was voted on */
    uint256 blockHash;
    
    /** Whether consensus was reached */
    bool consensusReached = false;
    
    /** Total number of eligible voters */
    uint32_t totalVoters = 0;
    
    /** Number of ACCEPT votes */
    uint32_t acceptVotes = 0;
    
    /** Number of REJECT votes */
    uint32_t rejectVotes = 0;
    
    /** Number of ABSTAIN votes */
    uint32_t abstainVotes = 0;
    
    /** Weighted accept percentage (0.0 - 1.0) */
    double weightedAcceptPercent = 0.0;
    
    /** Weighted reject percentage (0.0 - 1.0) */
    double weightedRejectPercent = 0.0;
    
    /** Timestamp when consensus was determined */
    uint64_t timestamp = 0;

    ConsensusResult() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHash);
        READWRITE(consensusReached);
        READWRITE(totalVoters);
        READWRITE(acceptVotes);
        READWRITE(rejectVotes);
        READWRITE(abstainVotes);
        // Note: doubles are not directly serializable, convert to uint64
        uint64_t acceptPct = static_cast<uint64_t>(weightedAcceptPercent * 1000000);
        uint64_t rejectPct = static_cast<uint64_t>(weightedRejectPercent * 1000000);
        READWRITE(acceptPct);
        READWRITE(rejectPct);
        if (ser_action.ForRead()) {
            weightedAcceptPercent = static_cast<double>(acceptPct) / 1000000.0;
            weightedRejectPercent = static_cast<double>(rejectPct) / 1000000.0;
        }
        READWRITE(timestamp);
    }
};

/**
 * @brief Finalized L2 block with consensus signatures
 * 
 * Note: This is a consensus-specific block structure used during the
 * voting process. For the full L2 block structure, see l2_block.h.
 */
struct ConsensusBlock {
    /** The original proposal */
    L2BlockProposal proposal;
    
    /** Signatures from sequencers who voted ACCEPT */
    std::vector<SequencerVote> acceptVotes;
    
    /** Consensus result */
    ConsensusResult consensusResult;
    
    /** Whether this block is finalized */
    bool isFinalized = false;

    ConsensusBlock() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(proposal);
        READWRITE(acceptVotes);
        READWRITE(consensusResult);
        READWRITE(isFinalized);
    }

    /**
     * @brief Get the block hash
     */
    uint256 GetHash() const {
        return proposal.GetHash();
    }

    /**
     * @brief Get the block number
     */
    uint64_t GetBlockNumber() const {
        return proposal.blockNumber;
    }
};

/**
 * @brief Sequencer Consensus Protocol Manager
 * 
 * Manages the consensus process for L2 block production.
 * The leader proposes blocks and other sequencers vote.
 * Consensus is reached when 2/3+ of weighted votes are ACCEPT.
 * 
 * Requirements: 2a.3, 2a.4, 2a.5, 2a.6, 22.1
 */
class SequencerConsensus {
public:
    /** Callback type for consensus reached notifications */
    using ConsensusCallback = std::function<void(const ConsensusBlock&)>;
    
    /** Callback type for consensus failed notifications */
    using ConsensusFailedCallback = std::function<void(const uint256&, const std::string&)>;

    /**
     * @brief Construct a new Sequencer Consensus instance
     * @param chainId The L2 chain ID
     */
    explicit SequencerConsensus(uint64_t chainId);

    /**
     * @brief Propose a new L2 block (leader only)
     * 
     * Creates and broadcasts a block proposal to other sequencers.
     * Only the current leader should call this method.
     * 
     * @param proposal The block proposal
     * @return true if proposal was accepted and broadcast
     * 
     * Requirements: 2a.3, 2a.4
     */
    bool ProposeBlock(const L2BlockProposal& proposal);

    /**
     * @brief Vote on a received block proposal
     * 
     * Validates the proposal and creates a vote. The vote is
     * automatically broadcast to other sequencers.
     * 
     * @param proposal The proposal to vote on
     * @param signingKey Key to sign the vote
     * @return The vote that was created
     * 
     * Requirements: 2a.4, 2a.5
     */
    SequencerVote VoteOnProposal(const L2BlockProposal& proposal, const CKey& signingKey);

    /**
     * @brief Process an incoming vote from another sequencer
     * 
     * Validates and records the vote. Checks if consensus is reached.
     * 
     * @param vote The vote to process
     * @return true if vote was accepted
     * 
     * Requirements: 2a.5
     */
    bool ProcessVote(const SequencerVote& vote);

    /**
     * @brief Check if consensus has been reached for a block
     * 
     * Consensus requires 2/3+ of weighted votes to be ACCEPT.
     * 
     * @param blockHash The block hash to check
     * @return true if consensus is reached
     * 
     * Requirements: 2a.5
     */
    bool HasConsensus(const uint256& blockHash) const;

    /**
     * @brief Get the finalized block if consensus was reached
     * @param blockHash The block hash
     * @return The finalized block, or nullopt if not finalized
     */
    std::optional<ConsensusBlock> GetFinalizedBlock(const uint256& blockHash) const;

    /**
     * @brief Handle consensus failure for a block
     * 
     * Called when consensus cannot be reached (timeout or too many rejects).
     * Triggers failover to next sequencer.
     * 
     * @param blockHash The block that failed consensus
     * 
     * Requirements: 2a.6
     */
    void HandleConsensusFailed(const uint256& blockHash);

    /**
     * @brief Get the current consensus state
     * @return Current ConsensusState
     */
    ConsensusState GetState() const;

    /**
     * @brief Get the current proposal being voted on
     * @return Current proposal, or nullopt if none
     */
    std::optional<L2BlockProposal> GetCurrentProposal() const;

    /**
     * @brief Get all votes for a block
     * @param blockHash The block hash
     * @return Map of voter address to vote
     */
    std::map<uint160, SequencerVote> GetVotes(const uint256& blockHash) const;

    /**
     * @brief Calculate weighted vote percentages
     * @param blockHash The block hash
     * @return ConsensusResult with vote counts and percentages
     */
    ConsensusResult CalculateWeightedVotes(const uint256& blockHash) const;

    /**
     * @brief Register callback for consensus reached
     * @param callback Function to call when consensus is reached
     */
    void RegisterConsensusCallback(ConsensusCallback callback);

    /**
     * @brief Register callback for consensus failed
     * @param callback Function to call when consensus fails
     */
    void RegisterConsensusFailedCallback(ConsensusFailedCallback callback);

    /**
     * @brief Set the local sequencer address
     * @param address Local sequencer address
     */
    void SetLocalSequencerAddress(const uint160& address);

    /**
     * @brief Get the consensus threshold (default 0.67 = 2/3)
     * @return Threshold as decimal (0.0 - 1.0)
     */
    double GetConsensusThreshold() const { return consensusThreshold_; }

    /**
     * @brief Set the consensus threshold
     * @param threshold Threshold as decimal (0.0 - 1.0)
     */
    void SetConsensusThreshold(double threshold) { consensusThreshold_ = threshold; }

    /**
     * @brief Get the vote timeout in milliseconds
     * @return Timeout in milliseconds
     */
    uint64_t GetVoteTimeoutMs() const { return voteTimeoutMs_; }

    /**
     * @brief Set the vote timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void SetVoteTimeoutMs(uint64_t timeoutMs) { voteTimeoutMs_ = timeoutMs; }

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

    /**
     * @brief Get the L2 chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Set sequencer weight for testing
     * 
     * Allows tests to set custom weights without requiring SequencerDiscovery.
     * When test weights are set, they override the SequencerDiscovery lookup.
     * 
     * @param address Sequencer address
     * @param weight Weight value
     */
    void SetTestSequencerWeight(const uint160& address, uint64_t weight);

    /**
     * @brief Clear all test sequencer weights
     */
    void ClearTestSequencerWeights();

    /**
     * @brief Check if test mode is enabled (test weights are set)
     * @return true if test weights are being used
     */
    bool IsTestMode() const { return !testSequencerWeights_.empty(); }

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Current consensus state */
    ConsensusState state_;

    /** Current proposal being voted on */
    std::optional<L2BlockProposal> currentProposal_;

    /** Votes for current proposal (voter address -> vote) */
    std::map<uint160, SequencerVote> currentVotes_;

    /** Finalized blocks (block hash -> block) */
    std::map<uint256, ConsensusBlock> finalizedBlocks_;

    /** Proposals that failed consensus */
    std::map<uint256, std::string> failedProposals_;

    /** Local sequencer address */
    uint160 localSequencerAddress_;

    /** Whether this node is a sequencer */
    bool isLocalSequencer_;

    /** Consensus threshold (default 2/3) */
    double consensusThreshold_;

    /** Vote timeout in milliseconds */
    uint64_t voteTimeoutMs_;

    /** Time when current proposal was received */
    std::chrono::steady_clock::time_point proposalReceivedTime_;

    /** Consensus reached callbacks */
    std::vector<ConsensusCallback> consensusCallbacks_;

    /** Consensus failed callbacks */
    std::vector<ConsensusFailedCallback> consensusFailedCallbacks_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_consensus_;

    /** Test sequencer weights (address -> weight) for testing without SequencerDiscovery */
    std::map<uint160, uint64_t> testSequencerWeights_;

    /** Maximum votes to store per block */
    static constexpr size_t MAX_VOTES_PER_BLOCK = 1000;

    /** Maximum finalized blocks to cache */
    static constexpr size_t MAX_FINALIZED_BLOCKS = 100;

    /**
     * @brief Validate a block proposal
     * @param proposal The proposal to validate
     * @return true if proposal is valid
     */
    bool ValidateProposal(const L2BlockProposal& proposal) const;

    /**
     * @brief Validate a vote
     * @param vote The vote to validate
     * @return true if vote is valid
     */
    bool ValidateVote(const SequencerVote& vote) const;

    /**
     * @brief Broadcast a vote to other sequencers
     * @param vote The vote to broadcast
     */
    void BroadcastVote(const SequencerVote& vote);

    /**
     * @brief Notify callbacks that consensus was reached
     * @param block The finalized block
     */
    void NotifyConsensusReached(const ConsensusBlock& block);

    /**
     * @brief Notify callbacks that consensus failed
     * @param blockHash The block that failed
     * @param reason Reason for failure
     */
    void NotifyConsensusFailed(const uint256& blockHash, const std::string& reason);

    /**
     * @brief Finalize a block after consensus
     * @param blockHash The block to finalize
     */
    void FinalizeBlock(const uint256& blockHash);

    /**
     * @brief Get total weight of all eligible sequencers
     * @return Total weight
     */
    uint64_t GetTotalSequencerWeight() const;

    /**
     * @brief Get weight of a specific sequencer
     * @param address Sequencer address
     * @return Sequencer weight
     */
    uint64_t GetSequencerWeight(const uint160& address) const;

    /**
     * @brief Prune old finalized blocks
     */
    void PruneOldBlocks();
};

/**
 * @brief Global sequencer consensus instance
 */
SequencerConsensus& GetSequencerConsensus();

/**
 * @brief Initialize the global sequencer consensus
 * @param chainId The L2 chain ID
 */
void InitSequencerConsensus(uint64_t chainId);

/**
 * @brief Check if sequencer consensus is initialized
 * @return true if initialized
 */
bool IsSequencerConsensusInitialized();

} // namespace l2

#endif // CASCOIN_L2_SEQUENCER_CONSENSUS_H
