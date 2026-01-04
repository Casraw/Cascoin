// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ACTIVATION_H
#define CASCOIN_CVM_ACTIVATION_H

class CBlockIndex;
namespace Consensus { struct Params; }

/**
 * Check if CVM-EVM enhancement features are active at the given block index.
 * This includes EVM bytecode execution, trust-aware operations, and sustainable gas system.
 * 
 * @param pindex The block index to check activation for
 * @param params Consensus parameters containing deployment configuration
 * @return true if CVM-EVM features are active, false otherwise
 */
bool IsCVMEVMEnabled(const CBlockIndex* pindex, const Consensus::Params& params);

/**
 * Check if CVM-EVM enhancement features are active at the given block height.
 * This is a convenience function that doesn't require a CBlockIndex.
 * 
 * @param nHeight The block height to check activation for
 * @param params Consensus parameters containing deployment configuration
 * @return true if CVM-EVM features are active, false otherwise
 */
bool IsCVMEVMEnabled(int nHeight, const Consensus::Params& params);

#endif // CASCOIN_CVM_ACTIVATION_H
