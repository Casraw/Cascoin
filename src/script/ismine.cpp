// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/ismine.h>

#include <key.h>
#include <keystore.h>
#include <script/script.h>
#include <script/sign.h>
#include <pubkey.h>


typedef std::vector<unsigned char> valtype;

/**
 * Check if the keystore contains a quantum key matching the given witness program.
 * The witness program for quantum addresses is the SHA256 hash of the FALCON-512 public key.
 *
 * Compatibility note: Pre-endianness-fix transactions stored the witness program
 * in big-endian (BE) byte order inside the script, while GetQuantumID() returns
 * little-endian (LE) bytes (the native uint256 layout).  Post-fix transactions
 * store LE directly.  To recognise both old and new UTXOs we compare against
 * the program as-is (LE, new format) AND against its byte-reversed form (BE,
 * legacy format).  This dual check can be removed after a testnet reset.
 *
 * @param keystore The keystore to search
 * @param witnessProgram The 32-byte witness program extracted from the script
 * @return true if a matching quantum key is found
 */
static bool HaveQuantumKey(const CKeyStore& keystore, const uint256& witnessProgram)
{
    // Pre-compute the byte-reversed witness program for legacy (BE) compatibility.
    uint256 witnessProgramReversed;
    const unsigned char* src = witnessProgram.begin();
    unsigned char* dst = witnessProgramReversed.begin();
    for (size_t i = 0; i < 32; i++) {
        dst[i] = src[31 - i];
    }

    std::set<CKeyID> keys = keystore.GetKeys();

    for (const CKeyID& keyID : keys) {
        CKey key;
        if (keystore.GetKey(keyID, key) && key.IsQuantum()) {
            CPubKey pubkey = key.GetPubKey();
            if (pubkey.IsValid() && pubkey.IsQuantum() && pubkey.size() > 0) {
                uint256 quantumID = pubkey.GetQuantumID();
                // Match against new (LE) format
                if (quantumID == witnessProgram) {
                    return true;
                }
                // Match against legacy (BE) format (pre-endianness-fix UTXOs)
                if (quantumID == witnessProgramReversed) {
                    return true;
                }
            }
        }
    }
    return false;
}

unsigned int HaveKeys(const std::vector<valtype>& pubkeys, const CKeyStore& keystore)
{
    unsigned int nResult = 0;
    for (const valtype& pubkey : pubkeys)
    {
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (keystore.HaveKey(keyID))
            ++nResult;
    }
    return nResult;
}

isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey, SigVersion sigversion)
{
    bool isInvalid = false;
    return IsMine(keystore, scriptPubKey, isInvalid, sigversion);
}

isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest, SigVersion sigversion)
{
    bool isInvalid = false;
    return IsMine(keystore, dest, isInvalid, sigversion);
}

isminetype IsMine(const CKeyStore &keystore, const CTxDestination& dest, bool& isInvalid, SigVersion sigversion)
{
    CScript script = GetScriptForDestination(dest);
    return IsMine(keystore, script, isInvalid, sigversion);
}

isminetype IsMine(const CKeyStore &keystore, const CScript& scriptPubKey, bool& isInvalid, SigVersion sigversion)
{
    isInvalid = false;

    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions)) {
        if (keystore.HaveWatchOnly(scriptPubKey))
            return ISMINE_WATCH_UNSOLVABLE;
        return ISMINE_NO;
    }

    CKeyID keyID;
    switch (whichType)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        break;
    case TX_WITNESS_UNKNOWN:
    {
        // Check for witness version 2 with 32-byte program (quantum address)
        // vSolutions[0] contains the witness version, vSolutions[1] contains the program
        if (vSolutions.size() >= 2) {
            unsigned int witnessVersion = vSolutions[0][0];
            if (witnessVersion == 2 && vSolutions[1].size() == 32) {
                // This is a quantum address - check if we have the corresponding quantum key
                // The witness program is now stored in canonical LE order (matching GetQuantumID()),
                // so we can copy directly without byte reversal.
                uint256 witnessProgram;
                memcpy(witnessProgram.begin(), vSolutions[1].data(), 32);
                
                if (HaveQuantumKey(keystore, witnessProgram)) {
                    return ISMINE_SPENDABLE;
                }
            }
        }
        break;
    }
    case TX_WITNESS_V2_QUANTUM:
    {
        // Cascoin: Quantum address (witness version 2)
        // vSolutions[0] contains the 32-byte witness program (SHA256 of pubkey)
        // The witness program is now stored in canonical LE order (matching GetQuantumID()),
        // so we can copy directly without byte reversal.
        if (vSolutions.size() >= 1 && vSolutions[0].size() == 32) {
            uint256 witnessProgram;
            memcpy(witnessProgram.begin(), vSolutions[0].data(), 32);
            
            if (HaveQuantumKey(keystore, witnessProgram)) {
                return ISMINE_SPENDABLE;
            }
        }
        break;
    }
    case TX_PUBKEY:
        keyID = CPubKey(vSolutions[0]).GetID();
        if (sigversion != SIGVERSION_BASE && vSolutions[0].size() != 33) {
            isInvalid = true;
            return ISMINE_NO;
        }
        if (keystore.HaveKey(keyID))
            return ISMINE_SPENDABLE;
        break;
    case TX_WITNESS_V0_KEYHASH:
    {
        if (!keystore.HaveCScript(CScriptID(CScript() << OP_0 << vSolutions[0]))) {
            // We do not support bare witness outputs unless the P2SH version of it would be
            // acceptable as well. This protects against matching before segwit activates.
            // This also applies to the P2WSH case.
            break;
        }
        isminetype ret = ::IsMine(keystore, GetScriptForDestination(CKeyID(uint160(vSolutions[0]))), isInvalid, SIGVERSION_WITNESS_V0);
        if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
            return ret;
        break;
    }
    case TX_PUBKEYHASH:
        keyID = CKeyID(uint160(vSolutions[0]));
        if (sigversion != SIGVERSION_BASE) {
            CPubKey pubkey;
            if (keystore.GetPubKey(keyID, pubkey) && !pubkey.IsCompressed()) {
                isInvalid = true;
                return ISMINE_NO;
            }
        }
        if (keystore.HaveKey(keyID))
            return ISMINE_SPENDABLE;
        break;
    case TX_SCRIPTHASH:
    {
        CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
        CScript subscript;
        if (keystore.GetCScript(scriptID, subscript)) {
            isminetype ret = IsMine(keystore, subscript, isInvalid);
            if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
                return ret;
        }
        break;
    }
    case TX_WITNESS_V0_SCRIPTHASH:
    {
        if (!keystore.HaveCScript(CScriptID(CScript() << OP_0 << vSolutions[0]))) {
            break;
        }
        uint160 hash;
        CRIPEMD160().Write(&vSolutions[0][0], vSolutions[0].size()).Finalize(hash.begin());
        CScriptID scriptID = CScriptID(hash);
        CScript subscript;
        if (keystore.GetCScript(scriptID, subscript)) {
            isminetype ret = IsMine(keystore, subscript, isInvalid, SIGVERSION_WITNESS_V0);
            if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
                return ret;
        }
        break;
    }

    case TX_MULTISIG:
    {
        // Only consider transactions "mine" if we own ALL the
        // keys involved. Multi-signature transactions that are
        // partially owned (somebody else has a key that can spend
        // them) enable spend-out-from-under-you attacks, especially
        // in shared-wallet situations.
        std::vector<valtype> keys(vSolutions.begin()+1, vSolutions.begin()+vSolutions.size()-1);
        if (sigversion != SIGVERSION_BASE) {
            for (size_t i = 0; i < keys.size(); i++) {
                if (keys[i].size() != 33) {
                    isInvalid = true;
                    return ISMINE_NO;
                }
            }
        }
        if (HaveKeys(keys, keystore) == keys.size())
            return ISMINE_SPENDABLE;
        break;
    }
    }

    if (keystore.HaveWatchOnly(scriptPubKey)) {
        // TODO: This could be optimized some by doing some work after the above solver
        SignatureData sigs;
        return ProduceSignature(DummySignatureCreator(&keystore), scriptPubKey, sigs) ? ISMINE_WATCH_SOLVABLE : ISMINE_WATCH_UNSOLVABLE;
    }
    return ISMINE_NO;
}
