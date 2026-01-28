// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// NOTE: This file is intended to be customised by the end user, and includes only local node policy logic

#include <policy/policy.h>

#include <consensus/validation.h>
#include <validation.h>
#include <coins.h>
#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <chainparams.h>    // Cascoin: Hive
#include <base58.h>    // Cascoin: Hive

CAmount GetDustThreshold(const CTxOut& txout, const CFeeRate& dustRelayFeeIn)
{
    // "Dust" is defined in terms of dustRelayFee,
    // which has units satoshis-per-kilobyte.
    // If you'd pay more in fees than the value of the output
    // to spend something, then we consider it dust.
    // A typical spendable non-segwit txout is 34 bytes big, and will
    // need a CTxIn of at least 148 bytes to spend:
    // so dust is a spendable txout less than
    // 182*dustRelayFee/1000 (in satoshis).
    // 546 satoshis at the default rate of 3000 sat/kB.
    // A typical spendable segwit txout is 31 bytes big, and will
    // need a CTxIn of at least 67 bytes to spend:
    // so dust is a spendable txout less than
    // 98*dustRelayFee/1000 (in satoshis).
    // 294 satoshis at the default rate of 3000 sat/kB.
    if (txout.scriptPubKey.IsUnspendable())
        return 0;

    size_t nSize = GetSerializeSize(txout, SER_DISK, 0);
    int witnessversion = 0;
    std::vector<unsigned char> witnessprogram;

    if (txout.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        // sum the sizes of the parts of a transaction input
        // with 75% segwit discount applied to the script size.
        nSize += (32 + 4 + 1 + (107 / WITNESS_SCALE_FACTOR) + 4);
    } else {
        nSize += (32 + 4 + 1 + 107 + 4); // the 148 mentioned above
    }

    return dustRelayFeeIn.GetFee(nSize);
}

bool IsDust(const CTxOut& txout, const CFeeRate& dustRelayFeeIn)
{
    return (txout.nValue < GetDustThreshold(txout, dustRelayFeeIn));
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType, const bool witnessEnabled)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_MULTISIG)
    {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    } else if (whichType == TX_NULL_DATA &&
               (!fAcceptDatacarrier || scriptPubKey.size() > nMaxDatacarrierBytes))
          return false;

    else if (!witnessEnabled && (whichType == TX_WITNESS_V0_KEYHASH || whichType == TX_WITNESS_V0_SCRIPTHASH))
        return false;

    return whichType != TX_NONSTANDARD && whichType != TX_WITNESS_UNKNOWN;
}

bool IsStandardTx(const CTransaction& tx, std::string& reason, const bool witnessEnabled)
{
    if (tx.nVersion > CTransaction::MAX_STANDARD_VERSION || tx.nVersion < 1) {
        reason = "version";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_WEIGHT mitigates CPU exhaustion attacks.
    unsigned int sz = GetTransactionWeight(tx);
    if (sz >= MAX_STANDARD_TX_WEIGHT) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn& txin : tx.vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys (remember the 520 byte limit on redeemScript size). That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard.
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    txnouttype whichType;

    const Consensus::Params& consensusParams = Params().GetConsensus();     // Cascoin: Hive
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.beeCreationAddress));   // Cascoin: Hive

    // Check if this transaction might be a bee creation transaction by looking at all outputs
    for (const CTxOut& txout : tx.vout) {
        // Special handling for bee creation transactions
        if (txout.scriptPubKey.size() >= 2 && 
            txout.scriptPubKey[0] == OP_RETURN && 
            txout.scriptPubKey[1] == OP_BEE) {
            // Accept any transaction with OP_RETURN OP_BEE marker as standard
            // This helps bootstrap The Labyrinth mining system
            return true;
        }
        
        // Special handling for Mice NFT transactions (CASTOK = tokenize, CASXFR = transfer)
        if (txout.scriptPubKey.size() >= 8 && 
            txout.scriptPubKey[0] == OP_RETURN &&
            txout.scriptPubKey[1] == 0x06) {  // PUSH 6 bytes
            std::vector<unsigned char> magic(txout.scriptPubKey.begin() + 2, txout.scriptPubKey.begin() + 8);
            std::vector<unsigned char> castok = {'C', 'A', 'S', 'T', 'O', 'K'};
            std::vector<unsigned char> casxfr = {'C', 'A', 'S', 'X', 'F', 'R'};
            if (magic == castok || magic == casxfr) {
                // Accept Mice NFT transactions as standard
                return true;
            }
        }
        
        // Standard BCT check
        if (CScript::IsBCTScript(txout.scriptPubKey, scriptPubKeyBCF))      // Cascoin: Hive
            return true;                                                    // Cascoin: Hive

        // Cascoin: Rialto: Not needed :) Nick registrations are valid transactions to unupgraded clients.
        //if (tx.IsNCT(consensusParams, scriptPubKeyNCF))                   // Cascoin: Rialto
        //    return true;                                                  // Cascoin: Rialto

        if (!::IsStandard(txout.scriptPubKey, whichType, witnessEnabled)) {
            reason = "scriptpubkey";
            return false;
        }

        if (whichType == TX_NULL_DATA)
            nDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (IsDust(txout, ::dustRelayFee)) {
            reason = "dust";
            return false;
        }
    }

    // only one OP_RETURN txout is permitted
    if (nDataOut > 1) {
        reason = "multi-op-return";
        return false;
    }

    return true;
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 *
 * Why bother? To avoid denial-of-service attacks; an attacker
 * can submit a standard HASH... OP_EQUAL transaction,
 * which will get accepted into blocks. The redemption
 * script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxOut& prev = mapInputs.AccessCoin(tx.vin[i].prevout).out;

        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            std::vector<std::vector<unsigned char> > stack;
            // convert the scriptSig into a stack, so we can inspect the redeemScript
            if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SIGVERSION_BASE))
                return false;
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            if (subscript.GetSigOpCount(true) > MAX_P2SH_SIGOPS) {
                return false;
            }
        }
    }

    return true;
}

bool IsWitnessStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase())
        return true; // Coinbases are skipped

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        // We don't care if witness for this input is empty, since it must not be bloated.
        // If the script is invalid without witness, it would be caught sooner or later during validation.
        if (tx.vin[i].scriptWitness.IsNull())
            continue;

        const CTxOut &prev = mapInputs.AccessCoin(tx.vin[i].prevout).out;

        // get the scriptPubKey corresponding to this input:
        CScript prevScript = prev.scriptPubKey;

        if (prevScript.IsPayToScriptHash()) {
            std::vector <std::vector<unsigned char> > stack;
            // If the scriptPubKey is P2SH, we try to extract the redeemScript casually by converting the scriptSig
            // into a stack. We do not check IsPushOnly nor compare the hash as these will be done later anyway.
            // If the check fails at this stage, we know that this txid must be a bad one.
            if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SIGVERSION_BASE))
                return false;
            if (stack.empty())
                return false;
            prevScript = CScript(stack.back().begin(), stack.back().end());
        }

        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;

        // Non-witness program must not be associated with any witness
        if (!prevScript.IsWitnessProgram(witnessversion, witnessprogram))
            return false;

        // Check P2WSH standard limits
        if (witnessversion == 0 && witnessprogram.size() == 32) {
            if (tx.vin[i].scriptWitness.stack.back().size() > MAX_STANDARD_P2WSH_SCRIPT_SIZE)
                return false;
            size_t sizeWitnessStack = tx.vin[i].scriptWitness.stack.size() - 1;
            if (sizeWitnessStack > MAX_STANDARD_P2WSH_STACK_ITEMS)
                return false;
            for (unsigned int j = 0; j < sizeWitnessStack; j++) {
                if (tx.vin[i].scriptWitness.stack[j].size() > MAX_STANDARD_P2WSH_STACK_ITEM_SIZE)
                    return false;
            }
        }
        
        // Cascoin: Check witness version 2 (quantum) standard limits
        // Requirements: 9.7 (enforce maximum 1024 bytes per signature)
        if (witnessversion == 2 && witnessprogram.size() == 32) {
            // Quantum witness should have exactly 2 stack items: [signature, pubkey]
            if (tx.vin[i].scriptWitness.stack.size() != 2)
                return false;
            
            const std::vector<unsigned char>& signature = tx.vin[i].scriptWitness.stack[0];
            const std::vector<unsigned char>& pubkey = tx.vin[i].scriptWitness.stack[1];
            
            // Enforce signature size limit (Requirements 9.7)
            if (signature.size() > MAX_QUANTUM_SIGNATURE_SIZE)
                return false;
            
            // Enforce pubkey size (must be exactly 897 bytes for FALCON-512)
            if (pubkey.size() != QUANTUM_PUBLIC_KEY_SIZE)
                return false;
        }
    }
    return true;
}

CFeeRate incrementalRelayFee = CFeeRate(DEFAULT_INCREMENTAL_RELAY_FEE);
CFeeRate dustRelayFee = CFeeRate(DUST_RELAY_TX_FEE);
unsigned int nBytesPerSigOp = DEFAULT_BYTES_PER_SIGOP;

int64_t GetVirtualTransactionSize(int64_t nWeight, int64_t nSigOpCost)
{
    return (std::max(nWeight, nSigOpCost * nBytesPerSigOp) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
}

int64_t GetVirtualTransactionSize(const CTransaction& tx, int64_t nSigOpCost)
{
    return GetVirtualTransactionSize(GetTransactionWeight(tx), nSigOpCost);
}

/**
 * Cascoin: Check if a transaction contains quantum (witness version 2) signatures.
 * This is used for fee estimation and relay filtering.
 * Requirements: 9.6 (include quantum signature size in vsize calculation)
 */
bool HasQuantumSignatures(const CTransaction& tx)
{
    for (const CTxIn& txin : tx.vin) {
        if (txin.scriptWitness.IsNull())
            continue;
        
        // Check if this is a witness version 2 (quantum) input
        // Quantum witness has exactly 2 stack items: [signature, pubkey]
        // where pubkey is 897 bytes (FALCON-512)
        if (txin.scriptWitness.stack.size() == 2) {
            const std::vector<unsigned char>& pubkey = txin.scriptWitness.stack[1];
            if (pubkey.size() == QUANTUM_PUBLIC_KEY_SIZE) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Cascoin: Calculate the quantum signature overhead for fee estimation.
 * Quantum signatures are larger than ECDSA signatures, so we need to account
 * for this in fee estimation.
 * Requirements: 9.6 (include quantum signature size in vsize calculation)
 * 
 * @param[in] tx The transaction to analyze
 * @return The additional virtual size overhead from quantum signatures
 */
int64_t GetQuantumSignatureOverhead(const CTransaction& tx)
{
    int64_t overhead = 0;
    
    for (const CTxIn& txin : tx.vin) {
        if (txin.scriptWitness.IsNull())
            continue;
        
        // Check if this is a quantum witness (2 stack items with 897-byte pubkey)
        if (txin.scriptWitness.stack.size() == 2) {
            const std::vector<unsigned char>& pubkey = txin.scriptWitness.stack[1];
            if (pubkey.size() == QUANTUM_PUBLIC_KEY_SIZE) {
                // Quantum signature overhead compared to typical ECDSA:
                // ECDSA: ~72 byte signature + 33 byte pubkey = ~105 bytes
                // Quantum: ~666 byte signature + 897 byte pubkey = ~1563 bytes
                // Overhead: ~1458 bytes per quantum input
                // 
                // However, witness data already has 4x discount in weight calculation,
                // so we don't need additional overhead here - the weight calculation
                // already accounts for the larger signature size.
                // 
                // This function returns 0 because GetTransactionWeight() already
                // includes the full witness data size in its calculation.
                overhead += 0;
            }
        }
    }
    
    return overhead;
}

/**
 * Cascoin: Check if quantum witness data is within standard limits.
 * Requirements: 9.7 (enforce maximum 1024 bytes per signature)
 * 
 * @param[in] tx The transaction to check
 * @param[in] mapInputs Map of previous transaction outputs
 * @return true if all quantum witnesses are within standard limits
 */
bool IsQuantumWitnessStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase())
        return true;
    
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        if (tx.vin[i].scriptWitness.IsNull())
            continue;
        
        const CTxOut& prev = mapInputs.AccessCoin(tx.vin[i].prevout).out;
        CScript prevScript = prev.scriptPubKey;
        
        // Handle P2SH-wrapped witness
        if (prevScript.IsPayToScriptHash()) {
            std::vector<std::vector<unsigned char>> stack;
            if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SIGVERSION_BASE))
                return false;
            if (stack.empty())
                return false;
            prevScript = CScript(stack.back().begin(), stack.back().end());
        }
        
        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;
        
        if (!prevScript.IsWitnessProgram(witnessversion, witnessprogram))
            continue;
        
        // Check witness version 2 (quantum) standard limits
        if (witnessversion == 2 && witnessprogram.size() == 32) {
            // Quantum witness should have exactly 2 stack items: [signature, pubkey]
            if (tx.vin[i].scriptWitness.stack.size() != 2)
                return false;
            
            const std::vector<unsigned char>& signature = tx.vin[i].scriptWitness.stack[0];
            const std::vector<unsigned char>& pubkey = tx.vin[i].scriptWitness.stack[1];
            
            // Enforce signature size limit (Requirements 9.7)
            if (signature.size() > MAX_QUANTUM_SIGNATURE_SIZE)
                return false;
            
            // Enforce pubkey size (must be exactly 897 bytes for FALCON-512)
            if (pubkey.size() != QUANTUM_PUBLIC_KEY_SIZE)
                return false;
        }
    }
    
    return true;
}
