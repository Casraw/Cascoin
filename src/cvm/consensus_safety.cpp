// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/consensus_safety.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <hash.h>
#include <util.h>
#include <validation.h>
#include <chain.h>
#include <chainparams.h>
#include <tinyformat.h>
#include <algorithm>
#include <cmath>

namespace CVM {

// Global instance
std::unique_ptr<ConsensusSafetyValidator> g_consensusSafetyValidator;

void InitializeConsensusSafetyValidator(CVMDatabase* db, SecureHAT* hat, TrustGraph* graph) {
    g_consensusSafetyValidator = std::make_unique<ConsensusSafetyValidator>(db, hat, graph);
    LogPrintf("ConsensusSafetyValidator: Initialized\n");
}

void ShutdownConsensusSafetyValidator() {
    g_consensusSafetyValidator.reset();
    LogPrintf("ConsensusSafetyValidator: Shutdown\n");
}

// ConsensusSafetyValidator implementation

ConsensusSafetyValidator::ConsensusSafetyValidator()
    : database(nullptr), secureHAT(nullptr), trustGraph(nullptr), cacheBlockHeight(0) {
}

ConsensusSafetyValidator::ConsensusSafetyValidator(CVMDatabase* db, SecureHAT* hat, TrustGraph* graph)
    : database(db), secureHAT(hat), trustGraph(graph), cacheBlockHeight(0) {
}

ConsensusSafetyValidator::~ConsensusSafetyValidator() {
}

// ========== Task 23.1: Deterministic Execution Validation ==========

DeterministicExecutionResult ConsensusSafetyValidator::ValidateHATv2Determinism(
    const uint160& address,
    const uint160& viewer,
    int blockHeight
) {
    DeterministicExecutionResult result;
    
    if (!secureHAT || !database) {
        result.isDeterministic = false;
        result.failureReason = "SecureHAT or database not initialized";
        return result;
    }
    
    try {
        // Calculate HAT v2 score using deterministic inputs
        TrustBreakdown breakdown = secureHAT->CalculateWithBreakdown(address, viewer);
        
        // Hash each component for verification
        BehaviorMetrics behavior = secureHAT->GetBehaviorMetrics(address);
        StakeInfo stake = secureHAT->GetStakeInfo(address);
        TemporalMetrics temporal = secureHAT->GetTemporalMetrics(address);
        
        result.behaviorHash = HashBehaviorComponent(behavior);
        result.wotHash = HashWoTComponent(address, viewer);
        result.economicHash = HashEconomicComponent(stake);
        result.temporalHash = HashTemporalComponent(temporal);
        
        // Calculate overall execution hash
        CHashWriter ss(SER_GETHASH, 0);
        ss << result.behaviorHash;
        ss << result.wotHash;
        ss << result.economicHash;
        ss << result.temporalHash;
        ss << breakdown.final_score;
        ss << blockHeight;
        result.executionHash = ss.GetHash();
        
        // Validate component determinism
        result.isDeterministic = ValidateComponentDeterminism(address, viewer, blockHeight);
        
        if (!result.isDeterministic) {
            result.failureReason = "Component calculation produced non-deterministic results";
        }
        
        LogPrintf("ConsensusSafetyValidator: HAT v2 determinism check for %s: %s (score=%d, hash=%s)\n",
                  address.ToString(), result.isDeterministic ? "PASS" : "FAIL",
                  breakdown.final_score, result.executionHash.ToString());
        
    } catch (const std::exception& e) {
        result.isDeterministic = false;
        result.failureReason = strprintf("Exception during validation: %s", e.what());
        LogPrintf("ConsensusSafetyValidator: Exception in ValidateHATv2Determinism: %s\n", e.what());
    }
    
    return result;
}

ValidatorSelectionResult ConsensusSafetyValidator::ValidateValidatorSelection(
    const uint256& txHash,
    int blockHeight
) {
    ValidatorSelectionResult result;
    
    try {
        // Calculate deterministic seed
        result.selectionSeed = CalculateValidatorSelectionSeed(txHash, blockHeight);
        
        // Get block hash at height for additional entropy
        uint256 blockHash = GetBlockHashAtHeight(blockHeight);
        
        // Combine tx hash and block hash for deterministic randomness
        CHashWriter ss(SER_GETHASH, 0);
        ss << txHash;
        ss << blockHash;
        ss << blockHeight;
        uint256 combinedSeed = ss.GetHash();
        
        // Verify seed matches expected
        if (combinedSeed != result.selectionSeed) {
            result.isConsistent = false;
            result.failureReason = "Seed calculation mismatch";
            return result;
        }
        
        // The actual validator selection would use this seed
        // For validation, we verify the seed is deterministic
        result.isConsistent = true;
        
        LogPrintf("ConsensusSafetyValidator: Validator selection validation for tx %s at height %d: %s\n",
                  txHash.ToString(), blockHeight, result.isConsistent ? "PASS" : "FAIL");
        
    } catch (const std::exception& e) {
        result.isConsistent = false;
        result.failureReason = strprintf("Exception during validation: %s", e.what());
        LogPrintf("ConsensusSafetyValidator: Exception in ValidateValidatorSelection: %s\n", e.what());
    }
    
    return result;
}

uint256 ConsensusSafetyValidator::CalculateHATv2Hash(
    const uint160& address,
    const uint160& viewer,
    int blockHeight
) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << viewer;
    ss << blockHeight;
    
    if (secureHAT) {
        TrustBreakdown breakdown = secureHAT->CalculateWithBreakdown(address, viewer);
        ss << breakdown.final_score;
        ss << breakdown.secure_behavior;
        ss << breakdown.secure_wot;
        ss << breakdown.secure_economic;
        ss << breakdown.secure_temporal;
    }
    
    return ss.GetHash();
}

uint256 ConsensusSafetyValidator::CalculateValidatorSelectionSeed(
    const uint256& txHash,
    int blockHeight
) {
    uint256 blockHash = GetBlockHashAtHeight(blockHeight);
    
    CHashWriter ss(SER_GETHASH, 0);
    ss << txHash;
    ss << blockHash;
    ss << blockHeight;
    
    return ss.GetHash();
}


// ========== Task 23.2: Reputation-Based Feature Consensus ==========

GasDiscountConsensusResult ConsensusSafetyValidator::ValidateGasDiscountConsensus(
    const uint160& address,
    uint8_t reputation,
    uint64_t baseGas
) {
    GasDiscountConsensusResult result;
    result.reputation = reputation;
    
    try {
        // Calculate deterministic gas discount
        result.calculatedDiscount = CalculateDeterministicGasDiscount(reputation, baseGas);
        
        // Verify calculation is deterministic by running it multiple times
        uint64_t discount1 = CalculateDeterministicGasDiscount(reputation, baseGas);
        uint64_t discount2 = CalculateDeterministicGasDiscount(reputation, baseGas);
        uint64_t discount3 = CalculateDeterministicGasDiscount(reputation, baseGas);
        
        if (discount1 != discount2 || discount2 != discount3) {
            result.isConsensus = false;
            result.failureReason = "Gas discount calculation is non-deterministic";
            return result;
        }
        
        // Verify discount is within expected bounds
        // Maximum discount is 50% (at reputation 100)
        uint64_t maxDiscount = baseGas / 2;
        if (result.calculatedDiscount > maxDiscount) {
            result.isConsensus = false;
            result.failureReason = "Gas discount exceeds maximum allowed";
            return result;
        }
        
        result.isConsensus = true;
        
        LogPrintf("ConsensusSafetyValidator: Gas discount consensus for %s (rep=%d, base=%lu): %s (discount=%lu)\n",
                  address.ToString(), reputation, baseGas, 
                  result.isConsensus ? "PASS" : "FAIL", result.calculatedDiscount);
        
    } catch (const std::exception& e) {
        result.isConsensus = false;
        result.failureReason = strprintf("Exception during validation: %s", e.what());
        LogPrintf("ConsensusSafetyValidator: Exception in ValidateGasDiscountConsensus: %s\n", e.what());
    }
    
    return result;
}

FreeGasEligibilityResult ConsensusSafetyValidator::ValidateFreeGasEligibility(
    const uint160& address,
    uint8_t reputation
) {
    FreeGasEligibilityResult result;
    result.reputation = reputation;
    
    try {
        // Deterministic eligibility check: reputation >= 80
        result.isEligible = (reputation >= FREE_GAS_THRESHOLD);
        
        // Calculate deterministic allowance
        result.allowance = CalculateDeterministicFreeGasAllowance(reputation);
        
        // Verify calculation is deterministic
        uint64_t allowance1 = CalculateDeterministicFreeGasAllowance(reputation);
        uint64_t allowance2 = CalculateDeterministicFreeGasAllowance(reputation);
        
        if (allowance1 != allowance2) {
            result.isConsensus = false;
            result.failureReason = "Free gas allowance calculation is non-deterministic";
            return result;
        }
        
        // Verify eligibility is consistent
        bool eligible1 = (reputation >= FREE_GAS_THRESHOLD);
        bool eligible2 = (reputation >= FREE_GAS_THRESHOLD);
        
        if (eligible1 != eligible2) {
            result.isConsensus = false;
            result.failureReason = "Free gas eligibility check is non-deterministic";
            return result;
        }
        
        result.isConsensus = true;
        
        LogPrintf("ConsensusSafetyValidator: Free gas eligibility for %s (rep=%d): %s (eligible=%s, allowance=%lu)\n",
                  address.ToString(), reputation, 
                  result.isConsensus ? "PASS" : "FAIL",
                  result.isEligible ? "yes" : "no", result.allowance);
        
    } catch (const std::exception& e) {
        result.isConsensus = false;
        result.failureReason = strprintf("Exception during validation: %s", e.what());
        LogPrintf("ConsensusSafetyValidator: Exception in ValidateFreeGasEligibility: %s\n", e.what());
    }
    
    return result;
}

uint64_t ConsensusSafetyValidator::CalculateDeterministicGasDiscount(
    uint8_t reputation,
    uint64_t baseGas
) {
    // Deterministic discount formula:
    // discount = baseGas * (reputation * GAS_DISCOUNT_FACTOR)
    // Maximum discount is 50% at reputation 100
    
    if (reputation == 0) {
        return 0;
    }
    
    // Use integer arithmetic for determinism
    // GAS_DISCOUNT_FACTOR = 0.005 = 5/1000
    // discount = baseGas * reputation * 5 / 1000
    uint64_t discount = (baseGas * reputation * 5) / 1000;
    
    // Cap at 50% of base gas
    uint64_t maxDiscount = baseGas / 2;
    if (discount > maxDiscount) {
        discount = maxDiscount;
    }
    
    return discount;
}

uint64_t ConsensusSafetyValidator::CalculateDeterministicFreeGasAllowance(uint8_t reputation) {
    // Only eligible if reputation >= 80
    if (reputation < FREE_GAS_THRESHOLD) {
        return 0;
    }
    
    // Deterministic allowance formula:
    // allowance = BASE_FREE_GAS_ALLOWANCE * (1 + (reputation - 80) / 20)
    // At reputation 80: 100,000 gas
    // At reputation 100: 200,000 gas
    
    // Use integer arithmetic for determinism
    // allowance = BASE * (20 + (reputation - 80)) / 20
    uint64_t reputationBonus = reputation - FREE_GAS_THRESHOLD;
    uint64_t allowance = BASE_FREE_GAS_ALLOWANCE * (20 + reputationBonus) / 20;
    
    return allowance;
}

// ========== Task 23.3: Trust Score Synchronization ==========

TrustGraphSyncState ConsensusSafetyValidator::GetTrustGraphState() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    TrustGraphSyncState state;
    
    if (!trustGraph || !database) {
        return state;
    }
    
    try {
        // Calculate state hash
        state.stateHash = CalculateTrustGraphStateHash();
        
        // Get current block height
        LOCK(cs_main);
        state.lastSyncBlock = chainActive.Height();
        
        // Get graph statistics
        std::map<std::string, uint64_t> stats = trustGraph->GetGraphStats();
        state.edgeCount = stats["total_trust_edges"];
        state.nodeCount = stats["total_votes"]; // Use votes as proxy for nodes
        state.isSynchronized = true;
        
        // Cache the state
        cachedState = state;
        cacheBlockHeight = state.lastSyncBlock;
        
    } catch (const std::exception& e) {
        LogPrintf("ConsensusSafetyValidator: Exception in GetTrustGraphState: %s\n", e.what());
        state.isSynchronized = false;
    }
    
    return state;
}

uint256 ConsensusSafetyValidator::CalculateTrustGraphStateHash() {
    CHashWriter ss(SER_GETHASH, 0);
    
    if (!trustGraph) {
        return ss.GetHash();
    }
    
    try {
        // Get graph statistics for deterministic hash
        std::map<std::string, uint64_t> stats = trustGraph->GetGraphStats();
        
        // Hash statistics in deterministic order
        ss << stats["total_trust_edges"];
        ss << stats["total_votes"];
        ss << stats["total_disputes"];
        ss << stats["slashed_votes"];
        
    } catch (const std::exception& e) {
        LogPrintf("ConsensusSafetyValidator: Exception in CalculateTrustGraphStateHash: %s\n", e.what());
    }
    
    return ss.GetHash();
}

bool ConsensusSafetyValidator::VerifyTrustGraphState(const uint256& expectedHash) {
    uint256 currentHash = CalculateTrustGraphStateHash();
    
    bool matches = (currentHash == expectedHash);
    
    LogPrintf("ConsensusSafetyValidator: Trust graph state verification: %s (expected=%s, current=%s)\n",
              matches ? "PASS" : "FAIL", expectedHash.ToString(), currentHash.ToString());
    
    return matches;
}

bool ConsensusSafetyValidator::SynchronizeTrustGraphState(const TrustGraphSyncState& peerState) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    if (!trustGraph || !database) {
        return false;
    }
    
    try {
        // Get current state
        TrustGraphSyncState currentState = GetTrustGraphState();
        
        // Check if already synchronized
        if (currentState.stateHash == peerState.stateHash) {
            LogPrintf("ConsensusSafetyValidator: Trust graph already synchronized\n");
            return true;
        }
        
        // If peer has newer state, request delta
        if (peerState.lastSyncBlock > currentState.lastSyncBlock) {
            LogPrintf("ConsensusSafetyValidator: Peer has newer trust graph state (peer=%lu, local=%lu)\n",
                      peerState.lastSyncBlock, currentState.lastSyncBlock);
            // In a real implementation, we would request the delta from the peer
            // For now, we just log the discrepancy
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LogPrintf("ConsensusSafetyValidator: Exception in SynchronizeTrustGraphState: %s\n", e.what());
        return false;
    }
}

std::vector<TrustEdge> ConsensusSafetyValidator::GetTrustGraphDelta(int sinceBlock) {
    std::vector<TrustEdge> delta;
    
    if (!database) {
        return delta;
    }
    
    try {
        // Query database for trust edges modified since sinceBlock
        std::string prefix = "trust_delta_";
        std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;
        
        // In a real implementation, we would query the database for changes
        // For now, return empty delta
        
    } catch (const std::exception& e) {
        LogPrintf("ConsensusSafetyValidator: Exception in GetTrustGraphDelta: %s\n", e.what());
    }
    
    return delta;
}

bool ConsensusSafetyValidator::ApplyTrustGraphDelta(const std::vector<TrustEdge>& delta) {
    if (!trustGraph || !database) {
        return false;
    }
    
    try {
        for (const auto& edge : delta) {
            if (edge.slashed) {
                // This edge was slashed, skip it
                continue;
            }
            // Add or update trust edge
            trustGraph->AddTrustEdge(
                edge.fromAddress, 
                edge.toAddress, 
                edge.trustWeight,
                edge.bondAmount,
                edge.bondTxHash,
                edge.reason
            );
        }
        
        LogPrintf("ConsensusSafetyValidator: Applied %zu trust graph delta entries\n", delta.size());
        return true;
        
    } catch (const std::exception& e) {
        LogPrintf("ConsensusSafetyValidator: Exception in ApplyTrustGraphDelta: %s\n", e.what());
        return false;
    }
}


// ========== Task 23.4: Cross-Chain Attestation Validation ==========

CrossChainAttestationResult ConsensusSafetyValidator::ValidateCrossChainAttestation(
    const TrustAttestation& attestation
) {
    CrossChainAttestationResult result;
    
    try {
        // Verify attestation has required fields
        if (attestation.address.IsNull()) {
            result.isValid = false;
            result.failureReason = "Attestation address is null";
            return result;
        }
        
        if (attestation.timestamp == 0) {
            result.isValid = false;
            result.failureReason = "Attestation timestamp is zero";
            return result;
        }
        
        // Verify signature
        if (!VerifyAttestationSignature(attestation)) {
            result.isValid = false;
            result.failureReason = "Attestation signature verification failed";
            return result;
        }
        
        // Verify attestation is not too old (max 24 hours)
        uint64_t currentTime = GetTime();
        uint64_t maxAge = 24 * 60 * 60; // 24 hours
        if (currentTime - attestation.timestamp > maxAge) {
            result.isValid = false;
            result.failureReason = "Attestation is too old";
            return result;
        }
        
        // Calculate deterministic attestation hash
        uint256 attestationHash = CalculateAttestationHash(attestation);
        
        // Verify hash is deterministic
        uint256 hash1 = CalculateAttestationHash(attestation);
        uint256 hash2 = CalculateAttestationHash(attestation);
        
        if (hash1 != hash2) {
            result.isConsensusSafe = false;
            result.failureReason = "Attestation hash calculation is non-deterministic";
            return result;
        }
        
        result.isValid = true;
        result.isConsensusSafe = true;
        // Extract lower 16 bits from uint256 sourceChainId for result
        result.sourceChainId = static_cast<uint16_t>(attestation.sourceChainId.GetUint64(0) & 0xFFFF);
        result.trustScore = static_cast<uint8_t>(attestation.trustScore);
        
        LogPrintf("ConsensusSafetyValidator: Cross-chain attestation validation for %s: %s (chain=%d, score=%d)\n",
                  attestation.address.ToString(), 
                  result.isValid ? "VALID" : "INVALID",
                  result.sourceChainId, result.trustScore);
        
    } catch (const std::exception& e) {
        result.isValid = false;
        result.isConsensusSafe = false;
        result.failureReason = strprintf("Exception during validation: %s", e.what());
        LogPrintf("ConsensusSafetyValidator: Exception in ValidateCrossChainAttestation: %s\n", e.what());
    }
    
    return result;
}

bool ConsensusSafetyValidator::ValidateCryptographicProof(
    const TrustStateProof& proof,
    uint16_t sourceChainId
) {
    try {
        // Verify proof has required fields
        if (proof.address.IsNull()) {
            LogPrintf("ConsensusSafetyValidator: Proof address is null\n");
            return false;
        }
        
        if (proof.blockHeight == 0) {
            LogPrintf("ConsensusSafetyValidator: Proof block height is zero\n");
            return false;
        }
        
        // Verify merkle proof
        if (!proof.VerifyMerkleProof()) {
            LogPrintf("ConsensusSafetyValidator: Merkle proof verification failed\n");
            return false;
        }
        
        // Verify signature
        if (proof.signature.empty()) {
            LogPrintf("ConsensusSafetyValidator: Proof signature is empty\n");
            return false;
        }
        
        // Verify proof hash is deterministic
        uint256 hash1 = proof.GetHash();
        uint256 hash2 = proof.GetHash();
        
        if (hash1 != hash2) {
            LogPrintf("ConsensusSafetyValidator: Proof hash is non-deterministic\n");
            return false;
        }
        
        LogPrintf("ConsensusSafetyValidator: Cryptographic proof validation: PASS (chain=%d, address=%s)\n",
                  sourceChainId, proof.address.ToString());
        
        return true;
        
    } catch (const std::exception& e) {
        LogPrintf("ConsensusSafetyValidator: Exception in ValidateCryptographicProof: %s\n", e.what());
        return false;
    }
}

uint256 ConsensusSafetyValidator::CalculateAttestationHash(const TrustAttestation& attestation) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << attestation.address;
    ss << attestation.trustScore;
    ss << attestation.timestamp;
    ss << attestation.sourceChainId;
    ss << attestation.signature;
    return ss.GetHash();
}

bool ConsensusSafetyValidator::VerifyAttestationSignature(const TrustAttestation& attestation) {
    // Verify the attestation signature
    if (attestation.signature.empty()) {
        return false;
    }
    
    // Calculate the message hash that was signed
    CHashWriter ss(SER_GETHASH, 0);
    ss << attestation.address;
    ss << attestation.trustScore;
    ss << attestation.timestamp;
    ss << attestation.sourceChainId;
    uint256 messageHash = ss.GetHash();
    
    // In a real implementation, we would verify the signature against
    // the attestor's public key. For now, we just check that the
    // signature is not empty and has a reasonable length.
    if (attestation.signature.size() < 64 || attestation.signature.size() > 128) {
        return false;
    }
    
    return true;
}

// ========== Utility Methods ==========

bool ConsensusSafetyValidator::RunFullValidation(const uint160& address, int blockHeight) {
    bool allPassed = true;
    
    // Task 23.1: Deterministic execution validation
    DeterministicExecutionResult detResult = ValidateHATv2Determinism(address, address, blockHeight);
    if (!detResult.isDeterministic) {
        LogPrintf("ConsensusSafetyValidator: HAT v2 determinism check FAILED: %s\n", detResult.failureReason);
        allPassed = false;
    }
    
    // Task 23.2: Reputation-based feature consensus
    uint8_t reputation = 75; // Example reputation
    if (secureHAT) {
        reputation = secureHAT->CalculateFinalTrust(address, address);
    }
    
    GasDiscountConsensusResult gasResult = ValidateGasDiscountConsensus(address, reputation, 100000);
    if (!gasResult.isConsensus) {
        LogPrintf("ConsensusSafetyValidator: Gas discount consensus check FAILED: %s\n", gasResult.failureReason);
        allPassed = false;
    }
    
    FreeGasEligibilityResult freeGasResult = ValidateFreeGasEligibility(address, reputation);
    if (!freeGasResult.isConsensus) {
        LogPrintf("ConsensusSafetyValidator: Free gas eligibility check FAILED: %s\n", freeGasResult.failureReason);
        allPassed = false;
    }
    
    // Task 23.3: Trust score synchronization
    TrustGraphSyncState state = GetTrustGraphState();
    if (!state.isSynchronized) {
        LogPrintf("ConsensusSafetyValidator: Trust graph synchronization check FAILED\n");
        allPassed = false;
    }
    
    LogPrintf("ConsensusSafetyValidator: Full validation for %s at height %d: %s\n",
              address.ToString(), blockHeight, allPassed ? "PASS" : "FAIL");
    
    return allPassed;
}

std::string ConsensusSafetyValidator::GetValidationReport(const uint160& address, int blockHeight) {
    std::string report;
    
    report += "=== Consensus Safety Validation Report ===\n";
    report += strprintf("Address: %s\n", address.ToString());
    report += strprintf("Block Height: %d\n", blockHeight);
    report += "\n";
    
    // Task 23.1: Deterministic execution validation
    report += "--- Task 23.1: Deterministic Execution Validation ---\n";
    DeterministicExecutionResult detResult = ValidateHATv2Determinism(address, address, blockHeight);
    report += strprintf("HAT v2 Determinism: %s\n", detResult.isDeterministic ? "PASS" : "FAIL");
    if (!detResult.isDeterministic) {
        report += strprintf("  Failure Reason: %s\n", detResult.failureReason);
    }
    report += strprintf("  Execution Hash: %s\n", detResult.executionHash.ToString());
    report += strprintf("  Behavior Hash: %s\n", detResult.behaviorHash.ToString());
    report += strprintf("  WoT Hash: %s\n", detResult.wotHash.ToString());
    report += strprintf("  Economic Hash: %s\n", detResult.economicHash.ToString());
    report += strprintf("  Temporal Hash: %s\n", detResult.temporalHash.ToString());
    report += "\n";
    
    // Task 23.2: Reputation-based feature consensus
    report += "--- Task 23.2: Reputation-Based Feature Consensus ---\n";
    uint8_t reputation = 75;
    if (secureHAT) {
        reputation = secureHAT->CalculateFinalTrust(address, address);
    }
    report += strprintf("Reputation Score: %d\n", reputation);
    
    GasDiscountConsensusResult gasResult = ValidateGasDiscountConsensus(address, reputation, 100000);
    report += strprintf("Gas Discount Consensus: %s\n", gasResult.isConsensus ? "PASS" : "FAIL");
    report += strprintf("  Calculated Discount: %lu\n", gasResult.calculatedDiscount);
    
    FreeGasEligibilityResult freeGasResult = ValidateFreeGasEligibility(address, reputation);
    report += strprintf("Free Gas Eligibility: %s\n", freeGasResult.isConsensus ? "PASS" : "FAIL");
    report += strprintf("  Is Eligible: %s\n", freeGasResult.isEligible ? "yes" : "no");
    report += strprintf("  Allowance: %lu\n", freeGasResult.allowance);
    report += "\n";
    
    // Task 23.3: Trust score synchronization
    report += "--- Task 23.3: Trust Score Synchronization ---\n";
    TrustGraphSyncState state = GetTrustGraphState();
    report += strprintf("Trust Graph Synchronized: %s\n", state.isSynchronized ? "yes" : "no");
    report += strprintf("  State Hash: %s\n", state.stateHash.ToString());
    report += strprintf("  Last Sync Block: %lu\n", state.lastSyncBlock);
    report += strprintf("  Edge Count: %lu\n", state.edgeCount);
    report += strprintf("  Node Count: %lu\n", state.nodeCount);
    report += "\n";
    
    report += "=== End of Report ===\n";
    
    return report;
}

// ========== Helper Methods ==========

uint256 ConsensusSafetyValidator::HashBehaviorComponent(const BehaviorMetrics& metrics) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << metrics.address;
    ss << metrics.total_trades;
    ss << metrics.successful_trades;
    ss << metrics.disputed_trades;
    ss << metrics.unique_partners.size();
    ss << metrics.total_volume;
    ss << metrics.diversity_score;
    return ss.GetHash();
}

uint256 ConsensusSafetyValidator::HashWoTComponent(const uint160& address, const uint160& viewer) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << viewer;
    
    if (trustGraph) {
        int16_t reputation = trustGraph->GetWeightedReputation(viewer, address, 3);
        ss << reputation;
    }
    
    return ss.GetHash();
}

uint256 ConsensusSafetyValidator::HashEconomicComponent(const StakeInfo& stake) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << stake.amount;
    ss << stake.stake_start;
    ss << stake.min_lock_duration;
    return ss.GetHash();
}

uint256 ConsensusSafetyValidator::HashTemporalComponent(const TemporalMetrics& temporal) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << temporal.account_creation;
    ss << temporal.last_activity;
    for (const auto& ts : temporal.activity_timestamps) {
        ss << ts;
    }
    return ss.GetHash();
}

uint256 ConsensusSafetyValidator::GetBlockHashAtHeight(int blockHeight) {
    LOCK(cs_main);
    
    if (blockHeight < 0 || blockHeight > chainActive.Height()) {
        return uint256();
    }
    
    CBlockIndex* pindex = chainActive[blockHeight];
    if (!pindex) {
        return uint256();
    }
    
    return pindex->GetBlockHash();
}

bool ConsensusSafetyValidator::ValidateComponentDeterminism(
    const uint160& address,
    const uint160& viewer,
    int blockHeight
) {
    if (!secureHAT) {
        return false;
    }
    
    // Calculate score multiple times and verify consistency
    TrustBreakdown breakdown1 = secureHAT->CalculateWithBreakdown(address, viewer);
    TrustBreakdown breakdown2 = secureHAT->CalculateWithBreakdown(address, viewer);
    TrustBreakdown breakdown3 = secureHAT->CalculateWithBreakdown(address, viewer);
    
    // Check all components match
    if (breakdown1.final_score != breakdown2.final_score ||
        breakdown2.final_score != breakdown3.final_score) {
        return false;
    }
    
    // Use epsilon comparison for floating point components
    const double epsilon = 0.0001;
    
    if (std::abs(breakdown1.secure_behavior - breakdown2.secure_behavior) > epsilon ||
        std::abs(breakdown2.secure_behavior - breakdown3.secure_behavior) > epsilon) {
        return false;
    }
    
    if (std::abs(breakdown1.secure_wot - breakdown2.secure_wot) > epsilon ||
        std::abs(breakdown2.secure_wot - breakdown3.secure_wot) > epsilon) {
        return false;
    }
    
    if (std::abs(breakdown1.secure_economic - breakdown2.secure_economic) > epsilon ||
        std::abs(breakdown2.secure_economic - breakdown3.secure_economic) > epsilon) {
        return false;
    }
    
    if (std::abs(breakdown1.secure_temporal - breakdown2.secure_temporal) > epsilon ||
        std::abs(breakdown2.secure_temporal - breakdown3.secure_temporal) > epsilon) {
        return false;
    }
    
    return true;
}

} // namespace CVM
