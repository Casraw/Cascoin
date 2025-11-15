// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUST_ATTESTATION_H
#define CASCOIN_CVM_TRUST_ATTESTATION_H

#include <uint256.h>
#include <serialize.h>
#include <key.h>
#include <vector>
#include <string>

namespace CVM {

/**
 * Trust Attestation Source
 * 
 * Identifies the source chain/system for cross-chain trust attestations.
 */
enum class AttestationSource {
    CASCOIN_MAINNET = 0,
    ETHEREUM_MAINNET = 1,
    POLYGON = 2,
    ARBITRUM = 3,
    OPTIMISM = 4,
    BASE = 5,
    OTHER = 99
};

/**
 * Trust Attestation
 * 
 * Cross-chain trust score update that can be propagated across the network.
 * Used for synchronizing reputation scores from other chains or systems.
 */
struct TrustAttestation {
    uint160 address;                    // Address being attested
    int16_t trustScore;                 // Trust score (0-100)
    AttestationSource source;           // Source chain/system
    uint256 sourceChainId;              // Source chain ID
    uint64_t timestamp;                 // Attestation timestamp
    uint256 attestationHash;            // Hash of attestation data
    
    // Cryptographic proof
    std::vector<uint8_t> attestorPubKey;  // Attestor's public key
    std::vector<uint8_t> signature;       // Attestor's signature
    std::string proofData;                // Additional proof data (e.g., merkle proof)
    
    TrustAttestation() : trustScore(0), source(AttestationSource::OTHER),
                        timestamp(0) {}

    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(trustScore);
        READWRITE((int&)source);
        READWRITE(sourceChainId);
        READWRITE(timestamp);
        READWRITE(attestationHash);
        READWRITE(attestorPubKey);
        READWRITE(signature);
        READWRITE(proofData);
    }
    
    /**
     * Verify attestation signature
     */
    bool VerifySignature() const;
    
    /**
     * Generate attestation hash
     */
    uint256 GetHash() const;
};

} // namespace CVM

#endif // CASCOIN_CVM_TRUST_ATTESTATION_H
