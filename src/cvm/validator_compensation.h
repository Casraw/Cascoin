// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_VALIDATOR_COMPENSATION_H
#define CASCOIN_CVM_VALIDATOR_COMPENSATION_H

#include <amount.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <script/standard.h>

#include <vector>
#include <map>

class CBlock;

/**
 * Gas Fee Distribution System
 * 
 * Implements the 70/30 split of gas fees between miners and validators:
 * - 70% goes to the miner (who mines the block)
 * - 30% goes to validators (split equally among all participating validators)
 * 
 * This creates passive income opportunities for anyone running a validator node.
 */

/**
 * Structure representing the distribution of gas fees for a single transaction
 */
struct GasFeeDistribution {
    CAmount totalGasFee;          // Total gas fee paid by transaction
    CAmount minerShare;           // 70% of total (goes to miner)
    CAmount validatorShare;       // 30% of total (split among validators)
    
    std::vector<uint160> validators;  // Validators who participated in consensus
    CAmount perValidatorShare;        // validatorShare / validators.size()
    
    GasFeeDistribution() : totalGasFee(0), minerShare(0), validatorShare(0), perValidatorShare(0) {}
};

/**
 * Calculate gas fee distribution for a transaction
 * 
 * @param gasUsed Amount of gas consumed by the transaction
 * @param gasPrice Price per unit of gas (in satoshis)
 * @param validators List of validator addresses who participated in consensus
 * @return GasFeeDistribution structure with calculated splits
 */
GasFeeDistribution CalculateGasFeeDistribution(
    uint64_t gasUsed,
    CAmount gasPrice,
    const std::vector<uint160>& validators
);

/**
 * Calculate total validator payments for a block
 * 
 * Aggregates validator payments across all transactions in the block.
 * If a validator participated in multiple transactions, their payments are combined.
 * 
 * @param block The block containing transactions
 * @param minerTotal Output parameter: total amount for miner (block reward + 70% gas fees)
 * @param validatorPayments Output parameter: map of validator address → total payment
 * @param blockReward The base block reward (subsidy)
 * @return true if calculation succeeded, false otherwise
 */
bool CalculateBlockValidatorPayments(
    const CBlock& block,
    CAmount& minerTotal,
    std::map<uint160, CAmount>& validatorPayments,
    CAmount blockReward
);

/**
 * Create coinbase transaction with validator payments
 * 
 * Creates a coinbase transaction with multiple outputs:
 * - Output 0: Miner (block reward + transaction fees + 70% of gas fees)
 * - Outputs 1-N: Validators (each gets their share of 30% of gas fees)
 * 
 * @param coinbaseTx Output parameter: the coinbase transaction to create
 * @param block The block being mined
 * @param minerScript The script for the miner's payment
 * @param blockReward The base block reward (subsidy)
 * @param nHeight The block height (for BIP34 compliance)
 * @param nFees Total transaction fees from all transactions in the block (default 0)
 * @return true if creation succeeded, false otherwise
 */
bool CreateCoinbaseWithValidatorPayments(
    CMutableTransaction& coinbaseTx,
    const CBlock& block,
    const CScript& minerScript,
    CAmount blockReward,
    int nHeight,
    CAmount nFees = 0
);

/**
 * Validate coinbase validator payments
 * 
 * Verifies that the coinbase transaction pays the correct amounts to:
 * - Miner (block reward + transaction fees + 70% of gas fees)
 * - Each validator (their share of 30% of gas fees)
 * 
 * This is a consensus rule - blocks with incorrect payments are invalid.
 * 
 * @param block The block to validate
 * @param blockReward The expected block reward (subsidy) + transaction fees
 * @return true if payments are correct, false otherwise
 */
bool CheckCoinbaseValidatorPayments(const CBlock& block, CAmount blockReward);

#endif // CASCOIN_CVM_VALIDATOR_COMPENSATION_H
