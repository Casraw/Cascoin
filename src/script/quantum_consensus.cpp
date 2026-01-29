// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file quantum_consensus.cpp
 * @brief Quantum registry functions for consensus library
 *
 * This file provides implementations of quantum registry functions that are
 * needed by the consensus library (interpreter.cpp). These are minimal
 * implementations that can be overridden by the full implementations in
 * quantum_registry.cpp when linking with libbitcoin_server.
 *
 * For cascoin-tx and other tools that don't need the full registry,
 * these stub implementations are sufficient.
 */

#include <quantum_registry_fwd.h>
#include <hash.h>

/**
 * Parse a quantum transaction witness
 * 
 * The witness format is:
 * - Registration (0x51): [marker][897-byte pubkey][signature]
 * - Reference (0x52): [marker][32-byte hash][signature]
 * 
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6
 */
QuantumWitnessData ParseQuantumWitness(const std::vector<std::vector<unsigned char>>& witness)
{
    QuantumWitnessData result;
    result.isValid = false;
    
    // Witness must have at least one element
    if (witness.empty()) {
        result.error = "Empty witness stack";
        return result;
    }
    
    // The quantum witness data is in the first element
    const std::vector<unsigned char>& witnessData = witness[0];
    
    // Minimum size: marker (1) + hash (32) + signature (1) = 34 bytes
    if (witnessData.size() < 34) {
        result.error = "Witness data too short";
        return result;
    }
    
    // Requirements: 4.3 - Read the first byte to determine transaction mode
    uint8_t marker = witnessData[0];
    
    // Requirements: 4.4 - Parse as Registration Transaction (0x51)
    if (marker == QUANTUM_WITNESS_MARKER_REGISTRATION) {
        result.isRegistration = true;
        
        // Minimum size for registration: marker (1) + pubkey (897) + signature (1) = 899 bytes
        if (witnessData.size() < 1 + QUANTUM_PUBKEY_SIZE + 1) {
            result.error = "Registration witness too short for public key";
            return result;
        }
        
        // Maximum size: marker (1) + pubkey (897) + max signature (700) = 1598 bytes
        if (witnessData.size() > 1 + QUANTUM_PUBKEY_SIZE + QUANTUM_MAX_SIGNATURE_SIZE) {
            result.error = "Registration witness exceeds maximum size";
            return result;
        }
        
        // Extract the 897-byte public key (bytes 1-897)
        result.pubkey.assign(witnessData.begin() + 1, 
                             witnessData.begin() + 1 + QUANTUM_PUBKEY_SIZE);
        
        // Compute the hash of the public key
        result.pubkeyHash = Hash(result.pubkey.begin(), result.pubkey.end());
        
        // Extract the signature (remaining bytes after pubkey)
        result.signature.assign(witnessData.begin() + 1 + QUANTUM_PUBKEY_SIZE,
                                witnessData.end());
        
        // Signature must be at least 1 byte
        if (result.signature.empty()) {
            result.error = "Registration witness missing signature";
            return result;
        }
        
        result.isValid = true;
        return result;
    }
    
    // Requirements: 4.5 - Parse as Reference Transaction (0x52)
    if (marker == QUANTUM_WITNESS_MARKER_REFERENCE) {
        result.isRegistration = false;
        
        // Minimum size for reference: marker (1) + hash (32) + signature (1) = 34 bytes
        if (witnessData.size() < 1 + QUANTUM_PUBKEY_HASH_SIZE + 1) {
            result.error = "Reference witness too short for hash";
            return result;
        }
        
        // Maximum size: marker (1) + hash (32) + max signature (700) = 733 bytes
        if (witnessData.size() > 1 + QUANTUM_PUBKEY_HASH_SIZE + QUANTUM_MAX_SIGNATURE_SIZE) {
            result.error = "Reference witness exceeds maximum size";
            return result;
        }
        
        // Extract the 32-byte hash (bytes 1-32)
        std::vector<unsigned char> hashBytes(witnessData.begin() + 1,
                                             witnessData.begin() + 1 + QUANTUM_PUBKEY_HASH_SIZE);
        
        // Convert to uint256 (note: uint256 stores in little-endian internally)
        result.pubkeyHash = uint256(hashBytes);
        
        // Extract the signature (remaining bytes after hash)
        result.signature.assign(witnessData.begin() + 1 + QUANTUM_PUBKEY_HASH_SIZE,
                                witnessData.end());
        
        // Signature must be at least 1 byte
        if (result.signature.empty()) {
            result.error = "Reference witness missing signature";
            return result;
        }
        
        // Public key is not included in reference transactions
        result.pubkey.clear();
        
        result.isValid = true;
        return result;
    }
    
    // Requirements: 4.6 - Invalid marker byte
    result.error = "Invalid quantum witness marker";
    return result;
}

/**
 * Lookup a public key from the global registry
 * 
 * This is a stub implementation for consensus-only builds.
 * The full implementation in quantum_registry.cpp will override this
 * when linking with libbitcoin_server.
 * 
 * Note: This function uses weak linkage so it can be overridden.
 */
bool __attribute__((weak)) LookupQuantumPubKey(const uint256& hash, std::vector<unsigned char>& pubkey)
{
    // In consensus-only builds, the registry is not available
    // Reference transactions (0x52) will fail validation
    return false;
}
