// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_QUANTUM_FALCON_H
#define BITCOIN_CRYPTO_QUANTUM_FALCON_H

#include <config/bitcoin-config.h>

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file falcon.h
 * @brief FALCON-512 Post-Quantum Cryptography Module
 *
 * This module provides FALCON-512 digital signature functionality using the
 * Open Quantum Safe (liboqs) library. FALCON-512 is a lattice-based signature
 * scheme selected by NIST for post-quantum standardization, providing NIST
 * Level 1 security (128-bit quantum security).
 *
 * Key sizes:
 * - Private key: 1281 bytes
 * - Public key: 897 bytes
 * - Signature: ~666 bytes (max 700 bytes)
 *
 * Requirements: 9.1 (NIST Level 1 security)
 */

namespace quantum {

/** FALCON-512 key and signature size constants */
static constexpr size_t FALCON512_PRIVATE_KEY_SIZE = 1281;
static constexpr size_t FALCON512_PUBLIC_KEY_SIZE = 897;
static constexpr size_t FALCON512_SIGNATURE_SIZE = 666;      // Typical size
static constexpr size_t FALCON512_MAX_SIGNATURE_SIZE = 700;  // Maximum size

/**
 * Initialize the post-quantum cryptography subsystem.
 * Must be called before any other PQC functions.
 * Thread-safe: Yes (can be called multiple times safely)
 */
void PQC_Start();

/**
 * Shutdown the post-quantum cryptography subsystem.
 * Should be called during application shutdown.
 * Thread-safe: Yes
 */
void PQC_Stop();

/**
 * Perform sanity check on the PQC subsystem.
 * Verifies that FALCON-512 is available and functioning correctly.
 *
 * @return true if sanity check passes, false otherwise
 */
bool PQC_InitSanityCheck();

/**
 * Generate a new FALCON-512 key pair.
 *
 * @param[out] privkey Output buffer for private key (will be resized to 1281 bytes)
 * @param[out] pubkey Output buffer for public key (will be resized to 897 bytes)
 * @return true on success, false on failure
 *
 * Requirements: 1.1, 1.2, 9.4 (256-bit entropy from system CSPRNG)
 */
bool GenerateKeyPair(
    std::vector<unsigned char>& privkey,
    std::vector<unsigned char>& pubkey
);

/**
 * Sign a message using FALCON-512.
 *
 * @param[in] privkey Private key (1281 bytes)
 * @param[in] message Pointer to message data
 * @param[in] messageLen Length of message in bytes
 * @param[out] signature Output buffer for signature (will be resized, typically ~666 bytes)
 * @return true on success, false on failure
 *
 * Requirements: 1.5, 9.5 (constant-time operations)
 */
bool Sign(
    const std::vector<unsigned char>& privkey,
    const unsigned char* message,
    size_t messageLen,
    std::vector<unsigned char>& signature
);

/**
 * Verify a FALCON-512 signature.
 *
 * @param[in] pubkey Public key (897 bytes)
 * @param[in] message Pointer to message data
 * @param[in] messageLen Length of message in bytes
 * @param[in] signature Signature to verify
 * @return true if signature is valid, false otherwise
 */
bool Verify(
    const std::vector<unsigned char>& pubkey,
    const unsigned char* message,
    size_t messageLen,
    const std::vector<unsigned char>& signature
);

/**
 * Check if a FALCON-512 signature is in canonical form.
 * Non-canonical signatures should be rejected to prevent malleability.
 *
 * @param[in] signature Signature to check
 * @return true if signature is canonical, false otherwise
 *
 * Requirements: 9.8, 9.9 (malleability prevention)
 */
bool IsCanonicalSignature(const std::vector<unsigned char>& signature);

/**
 * Derive a public key from a private key.
 *
 * @param[in] privkey Private key (1281 bytes)
 * @param[out] pubkey Output buffer for public key (will be resized to 897 bytes)
 * @return true on success, false on failure
 */
bool DerivePublicKey(
    const std::vector<unsigned char>& privkey,
    std::vector<unsigned char>& pubkey
);

} // namespace quantum

#endif // BITCOIN_CRYPTO_QUANTUM_FALCON_H
