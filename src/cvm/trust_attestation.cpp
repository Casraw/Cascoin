// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_attestation.h>
#include <hash.h>
#include <pubkey.h>
#include <util.h>

namespace CVM {

bool TrustAttestation::VerifySignature() const {
    if (attestorPubKey.empty() || signature.empty()) {
        return false;
    }
    
    try {
        // Create public key from bytes
        CPubKey pubkey(attestorPubKey.begin(), attestorPubKey.end());
        if (!pubkey.IsValid()) {
            LogPrint(BCLog::NET, "Trust Attestation: Invalid public key\n");
            return false;
        }
        
        // Get attestation hash
        uint256 hash = GetHash();
        
        // Verify signature
        return pubkey.Verify(hash, signature);
    } catch (const std::exception& e) {
        LogPrintf("Trust Attestation: Signature verification error: %s\n", e.what());
        return false;
    }
}

uint256 TrustAttestation::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << trustScore;
    ss << (int)source;
    ss << sourceChainId;
    ss << timestamp;
    ss << proofData;
    return ss.GetHash();
}

} // namespace CVM
