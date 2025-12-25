// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/txbuilder.h>
#include <cvm/softfork.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <policy/policy.h>
#include <policy/fees.h>
#include <validation.h>
#include <net.h>
#include <consensus/validation.h>
#include <util.h>
#include <utilmoneystr.h>

namespace CVM {

// Minimum transaction fee (satoshi per byte)
static const CAmount MIN_TX_FEE_PER_BYTE = 1;

// Dust threshold (in satoshis)
static const CAmount DUST_THRESHOLD = 546;

CMutableTransaction CVMTransactionBuilder::BuildVoteTransaction(
    CWallet* wallet,
    const uint160& targetAddress,
    int16_t voteValue,
    const std::string& reason,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    // Lock wallet for coin selection
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Build CVM reputation data
    CVMReputationData repData;
    repData.targetAddress = targetAddress;
    repData.voteValue = voteValue;
    repData.timestamp = static_cast<uint32_t>(GetTime());
    
    // 2. Create OP_RETURN output with CVM data
    std::vector<uint8_t> repBytes = repData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::REPUTATION_VOTE, repBytes);
    
    CTxOut cvmOutput(0, cvmScript); // OP_RETURN = 0 value
    tx.vout.push_back(cvmOutput);
    
    // 3. Estimate fee (we'll refine after selecting coins)
    CAmount estimatedFee = CalculateFee(tx);
    
    // 4. Select coins to cover fee
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, estimatedFee, nullptr, selectedCoins, totalSelected)) {
        error = "Insufficient funds for transaction fee";
        return CMutableTransaction();
    }
    
    // 5. Add inputs from selected coins
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 6. Calculate actual fee and change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee;
    
    // 7. Add change output if not dust
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
    } else {
        // If change is dust, add it to fee
        actualFee = totalSelected;
    }
    
    fee = actualFee;
    
    LogPrintf("CVM: Built vote transaction: fee=%s, inputs=%d, outputs=%d\n",
              FormatMoney(fee), tx.vin.size(), tx.vout.size());
    
    return tx;
}

CMutableTransaction CVMTransactionBuilder::BuildDeployTransaction(
    CWallet* wallet,
    const std::vector<uint8_t>& bytecode,
    uint64_t gasLimit,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Build CVM deployment data
    CVMDeployData deployData;
    deployData.codeHash = Hash(bytecode.begin(), bytecode.end());
    deployData.gasLimit = gasLimit;
    
    // 2. Create OP_RETURN output
    std::vector<uint8_t> deployBytes = deployData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::CONTRACT_DEPLOY, deployBytes);
    
    CTxOut cvmOutput(0, cvmScript);
    tx.vout.push_back(cvmOutput);
    
    // 3. Estimate fee
    CAmount estimatedFee = CalculateFee(tx);
    
    // 4. Select coins
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, estimatedFee, nullptr, selectedCoins, totalSelected)) {
        error = "Insufficient funds for transaction fee";
        return CMutableTransaction();
    }
    
    // 5. Add inputs
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 6. Calculate actual fee and change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee;
    
    // 7. Add change output
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
    } else {
        actualFee = totalSelected;
    }
    
    fee = actualFee;
    
    LogPrintf("CVM: Built deploy transaction: bytecode_hash=%s, fee=%s\n",
              deployData.codeHash.ToString(), FormatMoney(fee));
    
    return tx;
}

CMutableTransaction CVMTransactionBuilder::BuildCallTransaction(
    CWallet* wallet,
    const uint160& contractAddress,
    const std::vector<uint8_t>& callData,
    uint64_t gasLimit,
    CAmount value,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Build CVM call data
    CVMCallData cvmCallData;
    cvmCallData.contractAddress = contractAddress;
    cvmCallData.gasLimit = gasLimit;
    cvmCallData.callData = callData;
    
    // 2. Create OP_RETURN output
    std::vector<uint8_t> callBytes = cvmCallData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::CONTRACT_CALL, callBytes);
    
    CTxOut cvmOutput(0, cvmScript);
    tx.vout.push_back(cvmOutput);
    
    // 3. If sending value to contract, add output
    // (In production, this would go to contract's P2SH address)
    if (value > 0) {
        // TODO: Implement contract address resolution
        error = "Contract value transfers not yet implemented";
        return CMutableTransaction();
    }
    
    // 4. Estimate fee
    CAmount estimatedFee = CalculateFee(tx);
    CAmount totalNeeded = estimatedFee + value;
    
    // 5. Select coins
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, totalNeeded, nullptr, selectedCoins, totalSelected)) {
        error = "Insufficient funds";
        return CMutableTransaction();
    }
    
    // 6. Add inputs
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 7. Calculate change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee - value;
    
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
    } else {
        actualFee = totalSelected - value;
    }
    
    fee = actualFee;
    
    return tx;
}

bool CVMTransactionBuilder::SignTransaction(
    CWallet* wallet,
    CMutableTransaction& tx,
    std::string& error
) {
    if (!wallet) {
        error = "Wallet not available";
        return false;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // Sign all inputs
    int nIn = 0;
    for (auto& input : tx.vin) {
        // Get previous transaction
        const CWalletTx* prev = wallet->GetWalletTx(input.prevout.hash);
        if (!prev) {
            error = strprintf("Previous transaction not found: %s", input.prevout.hash.ToString());
            return false;
        }
        
        // Get script and value
        const CScript& scriptPubKey = prev->tx->vout[input.prevout.n].scriptPubKey;
        const CAmount& amount = prev->tx->vout[input.prevout.n].nValue;
        
        // Sign input
        SignatureData sigdata;
        if (!ProduceSignature(MutableTransactionSignatureCreator(wallet, &tx, nIn, amount, SIGHASH_ALL | SIGHASH_FORKID),
                              scriptPubKey, sigdata)) {
            error = strprintf("Failed to sign input %d", nIn);
            return false;
        }
        
        UpdateTransaction(tx, nIn, sigdata);
        nIn++;
    }
    
    LogPrintf("CVM: Transaction signed successfully: %d inputs\n", tx.vin.size());
    return true;
}

bool CVMTransactionBuilder::BroadcastTransaction(
    const CTransaction& tx,
    uint256& txid,
    std::string& error
) {
    // Validate transaction
    CValidationState state;
    bool fMissingInputs = false;
    
    if (!AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), 
                           &fMissingInputs, nullptr, false, 0)) {
        if (state.IsInvalid()) {
            error = strprintf("Transaction rejected: %s", FormatStateMessage(state));
            return false;
        } else {
            if (fMissingInputs) {
                error = "Missing inputs";
            } else {
                error = "Unknown error";
            }
            return false;
        }
    }
    
    // Relay to network
    CInv inv(MSG_TX, tx.GetHash());
    if (g_connman) {
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }
    
    txid = tx.GetHash();
    
    LogPrintf("CVM: Transaction broadcast successfully: %s\n", txid.ToString());
    return true;
}

bool CVMTransactionBuilder::SelectCoins(
    CWallet* wallet,
    CAmount amount,
    const CCoinControl* coinControl,
    std::vector<COutput>& selected,
    CAmount& total
) {
    // Get available coins
    std::vector<COutput> availableCoins;
    wallet->AvailableCoins(availableCoins, true, coinControl);
    
    if (availableCoins.empty()) {
        return false;
    }
    
    // Simple selection: take coins until we have enough
    // (In production, use more sophisticated coin selection)
    total = 0;
    for (const COutput& coin : availableCoins) {
        if (total >= amount) {
            break;
        }
        
        selected.push_back(coin);
        total += coin.tx->tx->vout[coin.i].nValue;
    }
    
    return total >= amount;
}

bool CVMTransactionBuilder::AddChangeOutput(
    CMutableTransaction& tx,
    CWallet* wallet,
    CAmount change
) {
    if (change <= 0) {
        return false;
    }
    
    // Get change address
    CPubKey changeKey;
    if (!wallet->GetKeyFromPool(changeKey)) {
        return false;
    }
    
    CKeyID changeKeyID = changeKey.GetID();
    CScript changeScript = GetScriptForDestination(changeKeyID);
    
    // Check if change is dust
    if (change < GetDustThreshold(changeScript)) {
        return false;
    }
    
    // Add change output
    CTxOut changeOutput(change, changeScript);
    tx.vout.push_back(changeOutput);
    
    return true;
}

CAmount CVMTransactionBuilder::CalculateFee(const CMutableTransaction& tx) {
    // Estimate transaction size
    // Base size + inputs + outputs
    unsigned int nBytes = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    
    // Add signature size estimate (approximately 107 bytes per input for witness data)
    nBytes += tx.vin.size() * 107;
    
    // Calculate fee - use higher rate to ensure acceptance
    CAmount fee = nBytes * MIN_TX_FEE_PER_BYTE * 10; // 10x to be safe
    
    // Ensure minimum relay fee
    CAmount minRelayFee = ::minRelayTxFee.GetFee(nBytes);
    if (fee < minRelayFee) {
        fee = minRelayFee * 2; // Double the min relay fee
    }
    
    // Minimum absolute fee
    if (fee < 10000) { // 0.0001 CAS minimum
        fee = 10000;
    }
    
    return fee;
}

CAmount CVMTransactionBuilder::GetDustThreshold(const CScript& scriptPubKey) {
    // Standard dust threshold is 546 satoshis
    // Can be customized based on script type
    size_t nSize = ::GetSerializeSize(CTxOut(0, scriptPubKey), SER_DISK, 0);
    return 3 * (nSize + 148) * ::minRelayTxFee.GetFeePerK() / 1000;
}

CScript CVMTransactionBuilder::CreateBondScript(
    const uint160& userPubKeyHash,
    int lockBlocks
) {
    // Create P2SH script with timelock
    // Allows user to reclaim after lockBlocks, or DAO to slash before
    
    CScript script;
    
    // Get current block height
    int currentHeight = chainActive.Height();
    int unlockHeight = currentHeight + lockBlocks;
    
    // Build redeem script:
    // OP_IF
    //   <unlockHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP
    //   OP_DUP OP_HASH160 <userPubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
    // OP_ELSE
    //   // DAO slash path (multisig) - TODO: implement when DAO is established
    //   OP_RETURN  // For now, no slash path (bond is simply locked)
    // OP_ENDIF
    
    CScript redeemScript;
    redeemScript << OP_IF;
    redeemScript << unlockHeight << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    redeemScript << OP_DUP << OP_HASH160 << ToByteVector(userPubKeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;
    redeemScript << OP_ELSE;
    redeemScript << OP_RETURN;  // Placeholder for DAO multisig
    redeemScript << OP_ENDIF;
    
    // Convert to P2SH
    CScriptID scriptID(redeemScript);
    script = GetScriptForDestination(scriptID);
    
    LogPrintf("CVM: Created bond script for user %s, unlock height %d\n",
              HexStr(userPubKeyHash), unlockHeight);
    
    return script;
}

bool CVMTransactionBuilder::AddBondOutput(
    CMutableTransaction& tx,
    const CScript& bondScript,
    CAmount bondAmount
) {
    if (bondAmount <= 0) {
        return false;
    }
    
    // Verify bond amount meets minimum
    if (bondAmount < COIN / 100) {  // Minimum 0.01 CAS
        return false;
    }
    
    // Add bond output
    CTxOut bondOutput(bondAmount, bondScript);
    tx.vout.push_back(bondOutput);
    
    return true;
}

CMutableTransaction CVMTransactionBuilder::BuildTrustTransaction(
    CWallet* wallet,
    const uint160& toAddress,
    int16_t weight,
    CAmount bondAmount,
    const std::string& reason,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    // Validate parameters
    if (weight < -100 || weight > 100) {
        error = "Weight must be between -100 and +100";
        return tx;
    }
    
    if (bondAmount < COIN / 100) {
        error = "Bond amount must be at least 0.01 CAS";
        return tx;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Get user's address for bond script
    CPubKey userKey;
    if (!wallet->GetKeyFromPool(userKey)) {
        error = "Failed to get key from wallet";
        return tx;
    }
    uint160 userAddress = userKey.GetID();
    
    // 2. Build CVM trust edge data
    CVMTrustEdgeData trustData;
    trustData.fromAddress = userAddress;
    trustData.toAddress = toAddress;
    trustData.weight = weight;
    trustData.bondAmount = bondAmount;
    trustData.timestamp = static_cast<uint32_t>(GetTime());
    
    // 3. Create OP_RETURN output with trust data
    std::vector<uint8_t> trustBytes = trustData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::TRUST_EDGE, trustBytes);
    
    CTxOut cvmOutput(0, cvmScript);
    tx.vout.push_back(cvmOutput);
    
    // 4. Create bond script and add bond output
    CScript bondScript = CreateBondScript(userAddress, 1440); // 1440 blocks = ~1 day
    if (!AddBondOutput(tx, bondScript, bondAmount)) {
        error = "Failed to add bond output";
        return CMutableTransaction();
    }
    
    // 5. Estimate fee
    CAmount estimatedFee = CalculateFee(tx);
    CAmount totalNeeded = estimatedFee + bondAmount + DUST_THRESHOLD; // Extra for change
    
    // 6. Select coins to cover fee + bond
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, totalNeeded, nullptr, selectedCoins, totalSelected)) {
        error = strprintf("Insufficient funds: need %s, have spendable balance",
                         FormatMoney(totalNeeded));
        return CMutableTransaction();
    }
    
    // 7. Add inputs from selected coins
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 8. Calculate actual fee and change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee - bondAmount;
    
    // 9. Add change output if not dust
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
        // Recalculate fee with change output
        actualFee = CalculateFee(tx);
        change = totalSelected - actualFee - bondAmount;
        if (change < 0) {
            error = "Insufficient funds after adding change";
            return CMutableTransaction();
        }
    } else {
        // If change is dust, add it to fee
        actualFee = totalSelected - bondAmount;
    }
    
    fee = actualFee;
    
    LogPrintf("CVM: Built trust transaction: from=%s, to=%s, weight=%d, bond=%s, fee=%s\n",
              HexStr(userAddress), HexStr(toAddress), weight,
              FormatMoney(bondAmount), FormatMoney(fee));
    
    return tx;
}

CMutableTransaction CVMTransactionBuilder::BuildBondedVoteTransaction(
    CWallet* wallet,
    const uint160& targetAddress,
    int16_t voteValue,
    CAmount bondAmount,
    const std::string& reason,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    // Validate parameters
    if (voteValue < -100 || voteValue > 100) {
        error = "Vote value must be between -100 and +100";
        return tx;
    }
    
    if (bondAmount < COIN / 100) {
        error = "Bond amount must be at least 0.01 CAS";
        return tx;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Get user's address for bond script
    CPubKey userKey;
    if (!wallet->GetKeyFromPool(userKey)) {
        error = "Failed to get key from wallet";
        return tx;
    }
    uint160 voterAddress = userKey.GetID();
    
    // 2. Build CVM bonded vote data
    CVMBondedVoteData voteData;
    voteData.voter = voterAddress;
    voteData.target = targetAddress;
    voteData.voteValue = voteValue;
    voteData.bondAmount = bondAmount;
    voteData.timestamp = static_cast<uint32_t>(GetTime());
    
    // 3. Create OP_RETURN output with vote data
    std::vector<uint8_t> voteBytes = voteData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::BONDED_VOTE, voteBytes);
    
    CTxOut cvmOutput(0, cvmScript);
    tx.vout.push_back(cvmOutput);
    
    // 4. Create bond script and add bond output
    CScript bondScript = CreateBondScript(voterAddress, 1440); // 1440 blocks = ~1 day
    if (!AddBondOutput(tx, bondScript, bondAmount)) {
        error = "Failed to add bond output";
        return CMutableTransaction();
    }
    
    // 5. Estimate fee
    CAmount estimatedFee = CalculateFee(tx);
    CAmount totalNeeded = estimatedFee + bondAmount + DUST_THRESHOLD;
    
    // 6. Select coins
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, totalNeeded, nullptr, selectedCoins, totalSelected)) {
        error = strprintf("Insufficient funds: need %s", FormatMoney(totalNeeded));
        return CMutableTransaction();
    }
    
    // 7. Add inputs
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 8. Calculate actual fee and change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee - bondAmount;
    
    // 9. Add change output if not dust
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
        // Recalculate
        actualFee = CalculateFee(tx);
        change = totalSelected - actualFee - bondAmount;
        if (change < 0) {
            error = "Insufficient funds after adding change";
            return CMutableTransaction();
        }
    } else {
        actualFee = totalSelected - bondAmount;
    }
    
    fee = actualFee;
    
    LogPrintf("CVM: Built bonded vote transaction: voter=%s, target=%s, value=%d, bond=%s, fee=%s\n",
              HexStr(voterAddress), HexStr(targetAddress), voteValue,
              FormatMoney(bondAmount), FormatMoney(fee));
    
    return tx;
}

CMutableTransaction CVMTransactionBuilder::BuildDisputeTransaction(
    CWallet* wallet,
    const uint256& originalVoteTx,
    CAmount challengeBond,
    const std::string& reason,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    // Validate parameters
    if (challengeBond < COIN / 10) {
        error = "Challenge bond must be at least 0.1 CAS";
        return tx;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Get challenger's address for bond script
    CPubKey challengerKey;
    if (!wallet->GetKeyFromPool(challengerKey)) {
        error = "Failed to get key from wallet";
        return tx;
    }
    uint160 challengerAddress = challengerKey.GetID();
    
    // 2. Build CVM dispute data
    CVMDAODisputeData disputeData;
    disputeData.challenger = challengerAddress;
    disputeData.originalVoteTxHash = originalVoteTx;
    disputeData.challengeBond = challengeBond;
    disputeData.reason = reason.substr(0, 64); // Max 64 chars
    disputeData.timestamp = static_cast<uint32_t>(GetTime());
    
    // 3. Create OP_RETURN output with dispute data
    std::vector<uint8_t> disputeBytes = disputeData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::DAO_DISPUTE, disputeBytes);
    
    CTxOut cvmOutput(0, cvmScript);
    tx.vout.push_back(cvmOutput);
    
    // 4. Create bond script and add challenge bond output
    CScript bondScript = CreateBondScript(challengerAddress, 2880); // 2880 blocks = ~2 days
    if (!AddBondOutput(tx, bondScript, challengeBond)) {
        error = "Failed to add challenge bond output";
        return CMutableTransaction();
    }
    
    // 5. Estimate fee
    CAmount estimatedFee = CalculateFee(tx);
    CAmount totalNeeded = estimatedFee + challengeBond + DUST_THRESHOLD;
    
    // 6. Select coins
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, totalNeeded, nullptr, selectedCoins, totalSelected)) {
        error = strprintf("Insufficient funds: need %s", FormatMoney(totalNeeded));
        return CMutableTransaction();
    }
    
    // 7. Add inputs
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 8. Calculate actual fee and change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee - challengeBond;
    
    // 9. Add change output if not dust
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
        // Recalculate
        actualFee = CalculateFee(tx);
        change = totalSelected - actualFee - challengeBond;
        if (change < 0) {
            error = "Insufficient funds after adding change";
            return CMutableTransaction();
        }
    } else {
        actualFee = totalSelected - challengeBond;
    }
    
    fee = actualFee;
    
    LogPrintf("CVM: Built dispute transaction: challenger=%s, vote_tx=%s, bond=%s, fee=%s\n",
              HexStr(challengerAddress), originalVoteTx.ToString(),
              FormatMoney(challengeBond), FormatMoney(fee));
    
    return tx;
}

CMutableTransaction CVMTransactionBuilder::BuildDisputeVoteTransaction(
    CWallet* wallet,
    const uint256& disputeId,
    bool supportSlash,
    CAmount stake,
    CAmount& fee,
    std::string& error
) {
    CMutableTransaction tx;
    
    if (!wallet) {
        error = "Wallet not available";
        return tx;
    }
    
    // Validate parameters
    if (stake < COIN / 10) {
        error = "Stake must be at least 0.1 CAS";
        return tx;
    }
    
    LOCK2(cs_main, wallet->cs_wallet);
    
    // 1. Get voter's address for stake script
    CPubKey voterKey;
    if (!wallet->GetKeyFromPool(voterKey)) {
        error = "Failed to get key from wallet";
        return tx;
    }
    uint160 voterAddress = voterKey.GetID();
    
    // 2. Build CVM dispute vote data
    CVMDAOVoteData voteData;
    voteData.daoMember = voterAddress;
    voteData.disputeId = disputeId;
    voteData.supportSlash = supportSlash;
    voteData.stake = stake;
    voteData.timestamp = static_cast<uint32_t>(GetTime());
    
    // 3. Create OP_RETURN output with vote data
    std::vector<uint8_t> voteBytes = voteData.Serialize();
    CScript cvmScript = BuildCVMOpReturn(CVMOpType::DAO_VOTE, voteBytes);
    
    CTxOut cvmOutput(0, cvmScript);
    tx.vout.push_back(cvmOutput);
    
    // 4. Create stake script and add stake output
    CScript stakeScript = CreateBondScript(voterAddress, 2880); // 2880 blocks = ~2 days
    if (!AddBondOutput(tx, stakeScript, stake)) {
        error = "Failed to add stake output";
        return CMutableTransaction();
    }
    
    // 5. Estimate fee
    CAmount estimatedFee = CalculateFee(tx);
    CAmount totalNeeded = estimatedFee + stake + DUST_THRESHOLD;
    
    // 6. Select coins
    std::vector<COutput> selectedCoins;
    CAmount totalSelected = 0;
    
    if (!SelectCoins(wallet, totalNeeded, nullptr, selectedCoins, totalSelected)) {
        error = strprintf("Insufficient funds: need %s", FormatMoney(totalNeeded));
        return CMutableTransaction();
    }
    
    // 7. Add inputs
    for (const COutput& coin : selectedCoins) {
        CTxIn input(coin.tx->GetHash(), coin.i);
        tx.vin.push_back(input);
    }
    
    // 8. Calculate actual fee and change
    CAmount actualFee = CalculateFee(tx);
    CAmount change = totalSelected - actualFee - stake;
    
    // 9. Add change output if not dust
    if (change > DUST_THRESHOLD) {
        if (!AddChangeOutput(tx, wallet, change)) {
            error = "Failed to add change output";
            return CMutableTransaction();
        }
        // Recalculate
        actualFee = CalculateFee(tx);
        change = totalSelected - actualFee - stake;
        if (change < 0) {
            error = "Insufficient funds after adding change";
            return CMutableTransaction();
        }
    } else {
        actualFee = totalSelected - stake;
    }
    
    fee = actualFee;
    
    LogPrintf("CVM: Built dispute vote transaction: voter=%s, dispute=%s, slash=%d, stake=%s, fee=%s\n",
              HexStr(voterAddress), disputeId.ToString(), supportSlash,
              FormatMoney(stake), FormatMoney(fee));
    
    return tx;
}

} // namespace CVM

