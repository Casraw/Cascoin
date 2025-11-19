// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validator_attestation.h"
#include "cvmdb.h"
#include "trustgraph.h"
#include "reputation.h"
#include "securehat.h"
#include "hash.h"
#include "utiltime.h"
#include "net.h"
#include "validation.h"
#include "chainparams.h"
#include "random.h"
#include "util.h"
#include "streams.h"
#include "version.h"
#include <algorithm>
#include <cmath>

// P2P message types
const char* MSG_VALIDATOR_ANNOUNCE = "valannounce";
const char* MSG_ATTESTATION_REQUEST = "attestreq";
const char* MSG_VALIDATOR_ATTESTATION = "attestation";
const char* MSG_BATCH_ATTESTATION_REQUEST = "batchattest";
const char* MSG_BATCH_ATTESTATION_RESPONSE = "batchresp";

// Global attestation manager instance
ValidatorAttestationManager* g_validatorAttestationManager = nullptr;

// Helper function to get validator address
// TODO: Integrate with wallet to get actual validator address
uint160 GetMyValidatorAddress();

// ValidatorEligibilityAnnouncement implementation
uint256 ValidatorEligibilityAnnouncement::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << validatorAddress;
    ss << stakeAmount;
    ss << stakeAge;
    ss << blocksSinceFirstSeen;
    ss << transactionCount;
    ss << uniqueInteractions;
    ss << timestamp;
    return ss.GetHash();
}

bool ValidatorEligibilityAnnouncement::Sign(const std::vector<uint8_t>& privateKey) {
    // TODO: Implement signature generation
    // For now, placeholder
    signature.resize(64);
    return true;
}

bool ValidatorEligibilityAnnouncement::VerifySignature() const {
    // TODO: Implement signature verification
    // For now, placeholder
    return !signature.empty();
}

// ValidatorAttestation implementation
uint256 ValidatorAttestation::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << validatorAddress;
    ss << attestorAddress;
    ss << stakeVerified;
    ss << historyVerified;
    ss << networkParticipationVerified;
    ss << behaviorVerified;
    ss << trustScore;
    ss << confidence;
    ss << timestamp;
    ss << attestorReputation;
    return ss.GetHash();
}

bool ValidatorAttestation::Sign(const std::vector<uint8_t>& privateKey) {
    // TODO: Implement signature generation
    signature.resize(64);
    return true;
}

bool ValidatorAttestation::VerifySignature() const {
    // TODO: Implement signature verification
    return !signature.empty();
}

// CompressedAttestation implementation
CompressedAttestation CompressedAttestation::Compress(const ValidatorAttestation& attestation) {
    CompressedAttestation compressed;
    compressed.validatorAddress = attestation.validatorAddress;
    compressed.attestorAddress = attestation.attestorAddress;
    
    // Compress boolean flags into single byte
    compressed.flags = 0;
    if (attestation.stakeVerified) compressed.flags |= (1 << 0);
    if (attestation.historyVerified) compressed.flags |= (1 << 1);
    if (attestation.networkParticipationVerified) compressed.flags |= (1 << 2);
    if (attestation.behaviorVerified) compressed.flags |= (1 << 3);
    
    compressed.trustScore = attestation.trustScore;
    compressed.confidenceByte = static_cast<uint8_t>(attestation.confidence * 255.0);
    compressed.attestorReputation = attestation.attestorReputation;
    compressed.signature = attestation.signature;
    compressed.timestamp = attestation.timestamp;
    
    return compressed;
}

ValidatorAttestation CompressedAttestation::Decompress() const {
    ValidatorAttestation attestation;
    attestation.validatorAddress = validatorAddress;
    attestation.attestorAddress = attestorAddress;
    
    // Decompress boolean flags
    attestation.stakeVerified = (flags & (1 << 0)) != 0;
    attestation.historyVerified = (flags & (1 << 1)) != 0;
    attestation.networkParticipationVerified = (flags & (1 << 2)) != 0;
    attestation.behaviorVerified = (flags & (1 << 3)) != 0;
    
    attestation.trustScore = trustScore;
    attestation.confidence = confidenceByte / 255.0;
    attestation.attestorReputation = attestorReputation;
    attestation.signature = signature;
    attestation.timestamp = timestamp;
    
    return attestation;
}

// BatchAttestationRequest implementation
uint256 BatchAttestationRequest::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    for (const auto& validator : validators) {
        ss << validator;
    }
    ss << timestamp;
    return ss.GetHash();
}

bool BatchAttestationRequest::Sign(const std::vector<uint8_t>& privateKey) {
    // TODO: Implement signature generation
    signature.resize(64);
    return true;
}

bool BatchAttestationRequest::VerifySignature() const {
    // TODO: Implement signature verification
    return !signature.empty();
}

// ValidatorAttestationManager implementation
ValidatorAttestationManager::ValidatorAttestationManager(CVMDatabase* database)
    : db(database), cacheExpiry(10000) {
    // Initialize bloom filter (1MB = 8M bits)
    attestationBloomFilter.resize(8 * 1024 * 1024, false);
}

ValidatorAttestationManager::~ValidatorAttestationManager() {
}

void ValidatorAttestationManager::AnnounceValidatorEligibility(const uint160& validatorAddress) {
    // Create announcement
    ValidatorEligibilityAnnouncement announcement = CreateEligibilityAnnouncement(validatorAddress);
    
    // Store announcement
    StoreValidatorAnnouncement(announcement);
    
    // Broadcast to network
    // TODO: Pass actual connman instance
    BroadcastValidatorAnnouncement(announcement, nullptr);
    
    // Request attestations from random nodes
    std::vector<uint160> attestors = SelectRandomAttestors(10);
    for (const auto& attestor : attestors) {
        // TODO: Pass actual connman instance
        SendAttestationRequest(attestor, validatorAddress, nullptr);
    }
    
    LogPrintf("ValidatorAttestation: Announced eligibility for %s, requested %d attestations\n",
              validatorAddress.ToString(), attestors.size());
}

ValidatorEligibilityAnnouncement ValidatorAttestationManager::CreateEligibilityAnnouncement(
    const uint160& validatorAddress) {
    ValidatorEligibilityAnnouncement announcement;
    announcement.validatorAddress = validatorAddress;
    
    // Get self-reported metrics
    int stakeAge = 0;
    VerifyStake(validatorAddress, announcement.stakeAmount, stakeAge);
    announcement.stakeAge = stakeAge;
    
    int blocksSinceFirstSeen = 0;
    int transactionCount = 0;
    int uniqueInteractions = 0;
    VerifyHistory(validatorAddress, blocksSinceFirstSeen, transactionCount, uniqueInteractions);
    announcement.blocksSinceFirstSeen = blocksSinceFirstSeen;
    announcement.transactionCount = transactionCount;
    announcement.uniqueInteractions = uniqueInteractions;
    
    announcement.timestamp = GetTime();
    announcement.requestAttestations = true;
    
    // Sign announcement
    std::vector<uint8_t> privateKey;  // TODO: Get from wallet
    announcement.Sign(privateKey);
    
    return announcement;
}

void ValidatorAttestationManager::ProcessValidatorAnnouncement(
    const ValidatorEligibilityAnnouncement& announcement) {
    // Verify signature
    if (!announcement.VerifySignature()) {
        LogPrintf("ValidatorAttestation: Invalid signature on announcement from %s\n",
                  announcement.validatorAddress.ToString());
        return;
    }
    
    // Store announcement
    StoreValidatorAnnouncement(announcement);
    
    // If we're eligible to attest, generate attestation
    uint160 myAddress = GetMyValidatorAddress();
    if (!myAddress.IsNull() && IsEligibleAttestor(myAddress)) {
        ProcessAttestationRequest(announcement.validatorAddress);
    }
    
    LogPrintf("ValidatorAttestation: Processed announcement from %s\n",
              announcement.validatorAddress.ToString());
}

ValidatorAttestation ValidatorAttestationManager::GenerateAttestation(const uint160& validatorAddress) {
    ValidatorAttestation attestation;
    attestation.validatorAddress = validatorAddress;
    attestation.attestorAddress = GetMyValidatorAddress();
    
    if (attestation.attestorAddress.IsNull()) {
        LogPrintf("ValidatorAttestation: Cannot generate attestation - no validator address configured\n");
        return attestation;
    }
    
    // Verify objective criteria (on-chain)
    CAmount stakeAmount;
    int stakeAge;
    attestation.stakeVerified = VerifyStake(validatorAddress, stakeAmount, stakeAge);
    
    int blocksSinceFirstSeen, transactionCount, uniqueInteractions;
    attestation.historyVerified = VerifyHistory(validatorAddress, blocksSinceFirstSeen, 
                                                transactionCount, uniqueInteractions);
    
    attestation.networkParticipationVerified = VerifyNetworkParticipation(validatorAddress);
    attestation.behaviorVerified = VerifyBehavior(validatorAddress);
    
    // Calculate trust score (personalized, based on my WoT)
    attestation.trustScore = CalculateMyTrustScore(validatorAddress);
    
    // Calculate confidence (based on my WoT connectivity)
    attestation.confidence = CalculateAttestationConfidence(validatorAddress);
    
    // Add my own reputation (for weighting)
    attestation.attestorReputation = GetMyReputation();
    
    // Sign attestation
    attestation.timestamp = GetTime();
    std::vector<uint8_t> privateKey;  // TODO: Get from wallet
    attestation.Sign(privateKey);
    
    return attestation;
}

void ValidatorAttestationManager::ProcessAttestationRequest(const uint160& validatorAddress) {
    // Generate attestation
    ValidatorAttestation attestation = GenerateAttestation(validatorAddress);
    
    // Store attestation
    StoreAttestation(attestation);
    
    // Broadcast attestation to network (transparency)
    // TODO: Pass actual connman instance
    BroadcastAttestation(attestation, nullptr);
    
    LogPrintf("ValidatorAttestation: Generated attestation for %s (trust=%d, confidence=%.2f)\n",
              validatorAddress.ToString(), attestation.trustScore, attestation.confidence);
}

BatchAttestationResponse ValidatorAttestationManager::ProcessBatchAttestationRequest(
    const BatchAttestationRequest& request) {
    // Verify signature
    if (!request.VerifySignature()) {
        LogPrintf("ValidatorAttestation: Invalid signature on batch request\n");
        return BatchAttestationResponse();
    }
    
    BatchAttestationResponse response;
    response.timestamp = GetTime();
    
    // Generate attestations for all validators in batch
    for (const auto& validator : request.validators) {
        ValidatorAttestation attestation = GenerateAttestation(validator);
        response.attestations.push_back(CompressedAttestation::Compress(attestation));
        
        // Also store attestation
        StoreAttestation(attestation);
    }
    
    LogPrintf("ValidatorAttestation: Generated %d attestations for batch request\n",
              response.attestations.size());
    
    return response;
}

void ValidatorAttestationManager::ProcessBatchAttestationResponse(
    const BatchAttestationResponse& response) {
    // Process each attestation in batch
    for (const auto& compressed : response.attestations) {
        ValidatorAttestation attestation = compressed.Decompress();
        
        // Verify signature
        if (!attestation.VerifySignature()) {
            LogPrintf("ValidatorAttestation: Invalid signature on attestation from %s\n",
                      attestation.attestorAddress.ToString());
            continue;
        }
        
        // Store attestation
        StoreAttestation(attestation);
        
        // Add to pending attestations
        LOCK(cs_attestations);
        pendingAttestations[attestation.validatorAddress].push_back(attestation);
    }
    
    LogPrintf("ValidatorAttestation: Processed batch response with %d attestations\n",
              response.attestations.size());
}

ValidatorCompositeScore ValidatorAttestationManager::AggregateAttestations(
    const uint160& validatorAddress,
    const std::vector<ValidatorAttestation>& attestations) {
    ValidatorCompositeScore score;
    score.validatorAddress = validatorAddress;
    score.attestations = attestations;
    score.attestationCount = attestations.size();
    
    if (attestations.empty()) {
        return score;
    }
    
    // Check objective criteria (require 80%+ agreement)
    int stakeVerifiedCount = 0;
    int historyVerifiedCount = 0;
    int networkVerifiedCount = 0;
    
    for (const auto& attestation : attestations) {
        if (attestation.stakeVerified) stakeVerifiedCount++;
        if (attestation.historyVerified) historyVerifiedCount++;
        if (attestation.networkParticipationVerified) networkVerifiedCount++;
    }
    
    score.stakeVerified = (stakeVerifiedCount >= attestations.size() * 0.8);
    score.historyVerified = (historyVerifiedCount >= attestations.size() * 0.8);
    score.networkVerified = (networkVerifiedCount >= attestations.size() * 0.8);
    
    // Calculate weighted average trust score
    double totalWeight = 0;
    double weightedSum = 0;
    
    for (const auto& attestation : attestations) {
        // Weight by attestor's reputation and confidence
        double weight = (attestation.attestorReputation / 100.0) * attestation.confidence;
        weightedSum += attestation.trustScore * weight;
        totalWeight += weight;
    }
    
    score.averageTrustScore = (totalWeight > 0) ? (weightedSum / totalWeight) : 0;
    
    // Calculate variance in trust scores
    double sumSquaredDiff = 0;
    for (const auto& attestation : attestations) {
        double diff = attestation.trustScore - score.averageTrustScore;
        sumSquaredDiff += diff * diff;
    }
    score.trustScoreVariance = sqrt(sumSquaredDiff / attestations.size());
    
    // Calculate eligibility confidence
    score.eligibilityConfidence = CalculateEligibilityConfidence(score);
    
    // Determine eligibility
    score.isEligible = CalculateValidatorEligibility(score);
    
    // Cache metadata
    score.cacheBlock = chainActive.Height();
    int stakeAge;
    VerifyStake(validatorAddress, score.stakeAmount, stakeAge);
    int blocksSinceFirstSeen, uniqueInteractions;
    VerifyHistory(validatorAddress, blocksSinceFirstSeen, score.transactionCount, uniqueInteractions);
    
    return score;
}

bool ValidatorAttestationManager::IsValidatorEligible(const uint160& validatorAddress) {
    // Check cache first
    ValidatorCompositeScore cached;
    if (GetCachedScore(validatorAddress, cached)) {
        return cached.isEligible;
    }
    
    // Check if we have pending attestations
    LOCK(cs_attestations);
    auto it = pendingAttestations.find(validatorAddress);
    if (it != pendingAttestations.end() && !it->second.empty()) {
        // Aggregate pending attestations
        ValidatorCompositeScore score = AggregateAttestations(validatorAddress, it->second);
        
        // Cache result
        CacheScore(validatorAddress, score);
        
        // Clear pending attestations
        pendingAttestations.erase(it);
        
        return score.isEligible;
    }
    
    // No attestations available
    return false;
}

bool ValidatorAttestationManager::CalculateValidatorEligibility(const ValidatorCompositeScore& score) {
    // Require minimum attestations
    if (score.attestationCount < 10) {
        return false;
    }
    
    // Require objective criteria verified
    if (!score.stakeVerified || !score.historyVerified || !score.networkVerified) {
        return false;
    }
    
    // Require reasonable trust score
    if (score.averageTrustScore < 50) {
        return false;
    }
    
    // Check variance (low variance = consensus, high variance = suspicious)
    if (score.trustScoreVariance > 30) {
        return false;
    }
    
    // Require high confidence
    if (score.eligibilityConfidence < 0.70) {
        return false;
    }
    
    return true;
}

double ValidatorAttestationManager::CalculateEligibilityConfidence(const ValidatorCompositeScore& score) {
    // Base confidence on number of attestations
    double attestationConfidence = std::min(1.0, score.attestationCount / 10.0);
    
    // Reduce confidence if objective criteria not verified
    double objectiveConfidence = 0.0;
    if (score.stakeVerified) objectiveConfidence += 0.33;
    if (score.historyVerified) objectiveConfidence += 0.33;
    if (score.networkVerified) objectiveConfidence += 0.34;
    
    // Reduce confidence if high variance (disagreement)
    double varianceConfidence = 1.0 - (score.trustScoreVariance / 100.0);
    varianceConfidence = std::max(0.0, varianceConfidence);
    
    // Combine confidences
    return attestationConfidence * objectiveConfidence * varianceConfidence;
}

bool ValidatorAttestationManager::GetCachedScore(const uint160& validator, 
                                                 ValidatorCompositeScore& score) {
    LOCK(cs_attestations);
    
    auto it = attestationCache.find(validator);
    if (it == attestationCache.end()) {
        return false;
    }
    
    // Check if expired
    int64_t age = chainActive.Height() - cacheTimestamps[validator];
    if (age > cacheExpiry) {
        attestationCache.erase(validator);
        cacheTimestamps.erase(validator);
        return false;
    }
    
    score = it->second;
    return true;
}

void ValidatorAttestationManager::CacheScore(const uint160& validator, 
                                             const ValidatorCompositeScore& score) {
    LOCK(cs_attestations);
    attestationCache[validator] = score;
    cacheTimestamps[validator] = chainActive.Height();
    
    // Add to bloom filter
    AddToBloomFilter(validator);
}

bool ValidatorAttestationManager::RequiresReattestation(const uint160& validator) {
    ValidatorCompositeScore cached;
    if (!GetCachedScore(validator, cached)) {
        return true;  // No cache, need attestation
    }
    
    // Check if stake changed significantly (>10%)
    CAmount currentStake;
    int stakeAge;
    VerifyStake(validator, currentStake, stakeAge);
    if (abs(currentStake - cached.stakeAmount) > cached.stakeAmount * 0.1) {
        return true;
    }
    
    // Check if transaction count increased significantly (>20%)
    int blocksSinceFirstSeen, currentTxCount, uniqueInteractions;
    VerifyHistory(validator, blocksSinceFirstSeen, currentTxCount, uniqueInteractions);
    if (currentTxCount > cached.transactionCount * 1.2) {
        return true;
    }
    
    // Check if cache expired
    int64_t age = chainActive.Height() - cached.cacheBlock;
    if (age > cacheExpiry) {
        return true;
    }
    
    return false;
}

void ValidatorAttestationManager::InvalidateCache(const uint160& validator) {
    LOCK(cs_attestations);
    attestationCache.erase(validator);
    cacheTimestamps.erase(validator);
}

std::vector<uint160> ValidatorAttestationManager::SelectRandomAttestors(int count) {
    // TODO: Implement proper attestor selection from connected peers
    // For now, return empty vector - this will be implemented when integrating with P2P layer
    std::vector<uint160> eligibleAttestors;
    
    LogPrint(BCLog::CVM, "ValidatorAttestation: SelectRandomAttestors not yet fully implemented\n");
    
    return eligibleAttestors;
}

bool ValidatorAttestationManager::IsEligibleAttestor(const uint160& address) {
    // Attestor must have minimum reputation (30+)
    uint8_t reputation = GetMyReputation();  // TODO: Get reputation for address
    if (reputation < 30) {
        return false;
    }
    
    // Attestor must be connected for minimum time (1000 blocks)
    // TODO: Check connection time
    
    return true;
}

bool ValidatorAttestationManager::VerifyStake(const uint160& validatorAddress, 
                                              CAmount& stakeAmount, int& stakeAge) {
    // Eligibility criteria:
    // - Minimum 10 CAS stake
    // - Stake aged 70 days (70 * 576 blocks = 40,320 blocks)
    // - Stake from 3+ diverse sources
    
    // TODO: Implement actual stake verification
    // For now, placeholder
    stakeAmount = 10 * COIN;
    stakeAge = 40320;
    
    return stakeAmount >= 10 * COIN && stakeAge >= 40320;
}

bool ValidatorAttestationManager::VerifyHistory(const uint160& validatorAddress,
                                                int& blocksSinceFirstSeen,
                                                int& transactionCount,
                                                int& uniqueInteractions) {
    // Eligibility criteria:
    // - 70 days presence (40,320 blocks)
    // - 100+ transactions
    // - 20+ unique interactions
    
    // TODO: Implement actual history verification
    // For now, placeholder
    blocksSinceFirstSeen = 40320;
    transactionCount = 100;
    uniqueInteractions = 20;
    
    return blocksSinceFirstSeen >= 40320 && 
           transactionCount >= 100 && 
           uniqueInteractions >= 20;
}

bool ValidatorAttestationManager::VerifyNetworkParticipation(const uint160& validatorAddress) {
    // Eligibility criteria:
    // - 1000+ blocks connected
    // - 1+ peer (inclusive of home users)
    
    // TODO: Implement actual network participation verification
    // For now, placeholder
    return true;
}

bool ValidatorAttestationManager::VerifyBehavior(const uint160& validatorAddress) {
    // Check for fraud records, suspicious behavior, etc.
    
    // TODO: Implement actual behavior verification
    // For now, placeholder
    return true;
}

uint8_t ValidatorAttestationManager::CalculateMyTrustScore(const uint160& validatorAddress) {
    // Calculate personalized trust score based on my WoT perspective
    
    // TODO: Integrate with TrustGraph and SecureHAT
    // For now, return neutral score
    return 50;
}

double ValidatorAttestationManager::CalculateAttestationConfidence(const uint160& validatorAddress) {
    // Calculate confidence based on my WoT connectivity to validator
    
    // TODO: Integrate with TrustGraph
    // For now, return moderate confidence
    return 0.75;
}

uint8_t ValidatorAttestationManager::GetMyReputation() {
    // Get my own reputation score
    
    // TODO: Integrate with ReputationSystem
    // For now, return moderate reputation
    return 50;
}

// Helper function to get validator address
// TODO: Integrate with wallet to get actual validator address
uint160 GetMyValidatorAddress() {
    return uint160();
}

void ValidatorAttestationManager::StoreAttestation(const ValidatorAttestation& attestation) {
    if (!db) {
        LogPrint(BCLog::CVM, "ValidatorAttestation: Database not initialized, cannot store attestation\n");
        return;
    }
    
    // Store attestation in database
    std::string key = "attestation_" + attestation.validatorAddress.ToString() + "_" + 
                     attestation.attestorAddress.ToString();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << attestation;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    // TODO: Implement Write method or use alternative storage
    // db->Write(key, data);
    
    LogPrint(BCLog::CVM, "ValidatorAttestation: Stored attestation for %s from %s\n",
             attestation.validatorAddress.ToString(), attestation.attestorAddress.ToString());
}

std::vector<ValidatorAttestation> ValidatorAttestationManager::GetAttestations(
    const uint160& validatorAddress) {
    std::vector<ValidatorAttestation> attestations;
    
    if (!db) return attestations;
    
    // TODO: Implement database iteration to get all attestations for validator
    // For now, return empty vector
    
    return attestations;
}

void ValidatorAttestationManager::StoreValidatorAnnouncement(
    const ValidatorEligibilityAnnouncement& announcement) {
    if (!db) {
        LogPrint(BCLog::CVM, "ValidatorAttestation: Database not initialized, cannot store announcement\n");
        return;
    }
    
    std::string key = "announcement_" + announcement.validatorAddress.ToString();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << announcement;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    // TODO: Implement Write method or use alternative storage
    // db->Write(key, data);
    
    LogPrint(BCLog::CVM, "ValidatorAttestation: Stored announcement for %s\n",
             announcement.validatorAddress.ToString());
}

void ValidatorAttestationManager::AddToBloomFilter(const uint160& validator) {
    // Add validator to bloom filter using 3 hash functions
    for (int i = 0; i < 3; i++) {
        CHashWriter ss(SER_GETHASH, i);
        ss << validator;
        uint256 hash = ss.GetHash();
        
        size_t index = hash.GetUint64(0) % attestationBloomFilter.size();
        attestationBloomFilter[index] = true;
    }
}

bool ValidatorAttestationManager::MayHaveAttestation(const uint160& validator) {
    // Check bloom filter using 3 hash functions
    for (int i = 0; i < 3; i++) {
        CHashWriter ss(SER_GETHASH, i);
        ss << validator;
        uint256 hash = ss.GetHash();
        
        size_t index = hash.GetUint64(0) % attestationBloomFilter.size();
        if (!attestationBloomFilter[index]) {
            return false;
        }
    }
    return true;
}

int ValidatorAttestationManager::GetAttestationCount(const uint160& validatorAddress) {
    LOCK(cs_attestations);
    auto it = pendingAttestations.find(validatorAddress);
    if (it != pendingAttestations.end()) {
        return it->second.size();
    }
    return 0;
}

int ValidatorAttestationManager::GetCachedValidatorCount() {
    LOCK(cs_attestations);
    return attestationCache.size();
}

int ValidatorAttestationManager::GetPendingAttestationCount() {
    LOCK(cs_attestations);
    int total = 0;
    for (const auto& pair : pendingAttestations) {
        total += pair.second.size();
    }
    return total;
}

// P2P message handlers
void ProcessValidatorAnnounceMessage(CNode* pfrom, const ValidatorEligibilityAnnouncement& announcement) {
    if (!g_validatorAttestationManager) {
        return;
    }
    
    // Process announcement
    g_validatorAttestationManager->ProcessValidatorAnnouncement(announcement);
    
    // Note: Relay is handled by net_processing.cpp using gossip protocol
}

void ProcessAttestationRequestMessage(CNode* pfrom, const uint160& validatorAddress) {
    if (!g_validatorAttestationManager) {
        return;
    }
    
    // Generate attestation
    g_validatorAttestationManager->ProcessAttestationRequest(validatorAddress);
}

void ProcessValidatorAttestationMessage(CNode* pfrom, const ValidatorAttestation& attestation) {
    if (!g_validatorAttestationManager) {
        return;
    }
    
    // Verify signature
    if (!attestation.VerifySignature()) {
        LogPrintf("ValidatorAttestation: Invalid signature on attestation from %s\n",
                  attestation.attestorAddress.ToString());
        return;
    }
    
    // Store attestation
    g_validatorAttestationManager->StoreAttestation(attestation);
    
    // Note: Relay is handled by net_processing.cpp using gossip protocol
}

void ProcessBatchAttestationRequestMessage(CNode* pfrom, const BatchAttestationRequest& request) {
    if (!g_validatorAttestationManager) {
        return;
    }
    
    // Process batch request
    BatchAttestationResponse response = g_validatorAttestationManager->ProcessBatchAttestationRequest(request);
    
    // Note: Response sending is handled by net_processing.cpp
    // Store response for retrieval
    // TODO: Implement response storage and retrieval mechanism
}

void ProcessBatchAttestationResponseMessage(CNode* pfrom, const BatchAttestationResponse& response) {
    if (!g_validatorAttestationManager) {
        return;
    }
    
    // Process batch response
    g_validatorAttestationManager->ProcessBatchAttestationResponse(response);
}

// Broadcast functions
void BroadcastValidatorAnnouncement(const ValidatorEligibilityAnnouncement& announcement, CConnman* connman) {
    if (!connman) return;
    
    // TODO: Implement using connman->ForEachNode
    // For now, placeholder
    LogPrint(BCLog::CVM, "ValidatorAttestation: BroadcastValidatorAnnouncement not yet fully implemented\n");
}

void BroadcastAttestation(const ValidatorAttestation& attestation, CConnman* connman) {
    if (!connman) return;
    
    // TODO: Implement using connman->ForEachNode
    // For now, placeholder
    LogPrint(BCLog::CVM, "ValidatorAttestation: BroadcastAttestation not yet fully implemented\n");
}

void SendAttestationRequest(const uint160& peer, const uint160& validatorAddress, CConnman* connman) {
    if (!connman) return;
    
    // TODO: Find CNode* for peer address and send request
    // For now, placeholder
    LogPrint(BCLog::CVM, "ValidatorAttestation: SendAttestationRequest not yet fully implemented\n");
}

void SendBatchAttestationRequest(const uint160& peer, const std::vector<uint160>& validators, CConnman* connman) {
    if (!connman) return;
    
    // Create batch request
    BatchAttestationRequest request;
    request.validators = validators;
    request.timestamp = GetTime();
    
    // Sign request
    std::vector<uint8_t> privateKey;  // TODO: Get from wallet
    request.Sign(privateKey);
    
    // TODO: Find CNode* for peer address and send request
    // For now, placeholder
    LogPrint(BCLog::CVM, "ValidatorAttestation: SendBatchAttestationRequest not yet fully implemented\n");
}
