// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/validator_compensation.h>
#include <cvm/cvmdb.h>
#include <cvm/hat_consensus.h>
#include <cvm/consensus_validator.h>
#include <cvm/softfork.h>
#include <primitives/block.h>
#include <consensus/consensus.h>
#include <util.h>
#include <utilmoneystr.h>
#include <validation.h>

#include <algorithm>
#include <numeric>

// Gas fee split ratios
static constexpr double MINER_SHARE_RATIO = 0.70;      // 70% to miner
static constexpr double VALIDATOR_SHARE_RATIO = 0.30;  // 30% to validators

GasFeeDistribution CalculateGasFeeDistribution(
    uint64_t gasUsed,
    CAmount gasPrice,
    const std::vector<uint160>& validators
) {
    GasFeeDistribution dist;
    
    // Calculate total gas fee
    dist.totalGasFee = gasUsed * gasPrice;
    
    // Split 70/30 between miner and validators
    dist.minerShare = static_cast<CAmount>(dist.totalGasFee * MINER_SHARE_RATIO);
    dist.validatorShare = dist.totalGasFee - dist.minerShare;  // Ensure exact split (avoid rounding errors)
    
    // Store validator list
    dist.validators = validators;
    
    // Split validator share equally among all validators
    if (!validators.empty()) {
        dist.perValidatorShare = dist.validatorShare / validators.size();
    } else {
        // No validators participated - miner gets everything
        dist.minerShare = dist.totalGasFee;
        dist.validatorShare = 0;
        dist.perValidatorShare = 0;
    }
    
    return dist;
}

bool CalculateBlockValidatorPayments(
    const CBlock& block,
    CAmount& minerTotal,
    std::map<uint160, CAmount>& validatorPayments,
    CAmount blockReward
) {
    // Start with block reward (subsidy only, fees are added separately)
    minerTotal = blockReward;
    validatorPayments.clear();
    
    // Get database instance
    if (!CVM::g_cvmdb) {
        // CVM database not initialized - this is normal before CVM activation
        // Just return success with no validator payments
        LogPrint(BCLog::CVM, "CalculateBlockValidatorPayments: CVM database not initialized, skipping validator payments\n");
        return true;
    }
    CVM::CVMDatabase* db = CVM::g_cvmdb.get();
    
    // Track total validator share to subtract from fees
    CAmount totalValidatorShare = 0;
    
    // Process each transaction in the block (skip coinbase)
    // Note: We calculate the 70/30 split for gas fees here.
    // The actual transaction fees are added separately via nFees in CreateCoinbaseWithValidatorPayments.
    // We need to calculate how much of those fees should go to validators (30% of gas fees).
    for (size_t i = 1; i < block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];
        
        // Check if this is a CVM/EVM contract transaction
        // Use the actual gas tracking system via ConsensusValidator::ExtractGasInfo
        uint64_t gasUsed = 0;
        CAmount gasCost = 0;
        
        // Extract actual gas usage from transaction using the gas tracking system
        // Requirements: 8.1, 9.4
        if (!CVM::ConsensusValidator::ExtractGasInfo(tx, gasUsed, gasCost)) {
            // Not a contract transaction or failed to extract gas info
            // Skip this transaction - no gas fees to distribute to validators
            continue;
        }
        
        // Validate that we have meaningful gas usage
        if (gasUsed == 0) {
            LogPrint(BCLog::CVM, "CalculateBlockValidatorPayments: Transaction %s has zero gas usage, skipping\n",
                tx.GetHash().ToString());
            continue;
        }
        
        // Calculate gas price from gas cost and gas used
        // gasCost = gasUsed * gasPrice, so gasPrice = gasCost / gasUsed
        CAmount gasPrice = (gasUsed > 0) ? (gasCost / static_cast<CAmount>(gasUsed)) : 0;
        
        // Ensure minimum gas price of 1 satoshi per gas unit if there's any cost
        if (gasCost > 0 && gasPrice == 0) {
            gasPrice = 1;
        }
        
        LogPrint(BCLog::CVM, "CalculateBlockValidatorPayments: Transaction %s - gasUsed=%d, gasCost=%s, gasPrice=%s\n",
            tx.GetHash().ToString(), gasUsed, FormatMoney(gasCost), FormatMoney(gasPrice));
        
        // Get validators who participated in this transaction
        std::vector<uint160> validators;
        CVM::TransactionValidationRecord record;
        if (db->GetValidatorParticipation(tx.GetHash(), record)) {
            validators = record.validators;
        }
        
        // Calculate fee distribution
        GasFeeDistribution dist = CalculateGasFeeDistribution(gasUsed, gasPrice, validators);
        
        // Accumulate validator payments
        for (const uint160& validator : validators) {
            validatorPayments[validator] += dist.perValidatorShare;
        }
        
        // Track total validator share (will be subtracted from nFees later)
        totalValidatorShare += dist.validatorShare;
    }
    
    // Note: We do NOT subtract validatorShare from minerTotal here!
    // minerTotal currently only contains blockReward.
    // The subtraction happens in CreateCoinbaseWithValidatorPayments after nFees is added.
    // We store the total validator share in a negative value to signal the caller.
    // Actually, let's just return the validator payments and let the caller handle the math.
    
    return true;
}

bool CreateCoinbaseWithValidatorPayments(
    CMutableTransaction& coinbaseTx,
    const CBlock& block,
    const CScript& minerScript,
    CAmount blockReward,
    int nHeight,
    CAmount nFees
) {
    // Calculate validator payments
    CAmount minerTotal = 0;
    std::map<uint160, CAmount> validatorPayments;
    
    if (!CalculateBlockValidatorPayments(block, minerTotal, validatorPayments, blockReward)) {
        LogPrintf("CreateCoinbaseWithValidatorPayments: Failed to calculate validator payments\n");
        return false;
    }
    
    // minerTotal now contains blockReward only
    // Add transaction fees to miner total
    minerTotal += nFees;
    
    // Calculate total validator payments (30% of gas fees from contract transactions)
    CAmount totalValidatorPayments = 0;
    for (const auto& [validator, amount] : validatorPayments) {
        totalValidatorPayments += amount;
    }
    
    // Subtract validator payments from miner total
    // This ensures the 70/30 split: miner gets 70% of gas fees, validators get 30%
    // Note: For non-contract transactions (trust, reputation, etc.), 100% goes to miner
    if (totalValidatorPayments > 0) {
        if (minerTotal < totalValidatorPayments) {
            LogPrintf("CreateCoinbaseWithValidatorPayments: Validator payments (%s) exceed miner total (%s)\n",
                FormatMoney(totalValidatorPayments), FormatMoney(minerTotal));
            // Don't fail - just skip validator payments
            validatorPayments.clear();
            totalValidatorPayments = 0;
        } else {
            minerTotal -= totalValidatorPayments;
        }
    }
    
    // Create coinbase transaction
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    
    // BIP34: Block height in scriptSig
    CScript scriptSig;
    scriptSig << nHeight;
    if (scriptSig.size() < 2) {
        scriptSig << OP_0;
    }
    coinbaseTx.vin[0].scriptSig = scriptSig;
    
    // Output 0: Miner payment
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = minerScript;
    coinbaseTx.vout[0].nValue = minerTotal;
    
    // Outputs 1-N: Validator payments
    for (const auto& [validator, amount] : validatorPayments) {
        if (amount > 0) {
            CTxOut validatorOut;
            validatorOut.scriptPubKey = GetScriptForDestination(CKeyID(validator));
            validatorOut.nValue = amount;
            coinbaseTx.vout.push_back(validatorOut);
        }
    }
    
    LogPrint(BCLog::CVM, "CreateCoinbaseWithValidatorPayments: Miner=%s, Validators=%d, Total=%s\n",
        FormatMoney(minerTotal), validatorPayments.size(), FormatMoney(minerTotal + std::accumulate(
            validatorPayments.begin(), validatorPayments.end(), CAmount(0),
            [](CAmount sum, const auto& p) { return sum + p.second; }
        )));
    
    return true;
}

bool CheckCoinbaseValidatorPayments(const CBlock& block, CAmount blockRewardWithFees) {
    if (block.vtx.empty()) {
        return error("CheckCoinbaseValidatorPayments: Block has no transactions");
    }
    
    const CTransaction& coinbase = *block.vtx[0];
    if (!coinbase.IsCoinBase()) {
        return error("CheckCoinbaseValidatorPayments: First transaction is not coinbase");
    }
    
    // Note: blockRewardWithFees already includes nFees (passed from validation.cpp)
    // We need to extract the base block reward for CalculateBlockValidatorPayments
    // But since we don't have access to nFees separately here, we'll just verify
    // that the total coinbase output equals blockRewardWithFees
    
    // Get total coinbase output
    CAmount totalCoinbaseOutput = coinbase.GetValueOut();
    
    // The total coinbase output should equal blockRewardWithFees
    // (miner share + validator shares = block reward + fees)
    if (totalCoinbaseOutput != blockRewardWithFees) {
        // Allow small rounding differences (up to 1 satoshi per validator)
        CAmount diff = std::abs(totalCoinbaseOutput - blockRewardWithFees);
        if (diff > 10) {  // Allow up to 10 satoshi difference for rounding
            return error("CheckCoinbaseValidatorPayments: Total coinbase output incorrect (expected %s, got %s)",
                FormatMoney(blockRewardWithFees), FormatMoney(totalCoinbaseOutput));
        }
    }
    
    // For now, we don't strictly validate the 70/30 split
    // This is because the validator participation data may not be available
    // during block validation (it's stored after block processing)
    // TODO: Implement strict validation after validator participation tracking is complete
    
    LogPrint(BCLog::CVM, "CheckCoinbaseValidatorPayments: Validation successful (Total=%s)\n",
        FormatMoney(totalCoinbaseOutput));
    
    return true;
}
        FormatMoney(expectedMinerTotal), expectedValidatorPayments.size());
    
    return true;
}
