// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ECLIPSE_SYBIL_PROTECTION_H
#define CASCOIN_CVM_ECLIPSE_SYBIL_PROTECTION_H

#include <uint256.h>
#include <amount.h>
#include <netaddress.h>
#include <serialize.h>
#include <vector>
#include <map>
#include <set>

namespace CVM {

// Forward declarations
class CVMDatabase;
class TrustGraph;
class WalletClusterAnalyzer;

/**
 * Validator Network Info
 * 
 * Tracks network topology information for a validator.
 */
struct ValidatorNetworkInfo {
    uint160 address;                    // Validator address
    CNetAddr ipAddress;                 // IP address
    std::set<uint160> connectedPeers;   // Connected peer addresses
    uint64_t firstSeen;                 // Block height first seen
    uint32_t validationCount;           // Total validations performed
    uint32_t accurateValidations;       // Accurate validations
    
    ValidatorNetworkInfo() : firstSeen(0), validationCount(0), accurateValidations(0) {}
    
    double GetAccuracy() const {
        if (validationCount == 0) return 0.0;
        return static_cast<double>(accurateValidations) / validationCount;
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(ipAddress);
        READWRITE(connectedPeers);
        READWRITE(firstSeen);
        READWRITE(validationCount);
        READWRITE(accurateValidations);
    }
};

/**
 * Validator Stake Info
 * 
 * Tracks stake information for a validator.
 */
struct ValidatorStakeInfo {
    uint160 address;                    // Validator address
    CAmount totalStake;                 // Total staked amount
    std::vector<uint256> stakeTxs;      // Stake transaction hashes
    std::map<uint160, CAmount> stakeSources; // Source addresses -> amounts
    uint64_t oldestStakeAge;            // Age of oldest stake (blocks)
    
    ValidatorStakeInfo() : totalStake(0), oldestStakeAge(0) {}
    
    size_t GetStakeSourceCount() const {
        return stakeSources.size();
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(totalStake);
        READWRITE(stakeTxs);
        READWRITE(stakeSources);
        READWRITE(oldestStakeAge);
    }
};

/**
 * Sybil Detection Result
 * 
 * Result of Sybil network detection analysis.
 */
struct SybilDetectionResult {
    bool isSybilNetwork;                // Is this a Sybil network?
    double confidence;                  // Confidence level (0.0-1.0)
    std::vector<uint160> suspiciousValidators; // Suspicious validator addresses
    std::string reason;                 // Detection reason
    
    // Detection metrics
    bool hasTopologyCollusion;          // Same IP subnet
    bool hasPeerCollusion;              // High peer overlap
    bool hasStakeCollusion;             // Stake concentration
    bool hasBehavioralCollusion;        // Coordinated behavior
    bool hasWoTCollusion;               // WoT group isolation
    
    SybilDetectionResult() : isSybilNetwork(false), confidence(0.0),
                            hasTopologyCollusion(false), hasPeerCollusion(false),
                            hasStakeCollusion(false), hasBehavioralCollusion(false),
                            hasWoTCollusion(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isSybilNetwork);
        READWRITE(confidence);
        READWRITE(suspiciousValidators);
        READWRITE(reason);
        READWRITE(hasTopologyCollusion);
        READWRITE(hasPeerCollusion);
        READWRITE(hasStakeCollusion);
        READWRITE(hasBehavioralCollusion);
        READWRITE(hasWoTCollusion);
    }
};

/**
 * Eclipse/Sybil Protection Manager
 * 
 * Implements comprehensive protection against Eclipse attacks and Sybil networks
 * in the HAT v2 consensus validator selection process.
 * 
 * Protection Mechanisms:
 * 1. Network Topology Diversity - Validators from different IP subnets
 * 2. Peer Connection Diversity - Validators with diverse peer connections
 * 3. Validator History Requirements - Minimum block history
 * 4. Validation History - Minimum validation count and accuracy
 * 5. Stake Concentration Limits - No entity controls >20% of stake
 * 6. Cross-Validation Requirements - Minimum non-WoT validators
 * 7. Cross-Group Agreement - WoT and non-WoT must agree
 * 8. Stake Age Requirements - Stake must be aged
 * 9. Stake Source Diversity - Stake from multiple sources
 * 10. Behavioral Analysis - Detect coordinated patterns
 * 11. Automatic DAO Escalation - Escalate suspicious cases
 */
class EclipseSybilProtection {
public:
    // Configuration constants
    static constexpr uint32_t MIN_VALIDATOR_HISTORY_BLOCKS = 10000;  // ~17 hours
    static constexpr uint32_t MIN_VALIDATION_COUNT = 50;
    static constexpr double MIN_VALIDATION_ACCURACY = 0.85;          // 85%
    static constexpr double MAX_STAKE_CONCENTRATION = 0.20;          // 20%
    static constexpr double MIN_NON_WOT_VALIDATORS = 0.40;           // 40%
    static constexpr double MAX_CROSS_GROUP_DISAGREEMENT = 0.60;     // 60%
    static constexpr uint64_t MIN_STAKE_AGE_BLOCKS = 1000;           // ~1.7 hours
    static constexpr size_t MIN_STAKE_SOURCES = 3;
    static constexpr double MAX_PEER_OVERLAP = 0.50;                 // 50%
    static constexpr uint8_t IP_SUBNET_MASK = 16;                    // /16 subnet
    
    EclipseSybilProtection(CVMDatabase& db, TrustGraph& trustGraph);
    
    /**
     * Check if a validator meets all eligibility requirements
     * 
     * @param validatorAddr Validator address to check
     * @param currentHeight Current block height
     * @return true if validator is eligible, false otherwise
     */
    bool IsValidatorEligible(const uint160& validatorAddr, int currentHeight);
    
    /**
     * Detect Sybil network among a set of validators
     * 
     * @param validators Set of validator addresses
     * @param currentHeight Current block height
     * @return Detection result with confidence and reasons
     */
    SybilDetectionResult DetectSybilNetwork(
        const std::vector<uint160>& validators,
        int currentHeight
    );
    
    /**
     * Validate validator set diversity
     * 
     * Ensures the validator set has sufficient diversity across:
     * - Network topology (IP addresses)
     * - Peer connections
     * - Stake sources
     * - WoT vs non-WoT validators
     * 
     * @param validators Set of validator addresses
     * @param currentHeight Current block height
     * @return true if validator set is sufficiently diverse
     */
    bool ValidateValidatorSetDiversity(
        const std::vector<uint160>& validators,
        int currentHeight
    );
    
    /**
     * Update validator network information
     * 
     * @param validatorAddr Validator address
     * @param ipAddr IP address
     * @param peers Connected peer addresses
     * @param currentHeight Current block height
     */
    void UpdateValidatorNetworkInfo(
        const uint160& validatorAddr,
        const CNetAddr& ipAddr,
        const std::set<uint160>& peers,
        int currentHeight
    );
    
    /**
     * Update validator stake information
     * 
     * @param validatorAddr Validator address
     * @param stakeInfo Stake information
     */
    void UpdateValidatorStakeInfo(
        const uint160& validatorAddr,
        const ValidatorStakeInfo& stakeInfo
    );
    
    /**
     * Record validation result for accuracy tracking
     * 
     * @param validatorAddr Validator address
     * @param wasAccurate Was the validation accurate?
     */
    void RecordValidationResult(const uint160& validatorAddr, bool wasAccurate);
    
    /**
     * Get validator network information
     * 
     * @param validatorAddr Validator address
     * @param info Output parameter for network info
     * @return true if info found, false otherwise
     */
    bool GetValidatorNetworkInfo(const uint160& validatorAddr, ValidatorNetworkInfo& info);
    
    /**
     * Get validator stake information
     * 
     * @param validatorAddr Validator address
     * @param info Output parameter for stake info
     * @return true if info found, false otherwise
     */
    bool GetValidatorStakeInfo(const uint160& validatorAddr, ValidatorStakeInfo& info);
    
    /**
     * Check cross-group agreement
     * 
     * Ensures WoT and non-WoT validators agree within 60%.
     * 
     * @param validators Validator addresses
     * @param votes Validator votes
     * @return true if agreement within threshold
     */
    bool CheckCrossGroupAgreement(
        const std::vector<uint160>& validators,
        const std::map<uint160, int>& votes
    );

private:
    CVMDatabase& m_db;
    TrustGraph& m_trustGraph;
    
    // Database keys
    static const char DB_VALIDATOR_NETWORK = 'N';  // 'N' + address -> ValidatorNetworkInfo
    static const char DB_VALIDATOR_STAKE = 'K';    // 'K' + address -> ValidatorStakeInfo
    
    // Internal helper methods
    
    /**
     * Check network topology diversity
     * 
     * Ensures validators are from different IP subnets (/16).
     * 
     * @param validators Validator addresses
     * @return true if sufficiently diverse
     */
    bool CheckTopologyDiversity(const std::vector<uint160>& validators);
    
    /**
     * Check peer connection diversity
     * 
     * Ensures validators have <50% peer overlap.
     * 
     * @param validators Validator addresses
     * @return true if sufficiently diverse
     */
    bool CheckPeerDiversity(const std::vector<uint160>& validators);
    
    /**
     * Check validator history requirements
     * 
     * Ensures validators have minimum block history (10,000 blocks).
     * 
     * @param validatorAddr Validator address
     * @param currentHeight Current block height
     * @return true if meets requirements
     */
    bool CheckValidatorHistory(const uint160& validatorAddr, int currentHeight);
    
    /**
     * Check validation history requirements
     * 
     * Ensures validators have minimum validation count (50) and accuracy (85%).
     * 
     * @param validatorAddr Validator address
     * @return true if meets requirements
     */
    bool CheckValidationHistory(const uint160& validatorAddr);
    
    /**
     * Check stake concentration limits
     * 
     * Ensures no entity controls >20% of total validator stake.
     * 
     * @param validators Validator addresses
     * @return true if within limits
     */
    bool CheckStakeConcentration(const std::vector<uint160>& validators);
    
    /**
     * Check cross-validation requirements
     * 
     * Ensures minimum 40% non-WoT validators.
     * 
     * @param validators Validator addresses
     * @return true if meets requirements
     */
    bool CheckCrossValidationRequirements(const std::vector<uint160>& validators);
    

    
    /**
     * Check stake age requirements
     * 
     * Ensures validator stake is aged 1000+ blocks.
     * 
     * @param validatorAddr Validator address
     * @return true if meets requirements
     */
    bool CheckStakeAge(const uint160& validatorAddr);
    
    /**
     * Check stake source diversity
     * 
     * Ensures stake comes from 3+ different sources.
     * 
     * @param validatorAddr Validator address
     * @return true if meets requirements
     */
    bool CheckStakeSourceDiversity(const uint160& validatorAddr);
    
    /**
     * Detect coordinated behavioral patterns
     * 
     * Analyzes transaction timing and patterns for coordination.
     * 
     * @param validators Validator addresses
     * @return true if coordinated behavior detected
     */
    bool DetectCoordinatedBehavior(const std::vector<uint160>& validators);
    
    /**
     * Calculate peer overlap between two validators
     * 
     * @param peers1 First validator's peers
     * @param peers2 Second validator's peers
     * @return Overlap ratio (0.0-1.0)
     */
    double CalculatePeerOverlap(
        const std::set<uint160>& peers1,
        const std::set<uint160>& peers2
    );
    
    /**
     * Get IP subnet for an address
     * 
     * @param ipAddr IP address
     * @return Subnet identifier (first 16 bits)
     */
    uint32_t GetIPSubnet(const CNetAddr& ipAddr);
    
    /**
     * Check if validator has WoT connection
     * 
     * @param validatorAddr Validator address
     * @return true if has WoT connection
     */
    bool HasWoTConnection(const uint160& validatorAddr);
};

} // namespace CVM

#endif // CASCOIN_CVM_ECLIPSE_SYBIL_PROTECTION_H
