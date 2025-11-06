// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_context.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <util.h>
#include <timedata.h>
#include <algorithm>
#include <numeric>

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
        return it->second;
    }
    return {};
}

uint64_t TrustContext::ApplyReputationGasDiscount(uint64_t base_gas, const uint160& address) const {
    uint32_t reputation = GetReputation(address);
    return TrustContextUtils::CalculateReputationGasDiscount(base_gas, reputation);
}

uint64_t TrustContext::GetGasAllowance(const uint160& address) const {
    uint32_t reputation = GetReputation(address);
    return TrustContextUtils::CalculateFreeGasAllowance(reputation, 86400); // 24 hours
}

bool TrustContext::HasFreeGasEligibility(const uint160& address) const {
    return GetReputation(address) >= FREE_GAS_REPUTATION_THRESHOLD;
}

void TrustContext::AddTrustWeightedValue(const std::string& key, const TrustWeightedValue& value) {
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
}

std::vector<TrustContext::TrustWeightedValue> TrustContext::GetTrustWeightedValues(const std::string& key) const {
    auto it = trust_weighted_data.find(key);
    if (it != trust_weighted_data.end()) {
        return it->second;
    }
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
        return true; // No policy configured, allow access
    }
    
    auto action_it = resource_it->second.find(action);
    if (action_it == resource_it->second.end()) {
        return true; // No policy for this action, allow access
    }
    
    const AccessPolicy& policy = action_it->second;
    uint32_t reputation = GetReputation(address);
    
    return reputation >= policy.min_reputation;
}

void TrustContext::SetAccessPolicy(const std::string& resource, const std::string& action, uint32_t min_reputation) {
    AccessPolicy policy;
    policy.min_reputation = min_reputation;
    policy.cooldown_period = 0;
    
    access_policies[resource][action] = policy;
}

void TrustContext::ApplyReputationDecay(int64_t current_time) {
    // Apply decay to all tracked addresses
    // This would typically be called periodically by the system
    for (auto& history_pair : reputation_history) {
        const uint160& address = history_pair.first;
        auto& events = history_pair.second;
        
        if (!events.empty()) {
            const auto& last_event = events.back();
            int64_t time_since_last = current_time - last_event.timestamp;
            
            if (time_since_last > REPUTATION_DECAY_PERIOD) {
                uint32_t current_reputation = GetReputation(address);
                uint32_t decayed_reputation = TrustContextUtils::ApplyReputationDecay(current_reputation, time_since_last);
                
                if (decayed_reputation != current_reputation) {
                    RecordReputationChange(address, current_reputation, decayed_reputation, "automatic_decay");
                }
            }
        }
    }
}

void TrustContext::UpdateReputationFromActivity(const uint160& address, const std::string& activity_type, int32_t reputation_delta) {
    uint32_t current_reputation = GetReputation(address);
    uint32_t new_reputation = std::max(0, std::min(100, static_cast<int32_t>(current_reputation) + reputation_delta));
    
    if (new_reputation != current_reputation) {
        RecordReputationChange(address, current_reputation, new_reputation, activity_type);
    }
}

uint32_t TrustContext::CalculateReputationScore(const uint160& address) const {
    if (!database) {
        return 0;
    }
    
    // This would integrate with the existing reputation system
    // For now, return a placeholder value
    return 50; // Default medium reputation
}

bool TrustContext::VerifyCrossChainAttestation(const CrossChainAttestation& attestation) const {
    // TODO: Implement cross-chain verification logic
    // This would verify cryptographic proofs from other chains
    return false;
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
    // TODO: Implement cross-chain trust verification
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