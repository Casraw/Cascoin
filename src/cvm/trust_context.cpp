// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_context.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/reputation.h>
#include <cvm/securehat.h>
#include <cvm/cross_chain_bridge.h>
#include <util.h>
#include <timedata.h>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace CVM {

TrustContext::TrustContext() 
    : database(nullptr), caller_reputation(0), contract_reputation(0) {
    InitializeDefaultTrustGates();
    InitializeDefaultAccessPolicies();
}

TrustContext::TrustContext(CVMDatabase* db) 
    : database(db), caller_reputation(0), contract_reputation(0) {
    InitializeDefaultTrustGates();
    InitializeDefaultAccessPolicies();
}

TrustContext::~TrustContext() {
}

uint32_t TrustContext::GetReputation(const uint160& address) const {
    if (!database) {
        return 0;
    }
    
    // Try to get reputation from database first
    uint32_t reputation = CalculateReputationScore(address);
    
    // Apply cross-chain aggregation if available
    uint32_t aggregated = GetAggregatedReputation(address);
    if (aggregated > reputation) {
        reputation = aggregated;
    }
    
    return reputation;
}

uint32_t TrustContext::GetWeightedReputation(const uint160& address, const uint160& observer) const {
    if (!trust_graph) {
        return GetReputation(address);
    }
    
    // Use trust graph to calculate weighted reputation from observer's perspective
    return trust_graph->GetWeightedReputation(address, observer);
}

bool TrustContext::HasMinimumReputation(const uint160& address, uint32_t min_reputation) const {
    return GetReputation(address) >= min_reputation;
}

bool TrustContext::CheckTrustGate(const uint160& address, const std::string& operation) const {
    auto it = trust_gates.find(operation);
    if (it == trust_gates.end()) {
        return true; // No gate configured, allow operation
    }
    
    const TrustGate& gate = it->second;
    uint32_t reputation = GetReputation(address);
    
    // Check minimum reputation requirement
    if (reputation < gate.min_reputation) {
        return false;
    }
    
    // Check cross-chain verification if required
    if (gate.require_cross_chain_verification) {
        auto attestations_it = cross_chain_attestations.find(address);
        if (attestations_it == cross_chain_attestations.end() || attestations_it->second.empty()) {
            return false;
        }
        
        // Verify at least one cross-chain attestation
        bool has_valid_attestation = false;
        for (const auto& attestation : attestations_it->second) {
            if (attestation.verified && attestation.reputation >= gate.min_reputation) {
                has_valid_attestation = true;
                break;
            }
        }
        
        if (!has_valid_attestation) {
            return false;
        }
    }
    
    return true;
}

bool TrustContext::IsHighReputationAddress(const uint160& address) const {
    return GetReputation(address) >= HIGH_REPUTATION_THRESHOLD;
}

bool TrustContext::CanPerformOperation(const uint160& address, const std::string& operation, uint64_t gas_limit) const {
    if (!CheckTrustGate(address, operation)) {
        return false;
    }
    
    auto it = trust_gates.find(operation);
    if (it != trust_gates.end() && gas_limit > it->second.max_gas_limit) {
        return false;
    }
    
    return true;
}

void TrustContext::InjectTrustContext(const uint160& caller, const uint160& contract) {
    current_caller = caller;
    current_contract = contract;
    caller_reputation = GetReputation(caller);
    contract_reputation = GetReputation(contract);
}

void TrustContext::AddCrossChainAttestation(const uint160& address, const std::string& chain, uint32_t reputation) {
    CrossChainAttestation attestation;
    attestation.source_chain = chain;
    attestation.reputation = reputation;
    attestation.timestamp = GetTime();
    attestation.verified = false; // Will be verified later
    
    cross_chain_attestations[address].push_back(attestation);
}

uint32_t TrustContext::GetCrossChainReputation(const uint160& address, const std::string& chain) const {
    auto it = cross_chain_attestations.find(address);
    if (it == cross_chain_attestations.end()) {
        return 0;
    }
    
    for (const auto& attestation : it->second) {
        if (attestation.source_chain == chain && attestation.verified) {
            return attestation.reputation;
        }
    }
    
    return 0;
}

uint32_t TrustContext::GetAggregatedReputation(const uint160& address) const {
    auto it = cross_chain_attestations.find(address);
    if (it == cross_chain_attestations.end()) {
        return GetReputation(address);
    }
    
    std::vector<uint32_t> scores;
    std::vector<uint32_t> weights;
    
    // Add local reputation with highest weight
    uint32_t local_reputation = CalculateReputationScore(address);
    scores.push_back(local_reputation);
    weights.push_back(100);
    
    // Add cross-chain attestations with lower weights
    for (const auto& attestation : it->second) {
        if (attestation.verified) {
            scores.push_back(attestation.reputation);
            weights.push_back(50); // Cross-chain attestations have 50% weight
        }
    }
    
    return TrustContextUtils::CalculateWeightedReputation(scores, weights);
}

void TrustContext::RecordReputationChange(const uint160& address, uint32_t old_reputation, 
                                        uint32_t new_reputation, const std::string& reason) {
    ReputationEvent event;
    event.address = address;
    event.old_reputation = old_reputation;
    event.new_reputation = new_reputation;
    event.reason = reason;
    event.timestamp = GetTime();
    // event.tx_hash would be set by caller
    
    reputation_history[address].push_back(event);
    
    // Limit history size to prevent unbounded growth
    if (reputation_history[address].size() > 1000) {
        reputation_history[address].erase(reputation_history[address].begin());
    }
}

std::vector<TrustContext::ReputationEvent> TrustContext::GetReputationHistory(const uint160& address) const {
    auto it = reputation_history.find(address);
    if (it != reputation_history.end()) {
        LogPrint(BCLog::CVM, "TrustContext: Retrieved %d reputation events for %s\n",
                 it->second.size(), address.ToString());
        return it->second;
    }
    
    LogPrint(BCLog::CVM, "TrustContext: No reputation history found for %s\n",
             address.ToString());
    return {};
}

uint64_t TrustContext::ApplyReputationGasDiscount(uint64_t base_gas, const uint160& address) const {
    uint32_t reputation = GetReputation(address);
    uint64_t discounted_gas = TrustContextUtils::CalculateReputationGasDiscount(base_gas, reputation);
    
    LogPrint(BCLog::CVM, "TrustContext: Applied gas discount for %s (reputation: %d): %d -> %d\n",
             address.ToString(), reputation, base_gas, discounted_gas);
    
    return discounted_gas;
}

uint64_t TrustContext::GetGasAllowance(const uint160& address) const {
    uint32_t reputation = GetReputation(address);
    uint64_t allowance = TrustContextUtils::CalculateFreeGasAllowance(reputation, 86400); // 24 hours
    
    LogPrint(BCLog::CVM, "TrustContext: Gas allowance for %s (reputation: %d): %d\n",
             address.ToString(), reputation, allowance);
    
    return allowance;
}

bool TrustContext::HasFreeGasEligibility(const uint160& address) const {
    uint32_t reputation = GetReputation(address);
    bool eligible = reputation >= FREE_GAS_REPUTATION_THRESHOLD;
    
    LogPrint(BCLog::CVM, "TrustContext: Free gas eligibility for %s (reputation: %d): %s\n",
             address.ToString(), reputation, eligible ? "yes" : "no");
    
    return eligible;
}

void TrustContext::AddTrustWeightedValue(const std::string& key, const TrustWeightedValue& value) {
    // Validate trust weight
    if (value.trust_weight > 100) {
        LogPrint(BCLog::CVM, "TrustContext: Invalid trust weight %d for key %s\n",
                 value.trust_weight, key);
        return;
    }
    
    // Get reputation of source address to validate
    uint32_t source_reputation = GetReputation(value.source_address);
    if (source_reputation < LOW_REPUTATION_THRESHOLD) {
        LogPrint(BCLog::CVM, "TrustContext: Rejecting value from low reputation address %s (reputation: %d)\n",
                 value.source_address.ToString(), source_reputation);
        return;
    }
    
    trust_weighted_data[key].push_back(value);
    
    // Sort by trust weight (highest first)
    std::sort(trust_weighted_data[key].begin(), trust_weighted_data[key].end(),
              [](const TrustWeightedValue& a, const TrustWeightedValue& b) {
                  return a.trust_weight > b.trust_weight;
              });
    
    // Limit to top 100 values to prevent unbounded growth
    if (trust_weighted_data[key].size() > 100) {
        trust_weighted_data[key].resize(100);
    }
    
    LogPrint(BCLog::CVM, "TrustContext: Added trust-weighted value for key %s (weight: %d, total values: %d)\n",
             key, value.trust_weight, trust_weighted_data[key].size());
}

std::vector<TrustContext::TrustWeightedValue> TrustContext::GetTrustWeightedValues(const std::string& key) const {
    auto it = trust_weighted_data.find(key);
    if (it != trust_weighted_data.end()) {
        LogPrint(BCLog::CVM, "TrustContext: Retrieved %d trust-weighted values for key %s\n",
                 it->second.size(), key);
        return it->second;
    }
    
    LogPrint(BCLog::CVM, "TrustContext: No trust-weighted values found for key %s\n", key);
    return {};
}

TrustContext::TrustWeightedValue TrustContext::GetHighestTrustValue(const std::string& key) const {
    auto values = GetTrustWeightedValues(key);
    if (!values.empty()) {
        return values[0]; // Already sorted by trust weight
    }
    
    TrustWeightedValue empty = {};
    return empty;
}

bool TrustContext::CheckAccessLevel(const uint160& address, const std::string& resource, const std::string& action) const {
    auto resource_it = access_policies.find(resource);
    if (resource_it == access_policies.end()) {
        LogPrint(BCLog::CVM, "TrustContext: No access policy for resource %s, allowing access\n", resource);
        return true; // No policy configured, allow access
    }
    
    auto action_it = resource_it->second.find(action);
    if (action_it == resource_it->second.end()) {
        LogPrint(BCLog::CVM, "TrustContext: No access policy for action %s on resource %s, allowing access\n",
                 action, resource);
        return true; // No policy for this action, allow access
    }
    
    const AccessPolicy& policy = action_it->second;
    uint32_t reputation = GetReputation(address);
    
    bool has_access = reputation >= policy.min_reputation;
    
    LogPrint(BCLog::CVM, "TrustContext: Access check for %s on %s.%s (reputation: %d, required: %d): %s\n",
             address.ToString(), resource, action, reputation, policy.min_reputation,
             has_access ? "granted" : "denied");
    
    // Check required attestations if specified
    if (has_access && !policy.required_attestations.empty()) {
        auto attestations_it = cross_chain_attestations.find(address);
        if (attestations_it == cross_chain_attestations.end()) {
            LogPrint(BCLog::CVM, "TrustContext: Access denied - no cross-chain attestations found\n");
            return false;
        }
        
        for (const auto& required_chain : policy.required_attestations) {
            bool found = false;
            for (const auto& attestation : attestations_it->second) {
                if (attestation.source_chain == required_chain && attestation.verified) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                LogPrint(BCLog::CVM, "TrustContext: Access denied - missing required attestation from %s\n",
                         required_chain);
                return false;
            }
        }
    }
    
    return has_access;
}

void TrustContext::SetAccessPolicy(const std::string& resource, const std::string& action, uint32_t min_reputation) {
    if (min_reputation > 100) {
        LogPrint(BCLog::CVM, "TrustContext: Invalid min_reputation %d for policy %s.%s\n",
                 min_reputation, resource, action);
        return;
    }
    
    AccessPolicy policy;
    policy.min_reputation = min_reputation;
    policy.cooldown_period = 0;
    
    access_policies[resource][action] = policy;
    
    LogPrint(BCLog::CVM, "TrustContext: Set access policy for %s.%s (min_reputation: %d)\n",
             resource, action, min_reputation);
}

void TrustContext::ApplyReputationDecay(int64_t current_time) {
    // Apply decay to all tracked addresses
    // This would typically be called periodically by the system
    int decay_count = 0;
    
    for (auto& history_pair : reputation_history) {
        const uint160& address = history_pair.first;
        auto& events = history_pair.second;
        
        if (!events.empty()) {
            const auto& last_event = events.back();
            int64_t time_since_last = current_time - last_event.timestamp;
            
            if (time_since_last > static_cast<int64_t>(REPUTATION_DECAY_PERIOD)) {
                uint32_t current_reputation = GetReputation(address);
                uint32_t decayed_reputation = TrustContextUtils::ApplyReputationDecay(current_reputation, time_since_last);
                
                if (decayed_reputation != current_reputation) {
                    RecordReputationChange(address, current_reputation, decayed_reputation, "automatic_decay");
                    decay_count++;
                    
                    LogPrint(BCLog::CVM, "TrustContext: Applied decay to %s: %d -> %d (inactive for %d seconds)\n",
                             address.ToString(), current_reputation, decayed_reputation, time_since_last);
                }
            }
        }
    }
    
    if (decay_count > 0) {
        LogPrint(BCLog::CVM, "TrustContext: Applied reputation decay to %d addresses\n", decay_count);
    }
}

void TrustContext::UpdateReputationFromActivity(const uint160& address, const std::string& activity_type, int32_t reputation_delta) {
    uint32_t current_reputation = GetReputation(address);
    int32_t new_reputation_int = static_cast<int32_t>(current_reputation) + reputation_delta;
    uint32_t new_reputation = std::max(0, std::min(100, new_reputation_int));
    
    if (new_reputation != current_reputation) {
        RecordReputationChange(address, current_reputation, new_reputation, activity_type);
        
        LogPrint(BCLog::CVM, "TrustContext: Updated reputation for %s from activity '%s': %d -> %d (delta: %d)\n",
                 address.ToString(), activity_type, current_reputation, new_reputation, reputation_delta);
        
        // If this is a significant change, also update in the database
        if (database && std::abs(reputation_delta) >= 5) {
            try {
                ReputationSystem reputation_system(*database);
                ReputationScore score;
                
                if (reputation_system.GetReputation(address, score)) {
                    // Update the score
                    score.score += (reputation_delta * 100); // Scale to -10000 to +10000 range
                    score.score = std::max(int64_t(-10000), std::min(int64_t(10000), score.score));
                    score.lastUpdated = GetTime();
                    
                    reputation_system.UpdateReputation(address, score);
                    
                    LogPrint(BCLog::CVM, "TrustContext: Persisted reputation update to database for %s\n",
                             address.ToString());
                }
            } catch (const std::exception& e) {
                LogPrint(BCLog::CVM, "TrustContext: Failed to persist reputation update: %s\n", e.what());
            }
        }
    }
}

uint32_t TrustContext::CalculateReputationScore(const uint160& address) const {
    if (!database) {
        LogPrint(BCLog::CVM, "TrustContext: No database available for reputation calculation\n");
        return 0;
    }
    
    try {
        // Integrate with existing reputation system
        ReputationSystem reputation_system(*database);
        ReputationScore rep_score;
        
        if (reputation_system.GetReputation(address, rep_score)) {
            // Convert reputation score (-10000 to +10000) to 0-100 scale
            // Map: -10000 -> 0, 0 -> 50, +10000 -> 100
            int64_t normalized = ((rep_score.score + 10000) * 100) / 20000;
            normalized = std::max(int64_t(0), std::min(int64_t(100), normalized));
            
            LogPrint(BCLog::CVM, "TrustContext: Reputation for %s: raw=%d, normalized=%d\n",
                     address.ToString(), rep_score.score, normalized);
            
            return static_cast<uint32_t>(normalized);
        }
        
        // If no reputation score exists, try HAT v2 system
        SecureHAT secure_hat(*database);
        
        // Use a default viewer address (could be improved with actual viewer context)
        uint160 default_viewer;
        int16_t hat_score = secure_hat.CalculateFinalTrust(address, default_viewer);
        
        // HAT score is already 0-100
        if (hat_score >= 0 && hat_score <= 100) {
            LogPrint(BCLog::CVM, "TrustContext: HAT v2 score for %s: %d\n",
                     address.ToString(), hat_score);
            return static_cast<uint32_t>(hat_score);
        }
        
        // Default to medium reputation if no data available
        LogPrint(BCLog::CVM, "TrustContext: No reputation data for %s, using default 50\n",
                 address.ToString());
        return 50;
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "TrustContext: Exception calculating reputation for %s: %s\n",
                 address.ToString(), e.what());
        return 50; // Default to medium reputation on error
    }
}

bool TrustContext::VerifyCrossChainAttestation(const CrossChainAttestation& attestation) const {
    // Verify cross-chain attestation with cryptographic proof verification
    
    // Check if attestation is recent (within 7 days)
    int64_t current_time = GetTime();
    int64_t max_age = 7 * 24 * 60 * 60; // 7 days in seconds
    
    if (current_time - attestation.timestamp > max_age) {
        LogPrint(BCLog::CVM, "TrustContext: Cross-chain attestation expired (age: %d seconds)\n",
                 current_time - attestation.timestamp);
        return false;
    }
    
    // Verify proof hash is not empty
    if (attestation.proof_hash.empty()) {
        LogPrint(BCLog::CVM, "TrustContext: Cross-chain attestation missing proof hash\n");
        return false;
    }
    
    // Verify reputation is in valid range
    if (attestation.reputation > 100) {
        LogPrint(BCLog::CVM, "TrustContext: Cross-chain attestation has invalid reputation: %d\n",
                 attestation.reputation);
        return false;
    }
    
    // Verify source chain is supported
    // In a full implementation, this would:
    // 1. Verify the cryptographic signature from the source chain
    // 2. Check that the source chain is in the list of trusted chains
    // 3. Validate the Merkle proof or other cryptographic proof
    // 4. Verify the attestation hasn't been revoked
    
    // For now, we perform basic validation
    // TODO: Implement full cryptographic verification with LayerZero/CCIP
    
    std::vector<std::string> supported_chains = {
        "ethereum", "polygon", "arbitrum", "optimism", "base", "avalanche"
    };
    
    bool chain_supported = false;
    for (const auto& chain : supported_chains) {
        if (attestation.source_chain == chain) {
            chain_supported = true;
            break;
        }
    }
    
    if (!chain_supported) {
        LogPrint(BCLog::CVM, "TrustContext: Unsupported source chain: %s\n",
                 attestation.source_chain);
        return false;
    }
    
    // Basic validation passed
    // In production, this would verify cryptographic proofs
    LogPrint(BCLog::CVM, "TrustContext: Cross-chain attestation from %s validated (basic checks only)\n",
             attestation.source_chain);
    
    return true;
}

void TrustContext::InitializeDefaultTrustGates() {
    // High-value operations require high reputation
    TrustGate high_value_gate;
    high_value_gate.min_reputation = HIGH_REPUTATION_THRESHOLD;
    high_value_gate.max_gas_limit = 1000000;
    high_value_gate.require_cross_chain_verification = false;
    
    trust_gates["contract_deployment"] = high_value_gate;
    trust_gates["large_transfer"] = high_value_gate;
    
    // Medium-value operations require medium reputation
    TrustGate medium_value_gate;
    medium_value_gate.min_reputation = MEDIUM_REPUTATION_THRESHOLD;
    medium_value_gate.max_gas_limit = 500000;
    medium_value_gate.require_cross_chain_verification = false;
    
    trust_gates["contract_call"] = medium_value_gate;
    trust_gates["token_transfer"] = medium_value_gate;
    
    // Low-value operations require low reputation
    TrustGate low_value_gate;
    low_value_gate.min_reputation = LOW_REPUTATION_THRESHOLD;
    low_value_gate.max_gas_limit = 100000;
    low_value_gate.require_cross_chain_verification = false;
    
    trust_gates["data_storage"] = low_value_gate;
    trust_gates["event_emission"] = low_value_gate;
}

void TrustContext::InitializeDefaultAccessPolicies() {
    // Contract deployment requires high reputation
    SetAccessPolicy("contract", "deploy", HIGH_REPUTATION_THRESHOLD);
    
    // Storage operations require medium reputation
    SetAccessPolicy("storage", "write", MEDIUM_REPUTATION_THRESHOLD);
    SetAccessPolicy("storage", "read", LOW_REPUTATION_THRESHOLD);
    
    // Cross-chain operations require high reputation
    SetAccessPolicy("cross_chain", "send", HIGH_REPUTATION_THRESHOLD);
    SetAccessPolicy("cross_chain", "receive", MEDIUM_REPUTATION_THRESHOLD);
}

// TrustContextFactory Implementation

std::unique_ptr<TrustContext> TrustContextFactory::CreateContext(CVMDatabase* db, std::shared_ptr<TrustGraph> trust_graph) {
    auto context = std::make_unique<TrustContext>(db);
    context->SetTrustGraph(trust_graph);
    return context;
}

std::unique_ptr<TrustContext> TrustContextFactory::CreateTestContext() {
    return std::make_unique<TrustContext>();
}

std::unique_ptr<TrustContext> TrustContextFactory::CreateCrossChainContext(const std::vector<std::string>& supported_chains) {
    auto context = std::make_unique<TrustContext>();
    
    // Configure for cross-chain operations
    for (const auto& chain : supported_chains) {
        // Add chain-specific trust gates and policies
        (void)chain; // Suppress unused variable warning
    }
    
    return context;
}

// TrustContextUtils Implementation

namespace TrustContextUtils {

uint32_t CalculateWeightedReputation(const std::vector<uint32_t>& scores, const std::vector<uint32_t>& weights) {
    if (scores.empty() || scores.size() != weights.size()) {
        return 0;
    }
    
    uint64_t weighted_sum = 0;
    uint64_t total_weight = 0;
    
    for (size_t i = 0; i < scores.size(); ++i) {
        weighted_sum += scores[i] * weights[i];
        total_weight += weights[i];
    }
    
    if (total_weight == 0) {
        return 0;
    }
    
    return static_cast<uint32_t>(weighted_sum / total_weight);
}

uint32_t ApplyReputationDecay(uint32_t current_reputation, int64_t time_since_last_activity) {
    // Apply exponential decay: reputation decreases by 1% per day of inactivity
    double decay_factor = std::pow(0.99, time_since_last_activity / 86400.0);
    return static_cast<uint32_t>(current_reputation * decay_factor);
}

bool ValidateTrustGateConfiguration(const std::string& operation, uint32_t min_reputation, uint64_t max_gas) {
    // Validate that trust gate configuration is reasonable
    return min_reputation <= 100 && max_gas > 0 && max_gas <= 10000000;
}

bool VerifyTrustAttestation(const std::string& chain, const std::string& proof, 
                           const uint160& address, uint32_t claimed_reputation) {
    // Cross-chain trust verification using the cross-chain bridge
    
    // Validate inputs
    if (chain.empty() || proof.empty()) {
        LogPrint(BCLog::CVM, "TrustContextUtils: Empty chain or proof for attestation verification\n");
        return false;
    }
    
    if (claimed_reputation > 100) {
        LogPrint(BCLog::CVM, "TrustContextUtils: Invalid claimed reputation %d\n", claimed_reputation);
        return false;
    }
    
    // Map chain name to chain ID
    uint16_t chainId = 99; // Default to OTHER
    if (chain == "ethereum" || chain == "eth") {
        chainId = 1;
    } else if (chain == "polygon" || chain == "matic") {
        chainId = 2;
    } else if (chain == "arbitrum" || chain == "arb") {
        chainId = 3;
    } else if (chain == "optimism" || chain == "op") {
        chainId = 4;
    } else if (chain == "base") {
        chainId = 5;
    } else if (chain == "cascoin" || chain == "cas") {
        chainId = 0;
    }
    
    // Use the global cross-chain bridge if available
    if (g_crossChainBridge) {
        // Check if chain is supported
        if (!g_crossChainBridge->IsChainSupported(chainId)) {
            LogPrint(BCLog::CVM, "TrustContextUtils: Chain %s (id=%d) not supported\n", 
                     chain, chainId);
            return false;
        }
        
        // Get cross-chain trust scores for this address
        auto scores = g_crossChainBridge->GetCrossChainTrustScores(address);
        
        // Look for a matching verified score from this chain
        for (const auto& score : scores) {
            if (score.chainId == chainId && score.isVerified) {
                // Check if claimed reputation matches (with tolerance)
                int diff = std::abs(static_cast<int>(score.trustScore) - 
                                   static_cast<int>(claimed_reputation));
                if (diff <= 5) { // Allow 5 point tolerance
                    LogPrint(BCLog::CVM, "TrustContextUtils: Attestation verified for %s from %s "
                             "(claimed=%d, actual=%d)\n",
                             address.ToString(), chain, claimed_reputation, score.trustScore);
                    return true;
                }
            }
        }
        
        // No matching verified score found
        LogPrint(BCLog::CVM, "TrustContextUtils: No verified attestation found for %s from %s\n",
                 address.ToString(), chain);
    } else {
        LogPrint(BCLog::CVM, "TrustContextUtils: Cross-chain bridge not initialized\n");
    }
    
    return false;
}

uint64_t CalculateReputationGasDiscount(uint64_t base_gas, uint32_t reputation) {
    if (reputation >= 80) {
        return base_gas / 2; // 50% discount for high reputation
    } else if (reputation >= 60) {
        return (base_gas * 3) / 4; // 25% discount for medium reputation
    } else if (reputation >= 40) {
        return (base_gas * 9) / 10; // 10% discount for low reputation
    }
    
    return base_gas; // No discount for very low reputation
}

uint64_t CalculateFreeGasAllowance(uint32_t reputation, int64_t time_period) {
    if (reputation >= 80) {
        return 100000; // 100k gas per day for high reputation
    } else if (reputation >= 60) {
        return 50000; // 50k gas per day for medium reputation
    }
    
    return 0; // No free gas for low reputation
}

} // namespace TrustContextUtils

} // namespace CVM