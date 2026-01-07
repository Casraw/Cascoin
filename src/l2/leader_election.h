// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_LEADER_ELECTION_H
#define CASCOIN_L2_LEADER_ELECTION_H

/**
 * @file leader_election.h
 * @brief Leader Election and Failover Management for Cascoin L2
 * 
 * This file implements deterministic leader election for the L2 sequencer
 * network. Leaders are elected based on slot number and sequencer weights,
 * with automatic failover when leaders fail to produce blocks.
 * 
 * Requirements: 2a.1, 2a.2, 2b.1, 2b.2, 2b.5
 */

#include <l2/l2_common.h>
#include <l2/sequencer_discovery.h>
#include <uint256.h>
#include <key.h>
#include <sync.h>
#include <hash.h>

#include <cstdint>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>

namespace l2 {

/**
 * @brief Result of a leader election
 * 
 * Contains the elected leader and backup sequencers for failover.
 */
struct LeaderElectionResult {
    /** Address of the elected leader */
    uint160 leaderAddress;
    
    /** Slot number this election is for */
    uint64_t slotNumber;
    
    /** L2 block height this leader is valid until */
    uint64_t validUntilBlock;
    
    /** Ordered list of backup sequencers for failover */
    std::vector<uint160> backupSequencers;
    
    /** Deterministic seed used for this election */
    uint256 electionSeed;
    
    /** Timestamp when this election was performed */
    uint64_t electionTimestamp;
    
    /** Whether this election result is valid */
    bool isValid;

    LeaderElectionResult()
        : slotNumber(0)
        , validUntilBlock(0)
        , electionTimestamp(0)
        , isValid(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(leaderAddress);
        READWRITE(slotNumber);
        READWRITE(validUntilBlock);
        READWRITE(backupSequencers);
        READWRITE(electionSeed);
        READWRITE(electionTimestamp);
        READWRITE(isValid);
    }

    /**
     * @brief Get the hash of this election result for verification
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << leaderAddress;
        ss << slotNumber;
        ss << validUntilBlock;
        ss << electionSeed;
        return ss.GetHash();
    }
};

/**
 * @brief Leadership claim for failover scenarios
 */
struct LeadershipClaim {
    /** Address claiming leadership */
    uint160 claimantAddress;
    
    /** Slot number being claimed */
    uint64_t slotNumber;
    
    /** Position in failover order (0 = primary, 1 = first backup, etc.) */
    uint32_t failoverPosition;
    
    /** Timestamp of the claim */
    uint64_t claimTimestamp;
    
    /** Signature proving ownership */
    std::vector<unsigned char> signature;
    
    /** Previous leader that failed (if failover) */
    uint160 previousLeader;
    
    /** Reason for claiming (timeout, explicit failure, etc.) */
    std::string claimReason;

    LeadershipClaim()
        : slotNumber(0)
        , failoverPosition(0)
        , claimTimestamp(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(claimantAddress);
        READWRITE(slotNumber);
        READWRITE(failoverPosition);
        READWRITE(claimTimestamp);
        READWRITE(signature);
        READWRITE(previousLeader);
        READWRITE(claimReason);
    }

    /**
     * @brief Get hash for signing
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << claimantAddress;
        ss << slotNumber;
        ss << failoverPosition;
        ss << claimTimestamp;
        ss << previousLeader;
        return ss.GetHash();
    }
};


/**
 * @brief Leader Election and Failover Management
 * 
 * Manages deterministic leader election for the L2 sequencer network.
 * The election algorithm ensures all nodes agree on the same leader
 * for any given slot, enabling decentralized block production.
 * 
 * Key features:
 * - Deterministic election based on slot number and random seed
 * - Weighted selection based on reputation and stake
 * - Automatic failover with deterministic backup ordering
 * - Split-brain prevention through consensus on active leader
 * 
 * Requirements: 2a.1, 2a.2, 2b.1, 2b.2, 2b.5
 */
class LeaderElection {
public:
    /** Callback type for leader change notifications */
    using LeaderChangeCallback = std::function<void(const LeaderElectionResult&)>;

    /**
     * @brief Construct a new Leader Election instance
     * @param chainId The L2 chain ID
     */
    explicit LeaderElection(uint64_t chainId);

    /**
     * @brief Perform deterministic leader election for a slot
     * 
     * Elects a leader from the eligible sequencers using a deterministic
     * algorithm based on the slot number and a random seed derived from
     * L1 block data.
     * 
     * @param slotNumber The slot to elect a leader for
     * @param eligibleSequencers List of eligible sequencers
     * @param randomSeed Deterministic seed for randomization
     * @return Election result with leader and backups
     * 
     * Requirements: 2a.1, 2a.2
     */
    LeaderElectionResult ElectLeader(
        uint64_t slotNumber,
        const std::vector<SequencerInfo>& eligibleSequencers,
        const uint256& randomSeed);

    /**
     * @brief Check if this node is the current leader
     * @return true if this node is the active leader
     */
    bool IsCurrentLeader() const;

    /**
     * @brief Get information about the current leader
     * @return Current leader's sequencer info, or nullopt if none
     */
    std::optional<SequencerInfo> GetCurrentLeader() const;

    /**
     * @brief Get the current election result
     * @return Current election result
     */
    LeaderElectionResult GetCurrentElection() const;

    /**
     * @brief Handle leader timeout (triggers failover)
     * 
     * Called when the current leader fails to produce a block
     * within the expected time. Initiates failover to the next
     * sequencer in the backup list.
     * 
     * @param slotNumber The slot where timeout occurred
     * 
     * Requirements: 2b.1, 2b.2
     */
    void HandleLeaderTimeout(uint64_t slotNumber);

    /**
     * @brief Claim leadership after timeout
     * 
     * Allows an eligible sequencer to claim leadership when the
     * current leader has failed. The claim must be valid according
     * to the failover order.
     * 
     * @param signingKey Key to sign the claim
     * @return true if claim was successful
     * 
     * Requirements: 2b.2, 2b.5
     */
    bool ClaimLeadership(const CKey& signingKey);

    /**
     * @brief Process a leadership claim from another node
     * @param claim The leadership claim to process
     * @return true if claim was accepted
     */
    bool ProcessLeadershipClaim(const LeadershipClaim& claim);

    /**
     * @brief Generate deterministic election seed from L1 data
     * 
     * Creates a seed that is deterministic across all nodes but
     * unpredictable before the L1 block is mined.
     * 
     * @param slotNumber The slot number
     * @return Deterministic seed
     */
    uint256 GenerateElectionSeed(uint64_t slotNumber) const;

    /**
     * @brief Perform weighted random selection
     * 
     * Selects a sequencer from the list using weighted random selection
     * based on reputation and stake. The selection is deterministic
     * given the same seed.
     * 
     * @param sequencers List of sequencers to select from
     * @param seed Random seed for selection
     * @return Address of selected sequencer
     */
    uint160 WeightedRandomSelect(
        const std::vector<SequencerInfo>& sequencers,
        const uint256& seed) const;

    /**
     * @brief Get the slot number for a given L2 block height
     * @param blockHeight L2 block height
     * @return Slot number
     */
    uint64_t GetSlotForBlock(uint64_t blockHeight) const;

    /**
     * @brief Get the current slot number
     * @return Current slot number
     */
    uint64_t GetCurrentSlot() const;

    /**
     * @brief Update the current L2 block height
     * @param blockHeight New block height
     */
    void UpdateBlockHeight(uint64_t blockHeight);

    /**
     * @brief Set the local sequencer address
     * @param address Local sequencer address
     */
    void SetLocalSequencerAddress(const uint160& address);

    /**
     * @brief Register callback for leader changes
     * @param callback Function to call on leader change
     */
    void RegisterLeaderChangeCallback(LeaderChangeCallback callback);

    /**
     * @brief Check if a failover is currently in progress
     * @return true if failover is in progress
     */
    bool IsFailoverInProgress() const;

    /**
     * @brief Get the failover position for an address
     * @param address Sequencer address
     * @return Position in failover order (0 = leader), or -1 if not in list
     */
    int GetFailoverPosition(const uint160& address) const;

    /**
     * @brief Resolve conflicting leadership claims
     * 
     * When multiple sequencers claim leadership, resolves using
     * highest reputation + earliest timestamp rule.
     * 
     * @param claims List of conflicting claims
     * @return The winning claim
     * 
     * Requirements: 2b.7
     */
    LeadershipClaim ResolveConflictingClaims(
        const std::vector<LeadershipClaim>& claims) const;

    /**
     * @brief Get the number of blocks per leader rotation
     * @return Blocks per leader
     */
    uint32_t GetBlocksPerLeader() const { return blocksPerLeader_; }

    /**
     * @brief Set the number of blocks per leader rotation
     * @param blocks Blocks per leader
     */
    void SetBlocksPerLeader(uint32_t blocks) { blocksPerLeader_ = blocks; }

    /**
     * @brief Get the leader timeout duration
     * @return Timeout in milliseconds
     */
    uint64_t GetLeaderTimeoutMs() const { return leaderTimeoutMs_; }

    /**
     * @brief Set the leader timeout duration
     * @param timeoutMs Timeout in milliseconds
     */
    void SetLeaderTimeoutMs(uint64_t timeoutMs) { leaderTimeoutMs_ = timeoutMs; }

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Current election result */
    LeaderElectionResult currentElection_;

    /** Current L2 block height */
    uint64_t currentBlockHeight_;

    /** Local sequencer address (if this node is a sequencer) */
    uint160 localSequencerAddress_;

    /** Whether this node is a sequencer */
    bool isLocalSequencer_;

    /** Time of last block production */
    std::chrono::steady_clock::time_point lastBlockTime_;

    /** Whether a failover is in progress */
    bool failoverInProgress_;

    /** Current failover position (which backup is being tried) */
    uint32_t currentFailoverPosition_;

    /** Pending leadership claims */
    std::vector<LeadershipClaim> pendingClaims_;

    /** Leader change callbacks */
    std::vector<LeaderChangeCallback> leaderChangeCallbacks_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_election_;

    /** Number of blocks per leader rotation (default: 10) */
    uint32_t blocksPerLeader_;

    /** Leader timeout in milliseconds (default: 3000) */
    uint64_t leaderTimeoutMs_;

    /** Maximum backup sequencers to track */
    static constexpr size_t MAX_BACKUP_SEQUENCERS = 10;

    /**
     * @brief Notify callbacks of leader change
     * @param result New election result
     */
    void NotifyLeaderChange(const LeaderElectionResult& result);

    /**
     * @brief Get current L1 block hash for seed generation
     * @return L1 block hash
     */
    uint256 GetL1BlockHash(uint64_t height) const;

    /**
     * @brief Validate a leadership claim
     * @param claim The claim to validate
     * @return true if claim is valid
     */
    bool ValidateLeadershipClaim(const LeadershipClaim& claim) const;

    /**
     * @brief Calculate total weight of sequencers
     * @param sequencers List of sequencers
     * @return Total weight
     */
    uint64_t CalculateTotalWeight(const std::vector<SequencerInfo>& sequencers) const;
};

/**
 * @brief Global leader election instance
 */
LeaderElection& GetLeaderElection();

/**
 * @brief Initialize the global leader election
 * @param chainId The L2 chain ID
 */
void InitLeaderElection(uint64_t chainId);

/**
 * @brief Check if leader election is initialized
 * @return true if initialized
 */
bool IsLeaderElectionInitialized();

} // namespace l2

#endif // CASCOIN_L2_LEADER_ELECTION_H
