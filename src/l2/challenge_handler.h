// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_CHALLENGE_HANDLER_H
#define CASCOIN_L2_CHALLENGE_HANDLER_H

/**
 * @file challenge_handler.h
 * @brief Challenge Handler for L2 Withdrawal Challenges
 * 
 * This file implements the challenge system for L2 withdrawals. It handles
 * the submission, validation, and resolution of challenges against pending
 * withdrawals, including bond management and slashing.
 * 
 * Key features:
 * - Challenge submission with bond requirement
 * - Challenge validation against fraud proofs
 * - Bond slashing for invalid challenges
 * - Challenge limits per address
 * - Priority processing for high-reputation withdrawals
 * 
 * Requirements: 4.6, 29.1, 29.2
 */

#include <l2/l2_common.h>
#include <l2/bridge_contract.h>
#include <l2/fraud_proof.h>
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

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Challenge bond required: 10 CAS (Requirement 29.1) */
static constexpr CAmount WITHDRAWAL_CHALLENGE_BOND = 10 * COIN;

/** Maximum active challenges per address (Requirement 29.3) */
static constexpr uint32_t MAX_CHALLENGES_PER_ADDRESS = 10;

/** Invalid challenge threshold for banning (Requirement 29.6) */
static constexpr uint32_t INVALID_CHALLENGE_BAN_THRESHOLD = 5;

/** Challenge resolution timeout in seconds: 24 hours */
static constexpr uint64_t CHALLENGE_RESOLUTION_TIMEOUT = 24 * 60 * 60;

/** Minimum HAT score for priority withdrawal processing */
static constexpr uint32_t PRIORITY_WITHDRAWAL_MIN_HAT = 70;

// ============================================================================
// Data Structures
// ============================================================================

/** Challenge status */
enum class ChallengeStatus : uint8_t {
    PENDING = 0,        // Challenge submitted, awaiting validation
    VALIDATING = 1,     // Challenge being validated
    VALID = 2,          // Challenge proven valid (withdrawal cancelled)
    INVALID = 3,        // Challenge proven invalid (bond slashed)
    EXPIRED = 4,        // Challenge expired without resolution
    CANCELLED = 5       // Challenge cancelled by challenger
};

inline std::ostream& operator<<(std::ostream& os, ChallengeStatus status) {
    switch (status) {
        case ChallengeStatus::PENDING: return os << "PENDING";
        case ChallengeStatus::VALIDATING: return os << "VALIDATING";
        case ChallengeStatus::VALID: return os << "VALID";
        case ChallengeStatus::INVALID: return os << "INVALID";
        case ChallengeStatus::EXPIRED: return os << "EXPIRED";
        case ChallengeStatus::CANCELLED: return os << "CANCELLED";
        default: return os << "UNKNOWN";
    }
}

/**
 * @brief Challenge submission for a withdrawal
 * 
 * Represents a challenge against a pending withdrawal. Challengers must
 * provide a bond and evidence (fraud proof) to support their challenge.
 * 
 * Requirements: 29.1, 29.2
 */
struct WithdrawalChallenge {
    /** Unique challenge identifier */
    uint256 challengeId;
    
    /** ID of the withdrawal being challenged */
    uint256 withdrawalId;
    
    /** Address of the challenger */
    uint160 challengerAddress;
    
    /** Challenge bond amount */
    CAmount bondAmount;
    
    /** Fraud proof evidence */
    std::vector<unsigned char> fraudProof;
    
    /** Challenge reason/description */
    std::string reason;
    
    /** Current status */
    ChallengeStatus status;
    
    /** Timestamp when challenge was submitted */
    uint64_t submittedAt;
    
    /** Timestamp when challenge was resolved */
    uint64_t resolvedAt;
    
    /** Resolution deadline */
    uint64_t deadline;
    
    /** L2 chain ID */
    uint64_t l2ChainId;
    
    /** Challenger's HAT score at submission time */
    uint32_t challengerHatScore;
    
    /** Whether bond has been returned/slashed */
    bool bondProcessed;

    WithdrawalChallenge()
        : bondAmount(0)
        , status(ChallengeStatus::PENDING)
        , submittedAt(0)
        , resolvedAt(0)
        , deadline(0)
        , l2ChainId(DEFAULT_L2_CHAIN_ID)
        , challengerHatScore(0)
        , bondProcessed(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(challengeId);
        READWRITE(withdrawalId);
        READWRITE(challengerAddress);
        READWRITE(bondAmount);
        READWRITE(fraudProof);
        READWRITE(reason);
        uint8_t statusByte = static_cast<uint8_t>(status);
        READWRITE(statusByte);
        if (ser_action.ForRead()) {
            status = static_cast<ChallengeStatus>(statusByte);
        }
        READWRITE(submittedAt);
        READWRITE(resolvedAt);
        READWRITE(deadline);
        READWRITE(l2ChainId);
        READWRITE(challengerHatScore);
        READWRITE(bondProcessed);
    }

    /** Compute unique hash for this challenge */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << challengeId << withdrawalId << challengerAddress 
           << bondAmount << submittedAt << l2ChainId;
        return ss.GetHash();
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /** Check if challenge has expired */
    bool IsExpired(uint64_t currentTime) const {
        return currentTime > deadline && status == ChallengeStatus::PENDING;
    }

    /** Check if challenge is still active */
    bool IsActive() const {
        return status == ChallengeStatus::PENDING || 
               status == ChallengeStatus::VALIDATING;
    }

    bool operator==(const WithdrawalChallenge& other) const {
        return challengeId == other.challengeId &&
               withdrawalId == other.withdrawalId &&
               challengerAddress == other.challengerAddress &&
               bondAmount == other.bondAmount &&
               status == other.status;
    }
};

/**
 * @brief Challenge result with details
 */
struct ChallengeResult {
    uint256 challengeId;
    ChallengeStatus finalStatus;
    bool bondSlashed;
    CAmount bondAmount;
    uint160 bondRecipient;  // Who receives the bond (challenger or challenged party)
    std::string resultReason;
    uint64_t resolvedAt;

    ChallengeResult()
        : finalStatus(ChallengeStatus::PENDING)
        , bondSlashed(false)
        , bondAmount(0)
        , resolvedAt(0)
    {}
};

/**
 * @brief Challenger statistics for tracking behavior
 */
struct ChallengerStats {
    uint160 challengerAddress;
    uint32_t totalChallenges;
    uint32_t validChallenges;
    uint32_t invalidChallenges;
    uint32_t expiredChallenges;
    CAmount totalBondsLost;
    CAmount totalBondsReturned;
    bool isBanned;
    uint64_t bannedUntil;

    ChallengerStats()
        : totalChallenges(0)
        , validChallenges(0)
        , invalidChallenges(0)
        , expiredChallenges(0)
        , totalBondsLost(0)
        , totalBondsReturned(0)
        , isBanned(false)
        , bannedUntil(0)
    {}

    /** Get success rate as percentage */
    uint32_t GetSuccessRate() const {
        if (totalChallenges == 0) return 0;
        return (validChallenges * 100) / totalChallenges;
    }

    /** Check if challenger should be banned */
    bool ShouldBeBanned() const {
        return invalidChallenges >= INVALID_CHALLENGE_BAN_THRESHOLD;
    }
};

// ============================================================================
// Challenge Handler Class
// ============================================================================

/**
 * @brief Challenge Handler for L2 Withdrawal Challenges
 * 
 * Manages the lifecycle of withdrawal challenges including submission,
 * validation, resolution, and bond management.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 4.6, 29.1, 29.2
 */
class ChallengeHandler {
public:
    /**
     * @brief Construct a new Challenge Handler
     * @param chainId The L2 chain ID
     */
    explicit ChallengeHandler(uint64_t chainId);

    // =========================================================================
    // Challenge Submission (Requirement 29.1)
    // =========================================================================

    /**
     * @brief Submit a challenge against a withdrawal
     * @param withdrawalId ID of the withdrawal to challenge
     * @param challenger Address of the challenger
     * @param bondAmount Bond amount (must be >= WITHDRAWAL_CHALLENGE_BOND)
     * @param fraudProof Evidence supporting the challenge
     * @param reason Description of why the withdrawal is invalid
     * @param currentTime Current timestamp
     * @param challengerHatScore Challenger's HAT v2 score
     * @return The created challenge, or nullopt if submission failed
     * 
     * Requirement 29.1: Require challenge bond to prevent frivolous challenges
     */
    std::optional<WithdrawalChallenge> ChallengeWithdrawal(
        const uint256& withdrawalId,
        const uint160& challenger,
        CAmount bondAmount,
        const std::vector<unsigned char>& fraudProof,
        const std::string& reason,
        uint64_t currentTime,
        uint32_t challengerHatScore = 0);

    /**
     * @brief Check if a withdrawal can be challenged
     * @param withdrawalId ID of the withdrawal
     * @param challenger Address of potential challenger
     * @return true if the withdrawal can be challenged by this address
     */
    bool CanChallengeWithdrawal(
        const uint256& withdrawalId,
        const uint160& challenger) const;

    // =========================================================================
    // Challenge Validation (Requirement 29.2)
    // =========================================================================

    /**
     * @brief Validate a challenge against a withdrawal
     * @param challengeId ID of the challenge to validate
     * @param withdrawal The withdrawal being challenged
     * @return true if the challenge is valid (withdrawal should be cancelled)
     * 
     * Requirement 29.2: Slash challenge bond if challenge is invalid
     */
    bool ValidateChallenge(
        const uint256& challengeId,
        const WithdrawalRequest& withdrawal);

    /**
     * @brief Process the result of a challenge validation
     * @param challengeId ID of the challenge
     * @param isValid Whether the challenge was valid
     * @param currentTime Current timestamp
     * @return The challenge result with bond disposition
     * 
     * Requirements 29.1, 29.2: Handle bond slashing/return
     */
    ChallengeResult ProcessChallengeResult(
        const uint256& challengeId,
        bool isValid,
        uint64_t currentTime);

    // =========================================================================
    // Challenge Management
    // =========================================================================

    /**
     * @brief Get a challenge by ID
     * @param challengeId The challenge identifier
     * @return The challenge if found
     */
    std::optional<WithdrawalChallenge> GetChallenge(const uint256& challengeId) const;

    /**
     * @brief Get all challenges for a withdrawal
     * @param withdrawalId The withdrawal identifier
     * @return Vector of challenges against this withdrawal
     */
    std::vector<WithdrawalChallenge> GetChallengesForWithdrawal(
        const uint256& withdrawalId) const;

    /**
     * @brief Get active challenges by a challenger
     * @param challenger The challenger address
     * @return Vector of active challenges by this address
     */
    std::vector<WithdrawalChallenge> GetActiveChallengesByChallenger(
        const uint160& challenger) const;

    /**
     * @brief Get the number of active challenges for an address
     * @param challenger The challenger address
     * @return Number of active challenges
     */
    uint32_t GetActiveChallengeCount(const uint160& challenger) const;

    /**
     * @brief Process expired challenges
     * @param currentTime Current timestamp
     * @return Number of challenges that expired
     */
    size_t ProcessExpiredChallenges(uint64_t currentTime);

    // =========================================================================
    // Challenger Management (Requirement 29.6)
    // =========================================================================

    /**
     * @brief Get statistics for a challenger
     * @param challenger The challenger address
     * @return Challenger statistics
     */
    ChallengerStats GetChallengerStats(const uint160& challenger) const;

    /**
     * @brief Check if a challenger is banned
     * @param challenger The challenger address
     * @param currentTime Current timestamp
     * @return true if the challenger is currently banned
     * 
     * Requirement 29.6: Ban challengers who repeatedly submit invalid challenges
     */
    bool IsChallengerBanned(const uint160& challenger, uint64_t currentTime) const;

    /**
     * @brief Ban a challenger
     * @param challenger The challenger address
     * @param duration Ban duration in seconds
     * @param currentTime Current timestamp
     */
    void BanChallenger(const uint160& challenger, uint64_t duration, uint64_t currentTime);

    // =========================================================================
    // Bond Management
    // =========================================================================

    /**
     * @brief Get total bonds held by the system
     * @return Total bond amount
     */
    CAmount GetTotalBondsHeld() const;

    /**
     * @brief Get bonds held for a specific challenger
     * @param challenger The challenger address
     * @return Total bonds held for this challenger
     */
    CAmount GetBondsHeldByChallenger(const uint160& challenger) const;

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Get total number of challenges
     * @return Challenge count
     */
    size_t GetTotalChallengeCount() const;

    /**
     * @brief Get number of active challenges
     * @return Active challenge count
     */
    size_t GetActiveChallengeCount() const;

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

    /**
     * @brief Register a withdrawal that can be challenged
     * @param withdrawalId The withdrawal ID
     * @param deadline Challenge deadline
     */
    void RegisterChallengeableWithdrawal(const uint256& withdrawalId, uint64_t deadline);

    /**
     * @brief Check if a withdrawal is registered as challengeable
     * @param withdrawalId The withdrawal ID
     * @return true if withdrawal can be challenged
     */
    bool IsWithdrawalChallengeable(const uint256& withdrawalId) const;

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** All challenges (challengeId -> challenge) */
    std::map<uint256, WithdrawalChallenge> challenges_;

    /** Challenges by withdrawal (withdrawalId -> set of challengeIds) */
    std::map<uint256, std::set<uint256>> challengesByWithdrawal_;

    /** Active challenges by challenger (challenger -> set of challengeIds) */
    std::map<uint160, std::set<uint256>> activeChallengesByChallenger_;

    /** Challenger statistics */
    std::map<uint160, ChallengerStats> challengerStats_;

    /** Challengeable withdrawals (withdrawalId -> deadline) */
    std::map<uint256, uint64_t> challengeableWithdrawals_;

    /** Total bonds held */
    CAmount totalBondsHeld_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_challenge_;

    /** Next challenge ID counter */
    uint64_t nextChallengeId_;

    /**
     * @brief Generate a unique challenge ID
     * @param withdrawalId Withdrawal being challenged
     * @param challenger Challenger address
     * @param timestamp Current timestamp
     * @return Unique challenge ID
     */
    uint256 GenerateChallengeId(
        const uint256& withdrawalId,
        const uint160& challenger,
        uint64_t timestamp) const;

    /**
     * @brief Update challenger statistics after resolution
     * @param challenger Challenger address
     * @param result Challenge result
     */
    void UpdateChallengerStats(
        const uint160& challenger,
        const ChallengeResult& result);

    /**
     * @brief Check if challenger has reached challenge limit
     * @param challenger Challenger address
     * @return true if at limit
     */
    bool IsAtChallengeLimit(const uint160& challenger) const;

    /**
     * @brief Verify fraud proof structure
     * @param fraudProof The fraud proof data
     * @return true if structure is valid
     */
    bool VerifyFraudProofStructure(const std::vector<unsigned char>& fraudProof) const;
};

} // namespace l2

#endif // CASCOIN_L2_CHALLENGE_HANDLER_H
