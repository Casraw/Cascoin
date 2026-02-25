// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_VALIDATOR_ATTESTATION_H
#define CASCOIN_CVM_VALIDATOR_ATTESTATION_H

#include "uint256.h"
#include "amount.h"
#include "serialize.h"
#include "sync.h"
#include <vector>
#include <map>
#include <cstdint>

class CNode;

// Forward declaration for CVMDatabase (in CVM namespace)
namespace CVM {
    class CVMDatabase;
}

/**
 * Automatic Validator Selection System
 * 
 * Validators are automatically selected from the pool of eligible addresses.
 * NO registration or announcement required - the system discovers eligible
 * validators by scanning on-chain data and selects them randomly.
 * 
 * Key Benefits:
 * - Fully automatic (no user action required)
 * - Truly decentralized (no registration = no gatekeeping)
 * - Fair selection (deterministic randomness from block hash)
 * - Sybil resistant (eligibility based on on-chain history)
 * - Passive income (validators earn fees without active participation)
 * 
 * How it works:
 * 1. System scans blockchain for addresses meeting eligibility criteria
 * 2. Eligible addresses are added to validator pool automatically
 * 3. For each validation task, random validators are selected from pool
 * 4. Selected validators are notified and compensated for participation
 */

/**
 * Validator eligibility record (discovered automatically from chain)
 * NOT announced by user - computed by each node independently
 */
struct ValidatorEligibilityRecord {
    uint160 validatorAddress;
    
    // On-chain verified metrics (computed, not self-reported)
    CAmount stakeAmount;
    int stakeAge;              // Blocks since stake was created
    int blocksSinceFirstSeen;  // Blocks since first transaction
    int transactionCount;      // Total transactions
    int uniqueInteractions;    // Unique addresses interacted with
    
    // Computed eligibility
    bool meetsStakeRequirement;
    bool meetsHistoryRequirement;
    bool meetsInteractionRequirement;
    bool isEligible;
    
    // Last update
    int64_t lastUpdateBlock;
    uint64_t lastUpdateTime;
    
    ValidatorEligibilityRecord() : 
        stakeAmount(0), stakeAge(0), blocksSinceFirstSeen(0),
        transactionCount(0), uniqueInteractions(0),
        meetsStakeRequirement(false), meetsHistoryRequirement(false),
        meetsInteractionRequirement(false), isEligible(false),
        lastUpdateBlock(0), lastUpdateTime(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(validatorAddress);
        READWRITE(stakeAmount);
        READWRITE(stakeAge);
        READWRITE(blocksSinceFirstSeen);
        READWRITE(transactionCount);
        READWRITE(uniqueInteractions);
        READWRITE(meetsStakeRequirement);
        READWRITE(meetsHistoryRequirement);
        READWRITE(meetsInteractionRequirement);
        READWRITE(isEligible);
        READWRITE(lastUpdateBlock);
        READWRITE(lastUpdateTime);
    }
    
    uint256 GetHash() const;
};

/**
 * Validator selection result
 * Contains the validators selected for a specific validation task
 */
struct ValidatorSelection {
    uint256 taskHash;          // Hash of the task (txHash + blockHeight)
    int64_t blockHeight;       // Block height when selection was made
    
    // Selected validators
    std::vector<uint160> selectedValidators;
    
    // Selection metadata
    uint256 selectionSeed;     // Deterministic seed used for selection
    int totalEligible;         // Total eligible validators at time of selection
    int targetCount;           // How many validators were requested
    
    uint64_t timestamp;
    
    ValidatorSelection() : blockHeight(0), totalEligible(0), targetCount(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(taskHash);
        READWRITE(blockHeight);
        READWRITE(selectedValidators);
        READWRITE(selectionSeed);
        READWRITE(totalEligible);
        READWRITE(targetCount);
        READWRITE(timestamp);
    }
    
    uint256 GetHash() const;
};

/**
 * Validation response from a selected validator
 * Sent when a validator participates in validation
 */
struct ValidationResponse {
    uint256 taskHash;          // Which task this responds to
    uint160 validatorAddress;  // Who is responding
    
    // Validation result
    bool isValid;              // Validator's verdict
    uint8_t confidence;        // 0-100, how confident in the result
    
    // Optional: Trust score from validator's WoT perspective
    uint8_t trustScore;        // 0-100, personalized trust score
    
    // Cryptographic proof
    std::vector<uint8_t> signature;
    uint64_t timestamp;
    
    ValidationResponse() : isValid(false), confidence(0), trustScore(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(taskHash);
        READWRITE(validatorAddress);
        READWRITE(isValid);
        READWRITE(confidence);
        READWRITE(trustScore);
        READWRITE(signature);
        READWRITE(timestamp);
    }
    
    uint256 GetHash() const;
    bool Sign(const std::vector<uint8_t>& privateKey);
    bool VerifySignature() const;
};

/**
 * Aggregated validation result
 * Combines responses from multiple validators
 */
struct AggregatedValidationResult {
    uint256 taskHash;
    
    // Aggregated result
    bool consensusReached;
    bool isValid;              // Final verdict (majority vote)
    double confidence;         // Aggregated confidence
    
    // Response statistics
    int totalSelected;         // How many validators were selected
    int totalResponded;        // How many responded
    int validVotes;            // Votes for valid
    int invalidVotes;          // Votes for invalid
    
    // Individual responses
    std::vector<ValidationResponse> responses;
    
    // Compensation info
    CAmount totalCompensation; // Total fees to distribute
    
    AggregatedValidationResult() :
        consensusReached(false), isValid(false), confidence(0.0),
        totalSelected(0), totalResponded(0), validVotes(0), invalidVotes(0),
        totalCompensation(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(taskHash);
        READWRITE(consensusReached);
        READWRITE(isValid);
        READWRITE(confidence);
        READWRITE(totalSelected);
        READWRITE(totalResponded);
        READWRITE(validVotes);
        READWRITE(invalidVotes);
        READWRITE(responses);
        READWRITE(totalCompensation);
    }
};

/**
 * Automatic Validator Selection Manager
 * 
 * Manages automatic discovery and selection of validators.
 * NO registration required - validators are discovered from on-chain data.
 */
class AutomaticValidatorManager {
private:
    CVM::CVMDatabase* db;
    CCriticalSection cs_validators;
    
    // Validator pool (discovered automatically)
    std::map<uint160, ValidatorEligibilityRecord> validatorPool;
    std::vector<uint160> eligibleValidators;  // Sorted list for fast selection
    
    // Selection cache
    std::map<uint256, ValidatorSelection> selectionCache;
    
    // Response tracking
    std::map<uint256, std::vector<ValidationResponse>> pendingResponses;
    
    // Pool update tracking
    int64_t lastPoolUpdateBlock;
    static const int POOL_UPDATE_INTERVAL = 100;  // Update pool every 100 blocks
    
    // Eligibility thresholds
    static const CAmount MIN_STAKE = 10 * COIN;           // 10 CAS minimum
    static const int MIN_STAKE_AGE = 40320;               // 70 days (70 * 576 blocks)
    static const int MIN_HISTORY_BLOCKS = 40320;          // 70 days presence
    static const int MIN_TRANSACTIONS = 100;              // 100+ transactions
    static const int MIN_UNIQUE_INTERACTIONS = 20;        // 20+ unique addresses
    
public:
    AutomaticValidatorManager(CVM::CVMDatabase* database);
    ~AutomaticValidatorManager();
    
    // ========== Pool Management (Automatic) ==========
    
    // Load validator pool from database on startup
    void LoadValidatorPool();
    
    // Scan blockchain and update validator pool
    void UpdateValidatorPool();
    void UpdateValidatorPool(int64_t currentBlock);
    
    // Check if address is eligible (computed, not announced)
    bool IsEligibleValidator(const uint160& address);
    ValidatorEligibilityRecord ComputeEligibility(const uint160& address);
    
    // Get all eligible validators
    std::vector<uint160> GetEligibleValidators();
    int GetEligibleValidatorCount();
    
    // ========== Validator Selection (Automatic & Random) ==========
    
    // Select random validators for a task
    ValidatorSelection SelectValidatorsForTask(const uint256& taskHash, int64_t blockHeight, int count = 10);
    
    // Deterministic random selection using block hash
    std::vector<uint160> SelectRandomValidators(const uint256& seed, int count);
    
    // Check if local node was selected for a task
    bool WasSelectedForTask(const uint256& taskHash, const uint160& myAddress);
    
    // ========== Validation Response Handling ==========
    
    // Process validation response from selected validator
    void ProcessValidationResponse(const ValidationResponse& response);
    
    // Aggregate responses and determine consensus
    AggregatedValidationResult AggregateResponses(const uint256& taskHash);
    
    // Check if consensus reached
    bool HasConsensus(const uint256& taskHash);
    
    // ========== Local Node Participation ==========
    
    // Called when local node is selected - generate response
    ValidationResponse GenerateValidationResponse(const uint256& taskHash, bool isValid, uint8_t confidence);
    
    // ========== Eligibility Verification (On-Chain) ==========
    
    // Verify stake requirements from UTXO set
    bool VerifyStakeRequirement(const uint160& address, CAmount& stakeAmount, int& stakeAge);
    
    // Verify history requirements from blockchain
    bool VerifyHistoryRequirement(const uint160& address, int& blocksSinceFirstSeen, 
                                  int& transactionCount, int& uniqueInteractions);
    
    // Verify anti-Sybil requirements (funding source diversity)
    bool VerifyAntiSybilRequirement(const uint160& address);
    
    // ========== Cache & Storage ==========
    
    void CacheSelection(const ValidatorSelection& selection);
    bool GetCachedSelection(const uint256& taskHash, ValidatorSelection& selection);
    
    void StoreEligibilityRecord(const ValidatorEligibilityRecord& record);
    bool GetEligibilityRecord(const uint160& address, ValidatorEligibilityRecord& record);
    
    // ========== Statistics ==========
    
    int GetTotalValidatorCount();
    int GetActiveValidatorCount();  // Validators who responded recently
    double GetAverageResponseRate();
};

// P2P message types for automatic validator system
extern const char* MSG_VALIDATION_TASK;       // Broadcast validation task to selected validators
extern const char* MSG_VALIDATION_RESPONSE;   // Response from selected validator

// P2P message handlers
void ProcessValidationTaskMessage(CNode* pfrom, const uint256& taskHash, int64_t blockHeight);
void ProcessValidationResponseMessage(CNode* pfrom, const ValidationResponse& response);

// Broadcast functions
class CConnman;
void BroadcastValidationTask(const uint256& taskHash, int64_t blockHeight, 
                             const std::vector<uint160>& selectedValidators, CConnman* connman);
void BroadcastValidationResponse(const ValidationResponse& response, CConnman* connman);

// Send message to specific peer (for attestation responses)
bool SendToPeer(CNode* peer, const std::string& msgType, const std::vector<uint8_t>& data, CConnman* connman);

// Helper function to derive validator address from public key
// Uses standard Bitcoin P2PKH derivation (Hash160 of public key)
class CPubKey;
uint160 DeriveValidatorAddress(const CPubKey& pubkey);

// Helper function to get the local validator's address
// Integrates with wallet to get primary receiving address
uint160 GetMyValidatorAddress();

// Helper function to get the validator's private key for signing
// Checks wallet lock status before access
class CKey;
bool GetValidatorKey(CKey& keyOut);

// Global automatic validator manager instance
extern AutomaticValidatorManager* g_automaticValidatorManager;

// ============================================================================
// Legacy Validator Attestation Types (for P2P message compatibility)
// ============================================================================

/**
 * Validator Eligibility Announcement (Legacy P2P message type)
 * Used for VALIDATOR_ANNOUNCE message handling
 */
struct ValidatorEligibilityAnnouncement {
    uint160 validatorAddress;
    CAmount stakeAmount;
    int stakeAge;
    uint64_t timestamp;
    std::vector<uint8_t> signature;
    
    ValidatorEligibilityAnnouncement() : stakeAmount(0), stakeAge(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(validatorAddress);
        READWRITE(stakeAmount);
        READWRITE(stakeAge);
        READWRITE(timestamp);
        READWRITE(signature);
    }
    
    std::string ToString() const {
        return validatorAddress.ToString();
    }
};

/**
 * Validator Attestation (Legacy P2P message type)
 * Used for VALIDATOR_ATTESTATION message handling
 */
struct ValidatorAttestation {
    uint160 validatorAddress;
    uint160 attestorAddress;
    uint8_t trustScore;
    uint64_t timestamp;
    std::vector<uint8_t> signature;
    
    ValidatorAttestation() : trustScore(0), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(validatorAddress);
        READWRITE(attestorAddress);
        READWRITE(trustScore);
        READWRITE(timestamp);
        READWRITE(signature);
    }
    
    std::string ToString() const {
        return validatorAddress.ToString();
    }
};

/**
 * Batch Attestation Request (Legacy P2P message type)
 * Used for BATCH_ATTESTATION_REQUEST message handling
 */
struct BatchAttestationRequest {
    std::vector<uint160> validators;
    uint160 requesterAddress;
    uint64_t timestamp;
    
    BatchAttestationRequest() : timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(validators);
        READWRITE(requesterAddress);
        READWRITE(timestamp);
    }
};

/**
 * Batch Attestation Response (Legacy P2P message type)
 * Used for BATCH_ATTESTATION_RESPONSE message handling
 */
struct BatchAttestationResponse {
    std::vector<ValidatorAttestation> attestations;
    uint160 responderAddress;
    uint64_t timestamp;
    
    BatchAttestationResponse() : timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(attestations);
        READWRITE(responderAddress);
        READWRITE(timestamp);
    }
};

/**
 * Legacy Validator Attestation Manager
 * Provides backward compatibility for P2P message handling
 */
class ValidatorAttestationManager {
private:
    CVM::CVMDatabase* db;
    CCriticalSection cs_attestations;
    
    // Attestation storage
    std::map<uint160, std::vector<ValidatorAttestation>> attestationsByValidator;
    std::map<uint160, ValidatorEligibilityAnnouncement> announcements;
    
public:
    ValidatorAttestationManager(CVM::CVMDatabase* database) : db(database) {}
    ~ValidatorAttestationManager() {}
    
    // Process incoming announcements
    bool ProcessAnnouncement(const ValidatorEligibilityAnnouncement& announcement) {
        LOCK(cs_attestations);
        announcements[announcement.validatorAddress] = announcement;
        return true;
    }
    
    // Process incoming attestations
    bool ProcessAttestation(const ValidatorAttestation& attestation) {
        LOCK(cs_attestations);
        attestationsByValidator[attestation.validatorAddress].push_back(attestation);
        return true;
    }
    
    // Get attestations for a validator
    std::vector<ValidatorAttestation> GetAttestationsForValidator(const uint160& validatorAddress) {
        LOCK(cs_attestations);
        auto it = attestationsByValidator.find(validatorAddress);
        if (it != attestationsByValidator.end()) {
            return it->second;
        }
        return std::vector<ValidatorAttestation>();
    }
    
    // Check if validator has announced
    bool HasAnnouncement(const uint160& validatorAddress) {
        LOCK(cs_attestations);
        return announcements.find(validatorAddress) != announcements.end();
    }
};

// Global legacy validator attestation manager instance
extern ValidatorAttestationManager* g_validatorAttestationManager;

// Legacy P2P message handlers
void ProcessValidatorAnnounceMessage(CNode* pfrom, const ValidatorEligibilityAnnouncement& announcement);
void ProcessAttestationRequestMessage(CNode* pfrom, const uint160& validatorAddress);
void ProcessValidatorAttestationMessage(CNode* pfrom, const ValidatorAttestation& attestation);
void ProcessBatchAttestationRequestMessage(CNode* pfrom, const BatchAttestationRequest& request);
void ProcessBatchAttestationResponseMessage(CNode* pfrom, const BatchAttestationResponse& response);

#endif // CASCOIN_CVM_VALIDATOR_ATTESTATION_H
