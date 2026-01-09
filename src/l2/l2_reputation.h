// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_REPUTATION_H
#define CASCOIN_L2_REPUTATION_H

/**
 * @file l2_reputation.h
 * @brief L2 Reputation Integration for Cascoin Layer 2
 * 
 * This file implements the reputation integration between L1 and L2,
 * allowing users to leverage their L1 HAT v2 reputation on L2 and
 * earn additional L2-specific reputation.
 * 
 * Key features:
 * - Import L1 HAT v2 reputation to L2
 * - Track L2-specific behavior and economic activity
 * - Aggregate L1 and L2 reputation for cross-layer operations
 * - Provide reputation-based benefits (fast withdrawals, gas discounts)
 * - Sync reputation changes back to L1
 * 
 * Requirements: 6.1, 6.2, 10.1, 10.2, 10.3, 10.4, 10.5, 18.5
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

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Minimum HAT score for fast withdrawal eligibility (Requirement 6.2) */
static constexpr uint32_t REPUTATION_FAST_WITHDRAWAL_THRESHOLD = 80;

/** Minimum HAT score for gas discount eligibility (Requirement 18.5) */
static constexpr uint32_t REPUTATION_GAS_DISCOUNT_THRESHOLD = 70;

/** Maximum gas discount percentage */
static constexpr uint32_t MAX_GAS_DISCOUNT_PERCENT = 50;

/** L1 sync interval in blocks (Requirement 10.4) */
static constexpr uint64_t L1_REPUTATION_SYNC_INTERVAL = 1000;

/** Weight of L1 reputation in aggregation (0-100) */
static constexpr uint32_t L1_REPUTATION_WEIGHT = 60;

/** Weight of L2 behavior score in aggregation (0-100) */
static constexpr uint32_t L2_BEHAVIOR_WEIGHT = 25;

/** Weight of L2 economic score in aggregation (0-100) */
static constexpr uint32_t L2_ECONOMIC_WEIGHT = 15;

/** Minimum transactions for L2 reputation to be considered */
static constexpr uint64_t MIN_L2_TRANSACTIONS_FOR_REPUTATION = 10;

/** Volume threshold for economic score calculation (in satoshis) */
static constexpr CAmount ECONOMIC_VOLUME_THRESHOLD = 1000 * COIN;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief L2-specific reputation data
 * 
 * Stores reputation information for an address on L2, including
 * imported L1 reputation and L2-specific behavior metrics.
 * 
 * Requirements: 10.1, 10.2
 */
struct L2ReputationData {
    /** Imported HAT v2 score from L1 (0-100) */
    uint32_t l1HatScore;
    
    /** L2-specific behavior score (0-100) */
    uint32_t l2BehaviorScore;
    
    /** L2 economic activity score (0-100) */
    uint32_t l2EconomicScore;
    
    /** Aggregated score combining L1 and L2 (0-100) */
    uint32_t aggregatedScore;
    
    /** L1 block number of last reputation sync */
    uint64_t lastL1Sync;
    
    /** Total L2 transactions by this address */
    uint64_t l2TransactionCount;
    
    /** Total volume traded on L2 (in satoshis) */
    CAmount l2VolumeTraded;
    
    /** Number of successful contract interactions */
    uint64_t successfulContractCalls;
    
    /** Number of failed transactions (reverts, out of gas) */
    uint64_t failedTransactions;
    
    /** L2 block number of last activity */
    uint64_t lastL2Activity;
    
    /** Whether this address has been flagged for suspicious activity */
    bool flaggedForReview;
    
    /** Timestamp when reputation was first established on L2 */
    uint64_t firstSeenOnL2;

    /** Default constructor */
    L2ReputationData()
        : l1HatScore(0)
        , l2BehaviorScore(0)
        , l2EconomicScore(0)
        , aggregatedScore(0)
        , lastL1Sync(0)
        , l2TransactionCount(0)
        , l2VolumeTraded(0)
        , successfulContractCalls(0)
        , failedTransactions(0)
        , lastL2Activity(0)
        , flaggedForReview(false)
        , firstSeenOnL2(0)
    {}

    /** Constructor with L1 HAT score */
    explicit L2ReputationData(uint32_t hatScore)
        : l1HatScore(hatScore)
        , l2BehaviorScore(0)
        , l2EconomicScore(0)
        , aggregatedScore(hatScore)  // Initially just L1 score
        , lastL1Sync(0)
        , l2TransactionCount(0)
        , l2VolumeTraded(0)
        , successfulContractCalls(0)
        , failedTransactions(0)
        , lastL2Activity(0)
        , flaggedForReview(false)
        , firstSeenOnL2(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l1HatScore);
        READWRITE(l2BehaviorScore);
        READWRITE(l2EconomicScore);
        READWRITE(aggregatedScore);
        READWRITE(lastL1Sync);
        READWRITE(l2TransactionCount);
        READWRITE(l2VolumeTraded);
        READWRITE(successfulContractCalls);
        READWRITE(failedTransactions);
        READWRITE(lastL2Activity);
        READWRITE(flaggedForReview);
        READWRITE(firstSeenOnL2);
    }

    /**
     * @brief Compute hash of this reputation data
     * @return SHA256 hash
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << *this;
        return ss.GetHash();
    }

    /**
     * @brief Serialize to bytes
     * @return Serialized bytes
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /**
     * @brief Deserialize from bytes
     * @param data Serialized data
     * @return true if successful
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) {
            *this = L2ReputationData();
            return true;
        }
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /**
     * @brief Check if this is a new/empty reputation record
     * @return true if no activity recorded
     */
    bool IsEmpty() const {
        return l1HatScore == 0 && l2TransactionCount == 0;
    }

    /**
     * @brief Get success rate for transactions
     * @return Success rate as percentage (0-100)
     */
    uint32_t GetSuccessRate() const {
        uint64_t total = l2TransactionCount;
        if (total == 0) return 100;  // No transactions = perfect rate
        uint64_t successful = total - failedTransactions;
        return static_cast<uint32_t>((successful * 100) / total);
    }

    bool operator==(const L2ReputationData& other) const {
        return l1HatScore == other.l1HatScore &&
               l2BehaviorScore == other.l2BehaviorScore &&
               l2EconomicScore == other.l2EconomicScore &&
               aggregatedScore == other.aggregatedScore &&
               lastL1Sync == other.lastL1Sync &&
               l2TransactionCount == other.l2TransactionCount &&
               l2VolumeTraded == other.l2VolumeTraded &&
               successfulContractCalls == other.successfulContractCalls &&
               failedTransactions == other.failedTransactions &&
               lastL2Activity == other.lastL2Activity &&
               flaggedForReview == other.flaggedForReview &&
               firstSeenOnL2 == other.firstSeenOnL2;
    }

    bool operator!=(const L2ReputationData& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Reputation-based benefits structure
 * 
 * Defines the benefits a user receives based on their reputation score.
 * Higher reputation unlocks better benefits.
 * 
 * Requirements: 6.1, 6.2, 18.5
 */
struct ReputationBenefits {
    /** Gas discount percentage (0-50%) */
    uint32_t gasDiscountPercent;
    
    /** Challenge period for withdrawals in seconds */
    uint64_t challengePeriodSeconds;
    
    /** Rate limit multiplier (higher = more transactions allowed) */
    uint32_t rateLimitMultiplier;
    
    /** Whether user gets instant soft-finality */
    bool instantSoftFinality;
    
    /** Transaction priority level (0-10, higher = more priority) */
    uint32_t priorityLevel;
    
    /** Whether user qualifies for fast withdrawal */
    bool qualifiesForFastWithdrawal;
    
    /** Maximum withdrawal amount without additional verification */
    CAmount maxWithdrawalWithoutVerification;

    /** Default constructor - no benefits */
    ReputationBenefits()
        : gasDiscountPercent(0)
        , challengePeriodSeconds(7 * 24 * 60 * 60)  // 7 days default
        , rateLimitMultiplier(1)
        , instantSoftFinality(false)
        , priorityLevel(0)
        , qualifiesForFastWithdrawal(false)
        , maxWithdrawalWithoutVerification(10000 * COIN)  // 10,000 CAS default
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(gasDiscountPercent);
        READWRITE(challengePeriodSeconds);
        READWRITE(rateLimitMultiplier);
        READWRITE(instantSoftFinality);
        READWRITE(priorityLevel);
        READWRITE(qualifiesForFastWithdrawal);
        READWRITE(maxWithdrawalWithoutVerification);
    }

    /**
     * @brief Serialize to bytes
     * @return Serialized bytes
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /**
     * @brief Deserialize from bytes
     * @param data Serialized data
     * @return true if successful
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) {
            *this = ReputationBenefits();
            return true;
        }
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool operator==(const ReputationBenefits& other) const {
        return gasDiscountPercent == other.gasDiscountPercent &&
               challengePeriodSeconds == other.challengePeriodSeconds &&
               rateLimitMultiplier == other.rateLimitMultiplier &&
               instantSoftFinality == other.instantSoftFinality &&
               priorityLevel == other.priorityLevel &&
               qualifiesForFastWithdrawal == other.qualifiesForFastWithdrawal &&
               maxWithdrawalWithoutVerification == other.maxWithdrawalWithoutVerification;
    }
};

/**
 * @brief L2 activity record for reputation updates
 * 
 * Records a single activity event that affects L2 reputation.
 */
struct L2Activity {
    /** Type of activity */
    enum class Type : uint8_t {
        TRANSACTION = 0,        // Regular transaction
        CONTRACT_CALL = 1,      // Contract interaction
        CONTRACT_DEPLOY = 2,    // Contract deployment
        DEPOSIT = 3,            // L1 -> L2 deposit
        WITHDRAWAL = 4,         // L2 -> L1 withdrawal
        FAILED_TX = 5           // Failed transaction
    };

    /** Activity type */
    Type type;
    
    /** Value involved (in satoshis) */
    CAmount value;
    
    /** Gas used */
    uint64_t gasUsed;
    
    /** L2 block number */
    uint64_t blockNumber;
    
    /** Whether the activity was successful */
    bool success;

    L2Activity()
        : type(Type::TRANSACTION)
        , value(0)
        , gasUsed(0)
        , blockNumber(0)
        , success(true)
    {}

    L2Activity(Type t, CAmount v, uint64_t gas, uint64_t block, bool ok)
        : type(t)
        , value(v)
        , gasUsed(gas)
        , blockNumber(block)
        , success(ok)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint8_t typeByte = static_cast<uint8_t>(type);
        READWRITE(typeByte);
        if (ser_action.ForRead()) {
            type = static_cast<Type>(typeByte);
        }
        READWRITE(value);
        READWRITE(gasUsed);
        READWRITE(blockNumber);
        READWRITE(success);
    }
};

/**
 * @brief Reputation sync request for L1 synchronization
 * 
 * Used to request or report reputation sync between L1 and L2.
 */
struct ReputationSyncRequest {
    /** Address to sync */
    uint160 address;
    
    /** L2 chain ID */
    uint64_t chainId;
    
    /** Current L2 aggregated score */
    uint32_t l2AggregatedScore;
    
    /** L2 transaction count */
    uint64_t l2TransactionCount;
    
    /** L2 volume traded */
    CAmount l2VolumeTraded;
    
    /** L2 block number at sync time */
    uint64_t l2BlockNumber;
    
    /** Timestamp of sync request */
    uint64_t timestamp;
    
    /** Signature proving L2 state */
    std::vector<unsigned char> signature;

    ReputationSyncRequest()
        : chainId(0)
        , l2AggregatedScore(0)
        , l2TransactionCount(0)
        , l2VolumeTraded(0)
        , l2BlockNumber(0)
        , timestamp(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(chainId);
        READWRITE(l2AggregatedScore);
        READWRITE(l2TransactionCount);
        READWRITE(l2VolumeTraded);
        READWRITE(l2BlockNumber);
        READWRITE(timestamp);
        READWRITE(signature);
    }

    /** Compute hash for signing */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << address << chainId << l2AggregatedScore << l2TransactionCount
           << l2VolumeTraded << l2BlockNumber << timestamp;
        return ss.GetHash();
    }
};

// ============================================================================
// L2 Reputation Manager Class
// ============================================================================

/**
 * @brief L2 Reputation Manager
 * 
 * Manages reputation data for all addresses on L2, including:
 * - Importing L1 HAT v2 reputation
 * - Tracking L2-specific activity
 * - Computing aggregated reputation scores
 * - Providing reputation-based benefits
 * - Syncing reputation changes back to L1
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 10.1, 10.3, 10.4, 10.5
 */
class L2ReputationManager {
public:
    /**
     * @brief Construct a new L2 Reputation Manager
     * @param chainId The L2 chain ID
     */
    explicit L2ReputationManager(uint64_t chainId);

    // =========================================================================
    // L1 Reputation Import (Requirement 10.1)
    // =========================================================================

    /**
     * @brief Import reputation from L1 for an address
     * @param address The address to import reputation for
     * @param l1HatScore The HAT v2 score from L1
     * @param l1BlockNumber The L1 block number of the score
     * @return true if import was successful
     * 
     * Requirement 10.1: Import HAT v2 scores from L1 for new L2 users
     */
    bool ImportL1Reputation(const uint160& address, uint32_t l1HatScore, uint64_t l1BlockNumber);

    /**
     * @brief Check if L1 reputation has been imported for an address
     * @param address The address to check
     * @return true if L1 reputation exists
     */
    bool HasL1Reputation(const uint160& address) const;

    /**
     * @brief Get the L1 HAT score for an address
     * @param address The address to query
     * @return L1 HAT score (0 if not imported)
     */
    uint32_t GetL1HatScore(const uint160& address) const;

    // =========================================================================
    // Aggregated Reputation (Requirement 10.3)
    // =========================================================================

    /**
     * @brief Get aggregated reputation combining L1 and L2
     * @param address The address to query
     * @return Aggregated reputation score (0-100)
     * 
     * Requirement 10.3: Aggregate L1 and L2 reputation for cross-layer operations
     */
    uint32_t GetAggregatedReputation(const uint160& address) const;

    /**
     * @brief Get full reputation data for an address
     * @param address The address to query
     * @return Reputation data (empty if not found)
     */
    L2ReputationData GetReputationData(const uint160& address) const;

    /**
     * @brief Check if an address has any reputation data
     * @param address The address to check
     * @return true if reputation data exists
     */
    bool HasReputationData(const uint160& address) const;

    // =========================================================================
    // L2 Reputation Updates (Requirement 10.2)
    // =========================================================================

    /**
     * @brief Update L2 reputation based on activity
     * @param address The address to update
     * @param activity The activity that occurred
     * 
     * Requirement 10.2: Maintain separate L2 reputation that evolves independently
     */
    void UpdateL2Reputation(const uint160& address, const L2Activity& activity);

    /**
     * @brief Record a successful transaction
     * @param address The address that sent the transaction
     * @param value Transaction value
     * @param gasUsed Gas consumed
     * @param blockNumber L2 block number
     */
    void RecordTransaction(const uint160& address, CAmount value, uint64_t gasUsed, uint64_t blockNumber);

    /**
     * @brief Record a failed transaction
     * @param address The address that sent the transaction
     * @param blockNumber L2 block number
     */
    void RecordFailedTransaction(const uint160& address, uint64_t blockNumber);

    /**
     * @brief Record a contract interaction
     * @param address The address that called the contract
     * @param value Value sent to contract
     * @param gasUsed Gas consumed
     * @param blockNumber L2 block number
     * @param success Whether the call succeeded
     */
    void RecordContractCall(const uint160& address, CAmount value, uint64_t gasUsed, 
                           uint64_t blockNumber, bool success);

    // =========================================================================
    // L1 Sync (Requirement 10.4)
    // =========================================================================

    /**
     * @brief Sync reputation changes back to L1
     * @param address The address to sync
     * @return Sync request to be submitted to L1
     * 
     * Requirement 10.4: Sync reputation changes back to L1 periodically
     */
    ReputationSyncRequest SyncToL1(const uint160& address) const;

    /**
     * @brief Check if address needs L1 sync
     * @param address The address to check
     * @param currentL2Block Current L2 block number
     * @return true if sync is needed
     */
    bool NeedsL1Sync(const uint160& address, uint64_t currentL2Block) const;

    /**
     * @brief Get addresses that need L1 sync
     * @param currentL2Block Current L2 block number
     * @return Vector of addresses needing sync
     */
    std::vector<uint160> GetAddressesNeedingSync(uint64_t currentL2Block) const;

    // =========================================================================
    // Reputation Benefits (Requirements 6.1, 6.2, 18.5)
    // =========================================================================

    /**
     * @brief Get reputation-based benefits for an address
     * @param address The address to query
     * @return Benefits based on reputation score
     * 
     * Requirements 6.1, 6.2, 18.5
     */
    ReputationBenefits GetBenefits(const uint160& address) const;

    /**
     * @brief Check if address qualifies for fast withdrawal
     * @param address The address to check
     * @return true if qualifies (aggregated score >= 80)
     * 
     * Requirement 6.2: Fast withdrawals for high-reputation users
     */
    bool QualifiesForFastWithdrawal(const uint160& address) const;

    /**
     * @brief Get gas discount percentage for an address
     * @param address The address to query
     * @return Discount percentage (0-50)
     * 
     * Requirement 18.5: Subsidize L2 fees for high-reputation users
     */
    uint32_t GetGasDiscount(const uint160& address) const;

    /**
     * @brief Get rate limit multiplier for an address
     * @param address The address to query
     * @return Rate limit multiplier (1-10)
     */
    uint32_t GetRateLimitMultiplier(const uint160& address) const;

    /**
     * @brief Check if address gets instant soft-finality
     * @param address The address to check
     * @return true if gets instant finality
     * 
     * Requirement 6.1: Instant soft-finality for reputation >80
     */
    bool HasInstantSoftFinality(const uint160& address) const;

    // =========================================================================
    // Anti-Gaming (Requirement 10.5)
    // =========================================================================

    /**
     * @brief Detect potential reputation gaming
     * @param address The address to check
     * @return true if suspicious activity detected
     * 
     * Requirement 10.5: Prevent reputation gaming through cross-layer arbitrage
     */
    bool DetectReputationGaming(const uint160& address) const;

    /**
     * @brief Flag an address for review
     * @param address The address to flag
     * @param reason Reason for flagging
     */
    void FlagForReview(const uint160& address, const std::string& reason);

    /**
     * @brief Clear flag for an address
     * @param address The address to clear
     */
    void ClearFlag(const uint160& address);

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Get number of tracked addresses
     * @return Address count
     */
    size_t GetAddressCount() const;

    /**
     * @brief Clear all reputation data (for testing)
     */
    void Clear();

    /**
     * @brief Calculate benefits from a reputation score
     * @param score Reputation score (0-100)
     * @return Calculated benefits
     */
    static ReputationBenefits CalculateBenefits(uint32_t score);

    /**
     * @brief Calculate aggregated score from components
     * @param l1Score L1 HAT score
     * @param l2Behavior L2 behavior score
     * @param l2Economic L2 economic score
     * @return Aggregated score (0-100)
     * 
     * Requirement 10.3: Deterministic aggregation function
     */
    static uint32_t CalculateAggregatedScore(uint32_t l1Score, uint32_t l2Behavior, uint32_t l2Economic);

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Reputation data cache (address -> data) */
    std::map<uint160, L2ReputationData> reputationCache_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_reputation_;

    /**
     * @brief Calculate L2 behavior score from activity metrics
     * @param data Reputation data
     * @return Behavior score (0-100)
     */
    static uint32_t CalculateBehaviorScore(const L2ReputationData& data);

    /**
     * @brief Calculate L2 economic score from volume
     * @param data Reputation data
     * @return Economic score (0-100)
     */
    static uint32_t CalculateEconomicScore(const L2ReputationData& data);

    /**
     * @brief Recalculate aggregated score for an address
     * @param address The address to recalculate
     */
    void RecalculateAggregatedScore(const uint160& address);

    /**
     * @brief Ensure reputation data exists for address
     * @param address The address
     * @return Reference to reputation data
     */
    L2ReputationData& EnsureReputationData(const uint160& address);
};

} // namespace l2

#endif // CASCOIN_L2_REPUTATION_H
