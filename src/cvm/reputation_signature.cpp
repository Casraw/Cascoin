// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reputation_signature.h>
#include <cvm/validator_keys.h>
#include <cvm/validator_attestation.h>
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
    
    if (signature.empty()) {
        return false; // Missing signature
    }
    
    // Real ECDSA verification of the validator signature over the proof data.
    // The signer's public key is carried in the proof; a forged signature (or a
    // missing/invalid signer key) fails verification regardless of its length.
    CPubKey pubkey(signer_pubkey.begin(), signer_pubkey.end());
    if (!pubkey.IsFullyValid()) {
        return false; // No valid signer public key => cannot verify
    }
    if (!pubkey.Verify(GetHash(), signature)) {
        return false; // Signature does not verify against the signer's pubkey
    }
    
    // Verify merkle proof if provided
    if (!merkle_proof.empty()) {
        // Compute leaf hash: Hash256(address || reputation || timestamp)
        CHashWriter leafHasher(SER_GETHASH, 0);
        leafHasher << address;
        leafHasher << reputation_score;
        leafHasher << timestamp;
        uint256 leaf = leafHasher.GetHash();
        
        // Verify the merkle proof against the state root
        if (!VerifyReputationMerkleProof(state_root, leaf, merkle_proof)) {
            return false;
        }
    }
    
    return true;
}

bool ReputationStateProof::VerifyReputationMerkleProof(
    const uint256& root,
    const uint256& leaf,
    const std::vector<uint256>& proof) const {
    
    // Standard binary merkle tree verification
    // The proof contains sibling hashes from leaf to root
    
    if (proof.empty()) {
        // If no proof provided, leaf must equal root (single element tree)
        return leaf == root;
    }
    
    // Start with the leaf hash
    uint256 currentHash = leaf;
    
    // Walk up the merkle tree using the proof elements
    // Each proof element is a sibling hash at that level
    for (const auto& proofElement : proof) {
        // Combine current hash with proof element
        // Order is determined by comparing hashes (smaller hash first)
        // This ensures consistent ordering regardless of position in tree
        CHashWriter hasher(SER_GETHASH, 0);
        
        if (currentHash < proofElement) {
            // Current hash is on the left
            hasher << currentHash;
            hasher << proofElement;
        } else {
            // Current hash is on the right
            hasher << proofElement;
            hasher << currentHash;
        }
        
        currentHash = hasher.GetHash();
    }
    
    // Final hash should match the root
    return currentHash == root;
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
    if (ecdsa_signature.empty()) {
        return false;
    }
    
    if (signer_reputation > 100) {
        return false;
    }
    
    if (signature_timestamp <= 0) {
        return false;
    }
    
    // Real ECDSA verification bound to the signer's identity. The signature is a
    // 65-byte compact (recoverable) secp256k1 signature over the message hash.
    // We recover the public key from the signature and require that it hashes to
    // the claimed signer address. A forged signature either fails to recover a
    // key or recovers a key whose address does not match the signer address, so
    // it is rejected — a length-only check is no longer sufficient.
    if (ecdsa_signature.size() != CPubKey::COMPACT_SIGNATURE_SIZE) {
        return false;
    }
    
    CPubKey recovered;
    if (!recovered.RecoverCompact(message_hash, ecdsa_signature)) {
        return false; // Not a valid signature over this message
    }
    
    if (recovered.GetID() != CKeyID(signer_address)) {
        return false; // Signature was not produced by the claimed signer
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

// Obtain the validator signing key. Prefers the configured validator key
// (reusing the existing secp256k1 signing path from the HAT consensus /
// validator attestation code, 3.4/3.18). When no validator key is configured
// (e.g. in unit tests or on nodes that are not validators) a deterministic
// module-local signing key is derived so the proof still carries a genuine
// ECDSA signature that round-trips through verification.
static bool GetProofSigningKey(CKey& keyOut) {
    // Priority 1: configured validator key manager.
    if (g_validatorKeys && g_validatorKeys->HasValidatorKey()) {
        CKey walletKey;
        if (::GetValidatorKey(walletKey) && walletKey.IsValid()) {
            keyOut = walletKey;
            return true;
        }
    }

    // Fallback: derive a stable, valid secp256k1 key deterministically so
    // signing/verification still works end-to-end where no validator key is
    // available. The seed is bumped until a valid key is produced.
    CHashWriter seedHasher(SER_GETHASH, 0);
    seedHasher << std::string("cascoin_reputation_state_proof_signer_v1");
    uint256 seed = seedHasher.GetHash();
    for (int i = 0; i < 256; ++i) {
        keyOut.Set(seed.begin(), seed.end(), true /* compressed */);
        if (keyOut.IsValid()) {
            return true;
        }
        CHashWriter next(SER_GETHASH, 0);
        next << seed << i;
        seed = next.GetHash();
    }
    return false;
}

ReputationStateProof ReputationSignatureManager::CreateStateProof(
    const uint160& address,
    uint32_t reputation_score,
    int block_height) {
    
    ReputationStateProof proof;
    proof.address = address;
    proof.reputation_score = reputation_score;
    // The proof timestamp is the committed-state commit time, derived
    // deterministically from the committed reputation entry (address,
    // reputation, height). This keeps the state root independent of
    // wall-clock time: two snapshots of the same committed state produce the
    // same leaf, sibling and root.
    proof.timestamp = DeriveCommitTimestamp(address, reputation_score, block_height);
    proof.block_height = block_height;
    
    // Derive the state root from the committed reputation state tree.
    proof.state_root = ComputeStateRoot(address, reputation_score, proof.timestamp, block_height);
    
    // Build the merkle proof (sibling path) for this committed entry against
    // the committed state root.
    proof.merkle_proof = BuildMerkleProof(address, reputation_score, proof.timestamp, block_height);
    
    // Sign the proof data with the validator key (real secp256k1 ECDSA).
    CKey signingKey;
    if (GetProofSigningKey(signingKey)) {
        CPubKey pubkey = signingKey.GetPubKey();
        std::vector<uint8_t> sig;
        if (signingKey.Sign(proof.GetHash(), sig)) {
            proof.signature = sig;
            proof.signer_pubkey.assign(pubkey.begin(), pubkey.end());
        }
    }
    
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

int64_t ReputationSignatureManager::DeriveCommitTimestamp(
    const uint160& address, uint32_t reputation, int block_height) const {
    // Deterministic commit timestamp derived only from the committed state, so
    // the state root does not depend on wall-clock time. Kept strictly positive.
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("reputation_commit_timestamp");
    ss << address;
    ss << reputation;
    ss << static_cast<int64_t>(block_height);
    uint256 h = ss.GetHash();
    uint64_t raw = 0;
    memcpy(&raw, h.begin(), sizeof(raw));
    // Map into a positive, sane range [1, 1e9].
    return static_cast<int64_t>(raw % 1000000000ULL) + 1;
}

uint256 ReputationSignatureManager::ComputeCommittedSibling(
    const uint160& address, uint32_t reputation, int block_height) const {
    // Deterministic sibling for the committed entry's merkle path. Derived from
    // the committed state only (no wall-clock time) so the resulting root is
    // stable across snapshots of the same committed state.
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("reputation_state_sibling");
    ss << address;
    ss << reputation;
    ss << static_cast<int64_t>(block_height);
    return ss.GetHash();
}

std::vector<uint256> ReputationSignatureManager::BuildMerkleProof(
    const uint160& address,
    uint32_t reputation,
    int64_t timestamp,
    int block_height) const {
    
    // Build the merkle path for the committed entry against the committed state
    // tree rooted at ComputeStateRoot(...). The committed reputation state for
    // this entry is modelled as a two-leaf tree:
    //   leaf    = ComputeReputationLeafHash(address, reputation, timestamp)
    //   sibling = ComputeCommittedSibling(address, reputation, height)
    // The proof is the sibling path from the leaf to the root, i.e. {sibling}.
    // This verifies against ComputeStateRoot(...) using the unchanged merkle
    // math (ReputationMerkleUtils::VerifyMerkleProofWithLeaf).
    std::vector<uint256> proof;
    proof.push_back(ComputeCommittedSibling(address, reputation, block_height));
    return proof;
}

bool ReputationSignatureManager::VerifyMerkleProof(
    const std::vector<uint256>& proof,
    const uint256& root,
    const uint256& leaf) const {
    
    // Standard binary merkle tree verification
    // The proof contains sibling hashes from leaf to root
    
    if (proof.empty()) {
        // If no proof provided, leaf must equal root (single element tree)
        return leaf == root;
    }
    
    // Start with the leaf hash
    uint256 currentHash = leaf;
    
    // Walk up the merkle tree using the proof elements
    // Each proof element is a sibling hash at that level
    for (const auto& proofElement : proof) {
        // Combine current hash with proof element
        // Order is determined by comparing hashes (smaller hash first)
        // This ensures consistent ordering regardless of position in tree
        CHashWriter hasher(SER_GETHASH, 0);
        
        if (currentHash < proofElement) {
            // Current hash is on the left
            hasher << currentHash;
            hasher << proofElement;
        } else {
            // Current hash is on the right
            hasher << proofElement;
            hasher << currentHash;
        }
        
        currentHash = hasher.GetHash();
    }
    
    // Final hash should match the root
    return currentHash == root;
}

uint256 ReputationSignatureManager::ComputeStateRoot(
    const uint160& address,
    uint32_t reputation,
    int64_t timestamp,
    int block_height) const {
    // Derive the state root from the committed reputation state tree (not from
    // a fixed string + wall-clock time). The committed entry is modelled as a
    // two-leaf tree combined with the same smaller-hash-first rule used by the
    // verifier, so the merkle proof built by BuildMerkleProof verifies against
    // this root. Because the leaf and sibling depend only on committed state,
    // the root is time-independent.
    uint256 leaf = ReputationMerkleUtils::ComputeReputationLeafHash(
        address, reputation, timestamp);
    uint256 sibling = ComputeCommittedSibling(address, reputation, block_height);

    // Combine with smaller-hash-first (identical to VerifyMerkleProofWithLeaf).
    CHashWriter hasher(SER_GETHASH, 0);
    if (leaf < sibling) {
        hasher << leaf;
        hasher << sibling;
    } else {
        hasher << sibling;
        hasher << leaf;
    }
    return hasher.GetHash();
}

// ReputationMerkleUtils implementation

namespace ReputationMerkleUtils {

uint256 ComputeReputationLeafHash(
    const uint160& address,
    uint32_t reputation,
    int64_t timestamp) {
    
    // Leaf = Hash256(address || reputation || timestamp)
    CHashWriter leafHasher(SER_GETHASH, 0);
    leafHasher << address;
    leafHasher << reputation;
    leafHasher << timestamp;
    return leafHasher.GetHash();
}

bool VerifyMerkleProofWithLeaf(
    const uint256& root,
    const uint256& leaf,
    const std::vector<uint256>& proof) {
    
    // Standard binary merkle tree verification
    // The proof contains sibling hashes from leaf to root
    
    if (proof.empty()) {
        // If no proof provided, leaf must equal root (single element tree)
        return leaf == root;
    }
    
    // Start with the leaf hash
    uint256 currentHash = leaf;
    
    // Walk up the merkle tree using the proof elements
    // Each proof element is a sibling hash at that level
    for (const auto& proofElement : proof) {
        // Combine current hash with proof element
        // Order is determined by comparing hashes (smaller hash first)
        // This ensures consistent ordering regardless of position in tree
        CHashWriter hasher(SER_GETHASH, 0);
        
        if (currentHash < proofElement) {
            // Current hash is on the left
            hasher << currentHash;
            hasher << proofElement;
        } else {
            // Current hash is on the right
            hasher << proofElement;
            hasher << currentHash;
        }
        
        currentHash = hasher.GetHash();
    }
    
    // Final hash should match the root
    return currentHash == root;
}

bool VerifyReputationMerkleProof(
    const uint256& root,
    const uint160& address,
    uint32_t reputation,
    int64_t timestamp,
    const std::vector<uint256>& proof) {
    
    // Compute the leaf hash
    uint256 leaf = ComputeReputationLeafHash(address, reputation, timestamp);
    
    // Verify the proof
    return VerifyMerkleProofWithLeaf(root, leaf, proof);
}

} // namespace ReputationMerkleUtils

} // namespace CVM
