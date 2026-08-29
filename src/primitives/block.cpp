// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <crypto/common.h>
#include <crypto/scrypt.h>
#include <chainparams.h>    // Cascoin: Hive

#include <crypto/minotaurx/minotaur.h>  // Cascoin: MinotaurX+Hive1.2
#include <validation.h>                 // Cascoin: MinotaurX+Hive1.2
#include <util.h>                       // Cascoin: MinotaurX+Hive1.2

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

/*
// Cascoin: MinotaurX+Hive1.2: Hash arbitrary data, using internally-managed thread-local memory for YP
uint256 CBlockHeader::MinotaurXHashArbitrary(const char* data) {
    return Minotaur(data, data + strlen(data), true);
}

// Cascoin: MinotaurX+Hive1.2: Hash a string with MinotaurX, using provided YP thread-local memory
uint256 CBlockHeader::MinotaurXHashStringWithLocal(std::string data, yespower_local_t *local) {
    return Minotaur(data.begin(), data.end(), true, local);
}*/

// Cascoin: MinotaurX+Hive1.2: Hash arbitrary data with classical Minotaur
uint256 CBlockHeader::MinotaurHashArbitrary(const char* data) {
    return Minotaur(data, data + strlen(data), false);
}

// Cascoin: MinotaurX+Hive1.2: Hash a string with classical Minotaur
uint256 CBlockHeader::MinotaurHashString(std::string data) {
    return Minotaur(data.begin(), data.end(), false);
}

// Cascoin: MinotaurX+Hive1.2: Get pow hash based on block type and UASF activation
uint256 CBlockHeader::GetPoWHash() const
{
    // Throttled summary logging for PoW hashing (only when -debug=minotaurx)
    const bool doLog = LogAcceptCategory(BCLog::MINOTAURX);
    static uint64_t calls_total = 0;
    static uint64_t calls_postfork = 0;
    static uint64_t calls_prefork = 0;
    static uint64_t type_sha256 = 0;
    static uint64_t type_minotaurx = 0;
    static uint64_t type_scrypt = 0;
    static int64_t next_summary_ms = 0;
    static int64_t period_start_ms = 0;
    const bool postFork = (nTime > Params().GetConsensus().powForkTime);
    auto emit_summary_if_due = [&]() {
        if (!doLog) return;
        const int64_t now = GetTimeMillis();
        if (next_summary_ms == 0) {
            next_summary_ms = now + 5000; // 5s window
            period_start_ms = now;
        }
        if (now >= next_summary_ms) {
            const int64_t elapsed = (period_start_ms > 0) ? (now - period_start_ms) : 0;
            LogPrint(BCLog::MINOTAURX, "GetPoWHash: %u calls in last %d ms (postFork=%u, preFork=%u; types: sha256=%u, minotaurx=%u, scrypt=%u)\n",
                (unsigned)calls_total, (int)elapsed, (unsigned)calls_postfork, (unsigned)calls_prefork,
                (unsigned)type_sha256, (unsigned)type_minotaurx, (unsigned)type_scrypt);
            calls_total = calls_postfork = calls_prefork = 0;
            type_sha256 = type_minotaurx = type_scrypt = 0;
            period_start_ms = now;
            next_summary_ms = now + 5000;
        }
    };

    if (postFork) { // Multi-algo logic is active

        POW_TYPE calculatedPowType = GetPoWType(); // (nVersion >> 16) & 0xFF

        // Explicit check for known algorithm types based on extracted bits
        if (calculatedPowType == POW_TYPE_MINOTAURX) {
            if (doLog) { calls_total++; calls_postfork++; type_minotaurx++; emit_summary_if_due(); }
            return Minotaur(BEGIN(nVersion), END(nNonce), true);
        }
        
        // Check for BIP9-style versioning (e.g. 0x20000000 for standard SHA256 blocks if other bits aren't set for specific algos)
        // This was the previous logic's first check after powForkTime.
        if (nVersion >= 0x20000000) {
             if (doLog) { calls_total++; calls_postfork++; type_sha256++; emit_summary_if_due(); }
             return GetHash();
        }

        // If not MinotaurX by explicit bits, and not a high-bit BIP9 version, then default to SHA256.
        // This covers PoWType being explicitly POW_TYPE_SHA256 (0) or any other value not caught above (like 217 from 0x04d98000).
        if (doLog) { calls_total++; calls_postfork++; type_sha256++; emit_summary_if_due(); }
        return GetHash(); // Default to SHA256

    } else { // Pre-multi-algo fork (powForkTime not reached)
        uint256 thash;
        scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
        if (doLog) { calls_total++; calls_prefork++; type_scrypt++; emit_summary_if_due(); }
        return thash;
    }
}

// Cascoin: Add helper definition
POW_TYPE CBlockHeader::GetEffectivePoWTypeForHashing(const Consensus::Params& consensusParams) const {
    // Check if we are in the multi-algorithm phase based on block time
    if (nTime > consensusParams.powForkTime) {
        // Multi-algorithm phase logic
        POW_TYPE calculatedPowType = GetPoWType(); // Get raw PoW type from nVersion bits: (nVersion >> 16) & 0xFF

        // Explicitly check for MinotaurX
        if (calculatedPowType == POW_TYPE_MINOTAURX) {
            return POW_TYPE_MINOTAURX;
        }
        
        // Check for BIP9-style versioning (e.g. 0x20000000 typically implies SHA256 if not MinotaurX)
        if (nVersion >= 0x20000000) {
             return POW_TYPE_SHA256;
        }

        // Default for multi-algo phase if not MinotaurX and not a high-bit nVersion:
        // This covers cases where calculatedPowType is POW_TYPE_SHA256 (0) or an "unknown" type (like 41 from 0x0a290000).
        // GetPoWHash would default to SHA256 (GetHash()) in these scenarios.
        return POW_TYPE_SHA256;

    } else {
        // Pre-multi-algorithm fork phase: Default to Scrypt (as per original Litecoin Cash behavior pre-Hive/MinotaurX)
        return POW_TYPE_SCRYPT; // LCC's original PoW was Scrypt
    }
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    // Cascoin: Hive: Include type
    bool isHive = IsHiveMined(Params().GetConsensus());
    s << strprintf("CBlock(type=%s, hash=%s, powHash=%s, powType=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        isHive ? "hive" : "pow",
        GetHash().ToString(),
        GetPoWHash().ToString(),
        isHive ? "n/a" : GetPoWTypeName(),  // Cascoin: MinotaurX+Hive1.2: Include pow type name
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
