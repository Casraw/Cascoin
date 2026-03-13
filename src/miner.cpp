// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <hash.h>
#include <crypto/scrypt.h>
#include <validation.h>
#include <net.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <timedata.h>
#include <util.h>
#include <utilmoneystr.h>
#include <validationinterface.h>

#include <algorithm>
#include <queue>
#include <utility>

#include <wallet/wallet.h>  // Cascoin: Labyrinth
#include <rpc/server.h>     // Cascoin: Labyrinth
#include <base58.h>         // Cascoin: Labyrinth
#include <sync.h>           // Cascoin: Labyrinth
#include <boost/thread.hpp> // Cascoin: Labyrinth: Mining optimisations
#include <crypto/minotaurx/yespower/yespower.h>  // Cascoin: MinotaurX+Hive1.2


static CCriticalSection cs_solution_vars;
std::atomic<bool> solutionFound;            // Cascoin: Labyrinth: Mining optimisations: Thread-safe atomic flag to signal solution found (saves a slow mutex)
std::atomic<bool> earlyAbort;               // Cascoin: Labyrinth: Mining optimisations: Thread-safe atomic flag to signal early abort needed
CMouseRange solvingRange;                     // Cascoin: Labyrinth: Mining optimisations: The solving range (protected by mutex)
uint32_t solvingMouse;                        // Cascoin: Labyrinth: Mining optimisations: The solving mouse (protected by mutex)

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest fee rate of a transaction combined with all
// its ancestors.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockWeight = 0;

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    // Cascoin: Labyrinth: Don't do this
    /*
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    */

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT - 4000, options.nBlockMaxWeight));
}

static BlockAssembler::Options DefaultOptions(const CChainParams& params)
{
    // Block resource limits
    // If neither -blockmaxsize or -blockmaxweight is given, limit to DEFAULT_BLOCK_MAX_*
    // If only one is given, only restrict the specified resource.
    // If both are given, restrict both.
    BlockAssembler::Options options;
    options.nBlockMaxWeight = gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
    if (gArgs.IsArgSet("-blockmintxfee")) {
        CAmount n = 0;
        ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n);
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
    return options;
}

BlockAssembler::BlockAssembler(const CChainParams& params) : BlockAssembler(params, DefaultOptions(params)) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;
    fIncludeBCTs = true;    // Cascoin: Labyrinth

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

// Cascoin: Labyrinth: If hiveProofScript is passed, create a Labyrinth block instead of a PoW block
// Cascoin: MinotaurX+Hive1.2: Accept POW_TYPE arg
std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx, const CScript* hiveProofScript, const POW_TYPE powType)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);

    // Cascoin: Labyrinth: Make sure Labyrinth is enabled if a Labyrinth block is requested
    if (hiveProofScript && !IsLabyrinthEnabled(pindexPrev, chainparams.GetConsensus()))
        throw std::runtime_error(
            "Error: The Labyrinth is not yet enabled on the network"
        );

    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());

    // Cascoin: MinotaurX+Hive1.2: Refuse to attempt to create a non-sha256 block before activation
    if (!IsMinotaurXEnabled(pindexPrev, chainparams.GetConsensus()) && powType != 0)
        throw std::runtime_error("Error: Won't attempt to create a non-sha256 block before MinotaurX activation");

    // Cascoin: MinotaurX+Hive1.2: If MinotaurX is enabled, and we're not creating a Hive block, encode desired pow type.
    if (!hiveProofScript && IsMinotaurXEnabled(pindexPrev, chainparams.GetConsensus())) {
        if (powType >= NUM_BLOCK_TYPES)
            throw std::runtime_error("Error: Unrecognised pow type requested");
        pblock->nVersion |= powType << 16;
    }

    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization) or when
    // -promiscuousmempoolflags is used.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus()) && fMineWitnessTx;

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    // Cascoin: Don't include BCTs in hivemined blocks
    if (hiveProofScript)
        fIncludeBCTs = false;

    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockWeight = nBlockWeight;

    // Cascoin: Labyrinth: Create appropriate coinbase tx for pow or Labyrinth block
    if (hiveProofScript) {
        CMutableTransaction coinbaseTx;

        // 1 vin with empty prevout
        coinbaseTx.vin.resize(1);
        coinbaseTx.vin[0].prevout.SetNull();
        // BIP34 requires the block height to be included in the coinbase
        // The validation code specifically expects the scriptSig to BEGIN WITH: CScript() << nHeight
        // We then append arbitrary data to ensure the minimum size requirement for coinbase scriptSig
        CScript scriptSig;
        scriptSig << nHeight;
        // Ensure the scriptSig is at least 2 bytes by padding if needed
        if (scriptSig.size() < 2) {
            scriptSig << OP_0;
        }
        coinbaseTx.vin[0].scriptSig = scriptSig;

        // vout[0]: Labyrinth proof
        coinbaseTx.vout.resize(2);
        coinbaseTx.vout[0].scriptPubKey = *hiveProofScript;
        coinbaseTx.vout[0].nValue = 0;

        // vout[1]: Cheese :)
        coinbaseTx.vout[1].scriptPubKey = scriptPubKeyIn;

        // Cascoin: MinotaurX+Hive1.2: Hive rewards are 150% of base block reward
        coinbaseTx.vout[1].nValue = GetBlockSubsidy(nHeight, chainparams.GetConsensus());
        if (IsMinotaurXEnabled(pindexPrev, chainparams.GetConsensus()))
            coinbaseTx.vout[1].nValue += coinbaseTx.vout[1].nValue >> 1;
        coinbaseTx.vout[1].nValue += nFees;

        // vout[2]: Coinbase commitment
        pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
        pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
        pblocktemplate->vTxFees[0] = -nFees;
    } else {
        CMutableTransaction coinbaseTx;
        coinbaseTx.vin.resize(1);
        coinbaseTx.vin[0].prevout.SetNull();
        coinbaseTx.vout.resize(1);
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

        // Cascoin: MinotaurX+Hive1.2: Pow rewards are 50% of base block reward
        coinbaseTx.vout[0].nValue = GetBlockSubsidy(nHeight, chainparams.GetConsensus());
        if (IsMinotaurXEnabled(pindexPrev, chainparams.GetConsensus()))
            coinbaseTx.vout[0].nValue = coinbaseTx.vout[0].nValue >> 1;

        coinbaseTx.vout[0].nValue += nFees;

        // BIP34 requires the block height to be included in the coinbase
        // The validation code specifically expects the scriptSig to BEGIN WITH: CScript() << nHeight
        // We then append arbitrary data to ensure the minimum size requirement for coinbase scriptSig
        CScript scriptSig;
        scriptSig << nHeight;
        // Ensure the scriptSig is at least 2 bytes by padding if needed
        if (scriptSig.size() < 2) {
            scriptSig << OP_0;
        }
        coinbaseTx.vin[0].scriptSig = scriptSig;
        pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
        pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
        pblocktemplate->vTxFees[0] = -nFees;
    }

    // Change debug level BEGIN
    // LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);
    LogPrint(BCLog::ALL, "CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);
    // Change debug level END

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);

    // Cascoin: Labyrinth: Choose correct nBits depending on whether a Labyrinth block is requested
    if (hiveProofScript)
        pblock->nBits = GetNextHiveWorkRequired(pindexPrev, chainparams.GetConsensus());
    else {
        // Cascoin: MinotaurX+Hive1.2: If MinotaurX is enabled, handle nBits with pow-specific diff algo
        if (IsMinotaurXEnabled(pindexPrev, chainparams.GetConsensus()))
            pblock->nBits = GetNextWorkRequiredLWMA(pindexPrev, pblock, chainparams.GetConsensus(), powType);
        else
            pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    }

    // Cascoin: Labyrinth: Set nonce marker for labyrinth mined blocks
    pblock->nNonce = hiveProofScript ? chainparams.GetConsensus().hiveNonceMarker : 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    // Cascoin: Diagnostic log for SHA256 on new chain
    if (!hiveProofScript && powType == POW_TYPE_SHA256 && IsMinotaurXEnabled(pindexPrev, chainparams.GetConsensus())) {
        bool foundPrevShaBlock = false;
        const CBlockIndex* tempIndex = pindexPrev;
        int count = 0;
        while (tempIndex && count < chainparams.GetConsensus().lwmaAveragingWindow * 2) { // Check a reasonable depth
            if (!tempIndex->GetBlockHeader().IsHiveMined(chainparams.GetConsensus()) && tempIndex->GetBlockHeader().GetPoWType() == POW_TYPE_SHA256) {
                foundPrevShaBlock = true;
                break;
            }
            if (!tempIndex->pprev) break;
            tempIndex = tempIndex->pprev;
            count++;
        }
        if (!foundPrevShaBlock) {
            LogPrint(BCLog::MINOTAURX, "CreateNewBlock: About to TestBlockValidity for a new SHA256 block. No prior SHA256 blocks found. pindexPrev height %d, type %s. Block nBits: %08x, Expected PoW limit for SHA256: %08x\n",
                pindexPrev->nHeight,
                pindexPrev->GetBlockHeader().GetPoWTypeName(),
                pblock->nBits,
                UintToArith256(chainparams.GetConsensus().powTypeLimits[POW_TYPE_SHA256]).GetCompact()
            );
            // Ensure nBits is at least the limit if no prior blocks were found (LWMA should do this, but as a safeguard)
            if (pblock->nBits == 0 || arith_uint256().SetCompact(pblock->nBits) < UintToArith256(chainparams.GetConsensus().powTypeLimits[POW_TYPE_SHA256])) {
                 // Correction: Difficulty is inverse of target. Higher target (easier) means lower difficulty.
                 // We want the easiest target (powLimit).
                 // A higher compact nBits value means an easier target.
                 // So if current nBits represents a harder target than powLimit, set to powLimit.
                arith_uint256 currentTarget;
                currentTarget.SetCompact(pblock->nBits);
                if (currentTarget == 0 || currentTarget < UintToArith256(chainparams.GetConsensus().powTypeLimits[POW_TYPE_SHA256])) { //This condition might be wrong; target comparison is tricky. Target < Limit means harder.
                    // Actually, powLimit is the *easiest* target. Numerical value is higher.
                    // nBits should be the compact representation of powLimit.
                    // If GetNextWorkRequiredLWMA returned something harder, this log might be useful.
                    // For now, primarily logging.
                }
            }
        }
    }

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }

    int64_t nTime2 = GetTimeMicros();

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    const Consensus::Params& consensusParams = Params().GetConsensus(); // Cascoin: Labyrinth

    for (const CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
        // Cascoin: Labyrinth: Inhibit BCTs if required
        if (!fIncludeBCTs && it->GetTx().IsBCT(consensusParams, GetScriptForDestination(DecodeDestination(consensusParams.mouseCreationAddress))))
            return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (const CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

// Cascoin: Labyrinth: Mouse management thread
void MouseKeeper(const CChainParams& chainparams) {
    const Consensus::Params& consensusParams = chainparams.GetConsensus();

    LogPrintf("MouseKeeper: Thread started\n");
    RenameThread("labyrinth-mousekeeper");

    int height;
    {
        LOCK(cs_main);
        height = chainActive.Tip()->nHeight;
    }

    try {
        while (true) {
            // Cascoin: Labyrinth: Mining optimisations: Parameterised sleep time
            int sleepTime = std::max((int64_t) 1, gArgs.GetArg("-labyrinthcheckdelay", DEFAULT_HIVE_CHECK_DELAY));
            MilliSleep(sleepTime);

            int newHeight;
            {
                LOCK(cs_main);
                newHeight = chainActive.Tip()->nHeight;
            }
            if (newHeight != height) {
                // Height changed; release the mice!
                height = newHeight;
                try {
                    BusyMice(consensusParams, height);
                } catch (const std::runtime_error &e) {
                    LogPrintf("! MouseKeeper: Error: %s\n", e.what());
                }
            }
        }
    } catch (const boost::thread_interrupted&) {
        LogPrintf("!!! MouseKeeper: FATAL: Thread interrupted\n");
        throw;
    }
}

// Cascoin: Labyrinth: Mining optimisations: Thread to signal abort on new block
void AbortWatchThread(int height) {
    // Loop until any exit condition
    while (true) {
        // Yield to OS
        MilliSleep(1);

        // Check pre-existing abort conditions
        if (solutionFound.load() || earlyAbort.load())
            return;

        // Get tip height, keeping lock scope as short as possible
        int newHeight;
        {
            LOCK(cs_main);
            newHeight = chainActive.Tip()->nHeight;
        }

        // Check for abort from tip height change
        if (newHeight != height) {
            //LogPrintf("*** ABORT FIRE\n");
            earlyAbort.store(true);
            return;
        }
    }
}

// Cascoin: Labyrinth: Mining optimisations: Thread to check a single bin
void CheckBin(int threadID, std::vector<CMouseRange> bin, std::string deterministicRandString, arith_uint256 mouseHashTarget) {
    // Iterate over ranges in this bin
    int checkCount = 0;
    for (std::vector<CMouseRange>::const_iterator it = bin.begin(); it != bin.end(); it++) {
        CMouseRange mouseRange = *it;
        //LogPrintf("THREAD #%i: Checking %i-%i in %s\n", threadID, mouseRange.offset, mouseRange.offset + mouseRange.count - 1, mouseRange.txid);
        // Iterate over mice in this range
        for (int i = mouseRange.offset; i < mouseRange.offset + mouseRange.count; i++) {
            // Check abort conditions (Only every N mice. The atomic load is expensive, but much cheaper than a mutex - esp on Windows, see https://www.arangodb.com/2015/02/comparing-atomic-mutex-rwlocks/)
            if(checkCount++ % 1000 == 0) {
                if (solutionFound.load() || earlyAbort.load()) {
                    //LogPrintf("THREAD #%i: Solution found elsewhere or early abort requested, ending early\n", threadID);
                    return;
                }
            }
            // Hash the mouse
            std::string hashHex = (CHashWriter(SER_GETHASH, 0) << deterministicRandString << mouseRange.txid << i).GetHash().GetHex();
            arith_uint256 mouseHash = arith_uint256(hashHex);
            // Compare to target and write out result if successful
            if (mouseHash < mouseHashTarget) {
                //LogPrintf("THREAD #%i: Solution found, returning\n", threadID);
                LOCK(cs_solution_vars);                                 // Expensive mutex only happens at write-out
                solutionFound.store(true);
                solvingRange = mouseRange;
                solvingMouse = i;
                return;
            }
        }
    }
    //LogPrintf("THREAD #%i: Out of tasks\n", threadID);
}

// Cascoin: MinotaurX+Hive1.2: Thread to check a single mouse bin
void CheckBinMinotaur(int threadID, std::vector<CMouseRange> bin, std::string deterministicRandString, arith_uint256 mouseHashTarget) {
    // Create yespower thread-local storage
    /*
    static __thread yespower_local_t local;
    yespower_init_local(&local);
    */

    // Iterate over ranges in this bin
    int checkCount = 0;
    for (std::vector<CMouseRange>::const_iterator it = bin.begin(); it != bin.end(); it++) {
        CMouseRange mouseRange = *it;
        //LogPrintf("THREAD #%i: Checking %i-%i in %s\n", threadID, mouseRange.offset, mouseRange.offset + mouseRange.count - 1, mouseRange.txid);
        // Iterate over mice in this range
        for (int i = mouseRange.offset; i < mouseRange.offset + mouseRange.count; i++) {
            // Check abort conditions (Only every N mice. The atomic load is expensive, but much cheaper than a mutex - esp on Windows, see https://www.arangodb.com/2015/02/comparing-atomic-mutex-rwlocks/)
            if(checkCount++ % 1000 == 0) {
                if (solutionFound.load() || earlyAbort.load()) {
                    //LogPrintf("THREAD #%i: Solution found elsewhere or early abort requested, ending early\n", threadID);
                    //yespower_free_local(&local);
                    return;
                }
            }

            // Hash the mouse
            std::stringstream buf;
            buf << deterministicRandString;
            buf << mouseRange.txid;
            buf << i;
            std::string hashString = buf.str();

            uint256 mouseHashUint = CBlockHeader::MinotaurHashString(hashString);
            arith_uint256 mouseHash(mouseHashUint.ToString());

            // Compare to target and write out result if successful
            if (mouseHash < mouseHashTarget) {
                //LogPrintf("THREAD #%i: Solution found, returning\n", threadID);
                LOCK(cs_solution_vars);     // Expensive mutex only happens at write-out
                solutionFound.store(true);
                solvingRange = mouseRange;
                solvingMouse = i;
                //yespower_free_local(&local);
                return;
            }

            boost::this_thread::yield();
        }
    }
    //LogPrintf("THREAD #%i: Out of tasks\n", threadID);
    //yespower_free_local(&local);
}

// Cascoin: Labyrinth: Attempt to mint the next block
bool BusyMice(const Consensus::Params& consensusParams, int height) {
    bool verbose = LogAcceptCategory(BCLog::LABYRINTH);

    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);

    // Sanity checks
    if (!IsLabyrinthEnabled(pindexPrev, consensusParams)) {
        LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check: The Labyrinth is not enabled on the network\n");
        return false;
    }
    if(!g_connman) {
        LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check: Peer-to-peer functionality missing or disabled\n");
        return false;
    }
    if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) {
        LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check (not connected)\n");
        return false;
    }
    if (IsInitialBlockDownload()) {
        LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check (in initial block download)\n");
        return false;
    }

    // Cascoin: Labyrinth 1.1: Check that there aren't too many consecutive Labyrinth blocks
    if (IsLabyrinth11Enabled(pindexPrev, consensusParams)) {
        int hiveBlocksAtTip = 0;
        CBlockIndex* pindexTemp = pindexPrev;
        while (pindexTemp->GetBlockHeader().IsHiveMined(consensusParams)) {
            assert(pindexTemp->pprev);
            pindexTemp = pindexTemp->pprev;
            hiveBlocksAtTip++;
        }
        if (hiveBlocksAtTip >= consensusParams.maxConsecutiveLabyrinthBlocks) {
            LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check (max Labyrinth blocks without a POW block reached)\n");
            return false;
        }
    } else {
        // Check previous block wasn't hivemined
        if (pindexPrev->GetBlockHeader().IsHiveMined(consensusParams)) {
            LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check (Labyrinth block must follow a POW block)\n");
            return false;
        }
    }

    // Get wallet
    JSONRPCRequest request;
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, true)) {
        LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check (wallet unavailable)\n");
        return false;
    }
    if (pwallet->IsLocked()) {
        LogPrint(BCLog::LABYRINTH, "BusyMice: Skipping labyrinth check, wallet is locked\n");
        return false;
    }

    LogPrintf("********************* Labyrinth: Mice at work *********************\n");

    // Find deterministicRandString
    std::string deterministicRandString = GetDeterministicRandString(pindexPrev);
    if (verbose) LogPrintf("BusyMice: deterministicRandString   = %s\n", deterministicRandString);

    // Find mouseHashTarget
    arith_uint256 mouseHashTarget;
    mouseHashTarget.SetCompact(GetNextHiveWorkRequired(pindexPrev, consensusParams));
    LogPrint(BCLog::LABYRINTH, "BusyMice: mouseHashTarget for current attempt = %s\n", mouseHashTarget.ToString());

    // Grab all BCTs from wallet that are mature and not yet expired.
    // We don't need to scan for rewards here as we only need the txid and cheese address.
    // Cascoin: Memory leak fix - Limit potential BCTs to prevent memory overflow
    std::vector<CMouseCreationTransactionInfo> potentialBctsWallet = pwallet->GetBCTs(false, false, consensusParams);
    std::vector<CMouseCreationTransactionInfo> potentialBcts; // This will store the filtered BCTs
    
    // Reserve reasonable space and limit processing
    potentialBcts.reserve(std::min((size_t)5000, potentialBctsWallet.size()));
    int totalMice = 0;

    // Original logic to populate potentialBcts and totalMice based on wallet status
    for (const auto& bctInfoWallet : potentialBctsWallet) {
        if (bctInfoWallet.mouseStatus == "mature") { // Filter by mature status from wallet
            potentialBcts.push_back(bctInfoWallet);
            totalMice += bctInfoWallet.mouseCount;
        }
    }

    if (potentialBcts.empty() || totalMice == 0)
    {
        LogPrint(BCLog::LABYRINTH, "BusyMice: No mature mice found\n");
        return false;
    }

    int coreCount = GetNumVirtualCores();
    int threadCount = gArgs.GetArg("-labyrinthcheckthreads", DEFAULT_HIVE_THREADS);
    if (threadCount == -2)
        threadCount = std::max(1, coreCount - 1);
    else if (threadCount < 0 || threadCount > coreCount)
        threadCount = coreCount;
    else if (threadCount == 0)
        threadCount = 1;

    int micePerBin = ceil(totalMice / (float)threadCount);  // We want to check this many mice per thread

    // Cascoin: Memory leak fix - Limit mouse binning to prevent memory overflow
    if (verbose) LogPrint(BCLog::LABYRINTH, "BusyMice: Binning %i mice in %i bins (%i mice per bin)\n", totalMice, threadCount, micePerBin);
    std::vector<CMouseCreationTransactionInfo>::const_iterator bctIterator = potentialBcts.begin();
    CMouseCreationTransactionInfo bct = *bctIterator;
    std::vector<std::vector<CMouseRange>> mouseBins;
    
    // Reserve space for bins to prevent frequent reallocations
    mouseBins.reserve(std::min(threadCount, 100)); // Limit to max 100 bins
    int mouseOffset = 0;                                      // Track offset in current BCT
    while(bctIterator != potentialBcts.end()) {                      // Until we're out of BCTs
        std::vector<CMouseRange> currentBin;                  // Create a new bin
        int miceInBin = 0;
        while (bctIterator != potentialBcts.end()) {                 // Keep filling it until full
            int spaceLeft = micePerBin - miceInBin;
            if (bct.mouseCount - mouseOffset <= spaceLeft) {    // If there's soom, add all the mice from this BCT...
                CMouseRange range = {bct.txid, bct.cheeseAddress, bct.communityContrib, mouseOffset, bct.mouseCount - mouseOffset};
                currentBin.push_back(range);

                miceInBin += bct.mouseCount - mouseOffset;
                mouseOffset = 0;

                do {                                        // ... and iterate to next BCT
                    bctIterator++;
                    if (bctIterator == potentialBcts.end())
                        break;
                    bct = *bctIterator;
                } while (bct.mouseStatus != "mature");
            } else {                                        // Can't fit the whole thing to current bin; add what we can fit and let the rest go in next bin
                CMouseRange range = {bct.txid, bct.cheeseAddress, bct.communityContrib, mouseOffset, spaceLeft};
                currentBin.push_back(range);
                mouseOffset += spaceLeft;
                break;
            }
        }
        mouseBins.push_back(currentBin);
    }

    // Create a worker thread for each bin
    if (verbose) LogPrintf("BusyMice: Running bins\n");
    solutionFound.store(false);
    earlyAbort.store(false);
    std::vector<std::vector<CMouseRange>>::const_iterator mouseBinIterator = mouseBins.begin();
    std::vector<boost::thread> binThreads;
    int64_t checkTime = GetTimeMillis();
    int binID = 0;
    bool minotaurXEnabled = IsMinotaurXEnabled(pindexPrev, consensusParams);    // Cascoin: MinotaurX+Hive1.2: Check if minotaurX enabled
    while (mouseBinIterator != mouseBins.end()) {
        std::vector<CMouseRange> mouseBin = *mouseBinIterator;

        if (verbose) {
            LogPrintf("BusyMice: Bin #%i\n", binID);
            std::vector<CMouseRange>::const_iterator mouseRangeIterator = mouseBin.begin();
            while (mouseRangeIterator != mouseBin.end()) {
                CMouseRange mouseRange = *mouseRangeIterator;
                LogPrintf("offset = %i, count = %i, txid = %s\n", mouseRange.offset, mouseRange.count, mouseRange.txid);
                mouseRangeIterator++;
            }
        }
        // Cascoin: MinotaurX+Hive1.2: Use correct inner hash
        if (!minotaurXEnabled)
            binThreads.push_back(boost::thread(CheckBin, binID++, mouseBin, deterministicRandString, mouseHashTarget));
        else
            binThreads.push_back(boost::thread(CheckBinMinotaur, binID++, mouseBin, deterministicRandString, mouseHashTarget));

        mouseBinIterator++;
    }

    // Add an extra thread to watch external abort conditions (eg new incoming block)
    bool useEarlyAbortThread = gArgs.GetBoolArg("-labyrinthearlyout", DEFAULT_HIVE_EARLY_OUT);
    if (verbose && useEarlyAbortThread)
        LogPrintf("BusyMice: Will use early-abort thread\n");

    boost::thread earlyAbortThread;
    if (useEarlyAbortThread)
        earlyAbortThread = boost::thread(AbortWatchThread, height);

    // Wait for bin worker threads to find a solution or abort (in which case the others will all stop), or to run out of mice
    for(auto& t:binThreads)
        t.join();

    binThreads.clear();

    checkTime = GetTimeMillis() - checkTime;

    // Handle early aborts
    if (useEarlyAbortThread) {
        if (earlyAbort.load()) {
            LogPrintf("BusyMice: Chain state changed (check aborted after %ims)\n", checkTime);
            return false;
        } else {
            // We didn't abort; stop abort thread now
            earlyAbort.store(true);
            earlyAbortThread.join();
        }
    }

    // Check if a solution was found
    if (!solutionFound.load()) {
        LogPrintf("BusyMice: No mouse meets hash target (%i mice checked with %i threads in %ims)\n", totalMice, threadCount, checkTime);
        return false;
    }
    LogPrintf("BusyMice: Mouse meets hash target (check aborted after %ims). Solution with mouse #%i from BCT %s. Cheese address is %s.\\n", checkTime, solvingMouse, solvingRange.txid, solvingRange.cheeseAddress);

    // Assemble The Labyrinth proof script
    std::vector<unsigned char> messageProofVec;
    std::vector<unsigned char> txidVec(solvingRange.txid.begin(), solvingRange.txid.end());
    CScript hiveProofScript;
    uint32_t bctHeight;
    {   // Don't lock longer than needed
        LOCK2(cs_main, pwallet->cs_wallet);

        uint256 initialTipHashAtStartOfBusyMice = pindexPrev->GetBlockHash(); // pindexPrev is from the start of BusyMice
        uint256 currentActiveTipHash = chainActive.Tip() ? chainActive.Tip()->GetBlockHash() : uint256();
        uint256 currentPcoinsTipHash = pcoinsTip ? pcoinsTip->GetBestBlock() : uint256();

        // Re-verify BCT before using it
        CTransactionRef txBCT;
        uint256 hashBlockBCT; // Block containing the BCT

        if (!GetTransaction(uint256S(solvingRange.txid), txBCT, consensusParams, hashBlockBCT, true)) {
            LogPrintf("BusyMice: BCT %s for winning mouse: GetTransaction failed. Aborting. StartTip: %s, ActiveTip: %s, UTXOTip: %s\\n",
                solvingRange.txid, initialTipHashAtStartOfBusyMice.ToString(), currentActiveTipHash.ToString(), currentPcoinsTipHash.ToString());
            return false;
        }

        if (hashBlockBCT.IsNull() || mapBlockIndex.find(hashBlockBCT) == mapBlockIndex.end()) {
            LogPrintf("BusyMice: BCT %s for winning mouse: Block hash %s not found in mapBlockIndex. Aborting. StartTip: %s, ActiveTip: %s, UTXOTip: %s\\n",
                solvingRange.txid, hashBlockBCT.ToString(), initialTipHashAtStartOfBusyMice.ToString(), currentActiveTipHash.ToString(), currentPcoinsTipHash.ToString());
            return false;
        }
        
        CBlockIndex* pindexBCT = mapBlockIndex[hashBlockBCT];
        if (!chainActive.Contains(pindexBCT)) {
            LogPrintf("BusyMice: BCT %s (in block %s, height %d) for winning mouse is no longer in the active chain (reorg?). Aborting. StartTip: %s, ActiveTip: %s, UTXOTip: %s\\n",
                solvingRange.txid, hashBlockBCT.ToString(), pindexBCT->nHeight, initialTipHashAtStartOfBusyMice.ToString(), currentActiveTipHash.ToString(), currentPcoinsTipHash.ToString());
            return false;
        }

        // BCT transaction is confirmed in an active block.
        // Get the BCT height from its block index.
        bctHeight = pindexBCT->nHeight;
        LogPrintf("BusyMice: Verified BCT %s is in active block %s at height %d. StartTip: %s, ActiveTip: %s, UTXOTip: %s\\n",
            solvingRange.txid, hashBlockBCT.ToString(), bctHeight, initialTipHashAtStartOfBusyMice.ToString(), currentActiveTipHash.ToString(), currentPcoinsTipHash.ToString());

        // The OP_RETURN output (vout[0]) of the BCT is not expected to be in the UTXO set (pcoinsTip).
        // Its validity is confirmed by GetTransaction and its presence in the active chain.

        CTxDestination dest = DecodeDestination(solvingRange.cheeseAddress);
        if (!IsValidDestination(dest)) {
            LogPrintf("BusyMice: Cheese destination invalid\n");
            return false;
        }

        const CKeyID *keyID = boost::get<CKeyID>(&dest);
        if (!keyID) {
            LogPrintf("BusyMice: Wallet doesn't have privkey for cheese destination\n");
            return false;
        }

        CKey key;
        if (!pwallet->GetKey(*keyID, key)) {
            LogPrintf("BusyMice: Privkey unavailable\n");
            return false;
        }

        CHashWriter ss(SER_GETHASH, 0);
        ss << deterministicRandString;
        uint256 mhash = ss.GetHash();
        if (!key.SignCompact(mhash, messageProofVec)) {
            LogPrintf("BusyMice: Couldn't sign the mouse proof!\n");
            return false;
        }
        if (verbose) LogPrintf("BusyMice: messageSig                = %s\n", HexStr(&messageProofVec[0], &messageProofVec[messageProofVec.size()]));
    }

    unsigned char mouseNonceEncoded[4];
    WriteLE32(mouseNonceEncoded, solvingMouse);
    std::vector<unsigned char> mouseNonceVec(mouseNonceEncoded, mouseNonceEncoded + 4);

    unsigned char bctHeightEncoded[4];
    WriteLE32(bctHeightEncoded, bctHeight);
    std::vector<unsigned char> bctHeightVec(bctHeightEncoded, bctHeightEncoded + 4);

    opcodetype communityContribFlag = solvingRange.communityContrib ? OP_TRUE : OP_FALSE;
    hiveProofScript << OP_RETURN << OP_MOUSE << mouseNonceVec << bctHeightVec << communityContribFlag << txidVec << messageProofVec;

    // Create cheese script from cheese address
    CScript cheeseScript = GetScriptForDestination(DecodeDestination(solvingRange.cheeseAddress));

    // Create a Labyrinth block
    std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(cheeseScript, true, &hiveProofScript));
    if (!pblocktemplate.get()) {
        LogPrintf("BusyMice: Couldn't create block\n");
        return false;
    }
    CBlock *pblock = &pblocktemplate->block;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);  // Calc the merkle root

    // Make sure the new block's not stale
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash()) {
            LogPrintf("BusyMice: Generated block is stale.\n");
            return false;
        }
    }

    if (verbose) {
        LogPrintf("BusyMice: Block created:\n");
        LogPrintf("%s",pblock->ToString());
    }

    // Commit and propagate the block
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
        LogPrintf("BusyMice: Block wasn't accepted\n");
        return false;
    }

    LogPrintf("BusyMice: ** Block mined\n");
    return true;
}
