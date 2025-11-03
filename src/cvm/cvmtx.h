// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CVMTX_H
#define CASCOIN_CVM_CVMTX_H

#include <primitives/transaction.h>
#include <consensus/params.h>
#include <string>

class CBlockIndex;
class CValidationState;
class CCoinsViewCache;

namespace CVM {

/**
 * Initialize CVM subsystem
 * Called during node initialization
 */
bool InitCVM(const std::string& datadir);

/**
 * Shutdown CVM subsystem
 * Called during node shutdown
 */
void ShutdownCVM();

/**
 * Check if CVM is active at given height
 */
bool IsCVMActive(int height, const Consensus::Params& params);

/**
 * Check if ASRS is active at given height
 */
bool IsASRSActive(int height, const Consensus::Params& params);

/**
 * Validate a transaction that may contain CVM operations
 * Called during mempool acceptance
 * 
 * @param tx Transaction to validate
 * @param state Validation state to record errors
 * @param height Current block height
 * @param params Consensus parameters
 * @return true if transaction is valid
 */
bool CheckCVMTransaction(const CTransaction& tx, CValidationState& state, 
                         int height, const Consensus::Params& params);

/**
 * Execute CVM operations in a block
 * Called during block validation
 * 
 * @param block Block to process
 * @param pindex Block index
 * @param view Coins view
 * @param params Consensus parameters
 * @return true if all CVM operations in block are valid
 */
bool ExecuteCVMBlock(const CBlock& block, const CBlockIndex* pindex,
                     CCoinsViewCache& view, const Consensus::Params& params);

/**
 * Update reputation scores based on block transactions
 * Called after block is connected
 * 
 * @param block Block that was connected
 * @param pindex Block index
 * @param params Consensus parameters
 */
void UpdateReputationScores(const CBlock& block, const CBlockIndex* pindex,
                            const Consensus::Params& params);

/**
 * Check if transaction contains CVM or reputation operations
 * 
 * @param tx Transaction to check
 * @return true if transaction has CVM/reputation data
 */
bool IsCVMOrReputationTx(const CTransaction& tx);

} // namespace CVM

#endif // CASCOIN_CVM_CVMTX_H

