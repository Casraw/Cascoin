// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file challenge_handler.cpp
 * @brief Implementation of Challenge Handler for L2 Withdrawal Challenges
 * 
 * Requirements: 4.6, 29.1, 29.2
 */

#include <l2/challenge_handler.h>
#include <l2/fraud_proof.h>
#include <hash.h>
#include <util.h>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

ChallengeHandler::ChallengeHandler(uint64_t chainId)
    : chainId_(chainId)
    , totalBondsHeld_(0)
    , nextChallengeId_(1)
{
}

// ============================================================================
// Challenge Submission (Requirement 29.1)
// ============================================================================

std::optional<WithdrawalChallenge> ChallengeHandler::ChallengeWithdrawal(
    const uint256& withdrawalId,
    const uint160& challenger,
    CAmount bondAmount,
    const std::vector<unsigned char>& fraudProof,
    const std::string& reason,
    uint64_t currentTime,
    uint32_t challengerHatScore)
{
    LOCK(cs_challenge_);

    // Check if challenger is banned
    if (IsChallengerBanned(challenger, currentTime)) {
        return std::nullopt;  // Challenger is banned
    }

    // Check if challenger has reached limit (Requirement 29.3)
    if (IsAtChallengeLimit(challenger)) {
        return std::nullopt;  // Too many active challenges
    }

    // Validate bond amount (Requirement 29.1)
    if (bondAmount < WITHDRAWAL_CHALLENGE_BOND) {
        return std::nullopt;  // Insufficient bond
    }

    // Check if withdrawal is challengeable
    if (!IsWithdrawalChallengeable(withdrawalId)) {
        return std::nullopt;  // Withdrawal not found or not challengeable
    }

    // Check if challenge deadline has passed
    auto deadlineIt = challengeableWithdrawals_.find(withdrawalId);
    if (deadlineIt != challengeableWithdrawals_.end() && currentTime > deadlineIt->second) {
        return std::nullopt;  // Challenge period has ended
    }

    // Verify fraud proof structure (basic validation)
    if (!fraudProof.empty() && !VerifyFraudProofStructure(fraudProof)) {
        return std::nullopt;  // Invalid fraud proof structure
    }

    // Create the challenge
    WithdrawalChallenge challenge;
    challenge.challengeId = GenerateChallengeId(withdrawalId, challenger, currentTime);
    challenge.withdrawalId = withdrawalId;
    challenge.challengerAddress = challenger;
    challenge.bondAmount = bondAmount;
    challenge.fraudProof = fraudProof;
    challenge.reason = reason;
    challenge.status = ChallengeStatus::PENDING;
    challenge.submittedAt = currentTime;
    challenge.resolvedAt = 0;
    challenge.deadline = currentTime + CHALLENGE_RESOLUTION_TIMEOUT;
    challenge.l2ChainId = chainId_;
    challenge.challengerHatScore = challengerHatScore;
    challenge.bondProcessed = false;

    // Store the challenge
    challenges_[challenge.challengeId] = challenge;
    challengesByWithdrawal_[withdrawalId].insert(challenge.challengeId);
    activeChallengesByChallenger_[challenger].insert(challenge.challengeId);

    // Update bond tracking
    totalBondsHeld_ += bondAmount;

    // Update challenger stats
    if (challengerStats_.find(challenger) == challengerStats_.end()) {
        ChallengerStats stats;
        stats.challengerAddress = challenger;
        challengerStats_[challenger] = stats;
    }
    challengerStats_[challenger].totalChallenges++;

    return challenge;
}

bool ChallengeHandler::CanChallengeWithdrawal(
    const uint256& withdrawalId,
    const uint160& challenger) const
{
    LOCK(cs_challenge_);

    // Check if withdrawal is challengeable
    if (!IsWithdrawalChallengeable(withdrawalId)) {
        return false;
    }

    // Check if challenger is banned (use current time approximation)
    // In production, this would use actual current time
    auto statsIt = challengerStats_.find(challenger);
    if (statsIt != challengerStats_.end() && statsIt->second.isBanned) {
        return false;
    }

    // Check challenge limit
    if (IsAtChallengeLimit(challenger)) {
        return false;
    }

    return true;
}

// ============================================================================
// Challenge Validation (Requirement 29.2)
// ============================================================================

bool ChallengeHandler::ValidateChallenge(
    const uint256& challengeId,
    const WithdrawalRequest& withdrawal)
{
    LOCK(cs_challenge_);

    auto it = challenges_.find(challengeId);
    if (it == challenges_.end()) {
        return false;  // Challenge not found
    }

    WithdrawalChallenge& challenge = it->second;

    // Can only validate pending challenges
    if (challenge.status != ChallengeStatus::PENDING) {
        return false;
    }

    // Verify the challenge is for this withdrawal
    if (challenge.withdrawalId != withdrawal.withdrawalId) {
        return false;
    }

    // Update status to validating
    challenge.status = ChallengeStatus::VALIDATING;

    // If fraud proof is provided, attempt to verify it
    if (!challenge.fraudProof.empty()) {
        // Deserialize and verify the fraud proof
        FraudProof proof;
        if (proof.Deserialize(challenge.fraudProof)) {
            // Basic structural validation
            if (!proof.ValidateStructure()) {
                return false;  // Invalid proof structure
            }

            // Check if the disputed state root matches the withdrawal's state root
            if (proof.disputedStateRoot == withdrawal.stateRoot) {
                // The fraud proof targets the correct state
                // In a full implementation, we would re-execute transactions
                // to verify the state transition is invalid
                return true;
            }
        }
    }

    // If no fraud proof or verification failed, challenge is invalid
    return false;
}

ChallengeResult ChallengeHandler::ProcessChallengeResult(
    const uint256& challengeId,
    bool isValid,
    uint64_t currentTime)
{
    LOCK(cs_challenge_);

    ChallengeResult result;
    result.challengeId = challengeId;
    result.resolvedAt = currentTime;

    auto it = challenges_.find(challengeId);
    if (it == challenges_.end()) {
        result.finalStatus = ChallengeStatus::CANCELLED;
        result.resultReason = "Challenge not found";
        return result;
    }

    WithdrawalChallenge& challenge = it->second;

    // Can only process pending or validating challenges
    if (challenge.status != ChallengeStatus::PENDING && 
        challenge.status != ChallengeStatus::VALIDATING) {
        result.finalStatus = challenge.status;
        result.resultReason = "Challenge already resolved";
        return result;
    }

    result.bondAmount = challenge.bondAmount;

    if (isValid) {
        // Challenge is valid - withdrawal should be cancelled
        // Bond is returned to challenger
        challenge.status = ChallengeStatus::VALID;
        result.finalStatus = ChallengeStatus::VALID;
        result.bondSlashed = false;
        result.bondRecipient = challenge.challengerAddress;
        result.resultReason = "Challenge valid - withdrawal cancelled";

        // Update challenger stats
        if (challengerStats_.find(challenge.challengerAddress) != challengerStats_.end()) {
            challengerStats_[challenge.challengerAddress].validChallenges++;
            challengerStats_[challenge.challengerAddress].totalBondsReturned += challenge.bondAmount;
        }
    } else {
        // Challenge is invalid - bond is slashed (Requirement 29.2)
        challenge.status = ChallengeStatus::INVALID;
        result.finalStatus = ChallengeStatus::INVALID;
        result.bondSlashed = true;
        // Bond goes to the challenged party (withdrawal requester)
        // In this case, we don't have direct access to the withdrawal requester,
        // so we leave bondRecipient empty (would be set by the bridge contract)
        result.resultReason = "Challenge invalid - bond slashed";

        // Update challenger stats
        if (challengerStats_.find(challenge.challengerAddress) != challengerStats_.end()) {
            ChallengerStats& stats = challengerStats_[challenge.challengerAddress];
            stats.invalidChallenges++;
            stats.totalBondsLost += challenge.bondAmount;

            // Check if challenger should be banned (Requirement 29.6)
            if (stats.ShouldBeBanned() && !stats.isBanned) {
                // Ban for 7 days
                stats.isBanned = true;
                stats.bannedUntil = currentTime + (7 * 24 * 60 * 60);
            }
        }
    }

    // Mark bond as processed
    challenge.bondProcessed = true;
    challenge.resolvedAt = currentTime;

    // Update bond tracking
    totalBondsHeld_ -= challenge.bondAmount;

    // Remove from active challenges
    activeChallengesByChallenger_[challenge.challengerAddress].erase(challengeId);

    return result;
}

// ============================================================================
// Challenge Management
// ============================================================================

std::optional<WithdrawalChallenge> ChallengeHandler::GetChallenge(
    const uint256& challengeId) const
{
    LOCK(cs_challenge_);
    auto it = challenges_.find(challengeId);
    if (it != challenges_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<WithdrawalChallenge> ChallengeHandler::GetChallengesForWithdrawal(
    const uint256& withdrawalId) const
{
    LOCK(cs_challenge_);
    std::vector<WithdrawalChallenge> result;

    auto it = challengesByWithdrawal_.find(withdrawalId);
    if (it != challengesByWithdrawal_.end()) {
        for (const auto& challengeId : it->second) {
            auto challengeIt = challenges_.find(challengeId);
            if (challengeIt != challenges_.end()) {
                result.push_back(challengeIt->second);
            }
        }
    }

    return result;
}

std::vector<WithdrawalChallenge> ChallengeHandler::GetActiveChallengesByChallenger(
    const uint160& challenger) const
{
    LOCK(cs_challenge_);
    std::vector<WithdrawalChallenge> result;

    auto it = activeChallengesByChallenger_.find(challenger);
    if (it != activeChallengesByChallenger_.end()) {
        for (const auto& challengeId : it->second) {
            auto challengeIt = challenges_.find(challengeId);
            if (challengeIt != challenges_.end() && challengeIt->second.IsActive()) {
                result.push_back(challengeIt->second);
            }
        }
    }

    return result;
}

uint32_t ChallengeHandler::GetActiveChallengeCount(const uint160& challenger) const
{
    LOCK(cs_challenge_);
    auto it = activeChallengesByChallenger_.find(challenger);
    if (it != activeChallengesByChallenger_.end()) {
        return static_cast<uint32_t>(it->second.size());
    }
    return 0;
}

size_t ChallengeHandler::ProcessExpiredChallenges(uint64_t currentTime)
{
    LOCK(cs_challenge_);
    size_t expiredCount = 0;

    for (auto& pair : challenges_) {
        WithdrawalChallenge& challenge = pair.second;

        if (challenge.IsExpired(currentTime) && !challenge.bondProcessed) {
            // Mark as expired
            challenge.status = ChallengeStatus::EXPIRED;
            challenge.resolvedAt = currentTime;
            challenge.bondProcessed = true;

            // Return bond to challenger (expired challenges don't lose bond)
            totalBondsHeld_ -= challenge.bondAmount;

            // Update stats
            if (challengerStats_.find(challenge.challengerAddress) != challengerStats_.end()) {
                challengerStats_[challenge.challengerAddress].expiredChallenges++;
                challengerStats_[challenge.challengerAddress].totalBondsReturned += challenge.bondAmount;
            }

            // Remove from active challenges
            activeChallengesByChallenger_[challenge.challengerAddress].erase(challenge.challengeId);

            expiredCount++;
        }
    }

    return expiredCount;
}

// ============================================================================
// Challenger Management (Requirement 29.6)
// ============================================================================

ChallengerStats ChallengeHandler::GetChallengerStats(const uint160& challenger) const
{
    LOCK(cs_challenge_);
    auto it = challengerStats_.find(challenger);
    if (it != challengerStats_.end()) {
        return it->second;
    }

    ChallengerStats stats;
    stats.challengerAddress = challenger;
    return stats;
}

bool ChallengeHandler::IsChallengerBanned(const uint160& challenger, uint64_t currentTime) const
{
    LOCK(cs_challenge_);
    auto it = challengerStats_.find(challenger);
    if (it != challengerStats_.end()) {
        const ChallengerStats& stats = it->second;
        if (stats.isBanned && currentTime < stats.bannedUntil) {
            return true;
        }
    }
    return false;
}

void ChallengeHandler::BanChallenger(const uint160& challenger, uint64_t duration, uint64_t currentTime)
{
    LOCK(cs_challenge_);

    if (challengerStats_.find(challenger) == challengerStats_.end()) {
        ChallengerStats stats;
        stats.challengerAddress = challenger;
        challengerStats_[challenger] = stats;
    }

    challengerStats_[challenger].isBanned = true;
    challengerStats_[challenger].bannedUntil = currentTime + duration;
}

// ============================================================================
// Bond Management
// ============================================================================

CAmount ChallengeHandler::GetTotalBondsHeld() const
{
    LOCK(cs_challenge_);
    return totalBondsHeld_;
}

CAmount ChallengeHandler::GetBondsHeldByChallenger(const uint160& challenger) const
{
    LOCK(cs_challenge_);
    CAmount total = 0;

    auto it = activeChallengesByChallenger_.find(challenger);
    if (it != activeChallengesByChallenger_.end()) {
        for (const auto& challengeId : it->second) {
            auto challengeIt = challenges_.find(challengeId);
            if (challengeIt != challenges_.end() && !challengeIt->second.bondProcessed) {
                total += challengeIt->second.bondAmount;
            }
        }
    }

    return total;
}

// ============================================================================
// Utility Methods
// ============================================================================

size_t ChallengeHandler::GetTotalChallengeCount() const
{
    LOCK(cs_challenge_);
    return challenges_.size();
}

size_t ChallengeHandler::GetActiveChallengeCount() const
{
    LOCK(cs_challenge_);
    size_t count = 0;
    for (const auto& pair : challenges_) {
        if (pair.second.IsActive()) {
            count++;
        }
    }
    return count;
}

void ChallengeHandler::Clear()
{
    LOCK(cs_challenge_);
    challenges_.clear();
    challengesByWithdrawal_.clear();
    activeChallengesByChallenger_.clear();
    challengerStats_.clear();
    challengeableWithdrawals_.clear();
    totalBondsHeld_ = 0;
    nextChallengeId_ = 1;
}

void ChallengeHandler::RegisterChallengeableWithdrawal(const uint256& withdrawalId, uint64_t deadline)
{
    LOCK(cs_challenge_);
    challengeableWithdrawals_[withdrawalId] = deadline;
}

bool ChallengeHandler::IsWithdrawalChallengeable(const uint256& withdrawalId) const
{
    LOCK(cs_challenge_);
    return challengeableWithdrawals_.find(withdrawalId) != challengeableWithdrawals_.end();
}

// ============================================================================
// Private Helper Methods
// ============================================================================

uint256 ChallengeHandler::GenerateChallengeId(
    const uint256& withdrawalId,
    const uint160& challenger,
    uint64_t timestamp) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << withdrawalId << challenger << timestamp << nextChallengeId_ << chainId_;
    return ss.GetHash();
}

void ChallengeHandler::UpdateChallengerStats(
    const uint160& challenger,
    const ChallengeResult& result)
{
    // This is called internally, lock should already be held
    if (challengerStats_.find(challenger) == challengerStats_.end()) {
        ChallengerStats stats;
        stats.challengerAddress = challenger;
        challengerStats_[challenger] = stats;
    }

    ChallengerStats& stats = challengerStats_[challenger];

    switch (result.finalStatus) {
        case ChallengeStatus::VALID:
            stats.validChallenges++;
            stats.totalBondsReturned += result.bondAmount;
            break;
        case ChallengeStatus::INVALID:
            stats.invalidChallenges++;
            stats.totalBondsLost += result.bondAmount;
            break;
        case ChallengeStatus::EXPIRED:
            stats.expiredChallenges++;
            stats.totalBondsReturned += result.bondAmount;
            break;
        default:
            break;
    }
}

bool ChallengeHandler::IsAtChallengeLimit(const uint160& challenger) const
{
    // Lock should already be held
    auto it = activeChallengesByChallenger_.find(challenger);
    if (it != activeChallengesByChallenger_.end()) {
        return it->second.size() >= MAX_CHALLENGES_PER_ADDRESS;
    }
    return false;
}

bool ChallengeHandler::VerifyFraudProofStructure(const std::vector<unsigned char>& fraudProof) const
{
    if (fraudProof.empty()) {
        return true;  // Empty proof is allowed (for simple challenges)
    }

    // Try to deserialize as a FraudProof
    FraudProof proof;
    if (!proof.Deserialize(fraudProof)) {
        return false;
    }

    // Validate basic structure
    return proof.ValidateStructure();
}

} // namespace l2
