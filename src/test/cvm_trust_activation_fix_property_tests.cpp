// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Trust System Activation — Fix-Check Property Test Suite (AFTER fix)
 *
 * Spec: .kiro/specs/trust-system-activation  (bugfix)
 * Task 4: "Write fix-check property / functional tests P1–P4".
 *
 * PURPOSE
 * -------
 * These property tests map to the four Bug-Condition Correctness Properties in
 * design.md (P1–P4) and extend/parallel the exploratory bug-condition tests
 * (task 1). They are run AFTER the fix (Alternative B: a slim, non-contract-only
 * dispatch `CVMBlockProcessor::ProcessNonContractBlock` wired into
 * `ConnectBlock`'s durable-write phase) and are EXPECTED TO PASS — passing
 * confirms every in-scope on-chain WoT/reputation record is now persisted on
 * block connect, idempotently and without re-executing contract work.
 *
 * Property map (design.md → Correctness Properties):
 *   P1  Trust edge persisted & traversable on connect          (2.1, 2.2, 2.3)
 *   P2  Reputation / bonded / DAO records persisted on connect  (2.4, 2.5, 2.6, 2.7)
 *   P3  Idempotent, reorg-safe, reconciled with propagation     (2.8)
 *   P4  Bounded-time persistence without contract re-execution  (2.9)
 *
 * TESTABILITY SEAM
 * ----------------
 * The fix places the non-contract dispatch in the durable-write phase of
 * `ConnectBlock` (`src/validation.cpp`), immediately before
 * `ProcessClusterUpdates()`. That entry point is the process-wide static
 * `CVM::CVMBlockProcessor::ProcessNonContractBlock(block, height, db)`. These
 * tests drive that exact production function against an in-memory CVM database —
 * the same seam used by `cvm_trust_activation_explore_tests.cpp` and
 * `cvm_trust_activation_preserve_tests.cpp`. Reorg (disconnect → reconnect) at
 * this seam maps to calling the dispatch twice on the same block (design
 * "Idempotency / reorg handling").
 *
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9
 */

#include <cvm/blockprocessor.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/trustnodeid.h>
#include <cvm/walletcluster.h>
#include <cvm/reputation.h>

#include <amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// DB-backed block-connection cases run a modest number of samples to keep the
// suite fast while still exercising many generated block/tx shapes.
static constexpr int kSamplesDb = 32;

// A bond comfortably above the required bond for every weight used here:
//   required(w) = minBondAmount (1 CAS) + bondPerVotePoint (0.01 CAS) * |w|
// (COIN == 10'000'000 in Cascoin). 3 CAS clears every weight up to 100.
static const CAmount kBond = 3 * COIN;
static const uint32_t kTimestamp = 1700000000;

uint160 RandU160()
{
    uint160 a;
    do {
        uint256 r = InsecureRand256();
        std::memcpy(a.begin(), r.begin(), 20);
    } while (a.IsNull());
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

// P2SH bond output (23 bytes: OP_HASH160 <20> OP_EQUAL) carrying the given
// amount. The bonded handlers require this at vout[1].
CTxOut MakeBondOutput(CAmount amount)
{
    CScript p2sh = CScript() << OP_HASH160 << ToByteVector(RandU160()) << OP_EQUAL;
    return CTxOut(amount, p2sh);
}

// TRUST_EDGE CVM transaction: vout[0] OP_RETURN payload, vout[1] P2SH bond.
CTransactionRef MakeTrustEdgeTx(const uint160& from, const uint160& to,
                                int16_t weight, CAmount bond = kBond)
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
CTransactionRef MakeReputationVoteTx(const uint160& target, int16_t voteValue)
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

// BONDED_VOTE CVM transaction: vout[0] OP_RETURN payload, vout[1] P2SH bond.
CTransactionRef MakeBondedVoteTx(const uint160& voter, const uint160& target,
                                 int16_t voteValue, CAmount bond = kBond)
{
    CVM::CVMBondedVoteData voteData;
    voteData.voter = CVM::TrustNodeId::FromLegacyUint160(voter);
    voteData.target = CVM::TrustNodeId::FromLegacyUint160(target);
    voteData.voteValue = voteValue;
    voteData.bondAmount = bond;
    voteData.timestamp = kTimestamp;

    std::vector<uint8_t> data = voteData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::BONDED_VOTE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(2);
    mtx.vout[0] = CTxOut(0, opret);
    mtx.vout[1] = MakeBondOutput(bond);
    return MakeTransactionRef(std::move(mtx));
}

// DAO_DISPUTE CVM transaction: vout[0] OP_RETURN payload, vout[1] challenge bond.
CTransactionRef MakeDAODisputeTx(const uint256& originalVoteTx,
                                 const uint160& challenger, CAmount bond = kBond)
{
    CVM::CVMDAODisputeData disputeData;
    disputeData.originalVoteTxHash = originalVoteTx;
    disputeData.challenger = CVM::TrustNodeId::FromLegacyUint160(challenger);
    disputeData.challengeBond = bond;
    disputeData.reason = "challenge";
    disputeData.timestamp = kTimestamp;

    std::vector<uint8_t> data = disputeData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::DAO_DISPUTE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(2);
    mtx.vout[0] = CTxOut(0, opret);
    mtx.vout[1] = MakeBondOutput(bond);
    return MakeTransactionRef(std::move(mtx));
}

// DAO_VOTE CVM transaction (no bond output required by the handler).
CTransactionRef MakeDAOVoteTx(const uint256& disputeId, const uint160& daoMember,
                              bool supportSlash, CAmount stake)
{
    CVM::CVMDAOVoteData voteData;
    voteData.disputeId = disputeId;
    voteData.daoMember = CVM::TrustNodeId::FromLegacyUint160(daoMember);
    voteData.supportSlash = supportSlash;
    voteData.stake = stake;
    voteData.timestamp = kTimestamp;

    std::vector<uint8_t> data = voteData.Serialize();
    CScript opret = CVM::BuildCVMOpReturn(CVM::CVMOpType::DAO_VOTE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, opret);
    return MakeTransactionRef(std::move(mtx));
}

// CONTRACT_DEPLOY CVM transaction carrying real (non-empty) bytecode. If the
// non-contract dispatch were to (wrongly) re-execute contract work, this would
// deploy a contract; ProcessNonContractBlock must leave it untouched.
CTransactionRef MakeContractDeployTx()
{
    CVM::CVMDeployData deployData;
    deployData.bytecode = std::vector<uint8_t>{0x00, 0x01, 0x02, 0x03};
    deployData.codeHash = Hash(deployData.bytecode.begin(), deployData.bytecode.end());
    deployData.gasLimit = 100000;

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

// Build a single-transaction block carrying the given CVM transaction (plus a
// coinbase) and connect it through the production non-contract dispatch.
void ConnectSingleTxBlock(const CTransactionRef& tx, int height, CVM::CVMDatabase& db)
{
    CBlock block;
    block.vtx.push_back(MakeCoinbase());
    block.vtx.push_back(tx);
    CVM::CVMBlockProcessor::ProcessNonContractBlock(block, height, db);
}

// Canonical forward edge count from the graph statistics.
uint64_t CanonicalEdgeCount(CVM::TrustGraph& tg)
{
    std::map<std::string, uint64_t> stats = tg.GetGraphStats();
    auto it = stats.find("total_trust_edges");
    return it != stats.end() ? it->second : 0;
}

// Make an address satisfy TrustGraph::IsDAOMember: reputation >= 70, >= 100 CAS
// bonded via a non-slashed outgoing edge, and a recent DAO-activity record.
void SetupDAOMember(CVM::CVMDatabase& db, const uint160& member)
{
    CVM::ReputationSystem repSystem(db);
    CVM::ReputationScore score;
    score.score = 80; // >= 70
    score.voteCount = 1;
    score.lastUpdated = kTimestamp;
    repSystem.UpdateReputation(member, score);

    CVM::TrustGraph tg(db);
    // 100 CAS bonded (>= MIN_DAO_STAKE) via a valid outgoing trust edge.
    BOOST_REQUIRE(tg.AddTrustEdge(member, RandU160(), 50, 100 * COIN, uint256(), "stake"));

    // Recent-activity marker (block 0); chainActive.Height() in the unit
    // environment keeps this within the 10,000-block activity window.
    int lastActivityBlock = 0;
    std::vector<uint8_t> activity(4);
    std::memcpy(activity.data(), &lastActivityBlock, 4);
    db.WriteGeneric("dao_activity_" + member.ToString(), activity);
}

// Fixture: regtest params + a fresh in-memory CVM database per test.
struct TrustActivationFixSetup : public BasicTestingSetup {
    TrustActivationFixSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~TrustActivationFixSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_trust_activation_fix_property_tests, TrustActivationFixSetup)

// ===========================================================================
// P1 — Trust edge persisted & traversable on connect.
//
// **Property 2: Expected Behavior**
//
// For any block carrying a TRUST_EDGE CVM transaction, connecting it via the
// non-contract dispatch persists a canonical trust_<from>_<to> edge (signer as
// `from`, payload target as `to`, with the payload weight/bond/bond-tx) that
// GetTrustEdge reads back and gettrustgraphstats counts.
//
// **Validates: Requirements 2.1, 2.2**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p1_trust_edge_persisted_on_connect_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]

        CTransactionRef tx = MakeTrustEdgeTx(from, to, weight);
        ConnectSingleTxBlock(tx, 1000, db);

        // 2.1 — canonical forward edge persisted with the payload's fields.
        CVM::TrustGraph tg(db);
        CVM::TrustEdge edge;
        BOOST_REQUIRE_MESSAGE(tg.GetTrustEdge(from, to, edge),
            "P1 (2.1): no canonical trust_<from>_<to> edge persisted after "
            "connecting a TRUST_EDGE tx (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.trustWeight == weight,
            "P1 (2.1): persisted weight " + std::to_string(edge.trustWeight) +
            " != payload weight " + std::to_string(weight) + " (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.bondAmount == kBond,
            "P1 (2.1): persisted bond mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.bondTxHash == tx->GetHash(),
            "P1 (2.1): persisted bond-tx hash must be the on-chain tx hash (#" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(!edge.slashed,
            "P1 (2.1): a freshly persisted edge must not be slashed (#" +
            std::to_string(i) + ")");

        // 2.2 — graph statistics count exactly the one canonical forward edge.
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 1,
            "P1 (2.2): gettrustgraphstats total_trust_edges = " +
            std::to_string(CanonicalEdgeCount(tg)) + " (expected 1) (#" +
            std::to_string(i) + ")");
    }
}

// The on-chain edge enumerates and traverses EXACTLY as an addtrust edge: an
// A→B→C→D chain built only with sendtrustrelation (modelled by connecting one
// TRUST_EDGE tx per hop) yields paths_found >= 1 and a non-zero weighted
// reputation EQUAL to the addtrust-built graph. **Validates: Requirements 2.2, 2.3**
BOOST_AUTO_TEST_CASE(p1_onchain_chain_equals_addtrust_property)
{
    for (int s = 0; s < kSamplesDb; ++s) {
        // Four identities and three hop weights shared by both graphs.
        std::vector<uint160> node(4);
        for (int i = 0; i < 4; ++i) node[i] = RandU160();
        int16_t w[3];
        for (int i = 0; i < 3; ++i) w[i] = static_cast<int16_t>(10 + InsecureRandRange(91));

        // --- On-chain graph: one TRUST_EDGE tx per hop, connected on block. ---
        CVM::CVMDatabase onchainDb(fs::temp_directory_path() / fs::unique_path(),
                                   1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        for (int i = 0; i < 3; ++i) {
            ConnectSingleTxBlock(MakeTrustEdgeTx(node[i], node[i + 1], w[i]),
                                 1000 + i, onchainDb);
        }

        // --- addtrust graph: same edges written directly. ---
        CVM::CVMDatabase addtrustDb(fs::temp_directory_path() / fs::unique_path(),
                                    1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        {
            CVM::TrustGraph tg(addtrustDb);
            for (int i = 0; i < 3; ++i) {
                BOOST_REQUIRE(tg.AddTrustEdge(node[i], node[i + 1], w[i], kBond,
                                              uint256(), ""));
            }
        }

        CVM::TrustGraph onchainTg(onchainDb);
        CVM::TrustGraph addtrustTg(addtrustDb);

        // 2.2 — enumeration counts agree (both graphs have three canonical edges).
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(onchainTg) == 3,
            "P1 (2.2): on-chain chain must persist 3 canonical edges; got " +
            std::to_string(CanonicalEdgeCount(onchainTg)) + " (sample " +
            std::to_string(s) + ")");
        BOOST_CHECK_MESSAGE(
            CanonicalEdgeCount(onchainTg) == CanonicalEdgeCount(addtrustTg),
            "P1 (2.2): on-chain vs addtrust canonical edge count differs (sample " +
            std::to_string(s) + ")");

        // 2.3 — A→...→D traversable in the on-chain graph.
        std::vector<CVM::TrustPath> paths = onchainTg.FindTrustPaths(node[0], node[3], 3);
        BOOST_CHECK_MESSAGE(paths.size() >= 1,
            "P1 (2.3): on-chain A→B→C→D chain must yield paths_found >= 1 (sample " +
            std::to_string(s) + ")");

        // 2.3 — non-zero weighted reputation, EQUAL to the addtrust-built graph.
        const double onchainRep = onchainTg.GetWeightedReputation(node[0], node[3], 3);
        const double addtrustRep = addtrustTg.GetWeightedReputation(node[0], node[3], 3);
        BOOST_CHECK_MESSAGE(onchainRep != 0.0,
            "P1 (2.3): on-chain weighted reputation must be non-zero (sample " +
            std::to_string(s) + ")");
        BOOST_CHECK_MESSAGE(onchainRep == addtrustRep,
            "P1 (2.3): on-chain weighted reputation (" + std::to_string(onchainRep) +
            ") must equal the addtrust-built result (" + std::to_string(addtrustRep) +
            ") (sample " + std::to_string(s) + ")");
    }
}

// ===========================================================================
// P2 — Reputation / bonded / DAO records persisted on connect.
//
// **Property 2: Expected Behavior**
//
// Connecting a block invokes each non-contract handler: a REPUTATION_VOTE
// updates the target's reputation; a valid BONDED_VOTE persists a bonded-vote
// record; a valid DAO_DISPUTE persists a listable/queryable dispute; and a valid
// DAO_VOTE records the vote and resolves the dispute at the existing threshold.
//
// **Validates: Requirements 2.4, 2.5, 2.6, 2.7**
// ===========================================================================

// 2.4 — reputation vote applied on connect.
BOOST_AUTO_TEST_CASE(p2_reputation_vote_applied_on_connect_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 target = RandU160();
        const int16_t voteValue = static_cast<int16_t>(1 + InsecureRandRange(100)); // [1,100]

        CVM::ReputationSystem repSystem(db);
        CVM::ReputationScore before;
        repSystem.GetReputation(target, before);

        ConnectSingleTxBlock(MakeReputationVoteTx(target, voteValue), 1000, db);

        CVM::ReputationScore after;
        repSystem.GetReputation(target, after);
        BOOST_CHECK_MESSAGE(after.score == before.score + voteValue,
            "P2 (2.4): reputation score " + std::to_string(after.score) +
            " != expected " + std::to_string(before.score + voteValue) + " (#" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(after.voteCount == before.voteCount + 1,
            "P2 (2.4): reputation vote count not incremented (#" + std::to_string(i) + ")");
    }
}

// 2.5 — bonded vote persisted on connect (record keyed by the bond-tx hash).
BOOST_AUTO_TEST_CASE(p2_bonded_vote_persisted_on_connect_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 voter = RandU160();
        const uint160 target = RandU160();
        const int16_t voteValue = static_cast<int16_t>(1 + InsecureRandRange(100));

        CTransactionRef tx = MakeBondedVoteTx(voter, target, voteValue);
        ConnectSingleTxBlock(tx, 1000, db);

        // RecordBondedVote persists under "vote_<bondTxHash>".
        const std::string voteKey = "vote_" + tx->GetHash().ToString();
        BOOST_CHECK_MESSAGE(db.ExistsGeneric(voteKey),
            "P2 (2.5): no bonded-vote record persisted for the mined BONDED_VOTE "
            "tx (#" + std::to_string(i) + ")");

        // And it is counted in the graph statistics (bonded votes are counted
        // under "total_votes", keyed by "vote_<bondTxHash>").
        CVM::TrustGraph tg(db);
        std::map<std::string, uint64_t> stats = tg.GetGraphStats();
        BOOST_CHECK_MESSAGE(stats["total_votes"] >= 1,
            "P2 (2.5): total_votes not incremented (#" + std::to_string(i) + ")");
    }
}

// 2.6 — DAO dispute persisted on connect (listable/queryable via GetDispute).
BOOST_AUTO_TEST_CASE(p2_dao_dispute_persisted_on_connect_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint256 originalVoteTx = InsecureRand256();
        const uint160 challenger = RandU160();

        CTransactionRef tx = MakeDAODisputeTx(originalVoteTx, challenger);
        ConnectSingleTxBlock(tx, 1000, db);

        // CreateDispute keys the dispute by the dispute tx hash.
        CVM::TrustGraph tg(db);
        CVM::DAODispute dispute;
        BOOST_REQUIRE_MESSAGE(tg.GetDispute(tx->GetHash(), dispute),
            "P2 (2.6): no dispute record persisted for the mined DAO_DISPUTE tx "
            "(#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(dispute.originalVoteTx == originalVoteTx,
            "P2 (2.6): persisted dispute references the wrong original vote (#" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(!dispute.resolved,
            "P2 (2.6): a freshly created dispute must be unresolved (#" +
            std::to_string(i) + ")");
    }
}

// 2.7 — DAO votes recorded and dispute resolved at the existing threshold
// (minDAOVotesForResolution == 5). The dispute and its five DAO votes are
// carried in one block; ProcessNonContractBlock processes them in vtx order so
// the dispute exists before the votes are applied, and the fifth vote crosses
// the resolution threshold.
BOOST_AUTO_TEST_CASE(p2_dao_vote_records_and_resolves_property)
{
    for (int s = 0; s < 8; ++s) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        // Five qualifying DAO members.
        std::vector<uint160> members(5);
        for (int i = 0; i < 5; ++i) {
            members[i] = RandU160();
            SetupDAOMember(db, members[i]);
        }

        // Dispute transaction (its hash becomes the dispute ID).
        const uint256 originalVoteTx = InsecureRand256();
        CTransactionRef disputeTx = MakeDAODisputeTx(originalVoteTx, members[0]);
        const uint256 disputeId = disputeTx->GetHash();

        // One block: the dispute first, then five DAO votes referencing it.
        CBlock block;
        block.vtx.push_back(MakeCoinbase());
        block.vtx.push_back(disputeTx);
        for (int i = 0; i < 5; ++i) {
            block.vtx.push_back(MakeDAOVoteTx(disputeId, members[i], /*supportSlash=*/true,
                                              2 * COIN));
        }

        CVM::CVMBlockProcessor::ProcessNonContractBlock(block, 1000, db);

        CVM::TrustGraph tg(db);
        CVM::DAODispute dispute;
        BOOST_REQUIRE_MESSAGE(tg.GetDispute(disputeId, dispute),
            "P2 (2.7): dispute must exist after connecting the block (sample " +
            std::to_string(s) + ")");

        // 2.7 — all five DAO votes recorded.
        BOOST_CHECK_MESSAGE(dispute.daoVotes.size() == 5,
            "P2 (2.7): expected 5 recorded DAO votes, got " +
            std::to_string(dispute.daoVotes.size()) + " (sample " +
            std::to_string(s) + ")");

        // 2.7 — dispute resolved once the threshold (5 votes) is met.
        BOOST_CHECK_MESSAGE(dispute.resolved,
            "P2 (2.7): dispute must be resolved once the DAO-vote threshold is met "
            "(sample " + std::to_string(s) + ")");
    }
}

// ===========================================================================
// P3 — Idempotent, reorg-safe persistence reconciled with off-chain propagation.
//
// **Property 2: Expected Behavior**
//
// Connecting → disconnecting → reconnecting a block (modelled at this seam by
// running the dispatch twice on the same block) persists each canonical record
// exactly once: no duplicate edges, no double-counted reputation. Coexisting
// off-chain trust_prop_* records do NOT inflate the canonical edge count.
//
// **Validates: Requirements 2.8**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p3_reorg_idempotent_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const uint160 repTarget = RandU160();
        const uint160 voter = RandU160();
        const int16_t voteValue = static_cast<int16_t>(1 + InsecureRandRange(100));

        CBlock block;
        block.vtx.push_back(MakeCoinbase());
        block.vtx.push_back(MakeTrustEdgeTx(from, to, 80));
        block.vtx.push_back(MakeReputationVoteTx(repTarget, voteValue));
        block.vtx.push_back(MakeBondedVoteTx(voter, repTarget, voteValue));

        // Connect → disconnect → reconnect == process the same block twice.
        CVM::CVMBlockProcessor::ProcessNonContractBlock(block, 1000, db);
        CVM::CVMBlockProcessor::ProcessNonContractBlock(block, 1000, db);

        CVM::TrustGraph tg(db);

        // Exactly one canonical trust edge (upsert, not duplicate).
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 1,
            "P3 (2.8): reconnecting the block inflated the canonical edge count to " +
            std::to_string(CanonicalEdgeCount(tg)) + " (expected 1) (#" +
            std::to_string(i) + ")");

        // Reputation applied exactly once (idempotency marker prevents a second
        // increment on reconnect).
        CVM::ReputationSystem repSystem(db);
        CVM::ReputationScore score;
        repSystem.GetReputation(repTarget, score);
        BOOST_CHECK_MESSAGE(score.score == voteValue,
            "P3 (2.8): reputation double-counted on reconnect: score " +
            std::to_string(score.score) + " != " + std::to_string(voteValue) + " (#" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(score.voteCount == 1,
            "P3 (2.8): reputation vote count double-counted on reconnect: " +
            std::to_string(score.voteCount) + " != 1 (#" + std::to_string(i) + ")");
    }
}

// Coexisting off-chain propagated records (trust_prop_* / trust_prop_idx_*) must
// NOT be counted as canonical edges: the on-chain canonical record agrees with,
// and does not double-count against, the off-chain propagation side effect.
// **Validates: Requirements 2.8**
BOOST_AUTO_TEST_CASE(p3_propagation_coexistence_not_double_counted_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        const uint160 from = RandU160();
        const uint160 to = RandU160();

        // Off-chain side effect first (as the broadcasting RPC would produce it):
        // write only trust_prop_* / trust_prop_idx_* records for the same edge.
        {
            CVM::TrustGraph tg(db);
            CVM::WalletClusterer clusterer(db);
            CVM::TrustPropagator propagator(db, clusterer, tg);

            CVM::TrustEdge edge;
            edge.fromAddress = CVM::TrustNodeId::FromLegacyUint160(from);
            edge.toAddress = CVM::TrustNodeId::FromLegacyUint160(to);
            edge.trustWeight = 80;
            edge.timestamp = kTimestamp;
            edge.bondAmount = kBond;
            edge.bondTxHash = uint256();
            edge.slashed = false;
            edge.reason = "propagated";
            propagator.PropagateTrustEdge(edge);
        }

        // Now connect the on-chain TRUST_EDGE for the same relationship.
        ConnectSingleTxBlock(MakeTrustEdgeTx(from, to, 80), 1000, db);

        // Exactly one canonical edge; the propagated records are filtered out of
        // canonical enumeration (IsCanonicalForwardEdgeKey).
        CVM::TrustGraph tg(db);
        BOOST_CHECK_MESSAGE(CanonicalEdgeCount(tg) == 1,
            "P3 (2.8): canonical edge count = " + std::to_string(CanonicalEdgeCount(tg)) +
            " (expected 1); coexisting trust_prop_* records must not inflate it (#" +
            std::to_string(i) + ")");

        CVM::TrustEdge edge;
        BOOST_CHECK_MESSAGE(tg.GetTrustEdge(from, to, edge),
            "P3 (2.8): the canonical on-chain edge must be present alongside the "
            "propagated records (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// P4 — Bounded-time persistence without contract re-execution.
//
// **Property 2: Expected Behavior**
//
// A block mixing contract (CONTRACT_DEPLOY / CONTRACT_CALL) and non-contract
// CVM transactions connects in bounded time (no hang), the non-contract records
// are persisted, and the non-contract dispatch does NOT re-execute contract
// deploy/call work (no contract is deployed by this path).
//
// **Validates: Requirements 2.9**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p4_bounded_time_no_contract_reexecution_property)
{
    CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                        1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    const uint160 from = RandU160();
    const uint160 to = RandU160();
    const uint160 repTarget = RandU160();

    // A block mixing many contract and non-contract CVM transactions.
    CBlock block;
    block.vtx.push_back(MakeCoinbase());
    for (int i = 0; i < 100; ++i) {
        block.vtx.push_back(MakeContractDeployTx());
        block.vtx.push_back(MakeContractCallTx());
        block.vtx.push_back(MakeTrustEdgeTx(RandU160(), RandU160(), 80));
        block.vtx.push_back(MakeReputationVoteTx(RandU160(), 50));
    }
    // A known non-contract pair to assert persistence afterward.
    block.vtx.push_back(MakeTrustEdgeTx(from, to, 80));
    block.vtx.push_back(MakeReputationVoteTx(repTarget, 42));

    const auto start = std::chrono::steady_clock::now();
    CVM::CVMBlockProcessor::ProcessNonContractBlock(block, 1000, db);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // 2.9 — bounded time (no hang) over a large mixed block.
    BOOST_CHECK_MESSAGE(elapsedMs < 10000,
        "P4 (2.9): connecting a mixed contract/non-contract block must complete in "
        "bounded time; took " + std::to_string(elapsedMs) + " ms");

    // 2.9 — the non-contract records were persisted.
    CVM::TrustGraph tg(db);
    CVM::TrustEdge edge;
    BOOST_CHECK_MESSAGE(tg.GetTrustEdge(from, to, edge),
        "P4 (2.9): the non-contract trust edge must be persisted even in a mixed block");

    CVM::ReputationSystem repSystem(db);
    CVM::ReputationScore score;
    repSystem.GetReputation(repTarget, score);
    BOOST_CHECK_MESSAGE(score.score == 42,
        "P4 (2.9): the non-contract reputation vote must be applied in a mixed block");

    // 2.9 — NO contract was deployed by the non-contract dispatch: contract
    // deploy/call work is BlockValidator's job and is not re-executed here.
    BOOST_CHECK_MESSAGE(db.ListContracts().empty(),
        "P4 (2.9): the non-contract dispatch must NOT deploy contracts (found " +
        std::to_string(db.ListContracts().size()) + ")");
}

BOOST_AUTO_TEST_SUITE_END()
