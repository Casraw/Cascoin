// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_TIMESTAMP_VALIDATOR_H
#define CASCOIN_L2_TIMESTAMP_VALIDATOR_H

/**
 * @file timestamp_validator.h
 * @brief Timestamp Validation for Cascoin Layer 2
 * 
 * This file implements timestamp validation mechanisms for L2 blocks,
 * ensuring reliable timestamps for time-sensitive smart contract logic.
 * 
 * Key features:
 * - L1 timestamp binding (max drift: 15 minutes)
 * - Monotonically increasing timestamps
 * - Future timestamp rejection (>30 seconds ahead)
 * - Timestamp manipulation detection
 * - L1 timestamp oracle for critical operations
 * 
 * Requirements: 27.1, 27.2, 27.3, 27.4, 27.5, 27.6
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <deque>
#include <chrono>
#include <optional>
#include <string>
#include <functional>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Maximum drift between L2 and L1 timestamps (15 minutes in seconds) (Requirement 27.1) */
static constexpr uint64_t MAX_L1_TIMESTAMP_DRIFT = 15 * 60;  // 900 seconds

/** Maximum future timestamp allowed (30 seconds) (Requirement 27.3) */
static constexpr uint64_t MAX_FUTURE_TIMESTAMP_SECONDS = 30;

/** Minimum timestamp increment between blocks (1 second) */
static constexpr uint64_t MIN_TIMESTAMP_INCREMENT = 1;

/** Number of blocks to track for manipulation detection (Requirement 27.6) */
static constexpr uint32_t TIMESTAMP_HISTORY_SIZE = 100;

/** Threshold for detecting timestamp manipulation (average drift) */
static constexpr uint64_t MANIPULATION_DETECTION_THRESHOLD = 60;  // 60 seconds average drift

/** Penalty for timestamp manipulation (reputation decrease) (Requirement 27.4) */
static constexpr uint32_t TIMESTAMP_MANIPULATION_PENALTY = 10;

/** Number of consecutive violations before flagging manipulation */
static constexpr uint32_t MANIPULATION_VIOLATION_THRESHOLD = 3;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Result of timestamp validation
 */
struct TimestampValidationResult {
    /** Whether the timestamp is valid */
    bool valid;
    
    /** Reason if invalid */
    std::string reason;
    
    /** Drift from L1 timestamp (if applicable) */
    int64_t l1Drift;
    
    /** Drift from previous block timestamp */
    int64_t previousBlockDrift;
    
    /** Whether manipulation was detected */
    bool manipulationDetected;
    
    /** Sequencer address if manipulation detected */
    uint160 manipulatingSequencer;

    TimestampValidationResult()
        : valid(true)
        , l1Drift(0)
        , previousBlockDrift(0)
        , manipulationDetected(false)
    {}

    static TimestampValidationResult Valid(int64_t l1Drift = 0, int64_t prevDrift = 0) {
        TimestampValidationResult result;
        result.valid = true;
        result.l1Drift = l1Drift;
        result.previousBlockDrift = prevDrift;
        return result;
    }

    static TimestampValidationResult Invalid(const std::string& reason) {
        TimestampValidationResult result;
        result.valid = false;
        result.reason = reason;
        return result;
    }

    static TimestampValidationResult ManipulationDetected(const uint160& sequencer) {
        TimestampValidationResult result;
        result.valid = false;
        result.reason = "Timestamp manipulation detected";
        result.manipulationDetected = true;
        result.manipulatingSequencer = sequencer;
        return result;
    }
};

/**
 * @brief L1 timestamp reference for binding
 */
struct L1TimestampReference {
    /** L1 block number */
    uint64_t blockNumber;
    
    /** L1 block timestamp */
    uint64_t timestamp;
    
    /** L1 block hash */
    uint256 blockHash;

    L1TimestampReference()
        : blockNumber(0)
        , timestamp(0)
    {}

    L1TimestampReference(uint64_t num, uint64_t ts, const uint256& hash)
        : blockNumber(num)
        , timestamp(ts)
        , blockHash(hash)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockNumber);
        READWRITE(timestamp);
        READWRITE(blockHash);
    }

    bool IsValid() const {
        return blockNumber > 0 && timestamp > 0;
    }
};

/**
 * @brief Timestamp history entry for a block
 */
struct TimestampHistoryEntry {
    /** L2 block number */
    uint64_t blockNumber;
    
    /** L2 block timestamp */
    uint64_t timestamp;
    
    /** Sequencer who produced the block */
    uint160 sequencer;
    
    /** L1 reference timestamp at the time */
    uint64_t l1ReferenceTimestamp;
    
    /** Drift from L1 timestamp */
    int64_t l1Drift;
    
    /** Drift from previous block */
    int64_t previousBlockDrift;

    TimestampHistoryEntry()
        : blockNumber(0)
        , timestamp(0)
        , l1ReferenceTimestamp(0)
        , l1Drift(0)
        , previousBlockDrift(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockNumber);
        READWRITE(timestamp);
        READWRITE(sequencer);
        READWRITE(l1ReferenceTimestamp);
        READWRITE(l1Drift);
        READWRITE(previousBlockDrift);
    }
};

/**
 * @brief Sequencer timestamp behavior tracking
 */
struct SequencerTimestampBehavior {
    /** Sequencer address */
    uint160 sequencer;
    
    /** Total blocks produced */
    uint32_t blocksProduced;
    
    /** Blocks with timestamp violations */
    uint32_t violationCount;
    
    /** Consecutive violations (for manipulation detection) */
    uint32_t consecutiveViolations;
    
    /** Average L1 drift (absolute value) */
    uint64_t averageL1Drift;
    
    /** Maximum L1 drift observed */
    uint64_t maxL1Drift;
    
    /** Whether sequencer is flagged for manipulation */
    bool flaggedForManipulation;
    
    /** Last block number produced */
    uint64_t lastBlockNumber;

    SequencerTimestampBehavior()
        : blocksProduced(0)
        , violationCount(0)
        , consecutiveViolations(0)
        , averageL1Drift(0)
        , maxL1Drift(0)
        , flaggedForManipulation(false)
        , lastBlockNumber(0)
    {}

    explicit SequencerTimestampBehavior(const uint160& addr)
        : sequencer(addr)
        , blocksProduced(0)
        , violationCount(0)
        , consecutiveViolations(0)
        , averageL1Drift(0)
        , maxL1Drift(0)
        , flaggedForManipulation(false)
        , lastBlockNumber(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencer);
        READWRITE(blocksProduced);
        READWRITE(violationCount);
        READWRITE(consecutiveViolations);
        READWRITE(averageL1Drift);
        READWRITE(maxL1Drift);
        READWRITE(flaggedForManipulation);
        READWRITE(lastBlockNumber);
    }

    /**
     * @brief Get violation rate as percentage
     * @return Violation rate (0-100)
     */
    uint32_t GetViolationRate() const {
        if (blocksProduced == 0) return 0;
        return (violationCount * 100) / blocksProduced;
    }
};

// ============================================================================
// Timestamp Validator Class
// ============================================================================

/**
 * @brief L2 Timestamp Validator
 * 
 * Validates L2 block timestamps to ensure reliability for smart contracts.
 * Implements L1 timestamp binding, monotonicity checks, future timestamp
 * rejection, and manipulation detection.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 27.1, 27.2, 27.3, 27.4, 27.5, 27.6
 */
class TimestampValidator {
public:
    /**
     * @brief Construct a new Timestamp Validator
     */
    TimestampValidator();

    // =========================================================================
    // L1 Timestamp Binding (Requirement 27.1)
    // =========================================================================

    /**
     * @brief Update the L1 timestamp reference
     * @param blockNumber L1 block number
     * @param timestamp L1 block timestamp
     * @param blockHash L1 block hash
     * 
     * Requirement 27.1: Bound L2 timestamps to L1 timestamps
     */
    void UpdateL1Reference(uint64_t blockNumber, uint64_t timestamp, const uint256& blockHash);

    /**
     * @brief Get the current L1 timestamp reference
     * @return L1 timestamp reference
     */
    L1TimestampReference GetL1Reference() const;

    /**
     * @brief Get the L1 timestamp for critical operations
     * @return L1 timestamp (0 if not available)
     * 
     * Requirement 27.5: Provide L1 timestamp oracle
     */
    uint64_t GetL1TimestampOracle() const;

    /**
     * @brief Check if L2 timestamp is within allowed drift from L1
     * @param l2Timestamp L2 block timestamp
     * @return true if within allowed drift
     * 
     * Requirement 27.1: Max drift 15 minutes
     */
    bool IsWithinL1Drift(uint64_t l2Timestamp) const;

    /**
     * @brief Calculate drift from L1 timestamp
     * @param l2Timestamp L2 block timestamp
     * @return Drift in seconds (positive = ahead, negative = behind)
     */
    int64_t CalculateL1Drift(uint64_t l2Timestamp) const;

    // =========================================================================
    // Monotonicity Check (Requirement 27.2)
    // =========================================================================

    /**
     * @brief Check if timestamp is monotonically increasing
     * @param timestamp New block timestamp
     * @param previousTimestamp Previous block timestamp
     * @return true if monotonically increasing
     * 
     * Requirement 27.2: Require monotonically increasing timestamps
     */
    bool IsMonotonicallyIncreasing(uint64_t timestamp, uint64_t previousTimestamp) const;

    /**
     * @brief Get the minimum valid timestamp for next block
     * @param previousTimestamp Previous block timestamp
     * @return Minimum valid timestamp
     */
    uint64_t GetMinimumNextTimestamp(uint64_t previousTimestamp) const;

    // =========================================================================
    // Future Timestamp Rejection (Requirement 27.3)
    // =========================================================================

    /**
     * @brief Check if timestamp is too far in the future
     * @param timestamp Block timestamp
     * @return true if timestamp is in the future beyond allowed threshold
     * 
     * Requirement 27.3: Reject blocks with future timestamps (>30 seconds)
     */
    bool IsFutureTimestamp(uint64_t timestamp) const;

    /**
     * @brief Get the maximum allowed timestamp for current time
     * @return Maximum allowed timestamp
     */
    uint64_t GetMaxAllowedTimestamp() const;

    /**
     * @brief Get current system time
     * @return Current Unix timestamp
     */
    uint64_t GetCurrentTime() const;

    // =========================================================================
    // Complete Validation
    // =========================================================================

    /**
     * @brief Validate a block timestamp
     * @param timestamp Block timestamp to validate
     * @param previousTimestamp Previous block timestamp
     * @param sequencer Sequencer who produced the block
     * @param blockNumber Block number
     * @return Validation result with details
     * 
     * Validates:
     * - L1 timestamp binding (Requirement 27.1)
     * - Monotonicity (Requirement 27.2)
     * - Future timestamp (Requirement 27.3)
     * - Manipulation patterns (Requirement 27.6)
     */
    TimestampValidationResult ValidateTimestamp(
        uint64_t timestamp,
        uint64_t previousTimestamp,
        const uint160& sequencer,
        uint64_t blockNumber
    );

    /**
     * @brief Record a validated timestamp for history tracking
     * @param blockNumber Block number
     * @param timestamp Block timestamp
     * @param sequencer Sequencer address
     * @param l1Drift Drift from L1 timestamp
     * @param previousBlockDrift Drift from previous block
     */
    void RecordTimestamp(
        uint64_t blockNumber,
        uint64_t timestamp,
        const uint160& sequencer,
        int64_t l1Drift,
        int64_t previousBlockDrift
    );

    // =========================================================================
    // Manipulation Detection (Requirements 27.4, 27.6)
    // =========================================================================

    /**
     * @brief Detect timestamp manipulation patterns
     * @param sequencer Sequencer to check
     * @return true if manipulation detected
     * 
     * Requirement 27.6: Detect timestamp manipulation patterns
     */
    bool DetectManipulation(const uint160& sequencer) const;

    /**
     * @brief Get sequencer timestamp behavior
     * @param sequencer Sequencer address
     * @return Behavior data (empty if not tracked)
     */
    std::optional<SequencerTimestampBehavior> GetSequencerBehavior(const uint160& sequencer) const;

    /**
     * @brief Get list of sequencers flagged for manipulation
     * @return List of flagged sequencer addresses
     * 
     * Requirement 27.4: Penalize sequencers who manipulate timestamps
     */
    std::vector<uint160> GetFlaggedSequencers() const;

    /**
     * @brief Clear manipulation flag for a sequencer
     * @param sequencer Sequencer address
     */
    void ClearManipulationFlag(const uint160& sequencer);

    // =========================================================================
    // History and Statistics
    // =========================================================================

    /**
     * @brief Get timestamp history
     * @param count Number of entries to return (0 = all)
     * @return Recent timestamp history entries
     */
    std::vector<TimestampHistoryEntry> GetHistory(size_t count = 0) const;

    /**
     * @brief Get average L1 drift across all recent blocks
     * @return Average drift in seconds
     */
    uint64_t GetAverageL1Drift() const;

    /**
     * @brief Get the last recorded timestamp
     * @return Last timestamp (0 if no history)
     */
    uint64_t GetLastTimestamp() const;

    /**
     * @brief Get the last recorded block number
     * @return Last block number (0 if no history)
     */
    uint64_t GetLastBlockNumber() const;

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Clear all history and tracking data (for testing)
     */
    void Clear();

    /**
     * @brief Get number of tracked sequencers
     * @return Sequencer count
     */
    size_t GetTrackedSequencerCount() const;

    /**
     * @brief Get history size
     * @return Number of entries in history
     */
    size_t GetHistorySize() const;

    /**
     * @brief Set custom time source (for testing)
     * @param timeFunc Function that returns current Unix timestamp
     */
    void SetTimeSource(std::function<uint64_t()> timeFunc);

    /**
     * @brief Reset to system time source
     */
    void ResetTimeSource();

private:
    /** Current L1 timestamp reference */
    L1TimestampReference l1Reference_;

    /** Timestamp history (recent blocks) */
    std::deque<TimestampHistoryEntry> history_;

    /** Per-sequencer behavior tracking */
    std::map<uint160, SequencerTimestampBehavior> sequencerBehavior_;

    /** Custom time source (for testing) */
    std::function<uint64_t()> timeSource_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_timestamp_;

    /**
     * @brief Get current time from time source
     * @return Current Unix timestamp
     */
    uint64_t GetTimeInternal() const;

    /**
     * @brief Update sequencer behavior after validation
     * @param sequencer Sequencer address
     * @param blockNumber Block number
     * @param l1Drift Drift from L1 timestamp
     * @param isViolation Whether this was a violation
     */
    void UpdateSequencerBehavior(
        const uint160& sequencer,
        uint64_t blockNumber,
        int64_t l1Drift,
        bool isViolation
    );

    /**
     * @brief Ensure sequencer behavior entry exists
     * @param sequencer Sequencer address
     * @return Reference to behavior entry
     */
    SequencerTimestampBehavior& EnsureSequencerBehavior(const uint160& sequencer);

    /**
     * @brief Clean up old history entries
     */
    void CleanupHistory();
};

} // namespace l2

#endif // CASCOIN_L2_TIMESTAMP_VALIDATOR_H
