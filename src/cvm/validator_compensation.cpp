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
    // Start with block reward
    minerTotal = blockReward;
    validatorPayments.clear();
    
    // Get database instance
    if (!CVM::g_cvmdb) {
        LogPrintf("CalculateBlockValidatorPayments: CVM database not initialized\n");
        return false;
    }
    CVM::CVMDatabase* db = CVM::g_cvmdb.get();
    
    // Process each transaction in the block (skip coinbase)
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
            // Skip this transaction - no gas fees to distribute
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
        
        // Add miner share to total
        minerTotal += dist.minerShare;
        
        // Accumulate validator payments
        for (const uint160& validator : validators) {
            validatorPayments[validator] += dist.perValidatorShare;
        }
    }
    
    return true;
}

bool CreateCoinbaseWithValidatorPayments(
    CMutableTransaction& coinbaseTx,
    const CBlock& block,
    const CScript& minerScript,
    CAmount blockReward,
    int nHeight
) {
    // Calculate validator payments
    CAmount minerTotal = 0;
    std::map<uint160, CAmount> validatorPayments;
    
    if (!CalculateBlockValidatorPayments(block, minerTotal, validatorPayments, blockReward)) {
        LogPrintf("CreateCoinbaseWithValidatorPayments: Failed to calculate validator payments\n");
        return false;
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

bool CheckCoinbaseValidatorPayments(const CBlock& block, CAmount blockReward) {
    if (block.vtx.empty()) {
        return error("CheckCoinbaseValidatorPayments: Block has no transactions");
    }
    
    const CTransaction& coinbase = *block.vtx[0];
    if (!coinbase.IsCoinBase()) {
        return error("CheckCoinbaseValidatorPayments: First transaction is not coinbase");
    }
    
    // Calculate expected payments
    CAmount expectedMinerTotal = 0;
    std::map<uint160, CAmount> expectedValidatorPayments;
    
    if (!CalculateBlockValidatorPayments(block, expectedMinerTotal, expectedValidatorPayments, blockReward)) {
        return error("CheckCoinbaseValidatorPayments: Failed to calculate expected payments");
    }
    
    // Verify miner payment (output 0)
    if (coinbase.vout.empty()) {
        return error("CheckCoinbaseValidatorPayments: Coinbase has no outputs");
    }
    
    if (coinbase.vout[0].nValue != expectedMinerTotal) {
        return error("CheckCoinbaseValidatorPayments: Miner payment incorrect (expected %s, got %s)",
            FormatMoney(expectedMinerTotal), FormatMoney(coinbase.vout[0].nValue));
    }
    
    // Verify validator payments (outputs 1-N)
    std::map<uint160, CAmount> actualValidatorPayments;
    for (size_t i = 1; i < coinbase.vout.size(); i++) {
        const CTxOut& out = coinbase.vout[i];
        
        // Extract validator address
        CTxDestination dest;
        if (!ExtractDestination(out.scriptPubKey, dest)) {
            return error("CheckCoinbaseValidatorPayments: Failed to extract destination from output %d", i);
        }
        
        const CKeyID* keyID = boost::get<CKeyID>(&dest);
        if (!keyID) {
            return error("CheckCoinbaseValidatorPayments: Output %d is not a P2PKH address", i);
        }
        
        uint160 validator(*keyID);
        actualValidatorPayments[validator] += out.nValue;
    }
    
    // Verify all expected validators are paid correctly
    for (const auto& [validator, expectedAmount] : expectedValidatorPayments) {
        auto it = actualValidatorPayments.find(validator);
        if (it == actualValidatorPayments.end()) {
            return error("CheckCoinbaseValidatorPayments: Validator %s not paid",
                HexStr(validator));
        }
        
        if (it->second != expectedAmount) {
            return error("CheckCoinbaseValidatorPayments: Validator %s payment incorrect (expected %s, got %s)",
                HexStr(validator), FormatMoney(expectedAmount), FormatMoney(it->second));
        }
    }
    
    // Verify no unexpected validators are paid
    for (const auto& [validator, actualAmount] : actualValidatorPayments) {
        if (expectedValidatorPayments.find(validator) == expectedValidatorPayments.end()) {
            return error("CheckCoinbaseValidatorPayments: Unexpected validator %s paid %s",
                HexStr(validator), FormatMoney(actualAmount));
        }
    }
    
    LogPrint(BCLog::CVM, "CheckCoinbaseValidatorPayments: Validation successful (Miner=%s, Validators=%d)\n",
        FormatMoney(expectedMinerTotal), expectedValidatorPayments.size());
    
    return true;
}
