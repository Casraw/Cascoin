// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util.h>
#include <core_io.h>            // Cascoin: Hive
#include <script/standard.h>    // Cascoin: Hive
#include <base58.h>             // Cascoin: Hive
#include <pubkey.h>             // Cascoin: Hive
#include <hash.h>               // Cascoin: Hive
#include <sync.h>               // Cascoin: Hive
#include <validation.h>         // Cascoin: Hive
#include <utilstrencodings.h>   // Cascoin: Hive

MousePopGraphPoint mousePopGraph[1024*40];       // Cascoin: Hive

// Cascoin: MinotaurX+Hive1.2: Diff adjustment for pow algos (post-MinotaurX activation)
// Modified LWMA-3
// Copyright (c) 2017-2021 The Bitcoin Gold developers, Zawy, iamstenman (Microbitcoin), The Cascoin developers
// MIT License
// Algorithm by Zawy, a modification of WT-144 by Tom Harding
// For updates see
// https://github.com/zawy12/difficulty-algorithms/issues/3#issuecomment-442129791
unsigned int GetNextWorkRequiredLWMA(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, const POW_TYPE powType) {
    const bool verbose = LogAcceptCategory(BCLog::MINOTAURX);
    const arith_uint256 powLimit = UintToArith256(params.powTypeLimits[powType]);   // Max target limit (easiest diff)
    const int64_t T = params.nPowTargetSpacing * 2;                                 // Target freq
    const int64_t N = params.lwmaAveragingWindow;                                   // Window size
    const int64_t k = N * (N + 1) * T / 2;                                          // Constant for proper averaging after weighting solvetimes
    const int64_t height = pindexLast->nHeight;                                     // Block height
    
    // TESTNET ONLY: Allow minimum difficulty blocks if we haven't seen a block for ostensibly 10 blocks worth of time.
    // Reading this code because you're porting CAS features? Considering doing this on your mainnet?
    // ***** THIS IS NOT SAFE TO DO ON YOUR MAINNET! *****
    if (params.fPowAllowMinDifficultyBlocks && pblock->GetBlockTime() > pindexLast->GetBlockTime() + T * 10) {
        if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (apparent testnet stall)\\n", POW_TYPE_NAMES[powType]);
        // Cascoin: Added detailed SHA256 logging
        if (powType == POW_TYPE_SHA256) {
            LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Testnet stall. Returning powLimit 0x%08x\\n", powLimit.GetCompact());
        }
        return powLimit.GetCompact();
    }

    // Not enough blocks on chain? Return limit
    if (height < N) {
        if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (short chain)\\n", POW_TYPE_NAMES[powType]);
        // Cascoin: Added detailed SHA256 logging
        if (powType == POW_TYPE_SHA256) {
            LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Short chain (height %lld < N %lld). Returning powLimit 0x%08x\\n", (long long)height, (long long)N, powLimit.GetCompact());
        }
        return powLimit.GetCompact();
    }

    arith_uint256 avgTarget, nextTarget;
    int64_t thisTimestamp, previousTimestamp;
    int64_t sumWeightedSolvetimes = 0, j = 0, blocksFound = 0;

    // Find previousTimestamp (N blocks of this blocktype back), and build list of wanted-type blocks as we go
    std::vector<const CBlockIndex*> wantedBlocks;
    const CBlockIndex* blockPreviousTimestamp = pindexLast;
    while (blocksFound < N) {
        // Reached forkpoint before finding N blocks of correct powtype? Return min
        if (!blockPreviousTimestamp) {
            if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (reached end of chain)\n", POW_TYPE_NAMES[powType]);
            // Cascoin: Added detailed SHA256 logging
            if (powType == POW_TYPE_SHA256) {
                LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Reached end of chain (null blockPreviousTimestamp). Returning powLimit 0x%08x\n", powLimit.GetCompact());
            }
            return powLimit.GetCompact();
        }

        // Cascoin: MinotaurX+Hive1.2: Check for old version blocks only relevant if MinotaurX is NOT enabled.
        // If MinotaurX is enabled, GetEffectivePoWTypeForHashing handles versioning.
        // The original check here was: if (blockPreviousTimestamp->GetBlockHeader().nVersion >= 0x20000000)
        // This would incorrectly return powLimit if MinotaurX was active and a block had a high nVersion
        // (e.g. a BIP9-style SHA256 block, which GetEffectivePoWTypeForHashing would correctly identify).
        // We should rely on IsMinotaurXEnabled and GetEffectivePoWTypeForHashing for type determination
        // when MinotaurX is active.
        // For pre-MinotaurX, the old version check might have been intended to stop at a "fork point"
        // to a new versioning scheme, but LWMA is only for post-MinotaurX.
        // Thus, the nVersion >= 0x20000000 check as a general stop condition here is problematic.
        // The primary concern is ensuring we are using the correct PoW type logic.
        // GetEffectivePoWTypeForHashing already considers powForkTime.

        // Wrong block type? Skip
        // Use GetEffectivePoWTypeForHashing to determine the block's type for LWMA purposes.
        POW_TYPE effectivePrevBlockType = blockPreviousTimestamp->GetBlockHeader().GetEffectivePoWTypeForHashing(params);
        if (blockPreviousTimestamp->GetBlockHeader().IsHiveMined(params) || effectivePrevBlockType != powType) {
            if (!blockPreviousTimestamp->pprev) {
                if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (reached end of chain while skipping blocks)\n", POW_TYPE_NAMES[powType]);
                // Cascoin: Added detailed SHA256 logging
                if (powType == POW_TYPE_SHA256) {
                    LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Reached end of chain while skipping unwanted blocks (effective type %d vs wanted %d). Returning powLimit 0x%08x\n", static_cast<int>(effectivePrevBlockType), static_cast<int>(powType), powLimit.GetCompact());
                }
                return powLimit.GetCompact();
            }
            blockPreviousTimestamp = blockPreviousTimestamp->pprev;
            continue;
        }
    
        wantedBlocks.push_back(blockPreviousTimestamp);

        blocksFound++;
        if (blocksFound == N)   // Don't step to next one if we're at the one we want
            break;

        if (!blockPreviousTimestamp->pprev) {
            if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (reached end of chain while collecting blocks)\n", POW_TYPE_NAMES[powType]);
            // Cascoin: Added detailed SHA256 logging
             if (powType == POW_TYPE_SHA256) {
                LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Reached end of chain (no pprev) while collecting blocks (found %lld). Returning powLimit 0x%08x\n", (long long)blocksFound, powLimit.GetCompact());
            }
            return powLimit.GetCompact();
        }
        blockPreviousTimestamp = blockPreviousTimestamp->pprev;
    }

    // Cascoin: Explicitly handle if no blocks of the specified powType were found
    if (blocksFound == 0) {
        if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (no blocks of type %s found in LWMA window)\n", POW_TYPE_NAMES[powType], POW_TYPE_NAMES[powType]);
        // Cascoin: Added detailed SHA256 logging
        if (powType == POW_TYPE_SHA256) {
            LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: No blocks of type found (blocksFound == 0). Returning powLimit 0x%08x\n", powLimit.GetCompact());
        }
        return powLimit.GetCompact();
    }

    previousTimestamp = blockPreviousTimestamp->GetBlockTime();
    //if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: previousTime: First in period is %s at height %i\\n", blockPreviousTimestamp->GetBlockHeader().GetHash().ToString().c_str(), blockPreviousTimestamp->nHeight);

    // Iterate forward from the oldest block (ie, reverse-iterate through the wantedBlocks vector)
    for (auto it = wantedBlocks.rbegin(); it != wantedBlocks.rend(); ++it) {
        const CBlockIndex* block = *it;

        // Prevent solvetimes from being negative in a safe way. It must be done like this. 
        // Do not attempt anything like  if (solvetime < 1) {solvetime=1;}
        // The +1 ensures new coins do not calculate nextTarget = 0.
        thisTimestamp = (block->GetBlockTime() > previousTimestamp) ? block->GetBlockTime() : previousTimestamp + 1;

        // 6*T limit prevents large drops in diff from long solvetimes which would cause oscillations.
        int64_t solvetime = std::min(6 * T, thisTimestamp - previousTimestamp);

        // The following is part of "preventing negative solvetimes". 
        previousTimestamp = thisTimestamp;
       
        // Give linearly higher weight to more recent solvetimes.
        j++;
        sumWeightedSolvetimes += solvetime * j; 

        arith_uint256 target;
        target.SetCompact(block->nBits);
        avgTarget += target / N / k; // Dividing by k here prevents an overflow below.
    } 

    nextTarget = avgTarget * sumWeightedSolvetimes;

    // Also check if nextTarget is 0 (infinitely difficult)
    if (nextTarget > powLimit || nextTarget == 0) { 
        if (verbose) LogPrintf("* GetNextWorkRequiredLWMA: Allowing %s pow limit (target %s calculated higher than limit, or zero)\n", POW_TYPE_NAMES[powType], nextTarget.ToString().c_str());
        // Cascoin: Added detailed SHA256 logging
        if (powType == POW_TYPE_SHA256) {
            LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Calculated nextTarget (%s) is > powLimit (%s) or zero. Returning powLimit 0x%08x\n", nextTarget.ToString().c_str(), powLimit.ToString().c_str(), powLimit.GetCompact());
        }
        return powLimit.GetCompact();
    }

    // Cascoin: Added detailed SHA256 logging
    if (powType == POW_TYPE_SHA256) {
        LogPrint(BCLog::MINOTAURX, "GetNextWorkRequiredLWMA: SHA256 - Path: Successful calculation. Returning nextTarget 0x%08x (Actual: %s)\n", nextTarget.GetCompact(), nextTarget.ToString().c_str());
    }
    return nextTarget.GetCompact();
}

// Cascoin: DarkGravity V3 (https://github.com/dashpay/dash/blob/master/src/pow.cpp#L82)
// By Evan Duffield <evan@dash.org>
// Used for sha256 from CAS fork point till MinotaurX activation
unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitSHA);
    int64_t nPastBlocks = 24;

    // Cascoin: Allow minimum difficulty blocks if we haven't seen a block for ostensibly 10 blocks worth of time
    if (params.fPowAllowMinDifficultyBlocks && pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 10)
        return bnPowLimit.GetCompact();

    // Cascoin: Hive 1.1: Skip over Hivemined blocks at tip
    if (IsHive11Enabled(pindexLast, params)) {
        while (pindexLast->GetBlockHeader().IsHiveMined(params)) {
            //LogPrintf("DarkGravityWave: Skipping hivemined block at %i\n", pindex->nHeight);
            assert(pindexLast->pprev); // should never fail
            pindexLast = pindexLast->pprev;
        }
    }

    // Cascoin: Make sure we have at least (nPastBlocks + 1) blocks since the fork, otherwise just return powLimitSHA
    if (!pindexLast || pindexLast->nHeight - params.lastScryptBlock < nPastBlocks)
        return bnPowLimit.GetCompact();

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        // Cascoin: Hive: Skip over Hivemined blocks; we only want to consider PoW blocks
        while (pindex->GetBlockHeader().IsHiveMined(params)) {
            //LogPrintf("DarkGravityWave: Skipping hivemined block at %i\n", pindex->nHeight);
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }

        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

// Call correct diff adjust for Scrypt and sha256 blocks prior to MinotaurX
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    // Cascoin: If past fork time, use Dark Gravity Wave
    if (pindexLast->nHeight >= params.lastScryptBlock)
        return DarkGravityWave(pindexLast, pblock, params);
    else
        return GetNextWorkRequiredLTC(pindexLast, pblock, params);
}

// LTC's diff adjust
unsigned int GetNextWorkRequiredLTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    // Cascoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = params.DifficultyAdjustmentInterval()-1;
    if ((pindexLast->nHeight+1) != params.DifficultyAdjustmentInterval())
        blockstogoback = params.DifficultyAdjustmentInterval();

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;

    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

// Used by LTC's diff adjust
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    // Cascoin: intermediate uint256 can overflow by 1 bit
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    bool fShift = bnNew.bits() > bnPowLimit.bits() - 1;
    if (fShift)
        bnNew >>= 1;
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;
    if (fShift)
        bnNew <<= 1;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    // Throttled summary logging (only when -debug=minotaurx)
    const bool doLog = LogAcceptCategory(BCLog::MINOTAURX);
    static uint64_t checks_total = 0;
    static uint64_t checks_valid = 0;
    static uint64_t checks_failed_range = 0;
    static uint64_t checks_failed_hash = 0;
    static int64_t next_summary_ms = 0;
    static int64_t period_start_ms = 0;
    auto emit_summary_if_due = [&]() {
        if (!doLog) return;
        const int64_t now = GetTimeMillis();
        if (next_summary_ms == 0) {
            next_summary_ms = now + 5000; // 5s window
            period_start_ms = now;
        }
        if (now >= next_summary_ms) {
            const int64_t elapsed = (period_start_ms > 0) ? (now - period_start_ms) : 0;
            LogPrint(BCLog::MINOTAURX, "CheckProofOfWork: %u checks in last %d ms (valid=%u, range_fail=%u, hash_fail=%u)\n",
                (unsigned)checks_total, (int)elapsed, (unsigned)checks_valid,
                (unsigned)checks_failed_range, (unsigned)checks_failed_hash);
            checks_total = checks_valid = checks_failed_range = checks_failed_hash = 0;
            period_start_ms = now;
            next_summary_ms = now + 5000;
        }
    };

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Cascoin: MinotaurX+Hive1.2: Use highest pow limit for limit check
    arith_uint256 overallPowLimit = 0;
    for (int i = 0; i < NUM_BLOCK_TYPES; i++) {
        arith_uint256 currentAlgoLimit = UintToArith256(params.powTypeLimits[i]);
        if (currentAlgoLimit > overallPowLimit) {
            overallPowLimit = currentAlgoLimit;
        }
        // LogPrintf("CheckProofOfWork: Algo %d (%s) Limit: %s (Compact: 0x%08x)\n", i, POW_TYPE_NAMES[i], currentAlgoLimit.ToString(), currentAlgoLimit.GetCompact()); // Optional: too verbose usually
    }
    // (details suppressed per-call)

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > overallPowLimit) {
        if (doLog) { checks_total++; checks_failed_range++; emit_summary_if_due(); }
        return false;
    }
    // range ok

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget) {
        if (doLog) { checks_total++; checks_failed_hash++; emit_summary_if_due(); }
        return false;
    }
    if (doLog) { checks_total++; checks_valid++; emit_summary_if_due(); }

    return true;
}

// Cascoin: Hive 1.1: SMA Hive Difficulty Adjust
unsigned int GetNextHive11WorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitHive);

    arith_uint256 mouseHashTarget = 0;
    int hiveBlockCount = 0;
    int totalBlockCount = 0;

    // Step back till we have found 24 hive blocks, or we ran out...
    while (hiveBlockCount < params.hiveDifficultyWindow && pindexLast->pprev && pindexLast->nHeight >= params.minHiveCheckBlock) {
        if (pindexLast->GetBlockHeader().IsHiveMined(params)) {
            mouseHashTarget += arith_uint256().SetCompact(pindexLast->nBits);
            hiveBlockCount++;
        }
        totalBlockCount++;
        pindexLast = pindexLast->pprev;
    }

    if (hiveBlockCount == 0) {          // Should only happen when chain is starting
        LogPrint(BCLog::HIVE, "GetNextHive11WorkRequired: No previous hive blocks found.\n");
        return bnPowLimit.GetCompact();
    }

    mouseHashTarget /= hiveBlockCount;    // Average the mouse hash targets in window

    // Retarget based on totalBlockCount
    int targetTotalBlockCount = hiveBlockCount * params.hiveBlockSpacingTarget;
    mouseHashTarget *= totalBlockCount;
    mouseHashTarget /= targetTotalBlockCount;

    if (mouseHashTarget > bnPowLimit)
        mouseHashTarget = bnPowLimit;

    return mouseHashTarget.GetCompact();
}

// Cascoin: MinotaurX+Hive1.2: Reset Hive difficulty after MinotaurX enable
unsigned int GetNextHive12WorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitHive);

    arith_uint256 mouseHashTarget = 0;
    int hiveBlockCount = 0;
    int totalBlockCount = 0;

    // Step back till we have found 24 hive blocks, or we ran out...
    while (hiveBlockCount < params.hiveDifficultyWindow && pindexLast->pprev && IsMinotaurXEnabled(pindexLast, params)) {        
        if (pindexLast->GetBlockHeader().IsHiveMined(params)) {
            mouseHashTarget += arith_uint256().SetCompact(pindexLast->nBits);
            hiveBlockCount++;
        }
        totalBlockCount++;
        pindexLast = pindexLast->pprev;
    }

    // Handle bootstrapping of The Labyrinth system with insufficient history
    if (hiveBlockCount < 24) {
        // Always return the easiest difficulty during blockchain startup
        // This helps bootstrap The Labyrinth mining system
        LogPrint(BCLog::HIVE, "GetNextHive12WorkRequired: Insufficient hive blocks - using easiest target for bootstrapping.\n");
        return bnPowLimit.GetCompact();
    }

    mouseHashTarget /= hiveBlockCount;    // Average the mouse hash targets in window

    // Retarget based on totalBlockCount
    int targetTotalBlockCount = hiveBlockCount * params.hiveBlockSpacingTarget;
    mouseHashTarget *= totalBlockCount;
    mouseHashTarget /= targetTotalBlockCount;

    if (mouseHashTarget > bnPowLimit)
        mouseHashTarget = bnPowLimit;

    return mouseHashTarget.GetCompact();
}

// Cascoin: Hive: Get the current Mouse Hash Target (Hive 1.0)
unsigned int GetNextHiveWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    // Cascoin: MinotaurX+Hive1.2
    if (IsMinotaurXEnabled(pindexLast, params))
        return GetNextHive12WorkRequired(pindexLast, params);
    // Cascoin: Hive 1.1: Use SMA diff adjust
    if (IsHive11Enabled(pindexLast, params))
        return GetNextHive11WorkRequired(pindexLast, params);

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitHive);
    const arith_uint256 bnImpossible = 0;
    arith_uint256 mouseHashTarget;

    //LogPrintf("GetNextHiveWorkRequired: Height     = %i\n", pindexLast->nHeight);

    int numPowBlocks = 0;
    CBlockHeader block;
    while (true) {
        if (!pindexLast->pprev || pindexLast->nHeight < params.minHiveCheckBlock) {   // Ran out of blocks without finding a Hive block? Return min target
            LogPrint(BCLog::HIVE, "GetNextHiveWorkRequired: No hivemined blocks found in history\n");
            //LogPrintf("GetNextHiveWorkRequired: This target= %s\n", bnPowLimit.ToString());
            return bnPowLimit.GetCompact();
        }

        block = pindexLast->GetBlockHeader();
        if (block.IsHiveMined(params)) {  // Found the last Hive block; pick up its mouse hash target
            mouseHashTarget.SetCompact(block.nBits);
            break;
        }

        pindexLast = pindexLast->pprev;
        numPowBlocks++;
    }

    //LogPrintf("GetNextHiveWorkRequired: powBlocks  = %i\n", numPowBlocks);
    if (numPowBlocks == 0)
        return bnImpossible.GetCompact();

    //LogPrintf("GetNextHiveWorkRequired: Last target= %s\n", mouseHashTarget.ToString());

	// Apply EMA
	int interval = params.hiveTargetAdjustAggression / params.hiveBlockSpacingTarget;
	mouseHashTarget *= (interval - 1) * params.hiveBlockSpacingTarget + numPowBlocks + numPowBlocks;
	mouseHashTarget /= (interval + 1) * params.hiveBlockSpacingTarget;

	// Clamp to min difficulty
	if (mouseHashTarget > bnPowLimit)
		mouseHashTarget = bnPowLimit;

    //LogPrintf("GetNextHiveWorkRequired: This target= %s\n", mouseHashTarget.ToString());

    return mouseHashTarget.GetCompact();
}

// Cascoin: Hive: Get count of all live and gestating BCTs on the network
bool GetNetworkHiveInfo(int& immatureMice, int& immatureBCTs, int& matureMice, int& matureBCTs, CAmount& potentialLifespanRewards, const Consensus::Params& consensusParams, bool recalcGraph) {
    int totalMouseLifespan = consensusParams.mouseLifespanBlocks + consensusParams.mouseGestationBlocks;
    immatureMice = immatureBCTs = matureMice = matureBCTs = 0;
    
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);
    int tipHeight = pindexPrev->nHeight;

    // Cascoin: MinotaurX+Hive1.2: Get correct hive block reward
    auto blockReward = GetBlockSubsidy(pindexPrev->nHeight, consensusParams);
    if (IsMinotaurXEnabled(pindexPrev, consensusParams))
        blockReward += blockReward >> 1;

    // Cascoin: Hive 1.1: Use correct typical spacing
    if (IsHive11Enabled(pindexPrev, consensusParams))
        potentialLifespanRewards = (consensusParams.mouseLifespanBlocks * blockReward) / consensusParams.hiveBlockSpacingTargetTypical_1_1;
    else
        potentialLifespanRewards = (consensusParams.mouseLifespanBlocks * blockReward) / consensusParams.hiveBlockSpacingTargetTypical;

    if (recalcGraph) {
        for (int i = 0; i < totalMouseLifespan; i++) {
            mousePopGraph[i].immaturePop = 0;
            mousePopGraph[i].maturePop = 0;
        }
    }

    if (IsInitialBlockDownload())   // Refuse if we're downloading
        return false;

    // Count mice in next blockCount blocks
    CBlock block;
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.mouseCreationAddress));
    CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.hiveCommunityAddress));

    for (int i = 0; i < totalMouseLifespan; i++) {
        if (fHavePruned && !(pindexPrev->nStatus & BLOCK_HAVE_DATA) && pindexPrev->nTx > 0) {
            LogPrint(BCLog::HIVE, "! GetNetworkHiveInfo: Warn: Block not available (pruned data); can't calculate network mouse count.");
            return false;
        }

        if (!pindexPrev->GetBlockHeader().IsHiveMined(consensusParams)) {                          // Don't check Hivemined blocks (no BCTs will be found in them)
            if (!ReadBlockFromDisk(block, pindexPrev, consensusParams)) {
                LogPrint(BCLog::HIVE, "! GetNetworkHiveInfo: Warn: Block not available (not found on disk); can't calculate network mouse count.");
                return false;
            }
            int blockHeight = pindexPrev->nHeight;
            CAmount mouseCost = GetMouseCost(blockHeight, consensusParams);
            if (block.vtx.size() > 0) {
                for(const auto& tx : block.vtx) {
                    CAmount mouseFeePaid;
                    if (tx->IsBCT(consensusParams, scriptPubKeyBCF, &mouseFeePaid)) {                 // If it's a BCT, total its mice
                        if (tx->vout.size() > 1 && tx->vout[1].scriptPubKey == scriptPubKeyCF) {    // If it has a community fund contrib...
                            CAmount donationAmount = tx->vout[1].nValue;
                            CAmount expectedDonationAmount = (mouseFeePaid + donationAmount) / consensusParams.communityContribFactor;  // ...check for valid donation amount
                            // Cascoin: MinotaurX+Hive1.2
                            if (IsMinotaurXEnabled(pindexPrev, consensusParams))
                                expectedDonationAmount += expectedDonationAmount >> 1;
                            if (donationAmount != expectedDonationAmount)
                                continue;
                            mouseFeePaid += donationAmount;                                           // Add donation amount back to total paid
                        }
                        int mouseCount = mouseFeePaid / mouseCost;
                        if (i < consensusParams.mouseGestationBlocks) {
                            immatureMice += mouseCount;
                            immatureBCTs++;
                        } else {
                            matureMice += mouseCount; 
                            matureBCTs++;
                        }

                        // Add these mice to pop graph
                        if (recalcGraph) {
                            /*
                            int beeStart = blockHeight + consensusParams.mouseGestationBlocks;
                            int beeStop = beeStart + consensusParams.mouseLifespanBlocks;
                            beeStart -= tipHeight;
                            beeStop -= tipHeight;
                            for (int j = beeStart; j < beeStop; j++) {
                                if (j > 0 && j < totalMouseLifespan) {
                                    if (i < consensusParams.mouseGestationBlocks) // THIS IS WRONG
                                        mousePopGraph[j].immaturePop += mouseCount;
                                    else
                                        mousePopGraph[j].maturePop += mouseCount;
                                }
                            }*/
                            int mouseBornBlock = blockHeight;
                            int mouseMaturesBlock = mouseBornBlock + consensusParams.mouseGestationBlocks;
                            int mouseDeathBlock = mouseMaturesBlock + consensusParams.mouseLifespanBlocks;
                            for (int j = mouseBornBlock; j < mouseDeathBlock; j++) {
                                int graphPos = j - tipHeight;
                                if (graphPos > 0 && graphPos < totalMouseLifespan) {
                                    if (j < mouseMaturesBlock)
                                        mousePopGraph[graphPos].immaturePop += mouseCount;
                                    else
                                        mousePopGraph[graphPos].maturePop += mouseCount;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!pindexPrev->pprev)     // Check we didn't run out of blocks
            return true;

        pindexPrev = pindexPrev->pprev;
    }

    return true;
}

// Cascoin: Hive: Check The Labyrinth proof for given block
bool CheckHiveProof(const CBlock* pblock, const Consensus::Params& consensusParams) {
    bool verbose = LogAcceptCategory(BCLog::HIVE);

    if (verbose)
        LogPrintf("********************* Hive: CheckHiveProof *********************\n");

    // Get height (a CBlockIndex isn't always available when this func is called, eg in reads from disk)
    int blockHeight;
    CBlockIndex* pindexPrev;
    {
        LOCK(cs_main);
        pindexPrev = mapBlockIndex[pblock->hashPrevBlock];
        blockHeight = pindexPrev->nHeight + 1;
    }
    if (!pindexPrev) {
        LogPrint(BCLog::HIVE, "CheckHiveProof: Couldn't get previous block's CBlockIndex!\n");
        return false;
    }
    if (verbose)
        LogPrintf("CheckHiveProof: nHeight             = %i\n", blockHeight);

    // Check hive is enabled on network
    if (!IsHiveEnabled(pindexPrev, consensusParams)) {
        LogPrint(BCLog::HIVE, "CheckHiveProof: Can't accept a Hive block; Hive is not yet enabled on the network.\n");
        return false;
    }

    // Cascoin: Hive 1.1: Check that there aren't too many consecutive Hive blocks
    if (IsHive11Enabled(pindexPrev, consensusParams)) {
        int hiveBlocksAtTip = 0;
        CBlockIndex* pindexTemp = pindexPrev;
        while (pindexTemp->GetBlockHeader().IsHiveMined(consensusParams)) {
            assert(pindexTemp->pprev);
            pindexTemp = pindexTemp->pprev;
            hiveBlocksAtTip++;
        }
        if (hiveBlocksAtTip >= consensusParams.maxConsecutiveHiveBlocks) {
            LogPrint(BCLog::HIVE, "CheckHiveProof: Too many Hive blocks without a POW block.\n");
            return false;
        }
    } else {
        if (pindexPrev->GetBlockHeader().IsHiveMined(consensusParams)) {
            LogPrint(BCLog::HIVE, "CheckHiveProof: Hive block must follow a POW block.\n");
            return false;
        }
    }

    // Block mustn't include any BCTs
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.mouseCreationAddress));
    if (pblock->vtx.size() > 1)
        for (unsigned int i=1; i < pblock->vtx.size(); i++)
            if (pblock->vtx[i]->IsBCT(consensusParams, scriptPubKeyBCF)) {
                LogPrintf("CheckHiveProof: Hivemined block contains BCTs!\n");
                return false;                
            }
    
    // Coinbase tx must be valid
    CTransactionRef txCoinbase = pblock->vtx[0];
    //LogPrintf("CheckHiveProof: Got coinbase tx: %s\n", txCoinbase->ToString());
    if (!txCoinbase->IsCoinBase()) {
        LogPrintf("CheckHiveProof: Coinbase tx isn't valid!\n");
        return false;
    }

    // Must have exactly 2 or 3 outputs
    if (txCoinbase->vout.size() < 2 || txCoinbase->vout.size() > 3) {
        LogPrintf("CheckHiveProof: Didn't expect %i vouts!\n", txCoinbase->vout.size());
        return false;
    }

    // vout[0] must be long enough to contain all encodings
    if (txCoinbase->vout[0].scriptPubKey.size() < 144) {
        LogPrintf("CheckHiveProof: vout[0].scriptPubKey isn't long enough to contain hive proof encodings\n");
        return false;
    }

    // vout[1] must start OP_RETURN OP_MOUSE (bytes 0-1)
    if (txCoinbase->vout[0].scriptPubKey[0] != OP_RETURN || txCoinbase->vout[0].scriptPubKey[1] != OP_MOUSE) {
        LogPrintf("CheckHiveProof: vout[0].scriptPubKey doesn't start OP_RETURN OP_MOUSE\n");
        return false;
    }

    // Grab the mouse nonce (bytes 3-6; byte 2 has value 04 as a size marker for this field)
    uint32_t mouseNonce = ReadLE32(&txCoinbase->vout[0].scriptPubKey[3]);
    if (verbose)
        LogPrintf("CheckHiveProof: mouseNonce            = %i\n", mouseNonce);

    // Grab the bct height (bytes 8-11; byte 7 has value 04 as a size marker for this field)
    uint32_t bctClaimedHeight = ReadLE32(&txCoinbase->vout[0].scriptPubKey[8]);
    if (verbose)
        LogPrintf("CheckHiveProof: bctHeight           = %i\n", bctClaimedHeight);

    // Get community contrib flag (byte 12)
    bool communityContrib = txCoinbase->vout[0].scriptPubKey[12] == OP_TRUE;
    if (verbose)
        LogPrintf("CheckHiveProof: communityContrib    = %s\n", communityContrib ? "true" : "false");

    // Grab the txid (bytes 14-78; byte 13 has val 64 as size marker)
    std::vector<unsigned char> txid(&txCoinbase->vout[0].scriptPubKey[14], &txCoinbase->vout[0].scriptPubKey[14 + 64]);
    std::string txidStr = std::string(txid.begin(), txid.end());
    if (verbose)
        LogPrintf("CheckHiveProof: bctTxId             = %s\n", txidStr);

    // Check mouse hash against target
    std::string deterministicRandString = GetDeterministicRandString(pindexPrev);
    if (verbose)
        LogPrintf("CheckHiveProof: detRandString       = %s\n", deterministicRandString);
    arith_uint256 mouseHashTarget;
    mouseHashTarget.SetCompact(GetNextHiveWorkRequired(pindexPrev, consensusParams));
    if (verbose)
        LogPrintf("CheckHiveProof: mouseHashTarget       = %s\n", mouseHashTarget.ToString());
    
    // Cascoin: MinotaurX+Hive1.2: Use the correct inner Hive hash
    if (!IsMinotaurXEnabled(pindexPrev, consensusParams)) {
        std::string hashHex = (CHashWriter(SER_GETHASH, 0) << deterministicRandString << txidStr << mouseNonce).GetHash().GetHex();
        arith_uint256 mouseHash = arith_uint256(hashHex);
        if (verbose)
            LogPrintf("CheckHiveProof: mouseHash             = %s\n", mouseHash.GetHex());
        if (mouseHash >= mouseHashTarget) {
            LogPrintf("CheckHiveProof: Mouse does not meet hash target!\n");
            return false;
        }
    } else {
        arith_uint256 mouseHash(CBlockHeader::MinotaurHashArbitrary(std::string(deterministicRandString + txidStr + std::to_string(mouseNonce)).c_str()).ToString());
        if (verbose)
            LogPrintf("CheckHive12Proof: mouseHash           = %s\n", mouseHash.GetHex());
        if (mouseHash >= mouseHashTarget) {
            LogPrintf("CheckHive12Proof: Mouse does not meet hash target!\n");
            return false;
        }
    }
    
    // Grab the message sig (bytes 79-end; byte 78 is size)
    std::vector<unsigned char> messageSig(&txCoinbase->vout[0].scriptPubKey[79], &txCoinbase->vout[0].scriptPubKey[79 + 65]);
    if (verbose)
        LogPrintf("CheckHiveProof: messageSig          = %s\n", HexStr(&messageSig[0], &messageSig[messageSig.size()]));
    
    // Grab the cheese address from the cheese vout
    CTxDestination cheeseDestination;
    if (!ExtractDestination(txCoinbase->vout[1].scriptPubKey, cheeseDestination)) {
        LogPrintf("CheckHiveProof: Couldn't extract cheese address\n");
        return false;
    }
    if (!IsValidDestination(cheeseDestination)) {
        LogPrintf("CheckHiveProof: Cheese address is invalid\n");
        return false;
    }
    if (verbose)
        LogPrintf("CheckHiveProof: cheeseAddress        = %s\n", EncodeDestination(cheeseDestination));

    // Verify the message sig
    const CKeyID *keyID = boost::get<CKeyID>(&cheeseDestination);
    if (!keyID) {
        LogPrintf("CheckHiveProof: Can't get pubkey for cheese address\n");
        return false;
    }
    CHashWriter ss(SER_GETHASH, 0);
    ss << deterministicRandString;
    uint256 mhash = ss.GetHash();
    CPubKey pubkey;
    if (!pubkey.RecoverCompact(mhash, messageSig)) {
        LogPrintf("CheckHiveProof: Couldn't recover pubkey from hash\n");
        return false;
    }
    if (pubkey.GetID() != *keyID) {
        LogPrint(BCLog::HIVE, "CheckHiveProof: Signature mismatch! GetID() = %s, *keyID = %s\n", pubkey.GetID().ToString(), (*keyID).ToString());
        return false;
    }

    // Grab the BCT utxo
    bool deepDrill = false;
    uint32_t bctFoundHeight;
    CAmount bctValue;
    CScript bctScriptPubKey;
    bool bctWasMinotaurXEnabled;    // Cascoin: MinotaurX+Hive1.2: Track whether Hive 1.2 was enabled at BCT creation time
    {
        LOCK(cs_main);

        COutPoint outMouseCreation(uint256S(txidStr), 0);
        COutPoint outCommFund(uint256S(txidStr), 1);
        Coin coin;
        CTransactionRef bct = nullptr;
        CBlockIndex foundAt;

        if (pcoinsTip && pcoinsTip->GetCoin(outMouseCreation, coin)) {        // First try the UTXO set (this pathway will hit on incoming blocks)
            if (verbose)
                LogPrintf("CheckHiveProof: Using UTXO set for outMouseCreation\n");
            bctValue = coin.out.nValue;
            bctScriptPubKey = coin.out.scriptPubKey;
            bctFoundHeight = coin.nHeight;
            bctWasMinotaurXEnabled = IsMinotaurXEnabled(chainActive[bctFoundHeight], consensusParams);  // Cascoin: MinotaurX+Hive1.2: Track whether Hive 1.2 was enabled at BCT creation time
        } else {                                                            // UTXO set isn't available when eg reindexing, so drill into block db (not too bad, since Alice put her BCT height in the coinbase tx)
            if (verbose)
                LogPrintf("! CheckHiveProof: Warn: Using deep drill for outMouseCreation\n");
            if (!GetTxByHashAndHeight(uint256S(txidStr), bctClaimedHeight, bct, foundAt, pindexPrev, consensusParams)) {
                LogPrintf("CheckHiveProof: Couldn't locate indicated BCT\n");
                return false;
            }
            deepDrill = true;
            bctFoundHeight = foundAt.nHeight;
            bctValue = bct->vout[0].nValue;
            bctScriptPubKey = bct->vout[0].scriptPubKey;
            bctWasMinotaurXEnabled = IsMinotaurXEnabled(&foundAt, consensusParams); // Cascoin: MinotaurX+Hive1.2: Track whether Hive 1.2 was enabled at BCT creation time
        }

        if (communityContrib) {
            CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.hiveCommunityAddress));
            CAmount donationAmount;

            if(bct == nullptr) {                                                                // If we dont have a ref to the BCT
                if (pcoinsTip && pcoinsTip->GetCoin(outCommFund, coin)) {                       // First try UTXO set
                    if (verbose)
                        LogPrintf("CheckHiveProof: Using UTXO set for outCommFund\n");
                    if (coin.out.scriptPubKey != scriptPubKeyCF) {                              // If we find it, validate the scriptPubKey and store amount
                        LogPrintf("CheckHiveProof: Community contrib was indicated but not found\n");
                        return false;
                    }
                    donationAmount = coin.out.nValue;
                } else {                                                                        // Fallback if we couldn't use UTXO set
                    if (verbose)
                        LogPrintf("! CheckHiveProof: Warn: Using deep drill for outCommFund\n");
                    if (!GetTxByHashAndHeight(uint256S(txidStr), bctClaimedHeight, bct, foundAt, pindexPrev, consensusParams)) {
                        LogPrintf("CheckHiveProof: Couldn't locate indicated BCT\n");           // Still couldn't find it
                        return false;
                    }
                    deepDrill = true;
                }
            }
            if(bct != nullptr) {                                                                // We have the BCT either way now (either from first or second drill). If got from UTXO set bct == nullptr still.
                if (bct->vout.size() < 2 || bct->vout[1].scriptPubKey != scriptPubKeyCF) {      // So Validate the scriptPubKey and store amount
                    LogPrintf("CheckHiveProof: Community contrib was indicated but not found\n");
                    return false;
                }
                donationAmount = bct->vout[1].nValue;
            }

            // Check for valid donation amount
            CAmount expectedDonationAmount = (bctValue + donationAmount) / consensusParams.communityContribFactor;

            // Cascoin: MinotaurX+Hive1.2
            if (bctWasMinotaurXEnabled)
                expectedDonationAmount += expectedDonationAmount >> 1;

            if (donationAmount != expectedDonationAmount) {
                LogPrintf("CheckHiveProof: BCT pays community fund incorrect amount %i (expected %i)\n", donationAmount, expectedDonationAmount);
                return false;
            }

            // Update amount paid
            bctValue += donationAmount;
        }
    }

    if (bctFoundHeight != bctClaimedHeight) {
        LogPrintf("CheckHiveProof: Claimed BCT height of %i conflicts with found height of %i\n", bctClaimedHeight, bctFoundHeight);
        return false;
    }

    // Check mouse maturity
    int bctDepth = blockHeight - bctFoundHeight;
    if (bctDepth < consensusParams.mouseGestationBlocks) {
        LogPrintf("CheckHiveProof: Indicated BCT is immature.\n");
        return false;
    }
    if (bctDepth > consensusParams.mouseGestationBlocks + consensusParams.mouseLifespanBlocks) {
        LogPrintf("CheckHiveProof: Indicated BCT is too old.\n");
        return false;
    }

    // Check for valid mouse creation script and get cheese scriptPubKey from BCT
    CScript scriptPubKeyCheese;
    if (!CScript::IsBCTScript(bctScriptPubKey, scriptPubKeyBCF, &scriptPubKeyCheese)) {
        LogPrintf("CheckHiveProof: Indicated utxo is not a valid BCT script\n");
        return false;
    }

    CTxDestination cheeseDestinationBCT;
    if (!ExtractDestination(scriptPubKeyCheese, cheeseDestinationBCT)) {
        LogPrintf("CheckHiveProof: Couldn't extract cheese address from BCT UTXO\n");
        return false;
    }

    // Check BCT's cheese address actually matches the claimed cheese address
    if (cheeseDestination != cheeseDestinationBCT) {
        LogPrintf("CheckHiveProof: BCT's cheese address does not match claimed cheese address!\n");
        return false;
    }

    // Find mouse count
    CAmount mouseCost = GetMouseCost(bctFoundHeight, consensusParams);
    if (bctValue < consensusParams.minMouseCost) {
        LogPrintf("CheckHiveProof: BCT fee is less than the minimum possible mouse cost\n");
        return false;
    }
    if (bctValue < mouseCost) {
        LogPrintf("CheckHiveProof: BCT fee is less than the cost for a single mouse\n");
        return false;
    }
    unsigned int mouseCount = bctValue / mouseCost;
    if (verbose) {
        LogPrintf("CheckHiveProof: bctValue            = %i\n", bctValue);
        LogPrintf("CheckHiveProof: mouseCost             = %i\n", mouseCost);
        LogPrintf("CheckHiveProof: mouseCount            = %i\n", mouseCount);
    }
    
    // Check enough mice were bought to include claimed mouseNonce
    if (mouseNonce >= mouseCount) {
        LogPrintf("CheckHiveProof: BCT did not create enough mice for claimed nonce!\n");
        return false;
    }

    if (verbose)
        LogPrintf("CheckHiveProof: Pass at %i%s\n", blockHeight, deepDrill ? " (used deepdrill)" : "");

    return true;
}
