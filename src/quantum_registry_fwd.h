// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QUANTUM_REGISTRY_FWD_H
#define BITCOIN_QUANTUM_REGISTRY_FWD_H

/**
 * @file quantum_registry_fwd.h
 * @brief Forward declarations for FALCON-512 Public Key Registry
 *
 * This header provides forward declarations and minimal types needed by
 * the consensus library (interpreter.cpp) without requiring LevelDB.
 * Include quantum_registry.h for the full implementation.
 */

#include <uint256.h>
#include <vector>
#include <string>

/**
 * Marker bytes for quantum witness format
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5
 */
static const uint8_t QUANTUM_WITNESS_MARKER_REGISTRATION = 0x51;
static const uint8_t QUANTUM_WITNESS_MARKER_REFERENCE = 0x52;

/**
 * Size constants for quantum transactions
 */
static const size_t QUANTUM_PUBKEY_HASH_SIZE = 32;
static const size_t QUANTUM_PUBKEY_SIZE = 897;
static const size_t QUANTUM_MAX_SIGNATURE_SIZE = 700;

/**
 * Result of parsing a quantum witness
 * Requirements: 4.1-4.6
 */
struct QuantumWitnessData {
    bool isValid;
    bool isRegistration;  // true = 0x51, false = 0x52
    uint256 pubkeyHash;
    std::vector<unsigned char> pubkey;  // Only populated for registration
    std::vector<unsigned char> signature;
    std::string error;
    
    QuantumWitnessData() : isValid(false), isRegistration(false) {}
};

/**
 * Parse a quantum transaction witness
 * @param witness The witness stack
 * @return QuantumWitnessData with parsed information
 * Requirements: 4.1-4.6
 * 
 * Implementation in script/quantum_consensus.cpp
 */
QuantumWitnessData ParseQuantumWitness(const std::vector<std::vector<unsigned char>>& witness);

/**
 * Lookup a public key from the global registry
 * This is a convenience function that wraps g_quantumRegistry->LookupPubKey()
 * and handles the case where the registry is not initialized.
 * 
 * @param hash The 32-byte SHA256 hash of the public key
 * @param pubkey Output: the full public key if found
 * @return true if found, false if not found or registry not initialized
 */
bool LookupQuantumPubKey(const uint256& hash, std::vector<unsigned char>& pubkey);

#endif // BITCOIN_QUANTUM_REGISTRY_FWD_H
