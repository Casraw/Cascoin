// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BRIDGE_CONTRACT_H
#define CASCOIN_L2_BRIDGE_CONTRACT_H

/**
 * @file bridge_contract.h
 * @brief Bridge Contract for L1<->L2 deposits and withdrawals
 * 
 * *** DEPRECATED - Task 12: Legacy Bridge Code ***
 * 
 * This bridge contract is DEPRECATED and will be removed in a future version.
 * The new burn-and-mint model replaces the old deposit/withdrawal system:
 * 
 * - Use l2_createburntx to burn CAS on L1 via OP_RETURN
 * - Use l2_sendburntx to broadcast the burn transaction
 * - Use l2_getburnstatus to check burn/mint status
 * - See src/rpc/l2_burn.cpp for the new implementation
 * 
 * Requirements: 11.1, 11.4 - Legacy bridge code deprecated
 * 
 * Old features (DEPRECATED):
 * - Deposit processing (L1 -> L2) - Use burn-and-mint instead
 * - Withdrawal initiation and finalization (L2 -> L1) - No longer supported
 * - Challenge period enforcement - Replaced by sequencer consensus
 * - Fast withdrawals for high-reputation users - No longer applicable
 * - Emergency withdrawals when L2 is unavailable - Handled differently
 */

#include <l2/l2_common.h>
#include <l2/sparse_merkle_tree.h>
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

namespace l2 {

// Forward declarations
class L2StateManager;

// ============================================================================
// Constants
// ============================================================================

/** Standard challenge period: 7 days in seconds */
static constexpr uint64_t STANDARD_CHALLENGE_PERIOD = 7 * 24 * 60 * 60;

/** Fast challenge period for high-reputation users: 1 day in seconds */
static constexpr uint64_t FAST_CHALLENGE_PERIOD = 1 * 24 * 60 * 60;

/** Maximum deposit per transaction: 10,000 CAS */
static constexpr CAmount MAX_DEPOSIT_PER_TX = 10000 * COIN;

/** Maximum daily deposit per address: 100,000 CAS */
static constexpr CAmount MAX_DAILY_DEPOSIT = 100000 * COIN;

/** Maximum withdrawal per transaction: 10,000 CAS */
static constexpr CAmount MAX_WITHDRAWAL_PER_TX = 10000 * COIN;

/** Large withdrawal threshold requiring additional verification: 50,000 CAS */
static constexpr CAmount LARGE_WITHDRAWAL_THRESHOLD = 50000 * COIN;

/** Minimum HAT score for fast withdrawal eligibility */
static constexpr uint32_t FAST_WITHDRAWAL_MIN_HAT_SCORE = 80;

/** Challenge bond required to challenge a withdrawal: 10 CAS */
static constexpr CAmount CHALLENGE_BOND = 10 * COIN;

/** Emergency mode activation threshold: 24 hours without sequencer activity */
static constexpr uint64_t EMERGENCY_MODE_THRESHOLD = 24 * 60 * 60;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Deposit event from L1 to L2
 * 
 * Represents a deposit of CAS from L1 to L2. When a user deposits CAS
 * into the Bridge Contract on L1, this event is created and processed
 * on L2 to mint equivalent tokens.
 * 
 * Requirement 4.1: Accept CAS deposits from L1 and mint equivalent tokens on L2
 */
struct DepositEvent {
    /** Unique deposit identifier */
    uint256 depositId;
    
    /** L1 address that made the deposit */
    uint160 depositor;
    
    /** L2 address to receive the funds */
    uint160 l2Recipient;
    
    /** Amount deposited in satoshis */
    CAmount amount;
    
    /** L1 block number where deposit occurred */
    uint64_t l1BlockNumber;
    
    /** L1 transaction hash of the deposit */
    uint256 l1TxHash;
    
    /** Timestamp of the deposit */
    uint64_t timestamp;
    
    /** Whether this deposit has been processed on L2 */
    bool processed;

    DepositEvent()
        : amount(0)
        , l1BlockNumber(0)
        , timestamp(0)
        , processed(false)
    {}

    DepositEvent(const uint256& id, const uint160& from, const uint160& to,
                 CAmount amt, uint64_t blockNum, const uint256& txHash, uint64_t ts)
        : depositId(id)
        , depositor(from)
        , l2Recipient(to)
        , amount(amt)
        , l1BlockNumber(blockNum)
        , l1TxHash(txHash)
        , timestamp(ts)
        , processed(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(depositId);
        READWRITE(depositor);
        READWRITE(l2Recipient);
        READWRITE(amount);
        READWRITE(l1BlockNumber);
        READWRITE(l1TxHash);
        READWRITE(timestamp);
        READWRITE(processed);
    }

    /** Compute unique hash for this deposit */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << depositId << depositor << l2Recipient << amount 
           << l1BlockNumber << l1TxHash << timestamp;
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

    bool operator==(const DepositEvent& other) const {
        return depositId == other.depositId &&
               depositor == other.depositor &&
               l2Recipient == other.l2Recipient &&
               amount == other.amount &&
               l1BlockNumber == other.l1BlockNumber &&
               l1TxHash == other.l1TxHash &&
               timestamp == other.timestamp &&
               processed == other.processed;
    }
};

/**
 * @brief Withdrawal request from L2 to L1
 * 
 * Represents a withdrawal request initiated on L2 to withdraw CAS back to L1.
 * Withdrawals go through a challenge period before they can be finalized.
 * 
 * Requirements: 4.2, 4.3, 4.4
 */
struct WithdrawalRequest {
    /** Unique withdrawal identifier */
    uint256 withdrawalId;
    
    /** L2 address initiating the withdrawal */
    uint160 l2Sender;
    
    /** L1 address to receive the funds */
    uint160 l1Recipient;
    
    /** Amount to withdraw in satoshis */
    CAmount amount;
    
    /** L2 block number where withdrawal was initiated */
    uint64_t l2BlockNumber;
    
    /** State root at the time of withdrawal */
    uint256 stateRoot;
    
    /** Merkle proof of the withdrawal in the state */
    std::vector<unsigned char> merkleProof;
    
    /** Timestamp when challenge period ends */
    uint64_t challengeDeadline;
    
    /** Timestamp when withdrawal was initiated */
    uint64_t initiatedAt;
    
    /** Current status of the withdrawal */
    WithdrawalStatus status;
    
    /** HAT score of the user at withdrawal time (for fast withdrawal) */
    uint32_t hatScore;
    
    /** Whether this is a fast withdrawal */
    bool isFastWithdrawal;
    
    /** Challenge bond amount (if challenged) */
    CAmount challengeBond;
    
    /** Address of challenger (if challenged) */
    uint160 challenger;

    WithdrawalRequest()
        : amount(0)
        , l2BlockNumber(0)
        , challengeDeadline(0)
        , initiatedAt(0)
        , status(WithdrawalStatus::PENDING)
        , hatScore(0)
        , isFastWithdrawal(false)
        , challengeBond(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(withdrawalId);
        READWRITE(l2Sender);
        READWRITE(l1Recipient);
        READWRITE(amount);
        READWRITE(l2BlockNumber);
        READWRITE(stateRoot);
        READWRITE(merkleProof);
        READWRITE(challengeDeadline);
        READWRITE(initiatedAt);
        uint8_t statusByte = static_cast<uint8_t>(status);
        READWRITE(statusByte);
        if (ser_action.ForRead()) {
            status = static_cast<WithdrawalStatus>(statusByte);
        }
        READWRITE(hatScore);
        READWRITE(isFastWithdrawal);
        READWRITE(challengeBond);
        READWRITE(challenger);
    }

    /** Compute unique hash for this withdrawal */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << withdrawalId << l2Sender << l1Recipient << amount 
           << l2BlockNumber << stateRoot << initiatedAt;
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

    /** Check if challenge period has passed */
    bool IsChallengePeriodOver(uint64_t currentTime) const {
        return currentTime >= challengeDeadline;
    }

    /** Check if withdrawal can be finalized */
    bool CanFinalize(uint64_t currentTime) const {
        return status == WithdrawalStatus::PENDING && 
               IsChallengePeriodOver(currentTime);
    }

    bool operator==(const WithdrawalRequest& other) const {
        return withdrawalId == other.withdrawalId &&
               l2Sender == other.l2Sender &&
               l1Recipient == other.l1Recipient &&
               amount == other.amount &&
               l2BlockNumber == other.l2BlockNumber &&
               stateRoot == other.stateRoot &&
               status == other.status;
    }
};


/**
 * @brief Emergency withdrawal request
 * 
 * When L2 sequencers are unavailable for extended periods, users can
 * initiate emergency withdrawals directly on L1 using their last known
 * balance proof.
 * 
 * Requirements: 12.1, 12.2, 12.3
 */
struct EmergencyWithdrawalRequest {
    /** User address requesting emergency withdrawal */
    uint160 user;
    
    /** Last valid state root used for proof */
    uint256 lastValidStateRoot;
    
    /** Balance proof (Merkle proof of user's balance) */
    std::vector<unsigned char> balanceProof;
    
    /** Claimed balance amount */
    CAmount claimedBalance;
    
    /** Timestamp of emergency withdrawal request */
    uint64_t requestedAt;
    
    /** Whether the withdrawal has been processed */
    bool processed;

    EmergencyWithdrawalRequest()
        : claimedBalance(0)
        , requestedAt(0)
        , processed(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(user);
        READWRITE(lastValidStateRoot);
        READWRITE(balanceProof);
        READWRITE(claimedBalance);
        READWRITE(requestedAt);
        READWRITE(processed);
    }

    /** Compute unique hash for this emergency withdrawal */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << user << lastValidStateRoot << claimedBalance << requestedAt;
        return ss.GetHash();
    }
};

/**
 * @brief Bridge statistics for monitoring
 */
struct BridgeStats {
    /** Total value locked (TVL) in the bridge */
    CAmount totalValueLocked;
    
    /** Total deposits processed */
    uint64_t totalDeposits;
    
    /** Total withdrawals completed */
    uint64_t totalWithdrawals;
    
    /** Total emergency withdrawals */
    uint64_t totalEmergencyWithdrawals;
    
    /** Number of pending withdrawals */
    uint64_t pendingWithdrawals;
    
    /** Number of challenged withdrawals */
    uint64_t challengedWithdrawals;

    BridgeStats()
        : totalValueLocked(0)
        , totalDeposits(0)
        , totalWithdrawals(0)
        , totalEmergencyWithdrawals(0)
        , pendingWithdrawals(0)
        , challengedWithdrawals(0)
    {}
};

// ============================================================================
// Bridge Contract Class
// ============================================================================

/**
 * @brief Bridge Contract for L1<->L2 asset transfers
 * 
 * The BridgeContract manages all deposits and withdrawals between L1 and L2.
 * It enforces challenge periods, supports fast withdrawals for high-reputation
 * users, and provides emergency exit mechanisms.
 * 
 * Thread-safe for concurrent access.
 */
class BridgeContract {
public:
    /**
     * @brief Construct a new Bridge Contract
     * @param chainId The L2 chain ID
     */
    explicit BridgeContract(uint64_t chainId);

    // =========================================================================
    // Deposit Operations (Requirement 4.1)
    // =========================================================================

    /**
     * @brief Process a deposit from L1 to L2
     * @param deposit The deposit event from L1
     * @return true if deposit was processed successfully
     * 
     * Requirement 4.1: Accept CAS deposits from L1 and mint equivalent tokens on L2
     */
    bool ProcessDeposit(const DepositEvent& deposit);

    /**
     * @brief Check if a deposit has been processed
     * @param depositId The deposit identifier
     * @return true if deposit exists and has been processed
     */
    bool IsDepositProcessed(const uint256& depositId) const;

    /**
     * @brief Get deposit by ID
     * @param depositId The deposit identifier
     * @return The deposit event if found
     */
    std::optional<DepositEvent> GetDeposit(const uint256& depositId) const;

    /**
     * @brief Get daily deposit total for an address
     * @param address The depositor address
     * @param currentTime Current timestamp
     * @return Total deposits in the last 24 hours
     */
    CAmount GetDailyDepositTotal(const uint160& address, uint64_t currentTime) const;

    // =========================================================================
    // Withdrawal Operations (Requirements 4.2, 4.3)
    // =========================================================================

    /**
     * @brief Initiate a withdrawal from L2 to L1
     * @param sender L2 address initiating withdrawal
     * @param l1Recipient L1 address to receive funds
     * @param amount Amount to withdraw
     * @param l2BlockNumber Current L2 block number
     * @param stateRoot Current state root
     * @param currentTime Current timestamp
     * @param hatScore User's HAT v2 score
     * @return The created withdrawal request
     * 
     * Requirements 4.2, 4.3: Process withdrawal requests with challenge period
     */
    WithdrawalRequest InitiateWithdrawal(
        const uint160& sender,
        const uint160& l1Recipient,
        CAmount amount,
        uint64_t l2BlockNumber,
        const uint256& stateRoot,
        uint64_t currentTime,
        uint32_t hatScore = 0);

    /**
     * @brief Finalize a withdrawal after challenge period
     * @param withdrawalId The withdrawal identifier
     * @param currentTime Current timestamp
     * @return true if withdrawal was finalized successfully
     * 
     * Requirement 4.3: Enforce challenge period for withdrawals
     */
    bool FinalizeWithdrawal(const uint256& withdrawalId, uint64_t currentTime);

    /**
     * @brief Get withdrawal by ID
     * @param withdrawalId The withdrawal identifier
     * @return The withdrawal request if found
     */
    std::optional<WithdrawalRequest> GetWithdrawal(const uint256& withdrawalId) const;

    /**
     * @brief Get withdrawal status
     * @param withdrawalId The withdrawal identifier
     * @return The withdrawal status
     */
    WithdrawalStatus GetWithdrawalStatus(const uint256& withdrawalId) const;

    /**
     * @brief Get all pending withdrawals for an address
     * @param address The L2 sender address
     * @return Vector of pending withdrawal requests
     */
    std::vector<WithdrawalRequest> GetPendingWithdrawals(const uint160& address) const;

    // =========================================================================
    // Fast Withdrawal (Requirements 4.4, 6.2)
    // =========================================================================

    /**
     * @brief Initiate a fast withdrawal for high-reputation users
     * @param sender L2 address initiating withdrawal
     * @param l1Recipient L1 address to receive funds
     * @param amount Amount to withdraw
     * @param l2BlockNumber Current L2 block number
     * @param stateRoot Current state root
     * @param currentTime Current timestamp
     * @param hatScore User's HAT v2 score (must be >= 80)
     * @return The created withdrawal request with reduced challenge period
     * 
     * Requirements 4.4, 6.2: Support fast withdrawals for high-reputation users
     */
    WithdrawalRequest FastWithdrawal(
        const uint160& sender,
        const uint160& l1Recipient,
        CAmount amount,
        uint64_t l2BlockNumber,
        const uint256& stateRoot,
        uint64_t currentTime,
        uint32_t hatScore);

    /**
     * @brief Check if user qualifies for fast withdrawal
     * @param hatScore User's HAT v2 score
     * @return true if user qualifies (score >= 80)
     */
    static bool QualifiesForFastWithdrawal(uint32_t hatScore);

    /**
     * @brief Calculate challenge period based on reputation
     * @param hatScore User's HAT v2 score
     * @return Challenge period in seconds
     */
    static uint64_t CalculateChallengePeriod(uint32_t hatScore);

    // =========================================================================
    // Challenge Operations (Requirement 4.6)
    // =========================================================================

    /**
     * @brief Challenge a withdrawal
     * @param withdrawalId The withdrawal to challenge
     * @param challenger Address of the challenger
     * @param fraudProof Proof that the withdrawal is invalid
     * @param currentTime Current timestamp
     * @return true if challenge was accepted
     * 
     * Requirement 4.6: If a withdrawal is challenged successfully, cancel and slash
     */
    bool ChallengeWithdrawal(
        const uint256& withdrawalId,
        const uint160& challenger,
        const std::vector<unsigned char>& fraudProof,
        uint64_t currentTime);

    /**
     * @brief Resolve a challenge (after verification)
     * @param withdrawalId The challenged withdrawal
     * @param challengeValid Whether the challenge was valid
     * @return true if resolution was processed
     */
    bool ResolveChallenge(const uint256& withdrawalId, bool challengeValid);

    // =========================================================================
    // Emergency Withdrawal (Requirements 12.1, 12.2, 12.3)
    // =========================================================================

    /**
     * @brief Check if emergency mode is active
     * @param lastSequencerActivity Timestamp of last sequencer activity
     * @param currentTime Current timestamp
     * @return true if emergency mode should be activated
     * 
     * Requirement 12.1: Enable emergency withdrawals if sequencers unavailable >24h
     */
    static bool IsEmergencyModeActive(uint64_t lastSequencerActivity, uint64_t currentTime);

    /**
     * @brief Process an emergency withdrawal
     * @param user User address
     * @param lastValidStateRoot Last valid state root
     * @param balanceProof Merkle proof of user's balance
     * @param claimedBalance The balance being claimed
     * @param currentTime Current timestamp
     * @return true if emergency withdrawal was processed
     * 
     * Requirements 12.1, 12.2, 12.3: Emergency withdrawal mechanism
     */
    bool EmergencyWithdrawal(
        const uint160& user,
        const uint256& lastValidStateRoot,
        const std::vector<unsigned char>& balanceProof,
        CAmount claimedBalance,
        uint64_t currentTime);

    /**
     * @brief Verify a balance proof for emergency withdrawal
     * @param user User address
     * @param stateRoot State root to verify against
     * @param balanceProof The Merkle proof
     * @param claimedBalance The claimed balance
     * @return true if proof is valid
     * 
     * Requirement 12.2: Allow users to prove their L2 balance
     */
    bool VerifyBalanceProof(
        const uint160& user,
        const uint256& stateRoot,
        const std::vector<unsigned char>& balanceProof,
        CAmount claimedBalance) const;

    /**
     * @brief Set emergency mode state
     * @param active Whether emergency mode is active
     */
    void SetEmergencyMode(bool active);

    /**
     * @brief Check if emergency mode is currently active
     * @return true if in emergency mode
     */
    bool IsInEmergencyMode() const;

    // =========================================================================
    // Accounting and Statistics (Requirement 4.5)
    // =========================================================================

    /**
     * @brief Get total value locked in the bridge
     * @return Total CAS locked
     * 
     * Requirement 4.5: Maintain accurate accounting of locked L1 funds
     */
    CAmount GetTotalValueLocked() const;

    /**
     * @brief Get bridge statistics
     * @return Current bridge statistics
     */
    BridgeStats GetStats() const;

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

    /**
     * @brief Get number of processed deposits
     * @return Deposit count
     */
    size_t GetDepositCount() const;

    /**
     * @brief Get number of withdrawals (all statuses)
     * @return Withdrawal count
     */
    size_t GetWithdrawalCount() const;

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Processed deposits (depositId -> deposit) */
    std::map<uint256, DepositEvent> deposits_;

    /** Withdrawal requests (withdrawalId -> request) */
    std::map<uint256, WithdrawalRequest> withdrawals_;

    /** Emergency withdrawal requests (user -> request) */
    std::map<uint160, EmergencyWithdrawalRequest> emergencyWithdrawals_;

    /** Daily deposit tracking (address -> (day -> total)) */
    std::map<uint160, std::map<uint64_t, CAmount>> dailyDeposits_;

    /** Total value locked */
    CAmount totalValueLocked_;

    /** Bridge statistics */
    BridgeStats stats_;

    /** Emergency mode flag */
    bool emergencyMode_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_bridge_;

    /** Next withdrawal ID counter */
    uint64_t nextWithdrawalId_;

    /**
     * @brief Generate a unique withdrawal ID
     * @param sender Sender address
     * @param amount Withdrawal amount
     * @param timestamp Current timestamp
     * @return Unique withdrawal ID
     */
    uint256 GenerateWithdrawalId(
        const uint160& sender,
        CAmount amount,
        uint64_t timestamp) const;

    /**
     * @brief Verify withdrawal proof
     * @param request The withdrawal request
     * @return true if proof is valid
     */
    bool VerifyWithdrawalProof(const WithdrawalRequest& request) const;

    /**
     * @brief Get day number from timestamp (for daily limits)
     * @param timestamp Unix timestamp
     * @return Day number since epoch
     */
    static uint64_t GetDayNumber(uint64_t timestamp);

    /**
     * @brief Update daily deposit tracking
     * @param address Depositor address
     * @param amount Deposit amount
     * @param timestamp Deposit timestamp
     */
    void UpdateDailyDeposits(const uint160& address, CAmount amount, uint64_t timestamp);
};

} // namespace l2

#endif // CASCOIN_L2_BRIDGE_CONTRACT_H
