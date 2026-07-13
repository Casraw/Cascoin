// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_KECCAK256_H
#define CASCOIN_CVM_KECCAK256_H

#include <uint256.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CVM {

/**
 * Ethereum-compatible Keccak-256 (the original Keccak submission padding, i.e.
 * the 0x01 domain suffix — NOT the NIST SHA3-256 0x06 suffix).
 *
 * This is a self-contained implementation so the EVM-compatibility paths do not
 * depend on the optional evmone/ethash headers being present at compile time.
 */
void Keccak256(const uint8_t* data, size_t len, uint8_t out[32]);

/** Convenience overload: hash a byte vector, returning the 32-byte digest. */
std::array<uint8_t, 32> Keccak256(const std::vector<uint8_t>& data);

/**
 * Compute the Ethereum CREATE contract address:
 *   address = keccak256(rlp([sender, nonce]))[12:]
 *
 * @param senderBE  Pointer to the 20-byte sender address in big-endian
 *                  (display / Ethereum) byte order.
 * @param nonce     The sender's account nonce.
 * @param outAddrBE Receives the 20-byte contract address in big-endian order.
 */
void EthCreateAddressBytes(const uint8_t senderBE[20], uint64_t nonce, uint8_t outAddrBE[20]);

} // namespace CVM

#endif // CASCOIN_CVM_KECCAK256_H
