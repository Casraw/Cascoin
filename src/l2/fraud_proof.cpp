// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file fraud_proof.cpp
 * @brief Implementation of the Fraud Proof System for Cascoin L2
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <l2/fraud_proof.h>
#include <l2/state_manager.h>
#include <hash.h>
#include <util.h>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

FraudProofSystem::FraudProofSystem(uint64_t chainId)
    : chainId_(chainId)
    , nextSessionId_(1)
{
}

// ============================================================================
// Single-Round Fraud Proofs (Requirements 5.1, 5.2)
// ============================================================================

bool FraudProofSystem::SubmitFraudProof(const FraudProof& proof, uint64_t currentTime)
{
    LOCK(cs_fraudproof_);

    // Validate basic structure
    if (!proof.ValidateStructure()) {
        return false;
    }

    // Check chain ID matches
    if (proof.l2ChainId != chainId_) {
        return false;
    }

    // Check challenge bond is sufficient
    if (proof.challengeBond < FRAUD_PROOF_CHALLENGE_BOND) {
        return false;
    }

    // Check if state root is still within challenge period
    auto deadlineIt = stateRootDeadlines_.find(proof.disputedStateRoot);
    if (deadlineIt != stateRootDeadlines_.end()) {
        if (currentTime > deadlineIt->second) {
            // Challenge period has expired
            return false;
        }
    }

    // Check for duplicate proof
    uint256 proofHash = proof.GetHash();
    if (activeProofs_.find(proofHash) != activeProofs_.end()) {
        return false;
    }

    // Store the proof
    activeProofs_[proofHash] = proof;
    proofResults_[proofHash] = FraudProofResult::PENDING;

    return true;
}

FraudProofVerificationResult FraudProofSystem::VerifyFraudProof(const FraudProof& proof)
{
    LOCK(cs_fraudproof_);

    // Validate structure
    if (!proof.ValidateStructure()) {
        return FraudProofVerificationResult::Invalid("Invalid proof structure");
    }

    // Check if we have the previous state root
    if (proof.previousStateRoot.IsNull()) {
        return FraudProofVerificationResult::Invalid("Missing previous state root");
    }

    // Re-execute transactions to compute expected state root
    uint256 computedStateRoot = proof.previousStateRoot;
    uint64_t totalGasUsed = 0;

    for (const auto& tx : proof.relevantTransactions) {
        ExecutionResult result = ReExecuteOnL1(tx, computedStateRoot);
        if (!result.success) {
            // Transaction failed during re-execution
            // This could indicate the sequencer included an invalid transaction
            if (proof.type == FraudProofType::INVALID_TRANSACTION) {
                return FraudProofVerificationResult::Valid(uint256(), proof.disputedStateRoot);
            }
            return FraudProofVerificationResult::Invalid("Transaction execution failed: " + result.error);
        }
        computedStateRoot = result.postStateRoot;
        totalGasUsed += result.gasUsed;
    }

    // Compare computed state root with disputed state root
    if (computedStateRoot != proof.disputedStateRoot) {
        // State roots don't match - fraud proof is valid
        FraudProofVerificationResult result = FraudProofVerificationResult::Valid(
            computedStateRoot, proof.disputedStateRoot);
        result.gasUsed = totalGasUsed;

        // Update proof result
        uint256 proofHash = proof.GetHash();
        proofResults_[proofHash] = FraudProofResult::VALID;

        return result;
    }

    // State roots match - fraud proof is invalid
    FraudProofVerificationResult result = FraudProofVerificationResult::Invalid(
        "Computed state root matches disputed state root");
    result.expectedStateRoot = computedStateRoot;
    result.actualStateRoot = proof.disputedStateRoot;
    result.gasUsed = totalGasUsed;

    // Update proof result
    uint256 proofHash = proof.GetHash();
    proofResults_[proofHash] = FraudProofResult::INVALID;

    return result;
}

ExecutionResult FraudProofSystem::ReExecuteOnL1(const CMutableTransaction& tx, const uint256& preStateRoot)
{
    // Create a temporary state manager for re-execution
    L2StateManager tempState(chainId_);

    // In a real implementation, we would:
    // 1. Load the state from preStateRoot
    // 2. Execute the transaction
    // 3. Return the new state root

    // For now, we simulate execution based on transaction type
    // This is a simplified implementation for testing

    // Check if transaction is valid
    if (tx.vin.empty() && tx.vout.empty()) {
        return ExecutionResult::Failure("Empty transaction");
    }

    // Simulate gas usage based on transaction size
    uint64_t gasUsed = 21000; // Base gas
    // Calculate approximate size from inputs and outputs
    gasUsed += (tx.vin.size() * 148 + tx.vout.size() * 34) * 16; // Data gas

    // Compute a deterministic post-state root based on pre-state and transaction
    CHashWriter ss(SER_GETHASH, 0);
    ss << preStateRoot;
    ss << tx.GetHash();
    uint256 postStateRoot = ss.GetHash();

    return ExecutionResult::Success(gasUsed, postStateRoot);
}

std::optional<FraudProof> FraudProofSystem::GetFraudProof(const uint256& proofHash) const
{
    LOCK(cs_fraudproof_);

    auto it = activeProofs_.find(proofHash);
    if (it != activeProofs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

FraudProofResult FraudProofSystem::GetFraudProofResult(const uint256& proofHash) const
{
    LOCK(cs_fraudproof_);

    auto it = proofResults_.find(proofHash);
    if (it != proofResults_.end()) {
        return it->second;
    }
    return FraudProofResult::PENDING;
}

bool FraudProofSystem::IsStateRootFinalized(const uint256& stateRoot, uint64_t currentTime) const
{
    LOCK(cs_fraudproof_);

    auto it = stateRootDeadlines_.find(stateRoot);
    if (it == stateRootDeadlines_.end()) {
        // Unknown state root - not finalized
        return false;
    }

    // Finalized if current time is past the challenge deadline
    return currentTime >= it->second;
}

uint64_t FraudProofSystem::GetChallengeDeadline(const uint256& stateRoot) const
{
    LOCK(cs_fraudproof_);

    auto it = stateRootDeadlines_.find(stateRoot);
    if (it != stateRootDeadlines_.end()) {
        return it->second;
    }
    return 0;
}

void FraudProofSystem::RegisterStateRoot(const uint256& stateRoot, uint64_t blockNumber, uint64_t deadline)
{
    LOCK(cs_fraudproof_);

    stateRootDeadlines_[stateRoot] = deadline;
    stateRootBlocks_[stateRoot] = blockNumber;
}


// ============================================================================
// Interactive Fraud Proofs (Requirement 5.6)
// ============================================================================

uint256 FraudProofSystem::StartInteractiveProof(
    const uint256& disputedStateRoot,
    const uint160& challenger,
    const uint160& sequencer,
    uint64_t totalSteps,
    uint64_t currentTime)
{
    LOCK(cs_fraudproof_);

    // Validate inputs
    if (disputedStateRoot.IsNull() || challenger.IsNull() || sequencer.IsNull()) {
        return uint256();
    }

    if (totalSteps == 0 || totalSteps > MAX_INTERACTIVE_STEPS) {
        return uint256();
    }

    // Generate session ID
    uint256 sessionId = GenerateSessionId(challenger, sequencer, currentTime);

    // Check for duplicate session
    if (interactiveSessions_.find(sessionId) != interactiveSessions_.end()) {
        return uint256();
    }

    // Create new session
    InteractiveProofSession session;
    session.sessionId = sessionId;
    session.challenger = challenger;
    session.sequencer = sequencer;
    session.state = InteractiveProofState::SEQUENCER_TURN; // Sequencer provides first step
    session.searchLower = 0;
    session.searchUpper = totalSteps;
    session.totalSteps = totalSteps;
    session.createdAt = currentTime;
    session.lastActivityAt = currentTime;
    session.stepDeadline = currentTime + INTERACTIVE_STEP_TIMEOUT;
    session.l2ChainId = chainId_;

    interactiveSessions_[sessionId] = session;

    return sessionId;
}

bool FraudProofSystem::SubmitInteractiveStep(
    const uint256& sessionId,
    const InteractiveFraudProofStep& step,
    uint64_t currentTime)
{
    LOCK(cs_fraudproof_);

    // Find session
    auto it = interactiveSessions_.find(sessionId);
    if (it == interactiveSessions_.end()) {
        return false;
    }

    InteractiveProofSession& session = it->second;

    // Check if session is still active
    if (session.IsResolved()) {
        return false;
    }

    // Check for timeout
    if (session.IsTimedOut(currentTime)) {
        // Handle timeout - the party who didn't respond loses
        if (session.IsChallengerTurn()) {
            session.winner = session.sequencer;
        } else {
            session.winner = session.challenger;
        }
        session.state = InteractiveProofState::TIMEOUT;
        return false;
    }

    // Validate step structure
    if (!step.ValidateStructure()) {
        return false;
    }

    // Check if it's the correct party's turn
    if (session.IsChallengerTurn() && step.submitter != session.challenger) {
        return false;
    }
    if (session.IsSequencerTurn() && step.submitter != session.sequencer) {
        return false;
    }

    // Validate step number is within current search range
    if (step.stepNumber < session.searchLower || step.stepNumber > session.searchUpper) {
        return false;
    }

    // Add step to session
    session.steps.push_back(step);
    session.lastActivityAt = currentTime;

    // Update search range based on binary search
    uint64_t midpoint = session.GetMidpoint();

    if (step.stepNumber <= midpoint) {
        // Narrow search to upper half
        session.searchLower = step.stepNumber;
    } else {
        // Narrow search to lower half
        session.searchUpper = step.stepNumber;
    }

    // Switch turns
    if (session.state == InteractiveProofState::CHALLENGER_TURN) {
        session.state = InteractiveProofState::SEQUENCER_TURN;
    } else {
        session.state = InteractiveProofState::CHALLENGER_TURN;
    }

    // Update deadline
    session.stepDeadline = currentTime + INTERACTIVE_STEP_TIMEOUT;

    // Check if binary search has converged
    if (session.HasConverged()) {
        // Found the invalid step - verify it
        session.invalidStepNumber = session.searchLower;

        // Verify the step at the convergence point
        bool stepValid = VerifyExecutionStep(step);

        if (stepValid) {
            // Sequencer was correct, challenger loses
            session.winner = session.sequencer;
        } else {
            // Sequencer was wrong, challenger wins
            session.winner = session.challenger;
        }

        session.state = InteractiveProofState::RESOLVED;
    }

    return true;
}

FraudProofResult FraudProofSystem::ResolveInteractiveProof(const uint256& sessionId, uint64_t currentTime)
{
    LOCK(cs_fraudproof_);

    auto it = interactiveSessions_.find(sessionId);
    if (it == interactiveSessions_.end()) {
        return FraudProofResult::INVALID;
    }

    InteractiveProofSession& session = it->second;

    // Check for timeout
    if (!session.IsResolved() && session.IsTimedOut(currentTime)) {
        // Handle timeout
        if (session.IsChallengerTurn()) {
            session.winner = session.sequencer;
        } else {
            session.winner = session.challenger;
        }
        session.state = InteractiveProofState::TIMEOUT;
    }

    if (!session.IsResolved()) {
        return FraudProofResult::PENDING;
    }

    // Determine result based on winner
    if (session.winner == session.challenger) {
        return FraudProofResult::VALID; // Fraud proof valid, sequencer cheated
    } else {
        return FraudProofResult::INVALID; // Fraud proof invalid, challenger wrong
    }
}

std::optional<InteractiveProofSession> FraudProofSystem::GetInteractiveSession(const uint256& sessionId) const
{
    LOCK(cs_fraudproof_);

    auto it = interactiveSessions_.find(sessionId);
    if (it != interactiveSessions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

size_t FraudProofSystem::ProcessTimeouts(uint64_t currentTime)
{
    LOCK(cs_fraudproof_);

    size_t resolved = 0;

    for (auto& [sessionId, session] : interactiveSessions_) {
        if (!session.IsResolved() && session.IsTimedOut(currentTime)) {
            // Handle timeout
            if (session.IsChallengerTurn()) {
                session.winner = session.sequencer;
            } else {
                session.winner = session.challenger;
            }
            session.state = InteractiveProofState::TIMEOUT;
            ++resolved;
        }
    }

    return resolved;
}

// ============================================================================
// Slashing and Rewards (Requirements 5.4, 5.5)
// ============================================================================

SlashingRecord FraudProofSystem::SlashSequencer(
    const uint160& sequencer,
    const FraudProof& proof,
    uint64_t currentTime)
{
    LOCK(cs_fraudproof_);

    SlashingRecord record;
    record.sequencerAddress = sequencer;
    record.fraudProofHash = proof.GetHash();
    record.challenger = proof.challengerAddress;
    record.slashedAt = currentTime;
    record.blockNumber = proof.disputedBlockNumber;

    // Get sequencer stake
    CAmount stake = GetSequencerStake(sequencer);

    // Calculate slashing amount based on fraud type
    record.slashedAmount = CalculateSlashingAmount(proof.type, stake);

    // Calculate reputation penalty
    record.reputationPenalty = CalculateReputationPenalty(proof.type);

    // Calculate challenger reward (50% of slashed amount)
    record.challengerReward = RewardChallenger(proof.challengerAddress, record.slashedAmount);

    // Update sequencer stake
    if (sequencerStakes_.find(sequencer) != sequencerStakes_.end()) {
        sequencerStakes_[sequencer] -= record.slashedAmount;
        if (sequencerStakes_[sequencer] < 0) {
            sequencerStakes_[sequencer] = 0;
        }
    }

    // Store slashing record
    slashingRecords_[sequencer].push_back(record);

    return record;
}

CAmount FraudProofSystem::RewardChallenger(const uint160& challenger, CAmount slashedAmount)
{
    LOCK(cs_fraudproof_);

    // Calculate reward (50% of slashed amount)
    CAmount reward = (slashedAmount * CHALLENGER_REWARD_PERCENT) / 100;

    // Track total rewards for challenger
    challengerRewards_[challenger] += reward;

    return reward;
}

std::vector<SlashingRecord> FraudProofSystem::GetSlashingRecords(const uint160& sequencer) const
{
    LOCK(cs_fraudproof_);

    auto it = slashingRecords_.find(sequencer);
    if (it != slashingRecords_.end()) {
        return it->second;
    }
    return {};
}

CAmount FraudProofSystem::GetTotalSlashed(const uint160& sequencer) const
{
    LOCK(cs_fraudproof_);

    CAmount total = 0;
    auto it = slashingRecords_.find(sequencer);
    if (it != slashingRecords_.end()) {
        for (const auto& record : it->second) {
            total += record.slashedAmount;
        }
    }
    return total;
}

// ============================================================================
// Utility Methods
// ============================================================================

size_t FraudProofSystem::GetActiveFraudProofCount() const
{
    LOCK(cs_fraudproof_);

    size_t count = 0;
    for (const auto& [hash, result] : proofResults_) {
        if (result == FraudProofResult::PENDING) {
            ++count;
        }
    }
    return count;
}

size_t FraudProofSystem::GetActiveSessionCount() const
{
    LOCK(cs_fraudproof_);

    size_t count = 0;
    for (const auto& [id, session] : interactiveSessions_) {
        if (!session.IsResolved()) {
            ++count;
        }
    }
    return count;
}

void FraudProofSystem::Clear()
{
    LOCK(cs_fraudproof_);

    activeProofs_.clear();
    proofResults_.clear();
    interactiveSessions_.clear();
    stateRootDeadlines_.clear();
    stateRootBlocks_.clear();
    slashingRecords_.clear();
    sequencerStakes_.clear();
    challengerRewards_.clear();
    nextSessionId_ = 1;
}

void FraudProofSystem::SetSequencerStake(const uint160& sequencer, CAmount stake)
{
    LOCK(cs_fraudproof_);
    sequencerStakes_[sequencer] = stake;
}

CAmount FraudProofSystem::GetSequencerStake(const uint160& sequencer) const
{
    LOCK(cs_fraudproof_);

    auto it = sequencerStakes_.find(sequencer);
    if (it != sequencerStakes_.end()) {
        return it->second;
    }
    return 0;
}

// ============================================================================
// Private Methods
// ============================================================================

uint256 FraudProofSystem::GenerateSessionId(
    const uint160& challenger,
    const uint160& sequencer,
    uint64_t timestamp) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << challenger;
    ss << sequencer;
    ss << timestamp;
    ss << nextSessionId_;
    return ss.GetHash();
}

uint64_t FraudProofSystem::BinarySearchInvalidStep(const InteractiveProofSession& session) const
{
    // The invalid step is at the convergence point of the binary search
    return session.searchLower;
}

bool FraudProofSystem::VerifyExecutionStep(const InteractiveFraudProofStep& step) const
{
    // Verify that executing the instruction on preStateRoot produces postStateRoot
    // This is a simplified implementation

    if (!step.ValidateStructure()) {
        return false;
    }

    // In a real implementation, we would:
    // 1. Load state from preStateRoot
    // 2. Execute the single instruction
    // 3. Compare result with postStateRoot

    // For now, we compute a deterministic result
    CHashWriter ss(SER_GETHASH, 0);
    ss << step.preStateRoot;
    ss << step.instruction;
    uint256 expectedPost = ss.GetHash();

    return expectedPost == step.postStateRoot;
}

CAmount FraudProofSystem::CalculateSlashingAmount(FraudProofType type, CAmount sequencerStake) const
{
    // Calculate slashing percentage based on fraud type severity
    uint32_t slashPercent = 0;

    switch (type) {
        case FraudProofType::DOUBLE_SPEND:
            slashPercent = 100; // Full slash for double spend
            break;
        case FraudProofType::INVALID_STATE_TRANSITION:
            slashPercent = 100; // Full slash for invalid state
            break;
        case FraudProofType::INVALID_SIGNATURE:
            slashPercent = 50; // 50% for signature issues
            break;
        case FraudProofType::DATA_WITHHOLDING:
            slashPercent = 75; // 75% for data withholding
            break;
        case FraudProofType::TIMESTAMP_MANIPULATION:
            slashPercent = 50; // 50% for timestamp manipulation
            break;
        case FraudProofType::INVALID_TRANSACTION:
            slashPercent = 75; // 75% for invalid transaction
            break;
        default:
            slashPercent = 50; // Default 50%
            break;
    }

    CAmount slashAmount = (sequencerStake * slashPercent) / 100;

    // Ensure minimum slashing amount
    if (slashAmount < MIN_SLASHING_AMOUNT && sequencerStake >= MIN_SLASHING_AMOUNT) {
        slashAmount = MIN_SLASHING_AMOUNT;
    }

    return slashAmount;
}

int32_t FraudProofSystem::CalculateReputationPenalty(FraudProofType type) const
{
    // Calculate reputation penalty based on fraud type severity
    switch (type) {
        case FraudProofType::DOUBLE_SPEND:
            return -50; // Severe penalty
        case FraudProofType::INVALID_STATE_TRANSITION:
            return -40;
        case FraudProofType::INVALID_SIGNATURE:
            return -20;
        case FraudProofType::DATA_WITHHOLDING:
            return -30;
        case FraudProofType::TIMESTAMP_MANIPULATION:
            return -15;
        case FraudProofType::INVALID_TRANSACTION:
            return -25;
        default:
            return -20;
    }
}

} // namespace l2
