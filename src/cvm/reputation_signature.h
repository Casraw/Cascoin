// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_REPUTATION_SIGNATURE_H
#define CASCOIN_CVM_REPUTATION_SIGNATURE_H

#include <uint256.h>
#include <serialize.h>
#include <vector>
#include <string>

namespace CVM {

/**
 * Reputation State Proof
 * 
 * Cryptographic proof of an address's reputation state at a specific time
 */
struct ReputationStateProof {
    uint160 address;                    // Address whose reputation is proven
    uint32_t reputation_score;          // Reputation score at proof time
    int64_t timestamp;                  // When the proof was created
    int block_height;                   // Block height at proof time
    uint256 state_root;                 // Merkle root of reputation state
    std::vector<uint256> merkle_proof;  // Merkle proof path
    std::vector<uint8_t> signature;     // Signature over proof data
    
    ReputationStateProof() 
        : reputation_score(0), timestamp(0), block_height(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(reputation_score);
        READWRITE(timestamp);
        READWRITE(block_height);
        READWRITE(state_root);
        READWRITE(merkle_proof);
        READWRITE(signature);
    }
    
    // Get hash of proof data (for signing)
    uint256 GetHash() const;
    
    // Verify proof validity
    bool Verify() const;
    
    // Verify merkle proof for reputation data
    // Standard binary merkle tree verification
    // Leaf = Hash256(address || reputation || timestamp)
    bool VerifyReputationMerkleProof(
        const uint256& root,
        const uint256& leaf,
        const std::vector<uint256>& proof) const;
    
    // Check if proof is still valid (not expired)
    bool IsValid(int64_t current_time, int current_height) const;
};

/**
 * Reputation Signature
 * 
 * Enhanced signature that includes reputation context
 */
struct ReputationSignature {
    std::vector<uint8_t> ecdsa_signature;  // Standard ECDSA signature
    uint160 signer_address;                // Address of signer
    uint32_t signer_reputation;            // Reputation at signing time
    int64_t signature_timestamp;           // When signature was created
    uint256 reputation_proof_hash;         // Hash of reputation state proof
    std::vector<uint8_t> trust_metadata;   // Additional trust metadata
    
    ReputationSignature() 
        : signer_reputation(0), signature_timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ecdsa_signature);
        READWRITE(signer_address);
        READWRITE(signer_reputation);
        READWRITE(signature_timestamp);
        READWRITE(reputation_proof_hash);
        READWRITE(trust_metadata);
    }
    
    // Get hash of signature data
    uint256 GetHash() const;
    
    // Verify signature with reputation context
    bool Verify(const uint256& message_hash) const;
    
    // Check if signature meets minimum reputation requirement
    bool MeetsReputationRequirement(uint32_t min_reputation) const;
};

/**
 * Reputation-Signed Transaction
 * 
 * Transaction with reputation signature for enhanced trust verification
 */
struct ReputationSignedTransaction {
    uint256 tx_hash;                       // Hash of underlying transaction
    ReputationSignature reputation_sig;    // Reputation signature
    ReputationStateProof state_proof;      // Proof of reputation state
    std::vector<uint160> trust_endorsers;  // Addresses endorsing this transaction
    uint32_t min_reputation_required;      // Minimum reputation to execute
    bool requires_high_trust;              // Flag for high-trust operations
    
    ReputationSignedTransaction() 
        : min_reputation_required(0), requires_high_trust(false) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tx_hash);
        READWRITE(reputation_sig);
        READWRITE(state_proof);
        READWRITE(trust_endorsers);
        READWRITE(min_reputation_required);
        READWRITE(requires_high_trust);
    }
    
    // Verify all signatures and proofs
    bool VerifyComplete() const;
    
    // Check if transaction can be executed by given address
    bool CanExecute(const uint160& executor, uint32_t executor_reputation) const;
};

/**
 * Reputation Signature Manager
 * 
 * Manages creation and verification of reputation signatures
 */
class ReputationSignatureManager {
public:
    ReputationSignatureManager();
    ~ReputationSignatureManager();
    
    // Create reputation signature for message
    ReputationSignature CreateSignature(
        const uint256& message_hash,
        const uint160& signer_address,
        uint32_t signer_reputation,
        const std::vector<uint8_t>& ecdsa_sig
    );
    
    // Verify reputation signature
    bool VerifySignature(
        const ReputationSignature& sig,
        const uint256& message_hash
    ) const;
    
    // Create reputation state proof
    ReputationStateProof CreateStateProof(
        const uint160& address,
        uint32_t reputation_score,
        int block_height
    );
    
    // Verify reputation state proof
    bool VerifyStateProof(const ReputationStateProof& proof) const;
    
    // Create reputation-signed transaction
    ReputationSignedTransaction CreateSignedTransaction(
        const uint256& tx_hash,
        const uint160& signer_address,
        uint32_t signer_reputation,
        const std::vector<uint8_t>& ecdsa_sig,
        uint32_t min_reputation_required = 0
    );
    
    // Verify reputation-signed transaction
    bool VerifySignedTransaction(const ReputationSignedTransaction& signed_tx) const;
    
    // Add trust endorser to transaction
    bool AddTrustEndorser(
        ReputationSignedTransaction& signed_tx,
        const uint160& endorser_address,
        const std::vector<uint8_t>& endorser_sig
    );
    
    // Check if transaction meets reputation requirements
    bool MeetsReputationRequirements(
        const ReputationSignedTransaction& signed_tx,
        const uint160& executor,
        uint32_t executor_reputation
    ) const;
    
    // Get reputation proof expiry time (in seconds)
    static constexpr int64_t PROOF_EXPIRY_TIME = 3600; // 1 hour
    
    // Get reputation proof expiry blocks
    static constexpr int PROOF_EXPIRY_BLOCKS = 144; // ~6 hours at 2.5 min blocks
    
private:
    // Build merkle proof for reputation state
    std::vector<uint256> BuildMerkleProof(const uint160& address, uint32_t reputation) const;
    
    // Verify merkle proof
    bool VerifyMerkleProof(
        const std::vector<uint256>& proof,
        const uint256& root,
        const uint256& leaf
    ) const;
    
    // Compute reputation state root
    uint256 ComputeStateRoot() const;
};

/**
 * Utility functions for reputation merkle proof verification
 */
namespace ReputationMerkleUtils {
    /**
     * Verify a reputation merkle proof
     * 
     * Standard binary merkle tree verification where:
     * - Leaf = Hash256(address || reputation || timestamp)
     * - Proof contains sibling hashes from leaf to root
     * - Hash ordering: smaller hash is always on the left
     * 
     * @param root The merkle root to verify against
     * @param address The address whose reputation is being proven
     * @param reputation The reputation score being proven
     * @param timestamp The timestamp of the reputation snapshot
     * @param proof The merkle proof (sibling hashes from leaf to root)
     * @return true if the proof is valid
     */
    bool VerifyReputationMerkleProof(
        const uint256& root,
        const uint160& address,
        uint32_t reputation,
        int64_t timestamp,
        const std::vector<uint256>& proof);
    
    /**
     * Compute the leaf hash for a reputation entry
     * 
     * @param address The address
     * @param reputation The reputation score
     * @param timestamp The timestamp
     * @return The leaf hash
     */
    uint256 ComputeReputationLeafHash(
        const uint160& address,
        uint32_t reputation,
        int64_t timestamp);
    
    /**
     * Verify a generic merkle proof given a pre-computed leaf hash
     * 
     * @param root The merkle root to verify against
     * @param leaf The pre-computed leaf hash
     * @param proof The merkle proof (sibling hashes from leaf to root)
     * @return true if the proof is valid
     */
    bool VerifyMerkleProofWithLeaf(
        const uint256& root,
        const uint256& leaf,
        const std::vector<uint256>& proof);
}

} // namespace CVM

#endif // CASCOIN_CVM_REPUTATION_SIGNATURE_H
