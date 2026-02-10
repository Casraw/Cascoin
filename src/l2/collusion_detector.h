// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_COLLUSION_DETECTOR_H
#define CASCOIN_L2_COLLUSION_DETECTOR_H

/**
 * @file collusion_detector.h
 * @brief Anti-Collusion Detection System for Cascoin L2
 * 
 * This file implements detection mechanisms for sequencer collusion.
 * It analyzes timing correlations, voting patterns, and integrates
 * with the wallet clustering system to identify colluding sequencers.
 * 
 * Requirements: 22.1, 22.2, 22.4
 */

#include <l2/l2_common.h>
#include <l2/sequencer_discovery.h>
#include <l2/sequencer_consensus.h>
#include <uint256.h>
#include <sync.h>
#include <hash.h>

#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <chrono>
#include <optional>
#include <functional>

namespace l2 {

/**
 * @brief Types of collusion detected
 */
enum class CollusionType {
    NONE,                       // No collusion detected
    TIMING_CORRELATION,         // Suspiciously correlated timing patterns
    VOTING_PATTERN,             // Coordinated voting behavior
    WALLET_CLUSTER,             // Sequencers from same wallet cluster
    STAKE_CONCENTRATION,        // Single entity controls too much stake
    COMBINED                    // Multiple indicators present
};

/**
 * @brief Severity level of detected collusion
 */
enum class CollusionSeverity {
    LOW,        // Suspicious but not conclusive
    MEDIUM,     // Likely collusion, warrants investigation
    HIGH,       // Strong evidence of collusion
    CRITICAL    // Definitive collusion, immediate action required
};

/**
 * @brief Record of a sequencer's action for timing analysis
 */
struct SequencerAction {
    uint160 sequencerAddress;
    uint64_t timestamp;         // Unix timestamp in milliseconds
    uint256 blockHash;          // Block this action relates to
    VoteType voteType;          // Vote cast (if voting action)
    bool isBlockProposal;       // True if this was a block proposal
    uint64_t slotNumber;        // Slot number for this action
    
    SequencerAction()
        : timestamp(0)
        , voteType(VoteType::ABSTAIN)
        , isBlockProposal(false)
        , slotNumber(0)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(timestamp);
        READWRITE(blockHash);
        if (ser_action.ForRead()) {
            uint8_t voteVal;
            READWRITE(voteVal);
            voteType = static_cast<VoteType>(voteVal);
        } else {
            uint8_t voteVal = static_cast<uint8_t>(voteType);
            READWRITE(voteVal);
        }
        READWRITE(isBlockProposal);
        READWRITE(slotNumber);
    }
};

/**
 * @brief Voting pattern statistics for a sequencer pair
 */
struct VotingPatternStats {
    uint160 sequencer1;
    uint160 sequencer2;
    uint32_t totalVotesCounted;     // Total votes analyzed
    uint32_t matchingVotes;         // Votes where both voted the same
    uint32_t opposingVotes;         // Votes where they voted differently
    double correlationScore;        // -1.0 to 1.0 (1.0 = perfect correlation)
    uint64_t lastUpdated;           // Timestamp of last update
    
    VotingPatternStats()
        : totalVotesCounted(0)
        , matchingVotes(0)
        , opposingVotes(0)
        , correlationScore(0.0)
        , lastUpdated(0)
    {}
    
    /**
     * @brief Calculate correlation score from vote counts
     */
    void UpdateCorrelation() {
        if (totalVotesCounted == 0) {
            correlationScore = 0.0;
            return;
        }
        // Correlation = (matching - opposing) / total
        // Cast to int32_t to handle negative results correctly
        correlationScore = static_cast<double>(static_cast<int32_t>(matchingVotes) - static_cast<int32_t>(opposingVotes)) / totalVotesCounted;
    }
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencer1);
        READWRITE(sequencer2);
        READWRITE(totalVotesCounted);
        READWRITE(matchingVotes);
        READWRITE(opposingVotes);
        // Serialize double as uint64 (range -1.0 to 1.0 -> 0 to 2000000)
        if (ser_action.ForRead()) {
            uint64_t corrScoreInt;
            READWRITE(corrScoreInt);
            correlationScore = (static_cast<double>(corrScoreInt) / 1000000.0) - 1.0;
        } else {
            uint64_t corrScoreInt = static_cast<uint64_t>((correlationScore + 1.0) * 1000000);
            READWRITE(corrScoreInt);
        }
        READWRITE(lastUpdated);
    }
};

/**
 * @brief Timing correlation statistics for a sequencer pair
 */
struct TimingCorrelationStats {
    uint160 sequencer1;
    uint160 sequencer2;
    uint32_t sampleCount;           // Number of timing samples
    double avgTimeDelta;            // Average time difference in ms
    double stdDevTimeDelta;         // Standard deviation of time differences
    double correlationScore;        // 0.0 to 1.0 (1.0 = highly correlated)
    uint64_t lastUpdated;
    
    TimingCorrelationStats()
        : sampleCount(0)
        , avgTimeDelta(0.0)
        , stdDevTimeDelta(0.0)
        , correlationScore(0.0)
        , lastUpdated(0)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencer1);
        READWRITE(sequencer2);
        READWRITE(sampleCount);
        // Serialize doubles as uint64
        if (ser_action.ForRead()) {
            uint64_t avgInt, stdDevInt, corrInt;
            READWRITE(avgInt);
            READWRITE(stdDevInt);
            READWRITE(corrInt);
            avgTimeDelta = static_cast<double>(avgInt) / 1000.0;
            stdDevTimeDelta = static_cast<double>(stdDevInt) / 1000.0;
            correlationScore = static_cast<double>(corrInt) / 1000000.0;
        } else {
            uint64_t avgInt = static_cast<uint64_t>(avgTimeDelta * 1000);
            uint64_t stdDevInt = static_cast<uint64_t>(stdDevTimeDelta * 1000);
            uint64_t corrInt = static_cast<uint64_t>(correlationScore * 1000000);
            READWRITE(avgInt);
            READWRITE(stdDevInt);
            READWRITE(corrInt);
        }
        READWRITE(lastUpdated);
    }
};

/**
 * @brief Result of collusion detection analysis
 */
struct CollusionDetectionResult {
    CollusionType type;
    CollusionSeverity severity;
    std::vector<uint160> involvedSequencers;
    double confidenceScore;         // 0.0 to 1.0
    std::string description;
    uint64_t detectionTimestamp;
    uint256 evidenceHash;           // Hash of evidence data
    
    // Detailed metrics
    double timingCorrelation;
    double votingCorrelation;
    bool sameWalletCluster;
    double stakeConcentration;
    
    CollusionDetectionResult()
        : type(CollusionType::NONE)
        , severity(CollusionSeverity::LOW)
        , confidenceScore(0.0)
        , detectionTimestamp(0)
        , timingCorrelation(0.0)
        , votingCorrelation(0.0)
        , sameWalletCluster(false)
        , stakeConcentration(0.0)
    {}
    
    /**
     * @brief Check if collusion was detected
     */
    bool IsCollusionDetected() const {
        return type != CollusionType::NONE;
    }
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            uint8_t typeVal, sevVal;
            READWRITE(typeVal);
            READWRITE(sevVal);
            type = static_cast<CollusionType>(typeVal);
            severity = static_cast<CollusionSeverity>(sevVal);
        } else {
            uint8_t typeVal = static_cast<uint8_t>(type);
            uint8_t sevVal = static_cast<uint8_t>(severity);
            READWRITE(typeVal);
            READWRITE(sevVal);
        }
        READWRITE(involvedSequencers);
        if (ser_action.ForRead()) {
            uint64_t confInt;
            READWRITE(confInt);
            confidenceScore = static_cast<double>(confInt) / 1000000.0;
        } else {
            uint64_t confInt = static_cast<uint64_t>(confidenceScore * 1000000);
            READWRITE(confInt);
        }
        READWRITE(description);
        READWRITE(detectionTimestamp);
        READWRITE(evidenceHash);
        // Serialize detailed metrics
        if (ser_action.ForRead()) {
            uint64_t timingInt, votingInt, stakeInt;
            READWRITE(timingInt);
            READWRITE(votingInt);
            READWRITE(sameWalletCluster);
            READWRITE(stakeInt);
            timingCorrelation = (static_cast<double>(timingInt) / 1000000.0) - 1.0;
            votingCorrelation = (static_cast<double>(votingInt) / 1000000.0) - 1.0;
            stakeConcentration = static_cast<double>(stakeInt) / 1000000.0;
        } else {
            uint64_t timingInt = static_cast<uint64_t>((timingCorrelation + 1.0) * 1000000);
            uint64_t votingInt = static_cast<uint64_t>((votingCorrelation + 1.0) * 1000000);
            uint64_t stakeInt = static_cast<uint64_t>(stakeConcentration * 1000000);
            READWRITE(timingInt);
            READWRITE(votingInt);
            READWRITE(sameWalletCluster);
            READWRITE(stakeInt);
        }
    }
};

/**
 * @brief Whistleblower report for collusion
 */
struct WhistleblowerReport {
    uint160 reporterAddress;
    std::vector<uint160> accusedSequencers;
    CollusionType accusedType;
    std::string evidence;
    uint256 evidenceHash;
    uint64_t reportTimestamp;
    std::vector<unsigned char> signature;
    CAmount bondAmount;             // Bond posted by reporter
    bool isValidated;               // Whether report has been validated
    bool isRewarded;                // Whether reporter has been rewarded
    
    WhistleblowerReport()
        : accusedType(CollusionType::NONE)
        , reportTimestamp(0)
        , bondAmount(0)
        , isValidated(false)
        , isRewarded(false)
    {}
    
    /**
     * @brief Get hash for signing
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << reporterAddress;
        ss << accusedSequencers;
        ss << static_cast<uint8_t>(accusedType);
        ss << evidenceHash;
        ss << reportTimestamp;
        return ss.GetHash();
    }
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(reporterAddress);
        READWRITE(accusedSequencers);
        if (ser_action.ForRead()) {
            uint8_t typeVal;
            READWRITE(typeVal);
            accusedType = static_cast<CollusionType>(typeVal);
        } else {
            uint8_t typeVal = static_cast<uint8_t>(accusedType);
            READWRITE(typeVal);
        }
        READWRITE(evidence);
        READWRITE(evidenceHash);
        READWRITE(reportTimestamp);
        READWRITE(signature);
        READWRITE(bondAmount);
        READWRITE(isValidated);
        READWRITE(isRewarded);
    }
};


/**
 * @brief Anti-Collusion Detection System
 * 
 * Detects and prevents sequencer collusion through multiple mechanisms:
 * 1. Timing Correlation Detection - Identifies suspiciously synchronized actions
 * 2. Voting Pattern Analysis - Detects coordinated voting behavior
 * 3. Wallet Cluster Integration - Ensures sequencers are from different wallets
 * 4. Stake Concentration Monitoring - Prevents single entity dominance
 * 
 * Requirements: 22.1, 22.2, 22.4
 */
class CollusionDetector {
public:
    /** Callback for collusion detection alerts */
    using CollusionAlertCallback = std::function<void(const CollusionDetectionResult&)>;
    
    /** Callback for whistleblower reward processing */
    using WhistleblowerRewardCallback = std::function<void(const uint160&, CAmount)>;

    /**
     * @brief Construct a new Collusion Detector
     * @param chainId The L2 chain ID
     */
    explicit CollusionDetector(uint64_t chainId);

    // ========================================================================
    // Timing Correlation Detection
    // ========================================================================

    /**
     * @brief Record a sequencer action for timing analysis
     * 
     * Records the timestamp and details of sequencer actions to build
     * a timing profile for correlation detection.
     * 
     * @param action The sequencer action to record
     * 
     * Requirements: 22.1
     */
    void RecordSequencerAction(const SequencerAction& action);

    /**
     * @brief Analyze timing correlation between two sequencers
     * 
     * Calculates how correlated the timing of actions is between
     * two sequencers. High correlation may indicate coordination.
     * 
     * @param seq1 First sequencer address
     * @param seq2 Second sequencer address
     * @return Timing correlation statistics
     * 
     * Requirements: 22.1
     */
    TimingCorrelationStats AnalyzeTimingCorrelation(
        const uint160& seq1,
        const uint160& seq2) const;

    /**
     * @brief Detect timing correlation across all sequencer pairs
     * 
     * Analyzes all pairs of sequencers for suspicious timing patterns.
     * 
     * @return Vector of pairs with high timing correlation
     * 
     * Requirements: 22.1
     */
    std::vector<std::pair<uint160, uint160>> DetectTimingCorrelation() const;

    // ========================================================================
    // Voting Pattern Analysis
    // ========================================================================

    /**
     * @brief Record a vote for pattern analysis
     * 
     * @param blockHash The block being voted on
     * @param voter The voting sequencer
     * @param vote The vote cast
     * 
     * Requirements: 22.1
     */
    void RecordVote(const uint256& blockHash, const uint160& voter, VoteType vote);

    /**
     * @brief Analyze voting pattern correlation between two sequencers
     * 
     * Calculates how often two sequencers vote the same way.
     * Perfect correlation (always same vote) is suspicious.
     * 
     * @param seq1 First sequencer address
     * @param seq2 Second sequencer address
     * @return Voting pattern statistics
     * 
     * Requirements: 22.1
     */
    VotingPatternStats AnalyzeVotingPattern(
        const uint160& seq1,
        const uint160& seq2) const;

    /**
     * @brief Detect suspicious voting patterns across all sequencer pairs
     * 
     * @return Vector of pairs with suspicious voting correlation
     * 
     * Requirements: 22.1
     */
    std::vector<std::pair<uint160, uint160>> DetectVotingPatternCollusion() const;

    // ========================================================================
    // Wallet Cluster Integration
    // ========================================================================

    /**
     * @brief Check if two sequencers are from the same wallet cluster
     * 
     * Uses the CVM wallet clustering system to determine if two
     * sequencer addresses belong to the same wallet.
     * 
     * @param seq1 First sequencer address
     * @param seq2 Second sequencer address
     * @return true if sequencers are in the same wallet cluster
     * 
     * Requirements: 22.2
     */
    bool AreInSameWalletCluster(const uint160& seq1, const uint160& seq2) const;

    /**
     * @brief Get the wallet cluster ID for a sequencer
     * 
     * @param sequencer Sequencer address
     * @return Cluster ID (primary address of cluster)
     */
    uint160 GetWalletCluster(const uint160& sequencer) const;

    /**
     * @brief Detect sequencers from the same wallet cluster
     * 
     * Identifies groups of sequencers that belong to the same wallet,
     * which violates the requirement for sequencers from different clusters.
     * 
     * @return Map of cluster ID to sequencer addresses in that cluster
     * 
     * Requirements: 22.2
     */
    std::map<uint160, std::vector<uint160>> DetectWalletClusterViolations() const;

    /**
     * @brief Validate that a new sequencer is from a different wallet cluster
     * 
     * @param newSequencer Address of the new sequencer
     * @param existingSequencers List of existing sequencer addresses
     * @return true if new sequencer is from a different cluster
     * 
     * Requirements: 22.2
     */
    bool ValidateNewSequencerCluster(
        const uint160& newSequencer,
        const std::vector<uint160>& existingSequencers) const;

    // ========================================================================
    // Stake Concentration Monitoring
    // ========================================================================

    /**
     * @brief Calculate stake concentration for an entity
     * 
     * Determines what percentage of total sequencer stake is controlled
     * by addresses in the same wallet cluster.
     * 
     * @param sequencer Sequencer address to check
     * @return Stake concentration as percentage (0.0 to 1.0)
     * 
     * Requirements: 22.3
     */
    double CalculateStakeConcentration(const uint160& sequencer) const;

    /**
     * @brief Check if stake concentration exceeds limit
     * 
     * @param sequencer Sequencer address to check
     * @return true if stake concentration exceeds 20% limit
     * 
     * Requirements: 22.3
     */
    bool ExceedsStakeConcentrationLimit(const uint160& sequencer) const;

    /**
     * @brief Get all entities exceeding stake concentration limit
     * 
     * @return Map of cluster ID to stake concentration percentage
     */
    std::map<uint160, double> GetStakeConcentrationViolations() const;

    // ========================================================================
    // Comprehensive Collusion Detection
    // ========================================================================

    /**
     * @brief Run comprehensive collusion detection
     * 
     * Analyzes all sequencers for collusion using all detection methods:
     * - Timing correlation
     * - Voting patterns
     * - Wallet clustering
     * - Stake concentration
     * 
     * @return Vector of all detected collusion instances
     * 
     * Requirements: 22.1, 22.2, 22.4
     */
    std::vector<CollusionDetectionResult> RunFullDetection();

    /**
     * @brief Analyze a specific pair of sequencers for collusion
     * 
     * @param seq1 First sequencer address
     * @param seq2 Second sequencer address
     * @return Detection result for this pair
     */
    CollusionDetectionResult AnalyzeSequencerPair(
        const uint160& seq1,
        const uint160& seq2);

    /**
     * @brief Get collusion risk score for a sequencer
     * 
     * Calculates an overall risk score based on all collusion indicators.
     * 
     * @param sequencer Sequencer address
     * @return Risk score (0.0 = no risk, 1.0 = definite collusion)
     */
    double GetCollusionRiskScore(const uint160& sequencer) const;

    // ========================================================================
    // Whistleblower System
    // ========================================================================

    /**
     * @brief Submit a whistleblower report
     * 
     * Allows anyone to report suspected collusion with evidence.
     * Reporter must post a bond that is returned if report is valid.
     * 
     * @param report The whistleblower report
     * @return true if report was accepted
     * 
     * Requirements: 22.5
     */
    bool SubmitWhistleblowerReport(const WhistleblowerReport& report);

    /**
     * @brief Validate a whistleblower report
     * 
     * Investigates the claims in a whistleblower report.
     * 
     * @param reportId Hash of the report
     * @return true if report is validated as accurate
     * 
     * Requirements: 22.5
     */
    bool ValidateWhistleblowerReport(const uint256& reportId);

    /**
     * @brief Get pending whistleblower reports
     * 
     * @return Vector of unvalidated reports
     */
    std::vector<WhistleblowerReport> GetPendingReports() const;

    /**
     * @brief Process whistleblower reward
     * 
     * Awards the reporter if their report led to slashing.
     * 
     * @param reportId Hash of the validated report
     * @return Reward amount
     * 
     * Requirements: 22.5
     */
    CAmount ProcessWhistleblowerReward(const uint256& reportId);

    // ========================================================================
    // Slashing and Penalties
    // ========================================================================

    /**
     * @brief Slash colluding sequencers
     * 
     * Applies penalties to sequencers found to be colluding.
     * 
     * @param result The collusion detection result
     * @return true if slashing was successful
     * 
     * Requirements: 22.6
     */
    bool SlashColludingSequencers(const CollusionDetectionResult& result);

    /**
     * @brief Get slashing amount for collusion type
     * 
     * @param type Type of collusion
     * @param severity Severity level
     * @return Amount to slash in satoshis
     */
    CAmount GetSlashingAmount(CollusionType type, CollusionSeverity severity) const;

    // ========================================================================
    // Configuration and Callbacks
    // ========================================================================

    /**
     * @brief Register callback for collusion alerts
     * @param callback Function to call when collusion is detected
     */
    void RegisterAlertCallback(CollusionAlertCallback callback);

    /**
     * @brief Register callback for whistleblower rewards
     * @param callback Function to call when reward is processed
     */
    void RegisterRewardCallback(WhistleblowerRewardCallback callback);

    /**
     * @brief Set timing correlation threshold
     * @param threshold Threshold (0.0 to 1.0, default 0.8)
     */
    void SetTimingCorrelationThreshold(double threshold);

    /**
     * @brief Set voting correlation threshold
     * @param threshold Threshold (-1.0 to 1.0, default 0.9)
     */
    void SetVotingCorrelationThreshold(double threshold);

    /**
     * @brief Set stake concentration limit
     * @param limit Limit as percentage (0.0 to 1.0, default 0.2)
     */
    void SetStakeConcentrationLimit(double limit);

    /**
     * @brief Get timing correlation threshold
     */
    double GetTimingCorrelationThreshold() const { return timingCorrelationThreshold_; }

    /**
     * @brief Get voting correlation threshold
     */
    double GetVotingCorrelationThreshold() const { return votingCorrelationThreshold_; }

    /**
     * @brief Get stake concentration limit
     */
    double GetStakeConcentrationLimit() const { return stakeConcentrationLimit_; }

    /**
     * @brief Clear all detection data (for testing)
     */
    void Clear();

    /**
     * @brief Get the L2 chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Set sequencer stake for testing
     * @param address Sequencer address
     * @param stake Stake amount
     */
    void SetTestSequencerStake(const uint160& address, CAmount stake);

    /**
     * @brief Set wallet cluster for testing
     * @param address Address
     * @param clusterId Cluster ID
     */
    void SetTestWalletCluster(const uint160& address, const uint160& clusterId);

    /**
     * @brief Clear test data
     */
    void ClearTestData();

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Recorded sequencer actions for timing analysis */
    std::map<uint160, std::deque<SequencerAction>> sequencerActions_;

    /** Voting records per block (block hash -> voter -> vote) */
    std::map<uint256, std::map<uint160, VoteType>> votingRecords_;

    /** Cached timing correlation stats */
    mutable std::map<std::pair<uint160, uint160>, TimingCorrelationStats> timingCorrelationCache_;

    /** Cached voting pattern stats */
    mutable std::map<std::pair<uint160, uint160>, VotingPatternStats> votingPatternCache_;

    /** Whistleblower reports (report hash -> report) */
    std::map<uint256, WhistleblowerReport> whistleblowerReports_;

    /** Detected collusion results */
    std::vector<CollusionDetectionResult> detectedCollusions_;

    /** Alert callbacks */
    std::vector<CollusionAlertCallback> alertCallbacks_;

    /** Reward callbacks */
    std::vector<WhistleblowerRewardCallback> rewardCallbacks_;

    /** Timing correlation threshold (default 0.8) */
    double timingCorrelationThreshold_;

    /** Voting correlation threshold (default 0.9) */
    double votingCorrelationThreshold_;

    /** Stake concentration limit (default 0.2 = 20%) */
    double stakeConcentrationLimit_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_collusion_;

    /** Maximum actions to store per sequencer */
    static constexpr size_t MAX_ACTIONS_PER_SEQUENCER = 1000;

    /** Maximum voting records to store */
    static constexpr size_t MAX_VOTING_RECORDS = 10000;

    /** Minimum samples for correlation analysis */
    static constexpr size_t MIN_SAMPLES_FOR_CORRELATION = 10;

    /** Whistleblower bond amount */
    static constexpr CAmount WHISTLEBLOWER_BOND = 10 * COIN;

    /** Whistleblower reward percentage of slashed amount */
    static constexpr double WHISTLEBLOWER_REWARD_PERCENT = 0.1;

    /** Test sequencer stakes */
    std::map<uint160, CAmount> testSequencerStakes_;

    /** Test wallet clusters */
    std::map<uint160, uint160> testWalletClusters_;

    /**
     * @brief Calculate timing correlation score
     */
    double CalculateTimingCorrelationScore(
        const std::deque<SequencerAction>& actions1,
        const std::deque<SequencerAction>& actions2) const;

    /**
     * @brief Calculate voting correlation score
     */
    double CalculateVotingCorrelationScore(
        const uint160& seq1,
        const uint160& seq2) const;

    /**
     * @brief Determine collusion severity from scores
     */
    CollusionSeverity DetermineSeverity(
        double timingCorr,
        double votingCorr,
        bool sameCluster,
        double stakeConc) const;

    /**
     * @brief Generate evidence hash for detection result
     */
    uint256 GenerateEvidenceHash(const CollusionDetectionResult& result) const;

    /**
     * @brief Notify alert callbacks
     */
    void NotifyAlertCallbacks(const CollusionDetectionResult& result);

    /**
     * @brief Prune old action records
     */
    void PruneOldActions();

    /**
     * @brief Prune old voting records
     */
    void PruneOldVotingRecords();

    /**
     * @brief Get sequencer stake (from discovery or test data)
     */
    CAmount GetSequencerStake(const uint160& address) const;

    /**
     * @brief Get total sequencer stake
     */
    CAmount GetTotalSequencerStake() const;

    /**
     * @brief Make ordered pair key for cache lookup
     */
    std::pair<uint160, uint160> MakeOrderedPair(
        const uint160& a,
        const uint160& b) const;
};

/**
 * @brief Global collusion detector instance
 */
CollusionDetector& GetCollusionDetector();

/**
 * @brief Initialize the global collusion detector
 * @param chainId The L2 chain ID
 */
void InitCollusionDetector(uint64_t chainId);

/**
 * @brief Check if collusion detector is initialized
 * @return true if initialized
 */
bool IsCollusionDetectorInitialized();

} // namespace l2

#endif // CASCOIN_L2_COLLUSION_DETECTOR_H
