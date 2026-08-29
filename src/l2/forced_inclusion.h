// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_FORCED_INCLUSION_H
#define CASCOIN_L2_FORCED_INCLUSION_H

/**
 * @file forced_inclusion.h
 * @brief Forced Transaction Inclusion System for Cascoin L2
 * 
 * This file implements the forced transaction inclusion system that enables
 * users to submit transactions directly to L1 when sequencers are censoring
 * their transactions. This provides censorship resistance for the L2 network.
 * 
 * Key features:
 * - L1 transaction submission for censored users
 * - Inclusion tracking with deadlines
 * - Sequencer slashing for ignoring forced transactions
 * - Emergency self-sequencing capability
 * 
 * Requirements: 17.1, 17.2, 17.3, 17.4, 17.5, 17.6
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <vector>
#include <optional>
#include <set>
#include <string>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Maximum time for sequencer to include forced transaction: 24 hours in seconds */
static constexpr uint64_t FORCED_INCLUSION_DEADLINE = 24 * 60 * 60;

/** Minimum bond required to submit forced transaction: 1 CAS */
static constexpr CAmount FORCED_INCLUSION_BOND = 1 * COIN;

/** Slashing amount for ignoring forced transaction: 100 CAS */
static constexpr CAmount FORCED_INCLUSION_SLASH_AMOUNT = 100 * COIN;

/** Maximum pending forced transactions per address */
static constexpr uint32_t MAX_FORCED_TX_PER_ADDRESS = 10;

/** Maximum total pending forced transactions */
static constexpr uint32_t MAX_TOTAL_FORCED_TX = 1000;

/** Censorship tracking window: 7 days in seconds */
static constexpr uint64_t CENSORSHIP_TRACKING_WINDOW = 7 * 24 * 60 * 60;

/** Threshold for repeat offender status */
static constexpr uint32_t REPEAT_OFFENDER_THRESHOLD = 3;

/** Emergency self-sequencing threshold: all sequencers censoring */
static constexpr uint32_t EMERGENCY_SELF_SEQUENCE_THRESHOLD = 3;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Status of a forced inclusion request
 */
enum class ForcedInclusionStatus : uint8_t {
    PENDING = 0,        // Waiting for inclusion
    INCLUDED = 1,       // Successfully included in L2 block
    EXPIRED = 2,        // Deadline passed without inclusion
    SLASHED = 3,        // Sequencer slashed for ignoring
    CANCELLED = 4,      // Cancelled by user
    INVALID = 5         // Transaction was invalid
};

inline std::ostream& operator<<(std::ostream& os, ForcedInclusionStatus status) {
    switch (status) {
        case ForcedInclusionStatus::PENDING: return os << "PENDING";
        case ForcedInclusionStatus::INCLUDED: return os << "INCLUDED";
        case ForcedInclusionStatus::EXPIRED: return os << "EXPIRED";
        case ForcedInclusionStatus::SLASHED: return os << "SLASHED";
        case ForcedInclusionStatus::CANCELLED: return os << "CANCELLED";
        case ForcedInclusionStatus::INVALID: return os << "INVALID";
        default: return os << "UNKNOWN";
    }
}

/**
 * @brief Forced inclusion request submitted via L1
 * 
 * When a user's transactions are being censored by sequencers, they can
 * submit the transaction directly to the L1 Bridge Contract. Sequencers
 * are then required to include this transaction within 24 hours or face
 * slashing.
 * 
 * Requirement 17.1: Enable users to submit transactions directly to L1 Bridge Contract
 */
struct ForcedInclusionRequest {
    /** Unique identifier for this request */
    uint256 requestId;
    
    /** L1 transaction hash that submitted this request */
    uint256 l1TxHash;
    
    /** L1 block number where request was submitted */
    uint64_t l1BlockNumber;
    
    /** Address that submitted the forced transaction */
    uint160 submitter;
    
    /** Target address on L2 */
    uint160 target;
    
    /** Value to transfer (in satoshis) */
    CAmount value;
    
    /** Transaction data (for contract calls) */
    std::vector<unsigned char> data;
    
    /** Gas limit for the transaction */
    uint64_t gasLimit;
    
    /** Maximum gas price willing to pay */
    uint256 maxGasPrice;
    
    /** Nonce for the transaction */
    uint64_t nonce;
    
    /** Bond amount deposited */
    CAmount bondAmount;
    
    /** Timestamp when request was submitted */
    uint64_t submittedAt;
    
    /** Deadline for inclusion (submittedAt + 24 hours) */
    uint64_t deadline;
    
    /** Current status */
    ForcedInclusionStatus status;
    
    /** L2 block number where included (if status == INCLUDED) */
    uint64_t includedInBlock;
    
    /** L2 transaction hash (if included) */
    uint256 l2TxHash;
    
    /** Sequencer address responsible for inclusion */
    uint160 assignedSequencer;
    
    /** L2 chain ID */
    uint64_t l2ChainId;

    ForcedInclusionRequest()
        : l1BlockNumber(0)
        , value(0)
        , gasLimit(0)
        , nonce(0)
        , bondAmount(0)
        , submittedAt(0)
        , deadline(0)
        , status(ForcedInclusionStatus::PENDING)
        , includedInBlock(0)
        , l2ChainId(DEFAULT_L2_CHAIN_ID)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(requestId);
        READWRITE(l1TxHash);
        READWRITE(l1BlockNumber);
        READWRITE(submitter);
        READWRITE(target);
        READWRITE(value);
        READWRITE(data);
        READWRITE(gasLimit);
        READWRITE(maxGasPrice);
        READWRITE(nonce);
        READWRITE(bondAmount);
        READWRITE(submittedAt);
        READWRITE(deadline);
        uint8_t statusByte = static_cast<uint8_t>(status);
        READWRITE(statusByte);
        if (ser_action.ForRead()) {
            status = static_cast<ForcedInclusionStatus>(statusByte);
        }
        READWRITE(includedInBlock);
        READWRITE(l2TxHash);
        READWRITE(assignedSequencer);
        READWRITE(l2ChainId);
    }

    /** Compute unique hash for this request */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << requestId << l1TxHash << l1BlockNumber << submitter << target;
        ss << value << data << gasLimit << nonce << submittedAt;
        return ss.GetHash();
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& bytes) {
        if (bytes.empty()) return false;
        try {
            CDataStream ss(bytes, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /** Check if request has expired */
    bool IsExpired(uint64_t currentTime) const {
        return currentTime > deadline && status == ForcedInclusionStatus::PENDING;
    }

    /** Check if request is still pending */
    bool IsPending() const {
        return status == ForcedInclusionStatus::PENDING;
    }

    /** Validate request structure */
    bool ValidateStructure() const {
        if (requestId.IsNull()) return false;
        if (submitter.IsNull()) return false;
        if (bondAmount < FORCED_INCLUSION_BOND) return false;
        if (gasLimit == 0) return false;
        if (deadline <= submittedAt) return false;
        return true;
    }

    bool operator==(const ForcedInclusionRequest& other) const {
        return requestId == other.requestId &&
               l1TxHash == other.l1TxHash &&
               submitter == other.submitter &&
               target == other.target &&
               value == other.value &&
               nonce == other.nonce;
    }
};

/**
 * @brief Record of sequencer censorship incident
 */
struct CensorshipIncident {
    /** Sequencer address that censored */
    uint160 sequencerAddress;
    
    /** Request ID that was ignored */
    uint256 requestId;
    
    /** Timestamp when incident was recorded */
    uint64_t timestamp;
    
    /** Amount slashed */
    CAmount slashedAmount;
    
    /** Whether sequencer was slashed */
    bool wasSlashed;

    CensorshipIncident()
        : timestamp(0)
        , slashedAmount(0)
        , wasSlashed(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(requestId);
        READWRITE(timestamp);
        READWRITE(slashedAmount);
        READWRITE(wasSlashed);
    }
};

/**
 * @brief Statistics for a sequencer's forced inclusion handling
 */
struct SequencerInclusionStats {
    /** Total forced transactions assigned */
    uint64_t totalAssigned;
    
    /** Successfully included on time */
    uint64_t includedOnTime;
    
    /** Missed deadlines (slashed) */
    uint64_t missedDeadlines;
    
    /** Total amount slashed */
    CAmount totalSlashed;
    
    /** Whether sequencer is a repeat offender */
    bool isRepeatOffender;
    
    /** Last incident timestamp */
    uint64_t lastIncidentAt;

    SequencerInclusionStats()
        : totalAssigned(0)
        , includedOnTime(0)
        , missedDeadlines(0)
        , totalSlashed(0)
        , isRepeatOffender(false)
        , lastIncidentAt(0)
    {}

    /** Calculate inclusion rate (0-100) */
    uint32_t GetInclusionRate() const {
        if (totalAssigned == 0) return 100;
        return static_cast<uint32_t>((includedOnTime * 100) / totalAssigned);
    }
};

/**
 * @brief Result of processing an expired forced inclusion
 */
struct ForcedInclusionResult {
    /** Request ID */
    uint256 requestId;
    
    /** Final status */
    ForcedInclusionStatus finalStatus;
    
    /** Sequencer that was slashed (if any) */
    uint160 slashedSequencer;
    
    /** Amount slashed */
    CAmount slashedAmount;
    
    /** Bond returned to submitter */
    CAmount bondReturned;
    
    /** Error message (if any) */
    std::string error;

    ForcedInclusionResult()
        : finalStatus(ForcedInclusionStatus::PENDING)
        , slashedAmount(0)
        , bondReturned(0)
    {}
};

// ============================================================================
// Forced Inclusion System Class
// ============================================================================

/**
 * @brief Forced Transaction Inclusion System
 * 
 * Manages forced transaction inclusion requests, tracks deadlines,
 * and handles sequencer slashing for censorship.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 17.1, 17.2, 17.3, 17.4, 17.5, 17.6
 */
class ForcedInclusionSystem {
public:
    /**
     * @brief Construct a new Forced Inclusion System
     * @param chainId The L2 chain ID
     */
    explicit ForcedInclusionSystem(uint64_t chainId);

    // =========================================================================
    // L1 Transaction Submission (Requirement 17.1)
    // =========================================================================

    /**
     * @brief Submit a forced inclusion request from L1
     * @param l1TxHash L1 transaction hash
     * @param l1BlockNumber L1 block number
     * @param submitter Address submitting the request
     * @param target Target address on L2
     * @param value Value to transfer
     * @param data Transaction data
     * @param gasLimit Gas limit
     * @param maxGasPrice Maximum gas price
     * @param nonce Transaction nonce
     * @param bondAmount Bond deposited
     * @param currentTime Current timestamp
     * @return The created request, or nullopt if failed
     * 
     * Requirement 17.1: Enable users to submit transactions directly to L1 Bridge Contract
     */
    std::optional<ForcedInclusionRequest> SubmitForcedTransaction(
        const uint256& l1TxHash,
        uint64_t l1BlockNumber,
        const uint160& submitter,
        const uint160& target,
        CAmount value,
        const std::vector<unsigned char>& data,
        uint64_t gasLimit,
        const uint256& maxGasPrice,
        uint64_t nonce,
        CAmount bondAmount,
        uint64_t currentTime);

    /**
     * @brief Get a forced inclusion request by ID
     * @param requestId The request identifier
     * @return The request if found
     */
    std::optional<ForcedInclusionRequest> GetRequest(const uint256& requestId) const;

    /**
     * @brief Get all pending requests for a submitter
     * @param submitter The submitter address
     * @return Vector of pending requests
     */
    std::vector<ForcedInclusionRequest> GetPendingRequests(const uint160& submitter) const;

    /**
     * @brief Get all pending requests that need inclusion
     * @return Vector of all pending requests
     */
    std::vector<ForcedInclusionRequest> GetAllPendingRequests() const;

    // =========================================================================
    // Inclusion Tracking (Requirement 17.2)
    // =========================================================================

    /**
     * @brief Mark a forced transaction as included in L2 block
     * @param requestId The request identifier
     * @param l2BlockNumber L2 block where included
     * @param l2TxHash L2 transaction hash
     * @param currentTime Current timestamp
     * @return true if successfully marked as included
     * 
     * Requirement 17.2: Force sequencers to include L1-submitted transactions within 24 hours
     */
    bool MarkAsIncluded(
        const uint256& requestId,
        uint64_t l2BlockNumber,
        const uint256& l2TxHash,
        uint64_t currentTime);

    /**
     * @brief Assign a sequencer to handle a forced transaction
     * @param requestId The request identifier
     * @param sequencer The sequencer address
     * @return true if successfully assigned
     */
    bool AssignSequencer(const uint256& requestId, const uint160& sequencer);

    /**
     * @brief Check if a request has expired
     * @param requestId The request identifier
     * @param currentTime Current timestamp
     * @return true if expired
     */
    bool IsRequestExpired(const uint256& requestId, uint64_t currentTime) const;

    /**
     * @brief Get time remaining until deadline
     * @param requestId The request identifier
     * @param currentTime Current timestamp
     * @return Seconds remaining, or 0 if expired
     */
    uint64_t GetTimeRemaining(const uint256& requestId, uint64_t currentTime) const;

    // =========================================================================
    // Sequencer Slashing (Requirement 17.3)
    // =========================================================================

    /**
     * @brief Process expired requests and slash sequencers
     * @param currentTime Current timestamp
     * @return Vector of results for processed requests
     * 
     * Requirement 17.3: If sequencer ignores forced transaction, slash sequencer and include via L1
     */
    std::vector<ForcedInclusionResult> ProcessExpiredRequests(uint64_t currentTime);

    /**
     * @brief Slash a sequencer for ignoring forced transaction
     * @param sequencer Sequencer address
     * @param requestId Request that was ignored
     * @param currentTime Current timestamp
     * @return Amount slashed
     */
    CAmount SlashSequencer(
        const uint160& sequencer,
        const uint256& requestId,
        uint64_t currentTime);

    /**
     * @brief Get sequencer inclusion statistics
     * @param sequencer Sequencer address
     * @return Statistics for the sequencer
     */
    SequencerInclusionStats GetSequencerStats(const uint160& sequencer) const;

    /**
     * @brief Check if sequencer is a repeat offender
     * @param sequencer Sequencer address
     * @return true if repeat offender
     */
    bool IsRepeatOffender(const uint160& sequencer) const;

    // =========================================================================
    // Censorship Tracking (Requirement 17.5)
    // =========================================================================

    /**
     * @brief Record a censorship incident
     * @param sequencer Sequencer that censored
     * @param requestId Request that was censored
     * @param currentTime Current timestamp
     */
    void RecordCensorshipIncident(
        const uint160& sequencer,
        const uint256& requestId,
        uint64_t currentTime);

    /**
     * @brief Get censorship incidents for a sequencer
     * @param sequencer Sequencer address
     * @return Vector of incidents
     */
    std::vector<CensorshipIncident> GetCensorshipIncidents(const uint160& sequencer) const;

    /**
     * @brief Get total censorship incidents in tracking window
     * @param sequencer Sequencer address
     * @param currentTime Current timestamp
     * @return Number of incidents in window
     */
    uint32_t GetRecentCensorshipCount(const uint160& sequencer, uint64_t currentTime) const;

    // =========================================================================
    // Emergency Self-Sequencing (Requirement 17.6)
    // =========================================================================

    /**
     * @brief Check if emergency self-sequencing is needed
     * @param currentTime Current timestamp
     * @return true if all sequencers are censoring
     * 
     * Requirement 17.6: Enable emergency self-sequencing if all sequencers censor
     */
    bool IsEmergencySelfSequencingNeeded(uint64_t currentTime) const;

    /**
     * @brief Get requests eligible for emergency self-sequencing
     * @param currentTime Current timestamp
     * @return Vector of requests that can be self-sequenced
     */
    std::vector<ForcedInclusionRequest> GetEmergencySelfSequenceRequests(uint64_t currentTime) const;

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Get total pending request count
     * @return Number of pending requests
     */
    size_t GetPendingRequestCount() const;

    /**
     * @brief Get total request count (all statuses)
     * @return Total request count
     */
    size_t GetTotalRequestCount() const;

    /**
     * @brief Get total bonds held
     * @return Total bond amount
     */
    CAmount GetTotalBondsHeld() const;

    /**
     * @brief Get total amount slashed
     * @return Total slashed amount
     */
    CAmount GetTotalSlashed() const;

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

    /**
     * @brief Set sequencer stake for slashing calculations
     * @param sequencer Sequencer address
     * @param stake Stake amount
     */
    void SetSequencerStake(const uint160& sequencer, CAmount stake);

    /**
     * @brief Get sequencer stake
     * @param sequencer Sequencer address
     * @return Stake amount
     */
    CAmount GetSequencerStake(const uint160& sequencer) const;

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** All forced inclusion requests (requestId -> request) */
    std::map<uint256, ForcedInclusionRequest> requests_;

    /** Pending requests by submitter (submitter -> set of requestIds) */
    std::map<uint160, std::set<uint256>> pendingBySubmitter_;

    /** Sequencer statistics (sequencer -> stats) */
    std::map<uint160, SequencerInclusionStats> sequencerStats_;

    /** Censorship incidents (sequencer -> incidents) */
    std::map<uint160, std::vector<CensorshipIncident>> censorshipIncidents_;

    /** Sequencer stakes for slashing */
    std::map<uint160, CAmount> sequencerStakes_;

    /** Total bonds held */
    CAmount totalBondsHeld_;

    /** Total amount slashed */
    CAmount totalSlashed_;

    /** Next request ID counter */
    uint64_t nextRequestId_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_forced_;

    /**
     * @brief Generate a unique request ID
     * @param submitter Submitter address
     * @param l1TxHash L1 transaction hash
     * @param timestamp Current timestamp
     * @return Unique request ID
     */
    uint256 GenerateRequestId(
        const uint160& submitter,
        const uint256& l1TxHash,
        uint64_t timestamp) const;

    /**
     * @brief Calculate slashing amount based on sequencer stake
     * @param sequencer Sequencer address
     * @return Amount to slash
     */
    CAmount CalculateSlashingAmount(const uint160& sequencer) const;

    /**
     * @brief Update sequencer statistics after inclusion
     * @param sequencer Sequencer address
     * @param onTime Whether included on time
     * @param slashedAmount Amount slashed (if any)
     */
    void UpdateSequencerStats(
        const uint160& sequencer,
        bool onTime,
        CAmount slashedAmount);

    /**
     * @brief Clean up old censorship incidents outside tracking window
     * @param currentTime Current timestamp
     */
    void CleanupOldIncidents(uint64_t currentTime);
};

} // namespace l2

#endif // CASCOIN_L2_FORCED_INCLUSION_H
