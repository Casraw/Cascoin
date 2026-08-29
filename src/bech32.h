// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Bech32 and Bech32m are string encoding formats used in address types.
// The output consists of a human-readable part (alphanumeric), a
// separator character (1), and a base32 data section, the last
// 6 characters of which are a checksum.
//
// Bech32 (BIP 173) is used for witness version 0 addresses.
// Bech32m (BIP 350) is used for witness version 1+ addresses, including
// quantum-resistant addresses (witness version 2).
//
// For more information, see BIP 173 and BIP 350.

#include <stdint.h>
#include <string>
#include <vector>

namespace bech32
{

/**
 * Encoding type enumeration for Bech32 variants.
 * - BECH32: Original BIP-173 encoding (witness version 0)
 * - BECH32M: BIP-350 encoding (witness version 1+, including quantum)
 */
enum class Encoding {
    INVALID,  //!< Invalid or failed decoding
    BECH32,   //!< BIP-173 Bech32 encoding (witness v0)
    BECH32M   //!< BIP-350 Bech32m encoding (witness v1+)
};

/**
 * Quantum address HRP (Human Readable Part) constants.
 * These are used for encoding quantum-resistant addresses using Bech32m.
 * Requirements: 3.1, 3.2, 3.3 (distinct HRPs for quantum addresses)
 */
static const std::string QUANTUM_HRP_MAINNET = "casq";   //!< Mainnet quantum address HRP
static const std::string QUANTUM_HRP_TESTNET = "tcasq";  //!< Testnet quantum address HRP
static const std::string QUANTUM_HRP_REGTEST = "rcasq";  //!< Regtest quantum address HRP

/** Encode a Bech32 string (original BIP-173 format). Returns the empty string in case of failure. */
std::string Encode(const std::string& hrp, const std::vector<uint8_t>& values);

/** Encode a Bech32m string (BIP-350 format). Returns the empty string in case of failure. */
std::string EncodeBech32m(const std::string& hrp, const std::vector<uint8_t>& values);

/**
 * Encode using specified encoding type.
 * @param encoding The encoding type (BECH32 or BECH32M)
 * @param hrp The human-readable part
 * @param values The data values (5-bit)
 * @return Encoded string, or empty string on failure
 */
std::string Encode(Encoding encoding, const std::string& hrp, const std::vector<uint8_t>& values);

/** Decode a Bech32 string. Returns (hrp, data). Empty hrp means failure. */
std::pair<std::string, std::vector<uint8_t>> Decode(const std::string& str);

/**
 * Decode a Bech32 or Bech32m string with encoding detection.
 * @param str The string to decode
 * @return Tuple of (encoding_type, hrp, data). INVALID encoding means failure.
 */
struct DecodeResult {
    Encoding encoding;
    std::string hrp;
    std::vector<uint8_t> data;
};
DecodeResult DecodeWithType(const std::string& str);

/**
 * Check if an HRP is a quantum address HRP.
 * @param hrp The human-readable part to check
 * @return true if hrp is casq, tcasq, or rcasq
 */
bool IsQuantumHRP(const std::string& hrp);

} // namespace bech32
