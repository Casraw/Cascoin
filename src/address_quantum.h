// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADDRESS_QUANTUM_H
#define BITCOIN_ADDRESS_QUANTUM_H

#include <pubkey.h>
#include <script/standard.h>
#include <uint256.h>

#include <string>
#include <vector>

class CChainParams;

/**
 * @file address_quantum.h
 * @brief Quantum address encoding and decoding for FALCON-512 public keys
 *
 * This module provides functions for encoding and decoding quantum-resistant
 * addresses using Bech32m encoding with witness version 2.
 *
 * Address format:
 * - HRP: "casq" (mainnet), "tcasq" (testnet), "rcasq" (regtest)
 * - Witness version: 2
 * - Program: SHA256(pubkey)[0:32] (32 bytes)
 *
 * Requirements: 3.1-3.9 (Quantum Address Format)
 */

namespace address {

/**
 * Witness version for quantum addresses.
 * Witness version 2 is designated for quantum transactions.
 */
static constexpr int QUANTUM_WITNESS_VERSION = 2;

/**
 * Size of the quantum address program (SHA256 hash of public key).
 */
static constexpr size_t QUANTUM_PROGRAM_SIZE = 32;

/**
 * Result structure for address decoding.
 * Contains all information needed to determine address type and route
 * to appropriate verification logic.
 *
 * Requirements: 3.5, 3.7, 3.8 (address type recognition and routing)
 */
struct DecodedAddress {
    bool isValid;           //!< Whether the address was successfully decoded
    bool isQuantum;         //!< Whether this is a quantum address (casq/tcasq/rcasq)
    int witnessVersion;     //!< Witness version (0, 1, 2, etc.) or -1 for non-witness
    std::vector<unsigned char> program;  //!< Witness program data
    std::string hrp;        //!< Human-readable part of the address
    
    DecodedAddress() : isValid(false), isQuantum(false), witnessVersion(-1) {}
};

/**
 * Encode a quantum public key as a Bech32m address.
 *
 * The address is encoded using:
 * - Bech32m encoding (BIP-350)
 * - Witness version 2
 * - Program = SHA256(pubkey) (32 bytes)
 * - HRP based on network (casq/tcasq/rcasq)
 *
 * @param[in] pubkey The FALCON-512 public key (897 bytes)
 * @param[in] params Chain parameters for network selection
 * @return Encoded quantum address string, or empty string on failure
 *
 * Requirements: 3.1, 3.2, 3.3, 3.4, 3.6 (quantum address encoding)
 */
std::string EncodeQuantumAddress(const CPubKey& pubkey, const CChainParams& params);

/**
 * Encode a quantum public key as a Bech32m address using raw public key data.
 *
 * @param[in] pubkeyData Raw FALCON-512 public key data (897 bytes)
 * @param[in] params Chain parameters for network selection
 * @return Encoded quantum address string, or empty string on failure
 */
std::string EncodeQuantumAddress(const std::vector<unsigned char>& pubkeyData, const CChainParams& params);

/**
 * Decode an address and determine its type.
 *
 * This function decodes any valid address (Base58, Bech32, Bech32m) and
 * returns information about its type, allowing the caller to route to
 * the appropriate verification logic.
 *
 * @param[in] address The address string to decode
 * @param[in] params Chain parameters for network validation
 * @return DecodedAddress structure with type information
 *
 * Requirements: 3.5, 3.7, 3.8, 3.9 (address decoding and type recognition)
 */
DecodedAddress DecodeAddress(const std::string& address, const CChainParams& params);

/**
 * Get the quantum HRP for a given chain.
 *
 * @param[in] params Chain parameters
 * @return Quantum HRP string (casq, tcasq, or rcasq)
 *
 * Requirements: 3.1, 3.2, 3.3 (network-specific HRPs)
 */
std::string GetQuantumHRP(const CChainParams& params);

/**
 * Check if an address string is a quantum address.
 *
 * @param[in] address The address string to check
 * @param[in] params Chain parameters for network validation
 * @return true if the address is a valid quantum address
 *
 * Requirements: 3.5 (quantum address recognition)
 */
bool IsQuantumAddress(const std::string& address, const CChainParams& params);

/**
 * Derive the witness program from a quantum public key.
 * The program is SHA256(pubkey), which is 32 bytes.
 *
 * @param[in] pubkey The FALCON-512 public key
 * @return 32-byte witness program (SHA256 hash of public key)
 *
 * Requirements: 3.6 (derive address program from SHA256(pubkey))
 */
uint256 GetQuantumWitnessProgram(const CPubKey& pubkey);

/**
 * Derive the witness program from raw quantum public key data.
 *
 * @param[in] pubkeyData Raw FALCON-512 public key data (897 bytes)
 * @return 32-byte witness program (SHA256 hash of public key)
 */
uint256 GetQuantumWitnessProgram(const std::vector<unsigned char>& pubkeyData);

} // namespace address

#endif // BITCOIN_ADDRESS_QUANTUM_H
