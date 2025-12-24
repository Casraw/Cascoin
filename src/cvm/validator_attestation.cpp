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

// P2P message types for automatic validator system
const char* MSG_VALIDATION_TASK = "valtask";
const char* MSG_VALIDATION_RESPONSE = "valresp";

// Global automatic validator manager instance
AutomaticValidatorManager* g_automaticValidatorManager = nullptr;

// Helper function to get local validator address
// TODO: Integrate with wallet to get actual validator address
uint160 GetMyValidatorAddress();

// ============================================================================
// ValidatorEligibilityRecord implementation
// ============================================================================

uint256 ValidatorEligibilityRecord::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << validatorAddress;
    ss << stakeAmount;
    ss << stakeAge;
    ss << blocksSinceFirstSeen;
    ss << transactionCount;
    ss << uniqueInteractions;
    ss << lastUpdateBlock;
    return ss.GetHash();
}

// ============================================================================
// ValidatorSelection implementation
// ============================================================================

uint256 ValidatorSelection::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << taskHash;
    ss << blockHeight;
    for (const auto& v : selectedValidators) {
        ss << v;
    }
    ss << selectionSeed;
    return ss.GetHash();
}

// ============================================================================
// ValidationResponse implementation
// ============================================================================

uint256 ValidationResponse::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << taskHash;
    ss << validatorAddress;
    ss << isValid;
    ss << confidence;
    ss << trustScore;
    ss << timestamp;
    return ss.GetHash();
}

bool ValidationResponse::Sign(const std::vector<uint8_t>& privateKey) {
    // TODO: Implement actual signature generation using secp256k1
    signature.resize(64);
    return true;
}

bool ValidationResponse::VerifySignature() const {
    // TODO: Implement actual signature verification
    return !signature.empty();
}

// ============================================================================
// AutomaticValidatorManager implementation
// ============================================================================

AutomaticValidatorManager::AutomaticValidatorManager(CVMDatabase* database)
    : db(database), lastPoolUpdateBlock(0) {
    LogPrintf("AutomaticValidatorManager: Initialized automatic validator selection system\n");
}

AutomaticValidatorManager::~AutomaticValidatorManager() {
}

void AutomaticValidatorManager::UpdateValidatorPool() {
    UpdateValidatorPool(chainActive.Height());
}

void AutomaticValidatorManager::UpdateValidatorPool(int64_t currentBlock) {
    // Only update every POOL_UPDATE_INTERVAL blocks
    if (currentBlock - lastPoolUpdateBlock < POOL_UPDATE_INTERVAL) {
        return;
    }
    
    LOCK(cs_validators);
    
    LogPrintf("AutomaticValidatorManager: Updating validator pool at block %d\n", currentBlock);
    
    // Clear eligible list for rebuild
    eligibleValidators.clear();
    
    // Scan all known addresses and check eligibility
    // In practice, this would iterate through address index or UTXO set
    // For now, we update existing pool entries
    
    for (auto& pair : validatorPool) {
        ValidatorEligibilityRecord& record = pair.second;
        
        // Re-compute eligibility
        ValidatorEligibilityRecord updated = ComputeEligibility(record.validatorAddress);
        record = updated;
        
        if (record.isEligible) {
            eligibleValidators.push_back(record.validatorAddress);
        }
    }
    
    // Sort for deterministic selection
    std::sort(eligibleValidators.begin(), eligibleValidators.end());
    
    lastPoolUpdateBlock = currentBlock;
    
    LogPrintf("AutomaticValidatorManager: Pool updated - %d eligible validators\n", 
              eligibleValidators.size());
}

bool AutomaticValidatorManager::IsEligibleValidator(const uint160& address) {
    LOCK(cs_validators);
    
    // Check cache first
    auto it = validatorPool.find(address);
    if (it != validatorPool.end()) {
        // Check if cache is fresh enough
        if (chainActive.Height() - it->second.lastUpdateBlock < POOL_UPDATE_INTERVAL) {
            return it->second.isEligible;
        }
    }
    
    // Compute eligibility
    ValidatorEligibilityRecord record = ComputeEligibility(address);
    validatorPool[address] = record;
    
    // Update eligible list if needed
    if (record.isEligible) {
        auto pos = std::lower_bound(eligibleValidators.begin(), eligibleValidators.end(), address);
        if (pos == eligibleValidators.end() || *pos != address) {
            eligibleValidators.insert(pos, address);
        }
    }
    
    return record.isEligible;
}

ValidatorEligibilityRecord AutomaticValidatorManager::ComputeEligibility(const uint160& address) {
    ValidatorEligibilityRecord record;
    record.validatorAddress = address;
    record.lastUpdateBlock = chainActive.Height();
    record.lastUpdateTime = GetTime();
    
    // Verify stake requirement (on-chain)
    record.meetsStakeRequirement = VerifyStakeRequirement(
        address, record.stakeAmount, record.stakeAge);
    
    // Verify history requirement (on-chain)
    record.meetsHistoryRequirement = VerifyHistoryRequirement(
        address, record.blocksSinceFirstSeen, 
        record.transactionCount, record.uniqueInteractions);
    
    // Interaction requirement is part of history
    record.meetsInteractionRequirement = (record.uniqueInteractions >= MIN_UNIQUE_INTERACTIONS);
    
    // Final eligibility: all requirements must be met
    record.isEligible = record.meetsStakeRequirement && 
                        record.meetsHistoryRequirement && 
                        record.meetsInteractionRequirement;
    
    LogPrint(BCLog::CVM, "AutomaticValidatorManager: Computed eligibility for %s: %s "
             "(stake=%s, history=%s, interactions=%s)\n",
             address.ToString(),
             record.isEligible ? "ELIGIBLE" : "NOT ELIGIBLE",
             record.meetsStakeRequirement ? "OK" : "FAIL",
             record.meetsHistoryRequirement ? "OK" : "FAIL",
             record.meetsInteractionRequirement ? "OK" : "FAIL");
    
    return record;
}

std::vector<uint160> AutomaticValidatorManager::GetEligibleValidators() {
    LOCK(cs_validators);
    return eligibleValidators;
}

int AutomaticValidatorManager::GetEligibleValidatorCount() {
    LOCK(cs_validators);
    return eligibleValidators.size();
}

// ============================================================================
// Validator Selection (Automatic & Random)
// ============================================================================

ValidatorSelection AutomaticValidatorManager::SelectValidatorsForTask(
    const uint256& taskHash, int64_t blockHeight, int count) {
    
    ValidatorSelection selection;
    selection.taskHash = taskHash;
    selection.blockHeight = blockHeight;
    selection.targetCount = count;
    selection.timestamp = GetTime();
    
    // Create deterministic seed from task hash and block height
    CHashWriter ss(SER_GETHASH, 0);
    ss << taskHash;
    ss << blockHeight;
    
    // Include block hash for additional randomness
    if (blockHeight > 0 && blockHeight <= chainActive.Height()) {
        CBlockIndex* pindex = chainActive[blockHeight];
        if (pindex) {
            ss << pindex->GetBlockHash();
        }
    }
    
    selection.selectionSeed = ss.GetHash();
    
    // Select random validators
    selection.selectedValidators = SelectRandomValidators(selection.selectionSeed, count);
    selection.totalEligible = GetEligibleValidatorCount();
    
    // Cache selection
    CacheSelection(selection);
    
    LogPrintf("AutomaticValidatorManager: Selected %d validators for task %s at block %d\n",
              selection.selectedValidators.size(), taskHash.ToString().substr(0, 16), blockHeight);
    
    return selection;
}

std::vector<uint160> AutomaticValidatorManager::SelectRandomValidators(
    const uint256& seed, int count) {
    
    LOCK(cs_validators);
    
    std::vector<uint160> selected;
    
    if (eligibleValidators.empty()) {
        LogPrintf("AutomaticValidatorManager: No eligible validators available\n");
        return selected;
    }
    
    // Create shuffled copy using deterministic randomness
    std::vector<uint160> shuffled = eligibleValidators;
    
    // Fisher-Yates shuffle with deterministic seed
    for (size_t i = shuffled.size() - 1; i > 0; i--) {
        // Generate deterministic random index
        CHashWriter ss(SER_GETHASH, 0);
        ss << seed;
        ss << i;
        uint256 hash = ss.GetHash();
        
        size_t j = hash.GetUint64(0) % (i + 1);
        std::swap(shuffled[i], shuffled[j]);
    }
    
    // Select first 'count' validators
    int selectCount = std::min(count, (int)shuffled.size());
    for (int i = 0; i < selectCount; i++) {
        selected.push_back(shuffled[i]);
    }
    
    return selected;
}

bool AutomaticValidatorManager::WasSelectedForTask(const uint256& taskHash, const uint160& myAddress) {
    ValidatorSelection selection;
    if (!GetCachedSelection(taskHash, selection)) {
        return false;
    }
    
    for (const auto& v : selection.selectedValidators) {
        if (v == myAddress) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Validation Response Handling
// ============================================================================

void AutomaticValidatorManager::ProcessValidationResponse(const ValidationResponse& response) {
    // Verify signature
    if (!response.VerifySignature()) {
        LogPrintf("AutomaticValidatorManager: Invalid signature on response from %s\n",
                  response.validatorAddress.ToString());
        return;
    }
    
    // Verify validator was actually selected for this task
    if (!WasSelectedForTask(response.taskHash, response.validatorAddress)) {
        LogPrintf("AutomaticValidatorManager: Validator %s was not selected for task %s\n",
                  response.validatorAddress.ToString(), response.taskHash.ToString().substr(0, 16));
        return;
    }
    
    // Store response
    LOCK(cs_validators);
    pendingResponses[response.taskHash].push_back(response);
    
    LogPrint(BCLog::CVM, "AutomaticValidatorManager: Received response from %s for task %s "
             "(valid=%s, confidence=%d)\n",
             response.validatorAddress.ToString(), response.taskHash.ToString().substr(0, 16),
             response.isValid ? "true" : "false", response.confidence);
}

AggregatedValidationResult AutomaticValidatorManager::AggregateResponses(const uint256& taskHash) {
    AggregatedValidationResult result;
    result.taskHash = taskHash;
    
    ValidatorSelection selection;
    if (!GetCachedSelection(taskHash, selection)) {
        LogPrintf("AutomaticValidatorManager: No selection found for task %s\n",
                  taskHash.ToString().substr(0, 16));
        return result;
    }
    
    result.totalSelected = selection.selectedValidators.size();
    
    LOCK(cs_validators);
    auto it = pendingResponses.find(taskHash);
    if (it == pendingResponses.end() || it->second.empty()) {
        return result;
    }
    
    result.responses = it->second;
    result.totalResponded = result.responses.size();
    
    // Count votes
    double totalConfidence = 0;
    for (const auto& response : result.responses) {
        if (response.isValid) {
            result.validVotes++;
        } else {
            result.invalidVotes++;
        }
        totalConfidence += response.confidence;
    }
    
    // Determine consensus (require 2/3 majority)
    int totalVotes = result.validVotes + result.invalidVotes;
    if (totalVotes >= result.totalSelected * 0.6) {  // At least 60% responded
        result.consensusReached = true;
        result.isValid = (result.validVotes > result.invalidVotes);
        result.confidence = totalConfidence / totalVotes / 100.0;  // Normalize to 0-1
    }
    
    LogPrintf("AutomaticValidatorManager: Aggregated %d responses for task %s: "
              "valid=%d, invalid=%d, consensus=%s\n",
              result.totalResponded, taskHash.ToString().substr(0, 16),
              result.validVotes, result.invalidVotes,
              result.consensusReached ? "YES" : "NO");
    
    return result;
}

bool AutomaticValidatorManager::HasConsensus(const uint256& taskHash) {
    AggregatedValidationResult result = AggregateResponses(taskHash);
    return result.consensusReached;
}

ValidationResponse AutomaticValidatorManager::GenerateValidationResponse(
    const uint256& taskHash, bool isValid, uint8_t confidence) {
    
    ValidationResponse response;
    response.taskHash = taskHash;
    response.validatorAddress = GetMyValidatorAddress();
    response.isValid = isValid;
    response.confidence = confidence;
    response.timestamp = GetTime();
    
    // Calculate trust score from local WoT perspective
    // TODO: Integrate with TrustGraph
    response.trustScore = 50;  // Neutral for now
    
    // Sign response
    std::vector<uint8_t> privateKey;  // TODO: Get from wallet
    response.Sign(privateKey);
    
    return response;
}

// ============================================================================
// Eligibility Verification (On-Chain)
// ============================================================================

bool AutomaticValidatorManager::VerifyStakeRequirement(
    const uint160& address, CAmount& stakeAmount, int& stakeAge) {
    
    // TODO: Implement actual UTXO scanning to verify stake
    // This would:
    // 1. Scan UTXO set for outputs belonging to this address
    // 2. Sum up the total value
    // 3. Find the oldest UTXO to determine stake age
    // 4. Verify stake comes from 3+ diverse sources (anti-Sybil)
    
    // For now, placeholder implementation
    stakeAmount = 10 * COIN;
    stakeAge = MIN_STAKE_AGE;
    
    return stakeAmount >= MIN_STAKE && stakeAge >= MIN_STAKE_AGE;
}

bool AutomaticValidatorManager::VerifyHistoryRequirement(
    const uint160& address, int& blocksSinceFirstSeen,
    int& transactionCount, int& uniqueInteractions) {
    
    // TODO: Implement actual blockchain scanning
    // This would:
    // 1. Find first transaction involving this address
    // 2. Count total transactions
    // 3. Count unique addresses interacted with
    
    // For now, placeholder implementation
    blocksSinceFirstSeen = MIN_HISTORY_BLOCKS;
    transactionCount = MIN_TRANSACTIONS;
    uniqueInteractions = MIN_UNIQUE_INTERACTIONS;
    
    return blocksSinceFirstSeen >= MIN_HISTORY_BLOCKS && 
           transactionCount >= MIN_TRANSACTIONS &&
           uniqueInteractions >= MIN_UNIQUE_INTERACTIONS;
}

// ============================================================================
// Cache & Storage
// ============================================================================

void AutomaticValidatorManager::CacheSelection(const ValidatorSelection& selection) {
    LOCK(cs_validators);
    selectionCache[selection.taskHash] = selection;
    
    // Limit cache size
    if (selectionCache.size() > 10000) {
        // Remove oldest entries
        auto it = selectionCache.begin();
        while (selectionCache.size() > 8000 && it != selectionCache.end()) {
            it = selectionCache.erase(it);
        }
    }
}

bool AutomaticValidatorManager::GetCachedSelection(const uint256& taskHash, ValidatorSelection& selection) {
    LOCK(cs_validators);
    auto it = selectionCache.find(taskHash);
    if (it == selectionCache.end()) {
        return false;
    }
    selection = it->second;
    return true;
}

void AutomaticValidatorManager::StoreEligibilityRecord(const ValidatorEligibilityRecord& record) {
    if (!db) return;
    
    std::string key = "validator_" + record.validatorAddress.ToString();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    // TODO: Implement database write
    // db->Write(key, data);
    
    LogPrint(BCLog::CVM, "AutomaticValidatorManager: Stored eligibility record for %s\n",
             record.validatorAddress.ToString());
}

bool AutomaticValidatorManager::GetEligibilityRecord(const uint160& address, 
                                                      ValidatorEligibilityRecord& record) {
    LOCK(cs_validators);
    auto it = validatorPool.find(address);
    if (it == validatorPool.end()) {
        return false;
    }
    record = it->second;
    return true;
}

// ============================================================================
// Statistics
// ============================================================================

int AutomaticValidatorManager::GetTotalValidatorCount() {
    LOCK(cs_validators);
    return validatorPool.size();
}

int AutomaticValidatorManager::GetActiveValidatorCount() {
    LOCK(cs_validators);
    int active = 0;
    int64_t currentBlock = chainActive.Height();
    
    for (const auto& pair : validatorPool) {
        // Consider active if updated within last 1000 blocks
        if (currentBlock - pair.second.lastUpdateBlock < 1000) {
            active++;
        }
    }
    return active;
}

double AutomaticValidatorManager::GetAverageResponseRate() {
    LOCK(cs_validators);
    
    if (selectionCache.empty()) return 0.0;
    
    int totalSelected = 0;
    int totalResponded = 0;
    
    for (const auto& pair : selectionCache) {
        totalSelected += pair.second.selectedValidators.size();
        
        auto respIt = pendingResponses.find(pair.first);
        if (respIt != pendingResponses.end()) {
            totalResponded += respIt->second.size();
        }
    }
    
    return totalSelected > 0 ? (double)totalResponded / totalSelected : 0.0;
}

// ============================================================================
// Helper function
// ============================================================================

uint160 GetMyValidatorAddress() {
    // TODO: Integrate with wallet to get actual validator address
    return uint160();
}

// ============================================================================
// P2P Message Handlers
// ============================================================================

void ProcessValidationTaskMessage(CNode* pfrom, const uint256& taskHash, int64_t blockHeight) {
    if (!g_automaticValidatorManager) return;
    
    // Check if we were selected for this task
    uint160 myAddress = GetMyValidatorAddress();
    if (myAddress.IsNull()) return;
    
    if (g_automaticValidatorManager->WasSelectedForTask(taskHash, myAddress)) {
        // We were selected! Generate and broadcast response
        // TODO: Actually validate the task and determine isValid
        ValidationResponse response = g_automaticValidatorManager->GenerateValidationResponse(
            taskHash, true, 80);  // Placeholder: assume valid with 80% confidence
        
        // Broadcast response
        BroadcastValidationResponse(response, nullptr);
        
        LogPrintf("AutomaticValidatorManager: Responded to validation task %s\n",
                  taskHash.ToString().substr(0, 16));
    }
}

void ProcessValidationResponseMessage(CNode* pfrom, const ValidationResponse& response) {
    if (!g_automaticValidatorManager) return;
    
    g_automaticValidatorManager->ProcessValidationResponse(response);
}

// ============================================================================
// Broadcast Functions
// ============================================================================

void BroadcastValidationTask(const uint256& taskHash, int64_t blockHeight,
                             const std::vector<uint160>& selectedValidators, CConnman* connman) {
    if (!connman) return;
    
    // TODO: Implement using connman->ForEachNode
    LogPrint(BCLog::CVM, "AutomaticValidatorManager: BroadcastValidationTask not yet fully implemented\n");
}

void BroadcastValidationResponse(const ValidationResponse& response, CConnman* connman) {
    if (!connman) return;
    
    // TODO: Implement using connman->ForEachNode
    LogPrint(BCLog::CVM, "AutomaticValidatorManager: BroadcastValidationResponse not yet fully implemented\n");
}
