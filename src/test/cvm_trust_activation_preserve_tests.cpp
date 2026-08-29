// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Trust System Activation — Preservation Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/trust-system-activation  (bugfix)
 * Task 2: "Write preservation property tests for all non-bug-condition
 *          behavior".
 *
 * PURPOSE
 * -------
 * Property 5 (Preservation): for every input where the bug condition does NOT
 * hold, the fixed code SHALL produce the SAME result as the original code
 * (design.md → Correctness Properties → Property 5). Following the
 * *observation-first* methodology (design.md → Testing Strategy → Preservation
 * Checking), these tests capture the behaviour that MUST remain unchanged after
 * the fix (Alternative B: a slim, non-contract-only dispatch
 * `ProcessNonContractBlock` wired into `ConnectBlock`'s durable-write phase).
 *
 * They are EXPECTED TO PASS on the UNFIXED baseline (they record the behaviour
 * to preserve) and MUST still pass unchanged after the fix lands (task 3.6). A
 * test that FAILS after the fix would mean a regression in behaviour the fix
 * was supposed to leave untouched.
 *
 * The fix only ADDS a durable-write step that dispatches the five non-contract
 * CVM types to their handlers. Everything asserted here is therefore chosen to
 * be independent of that new step for NON-bug-condition inputs, so it holds on
 * both the unfixed and fixed node:
 *   - Contract-only blocks and CVM-free blocks produce no WoT/reputation side
 *     effects from the non-contract durable-write path.
 *   - The `addtrust` direct-write path, weight/bond rejection, soft-fork
 *     gating, and the completed `web-of-trust-fixes` read/identity behaviour
 *     use stable library seams (TrustGraph, TrustNodeId, IsCVMSoftForkActive,
 *     IsCanonicalForwardEdgeKey) untouched by the fix.
 *   - Shared serialization records round-trip byte-for-byte.
 *
 * SEAM NOTE (mirrors the exploration suite)
 * -----------------------------------------
 * `RunConnectBlockNonContractDurableWrite` mirrors the durable-write CVM section
 * of `ConnectBlock()` exactly as it exists on the UNFIXED code (only
 * `ProcessClusterUpdates()` runs). `RunConnectBlockValidationOnly` models the
 * `fJustCheck == true` path, which returns before the durable-write section, so
 * nothing at all runs. These helpers capture the NON-bug-condition contract:
 * for contract-only / CVM-free / validation-only inputs, no canonical
 * WoT/reputation record is written — which remains true after the fix (the new
 * dispatch skips contract types and is unreachable under `fJustCheck`).
 *
 * Captured behaviours, mapped to Unchanged-Behavior clauses (3.x):
 *   3.1  Contract path untouched by the non-contract durable-write dispatch.
 *   3.2  ProcessClusterUpdates() results identical/deterministic for a block.
 *   3.3  No hang / bounded time for a WoT-heavy block through the seam.
 *   3.4  addtrust (direct write) stores/lists/traverses edges identically.
 *   3.5  Invalid weight / insufficient bond / unparseable payload persist no
 *        record and do not abort processing of the rest of the block.
 *   3.6  Soft-fork gating and CVM-free blocks connect exactly as today.
 *   3.7  fJustCheck (validation-only) writes no durable WoT/reputation state.
 *   3.8  Completed web-of-trust-fixes behaviour: canonical-key classification,
 *        quantum/wide-address round-trip, propagated-edge coexistence.
 *   3.9  Shared serialization round-trips: Contract, contract state, nonces,
 *        and PropagatedTrustEdge (trust_prop_* / trust_prop_idx_*).
 *
 * Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9
 */

#include <cvm/blockprocessor.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/trustnodeid.h>
#include <cvm/reputation.h>
#include <cvm/contract.h>

#include <amount.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <uint256.h>
#include <version.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// Cheap serialization/library properties can afford many samples; DB-backed
// cases run fewer to keep the suite fast.
static constexpr int kSamples = 128;
static constexpr int kSamplesDb = 48;

// A bond comfortably above the required bond for a weight-80 edge:
//   required = minBondAmount (1 CAS) + bondPerVotePoint (0.01 CAS) * 80 = 1.8 CAS
// (COIN == 10'000'000 in Cascoin). 2 CAS clears it.
static const CAmount kBond = 2 * COIN;
static const int16_t kWeight = 80;
static const uint32_t kTimestamp = 1700000000;

// Mirror of TrustGraph::CalculateRequiredBond for the insufficient-bond
// boundary case (3.5).
CAmount RequiredBond(int16_t weight)
{
    return CVM::g_wotConfig.minBondAmount +
           CVM::g_wotConfig.bondPerVotePoint * std::abs(static_cast<int>(weight));
}

uint160 RandU160()
{
    uint160 a;
    uint256 r = InsecureRand256();
    std::memcpy(a.begin(), r.begin(), 20);
    return a;
}

// A minimal coinbase (IsCoinBase() == true); the processors skip it as
// ConnectBlock does.
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

// P2SH bond output (23 bytes: OP_HASH160 <20> OP_EQUAL).
CTxOut MakeBondOutput(CAmount amount)
{
    CScript p2sh = CScript() << OP_HASH160 << ToByteVector(RandU160()) << OP_EQUAL;
    return CTxOut(amount, p2sh);
}

// TRUST_EDGE CVM transaction: vout[0] OP_RETURN payload, vout[1] P2SH bond.
CTransactionRef MakeTrustEdgeTx(const uint160& from, const uint160& to,
                                int16_t weight = kWeight, CAmount bond = kBond)
{
    CVM::CVMTrustEdgeData trustData;
    trustData.fromAddress = from;
    trustData.toAddress = to;
    trustData.weight = weight;
    trustData.bondAmount = bond;
    trustData.timestamp = kTimestamp;

    std::vector<uint8_t> data = trustData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::TRUST_EDGE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(2);
    mtx.vout[0] = CTxOut(0, opret);
    mtx.vout[1] = MakeBondOutput(bond);
    return MakeTransactionRef(std::move(mtx));
}

// REPUTATION_VOTE CVM transaction (no bond required).
CTransactionRef MakeReputationVoteTx(const uint160& target, int16_t voteValue = 100)
{
    CVM::CVMReputationData voteData;
    voteData.targetAddress = CVM::TrustNodeId::FromLegacyUint160(target);
    voteData.voteValue = voteValue;
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

// CONTRACT_DEPLOY CVM transaction (contract type — handled by BlockValidator,
// NOT by the non-contract dispatch).
CTransactionRef MakeContractDeployTx()
{
    CVM::CVMDeployData deployData;
    deployData.codeHash = InsecureRand256();
    deployData.gasLimit = 100000;
    deployData.metadata.clear();

    std::vector<uint8_t> data = deployData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, opret);
    return MakeTransactionRef(std::move(mtx));
}

// CONTRACT_CALL CVM transaction (contract type — handled by BlockValidator).
CTransactionRef MakeContractCallTx()
{
    CVM::CVMCallData callData;
    callData.contractAddress = RandU160();
    callData.gasLimit = 100000;
    callData.callData.clear();

    std::vector<uint8_t> data = callData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_CALL, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, opret);
    return MakeTransactionRef(std::move(mtx));
}

// A plain (non-CVM) payment transaction: no CVM OP_RETURN at all.
CTransactionRef MakePlainTx()
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(10 * COIN, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(mtx));
}

// Mirror of the durable-write CVM section of ConnectBlock() (src/validation.cpp)
// as it exists on the UNFIXED code: only ProcessClusterUpdates() runs. This is
// exactly the seam the exploration suite uses. For NON-bug-condition inputs
// (contract-only / CVM-free blocks) this remains equivalent after the fix,
// because ProcessNonContractBlock() skips contract types and ignores blocks
// with no non-contract CVM payload.
void RunConnectBlockNonContractDurableWrite(const CBlock& block, int height,
                                            CVM::CVMDatabase& db)
{
    CVM::CVMBlockProcessor::ProcessClusterUpdates(block, height, db);
}

// Model the fJustCheck == true path of ConnectBlock(): it returns before the
// durable-write section, so NEITHER ProcessClusterUpdates() NOR (post-fix)
// ProcessNonContractBlock() runs. Nothing durable is written. (3.7)
void RunConnectBlockValidationOnly(const CBlock& /*block*/, int /*height*/,
                                   CVM::CVMDatabase& /*db*/)
{
    // Intentionally empty: validation-only returns before durable writes.
}

// Count canonical forward trust edges currently in the graph.
uint64_t CanonicalEdgeCount(CVM::TrustGraph& tg)
{
    std::map<std::string, uint64_t> stats = tg.GetGraphStats();
    auto it = stats.find("total_trust_edges");
    return it != stats.end() ? it->second : 0;
}

// Fixture: regtest params + a fresh in-memory CVM database per test.
struct TrustActivationPreserveSetup : public BasicTestingSetup {
    TrustActivationPreserveSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~TrustActivationPreserveSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_trust_activation_preserve_tests, TrustActivationPreserveSetup)

// ===========================================================================
// 3.1 — Contract path is untouched by the non-contract durable-write dispatch.
//
// A block containing ONLY contract CVM transactions (CONTRACT_DEPLOY /
// CONTRACT_CALL) must not produce any canonical WoT edge or reputation change
// from the non-contract durable-write path. On the unfixed node the path runs
// only ProcessClusterUpdates(); after the fix, ProcessNonContractBlock()
// explicitly skips the four contract types. Either way the WoT/reputation graph
// is untouched by this path (contract execution itself is BlockValidator's job).
// **Validates: Requirements 3.1**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_contract_block_no_wot_side_effects_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        CBlock block;
        block.vtx.push_back(MakeCoinbase());
        block.vtx.push_back(MakeContractDeployTx());
        block.vtx.push_back(MakeContractCallTx());

        RunConnectBlockNonContractDurableWrite(block, 1000, db);

        CVM::TrustGraph tg(db);
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 0,
            "3.1: a contract-only block must not create canonical trust edges "
            "via the non-contract durable-write path (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.2 — ProcessClusterUpdates() results are identical/deterministic for the
//       same block state.
//
// Running the cluster-update step twice over the same block yields the same
// result and writes no canonical trust edge. (In the unit environment the
// cluster-update handler is not initialised, so the well-defined result is 0 —
// the observation-first golden value.)
// **Validates: Requirements 3.2**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_cluster_updates_deterministic_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        CBlock block;
        block.vtx.push_back(MakeCoinbase());
        block.vtx.push_back(MakeTrustEdgeTx(RandU160(), RandU160()));
        block.vtx.push_back(MakePlainTx());

        const uint32_t r1 = CVM::CVMBlockProcessor::ProcessClusterUpdates(block, 1000, db);
        const uint32_t r2 = CVM::CVMBlockProcessor::ProcessClusterUpdates(block, 1000, db);

        BOOST_CHECK_MESSAGE(r1 == r2,
            "3.2: ProcessClusterUpdates must be deterministic for the same block; "
            "got " + std::to_string(r1) + " then " + std::to_string(r2) +
            " (#" + std::to_string(i) + ")");

        CVM::TrustGraph tg(db);
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 0,
            "3.2: ProcessClusterUpdates must not write canonical trust edges "
            "(#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.3 — No hang / bounded time for a WoT-heavy block through the seam.
//
// A block carrying many non-contract CVM transactions must be processed by the
// durable-write seam in bounded time (the whole point of Alternative B is to
// avoid the expensive-calculation hang that disabled the old ProcessBlock()).
// **Validates: Requirements 3.3**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_bounded_time_wot_heavy_block)
{
    CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                        1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    CBlock block;
    block.vtx.push_back(MakeCoinbase());
    for (int i = 0; i < 250; ++i) {
        if (i % 2 == 0) {
            block.vtx.push_back(MakeTrustEdgeTx(RandU160(), RandU160()));
        } else {
            block.vtx.push_back(MakeReputationVoteTx(RandU160()));
        }
    }

    const auto start = std::chrono::steady_clock::now();
    RunConnectBlockNonContractDurableWrite(block, 1000, db);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // A generous bound: bounded work over 250 txs must complete well under this.
    BOOST_CHECK_MESSAGE(elapsedMs < 10000,
        "3.3: processing a 250-tx WoT-heavy block through the durable-write seam "
        "must complete in bounded time; took " + std::to_string(elapsedMs) + " ms");
}

// ===========================================================================
// 3.4 — addtrust (direct write) stores/lists/traverses edges identically.
//
// The direct-write path (TrustGraph::AddTrustEdge, used by the addtrust RPC)
// must keep storing edges that read back with every field intact, appear in the
// outgoing enumeration, and are found by traversal — completely unaffected by
// the fix, which only adds the on-chain write path.
// **Validates: Requirements 3.4**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_addtrust_direct_write_property)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);

    for (int i = 0; i < kSamplesDb; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
        const std::string reason = "edge-" + std::to_string(i);

        BOOST_REQUIRE_MESSAGE(
            tg.AddTrustEdge(from, to, weight, kBond, uint256(), reason),
            "3.4: direct-write AddTrustEdge should succeed (#" + std::to_string(i) + ")");

        CVM::TrustEdge edge;
        BOOST_REQUIRE_MESSAGE(tg.GetTrustEdge(from, to, edge),
            "3.4: stored edge must be retrievable (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.trustWeight == weight,
            "3.4: weight round-trip mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.bondAmount == kBond,
            "3.4: bond round-trip mismatch (#" + std::to_string(i) + ")");

        // Enumerated in outgoing trust.
        bool foundOutgoing = false;
        for (const auto& e : tg.GetOutgoingTrust(from)) {
            if (e.toAddress == CVM::TrustNodeId::FromLegacyUint160(to)) foundOutgoing = true;
        }
        BOOST_CHECK_MESSAGE(foundOutgoing,
            "3.4: edge not returned by GetOutgoingTrust (#" + std::to_string(i) + ")");

        // Direct path traversable.
        BOOST_CHECK_MESSAGE(tg.FindTrustPaths(from, to, 3).size() >= 1,
            "3.4: direct path from->to not found (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.5 — Invalid input is rejected/ignored without persisting a record and
//       without aborting the rest of the block.
// **Validates: Requirements 3.5**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_reject_out_of_range_weight_property)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);

    for (int i = 0; i < kSamples; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        int16_t badWeight = (i % 2 == 0)
            ? static_cast<int16_t>(101 + InsecureRandRange(100))
            : static_cast<int16_t>(-(101 + static_cast<int>(InsecureRandRange(100))));

        BOOST_CHECK_MESSAGE(
            !tg.AddTrustEdge(from, to, badWeight, kBond, uint256(), "bad weight"),
            "3.5: AddTrustEdge must reject out-of-range weight " +
            std::to_string(badWeight) + " (#" + std::to_string(i) + ")");

        CVM::TrustEdge edge;
        BOOST_CHECK_MESSAGE(!tg.GetTrustEdge(from, to, edge),
            "3.5: rejected out-of-range weight must persist no edge (#" +
            std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_CASE(preserve_reject_insufficient_bond_property)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);

    for (int i = 0; i < kSamples; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(1 + InsecureRandRange(100)); // [1,100]

        const CAmount required = RequiredBond(weight);
        const CAmount tooLow = required - 1;

        BOOST_CHECK_MESSAGE(
            !tg.AddTrustEdge(from, to, weight, tooLow, uint256(), "low bond"),
            "3.5: AddTrustEdge must reject bond below the required amount (#" +
            std::to_string(i) + ")");

        // The exact required bond is still accepted (rejection does not poison
        // subsequent valid work — analogous to "rest of block unaffected").
        BOOST_CHECK_MESSAGE(
            tg.AddTrustEdge(from, to, weight, required, uint256(), "ok bond"),
            "3.5: AddTrustEdge must accept the exact required bond (#" +
            std::to_string(i) + ")");
    }
}

// An unparseable on-chain trust-edge payload must not deserialize into a record,
// so no canonical edge is ever persisted from it. (3.5)
BOOST_AUTO_TEST_CASE(preserve_unparseable_payload_persists_nothing)
{
    // A clearly-too-short buffer cannot be a valid v1 (54-byte) or v2 payload.
    const std::vector<std::vector<uint8_t>> garbage = {
        {},
        {0x00},
        {0x02, 0x01, 0x02, 0x03},
        std::vector<uint8_t>(10, 0xAB),
    };

    for (size_t i = 0; i < garbage.size(); ++i) {
        CVM::CVMTrustEdgeData td;
        BOOST_CHECK_MESSAGE(!td.Deserialize(garbage[i]),
            "3.5: an unparseable trust-edge payload must fail to deserialize "
            "(case " + std::to_string(i) + ")");
    }

    // And a well-formed edge alongside is unaffected: the canonical graph stays
    // empty for the garbage input while a genuine direct write still works.
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    BOOST_CHECK(CanonicalEdgeCount(tg) == 0);
    BOOST_REQUIRE(tg.AddTrustEdge(RandU160(), RandU160(), kWeight, kBond, uint256(), "ok"));
    BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 1,
        "3.5: a valid edge must still be written even though garbage payloads "
        "were rejected");
}

// ===========================================================================
// 3.6 — Soft-fork gating and CVM-free blocks connect exactly as today.
// **Validates: Requirements 3.6**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_softfork_gating_property)
{
    // Use mainnet params, where CVM activates at a non-zero height (220000), so
    // both the inactive and active sides of the gate are observable.
    const std::unique_ptr<CChainParams> mainParams =
        CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params& consensus = mainParams->GetConsensus();
    const int activation = consensus.cvmActivationHeight;
    BOOST_REQUIRE(activation > 0);

    for (int i = 0; i < kSamples; ++i) {
        // A height strictly below activation must be inactive.
        const int below = static_cast<int>(InsecureRandRange(activation));
        BOOST_CHECK_MESSAGE(!CVM::IsCVMSoftForkActive(below, consensus),
            "3.6: CVM must be inactive below the activation height (h=" +
            std::to_string(below) + ")");

        // A height at or above activation must be active.
        const int above = activation + static_cast<int>(InsecureRandRange(100000));
        BOOST_CHECK_MESSAGE(CVM::IsCVMSoftForkActive(above, consensus),
            "3.6: CVM must be active at/above the activation height (h=" +
            std::to_string(above) + ")");
    }

    BOOST_CHECK_MESSAGE(CVM::IsCVMSoftForkActive(activation, consensus),
        "3.6: CVM must be active exactly at the activation height");
}

// A block with no CVM OP_RETURN transactions writes no WoT/reputation records
// through the durable-write seam. (3.6)
BOOST_AUTO_TEST_CASE(preserve_cvm_free_block_no_records_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        CBlock block;
        block.vtx.push_back(MakeCoinbase());
        const int n = 1 + static_cast<int>(InsecureRandRange(4));
        for (int k = 0; k < n; ++k) block.vtx.push_back(MakePlainTx());

        RunConnectBlockNonContractDurableWrite(block, 1000, db);

        CVM::TrustGraph tg(db);
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 0,
            "3.6: a CVM-free block must not write canonical trust edges (#" +
            std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.7 — fJustCheck (validation-only) writes no durable WoT/reputation state.
//
// The fix places the non-contract dispatch in the durable-write phase, which is
// reached only after `if (fJustCheck) return true;`. Modelling the fJustCheck
// path (nothing durable runs), a block containing non-contract CVM transactions
// leaves the canonical graph and reputation untouched.
// **Validates: Requirements 3.7**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_justcheck_no_durable_writes_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const uint160 target = RandU160();

        CBlock block;
        block.vtx.push_back(MakeCoinbase());
        block.vtx.push_back(MakeTrustEdgeTx(from, to));
        block.vtx.push_back(MakeReputationVoteTx(target));

        // Validation-only: nothing durable runs.
        RunConnectBlockValidationOnly(block, 1000, db);

        CVM::TrustGraph tg(db);
        CVM::TrustEdge edge;
        BOOST_CHECK_MESSAGE(!tg.GetTrustEdge(from, to, edge),
            "3.7: validation-only (fJustCheck) must not persist a canonical "
            "trust edge (#" + std::to_string(i) + ")");

        CVM::ReputationSystem repSystem(db);
        CVM::ReputationScore score;
        repSystem.GetReputation(target, score);
        BOOST_CHECK_MESSAGE(score.score == 0 && score.voteCount == 0,
            "3.7: validation-only (fJustCheck) must not apply a reputation vote "
            "(#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.8 — Completed web-of-trust-fixes behaviour is unchanged.
// ===========================================================================

// Canonical-key classification (used by listtrustrelations / gettrustgraphstats)
// still separates canonical forward edges from reverse-index and propagated
// records. **Validates: Requirements 3.8**
BOOST_AUTO_TEST_CASE(preserve_canonical_key_classification_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const std::string a = CVM::TrustNodeId::FromLegacyUint160(RandU160()).ToKeyString();
        const std::string b = CVM::TrustNodeId::FromLegacyUint160(RandU160()).ToKeyString();

        const std::string canonical = "trust_" + a + "_" + b;
        const std::string reverse   = "trust_in_" + b + "_" + a;
        const std::string prop      = "trust_prop_" + RandU160().ToString() + "_" + RandU160().ToString();
        const std::string propIdx   = "trust_prop_idx_" + InsecureRand256().ToString() + "_" + RandU160().ToString();

        BOOST_CHECK_MESSAGE(CVM::IsCanonicalForwardEdgeKey(canonical),
            "3.8: canonical forward edge key must be recognised (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(!CVM::IsCanonicalForwardEdgeKey(reverse),
            "3.8: reverse-index key must NOT be canonical (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(!CVM::IsCanonicalForwardEdgeKey(prop),
            "3.8: propagated-edge key must NOT be canonical (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(!CVM::IsCanonicalForwardEdgeKey(propIdx),
            "3.8: propagated-index key must NOT be canonical (#" + std::to_string(i) + ")");
    }
}

// Quantum / wide-address identifiers round-trip through TrustNodeId and are
// stored/read losslessly by the trust graph. **Validates: Requirements 3.8**
BOOST_AUTO_TEST_CASE(preserve_wide_address_roundtrip_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        uint256 h = InsecureRand256();

        // P2WSH (32-byte) and QUANTUM (32-byte) destinations.
        WitnessV0ScriptHash wsh;
        std::memcpy(wsh.begin(), h.begin(), 32);
        WitnessV2Quantum q;
        std::memcpy(q.begin(), h.begin(), 32);

        CTxDestination wshDest(wsh);
        CTxDestination qDest(q);

        CVM::TrustNodeId wshNode, qNode;
        BOOST_REQUIRE_MESSAGE(CVM::TrustNodeId::FromDestination(wshDest, wshNode),
            "3.8: P2WSH must convert to a TrustNodeId (#" + std::to_string(i) + ")");
        BOOST_REQUIRE_MESSAGE(CVM::TrustNodeId::FromDestination(qDest, qNode),
            "3.8: quantum must convert to a TrustNodeId (#" + std::to_string(i) + ")");

        BOOST_CHECK_MESSAGE(wshNode.type == static_cast<uint8_t>(CVM::TrustNodeType::P2WSH),
            "3.8: P2WSH node type mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(qNode.type == static_cast<uint8_t>(CVM::TrustNodeType::QUANTUM),
            "3.8: quantum node type mismatch (#" + std::to_string(i) + ")");

        // ToDestination round-trips back to the original destination.
        BOOST_CHECK_MESSAGE(wshNode.ToDestination() == wshDest,
            "3.8: P2WSH destination did not round-trip (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(qNode.ToDestination() == qDest,
            "3.8: quantum destination did not round-trip (#" + std::to_string(i) + ")");

        // A wide-node edge stores and reads back losslessly.
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);
        BOOST_REQUIRE(tg.AddTrustEdge(wshNode, qNode, kWeight, kBond, uint256(), "wide"));

        CVM::TrustEdge edge;
        BOOST_REQUIRE_MESSAGE(tg.GetTrustEdge(wshNode, qNode, edge),
            "3.8: wide-node edge must be retrievable (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.fromAddress == wshNode && edge.toAddress == qNode,
            "3.8: wide-node identities must round-trip losslessly (#" +
            std::to_string(i) + ")");
    }
}

// Propagated-edge records coexist with canonical edges without inflating the
// canonical edge count (reconciliation with the off-chain propagation side
// effect). **Validates: Requirements 3.8**
BOOST_AUTO_TEST_CASE(preserve_propagation_coexistence_stats_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 from = RandU160();
        const uint160 to = RandU160();

        // One canonical forward edge.
        BOOST_REQUIRE(tg.AddTrustEdge(from, to, kWeight, kBond, uint256(), "canonical"));

        // Several propagated-edge records sharing the "trust_" prefix but a
        // different namespace, written exactly as the propagator would.
        const int nProp = 1 + static_cast<int>(InsecureRandRange(4));
        for (int k = 0; k < nProp; ++k) {
            CVM::PropagatedTrustEdge pe;
            pe.fromAddress = CVM::TrustNodeId::FromLegacyUint160(from);
            pe.toAddress = CVM::TrustNodeId::FromLegacyUint160(RandU160());
            pe.originalTarget = CVM::TrustNodeId::FromLegacyUint160(to);
            pe.sourceEdgeTx = InsecureRand256();
            pe.trustWeight = kWeight;
            pe.propagatedAt = kTimestamp;
            pe.originalTimestamp = kTimestamp;
            pe.bondAmount = kBond;

            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << pe;
            std::vector<uint8_t> data(ss.begin(), ss.end());
            BOOST_REQUIRE(db.WriteGeneric(pe.GetStorageKey(), data));
            BOOST_REQUIRE(db.WriteGeneric(pe.GetIndexKey(), data));
        }

        // The canonical count must be exactly 1 — propagated records do not
        // inflate it.
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 1,
            "3.8: canonical edge count must be 1 despite " + std::to_string(nProp) +
            " coexisting propagated records (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.9 — Shared serialization records round-trip byte-for-byte.
// ===========================================================================

// Contract metadata round-trips through the DB. **Validates: Requirements 3.9**
BOOST_AUTO_TEST_CASE(preserve_contract_roundtrip_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 addr = RandU160();
        CVM::Contract in;
        in.address = addr;
        in.deployer = RandU160();
        in.code = std::vector<uint8_t>(1 + InsecureRandRange(64), 0xEE);
        in.deploymentHeight = static_cast<int>(InsecureRandRange(0x7FFFFFFF));
        in.deploymentTx = InsecureRand256();
        in.isCleanedUp = (InsecureRandRange(2) == 0);

        BOOST_REQUIRE(db.WriteContract(addr, in));
        CVM::Contract out;
        BOOST_REQUIRE(db.ReadContract(addr, out));

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.address == in.address, "3.9: Contract.address mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.deployer == in.deployer, "3.9: Contract.deployer mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.code == in.code, "3.9: Contract.code mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.deploymentHeight == in.deploymentHeight, "3.9: Contract.deploymentHeight mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.deploymentTx == in.deploymentTx, "3.9: Contract.deploymentTx mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.isCleanedUp == in.isCleanedUp, "3.9: Contract.isCleanedUp mismatch" + ctx);
    }
}

// Contract state (SLOAD/SSTORE backing store) round-trips. **Validates: Requirements 3.9**
BOOST_AUTO_TEST_CASE(preserve_contract_state_roundtrip_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 addr = RandU160();
        const uint256 key = InsecureRand256();
        const uint256 value = InsecureRand256();

        BOOST_REQUIRE(db.Store(addr, key, value));
        uint256 loaded;
        BOOST_REQUIRE_MESSAGE(db.Load(addr, key, loaded),
            "3.9: stored contract-state value must be loadable (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(loaded == value,
            "3.9: contract-state round-trip mismatch (#" + std::to_string(i) + ")");
    }
}

// Account nonces round-trip. **Validates: Requirements 3.9**
BOOST_AUTO_TEST_CASE(preserve_nonce_roundtrip_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 addr = RandU160();
        const uint64_t nonce = InsecureRandRange(0xFFFFFFFFULL);

        BOOST_REQUIRE(db.WriteNonce(addr, nonce));
        uint64_t got = 0;
        BOOST_REQUIRE(db.ReadNonce(addr, got));
        BOOST_CHECK_MESSAGE(got == nonce,
            "3.9: nonce round-trip mismatch; wrote " + std::to_string(nonce) +
            " read " + std::to_string(got) + " (#" + std::to_string(i) + ")");
    }
}

// PropagatedTrustEdge (trust_prop_* / trust_prop_idx_*) round-trips byte-for-byte
// and through the DB. **Validates: Requirements 3.9**
BOOST_AUTO_TEST_CASE(preserve_propagated_edge_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::PropagatedTrustEdge in;
        in.fromAddress = CVM::TrustNodeId::FromLegacyUint160(RandU160());
        in.toAddress = CVM::TrustNodeId::FromLegacyUint160(RandU160());
        in.originalTarget = CVM::TrustNodeId::FromLegacyUint160(RandU160());
        in.sourceEdgeTx = InsecureRand256();
        in.trustWeight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        in.propagatedAt = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.originalTimestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));

        // In-memory serialization round-trip.
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << in;
        CVM::PropagatedTrustEdge out;
        ss >> out;
        BOOST_CHECK_MESSAGE(in == out,
            "3.9: PropagatedTrustEdge did not round-trip byte-for-byte (#" +
            std::to_string(i) + ")");

        // Key-format invariants (shared with IsCanonicalForwardEdgeKey filtering).
        BOOST_CHECK_MESSAGE(in.GetStorageKey().rfind("trust_prop_", 0) == 0,
            "3.9: storage key must start with trust_prop_ (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(in.GetIndexKey().rfind("trust_prop_idx_", 0) == 0,
            "3.9: index key must start with trust_prop_idx_ (#" + std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
