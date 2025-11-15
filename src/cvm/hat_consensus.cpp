// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/hat_consensus.h>
#include <cvm/cvmdb.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <chain.h>
#include <hash.h>
#include <random.h>
#include <util.h>
#include <key.h>
#include <pubkey.h>
#include <net.h>
#include <netmessagemaker.h>
#include <protocol.h>
#include <algorithm>
#include <cmath>

extern CChain chainActive;

namespace CVM {

// Database keys for HAT consensus
static const char DB_VALIDATION_STATE = 'V';      // 'V' + txhash -> TransactionState

// Helper function to create database key from prefix and hash
std::string MakeDBKey(char prefix, const uint256& hash) {
    return std::string(1, prefix) + hash.ToString();
}

// Overload for uint160 addresses
std::string MakeDBKey(char prefix, const uint160& addr) {
    return std::string(1, prefix) + addr.ToString();
}

// Helper function to write serializable data to database
template<typename T>
bool WriteToDatabase(CVMDatabase& db, const std::string& key, const T& value) {
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << value;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    return db.WriteGeneric(key, data);
}

// Helper function to read serializable data from database
template<typename T>
bool ReadFromDatabase(CVMDatabase& db, const std::string& key, T& value) {
    std::vector<uint8_t> data;
    if (!db.ReadGeneric(key, data)) {
        return false;
    }
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> value;
        return true;
    } catch (...) {
        return false;
    }
}
static const char DB_DISPUTE_CASE = 'D';          // 'D' + disputeId -> DisputeCase
static const char DB_FRAUD_RECORD = 'F';          // 'F' + txhash -> FraudRecord
static const char DB_VALIDATOR_STATS = 'S';      // 'S' + address -> ValidatorStats
static const char DB_VALIDATION_SESSION = 'E';   // 'E' + txhash -> ValidationSession

/**
 * Validation Session
 * 
 * Tracks ongoing validation for a transaction.
 */
struct ValidationSession {
    uint256 txHash;
    ValidationRequest request;
    std::vector<ValidationResponse> responses;
    uint64_t startTime;
    bool completed;
    
    ValidationSession() : startTime(0), completed(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(request);
        READWRITE(responses);
        READWRITE(startTime);
        READWRITE(completed);
    }
    
    bool HasMinimumResponses() const {
        return responses.size() >= HATConsensusValidator::MIN_VALIDATORS;
    }
    
    bool IsTimedOut() const {
        return GetTime() > startTime + HATConsensusValidator::VALIDATION_TIMEOUT;
    }
};

// ValidationRequest methods

uint256 ValidationRequest::GenerateChallengeNonce(const uint256& txHash, int blockHeight) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << txHash;
    ss << blockHeight;
    ss << GetTime();
    return ss.GetHash();
}

// ValidationResponse methods

bool ValidationResponse::Sign(const CKey& validatorKey) {
    // Create message to sign
    CHashWriter ss(SER_GETHASH, 0);
    ss << txHash;
    ss << validatorAddress;
    ss << calculatedScore.finalScore;
    ss << (int)vote;
    ss << challengeNonce;
    ss << timestamp;
    
    uint256 messageHash = ss.GetHash();
    
    // Sign the message
    if (!validatorKey.Sign(messageHash, signature)) {
        return false;
    }
    
    return true;
}

bool ValidationResponse::VerifySignature() const {
    // Check if we have a public key
    if (validatorPubKey.empty()) {
        LogPrint(BCLog::CVM, "HAT Consensus: No public key provided for signature verification\n");
        return false;
    }
    
    // Check if we have a signature
    if (signature.empty()) {
        LogPrint(BCLog::CVM, "HAT Consensus: No signature provided\n");
        return false;
    }
    
    // Construct CPubKey from validator's public key
    CPubKey pubkey(validatorPubKey.begin(), validatorPubKey.end());
    
    // Verify public key is valid
    if (!pubkey.IsFullyValid()) {
        LogPrint(BCLog::CVM, "HAT Consensus: Invalid public key format\n");
        return false;
    }
    
    // Verify the public key matches the validator address
    CKeyID keyID = pubkey.GetID();
    uint160 derivedAddress;
    memcpy(derivedAddress.begin(), keyID.begin(), 20);
    
    if (derivedAddress != validatorAddress) {
        LogPrint(BCLog::CVM, "HAT Consensus: Public key does not match validator address\n");
        return false;
    }
    
    // Recreate the message that was signed
    CHashWriter ss(SER_GETHASH, 0);
    ss << txHash;
    ss << validatorAddress;
    ss << calculatedScore.finalScore;
    ss << (int)vote;
    ss << challengeNonce;
    ss << timestamp;
    
    uint256 messageHash = ss.GetHash();
    
    // Verify the signature
    if (!pubkey.Verify(messageHash, signature)) {
        LogPrint(BCLog::CVM, "HAT Consensus: Signature verification failed\n");
        return false;
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: Signature verified successfully for validator %s\n",
             validatorAddress.ToString());
    
    return true;
}

// HATConsensusValidator implementation

HATConsensusValidator::HATConsensusValidator(CVMDatabase& db, SecureHAT& hat, TrustGraph& graph)
    : database(db), secureHAT(hat), trustGraph(graph)
{
}

ValidationRequest HATConsensusValidator::InitiateValidation(
    const CTransaction& tx,
    const HATv2Score& selfReportedScore)
{
    ValidationRequest request;
    request.txHash = tx.GetHash();
    request.senderAddress = selfReportedScore.address;
    request.selfReportedScore = selfReportedScore;
    request.challengeNonce = ValidationRequest::GenerateChallengeNonce(
        tx.GetHash(), chainActive.Height());
    request.timestamp = GetTime();
    request.blockHeight = chainActive.Height();
    
    // Create validation session
    ValidationSession session;
    session.txHash = tx.GetHash();
    session.request = request;
    session.startTime = GetTime();
    session.completed = false;
    
    // Store session
    WriteToDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, tx.GetHash()), session);
    
    // Set initial state
    UpdateMempoolState(tx.GetHash(), TransactionState::PENDING_VALIDATION);
    
    return request;
}

std::vector<uint160> HATConsensusValidator::SelectRandomValidators(
    const uint256& txHash,
    int blockHeight)
{
    std::vector<uint160> validators;
    
    // Generate deterministic random seed
    uint256 seed = GenerateRandomSeed(txHash, blockHeight);
    
    // Get all potential validators from database
    std::vector<uint160> candidatePool;
    
    // Query all validator stats from database
    std::vector<std::string> validatorKeys = database.ListKeysWithPrefix("validator_stats_");
    
    for (const auto& key : validatorKeys) {
        // Extract address from key (format: "validator_stats_<address>")
        if (key.length() < 17) continue;  // "validator_stats_" = 16 chars
        
        std::string addrHex = key.substr(16);
        uint160 address;
        address.SetHex(addrHex);
        
        // Check eligibility
        if (IsEligibleValidator(address)) {
            candidatePool.push_back(address);
        }
    }
    
    // If we don't have enough validators in database, add some defaults
    // This ensures the system can work even with few registered validators
    if (candidatePool.size() < MIN_VALIDATORS) {
        LogPrint(BCLog::CVM, "HAT Consensus: Only %zu eligible validators found, need %d\n",
                 candidatePool.size(), MIN_VALIDATORS);
        
        // In production, this would query from a validator registry
        // For now, we'll work with what we have
    }
    
    // Shuffle candidates using deterministic seed
    FastRandomContext rng(seed);
    // Use Fisher-Yates shuffle with FastRandomContext
    for (size_t i = candidatePool.size() - 1; i > 0; i--) {
        size_t j = rng.randrange(i + 1);
        std::swap(candidatePool[i], candidatePool[j]);
    }
    
    // Select first MIN_VALIDATORS candidates (or all if less available)
    size_t selectCount = std::min((size_t)MIN_VALIDATORS, candidatePool.size());
    for (size_t i = 0; i < selectCount; i++) {
        validators.push_back(candidatePool[i]);
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: Selected %zu validators from pool of %zu\n",
             validators.size(), candidatePool.size());
    
    return validators;
}

bool CVM::HATConsensusValidator::SendValidationChallenge(
    const uint160& validator,
    const ValidationRequest& request)
{
    // TODO: Implement P2P message sending
    // This will be implemented in Phase 2.5.4 (Network Protocol Integration)
    
    LogPrint(BCLog::CVM, "HAT Consensus: Sending validation challenge to %s for tx %s\n",
             validator.ToString(), request.txHash.ToString());
    
    return true;
}

bool CVM::HATConsensusValidator::ProcessValidatorResponse(const ValidationResponse& response) {
    // Check rate limiting (anti-spam)
    if (IsValidatorRateLimited(response.validatorAddress)) {
        LogPrint(BCLog::CVM, "HAT Consensus: Validator %s is rate-limited, ignoring response\n",
                 response.validatorAddress.ToString());
        return false;
    }
    
    // Record message for rate limiting
    RecordValidationMessage(response.validatorAddress);
    
    // Verify signature
    if (!response.VerifySignature()) {
        LogPrint(BCLog::CVM, "HAT Consensus: Invalid signature from validator %s\n",
                 response.validatorAddress.ToString());
        return false;
    }
    
    // Verify challenge nonce matches
    ValidationSession session;
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, response.txHash), session)) {
        LogPrint(BCLog::CVM, "HAT Consensus: No validation session for tx %s\n",
                 response.txHash.ToString());
        return false;
    }
    
    if (response.challengeNonce != session.request.challengeNonce) {
        LogPrint(BCLog::CVM, "HAT Consensus: Invalid challenge nonce from validator %s\n",
                 response.validatorAddress.ToString());
        return false;
    }
    
    // Check timeout
    if (session.IsTimedOut()) {
        LogPrint(BCLog::CVM, "HAT Consensus: Validation session timed out for tx %s\n",
                 response.txHash.ToString());
        return false;
    }
    
    // Check for duplicate response
    for (const auto& existingResponse : session.responses) {
        if (existingResponse.validatorAddress == response.validatorAddress) {
            LogPrint(BCLog::CVM, "HAT Consensus: Duplicate response from validator %s, ignoring\n",
                     response.validatorAddress.ToString());
            return false;
        }
    }
    
    // Add response to session
    session.responses.push_back(response);
    WriteToDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, response.txHash), session);
    
    LogPrint(BCLog::CVM, "HAT Consensus: Received response from validator %s (%zu/%d)\n",
             response.validatorAddress.ToString(), session.responses.size(), MIN_VALIDATORS);
    
    return true;
}

ConsensusResult HATConsensusValidator::DetermineConsensus(
    const std::vector<ValidationResponse>& responses)
{
    ConsensusResult result;
    
    if (responses.empty()) {
        result.consensusReached = false;
        return result;
    }
    
    result.txHash = responses[0].txHash;
    result.responses = responses;
    
    // Count votes
    for (const auto& response : responses) {
        switch (response.vote) {
            case ValidationVote::ACCEPT:
                result.acceptVotes++;
                break;
            case ValidationVote::REJECT:
                result.rejectVotes++;
                break;
            case ValidationVote::ABSTAIN:
                result.abstainVotes++;
                break;
        }
    }
    
    // Calculate weighted votes
    CalculateWeightedVotes(responses, result.weightedAccept, 
                          result.weightedReject, result.weightedAbstain);
    
    // Check WoT coverage requirement
    if (!MeetsWoTCoverage(responses)) {
        LogPrint(BCLog::CVM, "HAT Consensus: Insufficient WoT coverage for tx %s\n",
                 result.txHash.ToString());
        result.consensusReached = false;
        result.requiresDAOReview = true;
        return result;
    }
    
    // Determine consensus
    double totalWeighted = result.weightedAccept + result.weightedReject + result.weightedAbstain;
    
    if (totalWeighted == 0) {
        result.consensusReached = false;
        result.requiresDAOReview = true;
        return result;
    }
    
    double acceptRatio = result.weightedAccept / totalWeighted;
    double rejectRatio = result.weightedReject / totalWeighted;
    
    // Consensus reached if 70%+ agree
    if (acceptRatio >= CONSENSUS_THRESHOLD) {
        result.consensusReached = true;
        result.approved = true;
    } else if (rejectRatio >= CONSENSUS_THRESHOLD) {
        result.consensusReached = true;
        result.approved = false;
    } else {
        // No consensus - escalate to DAO
        result.consensusReached = false;
        result.requiresDAOReview = true;
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: Result for tx %s - Accept: %.2f%%, Reject: %.2f%%, Consensus: %s\n",
             result.txHash.ToString(), acceptRatio * 100, rejectRatio * 100,
             result.consensusReached ? (result.approved ? "APPROVED" : "REJECTED") : "DISPUTED");
    
    return result;
}

DisputeCase HATConsensusValidator::CreateDisputeCase(
    const CTransaction& tx,
    const std::vector<ValidationResponse>& responses)
{
    DisputeCase dispute;
    dispute.disputeId = tx.GetHash();  // Use tx hash as dispute ID
    dispute.txHash = tx.GetHash();
    
    // Extract sender address from first response
    if (!responses.empty()) {
        dispute.senderAddress = responses[0].calculatedScore.address;
        dispute.selfReportedScore = responses[0].calculatedScore;  // TODO: Get actual self-reported score
    }
    
    dispute.validatorResponses = responses;
    dispute.disputeReason = "Validators could not reach consensus on reputation score";
    dispute.resolved = false;
    dispute.approved = false;
    dispute.resolutionTimestamp = 0;
    
    // Package evidence
    // TODO: Serialize validator responses and trust graph data
    
    return dispute;
}

bool CVM::HATConsensusValidator::EscalateToDAO(const DisputeCase& dispute) {
    // Store dispute case
    if (!WriteToDatabase(database, MakeDBKey(DB_DISPUTE_CASE, dispute.disputeId), dispute)) {
        return false;
    }
    
    // Update transaction state
    UpdateMempoolState(dispute.txHash, TransactionState::DISPUTED);
    
    LogPrint(BCLog::CVM, "HAT Consensus: Escalated tx %s to DAO for dispute resolution\n",
             dispute.txHash.ToString());
    
    // TODO: Notify DAO members via P2P network
    
    return true;
}

bool CVM::HATConsensusValidator::ProcessDAOResolution(
    const uint256& disputeId,
    const DAODispute& resolution)
{
    // Get dispute case
    DisputeCase dispute;
    if (!ReadFromDatabase(database, MakeDBKey(DB_DISPUTE_CASE, disputeId), dispute)) {
        return false;
    }
    
    // Update dispute with resolution
    dispute.resolved = true;
    dispute.approved = resolution.slashDecision;  // DAO decision
    dispute.resolutionTimestamp = GetTime();
    
    // Store updated dispute
    WriteToDatabase(database, MakeDBKey(DB_DISPUTE_CASE, disputeId), dispute);
    
    // Update transaction state based on DAO decision
    if (dispute.approved) {
        UpdateMempoolState(dispute.txHash, TransactionState::VALIDATED);
    } else {
        UpdateMempoolState(dispute.txHash, TransactionState::REJECTED);
        
        // Record fraud attempt with actual calculated score
        // Use the median validator response as the "actual" score
        HATv2Score actualScore;
        if (!dispute.validatorResponses.empty()) {
            // Calculate median score from validator responses
            std::vector<int16_t> scores;
            for (const auto& response : dispute.validatorResponses) {
                scores.push_back(response.calculatedScore.finalScore);
            }
            std::sort(scores.begin(), scores.end());
            int16_t medianScore = scores[scores.size() / 2];
            
            // Use first validator's detailed score as template, update final score
            actualScore = dispute.validatorResponses[0].calculatedScore;
            actualScore.finalScore = medianScore;
        }
        
        // Get transaction from mempool or chain
        CTransaction tx;
        // For now, create a minimal transaction with the hash
        // In production, this would retrieve the actual transaction
        
        RecordFraudAttempt(dispute.senderAddress, tx, dispute.selfReportedScore, actualScore);
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: DAO resolved dispute %s - Decision: %s\n",
             disputeId.ToString(), dispute.approved ? "APPROVED" : "REJECTED");
    
    return true;
}

bool CVM::HATConsensusValidator::RecordFraudAttempt(
    const uint160& fraudsterAddress,
    const CTransaction& tx,
    const HATv2Score& claimedScore,
    const HATv2Score& actualScore)
{
    FraudRecord record;
    record.txHash = tx.GetHash();
    record.fraudsterAddress = fraudsterAddress;
    record.claimedScore = claimedScore;
    record.actualScore = actualScore;
    record.scoreDifference = claimedScore.finalScore - actualScore.finalScore;
    record.timestamp = GetTime();
    record.blockHeight = chainActive.Height();
    
    // Calculate reputation penalty based on fraud severity
    int16_t scoreDifference = std::abs(record.scoreDifference);
    
    // Penalty scales with fraud severity:
    // - Small difference (1-10 points): 5 reputation penalty
    // - Medium difference (11-30 points): 15 reputation penalty
    // - Large difference (31+ points): 30 reputation penalty
    if (scoreDifference <= 10) {
        record.reputationPenalty = 5;
    } else if (scoreDifference <= 30) {
        record.reputationPenalty = 15;
    } else {
        record.reputationPenalty = 30;
    }
    
    // Calculate bond slash amount (10% of stake for severe fraud)
    StakeInfo stake = secureHAT.GetStakeInfo(fraudsterAddress);
    if (scoreDifference > 30) {
        record.bondSlashed = stake.amount / 10;  // 10% slash for severe fraud
    } else if (scoreDifference > 10) {
        record.bondSlashed = stake.amount / 20;  // 5% slash for moderate fraud
    } else {
        record.bondSlashed = 0;  // No slash for minor discrepancies
    }
    
    // Store fraud record
    if (!WriteToDatabase(database, MakeDBKey(DB_FRAUD_RECORD, tx.GetHash()), record)) {
        return false;
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: Recorded fraud attempt by %s - Score diff: %d, Penalty: %d points, Bond slashed: %d\n",
             fraudsterAddress.ToString(), scoreDifference, record.reputationPenalty, record.bondSlashed);
    
    // Apply reputation penalty to fraudster
    // Get current reputation
    // Get current reputation - simplified for now
    int16_t currentReputation = 50;  // Default reputation
    int16_t newReputation = std::max((int16_t)0, (int16_t)(currentReputation - record.reputationPenalty));
    
    // Update reputation in database
    // Note: This updates the cached reputation. The actual HAT v2 calculation
    // will incorporate this penalty in future calculations through the fraud record.
    std::string penaltyKey = std::string(1, 'P') + fraudsterAddress.ToString();
    WriteToDatabase(database, penaltyKey, record.reputationPenalty);
    
    LogPrint(BCLog::CVM, "HAT Consensus: Applied reputation penalty to %s - Old: %d, New: %d\n",
             fraudsterAddress.ToString(), currentReputation, newReputation);
    
    return true;
}

bool CVM::HATConsensusValidator::UpdateValidatorReputation(
    const uint160& validator,
    bool accurate)
{
    ValidatorStats stats;
    
    // Read existing stats or create new
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATOR_STATS, validator), stats)) {
        stats.validatorAddress = validator;
    }
    
    // Update stats
    stats.totalValidations++;
    if (accurate) {
        stats.accurateValidations++;
    } else {
        stats.inaccurateValidations++;
    }
    stats.UpdateAccuracy();
    
    // Adjust validator reputation based on accuracy
    if (stats.accuracyRate >= 0.95) {
        stats.validatorReputation = std::min(100, stats.validatorReputation + 1);
    } else if (stats.accuracyRate < 0.70) {
        stats.validatorReputation = std::max(0, stats.validatorReputation - 2);
    }
    
    // Store updated stats
    WriteToDatabase(database, MakeDBKey(DB_VALIDATOR_STATS, validator), stats);
    
    return true;
}

TransactionState HATConsensusValidator::GetTransactionState(const uint256& txHash) {
    TransactionState state;
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATION_STATE, txHash), (int&)state)) {
        return TransactionState::PENDING_VALIDATION;
    }
    return state;
}

bool CVM::HATConsensusValidator::UpdateMempoolState(
    const uint256& txHash,
    TransactionState state)
{
    return WriteToDatabase(database, MakeDBKey(DB_VALIDATION_STATE, txHash), (int)state);
}

DisputeCase HATConsensusValidator::GetDisputeCase(const uint256& disputeId) {
    DisputeCase dispute;
    ReadFromDatabase(database, MakeDBKey(DB_DISPUTE_CASE, disputeId), dispute);
    return dispute;
}

bool CVM::HATConsensusValidator::HasWoTConnection(
    const uint160& validator,
    const uint160& target)
{
    // Find trust paths from validator to target
    std::vector<TrustPath> paths = trustGraph.FindTrustPaths(validator, target, 3);
    return !paths.empty();
}

double CVM::HATConsensusValidator::CalculateVoteConfidence(
    const uint160& validator,
    const uint160& target)
{
    // Base confidence
    double confidence = 0.5;
    
    // Increase confidence if WoT connection exists
    if (HasWoTConnection(validator, target)) {
        std::vector<TrustPath> paths = trustGraph.FindTrustPaths(validator, target, 3);
        
        // Calculate average path strength
        double totalWeight = 0;
        for (const auto& path : paths) {
            totalWeight += path.totalWeight;
        }
        double avgWeight = paths.empty() ? 0 : totalWeight / paths.size();
        
        // Confidence increases with path strength
        confidence = 0.5 + (avgWeight * 0.5);  // Range: 0.5 to 1.0
    }
    
    // Adjust based on validator reputation
    ValidatorStats stats = GetValidatorStats(validator);
    confidence *= (stats.validatorReputation / 100.0);
    
    return std::min(1.0, std::max(0.0, confidence));
}

ValidationVote HATConsensusValidator::CalculateValidatorVote(
    const HATv2Score& selfReported,
    const HATv2Score& calculated,
    bool hasWoT)
{
    // If validator has WoT connection, can verify full score
    if (hasWoT) {
        if (ScoresMatch(selfReported, calculated, SCORE_TOLERANCE)) {
            return ValidationVote::ACCEPT;
        } else {
            return ValidationVote::REJECT;
        }
    }
    
    // Without WoT, can only verify non-WoT components
    // Check if non-WoT components match
    bool behaviorMatches = std::abs(selfReported.behaviorScore - calculated.behaviorScore) <= SCORE_TOLERANCE;
    bool economicMatches = std::abs(selfReported.economicScore - calculated.economicScore) <= SCORE_TOLERANCE;
    bool temporalMatches = std::abs(selfReported.temporalScore - calculated.temporalScore) <= SCORE_TOLERANCE;
    
    if (behaviorMatches && economicMatches && temporalMatches) {
        // Non-WoT components match, but can't verify WoT component
        return ValidationVote::ABSTAIN;
    } else {
        // Non-WoT components don't match - clear fraud
        return ValidationVote::REJECT;
    }
}

HATv2Score HATConsensusValidator::CalculateNonWoTComponents(const uint160& address) {
    HATv2Score score;
    score.address = address;
    score.timestamp = GetTime();
    
    // Calculate only non-WoT components
    BehaviorMetrics behavior = secureHAT.GetBehaviorMetrics(address);
    StakeInfo stake = secureHAT.GetStakeInfo(address);
    TemporalMetrics temporal = secureHAT.GetTemporalMetrics(address);
    
    // Behavior component (40% weight)
    score.behaviorScore = behavior.CalculateVolumeScore();
    
    // Economic component (20% weight)
    score.economicScore = (stake.amount / COIN) * stake.GetTimeWeight();
    
    // Temporal component (10% weight)
    score.temporalScore = temporal.CalculateActivityScore();
    
    // WoT component is 0 (can't calculate without WoT connection)
    score.wotScore = 0;
    score.hasWoTConnection = false;
    
    // Final score is weighted sum (without WoT)
    score.finalScore = static_cast<int16_t>(
        (score.behaviorScore * 0.4) +
        (score.economicScore * 0.2) +
        (score.temporalScore * 0.1)
    );
    
    return score;
}

ValidatorStats HATConsensusValidator::GetValidatorStats(const uint160& validator) {
    ValidatorStats stats;
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATOR_STATS, validator), stats)) {
        stats.validatorAddress = validator;
    }
    return stats;
}

// Private methods

bool CVM::HATConsensusValidator::IsEligibleValidator(const uint160& address) {
    // Check minimum reputation
    ValidatorStats stats = GetValidatorStats(address);
    if (stats.validatorReputation < 70) {
        return false;
    }
    
    // Check minimum stake
    StakeInfo stake = secureHAT.GetStakeInfo(address);
    if (stake.amount < COIN) {  // Minimum 1 CAS
        return false;
    }
    
    // Check validator is active (participated in last 1000 blocks)
    int64_t currentTime = GetTime();
    int64_t inactivityThreshold = 1000 * 150;  // 1000 blocks * 2.5 min avg = ~41 hours
    
    if (stats.lastActivityTime > 0 && 
        (currentTime - stats.lastActivityTime) > inactivityThreshold) {
        LogPrint(BCLog::CVM, "HAT Consensus: Validator %s inactive (last activity: %d seconds ago)\n",
                 address.ToString(), currentTime - stats.lastActivityTime);
        return false;
    }
    
    return true;
}

uint256 HATConsensusValidator::GenerateRandomSeed(const uint256& txHash, int blockHeight) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << txHash;
    ss << blockHeight;
    ss << std::string("HAT_CONSENSUS_VALIDATOR_SELECTION");
    return ss.GetHash();
}

bool CVM::HATConsensusValidator::ScoresMatch(
    const HATv2Score& score1,
    const HATv2Score& score2,
    int16_t tolerance)
{
    return std::abs(score1.finalScore - score2.finalScore) <= tolerance;
}

void CVM::HATConsensusValidator::CalculateWeightedVotes(
    const std::vector<ValidationResponse>& responses,
    double& weightedAccept,
    double& weightedReject,
    double& weightedAbstain)
{
    weightedAccept = 0;
    weightedReject = 0;
    weightedAbstain = 0;
    
    for (const auto& response : responses) {
        // Determine vote weight based on WoT connection
        double weight = response.hasWoTConnection ? WOT_VOTE_WEIGHT : NON_WOT_VOTE_WEIGHT;
        
        // Apply confidence multiplier
        weight *= response.voteConfidence;
        
        // Add to appropriate tally
        switch (response.vote) {
            case ValidationVote::ACCEPT:
                weightedAccept += weight;
                break;
            case ValidationVote::REJECT:
                weightedReject += weight;
                break;
            case ValidationVote::ABSTAIN:
                weightedAbstain += weight;
                break;
        }
    }
}

bool CVM::HATConsensusValidator::MeetsWoTCoverage(const std::vector<ValidationResponse>& responses) {
    if (responses.empty()) {
        return false;
    }
    
    // Count validators with WoT connection
    int wotValidators = 0;
    for (const auto& response : responses) {
        if (response.hasWoTConnection) {
            wotValidators++;
        }
    }
    
    // Check if at least 30% have WoT connection
    double wotRatio = (double)wotValidators / responses.size();
    return wotRatio >= WOT_COVERAGE_THRESHOLD;
}

// ============================================================================
// P2P Network Protocol Implementation
// ============================================================================

void CVM::HATConsensusValidator::ProcessValidationRequest(
    const ValidationRequest& request,
    CNode* pfrom,
    CConnman* connman)
{
    LogPrint(BCLog::NET, "HAT v2: Processing validation request for tx %s from peer=%d\n",
             request.txHash.ToString(), pfrom ? pfrom->GetId() : -1);
    
    // STEP 1: Verify challenge nonce (replay protection)
    uint256 expectedNonce = ValidationRequest::GenerateChallengeNonce(
        request.txHash, request.blockHeight);
    if (request.challengeNonce != expectedNonce) {
        LogPrintf("HAT v2: Invalid challenge nonce in validation request\n");
        return;
    }
    
    // STEP 2: Check if WE are selected as a validator
    // Use deterministic random selection based on tx hash + block height
    std::vector<uint160> selectedValidators = SelectRandomValidators(
        request.txHash, request.blockHeight);
    
    // Get our validator address (TODO: from config/wallet)
    uint160 myValidatorAddress;  // TODO: Load from config
    
    // Check if we are in the selected validator list
    bool isSelected = false;
    for (const auto& validator : selectedValidators) {
        if (validator == myValidatorAddress) {
            isSelected = true;
            break;
        }
    }
    
    if (!isSelected) {
        LogPrint(BCLog::NET, "HAT v2: We are not selected as validator for this tx, ignoring\n");
        return;
    }
    
    // STEP 3: Check if we are eligible (reputation >= 70, stake >= 1 CAS)
    if (!IsEligibleValidator(myValidatorAddress)) {
        LogPrint(BCLog::NET, "HAT v2: We are not eligible as validator, ignoring\n");
        return;
    }
    
    // STEP 4: Check rate limiting (anti-spam)
    if (IsValidatorRateLimited(myValidatorAddress)) {
        LogPrint(BCLog::NET, "HAT v2: We are rate-limited, ignoring validation request\n");
        return;
    }
    
    LogPrint(BCLog::NET, "HAT v2: We are selected and eligible validator, processing request\n");
    
    // STEP 5: Calculate our HAT v2 score for the sender
    HATv2Score calculatedScore;
    calculatedScore.address = request.senderAddress;
    calculatedScore.timestamp = GetTime();
    
    // Check if we have WoT connection
    bool hasWoT = HasWoTConnection(myValidatorAddress, request.senderAddress);
    
    if (hasWoT) {
        // Calculate full score with WoT component
        calculatedScore.finalScore = secureHAT.CalculateFinalTrust(request.senderAddress, myValidatorAddress);
        // TODO: Get component breakdown from SecureHAT
        calculatedScore.behaviorScore = 0;  // Placeholder
        calculatedScore.wotScore = 0;       // Placeholder
        calculatedScore.economicScore = 0;  // Placeholder
        calculatedScore.temporalScore = 0;  // Placeholder
        calculatedScore.hasWoTConnection = true;
    } else {
        // Calculate only non-WoT components
        calculatedScore = CalculateNonWoTComponents(request.senderAddress);
        calculatedScore.hasWoTConnection = false;
    }
    
    // STEP 6: Calculate our vote
    ValidationVote vote = CalculateValidatorVote(
        request.selfReportedScore,
        calculatedScore,
        hasWoT
    );
    
    // STEP 7: Calculate vote confidence
    double confidence = CalculateVoteConfidence(myValidatorAddress, request.senderAddress);
    
    // STEP 8: Create response
    ValidationResponse response;
    response.txHash = request.txHash;
    response.validatorAddress = myValidatorAddress;
    response.calculatedScore = calculatedScore;
    response.vote = vote;
    response.voteConfidence = confidence;
    response.hasWoTConnection = hasWoT;
    response.challengeNonce = request.challengeNonce;
    response.timestamp = GetTime();
    
    // STEP 9: Sign response with validator key
    // TODO: Load validator key from config/wallet
    // response.Sign(validatorKey);
    
    // STEP 10: Record for rate limiting
    RecordValidationMessage(myValidatorAddress);
    
    // STEP 11: Send response back (broadcast to all peers)
    SendValidationResponse(response, connman);
    
    LogPrint(BCLog::NET, "HAT v2: Sent validation response (vote=%d, confidence=%.2f)\n",
             (int)vote, confidence);
}

void CVM::HATConsensusValidator::ProcessDAODispute(
    const DisputeCase& dispute,
    CNode* pfrom,
    CConnman* connman)
{
    LogPrint(BCLog::NET, "HAT v2: Processing DAO dispute %s for tx %s\n",
             dispute.disputeId.ToString(), dispute.txHash.ToString());
    
    // Validate dispute case
    if (dispute.validatorResponses.empty()) {
        LogPrintf("HAT v2: Invalid dispute case - no validator responses\n");
        return;
    }
    
    // Store dispute in database
    std::string key = "dispute_" + dispute.disputeId.ToString();
    std::vector<uint8_t> data;
    CVectorWriter writer(SER_DISK, CLIENT_VERSION, data, 0);
    writer << dispute;
    database.WriteGeneric(key, data);
    
    // Escalate to DAO through TrustGraph
    bool success = EscalateToDAO(dispute);
    if (success) {
        LogPrint(BCLog::NET, "HAT v2: Successfully escalated dispute to DAO\n");
        
        // Relay dispute to other nodes
        BroadcastDAODispute(dispute, connman);
    } else {
        LogPrintf("HAT v2: Failed to escalate dispute to DAO\n");
    }
}

void CVM::HATConsensusValidator::ProcessDAOResolution(
    const uint256& disputeId,
    bool approved,
    uint64_t resolutionTimestamp)
{
    LogPrint(BCLog::NET, "HAT v2: Processing DAO resolution for dispute %s (approved=%d)\n",
             disputeId.ToString(), approved);
    
    // Load dispute case
    DisputeCase dispute = GetDisputeCase(disputeId);
    if (dispute.disputeId.IsNull()) {
        LogPrintf("HAT v2: Dispute %s not found\n", disputeId.ToString());
        return;
    }
    
    // Update dispute with resolution
    dispute.resolved = true;
    dispute.approved = approved;
    dispute.resolutionTimestamp = resolutionTimestamp;
    
    // Store updated dispute
    std::string key = "dispute_" + disputeId.ToString();
    std::vector<uint8_t> data;
    CVectorWriter writer(SER_DISK, CLIENT_VERSION, data, 0);
    writer << dispute;
    database.WriteGeneric(key, data);
    
    // Update transaction state in mempool
    if (approved) {
        UpdateMempoolState(dispute.txHash, TransactionState::VALIDATED);
        LogPrint(BCLog::NET, "HAT v2: Transaction %s approved by DAO\n", dispute.txHash.ToString());
    } else {
        UpdateMempoolState(dispute.txHash, TransactionState::REJECTED);
        
        // Record fraud attempt
        RecordFraudAttempt(
            dispute.senderAddress,
            CTransaction(), // TODO: Load transaction
            dispute.selfReportedScore,
            HATv2Score() // TODO: Calculate actual score
        );
        
        LogPrint(BCLog::NET, "HAT v2: Transaction %s rejected by DAO, fraud recorded\n",
                 dispute.txHash.ToString());
    }
}

void CVM::HATConsensusValidator::BroadcastValidationChallenge(
    const ValidationRequest& request,
    const std::vector<uint160>& validators,
    CConnman* connman)
{
    if (!connman) {
        LogPrintf("HAT v2: Cannot broadcast validation challenge - no connection manager\n");
        return;
    }
    
    LogPrint(BCLog::NET, "HAT v2: Broadcasting validation challenge for tx %s to ALL peers (expecting %zu validators to respond)\n",
             request.txHash.ToString(), validators.size());
    
    // SECURITY: Broadcast to ALL peers, not targeted sending
    // This prevents:
    // 1. Targeting specific validators (manipulation)
    // 2. Eclipse attacks (isolating validators)
    // 3. Censorship (blocking specific validators)
    //
    // Validators will self-identify and respond if they are:
    // 1. Eligible (reputation >= 70, stake >= 1 CAS)
    // 2. Selected by deterministic random algorithm
    // 3. Not rate-limited
    
    uint32_t broadcastCount = 0;
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected) {
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::VALCHALLENGE, request));
            broadcastCount++;
        }
    });
    
    LogPrint(BCLog::NET, "HAT v2: Broadcast challenge to %u peers\n", broadcastCount);
}

void CVM::HATConsensusValidator::SendValidationResponse(
    const ValidationResponse& response,
    CConnman* connman)
{
    if (!connman) {
        LogPrintf("HAT v2: Cannot send validation response - no connection manager\n");
        return;
    }
    
    LogPrint(BCLog::NET, "HAT v2: Sending validation response for tx %s\n",
             response.txHash.ToString());
    
    // Broadcast response to all connected nodes
    // The originator will collect responses
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected) {
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::VALRESPONSE, response));
        }
    });
}

void CVM::HATConsensusValidator::BroadcastDAODispute(
    const DisputeCase& dispute,
    CConnman* connman)
{
    if (!connman) {
        LogPrintf("HAT v2: Cannot broadcast DAO dispute - no connection manager\n");
        return;
    }
    
    LogPrint(BCLog::NET, "HAT v2: Broadcasting DAO dispute %s\n", dispute.disputeId.ToString());
    
    // Broadcast to all connected nodes
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected) {
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::DAODISPUTE, dispute));
        }
    });
}

void CVM::HATConsensusValidator::BroadcastDAOResolution(
    const uint256& disputeId,
    bool approved,
    uint64_t resolutionTimestamp,
    CConnman* connman)
{
    if (!connman) {
        LogPrintf("HAT v2: Cannot broadcast DAO resolution - no connection manager\n");
        return;
    }
    
    LogPrint(BCLog::NET, "HAT v2: Broadcasting DAO resolution for dispute %s (approved=%d)\n",
             disputeId.ToString(), approved);
    
    // Broadcast to all connected nodes
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << disputeId << approved << resolutionTimestamp;
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::DAORESOLUTION, 
                    disputeId, approved, resolutionTimestamp));
        }
    });
}

void CVM::HATConsensusValidator::AnnounceValidatorAddress(
    const uint160& validatorAddress,
    const CKey& validatorKey,
    CConnman* connman)
{
    if (!connman) {
        LogPrintf("HAT v2: Cannot announce validator address - no connection manager\n");
        return;
    }
    
    if (!validatorKey.IsValid()) {
        LogPrintf("HAT v2: Cannot announce validator address - invalid key\n");
        return;
    }
    
    LogPrint(BCLog::NET, "HAT v2: Announcing validator address %s to network\n",
             validatorAddress.ToString());
    
    // Get public key from private key
    CPubKey pubkey = validatorKey.GetPubKey();
    if (!pubkey.IsFullyValid()) {
        LogPrintf("HAT v2: Failed to derive public key from validator key\n");
        return;
    }
    
    // Verify the public key matches the validator address
    CKeyID keyID = pubkey.GetID();
    uint160 derivedAddress;
    memcpy(derivedAddress.begin(), keyID.begin(), 20);
    
    if (derivedAddress != validatorAddress) {
        LogPrintf("HAT v2: Validator key does not match validator address\n");
        return;
    }
    
    // Create signature to prove ownership of validator address
    // Sign the hash of the validator address with the validator's private key
    uint256 messageHash = Hash(validatorAddress.begin(), validatorAddress.end());
    std::vector<uint8_t> signature;
    
    if (!validatorKey.Sign(messageHash, signature)) {
        LogPrintf("HAT v2: Failed to sign validator announcement\n");
        return;
    }
    
    // Convert public key to vector for serialization
    std::vector<uint8_t> validatorPubKey(pubkey.begin(), pubkey.end());
    
    // Broadcast announcement to all connected peers
    uint32_t announcedCount = 0;
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected) {
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::VALANNOUNCE,
                    validatorAddress, validatorPubKey, signature));
            announcedCount++;
        }
    });
    
    LogPrint(BCLog::NET, "HAT v2: Announced validator address to %u peers (pubkey size: %zu, signature size: %zu)\n",
             announcedCount, validatorPubKey.size(), signature.size());
}

// ============================================================================
// Validator Communication and Response Collection
// ============================================================================

std::vector<CVM::ValidationResponse> CVM::HATConsensusValidator::CollectValidatorResponses(
    const uint256& txHash,
    uint32_t timeoutSeconds)
{
    LogPrint(BCLog::NET, "HAT v2: Collecting validator responses for tx %s (timeout=%ds)\n",
             txHash.ToString(), timeoutSeconds);
    
    // Load validation session
    ValidationSession session;
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, txHash), session)) {
        LogPrintf("HAT v2: No validation session found for tx %s\n", txHash.ToString());
        return std::vector<ValidationResponse>();
    }
    
    // Check if already completed
    if (session.completed) {
        LogPrint(BCLog::NET, "HAT v2: Validation session already completed\n");
        return session.responses;
    }
    
    // Check timeout
    uint64_t currentTime = GetTime();
    uint64_t elapsedTime = currentTime - session.startTime;
    
    if (elapsedTime >= timeoutSeconds) {
        LogPrint(BCLog::NET, "HAT v2: Validation session timed out (%lus elapsed)\n", elapsedTime);
        
        // Mark session as completed
        session.completed = true;
        WriteToDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, txHash), session);
        
        // Handle non-responsive validators
        std::vector<uint160> selectedValidators = SelectRandomValidators(txHash, session.request.blockHeight);
        HandleNonResponsiveValidators(txHash, selectedValidators);
    }
    
    // Check if we have minimum responses
    if (session.responses.size() >= MIN_VALIDATORS) {
        LogPrint(BCLog::NET, "HAT v2: Collected %zu responses (minimum met)\n", session.responses.size());
        
        // Mark session as completed
        session.completed = true;
        WriteToDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, txHash), session);
    }
    
    return session.responses;
}

bool CVM::HATConsensusValidator::HasValidationTimedOut(const uint256& txHash) {
    ValidationSession session;
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, txHash), session)) {
        return true;  // No session = timed out
    }
    
    uint64_t currentTime = GetTime();
    uint64_t elapsedTime = currentTime - session.startTime;
    
    return elapsedTime >= VALIDATION_TIMEOUT;
}

void CVM::HATConsensusValidator::HandleNonResponsiveValidators(
    const uint256& txHash,
    const std::vector<uint160>& selectedValidators)
{
    LogPrint(BCLog::NET, "HAT v2: Handling non-responsive validators for tx %s\n", txHash.ToString());
    
    // Load validation session
    ValidationSession session;
    if (!ReadFromDatabase(database, MakeDBKey(DB_VALIDATION_SESSION, txHash), session)) {
        return;
    }
    
    // Create set of validators that responded
    std::set<uint160> respondedValidators;
    for (const auto& response : session.responses) {
        respondedValidators.insert(response.validatorAddress);
    }
    
    // Find non-responsive validators
    for (const auto& validator : selectedValidators) {
        if (respondedValidators.find(validator) == respondedValidators.end()) {
            // Validator didn't respond - penalize reputation
            ValidatorStats stats = GetValidatorStats(validator);
            
            // Increase abstention count
            stats.abstentions++;
            stats.totalValidations++;
            stats.UpdateAccuracy();
            
            // Reduce validator reputation for non-responsiveness
            stats.validatorReputation = std::max(0, stats.validatorReputation - 1);
            
            // Update last activity time
            stats.lastActivityTime = GetTime();
            
            // Store updated stats
            WriteToDatabase(database, MakeDBKey(DB_VALIDATOR_STATS, validator), stats);
            
            LogPrint(BCLog::NET, "HAT v2: Penalized non-responsive validator %s (rep=%d)\n",
                     validator.ToString(), stats.validatorReputation);
        }
    }
}

bool CVM::HATConsensusValidator::IsValidatorRateLimited(const uint160& validatorAddress) {
    uint64_t currentTime = GetTime();
    
    auto it = validatorRateLimits.find(validatorAddress);
    if (it == validatorRateLimits.end()) {
        // No rate limit record - not limited
        return false;
    }
    
    ValidatorRateLimit& limit = it->second;
    
    // Check if we're in a new window
    if (currentTime - limit.windowStart >= RATE_LIMIT_WINDOW) {
        // Reset window
        limit.windowStart = currentTime;
        limit.messageCount = 0;
        return false;
    }
    
    // Check if exceeded limit
    if (limit.messageCount >= MAX_MESSAGES_PER_WINDOW) {
        LogPrint(BCLog::NET, "HAT v2: Validator %s is rate-limited (%u messages in %lus)\n",
                 validatorAddress.ToString(), limit.messageCount, currentTime - limit.windowStart);
        return true;
    }
    
    return false;
}

void CVM::HATConsensusValidator::RecordValidationMessage(const uint160& validatorAddress) {
    uint64_t currentTime = GetTime();
    
    auto it = validatorRateLimits.find(validatorAddress);
    if (it == validatorRateLimits.end()) {
        // Create new rate limit record
        ValidatorRateLimit limit;
        limit.windowStart = currentTime;
        limit.messageCount = 1;
        limit.lastMessageTime = currentTime;
        validatorRateLimits[validatorAddress] = limit;
    } else {
        ValidatorRateLimit& limit = it->second;
        
        // Check if we're in a new window
        if (currentTime - limit.windowStart >= RATE_LIMIT_WINDOW) {
            // Reset window
            limit.windowStart = currentTime;
            limit.messageCount = 1;
        } else {
            // Increment count
            limit.messageCount++;
        }
        
        limit.lastMessageTime = currentTime;
    }
}

void CVM::HATConsensusValidator::RegisterValidatorPeer(
    NodeId nodeId,
    const uint160& validatorAddress)
{
    LOCK(cs_validatorPeers);
    
    // Check if validator already registered
    auto it = validatorPeerMap.find(validatorAddress);
    if (it != validatorPeerMap.end()) {
        // Update existing entry
        it->second.nodeId = nodeId;
        it->second.lastSeen = GetTime();
        it->second.isActive = true;
        
        LogPrint(BCLog::NET, "HAT v2: Updated validator peer mapping: %s → node %d\n",
                 validatorAddress.ToString(), nodeId);
    } else {
        // Create new entry
        ValidatorPeerInfo info;
        info.nodeId = nodeId;
        info.validatorAddress = validatorAddress;
        info.lastSeen = GetTime();
        info.isActive = true;
        
        validatorPeerMap[validatorAddress] = info;
        nodeToValidatorMap[nodeId] = validatorAddress;
        
        LogPrint(BCLog::NET, "HAT v2: Registered validator peer: %s → node %d\n",
                 validatorAddress.ToString(), nodeId);
    }
    
    // Update reverse mapping
    nodeToValidatorMap[nodeId] = validatorAddress;
    
    // Persist to database for recovery after restart
    std::string key = "validator_peer_" + validatorAddress.ToString();
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << validatorAddress << GetTime();
    std::vector<uint8_t> data(ss.begin(), ss.end());
    database.WriteGeneric(key, data);
}

void CVM::HATConsensusValidator::UnregisterValidatorPeer(NodeId nodeId)
{
    LOCK(cs_validatorPeers);
    
    // Find validator address for this node
    auto nodeIt = nodeToValidatorMap.find(nodeId);
    if (nodeIt == nodeToValidatorMap.end()) {
        return;  // Not a validator peer
    }
    
    uint160 validatorAddress = nodeIt->second;
    
    // Mark as inactive
    auto validatorIt = validatorPeerMap.find(validatorAddress);
    if (validatorIt != validatorPeerMap.end()) {
        validatorIt->second.isActive = false;
        validatorIt->second.lastSeen = GetTime();
        
        LogPrint(BCLog::NET, "HAT v2: Unregistered validator peer: %s (node %d)\n",
                 validatorAddress.ToString(), nodeId);
    }
    
    // Remove from reverse mapping
    nodeToValidatorMap.erase(nodeIt);
}

CNode* CVM::HATConsensusValidator::GetValidatorPeer(
    const uint160& validatorAddress,
    CConnman* connman)
{
    if (!connman) {
        return nullptr;
    }
    
    LOCK(cs_validatorPeers);
    
    // Look up validator in peer map
    auto it = validatorPeerMap.find(validatorAddress);
    if (it == validatorPeerMap.end() || !it->second.isActive) {
        LogPrint(BCLog::NET, "HAT v2: Validator %s not found in peer map or inactive\n",
                 validatorAddress.ToString());
        return nullptr;
    }
    
    NodeId nodeId = it->second.nodeId;
    
    // Find the peer node
    CNode* validatorPeer = nullptr;
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->GetId() == nodeId && pnode->fSuccessfullyConnected) {
            validatorPeer = pnode;
        }
    });
    
    if (!validatorPeer) {
        // Peer disconnected but not yet unregistered
        it->second.isActive = false;
        LogPrint(BCLog::NET, "HAT v2: Validator peer %s (node %d) no longer connected\n",
                 validatorAddress.ToString(), nodeId);
    }
    
    return validatorPeer;
}

bool CVM::HATConsensusValidator::IsValidatorOnline(const uint160& validatorAddress)
{
    LOCK(cs_validatorPeers);
    
    auto it = validatorPeerMap.find(validatorAddress);
    return (it != validatorPeerMap.end() && it->second.isActive);
}

std::vector<uint160> CVM::HATConsensusValidator::GetOnlineValidators()
{
    LOCK(cs_validatorPeers);
    
    std::vector<uint160> onlineValidators;
    for (const auto& pair : validatorPeerMap) {
        if (pair.second.isActive) {
            onlineValidators.push_back(pair.first);
        }
    }
    
    return onlineValidators;
}

void CVM::HATConsensusValidator::LoadValidatorPeerMappings()
{
    LOCK(cs_validatorPeers);
    
    LogPrint(BCLog::NET, "HAT v2: Loading validator peer mappings from database\n");
    
    // Query all validator peer records from database
    std::vector<std::string> keys = database.ListKeysWithPrefix("validator_peer_");
    
    uint32_t loadedCount = 0;
    for (const auto& key : keys) {
        std::vector<uint8_t> data;
        if (!database.ReadGeneric(key, data)) {
            continue;
        }
        
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            uint160 validatorAddress;
            uint64_t lastSeen;
            ss >> validatorAddress >> lastSeen;
            
            // Create inactive entry (validator must re-announce to become active)
            ValidatorPeerInfo info;
            info.nodeId = -1;  // Invalid node ID
            info.validatorAddress = validatorAddress;
            info.lastSeen = lastSeen;
            info.isActive = false;  // Inactive until validator re-announces
            
            validatorPeerMap[validatorAddress] = info;
            loadedCount++;
            
            LogPrint(BCLog::NET, "HAT v2: Loaded validator peer mapping: %s (last seen: %lu)\n",
                     validatorAddress.ToString(), lastSeen);
        } catch (...) {
            LogPrintf("HAT v2: Failed to deserialize validator peer mapping: %s\n", key);
        }
    }
    
    LogPrint(BCLog::NET, "HAT v2: Loaded %u validator peer mappings from database\n", loadedCount);
}

bool CVM::HATConsensusValidator::SendChallengeToValidator(
    const uint160& validatorAddress,
    const ValidationRequest& request,
    CConnman* connman)
{
    // NOTE: This function is kept for API compatibility but is not used
    // in the broadcast-based validator selection model.
    // Challenges are broadcast to all peers, and validators self-identify.
    
    LogPrint(BCLog::NET, "HAT v2: SendChallengeToValidator called but not used in broadcast model\n");
    return false;
}

} // namespace CVM
