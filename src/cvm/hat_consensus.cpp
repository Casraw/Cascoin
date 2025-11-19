// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/hat_consensus.h>
#include <cvm/cvmdb.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <cvm/validator_keys.h>
#include <cvm/walletcluster.h>
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
    : database(db), secureHAT(hat), trustGraph(graph),
      m_eclipseSybilProtection(new EclipseSybilProtection(db, graph))
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
    
    // Eclipse/Sybil Protection: Validate validator set diversity
    if (!ValidateValidatorSetDiversity(validators, blockHeight)) {
        LogPrint(BCLog::CVM, "HAT Consensus: Validator set failed diversity check, reselecting...\n");
        
        // Try to find a more diverse set by continuing selection
        for (size_t i = selectCount; i < candidatePool.size() && validators.size() < MIN_VALIDATORS * 2; i++) {
            validators.push_back(candidatePool[i]);
            
            // Check if diversity improved
            if (ValidateValidatorSetDiversity(validators, blockHeight)) {
                LogPrint(BCLog::CVM, "HAT Consensus: Found diverse validator set with %zu validators\n",
                        validators.size());
                break;
            }
        }
    }
    
    // Eclipse/Sybil Protection: Detect Sybil network
    SybilDetectionResult sybilResult = DetectValidatorSybilNetwork(validators, blockHeight);
    if (sybilResult.isSybilNetwork) {
        LogPrint(BCLog::CVM, "HAT Consensus: WARNING - Sybil network detected among validators (confidence: %.0f%%): %s\n",
                sybilResult.confidence * 100, sybilResult.reason);
        
        // Remove suspicious validators
        std::vector<uint160> cleanValidators;
        for (const auto& validator : validators) {
            bool isSuspicious = false;
            for (const auto& suspicious : sybilResult.suspiciousValidators) {
                if (validator == suspicious) {
                    isSuspicious = true;
                    break;
                }
            }
            if (!isSuspicious) {
                cleanValidators.push_back(validator);
            }
        }
        
        // If we removed too many, we need to add more from the pool
        if (cleanValidators.size() < MIN_VALIDATORS) {
            LogPrint(BCLog::CVM, "HAT Consensus: Not enough clean validators (%zu), adding more from pool\n",
                    cleanValidators.size());
            
            for (size_t i = selectCount; i < candidatePool.size() && cleanValidators.size() < MIN_VALIDATORS; i++) {
                bool isAlreadySelected = false;
                for (const auto& selected : validators) {
                    if (candidatePool[i] == selected) {
                        isAlreadySelected = true;
                        break;
                    }
                }
                if (!isAlreadySelected) {
                    cleanValidators.push_back(candidatePool[i]);
                }
            }
        }
        
        validators = cleanValidators;
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: Selected %zu validators from pool of %zu (after Eclipse/Sybil protection)\n",
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
    // ANTI-MANIPULATION PROTECTION #1: Only accept fraud records from DAO consensus
    // This prevents arbitrary users from creating false fraud accusations
    // Fraud must be verified by distributed validator consensus first
    
    // ANTI-MANIPULATION PROTECTION #2: Minimum score difference threshold
    // Small differences (< 5 points) could be legitimate measurement variance
    // Only record fraud for significant discrepancies
    int16_t scoreDiff = claimedScore.finalScore - actualScore.finalScore;
    if (std::abs(scoreDiff) < 5) {
        LogPrint(BCLog::CVM, "HAT Consensus: Score difference too small (%d points) - not recording as fraud\n",
                 std::abs(scoreDiff));
        return false;  // Not fraud, just measurement variance
    }
    
    // ANTI-MANIPULATION PROTECTION #3: Check for Sybil attack patterns
    // If fraudster address is part of a wallet cluster, check if accuser is also in cluster
    // This prevents self-accusation attacks to game the system
    WalletCluster fraudsterCluster = GetWalletCluster(fraudsterAddress);
    if (fraudsterCluster.addresses.size() > 1) {
        LogPrint(BCLog::CVM, "HAT Consensus: Fraudster %s is part of wallet cluster with %d addresses\n",
                 fraudsterAddress.ToString(), fraudsterCluster.addresses.size());
        
        // Check if this looks like a coordinated Sybil attack
        // (multiple fraud accusations from same cluster)
        int recentFraudCount = CountRecentFraudRecords(fraudsterCluster.addresses, 1000);  // Last 1000 blocks
        if (recentFraudCount > 5) {
            LogPrintf("HAT Consensus: WARNING - Possible Sybil attack detected! Cluster has %d recent fraud records\n",
                     recentFraudCount);
            // Escalate to DAO for manual review
            return false;
        }
    }
    
    FraudRecord record;
    record.txHash = tx.GetHash();
    record.fraudsterAddress = fraudsterAddress;
    record.claimedScore = claimedScore;
    record.actualScore = actualScore;
    record.scoreDifference = scoreDiff;
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
    
    // Task 19.2: Integrate fraud record with BehaviorMetrics
    BehaviorMetrics metrics = secureHAT.GetBehaviorMetrics(fraudsterAddress);
    metrics.AddFraudRecord(record.txHash, record.reputationPenalty, record.timestamp);
    secureHAT.StoreBehaviorMetrics(metrics);
    
    LogPrint(BCLog::CVM, "HAT Consensus: Updated behavior metrics for %s - Fraud count: %d, Fraud score: %.2f\n",
             fraudsterAddress.ToString(), metrics.fraud_count, metrics.fraud_score);
    
    // Apply reputation penalty to fraudster
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
    // Component-based verification logic (Task 19.2.1)
    ComponentStatus status;
    
    // Define tolerances for each component
    const double BEHAVIOR_TOLERANCE = 0.03;  // ±3 points (3% of 100)
    const double ECONOMIC_TOLERANCE = 0.03;  // ±3 points
    const double TEMPORAL_TOLERANCE = 0.03;  // ±3 points
    const double WOT_TOLERANCE = 0.05;       // ±5 points (WoT is more subjective)
    
    // Verify each component
    status.behaviorDifference = std::abs(selfReported.behaviorScore - calculated.behaviorScore);
    status.behaviorVerified = (status.behaviorDifference <= BEHAVIOR_TOLERANCE);
    
    status.economicDifference = std::abs(selfReported.economicScore - calculated.economicScore);
    status.economicVerified = (status.economicDifference <= ECONOMIC_TOLERANCE);
    
    status.temporalDifference = std::abs(selfReported.temporalScore - calculated.temporalScore);
    status.temporalVerified = (status.temporalDifference <= TEMPORAL_TOLERANCE);
    
    if (hasWoT) {
        // Validator WITH WoT connection: verify all 4 components
        double wotDifference = std::abs(selfReported.wotScore - calculated.wotScore);
        status.wotVerified = (wotDifference <= WOT_TOLERANCE);
        
        // All components must match for ACCEPT
        if (status.behaviorVerified && status.economicVerified && 
            status.temporalVerified && status.wotVerified) {
            LogPrint(BCLog::CVM, "HAT Consensus: ACCEPT - All components verified (WoT validator)\n");
            return ValidationVote::ACCEPT;
        }
        
        // If any component fails, REJECT
        LogPrint(BCLog::CVM, "HAT Consensus: REJECT - Component mismatch (WoT validator): "
                 "Behavior=%s (%.3f), Economic=%s (%.3f), Temporal=%s (%.3f), WoT=%s (%.3f)\n",
                 status.behaviorVerified ? "OK" : "FAIL", status.behaviorDifference,
                 status.economicVerified ? "OK" : "FAIL", status.economicDifference,
                 status.temporalVerified ? "OK" : "FAIL", status.temporalDifference,
                 status.wotVerified ? "OK" : "FAIL", wotDifference);
        return ValidationVote::REJECT;
    } else {
        // Validator WITHOUT WoT connection: verify only non-WoT components
        // IGNORE WoT component completely
        
        // All non-WoT components must match for ACCEPT
        if (status.behaviorVerified && status.economicVerified && status.temporalVerified) {
            LogPrint(BCLog::CVM, "HAT Consensus: ACCEPT - Non-WoT components verified (non-WoT validator)\n");
            return ValidationVote::ACCEPT;
        }
        
        // If any non-WoT component fails, REJECT
        LogPrint(BCLog::CVM, "HAT Consensus: REJECT - Component mismatch (non-WoT validator): "
                 "Behavior=%s (%.3f), Economic=%s (%.3f), Temporal=%s (%.3f)\n",
                 status.behaviorVerified ? "OK" : "FAIL", status.behaviorDifference,
                 status.economicVerified ? "OK" : "FAIL", status.economicDifference,
                 status.temporalVerified ? "OK" : "FAIL", status.temporalDifference);
        return ValidationVote::REJECT;
    }
}

HATv2Score HATConsensusValidator::CalculateNonWoTComponents(const uint160& address) {
    HATv2Score score;
    score.address = address;
    score.timestamp = GetTime();
    score.hasWoTConnection = false;
    score.wotPathCount = 0;
    score.wotPathStrength = 0.0;
    
    // Get component breakdown from SecureHAT (Task 19.2.1)
    // Note: We pass address as both target and viewer since we don't have WoT connection
    TrustBreakdown breakdown = secureHAT.CalculateWithBreakdown(address, address);
    
    // Extract non-WoT components (actual HAT v2 scores, not placeholders)
    score.behaviorScore = breakdown.secure_behavior;
    score.economicScore = breakdown.secure_economic;
    score.temporalScore = breakdown.secure_temporal;
    score.wotScore = 0.0;  // No WoT component for validators without connection
    
    // Calculate final score WITHOUT WoT component
    // Adjust weights: Behavior 57%, Economic 29%, Temporal 14% (proportional to original 40/20/10)
    double finalTrust = 0.57 * score.behaviorScore +
                       0.29 * score.economicScore +
                       0.14 * score.temporalScore;
    
    score.finalScore = static_cast<int16_t>(finalTrust * 100.0);
    score.finalScore = std::max(int16_t(0), std::min(int16_t(100), score.finalScore));
    
    LogPrint(BCLog::CVM, "HAT Consensus: Calculated non-WoT score for %s: "
             "Behavior=%.2f, Economic=%.2f, Temporal=%.2f, Final=%d\n",
             address.ToString(), score.behaviorScore, score.economicScore,
             score.temporalScore, score.finalScore);
    
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
    
    // Get our validator address from key manager
    if (!g_validatorKeys || !g_validatorKeys->HasValidatorKey()) {
        LogPrint(BCLog::NET, "HAT v2: No validator key configured, ignoring request\n");
        return;
    }
    
    uint160 myValidatorAddress = g_validatorKeys->GetValidatorAddress();
    
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
        TrustBreakdown breakdown = secureHAT.CalculateWithBreakdown(request.senderAddress, myValidatorAddress);
        
        // Extract component scores from breakdown
        calculatedScore.behaviorScore = breakdown.secure_behavior;
        calculatedScore.wotScore = breakdown.secure_wot;
        calculatedScore.economicScore = breakdown.secure_economic;
        calculatedScore.temporalScore = breakdown.secure_temporal;
        calculatedScore.finalScore = breakdown.final_score;
        calculatedScore.hasWoTConnection = true;
        
        LogPrint(BCLog::CVM, "HAT Consensus: Calculated full score with WoT for %s: "
                 "Behavior=%.2f, WoT=%.2f, Economic=%.2f, Temporal=%.2f, Final=%d\n",
                 request.senderAddress.ToString(), calculatedScore.behaviorScore,
                 calculatedScore.wotScore, calculatedScore.economicScore,
                 calculatedScore.temporalScore, calculatedScore.finalScore);
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
    std::vector<uint8_t> signature;
    uint256 responseHash = response.GetHash();
    if (!g_validatorKeys->Sign(responseHash, signature)) {
        LogPrintf("HAT v2: Failed to sign validation response\n");
        return;
    }
    response.signature = signature;
    
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

// Anti-Manipulation Helper Methods

WalletCluster CVM::HATConsensusValidator::GetWalletCluster(const uint160& address) {
    // Integrate with actual wallet clustering system
    WalletClusterer clusterer(database);
    
    // Get all addresses in the same cluster
    std::set<uint160> members = clusterer.GetClusterMembers(address);
    
    // Build WalletCluster structure
    WalletCluster cluster;
    cluster.addresses = std::vector<uint160>(members.begin(), members.end());
    
    // Calculate confidence based on cluster size and evidence strength
    // Larger clusters with more transaction evidence = higher confidence
    WalletClusterInfo info = clusterer.GetClusterInfo(clusterer.GetClusterForAddress(address));
    
    // Confidence formula:
    // - Single address: 1.0 (certain)
    // - 2-5 addresses: 0.9 (high confidence)
    // - 6-10 addresses: 0.8 (good confidence)
    // - 11-20 addresses: 0.7 (moderate confidence)
    // - 21+ addresses: 0.6 (lower confidence, but still significant)
    
    if (cluster.addresses.size() == 1) {
        cluster.confidence = 1.0;
    } else if (cluster.addresses.size() <= 5) {
        cluster.confidence = 0.9;
    } else if (cluster.addresses.size() <= 10) {
        cluster.confidence = 0.8;
    } else if (cluster.addresses.size() <= 20) {
        cluster.confidence = 0.7;
    } else {
        cluster.confidence = 0.6;
    }
    
    // Adjust confidence based on transaction count (more evidence = higher confidence)
    if (info.transaction_count > 100) {
        cluster.confidence = std::min(1.0, cluster.confidence + 0.1);
    }
    
    LogPrint(BCLog::CVM, "HAT Consensus: Wallet cluster for %s: %d addresses, confidence=%.2f\n",
             address.ToString(), cluster.addresses.size(), cluster.confidence);
    
    return cluster;
}

int CVM::HATConsensusValidator::CountRecentFraudRecords(
    const std::vector<uint160>& addresses, 
    int blockWindow) {
    int count = 0;
    int currentHeight = chainActive.Height();
    int minHeight = currentHeight - blockWindow;
    
    // Check fraud records for each address in the cluster
    for (const auto& addr : addresses) {
        // Iterate through fraud records in database
        // TODO: Implement efficient database query
        // For now, simplified check
        
        // Check if address has recent fraud records
        std::string key = std::string(1, DB_FRAUD_RECORD) + addr.ToString();
        FraudRecord record;
        if (ReadFromDatabase(database, key, record)) {
            if (record.blockHeight >= minHeight) {
                count++;
            }
        }
    }
    
    return count;
}

bool CVM::HATConsensusValidator::ValidateFraudRecord(const FraudRecord& record) {
    // PROTECTION #1: Minimum score difference threshold
    if (std::abs(record.scoreDifference) < 5) {
        LogPrint(BCLog::CVM, "HAT Consensus: Fraud record rejected - score difference too small (%d)\n",
                 std::abs(record.scoreDifference));
        return false;
    }
    
    // PROTECTION #2: Check for Sybil attack patterns
    WalletCluster cluster = GetWalletCluster(record.fraudsterAddress);
    if (cluster.addresses.size() > 10) {
        // Large cluster - check for coordinated fraud accusations
        int recentFraudCount = CountRecentFraudRecords(cluster.addresses, 1000);
        if (recentFraudCount > 5) {
            LogPrintf("HAT Consensus: Fraud record rejected - possible Sybil attack (cluster size=%d, recent frauds=%d)\n",
                     cluster.addresses.size(), recentFraudCount);
            return false;
        }
    }
    
    // PROTECTION #3: Check timestamp validity
    int64_t currentTime = GetTime();
    if (record.timestamp > currentTime + 300) {  // Max 5 minutes in future
        LogPrintf("HAT Consensus: Fraud record rejected - future timestamp\n");
        return false;
    }
    if (currentTime - record.timestamp > 86400) {  // Max 24 hours old
        LogPrint(BCLog::CVM, "HAT Consensus: Fraud record rejected - too old\n");
        return false;
    }
    
    // PROTECTION #4: Check block height validity
    int currentHeight = chainActive.Height();
    if (record.blockHeight > currentHeight) {
        LogPrintf("HAT Consensus: Fraud record rejected - future block height\n");
        return false;
    }
    if (currentHeight - record.blockHeight > 144) {  // Max 144 blocks old (~6 hours)
        LogPrint(BCLog::CVM, "HAT Consensus: Fraud record rejected - block height too old\n");
        return false;
    }
    
    // PROTECTION #5: Verify score components are reasonable
    if (record.claimedScore.finalScore < 0 || record.claimedScore.finalScore > 100) {
        LogPrintf("HAT Consensus: Fraud record rejected - invalid claimed score (%d)\n",
                 record.claimedScore.finalScore);
        return false;
    }
    if (record.actualScore.finalScore < 0 || record.actualScore.finalScore > 100) {
        LogPrintf("HAT Consensus: Fraud record rejected - invalid actual score (%d)\n",
                 record.actualScore.finalScore);
        return false;
    }
    
    // All checks passed
    return true;
}

// Eclipse/Sybil Protection Implementation (Task 19.2.4)

bool HATConsensusValidator::IsValidatorEligible(const uint160& validatorAddr, int currentHeight)
{
    return m_eclipseSybilProtection->IsValidatorEligible(validatorAddr, currentHeight);
}

SybilDetectionResult HATConsensusValidator::DetectValidatorSybilNetwork(
    const std::vector<uint160>& validators,
    int currentHeight)
{
    return m_eclipseSybilProtection->DetectSybilNetwork(validators, currentHeight);
}

bool HATConsensusValidator::ValidateValidatorSetDiversity(
    const std::vector<uint160>& validators,
    int currentHeight)
{
    return m_eclipseSybilProtection->ValidateValidatorSetDiversity(validators, currentHeight);
}

void HATConsensusValidator::UpdateValidatorNetworkInfo(
    const uint160& validatorAddr,
    const CNetAddr& ipAddr,
    const std::set<uint160>& peers,
    int currentHeight)
{
    m_eclipseSybilProtection->UpdateValidatorNetworkInfo(validatorAddr, ipAddr, peers, currentHeight);
}

void HATConsensusValidator::UpdateValidatorStakeInfo(
    const uint160& validatorAddr,
    const ValidatorStakeInfo& stakeInfo)
{
    m_eclipseSybilProtection->UpdateValidatorStakeInfo(validatorAddr, stakeInfo);
}

bool HATConsensusValidator::CheckCrossGroupAgreement(
    const std::vector<uint160>& validators,
    const std::map<uint160, int>& votes)
{
    return m_eclipseSybilProtection->CheckCrossGroupAgreement(validators, votes);
}

bool HATConsensusValidator::EscalateSybilDetectionToDAO(
    const SybilDetectionResult& detectionResult,
    const CTransaction& tx)
{
    if (!detectionResult.isSybilNetwork) {
        return false;
    }
    
    // Only escalate if confidence is high (>60%)
    if (detectionResult.confidence < 0.60) {
        LogPrint(BCLog::CVM, "HATConsensusValidator: Sybil detection confidence %.0f%% too low for DAO escalation\n",
                detectionResult.confidence * 100);
        return false;
    }
    
    // Create dispute case
    DisputeCase dispute;
    dispute.disputeId = tx.GetHash();
    dispute.txHash = tx.GetHash();
    dispute.senderAddress = uint160();  // Not applicable for Sybil detection
    dispute.resolved = false;
    dispute.approved = false;
    dispute.resolutionTimestamp = 0;
    
    // Add evidence
    dispute.disputeReason = "Sybil network detected with " + std::to_string(detectionResult.confidence * 100) + "% confidence: " + detectionResult.reason;
    
    // Add suspicious validators to evidence data (serialize as string for now)
    std::string validatorList = "Suspicious validators: ";
    for (size_t i = 0; i < detectionResult.suspiciousValidators.size(); i++) {
        validatorList += detectionResult.suspiciousValidators[i].ToString();
        if (i < detectionResult.suspiciousValidators.size() - 1) {
            validatorList += ", ";
        }
    }
    dispute.evidenceData = std::vector<uint8_t>(validatorList.begin(), validatorList.end());
    
    // Escalate to DAO
    bool success = EscalateToDAO(dispute);
    
    if (success) {
        LogPrint(BCLog::CVM, "HATConsensusValidator: Escalated Sybil network detection to DAO (confidence: %.0f%%)\n",
                detectionResult.confidence * 100);
    } else {
        LogPrint(BCLog::CVM, "HATConsensusValidator: Failed to escalate Sybil network detection to DAO\n");
    }
    
    return success;
}

// Sybil Attack Detection Implementation (Task 19.2.3)

bool HATConsensusValidator::DetectSybilNetwork(const uint160& address) {
    // Calculate Sybil risk score
    double riskScore = CalculateSybilRiskScore(address);
    
    // Threshold for Sybil detection: 0.7 or higher
    const double SYBIL_THRESHOLD = 0.7;
    
    if (riskScore >= SYBIL_THRESHOLD) {
        LogPrintf("HAT Consensus: Sybil network detected for address %s (risk=%.2f)\n",
                 address.ToString(), riskScore);
        
        // Create alert for review
        std::string reason = strprintf("High Sybil risk score: %.2f", riskScore);
        CreateSybilAlert(address, riskScore, reason);
        
        return true;
    }
    
    return false;
}

double CVM::HATConsensusValidator::CalculateSybilRiskScore(const uint160& address) {
    double riskScore = 0.0;
    
    // Get wallet cluster
    WalletCluster cluster = GetWalletCluster(address);
    
    // FACTOR 1: Cluster size (weight: 0.25)
    // Large clusters are suspicious (potential Sybil network)
    double clusterSizeRisk = 0.0;
    if (cluster.addresses.size() > 50) {
        clusterSizeRisk = 1.0;  // Very suspicious
    } else if (cluster.addresses.size() > 20) {
        clusterSizeRisk = 0.8;  // Suspicious
    } else if (cluster.addresses.size() > 10) {
        clusterSizeRisk = 0.5;  // Moderately suspicious
    } else if (cluster.addresses.size() > 5) {
        clusterSizeRisk = 0.3;  // Slightly suspicious
    }
    // else: 0.0 (not suspicious)
    
    riskScore += clusterSizeRisk * 0.25;
    
    // FACTOR 2: Cluster creation time (weight: 0.20)
    // All addresses created recently = suspicious
    WalletClusterer clusterer(database);
    WalletClusterInfo info = clusterer.GetClusterInfo(clusterer.GetClusterForAddress(address));
    
    int64_t currentTime = GetTime();
    int64_t clusterAge = currentTime - info.first_seen;
    
    double creationTimeRisk = 0.0;
    if (clusterAge < 86400) {  // Less than 1 day old
        creationTimeRisk = 1.0;
    } else if (clusterAge < 604800) {  // Less than 1 week old
        creationTimeRisk = 0.7;
    } else if (clusterAge < 2592000) {  // Less than 30 days old
        creationTimeRisk = 0.4;
    }
    // else: 0.0 (established cluster)
    
    riskScore += creationTimeRisk * 0.20;
    
    // FACTOR 3: Transaction patterns (weight: 0.20)
    // Coordinated activity = suspicious
    double patternRisk = 0.0;
    
    // Check if all addresses have similar transaction counts
    if (cluster.addresses.size() > 1) {
        std::vector<uint32_t> txCounts;
        for (const auto& addr : cluster.addresses) {
            WalletClusterInfo addrInfo = clusterer.GetClusterInfo(addr);
            txCounts.push_back(addrInfo.transaction_count);
        }
        
        // Calculate coefficient of variation
        if (!txCounts.empty()) {
            double sum = 0.0;
            for (uint32_t count : txCounts) {
                sum += count;
            }
            double mean = sum / txCounts.size();
            
            double variance = 0.0;
            for (uint32_t count : txCounts) {
                variance += (count - mean) * (count - mean);
            }
            variance /= txCounts.size();
            double stddev = std::sqrt(variance);
            
            double cv = (mean > 0) ? (stddev / mean) : 0.0;
            
            // Low CV = coordinated (suspicious)
            if (cv < 0.3) {
                patternRisk = 0.9;
            } else if (cv < 0.5) {
                patternRisk = 0.6;
            } else if (cv < 0.7) {
                patternRisk = 0.3;
            }
        }
    }
    
    riskScore += patternRisk * 0.20;
    
    // FACTOR 4: Reputation patterns (weight: 0.20)
    // All addresses have similar reputation scores = suspicious
    double reputationRisk = 0.0;
    
    if (cluster.addresses.size() > 1) {
        std::vector<double> reputations;
        for (const auto& addr : cluster.addresses) {
            double rep = clusterer.GetEffectiveHATScore(addr);
            reputations.push_back(rep);
        }
        
        // Calculate coefficient of variation for reputation
        if (!reputations.empty()) {
            double sum = 0.0;
            for (double rep : reputations) {
                sum += rep;
            }
            double mean = sum / reputations.size();
            
            double variance = 0.0;
            for (double rep : reputations) {
                variance += (rep - mean) * (rep - mean);
            }
            variance /= reputations.size();
            double stddev = std::sqrt(variance);
            
            double cv = (mean > 0) ? (stddev / mean) : 0.0;
            
            // Low CV = similar reputations (suspicious)
            if (cv < 0.1) {
                reputationRisk = 1.0;
            } else if (cv < 0.2) {
                reputationRisk = 0.7;
            } else if (cv < 0.3) {
                reputationRisk = 0.4;
            }
        }
    }
    
    riskScore += reputationRisk * 0.20;
    
    // FACTOR 5: Fraud history (weight: 0.15)
    // Multiple fraud attempts in cluster = suspicious
    int fraudCount = CountRecentFraudRecords(cluster.addresses, 10000);  // Last 10,000 blocks
    
    double fraudRisk = 0.0;
    if (fraudCount >= 5) {
        fraudRisk = 1.0;
    } else if (fraudCount >= 3) {
        fraudRisk = 0.7;
    } else if (fraudCount >= 1) {
        fraudRisk = 0.4;
    }
    
    riskScore += fraudRisk * 0.15;
    
    LogPrint(BCLog::CVM, "HAT Consensus: Sybil risk score for %s: %.2f (cluster=%d, age=%d, pattern=%.2f, rep=%.2f, fraud=%d)\n",
             address.ToString(), riskScore, cluster.addresses.size(), clusterAge, patternRisk, reputationRisk, fraudCount);
    
    return riskScore;
}

bool CVM::HATConsensusValidator::ApplySybilPenalty(const WalletCluster& cluster) {
    LogPrintf("HAT Consensus: Applying Sybil penalty to cluster with %d addresses\n",
             cluster.addresses.size());
    
    // Apply reputation penalty to all addresses in cluster
    const int16_t SYBIL_PENALTY = 50;  // -50 reputation points
    
    for (const auto& addr : cluster.addresses) {
        // Record fraud attempt for each address
        FraudRecord record;
        record.fraudsterAddress = addr;
        record.timestamp = GetTime();
        record.blockHeight = chainActive.Height();
        record.reputationPenalty = SYBIL_PENALTY;
        record.scoreDifference = SYBIL_PENALTY;
        
        // Save to database
        std::string key = std::string(1, DB_FRAUD_RECORD) + addr.ToString() + "_sybil_" + std::to_string(record.timestamp);
        WriteToDatabase(database, key, record);
        
        LogPrint(BCLog::CVM, "HAT Consensus: Applied Sybil penalty to address %s\n",
                 addr.ToString());
    }
    
    return true;
}

bool CVM::HATConsensusValidator::CreateSybilAlert(const uint160& address, double riskScore, const std::string& reason) {
    LogPrintf("HAT Consensus: Creating Sybil alert for address %s (risk=%.2f): %s\n",
             address.ToString(), riskScore, reason);
    
    // Create alert structure
    SybilAlert alert;
    alert.address = address;
    alert.riskScore = riskScore;
    alert.reason = reason;
    alert.timestamp = GetTime();
    alert.blockHeight = chainActive.Height();
    alert.reviewed = false;
    
    // Save to database
    std::string key = "sybil_alert_" + address.ToString() + "_" + std::to_string(alert.timestamp);
    WriteToDatabase(database, key, alert);
    
    // If risk is very high (>= 0.9), automatically apply penalty
    if (riskScore >= 0.9) {
        WalletCluster cluster = GetWalletCluster(address);
        ApplySybilPenalty(cluster);
    }
    
    return true;
}

bool CVM::HATConsensusValidator::DetectCoordinatedSybilAttack(const std::vector<ValidationResponse>& responses) {
    if (responses.size() < 3) {
        return false;  // Need at least 3 validators to detect coordination
    }
    
    // Check for coordinated voting patterns
    // Indicators:
    // 1. Multiple validators from same wallet cluster
    // 2. All validators vote the same way with identical scores
    // 3. Validators have similar reputation scores
    // 4. Validators responded at nearly the same time
    
    // INDICATOR 1: Check for wallet clustering among validators
    std::map<uint160, std::vector<uint160>> clusterMap;  // cluster_id -> validator addresses
    
    WalletClusterer clusterer(database);
    for (const auto& response : responses) {
        uint160 clusterId = clusterer.GetClusterForAddress(response.validatorAddress);
        clusterMap[clusterId].push_back(response.validatorAddress);
    }
    
    // If any cluster has 3+ validators, that's suspicious
    for (const auto& pair : clusterMap) {
        if (pair.second.size() >= 3) {
            LogPrintf("HAT Consensus: Coordinated Sybil attack detected - %d validators from same cluster\n",
                     pair.second.size());
            return true;
        }
    }
    
    // INDICATOR 2: Check for identical votes and scores
    std::map<std::string, int> votePatterns;  // vote pattern -> count
    
    for (const auto& response : responses) {
        std::string pattern = strprintf("%d_%d_%d",
                                       static_cast<int>(response.vote),
                                       response.calculatedScore.finalScore,
                                       response.calculatedScore.behaviorScore);
        votePatterns[pattern]++;
    }
    
    // If 50%+ validators have identical pattern, suspicious
    for (const auto& pair : votePatterns) {
        if (pair.second >= (int)(responses.size() * 0.5)) {
            LogPrintf("HAT Consensus: Coordinated Sybil attack detected - %d validators with identical vote pattern\n",
                     pair.second);
            return true;
        }
    }
    
    // INDICATOR 3: Check for similar reputation scores among validators
    std::vector<double> validatorReputations;
    for (const auto& response : responses) {
        double rep = clusterer.GetEffectiveHATScore(response.validatorAddress);
        validatorReputations.push_back(rep);
    }
    
    if (validatorReputations.size() >= 3) {
        double sum = 0.0;
        for (double rep : validatorReputations) {
            sum += rep;
        }
        double mean = sum / validatorReputations.size();
        
        double variance = 0.0;
        for (double rep : validatorReputations) {
            variance += (rep - mean) * (rep - mean);
        }
        variance /= validatorReputations.size();
        double stddev = std::sqrt(variance);
        
        double cv = (mean > 0) ? (stddev / mean) : 0.0;
        
        // Very low CV = coordinated (suspicious)
        if (cv < 0.1) {
            LogPrintf("HAT Consensus: Coordinated Sybil attack detected - validators have suspiciously similar reputations (CV=%.2f)\n",
                     cv);
            return true;
        }
    }
    
    // INDICATOR 4: Check for coordinated response timing
    std::vector<uint64_t> timestamps;
    for (const auto& response : responses) {
        timestamps.push_back(response.timestamp);
    }
    
    if (timestamps.size() >= 3) {
        std::sort(timestamps.begin(), timestamps.end());
        
        // Check if all responses came within 1 second
        uint64_t timeSpan = timestamps.back() - timestamps.front();
        if (timeSpan <= 1) {
            LogPrintf("HAT Consensus: Coordinated Sybil attack detected - all validators responded within 1 second\n");
            return true;
        }
    }
    
    return false;
}

} // namespace CVM
