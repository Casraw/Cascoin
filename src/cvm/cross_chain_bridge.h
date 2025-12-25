// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CROSS_CHAIN_BRIDGE_H
#define CASCOIN_CVM_CROSS_CHAIN_BRIDGE_H

#include <uint256.h>
#include <serialize.h>
#include <key.h>
#include <cvm/trust_attestation.h>
#include <cvm/cvmdb.h>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <mutex>

namespace CVM {

/**
 * Chain Configuration
 * 
 * Configuration for a supported cross-chain bridge.
 */
struct ChainConfig {
    uint16_t chainId;                    // Chain identifier
    std::string chainName;               // Human-readable name
    uint256 chainSelector;               // Chain selector (for CCIP)
    std::string bridgeEndpoint;          // Bridge endpoint URL
    bool isActive;                       // Whether bridge is active
    uint64_t minConfirmations;           // Minimum confirmations required
    uint64_t maxAttestationAge;          // Maximum attestation age in seconds
    
    ChainConfig() : chainId(0), isActive(false), minConfirmations(12), maxAttestationAge(86400) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainId);
        READWRITE(chainName);
        READWRITE(chainSelector);
        READWRITE(bridgeEndpoint);
        READWRITE(isActive);
        READWRITE(minConfirmations);
        READWRITE(maxAttestationAge);
    }
};

/**
 * Chain Trust Score
 * 
 * Trust score from a specific chain.
 */
struct ChainTrustScore {
    uint16_t chainId;                    // Source chain ID
    uint8_t trustScore;                  // Trust score (0-100)
    uint64_t timestamp;                  // When score was recorded
    bool isVerified;                     // Whether score has been verified
    uint256 proofHash;                   // Hash of verification proof
    
    ChainTrustScore() : chainId(0), trustScore(0), timestamp(0), isVerified(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainId);
        READWRITE(trustScore);
        READWRITE(timestamp);
        READWRITE(isVerified);
        READWRITE(proofHash);
    }
};

/**
 * Trust State Proof
 * 
 * Cryptographic proof of trust state for cross-chain verification.
 */
struct TrustStateProof {
    uint160 address;                     // Address being proven
    uint8_t trustScore;                  // Trust score
    uint64_t blockHeight;                // Block height of proof
    uint256 blockHash;                   // Block hash
    uint256 stateRoot;                   // State root at block
    std::vector<uint256> merkleProof;    // Merkle proof path
    std::vector<uint8_t> signature;      // Signature from attestor
    
    TrustStateProof() : trustScore(0), blockHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(trustScore);
        READWRITE(blockHeight);
        READWRITE(blockHash);
        READWRITE(stateRoot);
        READWRITE(merkleProof);
        READWRITE(signature);
    }
    
    /**
     * Verify the merkle proof
     */
    bool VerifyMerkleProof() const;
    
    /**
     * Get hash of the proof
     */
    uint256 GetHash() const;
};

/**
 * Reputation Proof
 * 
 * Proof of reputation for CCIP verification.
 */
struct ReputationProof {
    uint160 address;                     // Address
    uint8_t reputation;                  // Reputation score
    uint64_t timestamp;                  // Timestamp
    uint64_t sourceChainSelector;        // Source chain selector
    std::vector<uint8_t> proof;          // Cryptographic proof
    std::vector<uint8_t> signature;      // Signature
    
    ReputationProof() : reputation(0), timestamp(0), sourceChainSelector(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(reputation);
        READWRITE(timestamp);
        READWRITE(sourceChainSelector);
        READWRITE(proof);
        READWRITE(signature);
    }
    
    /**
     * Verify the proof
     */
    bool Verify() const;
    
    /**
     * Get hash of the proof
     */
    uint256 GetHash() const;
};

/**
 * Cross-Chain Trust Bridge
 * 
 * Manages cross-chain trust attestations and verification.
 * Integrates with LayerZero and Chainlink CCIP protocols.
 */
class CrossChainTrustBridge {
public:
    CrossChainTrustBridge();
    explicit CrossChainTrustBridge(CVMDatabase* db);
    ~CrossChainTrustBridge();
    
    // ========== LayerZero Integration ==========
    
    /**
     * Send trust attestation via LayerZero
     * @param dstChainId Destination chain ID
     * @param address Address being attested
     * @param attestation Trust attestation to send
     * @return true if attestation was sent successfully
     */
    bool SendTrustAttestation(uint16_t dstChainId, const uint160& address, 
                             const TrustAttestation& attestation);
    
    /**
     * Receive trust attestation from LayerZero
     * @param srcChainId Source chain ID
     * @param attestation Received attestation
     */
    void ReceiveTrustAttestation(uint16_t srcChainId, const TrustAttestation& attestation);
    
    // ========== Chainlink CCIP Integration ==========
    
    /**
     * Verify reputation via CCIP
     * @param sourceChainSelector Source chain selector
     * @param address Address to verify
     * @param proof Reputation proof
     * @return true if reputation is verified
     */
    bool VerifyReputationViaCCIP(uint64_t sourceChainSelector, const uint160& address,
                                const ReputationProof& proof);
    
    /**
     * Send reputation proof via CCIP
     * @param destChainSelector Destination chain selector
     * @param proof Reputation proof to send
     */
    void SendReputationProofViaCCIP(uint64_t destChainSelector, const ReputationProof& proof);
    
    // ========== Trust State Proofs ==========
    
    /**
     * Generate trust state proof for an address
     * @param address Address to generate proof for
     * @return Trust state proof
     */
    TrustStateProof GenerateTrustStateProof(const uint160& address);
    
    /**
     * Verify trust state proof from another chain
     * @param proof Trust state proof to verify
     * @param sourceChain Source chain ID
     * @return true if proof is valid
     */
    bool VerifyTrustStateProof(const TrustStateProof& proof, uint16_t sourceChain);
    
    // ========== Trust Score Aggregation ==========
    
    /**
     * Aggregate trust scores from multiple chains
     * @param address Address to aggregate scores for
     * @param scores Vector of chain trust scores
     * @return Aggregated trust score (0-100)
     */
    uint8_t AggregateCrossChainTrust(const uint160& address, 
                                    const std::vector<ChainTrustScore>& scores);
    
    /**
     * Get all cross-chain trust scores for an address
     * @param address Address to query
     * @return Vector of chain trust scores
     */
    std::vector<ChainTrustScore> GetCrossChainTrustScores(const uint160& address) const;
    
    /**
     * Get aggregated cross-chain trust score
     * @param address Address to query
     * @return Aggregated trust score (0-100)
     */
    uint8_t GetAggregatedTrustScore(const uint160& address) const;
    
    // ========== Chain Reorganization Handling ==========
    
    /**
     * Handle chain reorganization
     * @param chainId Chain that reorganized
     * @param invalidatedBlocks Blocks that were invalidated
     */
    void HandleChainReorg(uint16_t chainId, const std::vector<uint256>& invalidatedBlocks);
    
    // ========== Chain Configuration ==========
    
    /**
     * Add supported chain configuration
     * @param config Chain configuration
     */
    void AddSupportedChain(const ChainConfig& config);
    
    /**
     * Get chain configuration
     * @param chainId Chain ID
     * @return Chain configuration (nullptr if not found)
     */
    const ChainConfig* GetChainConfig(uint16_t chainId) const;
    
    /**
     * Check if chain is supported
     * @param chainId Chain ID
     * @return true if chain is supported
     */
    bool IsChainSupported(uint16_t chainId) const;
    
    /**
     * Get all supported chains
     * @return Vector of supported chain IDs
     */
    std::vector<uint16_t> GetSupportedChains() const;
    
    // ========== Attestation Storage ==========
    
    /**
     * Store attestation in database
     * @param attestation Attestation to store
     * @return true if stored successfully
     */
    bool StoreAttestation(const TrustAttestation& attestation);
    
    /**
     * Get attestations for an address
     * @param address Address to query
     * @return Vector of attestations
     */
    std::vector<TrustAttestation> GetAttestations(const uint160& address) const;
    
    /**
     * Get latest attestation for an address from a specific chain
     * @param address Address to query
     * @param chainId Source chain ID
     * @return Latest attestation (empty if not found)
     */
    TrustAttestation GetLatestAttestation(const uint160& address, uint16_t chainId) const;
    
    /**
     * Prune old attestations
     * @param maxAge Maximum age in seconds
     * @return Number of attestations pruned
     */
    size_t PruneOldAttestations(uint64_t maxAge);
    
    // ========== Statistics ==========
    
    /**
     * Get total number of stored attestations
     */
    size_t GetAttestationCount() const;
    
    /**
     * Get number of attestations per chain
     */
    std::map<uint16_t, size_t> GetAttestationCountByChain() const;
    
private:
    CVMDatabase* database;
    std::map<uint16_t, ChainConfig> supportedChains;
    std::map<uint160, std::vector<ChainTrustScore>> crossChainTrustCache;
    mutable std::mutex cacheMutex;
    
    // Chain weight factors for aggregation
    std::map<uint16_t, double> chainWeights;
    
    /**
     * Initialize default supported chains
     */
    void InitializeDefaultChains();
    
    /**
     * Update trust cache for an address
     */
    void UpdateTrustCache(const uint160& address, const ChainTrustScore& score);
    
    /**
     * Validate attestation
     */
    bool ValidateAttestation(const TrustAttestation& attestation) const;
    
    /**
     * Calculate chain weight for aggregation
     */
    double GetChainWeight(uint16_t chainId) const;
};

// Global cross-chain bridge instance
extern std::unique_ptr<CrossChainTrustBridge> g_crossChainBridge;

/**
 * Initialize the global cross-chain bridge
 */
void InitializeCrossChainBridge(CVMDatabase* db);

/**
 * Shutdown the global cross-chain bridge
 */
void ShutdownCrossChainBridge();

} // namespace CVM

#endif // CASCOIN_CVM_CROSS_CHAIN_BRIDGE_H
