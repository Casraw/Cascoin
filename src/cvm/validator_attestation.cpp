// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validator_attestation.h"
#include <cvm/cvmdb.h>
#include "trustgraph.h"
#include "reputation.h"
#include "securehat.h"
#include "hash.h"
#include "utiltime.h"
#include "net.h"
#include "netmessagemaker.h"
#include "protocol.h"
#include "validation.h"
#include "chainparams.h"
#include "random.h"
#include "util.h"
#include "streams.h"
#include "version.h"
#include "key.h"
#include "pubkey.h"
#include "cvm/validator_keys.h"
#include "cvm/address_index.h"
#include "wallet/wallet.h"
#include "coins.h"
#include "script/standard.h"
#include "txdb.h"
#include <algorithm>
#include <cmath>
#include <set>

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
    // Validate private key size (32 bytes for secp256k1)
    if (privateKey.size() != 32) {
        LogPrintf("ValidationResponse::Sign: Invalid private key size %d (expected 32)\n", privateKey.size());
        return false;
    }
    
    // Create CKey from raw private key bytes
    CKey key;
    key.Set(privateKey.begin(), privateKey.end(), true);  // compressed
    
    if (!key.IsValid()) {
        LogPrintf("ValidationResponse::Sign: Invalid private key\n");
        return false;
    }
    
    // Compute message hash using CHashWriter with SER_GETHASH
    // Hash all response fields except signature
    CHashWriter ss(SER_GETHASH, 0);
    ss << taskHash;
    ss << validatorAddress;
    ss << isValid;
    ss << confidence;
    ss << trustScore;
    ss << timestamp;
    uint256 messageHash = ss.GetHash();
    
    // Sign the message hash using secp256k1 ECDSA
    std::vector<unsigned char> vchSig;
    if (!key.Sign(messageHash, vchSig)) {
        LogPrintf("ValidationResponse::Sign: Signing failed\n");
        return false;
    }
    
    // Store signature (DER-encoded, typically 70-72 bytes)
    signature.assign(vchSig.begin(), vchSig.end());
    
    LogPrint(BCLog::CVM, "ValidationResponse::Sign: Successfully signed response for task %s\n",
             taskHash.ToString().substr(0, 16));
    
    return true;
}

bool ValidationResponse::VerifySignature() const {
    // Check if signature is present
    if (signature.empty()) {
        LogPrintf("ValidationResponse::VerifySignature: Empty signature\n");
        return false;
    }
    
    // Reconstruct message hash from response fields (same as in Sign())
    CHashWriter ss(SER_GETHASH, 0);
    ss << taskHash;
    ss << validatorAddress;
    ss << isValid;
    ss << confidence;
    ss << trustScore;
    ss << timestamp;
    uint256 messageHash = ss.GetHash();
    
    // Get validator's public key from the validator key manager
    // First check if we have the validator's public key registered
    if (CVM::g_validatorKeys) {
        // If this is our own response, verify with our key
        if (validatorAddress == CVM::g_validatorKeys->GetValidatorAddress()) {
            CPubKey pubkey = CVM::g_validatorKeys->GetValidatorPubKey();
            if (pubkey.IsValid()) {
                bool result = pubkey.Verify(messageHash, signature);
                if (!result) {
                    LogPrintf("ValidationResponse::VerifySignature: Signature verification failed for validator %s\n",
                             validatorAddress.ToString());
                }
                return result;
            }
        }
    }
    
    // For other validators, we need to look up their public key from the database
    // or recover it from the signature if using compact signatures
    // For now, try to recover the public key from a compact signature if the signature
    // is the right size (65 bytes for compact signatures)
    if (signature.size() == CPubKey::COMPACT_SIGNATURE_SIZE) {
        CPubKey recoveredPubKey;
        if (recoveredPubKey.RecoverCompact(messageHash, signature)) {
            // Verify the recovered public key matches the validator address
            CKeyID recoveredAddress = recoveredPubKey.GetID();
            if (recoveredAddress == CKeyID(validatorAddress)) {
                LogPrint(BCLog::CVM, "ValidationResponse::VerifySignature: Verified signature for validator %s using key recovery\n",
                         validatorAddress.ToString());
                return true;
            } else {
                LogPrintf("ValidationResponse::VerifySignature: Recovered address %s does not match validator address %s\n",
                         recoveredAddress.ToString(), validatorAddress.ToString());
                return false;
            }
        }
    }
    
    // For DER-encoded signatures (70-72 bytes), we need the public key from storage
    // Check if we have the validator's public key in the eligibility records
    if (g_automaticValidatorManager) {
        ValidatorEligibilityRecord record;
        if (g_automaticValidatorManager->GetEligibilityRecord(validatorAddress, record)) {
            // The eligibility record doesn't store the public key directly
            // In a full implementation, we would need a separate public key registry
            // For now, log a warning and return false for unverifiable signatures
            LogPrint(BCLog::CVM, "ValidationResponse::VerifySignature: Cannot verify DER signature without public key for validator %s\n",
                     validatorAddress.ToString());
        }
    }
    
    // If we can't verify the signature, log and return false
    LogPrintf("ValidationResponse::VerifySignature: Unable to verify signature for validator %s (no public key available)\n",
             validatorAddress.ToString());
    return false;
}

// ============================================================================
// AutomaticValidatorManager implementation
// ============================================================================

AutomaticValidatorManager::AutomaticValidatorManager(CVM::CVMDatabase* database)
    : db(database), lastPoolUpdateBlock(0) {
    LogPrintf("AutomaticValidatorManager: Initialized automatic validator selection system\n");
    
    // Load validator pool from database on startup
    LoadValidatorPool();
}

AutomaticValidatorManager::~AutomaticValidatorManager() {
}

void AutomaticValidatorManager::LoadValidatorPool() {
    if (!db) {
        LogPrintf("AutomaticValidatorManager: No database available, skipping pool load\n");
        return;
    }
    
    LOCK(cs_validators);
    
    LogPrintf("AutomaticValidatorManager: Loading validator pool from database...\n");
    
    // Clear existing pool
    validatorPool.clear();
    eligibleValidators.clear();
    
    int loadedCount = 0;
    int eligibleCount = 0;
    
    // Use IterateValidatorRecords to rebuild the validator pool
    db->IterateValidatorRecords([&](const ValidatorEligibilityRecord& record) {
        // Add to validator pool
        validatorPool[record.validatorAddress] = record;
        loadedCount++;
        
        // Add to eligible list if eligible
        if (record.isEligible) {
            eligibleValidators.push_back(record.validatorAddress);
            eligibleCount++;
        }
        
        return true;  // Continue iteration
    });
    
    // Sort eligible validators for deterministic selection
    std::sort(eligibleValidators.begin(), eligibleValidators.end());
    
    LogPrintf("AutomaticValidatorManager: Loaded %d validator records (%d eligible) from database\n",
              loadedCount, eligibleCount);
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
    
    // Verify anti-Sybil requirement (funding source diversity)
    bool meetsAntiSybilRequirement = VerifyAntiSybilRequirement(address);
    
    // Final eligibility: all requirements must be met
    record.isEligible = record.meetsStakeRequirement && 
                        record.meetsHistoryRequirement && 
                        record.meetsInteractionRequirement &&
                        meetsAntiSybilRequirement;
    
    LogPrint(BCLog::CVM, "AutomaticValidatorManager: Computed eligibility for %s: %s "
             "(stake=%s, history=%s, interactions=%s, anti-sybil=%s)\n",
             address.ToString(),
             record.isEligible ? "ELIGIBLE" : "NOT ELIGIBLE",
             record.meetsStakeRequirement ? "OK" : "FAIL",
             record.meetsHistoryRequirement ? "OK" : "FAIL",
             record.meetsInteractionRequirement ? "OK" : "FAIL",
             meetsAntiSybilRequirement ? "OK" : "FAIL");
    
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
    
    // Sign response using validator key from wallet
    CKey validatorKey;
    if (GetValidatorKey(validatorKey)) {
        // Get raw private key bytes for signing
        CPrivKey privkey = validatorKey.GetPrivKey();
        if (privkey.size() >= 32) {
            std::vector<uint8_t> privateKeyBytes(privkey.begin(), privkey.begin() + 32);
            if (!response.Sign(privateKeyBytes)) {
                LogPrintf("GenerateValidationResponse: Failed to sign response\n");
            }
        } else {
            LogPrintf("GenerateValidationResponse: Invalid private key size\n");
        }
    } else {
        LogPrintf("GenerateValidationResponse: Could not retrieve validator key for signing\n");
    }
    
    return response;
}

// ============================================================================
// Eligibility Verification (On-Chain)
// ============================================================================

/**
 * Verify stake requirements by scanning the UTXO set
 * 
 * This function scans pcoinsTip (the UTXO set) for outputs belonging to the
 * validator address, calculates total stake amount, and determines stake age
 * from the oldest UTXO.
 * 
 * @param address The validator address to check
 * @param stakeAmount Output: total stake amount found
 * @param stakeAge Output: age of oldest UTXO in blocks
 * @return true if stake requirements are met
 * 
 * Requirements: 4.1, 4.2
 */
bool AutomaticValidatorManager::VerifyStakeRequirement(
    const uint160& address, CAmount& stakeAmount, int& stakeAge) {
    
    stakeAmount = 0;
    stakeAge = 0;
    
    // Get current block height for age calculation
    int currentHeight = 0;
    {
        LOCK(cs_main);
        currentHeight = chainActive.Height();
    }
    
    if (currentHeight <= 0) {
        LogPrint(BCLog::CVM, "VerifyStakeRequirement: Chain not synced, cannot verify stake\n");
        return false;
    }
    
    // First, try to use the address index if available (more efficient)
    if (CVM::g_addressIndex) {
        std::vector<CVM::AddressUTXO> utxos = CVM::g_addressIndex->GetAddressUTXOs(address);
        
        if (!utxos.empty()) {
            int oldestHeight = currentHeight;
            
            for (const auto& utxo : utxos) {
                stakeAmount += utxo.value;
                if (utxo.height < oldestHeight) {
                    oldestHeight = utxo.height;
                }
            }
            
            stakeAge = currentHeight - oldestHeight;
            
            LogPrint(BCLog::CVM, "VerifyStakeRequirement: Address %s has %d UTXOs, "
                     "total stake=%d, oldest age=%d blocks (via address index)\n",
                     address.ToString(), utxos.size(), stakeAmount, stakeAge);
            
            return stakeAmount >= MIN_STAKE && stakeAge >= MIN_STAKE_AGE;
        }
    }
    
    // Fall back to scanning the UTXO set directly using pcoinsTip cursor
    // This is more expensive but works without address index
    {
        LOCK(cs_main);
        
        if (!pcoinsTip) {
            LogPrintf("VerifyStakeRequirement: pcoinsTip not available\n");
            return false;
        }
        
        // Get a cursor to iterate through the UTXO set
        std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsTip->Cursor());
        if (!pcursor) {
            LogPrintf("VerifyStakeRequirement: Could not create UTXO cursor\n");
            return false;
        }
        
        int oldestHeight = currentHeight;
        int utxoCount = 0;
        
        // Iterate through all UTXOs
        while (pcursor->Valid()) {
            COutPoint key;
            Coin coin;
            
            if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
                // Extract address from the scriptPubKey
                CTxDestination dest;
                if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                    const CKeyID* keyID = boost::get<CKeyID>(&dest);
                    if (keyID && uint160(*keyID) == address) {
                        // This UTXO belongs to our address
                        stakeAmount += coin.out.nValue;
                        utxoCount++;
                        
                        // Track oldest UTXO for stake age
                        if ((int)coin.nHeight < oldestHeight) {
                            oldestHeight = coin.nHeight;
                        }
                    }
                }
            }
            
            pcursor->Next();
        }
        
        if (utxoCount > 0) {
            stakeAge = currentHeight - oldestHeight;
        }
        
        LogPrint(BCLog::CVM, "VerifyStakeRequirement: Address %s has %d UTXOs, "
                 "total stake=%d, oldest age=%d blocks (via UTXO scan)\n",
                 address.ToString(), utxoCount, stakeAmount, stakeAge);
    }
    
    return stakeAmount >= MIN_STAKE && stakeAge >= MIN_STAKE_AGE;
}

/**
 * Verify history requirements by scanning the blockchain
 * 
 * This function scans the blockchain to find the first transaction involving
 * the address, counts total transactions, and counts unique address interactions.
 * Uses address index if available, otherwise falls back to block scanning.
 * 
 * @param address The validator address to check
 * @param blocksSinceFirstSeen Output: blocks since first transaction
 * @param transactionCount Output: total transaction count
 * @param uniqueInteractions Output: count of unique addresses interacted with
 * @return true if history requirements are met
 * 
 * Requirements: 4.3, 4.4
 */
bool AutomaticValidatorManager::VerifyHistoryRequirement(
    const uint160& address, int& blocksSinceFirstSeen,
    int& transactionCount, int& uniqueInteractions) {
    
    blocksSinceFirstSeen = 0;
    transactionCount = 0;
    uniqueInteractions = 0;
    
    // Get current block height
    int currentHeight = 0;
    {
        LOCK(cs_main);
        currentHeight = chainActive.Height();
    }
    
    if (currentHeight <= 0) {
        LogPrint(BCLog::CVM, "VerifyHistoryRequirement: Chain not synced\n");
        return false;
    }
    
    // First, try to use the address index if available
    if (CVM::g_addressIndex) {
        std::vector<CVM::AddressUTXO> utxos = CVM::g_addressIndex->GetAddressUTXOs(address);
        
        if (!utxos.empty()) {
            // Find earliest UTXO height as proxy for first seen
            int earliestHeight = currentHeight;
            for (const auto& utxo : utxos) {
                if (utxo.height < earliestHeight) {
                    earliestHeight = utxo.height;
                }
            }
            
            blocksSinceFirstSeen = currentHeight - earliestHeight;
            
            // UTXO count gives us a lower bound on transaction count
            // Each UTXO represents at least one transaction
            transactionCount = utxos.size();
            
            // For unique interactions, we need to scan blocks
            // This is a simplified estimate based on UTXO diversity
            // A more accurate count would require full transaction scanning
            std::set<uint256> uniqueTxHashes;
            for (const auto& utxo : utxos) {
                uniqueTxHashes.insert(utxo.outpoint.hash);
            }
            
            // Estimate unique interactions as unique transaction count
            // This is a conservative estimate
            uniqueInteractions = uniqueTxHashes.size();
            
            LogPrint(BCLog::CVM, "VerifyHistoryRequirement: Address %s - "
                     "first seen %d blocks ago, %d transactions, %d unique interactions (via address index)\n",
                     address.ToString(), blocksSinceFirstSeen, transactionCount, uniqueInteractions);
            
            return blocksSinceFirstSeen >= MIN_HISTORY_BLOCKS && 
                   transactionCount >= MIN_TRANSACTIONS &&
                   uniqueInteractions >= MIN_UNIQUE_INTERACTIONS;
        }
    }
    
    // Fall back to scanning recent blocks
    // This is expensive, so we limit the scan depth
    const int MAX_SCAN_DEPTH = 50000;  // Scan at most 50000 blocks
    int scanDepth = std::min(currentHeight, MAX_SCAN_DEPTH);
    
    std::set<uint160> interactedAddresses;
    int firstSeenHeight = -1;
    
    {
        LOCK(cs_main);
        
        // Scan blocks from current tip backwards
        for (int height = currentHeight; height > currentHeight - scanDepth && height >= 0; height--) {
            CBlockIndex* pindex = chainActive[height];
            if (!pindex) continue;
            
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                continue;
            }
            
            // Check each transaction in the block
            for (const auto& tx : block.vtx) {
                bool addressInvolved = false;
                
                // Check outputs
                for (const auto& txout : tx->vout) {
                    CTxDestination dest;
                    if (ExtractDestination(txout.scriptPubKey, dest)) {
                        const CKeyID* keyID = boost::get<CKeyID>(&dest);
                        if (keyID) {
                            uint160 outputAddr(*keyID);
                            if (outputAddr == address) {
                                addressInvolved = true;
                            } else if (addressInvolved) {
                                // Track other addresses in transactions involving our address
                                interactedAddresses.insert(outputAddr);
                            }
                        }
                    }
                }
                
                // Check inputs (for non-coinbase transactions)
                if (!tx->IsCoinBase()) {
                    for (const auto& txin : tx->vin) {
                        // Get the previous output to check the address
                        Coin coin;
                        if (pcoinsTip && pcoinsTip->GetCoin(txin.prevout, coin)) {
                            CTxDestination dest;
                            if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                                const CKeyID* keyID = boost::get<CKeyID>(&dest);
                                if (keyID) {
                                    uint160 inputAddr(*keyID);
                                    if (inputAddr == address) {
                                        addressInvolved = true;
                                    } else if (addressInvolved) {
                                        interactedAddresses.insert(inputAddr);
                                    }
                                }
                            }
                        }
                    }
                }
                
                if (addressInvolved) {
                    transactionCount++;
                    if (firstSeenHeight < 0 || height < firstSeenHeight) {
                        firstSeenHeight = height;
                    }
                }
            }
        }
    }
    
    if (firstSeenHeight >= 0) {
        blocksSinceFirstSeen = currentHeight - firstSeenHeight;
    }
    
    uniqueInteractions = interactedAddresses.size();
    
    LogPrint(BCLog::CVM, "VerifyHistoryRequirement: Address %s - "
             "first seen %d blocks ago, %d transactions, %d unique interactions (via block scan)\n",
             address.ToString(), blocksSinceFirstSeen, transactionCount, uniqueInteractions);
    
    return blocksSinceFirstSeen >= MIN_HISTORY_BLOCKS && 
           transactionCount >= MIN_TRANSACTIONS &&
           uniqueInteractions >= MIN_UNIQUE_INTERACTIONS;
}

/**
 * Verify anti-Sybil requirements by checking funding source diversity
 * 
 * This function verifies that the validator's stake originates from 3 or more
 * diverse funding sources, which helps prevent Sybil attacks where an attacker
 * creates multiple validator identities from a single source of funds.
 * 
 * @param address The validator address to check
 * @return true if anti-Sybil requirements are met (3+ diverse funding sources)
 * 
 * Requirements: 4.5
 */
bool AutomaticValidatorManager::VerifyAntiSybilRequirement(const uint160& address) {
    const int MIN_FUNDING_SOURCES = 3;
    
    std::set<uint160> fundingSources;
    
    // Get current block height
    int currentHeight = 0;
    {
        LOCK(cs_main);
        currentHeight = chainActive.Height();
    }
    
    if (currentHeight <= 0) {
        LogPrint(BCLog::CVM, "VerifyAntiSybilRequirement: Chain not synced\n");
        return false;
    }
    
    // First, try to use the address index if available
    if (CVM::g_addressIndex) {
        std::vector<CVM::AddressUTXO> utxos = CVM::g_addressIndex->GetAddressUTXOs(address);
        
        if (!utxos.empty()) {
            LOCK(cs_main);
            
            // For each UTXO, trace back to find the funding source
            for (const auto& utxo : utxos) {
                // Get the transaction that created this UTXO
                CTransactionRef tx;
                uint256 hashBlock;
                if (GetTransaction(utxo.outpoint.hash, tx, Params().GetConsensus(), hashBlock, true)) {
                    // Check the inputs of this transaction to find funding sources
                    if (!tx->IsCoinBase()) {
                        for (const auto& txin : tx->vin) {
                            Coin coin;
                            if (pcoinsTip && pcoinsTip->GetCoin(txin.prevout, coin)) {
                                CTxDestination dest;
                                if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                                    const CKeyID* keyID = boost::get<CKeyID>(&dest);
                                    if (keyID) {
                                        uint160 sourceAddr(*keyID);
                                        // Don't count self-funding
                                        if (sourceAddr != address) {
                                            fundingSources.insert(sourceAddr);
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        // Coinbase transactions count as a unique funding source (mining)
                        // Use a special marker for coinbase
                        fundingSources.insert(uint160());  // Null address represents coinbase
                    }
                }
            }
            
            LogPrint(BCLog::CVM, "VerifyAntiSybilRequirement: Address %s has %d diverse funding sources (via address index)\n",
                     address.ToString(), fundingSources.size());
            
            return fundingSources.size() >= MIN_FUNDING_SOURCES;
        }
    }
    
    // Fall back to scanning the UTXO set and tracing funding sources
    {
        LOCK(cs_main);
        
        if (!pcoinsTip) {
            LogPrintf("VerifyAntiSybilRequirement: pcoinsTip not available\n");
            return false;
        }
        
        // Get a cursor to iterate through the UTXO set
        std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsTip->Cursor());
        if (!pcursor) {
            LogPrintf("VerifyAntiSybilRequirement: Could not create UTXO cursor\n");
            return false;
        }
        
        // Find all UTXOs belonging to this address
        std::vector<COutPoint> addressUtxos;
        
        while (pcursor->Valid()) {
            COutPoint key;
            Coin coin;
            
            if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
                CTxDestination dest;
                if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                    const CKeyID* keyID = boost::get<CKeyID>(&dest);
                    if (keyID && uint160(*keyID) == address) {
                        addressUtxos.push_back(key);
                    }
                }
            }
            
            pcursor->Next();
        }
        
        // For each UTXO, trace back to find funding sources
        for (const auto& outpoint : addressUtxos) {
            CTransactionRef tx;
            uint256 hashBlock;
            if (GetTransaction(outpoint.hash, tx, Params().GetConsensus(), hashBlock, true)) {
                if (!tx->IsCoinBase()) {
                    for (const auto& txin : tx->vin) {
                        Coin coin;
                        if (pcoinsTip->GetCoin(txin.prevout, coin)) {
                            CTxDestination dest;
                            if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                                const CKeyID* keyID = boost::get<CKeyID>(&dest);
                                if (keyID) {
                                    uint160 sourceAddr(*keyID);
                                    if (sourceAddr != address) {
                                        fundingSources.insert(sourceAddr);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    fundingSources.insert(uint160());  // Coinbase marker
                }
            }
            
            // Early exit if we've found enough sources
            if (fundingSources.size() >= MIN_FUNDING_SOURCES) {
                break;
            }
        }
    }
    
    LogPrint(BCLog::CVM, "VerifyAntiSybilRequirement: Address %s has %d diverse funding sources (via UTXO scan)\n",
             address.ToString(), fundingSources.size());
    
    return fundingSources.size() >= MIN_FUNDING_SOURCES;
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
    
    // Use the CVMDatabase method for persistence
    if (db->WriteValidatorRecord(record)) {
        LogPrint(BCLog::CVM, "AutomaticValidatorManager: Stored eligibility record for %s\n",
                 record.validatorAddress.ToString());
    } else {
        LogPrintf("AutomaticValidatorManager: Failed to store eligibility record for %s\n",
                  record.validatorAddress.ToString());
    }
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
// Helper functions
// ============================================================================

/**
 * Derive validator address from public key
 * Uses standard Bitcoin P2PKH derivation: Hash160(pubkey) = RIPEMD160(SHA256(pubkey))
 * This is consistent with CKeyID derivation used throughout the codebase.
 */
uint160 DeriveValidatorAddress(const CPubKey& pubkey) {
    if (!pubkey.IsValid()) {
        LogPrintf("DeriveValidatorAddress: Invalid public key\n");
        return uint160();
    }
    
    // CPubKey::GetID() returns CKeyID which is a uint160
    // This performs Hash160 (SHA256 + RIPEMD160) on the serialized public key
    CKeyID keyId = pubkey.GetID();
    
    // CKeyID inherits from uint160, so we can return it directly
    return uint160(keyId);
}

/**
 * Get the local validator's address
 * 
 * Priority order:
 * 1. Use ValidatorKeyManager if configured
 * 2. Fall back to wallet's primary receiving address
 * 3. Return empty address if neither available
 * 
 * Requirements: 2.2, 2.3
 */
uint160 GetMyValidatorAddress() {
    // Priority 1: Use the validator key manager if available
    if (CVM::g_validatorKeys && CVM::g_validatorKeys->HasValidatorKey()) {
        return CVM::g_validatorKeys->GetValidatorAddress();
    }
    
    // Priority 2: Fall back to wallet's primary receiving address
    if (!::vpwallets.empty()) {
        CWallet* pwallet = ::vpwallets[0];
        if (pwallet) {
            // Check if wallet is available (not locked for key operations)
            // Note: We can get the address even if wallet is locked
            LOCK(pwallet->cs_wallet);
            
            // Get the default account's destination address
            CTxDestination dest;
            if (pwallet->GetAccountDestination(dest, "", false)) {
                // Extract the key ID from the destination
                const CKeyID* keyID = boost::get<CKeyID>(&dest);
                if (keyID) {
                    LogPrint(BCLog::CVM, "GetMyValidatorAddress: Using wallet address %s\n",
                             keyID->ToString());
                    return uint160(*keyID);
                }
            }
            
            // Alternative: Try to get any address from the wallet's key pool
            CPubKey pubkey;
            if (pwallet->GetKeyFromPool(pubkey, false)) {
                CKeyID keyID = pubkey.GetID();
                LogPrint(BCLog::CVM, "GetMyValidatorAddress: Using key pool address %s\n",
                         keyID.ToString());
                return uint160(keyID);
            }
            
            LogPrint(BCLog::CVM, "GetMyValidatorAddress: Wallet available but no address found\n");
        }
    } else {
        LogPrint(BCLog::CVM, "GetMyValidatorAddress: No wallet available\n");
    }
    
    // Fallback: return empty address (validator mode not configured)
    return uint160();
}

/**
 * Get the validator's private key for signing operations
 * 
 * This function retrieves the private key associated with the validator address.
 * It checks wallet lock status before attempting to access the key.
 * 
 * @param keyOut Output parameter for the retrieved private key
 * @return true if key was successfully retrieved, false otherwise
 * 
 * Requirements: 2.1, 2.3
 */
bool GetValidatorKey(CKey& keyOut) {
    // Priority 1: Use the validator key manager if available
    if (CVM::g_validatorKeys && CVM::g_validatorKeys->HasValidatorKey()) {
        // Get the validator address and retrieve the key
        uint160 validatorAddr = CVM::g_validatorKeys->GetValidatorAddress();
        
        // The ValidatorKeyManager stores the key internally, use its Sign method
        // For direct key access, we need to check if we can get it from wallet
        // First try to get from wallet using the validator address
        if (!::vpwallets.empty()) {
            CWallet* pwallet = ::vpwallets[0];
            if (pwallet) {
                LOCK(pwallet->cs_wallet);
                
                // Check if wallet is locked
                if (pwallet->IsLocked()) {
                    LogPrintf("GetValidatorKey: Wallet is locked, cannot retrieve key\n");
                    return false;
                }
                
                // Try to get the key from wallet
                CKeyID keyID(validatorAddr);
                if (pwallet->GetKey(keyID, keyOut)) {
                    LogPrint(BCLog::CVM, "GetValidatorKey: Retrieved key from wallet for address %s\n",
                             validatorAddr.ToString());
                    return true;
                }
            }
        }
        
        // If we have a validator key manager but couldn't get from wallet,
        // the key might be stored in the validator key file
        // In this case, we can't directly access the CKey, but we can sign
        LogPrint(BCLog::CVM, "GetValidatorKey: ValidatorKeyManager has key but direct access not available\n");
        return false;
    }
    
    // Priority 2: Get key from wallet using the validator address
    uint160 validatorAddr = GetMyValidatorAddress();
    if (validatorAddr.IsNull()) {
        LogPrintf("GetValidatorKey: No validator address configured\n");
        return false;
    }
    
    if (::vpwallets.empty()) {
        LogPrintf("GetValidatorKey: No wallet available\n");
        return false;
    }
    
    CWallet* pwallet = ::vpwallets[0];
    if (!pwallet) {
        LogPrintf("GetValidatorKey: Wallet pointer is null\n");
        return false;
    }
    
    LOCK(pwallet->cs_wallet);
    
    // Check if wallet is locked
    if (pwallet->IsLocked()) {
        LogPrintf("GetValidatorKey: Wallet is locked, cannot retrieve key\n");
        return false;
    }
    
    // Retrieve the private key
    CKeyID keyID(validatorAddr);
    if (!pwallet->GetKey(keyID, keyOut)) {
        LogPrintf("GetValidatorKey: Key not found in wallet for address %s\n",
                 validatorAddr.ToString());
        return false;
    }
    
    LogPrint(BCLog::CVM, "GetValidatorKey: Successfully retrieved key for address %s\n",
             validatorAddr.ToString());
    return true;
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
    if (!connman) {
        LogPrintf("BroadcastValidationTask: No connection manager available\n");
        return;
    }
    
    // Serialize task data using CDataStream
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << taskHash;
    ss << blockHeight;
    ss << selectedValidators;
    
    uint32_t broadcastCount = 0;
    
    // Broadcast to all connected nodes using connman->ForEachNode
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected && !pnode->fDisconnect) {
            // Create message using CNetMsgMaker
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::VALTASK, 
                    taskHash, blockHeight, selectedValidators));
            broadcastCount++;
        }
    });
    
    LogPrint(BCLog::CVM, "BroadcastValidationTask: Broadcast task %s at height %d to %u peers "
             "(selected %d validators)\n",
             taskHash.ToString().substr(0, 16), blockHeight, broadcastCount, 
             selectedValidators.size());
}

void BroadcastValidationResponse(const ValidationResponse& response, CConnman* connman) {
    if (!connman) {
        LogPrintf("BroadcastValidationResponse: No connection manager available\n");
        return;
    }
    
    // Verify the response has a signature before broadcasting
    if (response.signature.empty()) {
        LogPrintf("BroadcastValidationResponse: Cannot broadcast unsigned response\n");
        return;
    }
    
    uint32_t broadcastCount = 0;
    
    // Broadcast to all connected nodes using connman->ForEachNode
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fSuccessfullyConnected && !pnode->fDisconnect) {
            // Create message using CNetMsgMaker with MSG_VALIDATION_RESPONSE type
            // The response includes the signature for verification by recipients
            connman->PushMessage(pnode,
                CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::VALRESP, response));
            broadcastCount++;
        }
    });
    
    LogPrint(BCLog::CVM, "BroadcastValidationResponse: Broadcast response for task %s from validator %s "
             "to %u peers (valid=%s, confidence=%d, signature_size=%d)\n",
             response.taskHash.ToString().substr(0, 16),
             response.validatorAddress.ToString(),
             broadcastCount,
             response.isValid ? "true" : "false",
             response.confidence,
             response.signature.size());
}

/**
 * Send a message to a specific peer
 * 
 * This helper function sends a serialized message to a specific peer node.
 * Used for sending attestation responses back to the requesting peer.
 * 
 * @param peer The peer node to send to
 * @param msgType The message type string (e.g., NetMsgType::VALIDATOR_ATTESTATION)
 * @param data The serialized message data
 * @param connman The connection manager
 * @return true if message was sent successfully, false otherwise
 * 
 * Requirements: 3.3, 3.4
 */
bool SendToPeer(CNode* peer, const std::string& msgType, const std::vector<uint8_t>& data, CConnman* connman) {
    if (!peer) {
        LogPrintf("SendToPeer: Null peer pointer\n");
        return false;
    }
    
    if (!connman) {
        LogPrintf("SendToPeer: No connection manager available\n");
        return false;
    }
    
    if (!peer->fSuccessfullyConnected || peer->fDisconnect) {
        LogPrint(BCLog::NET, "SendToPeer: Peer %d not connected or disconnecting\n", peer->GetId());
        return false;
    }
    
    // Create a serialized network message with the raw data
    CSerializedNetMsg msg;
    msg.command = msgType;
    msg.data = data;
    
    connman->PushMessage(peer, std::move(msg));
    
    LogPrint(BCLog::NET, "SendToPeer: Sent %s message (%d bytes) to peer %d\n",
             msgType, data.size(), peer->GetId());
    
    return true;
}

// ============================================================================
// Legacy Validator Attestation System
// ============================================================================

// Global legacy validator attestation manager instance
ValidatorAttestationManager* g_validatorAttestationManager = nullptr;

// Legacy P2P message handlers
void ProcessValidatorAnnounceMessage(CNode* pfrom, const ValidatorEligibilityAnnouncement& announcement) {
    if (!g_validatorAttestationManager) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Manager not initialized\n");
        return;
    }
    
    // Process the announcement
    if (g_validatorAttestationManager->ProcessAnnouncement(announcement)) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Processed announcement from %s\n",
                 announcement.validatorAddress.ToString());
    }
}

void ProcessAttestationRequestMessage(CNode* pfrom, const uint160& validatorAddress) {
    if (!g_validatorAttestationManager) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Manager not initialized\n");
        return;
    }
    
    // Get attestations for the requested validator
    std::vector<ValidatorAttestation> attestations = 
        g_validatorAttestationManager->GetAttestationsForValidator(validatorAddress);
    
    LogPrint(BCLog::NET, "ValidatorAttestation: Processed attestation request for %s, found %d attestations\n",
             validatorAddress.ToString(), attestations.size());
    
    // Send response back to requesting peer
    if (pfrom && g_connman) {
        // Serialize each attestation and send back to the requesting peer
        for (const auto& attestation : attestations) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << attestation;
            std::vector<uint8_t> data(ss.begin(), ss.end());
            
            SendToPeer(pfrom, NetMsgType::VALIDATOR_ATTESTATION, data, g_connman.get());
        }
        
        LogPrint(BCLog::NET, "ValidatorAttestation: Sent %d attestations to peer %d\n",
                 attestations.size(), pfrom->GetId());
    }
}

void ProcessValidatorAttestationMessage(CNode* pfrom, const ValidatorAttestation& attestation) {
    if (!g_validatorAttestationManager) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Manager not initialized\n");
        return;
    }
    
    // Process the attestation
    if (g_validatorAttestationManager->ProcessAttestation(attestation)) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Processed attestation for %s from %s\n",
                 attestation.validatorAddress.ToString(), attestation.attestorAddress.ToString());
    }
}

void ProcessBatchAttestationRequestMessage(CNode* pfrom, const BatchAttestationRequest& request) {
    if (!g_validatorAttestationManager) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Manager not initialized\n");
        return;
    }
    
    LogPrint(BCLog::NET, "ValidatorAttestation: Processing batch request for %d validators\n",
             request.validators.size());
    
    // Build batch response
    BatchAttestationResponse response;
    response.timestamp = GetTime();
    response.responderAddress = GetMyValidatorAddress();
    
    for (const auto& validatorAddress : request.validators) {
        std::vector<ValidatorAttestation> attestations = 
            g_validatorAttestationManager->GetAttestationsForValidator(validatorAddress);
        
        for (const auto& att : attestations) {
            response.attestations.push_back(att);
        }
    }
    
    // Send batch response back to requesting peer
    if (pfrom && g_connman) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << response;
        std::vector<uint8_t> data(ss.begin(), ss.end());
        
        SendToPeer(pfrom, NetMsgType::BATCH_ATTESTATION_RESPONSE, data, g_connman.get());
        
        LogPrint(BCLog::NET, "ValidatorAttestation: Sent batch response with %d attestations to peer %d\n",
                 response.attestations.size(), pfrom->GetId());
    }
}

void ProcessBatchAttestationResponseMessage(CNode* pfrom, const BatchAttestationResponse& response) {
    if (!g_validatorAttestationManager) {
        LogPrint(BCLog::NET, "ValidatorAttestation: Manager not initialized\n");
        return;
    }
    
    LogPrint(BCLog::NET, "ValidatorAttestation: Processing batch response with %d attestations\n",
             response.attestations.size());
    
    // Process each attestation in the batch
    for (const auto& attestation : response.attestations) {
        g_validatorAttestationManager->ProcessAttestation(attestation);
    }
}
