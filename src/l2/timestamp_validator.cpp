// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file timestamp_validator.cpp
 * @brief Implementation of Timestamp Validation for Cascoin Layer 2
 * 
 * Requirements: 27.1, 27.2, 27.3, 27.4, 27.5, 27.6
 */

#include <l2/timestamp_validator.h>
#include <utiltime.h>

#include <algorithm>
#include <numeric>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

TimestampValidator::TimestampValidator()
    : timeSource_(nullptr)
{
}

// ============================================================================
// L1 Timestamp Binding (Requirement 27.1)
// ============================================================================

void TimestampValidator::UpdateL1Reference(uint64_t blockNumber, uint64_t timestamp, const uint256& blockHash)
{
    LOCK(cs_timestamp_);
    l1Reference_ = L1TimestampReference(blockNumber, timestamp, blockHash);
}

L1TimestampReference TimestampValidator::GetL1Reference() const
{
    LOCK(cs_timestamp_);
    return l1Reference_;
}

uint64_t TimestampValidator::GetL1TimestampOracle() const
{
    LOCK(cs_timestamp_);
    return l1Reference_.timestamp;
}

bool TimestampValidator::IsWithinL1Drift(uint64_t l2Timestamp) const
{
    LOCK(cs_timestamp_);
    
    if (!l1Reference_.IsValid()) {
        // No L1 reference available, allow any timestamp
        return true;
    }
    
    int64_t drift = CalculateL1Drift(l2Timestamp);
    uint64_t absDrift = static_cast<uint64_t>(drift < 0 ? -drift : drift);
    
    return absDrift <= MAX_L1_TIMESTAMP_DRIFT;
}

int64_t TimestampValidator::CalculateL1Drift(uint64_t l2Timestamp) const
{
    // Note: Caller should hold cs_timestamp_ lock
    if (!l1Reference_.IsValid()) {
        return 0;
    }
    
    return static_cast<int64_t>(l2Timestamp) - static_cast<int64_t>(l1Reference_.timestamp);
}

// ============================================================================
// Monotonicity Check (Requirement 27.2)
// ============================================================================

bool TimestampValidator::IsMonotonicallyIncreasing(uint64_t timestamp, uint64_t previousTimestamp) const
{
    // Timestamps must be strictly increasing
    return timestamp > previousTimestamp;
}

uint64_t TimestampValidator::GetMinimumNextTimestamp(uint64_t previousTimestamp) const
{
    return previousTimestamp + MIN_TIMESTAMP_INCREMENT;
}

// ============================================================================
// Future Timestamp Rejection (Requirement 27.3)
// ============================================================================

bool TimestampValidator::IsFutureTimestamp(uint64_t timestamp) const
{
    uint64_t currentTime = GetTimeInternal();
    uint64_t maxAllowed = currentTime + MAX_FUTURE_TIMESTAMP_SECONDS;
    
    return timestamp > maxAllowed;
}

uint64_t TimestampValidator::GetMaxAllowedTimestamp() const
{
    return GetTimeInternal() + MAX_FUTURE_TIMESTAMP_SECONDS;
}

uint64_t TimestampValidator::GetCurrentTime() const
{
    return GetTimeInternal();
}

// ============================================================================
// Complete Validation
// ============================================================================

TimestampValidationResult TimestampValidator::ValidateTimestamp(
    uint64_t timestamp,
    uint64_t previousTimestamp,
    const uint160& sequencer,
    uint64_t blockNumber)
{
    LOCK(cs_timestamp_);
    
    // Check monotonicity (Requirement 27.2)
    if (!IsMonotonicallyIncreasing(timestamp, previousTimestamp)) {
        UpdateSequencerBehavior(sequencer, blockNumber, 0, true);
        return TimestampValidationResult::Invalid(
            "Timestamp not monotonically increasing: " + 
            std::to_string(timestamp) + " <= " + std::to_string(previousTimestamp));
    }
    
    // Check future timestamp (Requirement 27.3)
    if (IsFutureTimestamp(timestamp)) {
        UpdateSequencerBehavior(sequencer, blockNumber, 0, true);
        return TimestampValidationResult::Invalid(
            "Timestamp too far in future: " + std::to_string(timestamp) + 
            " > max allowed " + std::to_string(GetMaxAllowedTimestamp()));
    }
    
    // Calculate drifts
    int64_t l1Drift = CalculateL1Drift(timestamp);
    int64_t previousBlockDrift = static_cast<int64_t>(timestamp) - static_cast<int64_t>(previousTimestamp);
    
    // Check L1 drift (Requirement 27.1)
    if (l1Reference_.IsValid()) {
        uint64_t absDrift = static_cast<uint64_t>(l1Drift < 0 ? -l1Drift : l1Drift);
        if (absDrift > MAX_L1_TIMESTAMP_DRIFT) {
            UpdateSequencerBehavior(sequencer, blockNumber, l1Drift, true);
            return TimestampValidationResult::Invalid(
                "L1 timestamp drift too large: " + std::to_string(l1Drift) + 
                " seconds (max: " + std::to_string(MAX_L1_TIMESTAMP_DRIFT) + ")");
        }
    }
    
    // Update sequencer behavior (no violation)
    UpdateSequencerBehavior(sequencer, blockNumber, l1Drift, false);
    
    // Check for manipulation patterns (Requirement 27.6)
    if (DetectManipulation(sequencer)) {
        return TimestampValidationResult::ManipulationDetected(sequencer);
    }
    
    return TimestampValidationResult::Valid(l1Drift, previousBlockDrift);
}

void TimestampValidator::RecordTimestamp(
    uint64_t blockNumber,
    uint64_t timestamp,
    const uint160& sequencer,
    int64_t l1Drift,
    int64_t previousBlockDrift)
{
    LOCK(cs_timestamp_);
    
    TimestampHistoryEntry entry;
    entry.blockNumber = blockNumber;
    entry.timestamp = timestamp;
    entry.sequencer = sequencer;
    entry.l1ReferenceTimestamp = l1Reference_.timestamp;
    entry.l1Drift = l1Drift;
    entry.previousBlockDrift = previousBlockDrift;
    
    history_.push_back(entry);
    
    // Clean up old entries
    CleanupHistory();
}

// ============================================================================
// Manipulation Detection (Requirements 27.4, 27.6)
// ============================================================================

bool TimestampValidator::DetectManipulation(const uint160& sequencer) const
{
    // Note: Caller should hold cs_timestamp_ lock
    auto it = sequencerBehavior_.find(sequencer);
    if (it == sequencerBehavior_.end()) {
        return false;
    }
    
    const SequencerTimestampBehavior& behavior = it->second;
    
    // Check if already flagged
    if (behavior.flaggedForManipulation) {
        return true;
    }
    
    // Check consecutive violations
    if (behavior.consecutiveViolations >= MANIPULATION_VIOLATION_THRESHOLD) {
        return true;
    }
    
    // Check violation rate (more than 20% violations is suspicious)
    // Only flag if there are actual violations - high average drift alone
    // is not manipulation if all individual timestamps passed validation
    if (behavior.blocksProduced >= 10 && behavior.GetViolationRate() > 20) {
        return true;
    }
    
    // Check average drift threshold only if there have been violations
    // This prevents false positives when L1 reference is stale but all
    // individual timestamps are valid
    if (behavior.blocksProduced >= 10 && 
        behavior.violationCount > 0 &&
        behavior.averageL1Drift > MANIPULATION_DETECTION_THRESHOLD) {
        return true;
    }
    
    return false;
}

std::optional<SequencerTimestampBehavior> TimestampValidator::GetSequencerBehavior(const uint160& sequencer) const
{
    LOCK(cs_timestamp_);
    
    auto it = sequencerBehavior_.find(sequencer);
    if (it == sequencerBehavior_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

std::vector<uint160> TimestampValidator::GetFlaggedSequencers() const
{
    LOCK(cs_timestamp_);
    
    std::vector<uint160> flagged;
    for (const auto& pair : sequencerBehavior_) {
        if (pair.second.flaggedForManipulation) {
            flagged.push_back(pair.first);
        }
    }
    
    return flagged;
}

void TimestampValidator::ClearManipulationFlag(const uint160& sequencer)
{
    LOCK(cs_timestamp_);
    
    auto it = sequencerBehavior_.find(sequencer);
    if (it != sequencerBehavior_.end()) {
        it->second.flaggedForManipulation = false;
        it->second.consecutiveViolations = 0;
    }
}

// ============================================================================
// History and Statistics
// ============================================================================

std::vector<TimestampHistoryEntry> TimestampValidator::GetHistory(size_t count) const
{
    LOCK(cs_timestamp_);
    
    if (count == 0 || count >= history_.size()) {
        return std::vector<TimestampHistoryEntry>(history_.begin(), history_.end());
    }
    
    // Return the most recent 'count' entries
    auto start = history_.end() - count;
    return std::vector<TimestampHistoryEntry>(start, history_.end());
}

uint64_t TimestampValidator::GetAverageL1Drift() const
{
    LOCK(cs_timestamp_);
    
    if (history_.empty()) {
        return 0;
    }
    
    uint64_t totalDrift = 0;
    for (const auto& entry : history_) {
        totalDrift += static_cast<uint64_t>(entry.l1Drift < 0 ? -entry.l1Drift : entry.l1Drift);
    }
    
    return totalDrift / history_.size();
}

uint64_t TimestampValidator::GetLastTimestamp() const
{
    LOCK(cs_timestamp_);
    
    if (history_.empty()) {
        return 0;
    }
    
    return history_.back().timestamp;
}

uint64_t TimestampValidator::GetLastBlockNumber() const
{
    LOCK(cs_timestamp_);
    
    if (history_.empty()) {
        return 0;
    }
    
    return history_.back().blockNumber;
}

// ============================================================================
// Utility Methods
// ============================================================================

void TimestampValidator::Clear()
{
    LOCK(cs_timestamp_);
    
    l1Reference_ = L1TimestampReference();
    history_.clear();
    sequencerBehavior_.clear();
}

size_t TimestampValidator::GetTrackedSequencerCount() const
{
    LOCK(cs_timestamp_);
    return sequencerBehavior_.size();
}

size_t TimestampValidator::GetHistorySize() const
{
    LOCK(cs_timestamp_);
    return history_.size();
}

void TimestampValidator::SetTimeSource(std::function<uint64_t()> timeFunc)
{
    LOCK(cs_timestamp_);
    timeSource_ = timeFunc;
}

void TimestampValidator::ResetTimeSource()
{
    LOCK(cs_timestamp_);
    timeSource_ = nullptr;
}

// ============================================================================
// Private Methods
// ============================================================================

uint64_t TimestampValidator::GetTimeInternal() const
{
    if (timeSource_) {
        return timeSource_();
    }
    return GetTime();
}

void TimestampValidator::UpdateSequencerBehavior(
    const uint160& sequencer,
    uint64_t blockNumber,
    int64_t l1Drift,
    bool isViolation)
{
    // Note: Caller should hold cs_timestamp_ lock
    SequencerTimestampBehavior& behavior = EnsureSequencerBehavior(sequencer);
    
    behavior.blocksProduced++;
    behavior.lastBlockNumber = blockNumber;
    
    // Update drift statistics
    uint64_t absDrift = static_cast<uint64_t>(l1Drift < 0 ? -l1Drift : l1Drift);
    
    // Update average drift (exponential moving average)
    if (behavior.blocksProduced == 1) {
        behavior.averageL1Drift = absDrift;
    } else {
        // EMA with alpha = 0.1
        behavior.averageL1Drift = (behavior.averageL1Drift * 9 + absDrift) / 10;
    }
    
    // Update max drift
    if (absDrift > behavior.maxL1Drift) {
        behavior.maxL1Drift = absDrift;
    }
    
    // Update violation tracking
    if (isViolation) {
        behavior.violationCount++;
        behavior.consecutiveViolations++;
        
        // Flag for manipulation if threshold reached
        if (behavior.consecutiveViolations >= MANIPULATION_VIOLATION_THRESHOLD) {
            behavior.flaggedForManipulation = true;
        }
    } else {
        // Reset consecutive violations on valid block
        behavior.consecutiveViolations = 0;
    }
}

SequencerTimestampBehavior& TimestampValidator::EnsureSequencerBehavior(const uint160& sequencer)
{
    // Note: Caller should hold cs_timestamp_ lock
    auto it = sequencerBehavior_.find(sequencer);
    if (it == sequencerBehavior_.end()) {
        auto result = sequencerBehavior_.emplace(sequencer, SequencerTimestampBehavior(sequencer));
        return result.first->second;
    }
    return it->second;
}

void TimestampValidator::CleanupHistory()
{
    // Note: Caller should hold cs_timestamp_ lock
    while (history_.size() > TIMESTAMP_HISTORY_SIZE) {
        history_.pop_front();
    }
}

} // namespace l2
