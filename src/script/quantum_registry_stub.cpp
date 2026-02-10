// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file quantum_registry_stub.cpp
 * @brief Stub implementation of LookupQuantumPubKey for consensus-only builds
 *
 * This file provides a stub implementation of LookupQuantumPubKey for builds
 * that don't include the full quantum registry (like cascoin-tx and
 * libbitcoinconsensus). The full implementation is in quantum_registry.cpp.
 */

#include <quantum_registry_fwd.h>

/**
 * Stub implementation of LookupQuantumPubKey
 * 
 * This always returns false because the registry is not available in
 * consensus-only builds. Reference transactions (0x52) will fail validation.
 */
bool LookupQuantumPubKey(const uint256& hash, std::vector<unsigned char>& pubkey)
{
    (void)hash;
    (void)pubkey;
    return false;
}
