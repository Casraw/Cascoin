// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Trust System Activation — Exploratory Bug-Condition Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/trust-system-activation  (bugfix)
 * Task 1: "Write exploratory bug-condition tests that reproduce ALL defects on
 *          the UNFIXED code".
 *
 * PURPOSE
 * -------
 * The on-chain Web-of-Trust WRITE path is dead code. When a block containing a
 * non-contract CVM OP_RETURN transaction (`TRUST_EDGE`, `REPUTATION_VOTE`,
 * `BONDED_VOTE`, `DAO_DISPUTE`, `DAO_VOTE`) is connected, the node is supposed
 * to persist the canonical record the WoT/reputation read APIs enumerate. It
 * does not: `CVMBlockProcessor::ProcessBlock()` is disabled in `ConnectBlock()`
 * and the active path `BlockValidator::ValidateBlock()` skips every non-contract
 * CVM type. The only step `ConnectBlock()` runs for non-contract CVM data in the
 * durable-write phase is `CVMBlockProcessor::ProcessClusterUpdates()`, which does
 * NOT persist canonical trust edges or apply reputation votes.
 *
 * This suite reproduces that defect at the block-processing seam. It mirrors the
 * durable-write CVM section of `ConnectBlock()` exactly as it exists on the
 * UNFIXED code (only `ProcessClusterUpdates()` runs — see
 * `RunConnectBlockNonContractDurableWrite` below) and then asserts the EXPECTED
 * (post-fix, clauses 2.1/2.4) behaviour: the canonical `trust_<from>_<to>` edge
 * is persisted and the on-chain reputation vote is applied. On the UNFIXED code
 * these assertions FAIL — each failure is a concrete counterexample confirming
 * the root cause.
 *
 * POST-FIX NOTE (task 3.2 / design "Change 2"): the fix wires
 * `CVMBlockProcessor::ProcessNonContractBlock(block, height, db)` into exactly
 * this durable-write section of `ConnectBlock()`, immediately before
 * `ProcessClusterUpdates()`. When that call is added to
 * `RunConnectBlockNonContractDurableWrite` below (mirroring the production
 * change), the SAME assertions in this file pass unchanged (task 3.5). The
 * end-to-end functional regtest `feature_trust_activation.py` drives the real
 * `ConnectBlock()` and flips from FAIL to PASS automatically once the fix lands.
 *
 * DO NOT fix the production code or these tests when they fail here. Surfacing
 * the counterexamples is the whole point of this task.
 *
 * Bug-condition coverage (bugfix.md / design.md):
 *   Case 1  Trust edge not persisted as a canonical edge on connect   (1.1, 1.2, 1.3)
 *   Case 2  Reputation vote not applied on connect                    (1.4)
 *
 * Requirements: 1.1, 1.2, 1.3, 1.4
 */

#include <cvm/blockprocessor.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/reputation.h>

#include <amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace {

// A bond comfortably above the required bond for a weight-80 edge:
//   required = minBondAmount (1 CAS) + bondPerVotePoint (0.01 CAS) * 80 = 1.8 CAS
// (COIN == 10'000'000 in Cascoin). 2 CAS clears it.
static const CAmount kBond = 2 * COIN;
static const int16_t kWeight = 80;
static const int16_t kVoteValue = 100;
static const uint32_t kTimestamp = 1700000000;

// A random uint160 (20-byte trust-node value).
uint160 RandU160()
{
    uint160 a;
    uint256 r = InsecureRand256();
    std::memcpy(a.begin(), r.begin(), 20);
    return a;
}

// A minimal coinbase transaction (IsCoinBase() == true) so the block has a
// realistic vtx[0]; the processors skip it, as ConnectBlock does.
CTransactionRef MakeCoinbase()
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(50 * COIN, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(mtx));
}

// Build a P2SH bond output (23 bytes: OP_HASH160 <20> OP_EQUAL) carrying the
// given amount. ProcessTrustEdge -> ValidateBond requires this at vout[1].
CTxOut MakeBondOutput(CAmount amount)
{
    CScript p2sh = CScript() << OP_HASH160 << ToByteVector(RandU160()) << OP_EQUAL;
    return CTxOut(amount, p2sh);
}

// Build a TRUST_EDGE CVM transaction: vout[0] is the OP_RETURN payload, vout[1]
// is the P2SH bond output required by the handler's bond validation.
CTransactionRef MakeTrustEdgeTx(const uint160& from, const uint160& to)
{
    CVM::CVMTrustEdgeData trustData;
    trustData.fromAddress = from;
    trustData.toAddress = to;
    trustData.weight = kWeight;
    trustData.bondAmount = kBond;
    trustData.timestamp = kTimestamp;

    std::vector<uint8_t> data = trustData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::TRUST_EDGE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(2);
    mtx.vout[0] = CTxOut(0, opret);
    mtx.vout[1] = MakeBondOutput(kBond);
    return MakeTransactionRef(std::move(mtx));
}

// Build a REPUTATION_VOTE CVM transaction (no bond required).
CTransactionRef MakeReputationVoteTx(const uint160& target)
{
    CVM::CVMReputationData voteData;
    voteData.targetAddress = target;
    voteData.voteValue = kVoteValue;
    voteData.timestamp = kTimestamp;

    std::vector<uint8_t> data = voteData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::REPUTATION_VOTE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, opret);
    return MakeTransactionRef(std::move(mtx));
}

// Mirror of the durable-write CVM section of ConnectBlock() (src/validation.cpp)
// as it exists on the UNFIXED code: only ProcessClusterUpdates() runs; the
// per-op non-contract dispatch (ProcessBlock) is disabled, and BlockValidator
// (the validation-phase contract path) skips non-contract types.
//
// POST-FIX (task 3.2 / design "Change 2"): add
//   CVM::CVMBlockProcessor::ProcessNonContractBlock(block, height, db);
// immediately before ProcessClusterUpdates() below, mirroring the production
// change. The assertions in this file then pass unchanged (task 3.5).
void RunConnectBlockNonContractDurableWrite(const CBlock& block, int height,
                                            CVM::CVMDatabase& db)
{
    // POST-FIX (task 3.2 / design "Change 2"): mirror the production ConnectBlock
    // change — the slim non-contract-only dispatch runs immediately before
    // ProcessClusterUpdates(), persisting canonical records for the five
    // non-contract CVM op types. With this call present the assertions below
    // pass unchanged (task 3.5).
    CVM::CVMBlockProcessor::ProcessNonContractBlock(block, height, db);
    CVM::CVMBlockProcessor::ProcessClusterUpdates(block, height, db);
}

// Fixture: a fresh in-memory CVM database per test (same seam the WoT and
// workstream property suites use).
struct TrustActivationExploreSetup : public BasicTestingSetup {
    TrustActivationExploreSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~TrustActivationExploreSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_trust_activation_explore_tests, TrustActivationExploreSetup)

// ===========================================================================
// Case 1 — On-chain trust edge is not persisted as a canonical edge.
//                                                          (Bug 1.1, 1.2, 1.3)
//
// Expected (2.1/2.2): connecting a block that carries a TRUST_EDGE CVM OP_RETURN
// transaction SHALL persist a canonical trust_<from>_<to> edge (signer as `from`,
// payload target as `to`, with the payload weight/bond) that
// TrustGraph::GetTrustEdge can read back.
//
// UNFIXED: ConnectBlock's durable-write non-contract path runs only
// ProcessClusterUpdates(); ProcessTrustEdge never fires, so no canonical edge is
// written -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case1_trust_edge_not_persisted_on_connect)
{
    const uint160 from = RandU160();
    const uint160 to = RandU160();

    CBlock block;
    block.vtx.push_back(MakeCoinbase());
    block.vtx.push_back(MakeTrustEdgeTx(from, to));

    const int height = 1000;
    RunConnectBlockNonContractDurableWrite(block, height, *CVM::g_cvmdb);

    // The canonical forward edge must be readable after block connection.
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::TrustEdge edge;
    bool persisted = tg.GetTrustEdge(from, to, edge);

    BOOST_CHECK_MESSAGE(persisted,
        "Case 1 (2.1/2.2): no canonical trust_<from>_<to> edge was persisted "
        "after connecting a block containing a TRUST_EDGE CVM transaction. "
        "ConnectBlock's non-contract durable-write path runs only "
        "ProcessClusterUpdates(); ProcessTrustEdge never fires because "
        "ProcessBlock() is disabled and BlockValidator::ValidateBlock() skips "
        "non-contract types.");

    if (persisted) {
        BOOST_CHECK_MESSAGE(edge.trustWeight == kWeight,
            "Case 1 (2.1): persisted edge weight " + std::to_string(edge.trustWeight)
            + " != expected " + std::to_string(kWeight) + ".");
        BOOST_CHECK_MESSAGE(edge.bondAmount == kBond,
            "Case 1 (2.1): persisted edge bond " + std::to_string(edge.bondAmount)
            + " != expected " + std::to_string(kBond) + ".");
    }

    // The graph statistics must count the one canonical forward edge (2.2).
    std::map<std::string, uint64_t> stats = tg.GetGraphStats();
    uint64_t totalEdges = 0;
    auto it = stats.find("total_trust_edges");
    if (it != stats.end()) totalEdges = it->second;
    BOOST_CHECK_MESSAGE(totalEdges >= 1,
        "Case 1 (2.2): gettrustgraphstats total_trust_edges = "
        + std::to_string(totalEdges) + " after a mined on-chain trust edge; "
        "the canonical edge was never persisted on connect.");
}

// ===========================================================================
// Case 2 — On-chain reputation vote is not applied on connect.      (Bug 1.4)
//
// Expected (2.4): connecting a block that carries a REPUTATION_VOTE CVM
// OP_RETURN transaction SHALL update the target's reputation consistently with
// the vote payload (reflected by getreputation).
//
// UNFIXED: ProcessVote never fires during block connection -> the target's
// reputation is unchanged -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case2_reputation_vote_not_applied_on_connect)
{
    const uint160 target = RandU160();

    // Baseline reputation (fresh address -> no record -> score 0).
    CVM::ReputationSystem repSystem(*CVM::g_cvmdb);
    CVM::ReputationScore before;
    repSystem.GetReputation(target, before);

    CBlock block;
    block.vtx.push_back(MakeCoinbase());
    block.vtx.push_back(MakeReputationVoteTx(target));

    const int height = 1000;
    RunConnectBlockNonContractDurableWrite(block, height, *CVM::g_cvmdb);

    CVM::ReputationScore after;
    repSystem.GetReputation(target, after);

    // EXPECTED (post-fix): the mined vote is applied on connect.
    BOOST_CHECK_MESSAGE(after.score == before.score + kVoteValue,
        "Case 2 (2.4): reputation score for the target is "
        + std::to_string(after.score) + " after a mined REPUTATION_VOTE (expected "
        + std::to_string(before.score + kVoteValue) + "); ProcessVote never fires "
        "during block connection.");
    BOOST_CHECK_MESSAGE(after.voteCount == before.voteCount + 1,
        "Case 2 (2.4): reputation vote count for the target is "
        + std::to_string(after.voteCount) + " after a mined REPUTATION_VOTE "
        "(expected " + std::to_string(before.voteCount + 1) + "); the on-chain "
        "vote was not applied on connect.");
}

BOOST_AUTO_TEST_SUITE_END()
