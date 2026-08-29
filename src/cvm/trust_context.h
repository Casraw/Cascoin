// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUST_CONTEXT_H
#define CASCOIN_CVM_TRUST_CONTEXT_H

#include <uint256.h>
#include <serialize.h>
#include <pubkey.h>
#include <map>
#include <string>
#include <vector>
#include <memory>

namespace CVM {

// Forward declarations
class CVMDatabase;
class TrustGraph;

/**
 * Trust Context Manager
 * 
 * Manages trust and reputation context for smart contract execution.
 * Provides automatic trust context injection, reputation-based operation
 * gating, and cross-chain trust aggregation.
 */
class TrustContext {
public:
    TrustContext();
    explicit TrustContext(CVMDatabase* db);
    ~TrustContext();

    // Trust context management
    void SetDatabase(CVMDatabase* db) { database = db; }
    void SetTrustGraph(std::shared_ptr<TrustGraph> graph) { trust_graph = graph; }
    
    // Reputation queries
    uint32_t GetReputation(const uint160& address) const;
    uint32_t GetWeightedReputation(const uint160& address, const uint160& observer) const;
    bool HasMinimumReputation(const uint160& address, uint32_t min_reputation) const;
    
    // Trust-gated operations
    bool CheckTrustGate(const uint160& address, const std::string& operation) const;
    bool IsHighReputationAddress(const uint160& address) const;
    bool CanPerformOperation(const uint160& address, const std::string& operation, uint64_t gas_limit) const;
    
    // Trust context injection
    void InjectTrustContext(const uint160& caller, const uint160& contract);
    void SetCallerReputation(uint32_t reputation) { caller_reputation = reputation; }
    void SetContractReputation(uint32_t reputation) { contract_reputation = reputation; }
    
    // Getters for current context
    uint32_t GetCallerReputation() const { return caller_reputation; }
    uint32_t GetContractReputation() const { return contract_reputation; }
    
    // Cross-chain trust
    void AddCrossChainAttestation(const uint160& address, const std::string& chain, uint32_t reputation);
    uint32_t GetCrossChainReputation(const uint160& address, const std::string& chain) const;
    uint32_t GetAggregatedReputation(const uint160& address) const;
    
    // Trust history and tracking
    void RecordReputationChange(const uint160& address, uint32_t old_reputation, uint32_t new_reputation, const std::string& reason);
    
    // Gas and resource management
    uint64_t ApplyReputationGasDiscount(uint64_t base_gas, const uint160& address) const;
    uint64_t GetGasAllowance(const uint160& address) const;
    bool HasFreeGasEligibility(const uint160& address) const;
    
    // Trust-aware data structures

    
    struct TrustWeightedValue {
        uint256 value;
        uint32_t trust_weight;
        uint160 source_address;
        int64_t timestamp;
        
        ADD_SERIALIZE_METHODS;
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(value);
            READWRITE(trust_weight);
            READWRITE(source_address);
            READWRITE(timestamp);
        }
    };
    
    struct ReputationEvent {
        uint160 address;
        uint32_t old_reputation;
        uint32_t new_reputation;
        std::string reason;
        int64_t timestamp;
        uint256 tx_hash;
        
        ADD_SERIALIZE_METHODS;
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(address);
            READWRITE(old_reputation);
            READWRITE(new_reputation);
            READWRITE(reason);
            READWRITE(timestamp);
            READWRITE(tx_hash);
        }
    };
    
    // Method that uses ReputationEvent (must be declared after the struct)
    std::vector<ReputationEvent> GetReputationHistory(const uint160& address) const;
    
    struct CrossChainAttestation {
        std::string source_chain;
        uint32_t reputation;
        int64_t timestamp;
        std::string proof_hash;
        bool verified;
        
        ADD_SERIALIZE_METHODS;
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(source_chain);
            READWRITE(reputation);
            READWRITE(timestamp);
            READWRITE(proof_hash);
            READWRITE(verified);
        }
    };
    
    // Trust-enhanced operations
    void AddTrustWeightedValue(const std::string& key, const TrustWeightedValue& value);
    std::vector<TrustWeightedValue> GetTrustWeightedValues(const std::string& key) const;
    TrustWeightedValue GetHighestTrustValue(const std::string& key) const;
    
    // Reputation-based access control
    bool CheckAccessLevel(const uint160& address, const std::string& resource, const std::string& action) const;
    void SetAccessPolicy(const std::string& resource, const std::string& action, uint32_t min_reputation);
    
    // Trust decay and maintenance
    void ApplyReputationDecay(int64_t current_time);
    void UpdateReputationFromActivity(const uint160& address, const std::string& activity_type, int32_t reputation_delta);
    
    // Serialization
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(caller_reputation);
        READWRITE(contract_reputation);
        READWRITE(trust_weighted_data);
        READWRITE(cross_chain_attestations);
        READWRITE(reputation_history);
        READWRITE(access_policies);
    }

private:
    // Database and trust graph
    CVMDatabase* database;
    std::shared_ptr<TrustGraph> trust_graph;
    
    // Current execution context
    uint32_t caller_reputation;
    uint32_t contract_reputation;
    uint160 current_caller;
    uint160 current_contract;
    
    // Trust-weighted data storage
    std::map<std::string, std::vector<TrustWeightedValue>> trust_weighted_data;
    
    // Cross-chain trust attestations
    std::map<uint160, std::vector<CrossChainAttestation>> cross_chain_attestations;
    
    // Reputation history tracking
    std::map<uint160, std::vector<ReputationEvent>> reputation_history;
    
    // Access control policies
    struct AccessPolicy {
        uint32_t min_reputation;
        std::vector<std::string> required_attestations;
        uint64_t cooldown_period;
    };
    std::map<std::string, std::map<std::string, AccessPolicy>> access_policies; // resource -> action -> policy
    
    // Trust gate configurations
    struct TrustGate {
        uint32_t min_reputation;
        uint64_t max_gas_limit;
        std::vector<std::string> allowed_operations;
        bool require_cross_chain_verification;
    };
    std::map<std::string, TrustGate> trust_gates;
    
    // Helper methods
    uint32_t CalculateReputationScore(const uint160& address) const;
    bool VerifyCrossChainAttestation(const CrossChainAttestation& attestation) const;
    void InitializeDefaultTrustGates();
    void InitializeDefaultAccessPolicies();
    
    // Cross-chain trust verification methods
    bool VerifyCrossChainTrust(const CrossChainAttestation& attestation) const;
    bool VerifyLayerZeroAttestation(const CrossChainAttestation& attestation, uint16_t chainId) const;
    bool VerifyCCIPAttestation(const CrossChainAttestation& attestation, uint16_t chainId) const;
    bool IsKnownLayerZeroOracle(const CPubKey& pubkey, uint16_t chainId) const;
    bool IsKnownLayerZeroRelayer(const CPubKey& pubkey, uint16_t chainId) const;
    bool IsKnownChainlinkDON(const CPubKey& pubkey, uint16_t chainId) const;
    bool IsCCIPMessageProcessed(const uint256& messageId) const;
    
    // Constants
    static constexpr uint32_t HIGH_REPUTATION_THRESHOLD = 80;
    static constexpr uint32_t MEDIUM_REPUTATION_THRESHOLD = 60;
    static constexpr uint32_t LOW_REPUTATION_THRESHOLD = 40;
    static constexpr uint64_t FREE_GAS_REPUTATION_THRESHOLD = 80;
    static constexpr uint64_t REPUTATION_DECAY_PERIOD = 86400; // 24 hours in seconds
};

/**
 * Trust Context Factory
 * 
 * Creates and manages trust contexts for different execution environments
 */
class TrustContextFactory {
public:
    static std::unique_ptr<TrustContext> CreateContext(CVMDatabase* db, std::shared_ptr<TrustGraph> trust_graph);
    static std::unique_ptr<TrustContext> CreateTestContext();
    static std::unique_ptr<TrustContext> CreateCrossChainContext(const std::vector<std::string>& supported_chains);
};

/**
 * Trust Context Utilities
 */
namespace TrustContextUtils {
    // Reputation calculation helpers
    uint32_t CalculateWeightedReputation(const std::vector<uint32_t>& scores, const std::vector<uint32_t>& weights);
    uint32_t ApplyReputationDecay(uint32_t current_reputation, int64_t time_since_last_activity);
    
    // Trust gate validation
    bool ValidateTrustGateConfiguration(const std::string& operation, uint32_t min_reputation, uint64_t max_gas);
    
    // Cross-chain trust verification
    bool VerifyTrustAttestation(const std::string& chain, const std::string& proof, const uint160& address, uint32_t claimed_reputation);
    
    // Gas calculation helpers
    uint64_t CalculateReputationGasDiscount(uint64_t base_gas, uint32_t reputation);
    uint64_t CalculateFreeGasAllowance(uint32_t reputation, int64_t time_period);
}

} // namespace CVM

#endif // CASCOIN_CVM_TRUST_CONTEXT_H