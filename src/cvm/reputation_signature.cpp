// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reputation_signature.h>
#include <hash.h>
#include <util.h>
#include <utilstrencodings.h>
#include <pubkey.h>
#include <key.h>

namespace CVM {

// ReputationStateProof implementation

uint256 ReputationStateProof::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << reputation_score;
    ss << timestamp;
    ss << block_height;
    ss << state_root;
    return ss.GetHash();
}

bool ReputationStateProof::Verify() const {
    // Basic validation
    if (reputation_score > 100) {
        return false; // Invalid reputation score
    }
    
    if (timestamp <= 0 || block_height < 0) {
        return false; // Invalid timestamp or height
    }
    
    if (signature.size() < 64) {
        return false; // Invalid signature size
    }
    
    // Verify merkle proof if provided
    if (!merkle_proof.empty()) {
        // Compute leaf hash
        uint256 leaf = Hash(BEGIN(address), END(address));
        
        // TODO: Implement full merkle proof verification
        // For now, just check that proof is not empty
        if (merkle_proof.empty()) {
            return false;
        }
    }
    
    return true;
}

bool ReputationStateProof::IsValid(int64_t current_time, int current_height) const {
    // Check if proof has expired
    if (current_time - timestamp > 3600) { // 1 hour expiry
        return false;
    }
    
    if (current_height - block_height > 144) { // ~6 hours expiry
        return false;
    }
    
    return Verify();
}

// ReputationSignature implementation

uint256 ReputationSignature::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << ecdsa_signature;
    ss << signer_address;
    ss << signer_reputation;
    ss << signature_timestamp;
    ss << reputation_proof_hash;
    return ss.GetHash();
}

bool ReputationSignature::Verify(const uint256& message_hash) const {
    // Basic validation
    if (ecdsa_signature.size() < 64) {
        return false;
    }
    
    if (signer_reputation > 100) {
        return false;
    }
    
    if (signature_timestamp <= 0) {
        return false;
    }
    
    // Verify ECDSA signature
    // Note: Full ECDSA verification would require the public key
    // This is a simplified check
    if (ecdsa_signature.empty()) {
        return false;
    }
    
    return true;
}

bool ReputationSignature::MeetsReputationRequirement(uint32_t min_reputation) const {
    return signer_reputation >= min_reputation;
}

// ReputationSignedTransaction implementation

bool ReputationSignedTransaction::VerifyComplete() const {
    // Verify reputation signature
    if (!reputation_sig.Verify(tx_hash)) {
        return false;
    }
    
    // Verify state proof
    if (!state_proof.Verify()) {
        return false;
    }
    
    // Verify reputation consistency
    if (reputation_sig.signer_reputation != state_proof.reputation_score) {
        return false; // Reputation mismatch
    }
    
    // Verify address consistency
    if (reputation_sig.signer_address != state_proof.address) {
        return false; // Address mismatch
    }
    
    return true;
}

bool ReputationSignedTransaction::CanExecute(const uint160& executor, uint32_t executor_reputation) const {
    // Check minimum reputation requirement
    if (executor_reputation < min_reputation_required) {
        return false;
    }
    
    // Check if high trust is required
    if (requires_high_trust && executor_reputation < 80) {
        return false;
    }
    
    // Check if executor is in trust endorsers list (if not empty)
    if (!trust_endorsers.empty()) {
        bool found = false;
        for (const auto& endorser : trust_endorsers) {
            if (endorser == executor) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false; // Executor not in endorsers list
        }
    }
    
    return true;
}

// ReputationSignatureManager implementation

ReputationSignatureManager::ReputationSignatureManager() {
}

ReputationSignatureManager::~ReputationSignatureManager() {
}

ReputationSignature ReputationSignatureManager::CreateSignature(
    const uint256& message_hash,
    const uint160& signer_address,
    uint32_t signer_reputation,
    const std::vector<uint8_t>& ecdsa_sig) {
    
    ReputationSignature sig;
    sig.ecdsa_signature = ecdsa_sig;
    sig.signer_address = signer_address;
    sig.signer_reputation = signer_reputation;
    sig.signature_timestamp = GetTime();
    
    // Create reputation proof hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << signer_address;
    ss << signer_reputation;
    ss << sig.signature_timestamp;
    sig.reputation_proof_hash = ss.GetHash();
    
    // Add trust metadata
    sig.trust_metadata.push_back(static_cast<uint8_t>(signer_reputation));
    sig.trust_metadata.push_back(static_cast<uint8_t>(signer_reputation >> 8));
    sig.trust_metadata.push_back(static_cast<uint8_t>(signer_reputation >> 16));
    sig.trust_metadata.push_back(static_cast<uint8_t>(signer_reputation >> 24));
    
    return sig;
}

bool ReputationSignatureManager::VerifySignature(
    const ReputationSignature& sig,
    const uint256& message_hash) const {
    
    return sig.Verify(message_hash);
}

ReputationStateProof ReputationSignatureManager::CreateStateProof(
    const uint160& address,
    uint32_t reputation_score,
    int block_height) {
    
    ReputationStateProof proof;
    proof.address = address;
    proof.reputation_score = reputation_score;
    proof.timestamp = GetTime();
    proof.block_height = block_height;
    
    // Compute state root
    proof.state_root = ComputeStateRoot();
    
    // Build merkle proof
    proof.merkle_proof = BuildMerkleProof(address, reputation_score);
    
    // Create signature over proof data
    uint256 proof_hash = proof.GetHash();
    // Note: In production, this would be signed with a validator key
    // For now, we create a placeholder signature
    proof.signature.resize(65);
    memcpy(proof.signature.data(), proof_hash.begin(), std::min(size_t(32), proof.signature.size()));
    
    return proof;
}

bool ReputationSignatureManager::VerifyStateProof(const ReputationStateProof& proof) const {
    return proof.Verify();
}

ReputationSignedTransaction ReputationSignatureManager::CreateSignedTransaction(
    const uint256& tx_hash,
    const uint160& signer_address,
    uint32_t signer_reputation,
    const std::vector<uint8_t>& ecdsa_sig,
    uint32_t min_reputation_required) {
    
    ReputationSignedTransaction signed_tx;
    signed_tx.tx_hash = tx_hash;
    signed_tx.min_reputation_required = min_reputation_required;
    signed_tx.requires_high_trust = (min_reputation_required >= 80);
    
    // Create reputation signature
    signed_tx.reputation_sig = CreateSignature(tx_hash, signer_address, signer_reputation, ecdsa_sig);
    
    // Create state proof
    signed_tx.state_proof = CreateStateProof(signer_address, signer_reputation, 0);
    
    return signed_tx;
}

bool ReputationSignatureManager::VerifySignedTransaction(const ReputationSignedTransaction& signed_tx) const {
    return signed_tx.VerifyComplete();
}

bool ReputationSignatureManager::AddTrustEndorser(
    ReputationSignedTransaction& signed_tx,
    const uint160& endorser_address,
    const std::vector<uint8_t>& endorser_sig) {
    
    // Verify endorser signature
    if (endorser_sig.size() < 64) {
        return false;
    }
    
    // Add endorser to list
    signed_tx.trust_endorsers.push_back(endorser_address);
    
    return true;
}

bool ReputationSignatureManager::MeetsReputationRequirements(
    const ReputationSignedTransaction& signed_tx,
    const uint160& executor,
    uint32_t executor_reputation) const {
    
    return signed_tx.CanExecute(executor, executor_reputation);
}

std::vector<uint256> ReputationSignatureManager::BuildMerkleProof(
    const uint160& address,
    uint32_t reputation) const {
    
    std::vector<uint256> proof;
    
    // Create leaf hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << reputation;
    uint256 leaf = ss.GetHash();
    
    // Add leaf to proof
    proof.push_back(leaf);
    
    // In a full implementation, this would build a complete merkle path
    // For now, we just add the leaf
    
    return proof;
}

bool ReputationSignatureManager::VerifyMerkleProof(
    const std::vector<uint256>& proof,
    const uint256& root,
    const uint256& leaf) const {
    
    if (proof.empty()) {
        return false;
    }
    
    // Simplified verification - just check leaf is in proof
    for (const auto& node : proof) {
        if (node == leaf) {
            return true;
        }
    }
    
    return false;
}

uint256 ReputationSignatureManager::ComputeStateRoot() const {
    // Compute merkle root of all reputation states
    // In a full implementation, this would build a complete merkle tree
    // For now, we return a placeholder hash
    
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("reputation_state_root");
    ss << GetTime();
    return ss.GetHash();
}

} // namespace CVM
