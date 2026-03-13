// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>        // Cascoin: Labyrinth
#include <util.h>               // Cascoin: Labyrinth
#include <rpc/blockchain.h>     // Cascoin: Labyrinth 1.1
#include <validation.h>         // Cascoin: Labyrinth 1.1

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    if (pindex == nullptr) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime) const
{
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), nTime,
        [](CBlockIndex* pBlock, const int64_t& time) -> bool { return pBlock->GetBlockTimeMax() < time; });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

// Cascoin: Labyrinth: Grant labyrinth-mined blocks bonus work value - they get the work value of
// their own block plus that of the PoW block behind them
// In chain.cpp

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // --- 1) Generator‐Guard: kein work für Genesis (pprev == nullptr) ---
    if (block.pprev == nullptr) {
        // Genesis-Block hat per Definition keine Chainwork
        return arith_uint256(0);
    }

    // --- 2) Basis-Target berechnen ---
    arith_uint256 bnTarget;
    bool fNegative = false, fOverflow = false;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0) {
        return arith_uint256(0);
    }
    // Proof = ~target/(target+1) + 1
    arith_uint256 bnTargetScaled = (~bnTarget / (bnTarget + 1)) + 1;

    // --- 3) Labyrinth-Bonus, nur wenn Block tatsächlich als Labyrinth-Block gemined ist ---
    CBlockHeader header = block.GetBlockHeader();
    if (header.IsHiveMined(consensusParams)) {
        // Suche zurück nach dem letzten POW-Block
        const CBlockIndex* p = block.pprev;
        while (p && p->GetBlockHeader().IsHiveMined(consensusParams)) {
            p = p->pprev;
        }
        if (p) {
            // p ist jetzt der letzte POW-Block vor einer Labyrinth-Kette
            arith_uint256 bnPrev;
            bool neg2=false, ovf2=false;
            bnPrev.SetCompact(p->nBits, &neg2, &ovf2);
            if (!(neg2||ovf2) && bnPrev != 0) {
                bnTargetScaled += (~bnPrev / (bnPrev + 1)) + 1;
            }
        }
        // (Hier kannst Du dein Labyrinth 1.1-Bonus-Scaling integrieren,
        //  achte aber darauf, p niemals derefenzierst, wenn es nullptr ist.)
    }
    // --- 4) Optional: POW-Bonus für Labyrinth 1.1 nur, wenn aktiv ---
    else if (IsLabyrinth11Enabled(&block, consensusParams)) {
        // Finde letzten Labyrinth-Block
        const CBlockIndex* q = block.pprev;
        int blocksSinceLabyrinth = 0;
        double lastLabyrinthDiff = 0.0;
        for (; q && blocksSinceLabyrinth < consensusParams.maxKPow; ++blocksSinceLabyrinth) {
            if (q->GetBlockHeader().IsHiveMined(consensusParams)) {
                lastLabyrinthDiff = GetDifficulty(q, true);
                break;
            }
            q = q->pprev;
        }
        // Apply k-Scaling, auch hier q kann nullptr sein, aber wir verwenden nur lastLabyrinthDiff
        unsigned int k = consensusParams.maxKPow - blocksSinceLabyrinth;
        if (lastLabyrinthDiff < consensusParams.powSplit1) k >>= 1;
        if (lastLabyrinthDiff < consensusParams.powSplit2) k >>= 1;
        if (k < 1) k = 1;
        bnTargetScaled *= k;
    }

    return bnTargetScaled;
}


// Cascoin: Labyrinth: Use this to compute estimated hashes for GetNetworkHashPS()
// Cascoin: MinotaurX+Hive1.2: Only consider the requested powType
arith_uint256 GetNumHashes(const CBlockIndex& block, POW_TYPE powType)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0 || block.GetBlockHeader().IsHiveMined(Params().GetConsensus()))
        return 0;

    // Cascoin: MinotaurX+Hive1.2: skip the wrong pow type
    if (IsMinotaurXEnabled(&block, Params().GetConsensus()) && block.GetBlockHeader().GetPoWType() != powType)
        return 0;
    // Cascoin: MinotaurX+Hive1.2: if you ask for minotaurx hashes before it's enabled, there aren't any!
    if (!IsMinotaurXEnabled(&block, Params().GetConsensus()) && powType == POW_TYPE_MINOTAURX) 
        return 0;
 
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for an arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (bnTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-nullptr. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}
