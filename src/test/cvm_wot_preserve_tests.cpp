// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Web-of-Trust Fixes — Preservation Test Suite
 *
 * Spec: .kiro/specs/web-of-trust-fixes  (bugfix)
 * Task 10: "Write preservation property tests for shared serialization and
 *           legacy round-trips".
 *
 * PURPOSE
 * -------
 * Property 5 / Property 6 (Preservation): for inputs where the bug condition
 * does NOT hold, the fixed code produces the SAME result as the original code.
 * Following the observation-first methodology (design.md → Correctness
 * Properties 5 & 6), these tests capture the behaviour that MUST remain
 * unchanged after the WoT fixes and are EXPECTED TO PASS both on the baseline
 * and after the fixes land (no regressions).
 *
 * A test that FAILS here would mean a fix altered behaviour it was supposed to
 * preserve.
 *
 * Captured behaviours, mapped to Unchanged-Behavior clauses (3.x):
 *   3.3  Legacy P2PKH edges still store/list/traverse identically.
 *   3.7  PropagatedTrustEdge / BondedVote / DAODispute / reputation records
 *        still round-trip byte-for-byte (shared serialization infrastructure).
 *   3.1  Out-of-range trust weight is still rejected.
 *   3.2  Insufficient bond is still rejected.
 *   3.6  maxdepth outside 1..10 is still rejected with the same message.
 *   3.4  Traversal still skips low-weight (< 10), slashed, and visited (cycle)
 *        edges.
 *   3.5  viewer == target still returns the average of non-slashed incoming
 *        trust.
 *   3.9  Existing quantum handling (DecodeDestination / IsQuantumAddress /
 *        EncodeDestination) and legacy v1 on-chain CVMTrustEdgeData decode
 *        unchanged.
 *   3.8  Cluster propagation fields (edges_propagated and propagated-edge
 *        contents) unchanged for unaffected (legacy P2PKH) inputs.
 *   3.10 The same address representation is used on write and on read for every
 *        supported type.
 *
 * Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10
 */

#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/trustnodeid.h>
#include <cvm/softfork.h>
#include <cvm/reputation.h>
#include <cvm/walletcluster.h>
#include <cvm/cvmdb.h>

#include <address_quantum.h>
#include <base58.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <key.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <streams.h>
#include <univalue.h>
#include <uint256.h>
#include <amount.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Forward declarations of the (non-static) WoT RPC handlers defined in
// rpc/cvm.cpp. They are not exported via a header, so we declare them here to
// invoke them directly at the unit seam (same approach as the exploration
// suite cvm_wot_fix_explore_tests.cpp).
// ---------------------------------------------------------------------------
UniValue addtrust(const JSONRPCRequest& request);
UniValue getweightedreputation(const JSONRPCRequest& request);

namespace {

// Property sample count. Serialization round-trips are cheap; DB-backed
// traversal cases run fewer samples to keep the suite fast.
static constexpr int kSamples = 128;
static constexpr int kSamplesDb = 48;

// A bond comfortably above the required bond for the weights used here:
//   required(w) = minBondAmount (1 CAS) + bondPerVotePoint (0.01 CAS) * |w|
// (COIN == 10'000'000 in Cascoin). 3 CAS clears every weight up to 100.
static const CAmount kBond = 3 * COIN;

// Required bond mirror of TrustGraph::CalculateRequiredBond, used to build the
// insufficient-bond boundary case (3.2).
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

// Encode a uint160 as a legacy base58 P2PKH address (regtest: m.../n...).
std::string P2PKH(const uint160& a)
{
    return EncodeDestination(CKeyID(a));
}

// Build a fresh, well-formed request carrying the given positional params.
JSONRPCRequest MakeRequest(const UniValue& params)
{
    JSONRPCRequest request;
    request.params = params;
    request.fHelp = false;
    return request;
}

UniValue Arr(const std::vector<UniValue>& items)
{
    UniValue arr(UniValue::VARR);
    for (const auto& it : items) arr.push_back(it);
    return arr;
}

std::string RpcErrMessage(const UniValue& e)
{
    if (e.isObject() && e.exists("message") && e["message"].isStr()) {
        return e["message"].get_str();
    }
    return "";
}

// Write a TrustEdge directly under the canonical v2 forward + reverse-index
// keys, exactly as TrustGraph::AddTrustEdge does internally
// ("trust_<from>_<to>" and "trust_in_<to>_<from>", segments from
// TrustNodeId::ToKeyString). This is NOT a mock: it exercises the real
// serialization and the real DB read paths. It is needed to plant records
// whose `slashed` flag is set (AddTrustEdge always writes slashed=false), so
// the traversal/self-view filters can be observed on genuine stored records.
void WriteRawEdge(CVM::CVMDatabase& db, const CVM::TrustEdge& edge)
{
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << edge;
    std::vector<uint8_t> data(ss.begin(), ss.end());

    const std::string fwd = "trust_" + edge.fromAddress.ToKeyString() +
                            "_" + edge.toAddress.ToKeyString();
    const std::string rev = "trust_in_" + edge.toAddress.ToKeyString() +
                            "_" + edge.fromAddress.ToKeyString();
    BOOST_REQUIRE(db.WriteGeneric(fwd, data));
    BOOST_REQUIRE(db.WriteGeneric(rev, data));
}

// Construct a TrustEdge (in-memory v2) with the given legacy P2PKH endpoints.
CVM::TrustEdge MakeEdge(const uint160& from, const uint160& to, int16_t weight,
                        bool slashed, const std::string& reason)
{
    CVM::TrustEdge e;
    e.fromAddress = CVM::TrustNodeId::FromLegacyUint160(from);
    e.toAddress = CVM::TrustNodeId::FromLegacyUint160(to);
    e.trustWeight = weight;
    e.timestamp = 1700000000;
    e.bondAmount = kBond;
    e.bondTxHash = uint256();
    e.slashed = slashed;
    e.reason = reason;
    return e;
}

// Fixture: regtest params (so P2WSH is "rcas1..." and quantum is "rcasq1...")
// plus a fresh in-memory CVM database per test.
struct WoTPreserveSetup : public BasicTestingSetup {
    WoTPreserveSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~WoTPreserveSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_wot_preserve_tests, WoTPreserveSetup)

// ===========================================================================
// 3.3 — Legacy P2PKH edges store/list/traverse identically.
//
// A legacy uint160 (P2PKH) edge written via the uint160 overload must read back
// with every field intact, appear in GetOutgoingTrust, and be found by
// FindTrustPaths. This is the "path that works today must keep working".
// **Validates: Requirements 3.3**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_legacy_p2pkh_edge_roundtrip_property)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);

    for (int i = 0; i < kSamplesDb; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10, 100]
        const std::string reason = "edge-" + std::to_string(i);

        BOOST_REQUIRE_MESSAGE(
            tg.AddTrustEdge(from, to, weight, kBond, uint256(), reason),
            "3.3: legacy AddTrustEdge should succeed (#" + std::to_string(i) + ")");

        // GetTrustEdge round-trip: every field intact.
        CVM::TrustEdge edge;
        BOOST_REQUIRE_MESSAGE(tg.GetTrustEdge(from, to, edge),
            "3.3: stored legacy edge must be retrievable (#" + std::to_string(i) + ")");

        BOOST_CHECK_MESSAGE(edge.trustWeight == weight,
            "3.3: weight round-trip mismatch (#" + std::to_string(i) + "): got " +
            std::to_string(edge.trustWeight));
        BOOST_CHECK_MESSAGE(edge.bondAmount == kBond,
            "3.3: bond round-trip mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.reason == reason,
            "3.3: reason round-trip mismatch (#" + std::to_string(i) + "): \"" +
            edge.reason + "\"");
        BOOST_CHECK_MESSAGE(!edge.slashed,
            "3.3: a freshly added edge must not be slashed (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.fromAddress == CVM::TrustNodeId::FromLegacyUint160(from),
            "3.3: from identity mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.toAddress == CVM::TrustNodeId::FromLegacyUint160(to),
            "3.3: to identity mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(edge.timestamp != 0,
            "3.3: timestamp must be populated (#" + std::to_string(i) + ")");

        // Enumeration: the edge appears in outgoing trust for `from`.
        std::vector<CVM::TrustEdge> outgoing = tg.GetOutgoingTrust(from);
        bool foundOutgoing = false;
        for (const auto& e : outgoing) {
            if (e.toAddress == CVM::TrustNodeId::FromLegacyUint160(to)) foundOutgoing = true;
        }
        BOOST_CHECK_MESSAGE(foundOutgoing,
            "3.3: legacy edge not returned by GetOutgoingTrust (#" + std::to_string(i) + ")");

        // Traversal: a direct path from `from` to `to` is found.
        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(from, to, 3);
        BOOST_CHECK_MESSAGE(paths.size() >= 1,
            "3.3: direct legacy path from->to not found (#" + std::to_string(i) + ")");
    }
}

// Legacy multi-hop chains still traverse. **Validates: Requirements 3.3**
BOOST_AUTO_TEST_CASE(preserve_legacy_p2pkh_chain_traversal_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        // Fresh graph per sample to keep the search space small/deterministic.
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 A = RandU160();
        const uint160 B = RandU160();
        const uint160 C = RandU160();
        const int16_t w = static_cast<int16_t>(50 + InsecureRandRange(51)); // [50,100]

        BOOST_REQUIRE(tg.AddTrustEdge(A, B, w, kBond, uint256(), "A->B"));
        BOOST_REQUIRE(tg.AddTrustEdge(B, C, w, kBond, uint256(), "B->C"));

        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(A, C, 3);
        BOOST_CHECK_MESSAGE(paths.size() >= 1,
            "3.3: 2-hop legacy chain A->B->C not traversed (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.7 — Shared serialization records still round-trip byte-for-byte.
//
// The corruption fix filters foreign records in the *enumerators*; it must NOT
// change the serialization of the shared record types. PropagatedTrustEdge,
// BondedVote, DAODispute, and ReputationScore must all serialize/deserialize
// unchanged.
// **Validates: Requirements 3.7**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_propagated_edge_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::PropagatedTrustEdge in;
        in.fromAddress = RandU160();
        in.toAddress = RandU160();
        in.originalTarget = RandU160();
        in.sourceEdgeTx = InsecureRand256();
        in.trustWeight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        in.propagatedAt = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.originalTimestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));

        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << in;
        CVM::PropagatedTrustEdge out;
        ss >> out;

        BOOST_CHECK_MESSAGE(in == out,
            "3.7: PropagatedTrustEdge did not round-trip byte-for-byte (#" +
            std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_CASE(preserve_bonded_vote_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::BondedVote in;
        in.voter = RandU160();
        in.target = RandU160();
        in.voteValue = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        in.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        in.bondTxHash = InsecureRand256();
        in.timestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.slashed = (InsecureRandRange(2) == 0);
        in.slashTxHash = InsecureRand256();
        in.reason = "vote-reason-" + std::to_string(i);

        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << in;
        CVM::BondedVote out;
        ss >> out;

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.voter == in.voter, "3.7: BondedVote.voter mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.target == in.target, "3.7: BondedVote.target mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.voteValue == in.voteValue, "3.7: BondedVote.voteValue mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.bondAmount == in.bondAmount, "3.7: BondedVote.bondAmount mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.bondTxHash == in.bondTxHash, "3.7: BondedVote.bondTxHash mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.timestamp == in.timestamp, "3.7: BondedVote.timestamp mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.slashed == in.slashed, "3.7: BondedVote.slashed mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.slashTxHash == in.slashTxHash, "3.7: BondedVote.slashTxHash mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.reason == in.reason, "3.7: BondedVote.reason mismatch" + ctx);
    }
}

BOOST_AUTO_TEST_CASE(preserve_dao_dispute_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::DAODispute in;
        in.disputeId = InsecureRand256();
        in.originalVoteTx = InsecureRand256();
        in.challenger = RandU160();
        in.challengeBond = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        in.challengeReason = "dispute-" + std::to_string(i);
        in.createdTime = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        // A couple of DAO votes/stakes so the map serialization is exercised.
        const int nVotes = 1 + static_cast<int>(InsecureRandRange(3));
        for (int v = 0; v < nVotes; ++v) {
            uint160 member = RandU160();
            in.daoVotes[member] = (InsecureRandRange(2) == 0);
            in.daoStakes[member] = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFULL));
        }
        in.resolved = (InsecureRandRange(2) == 0);
        in.slashDecision = (InsecureRandRange(2) == 0);
        in.resolvedTime = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.commitPhaseStart = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.revealPhaseStart = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.useCommitReveal = (InsecureRandRange(2) == 0);
        in.rewardsDistributed = (InsecureRandRange(2) == 0);
        in.rewardDistributionId = InsecureRand256();

        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << in;
        CVM::DAODispute out;
        ss >> out;

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.disputeId == in.disputeId, "3.7: DAODispute.disputeId mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.originalVoteTx == in.originalVoteTx, "3.7: DAODispute.originalVoteTx mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.challenger == in.challenger, "3.7: DAODispute.challenger mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.challengeBond == in.challengeBond, "3.7: DAODispute.challengeBond mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.challengeReason == in.challengeReason, "3.7: DAODispute.challengeReason mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.daoVotes == in.daoVotes, "3.7: DAODispute.daoVotes mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.daoStakes == in.daoStakes, "3.7: DAODispute.daoStakes mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.resolved == in.resolved, "3.7: DAODispute.resolved mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.useCommitReveal == in.useCommitReveal, "3.7: DAODispute.useCommitReveal mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.rewardDistributionId == in.rewardDistributionId, "3.7: DAODispute.rewardDistributionId mismatch" + ctx);
    }
}

BOOST_AUTO_TEST_CASE(preserve_reputation_record_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::ReputationScore in;
        in.address = RandU160();
        in.score = static_cast<int64_t>(InsecureRandRange(20001)) - 10000; // [-10000, 10000]
        in.voteCount = InsecureRandRange(0xFFFFFFFFULL);
        in.lastUpdated = static_cast<int64_t>(InsecureRandRange(0xFFFFFFFFULL));
        in.category = (i % 2 == 0) ? "normal" : "exchange";
        in.totalTransactions = InsecureRandRange(0xFFFFFFFFULL);
        in.totalVolume = InsecureRandRange(0xFFFFFFFFULL);
        in.suspiciousPatterns = InsecureRandRange(0xFFFFULL);

        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << in;
        CVM::ReputationScore out;
        ss >> out;

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.address == in.address, "3.7: ReputationScore.address mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.score == in.score, "3.7: ReputationScore.score mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.voteCount == in.voteCount, "3.7: ReputationScore.voteCount mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.lastUpdated == in.lastUpdated, "3.7: ReputationScore.lastUpdated mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.category == in.category, "3.7: ReputationScore.category mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.totalTransactions == in.totalTransactions, "3.7: ReputationScore.totalTransactions mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.totalVolume == in.totalVolume, "3.7: ReputationScore.totalVolume mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.suspiciousPatterns == in.suspiciousPatterns, "3.7: ReputationScore.suspiciousPatterns mismatch" + ctx);
    }
}

// ===========================================================================
// 3.1 / 3.2 — Weight-range and bond validation still reject invalid input.
//
// TrustGraph::AddTrustEdge rejects out-of-range weight (3.1) and insufficient
// bond (3.2); the addtrust RPC still throws the same "Weight must be between
// -100 and +100" message (3.1). These filters are unchanged by the fixes.
// **Validates: Requirements 3.1, 3.2**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_weight_range_validation_property)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);

    for (int i = 0; i < kSamples; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();

        // Out-of-range weight (either > 100 or < -100) must be rejected.
        int16_t badWeight = (i % 2 == 0)
            ? static_cast<int16_t>(101 + InsecureRandRange(100))
            : static_cast<int16_t>(-(101 + static_cast<int>(InsecureRandRange(100))));

        BOOST_CHECK_MESSAGE(
            !tg.AddTrustEdge(from, to, badWeight, kBond, uint256(), "bad weight"),
            "3.1: AddTrustEdge must reject out-of-range weight " +
            std::to_string(badWeight) + " (#" + std::to_string(i) + ")");
    }

    // RPC path: the same validation message is preserved. The weight check runs
    // before any wallet/decode work, so no wallet is required.
    {
        const std::string toStr = P2PKH(RandU160());
        bool threw = false;
        std::string msg;
        try {
            UniValue params = Arr({UniValue(toStr), UniValue((int64_t)200),
                                   UniValue(UniValue::VNUM, "3.00000000"),
                                   UniValue(std::string("bad weight"))});
            addtrust(MakeRequest(params));
        } catch (const UniValue& e) {
            threw = true;
            msg = RpcErrMessage(e);
        }
        BOOST_CHECK_MESSAGE(threw && msg.find("Weight must be between -100 and +100") != std::string::npos,
            "3.1: addtrust must reject out-of-range weight with the preserved "
            "message; got \"" + msg + "\"");
    }
}

BOOST_AUTO_TEST_CASE(preserve_insufficient_bond_validation_property)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);

    for (int i = 0; i < kSamples; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(1 + InsecureRandRange(100)); // [1,100]

        // One satoshi below the required bond must be rejected.
        CAmount required = RequiredBond(weight);
        CAmount tooLow = required - 1;

        BOOST_CHECK_MESSAGE(
            !tg.AddTrustEdge(from, to, weight, tooLow, uint256(), "low bond"),
            "3.2: AddTrustEdge must reject bond below the required amount "
            "(weight " + std::to_string(weight) + ", have " + std::to_string(tooLow) +
            ", need " + std::to_string(required) + ") (#" + std::to_string(i) + ")");

        // Exactly the required bond must be accepted (boundary preserved).
        BOOST_CHECK_MESSAGE(
            tg.AddTrustEdge(from, to, weight, required, uint256(), "ok bond"),
            "3.2: AddTrustEdge must accept the exact required bond (#" +
            std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.6 — maxdepth outside 1..10 still rejected with the same message.
// **Validates: Requirements 3.6**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_maxdepth_validation_property)
{
    const std::string targetStr = P2PKH(RandU160());
    const std::string viewerStr = P2PKH(RandU160());

    const std::vector<int64_t> badDepths = {0, -1, 11, 50};
    for (int64_t depth : badDepths) {
        bool threw = false;
        std::string msg;
        try {
            UniValue params = Arr({UniValue(targetStr), UniValue(viewerStr),
                                   UniValue(depth)});
            getweightedreputation(MakeRequest(params));
        } catch (const UniValue& e) {
            threw = true;
            msg = RpcErrMessage(e);
        }
        BOOST_CHECK_MESSAGE(
            threw && msg.find("Max depth must be between 1 and 10") != std::string::npos,
            "3.6: getweightedreputation must reject maxdepth " + std::to_string(depth) +
            " with the preserved message; got \"" + msg + "\"");
    }
}

// ===========================================================================
// 3.4 — Traversal still skips low-weight (< 10), slashed, and cycle edges.
// **Validates: Requirements 3.4**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_traversal_skips_low_weight_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 A = RandU160();
        const uint160 B = RandU160();
        // Weight strictly below the traversal threshold of 10.
        const int16_t lowWeight = static_cast<int16_t>(InsecureRandRange(10)); // [0,9]

        BOOST_REQUIRE(tg.AddTrustEdge(A, B, lowWeight, kBond, uint256(), "low"));

        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(A, B, 3);
        BOOST_CHECK_MESSAGE(paths.empty(),
            "3.4: traversal must skip the low-weight (" + std::to_string(lowWeight) +
            " < 10) edge A->B (#" + std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_CASE(preserve_traversal_skips_slashed_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 A = RandU160();
        const uint160 B = RandU160();

        // A slashed edge (weight well above threshold) must still be skipped.
        WriteRawEdge(db, MakeEdge(A, B, 80, /*slashed=*/true, "slashed"));

        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(A, B, 3);
        BOOST_CHECK_MESSAGE(paths.empty(),
            "3.4: traversal must skip the slashed edge A->B (#" +
            std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_CASE(preserve_traversal_skips_cycles_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 A = RandU160();
        const uint160 B = RandU160();
        const uint160 C = RandU160(); // unreachable target (node with no incoming edge)

        // Mutual cycle A <-> B, both above threshold. Searching for the
        // unreachable C must terminate (visited-set cycle skip) and find no path.
        BOOST_REQUIRE(tg.AddTrustEdge(A, B, 80, kBond, uint256(), "A->B"));
        BOOST_REQUIRE(tg.AddTrustEdge(B, A, 80, kBond, uint256(), "B->A"));

        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(A, C, 3);
        BOOST_CHECK_MESSAGE(paths.empty(),
            "3.4: traversal over the cycle A<->B must terminate and find no path "
            "to the unreachable C (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.5 — Self-view (viewer == target) returns the average of non-slashed
//       incoming trust.
// **Validates: Requirements 3.5**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_self_view_average_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 target = RandU160();
        const CVM::TrustNodeId targetNode = CVM::TrustNodeId::FromLegacyUint160(target);

        // Write several incoming edges, a random subset slashed. The self-view
        // must average ONLY the non-slashed weights.
        const int n = 2 + static_cast<int>(InsecureRandRange(5)); // [2,6]
        double sum = 0.0;
        int count = 0;
        for (int k = 0; k < n; ++k) {
            const uint160 src = RandU160();
            const int16_t w = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
            const bool slashed = (InsecureRandRange(2) == 0);
            WriteRawEdge(db, MakeEdge(src, target, w, slashed, "in"));
            if (!slashed) { sum += w; count++; }
        }

        const double expected = count > 0 ? (sum / count) : 0.0;
        const double actual = tg.GetWeightedReputation(targetNode, targetNode, 3);

        BOOST_CHECK_MESSAGE(std::abs(actual - expected) < 1e-9,
            "3.5: self-view must equal the average of non-slashed incoming trust; "
            "expected " + std::to_string(expected) + ", got " + std::to_string(actual) +
            " (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 3.9 / 3.10 — Existing address handling unchanged.
//
// DecodeDestination / EncodeDestination round-trip every standard type, and
// IsQuantumAddress still recognises quantum addresses (and only those). The WoT
// fix only *extends acceptance* inside the RPCs and MUST NOT alter this
// existing parsing/encoding.
// **Validates: Requirements 3.9, 3.10**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_address_encode_decode_roundtrip_property)
{
    const CChainParams& params = Params();

    for (int i = 0; i < kSamples; ++i) {
        // Build one destination of each standard type from random material.
        uint160 h160 = RandU160();
        uint256 h256 = InsecureRand256();

        std::vector<std::pair<std::string, CTxDestination>> dests;
        dests.emplace_back("P2PKH", CTxDestination(CKeyID(h160)));
        dests.emplace_back("P2SH", CTxDestination(CScriptID(h160)));
        dests.emplace_back("P2WPKH", CTxDestination(WitnessV0KeyHash(h160)));
        {
            WitnessV0ScriptHash wsh;
            std::memcpy(wsh.begin(), h256.begin(), 32);
            dests.emplace_back("P2WSH", CTxDestination(wsh));
        }
        {
            WitnessV2Quantum q;
            std::memcpy(q.begin(), h256.begin(), 32);
            dests.emplace_back("QUANTUM", CTxDestination(q));
        }

        for (const auto& d : dests) {
            const std::string encoded = EncodeDestination(d.second);
            BOOST_REQUIRE_MESSAGE(!encoded.empty(),
                "3.9: EncodeDestination produced empty string for " + d.first +
                " (#" + std::to_string(i) + ")");

            CTxDestination decoded = DecodeDestination(encoded);
            BOOST_CHECK_MESSAGE(decoded == d.second,
                "3.9: DecodeDestination(EncodeDestination(x)) != x for " + d.first +
                " (#" + std::to_string(i) + ")");

            // Quantum recognition is unchanged: true iff the type is quantum.
            const bool isQuantum = address::IsQuantumAddress(encoded, params);
            const bool shouldBeQuantum = (d.first == "QUANTUM");
            BOOST_CHECK_MESSAGE(isQuantum == shouldBeQuantum,
                "3.9: IsQuantumAddress mismatch for " + d.first + " (got " +
                std::to_string(isQuantum) + ") (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// 3.9 — Legacy v1 on-chain CVMTrustEdgeData decodes to the same edge.
//
// A pure-uint160 (P2PKH) edge is emitted as the fixed 54-byte v1 layout and a
// hand-built 54-byte v1 blob decodes to the exact same fields as before.
// **Validates: Requirements 3.9**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_legacy_v1_onchain_payload_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        const CAmount bond = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        const uint32_t ts = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));

        // A pure-uint160 edge (wide from/to left unset) must emit v1 (54 bytes).
        CVM::CVMTrustEdgeData in;
        in.fromAddress = from;
        in.toAddress = to;
        in.weight = weight;
        in.bondAmount = bond;
        in.timestamp = ts;

        std::vector<uint8_t> bytes = in.Serialize();
        BOOST_CHECK_MESSAGE(bytes.size() == 54,
            "3.9: a pure-uint160 edge must serialize as the 54-byte v1 layout; got " +
            std::to_string(bytes.size()) + " (#" + std::to_string(i) + ")");

        CVM::CVMTrustEdgeData out;
        BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes),
            "3.9: v1 payload must deserialize (#" + std::to_string(i) + ")");

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.fromAddress == from, "3.9: v1 fromAddress mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.toAddress == to, "3.9: v1 toAddress mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.weight == weight, "3.9: v1 weight mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.bondAmount == bond, "3.9: v1 bondAmount mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.timestamp == ts, "3.9: v1 timestamp mismatch" + ctx);
    }

    // Hand-built 54-byte v1 blob (the exact historical layout:
    // from(20) to(20) weight(2 LE) bond(8 LE) ts(4 LE)) decodes as before.
    {
        uint160 from = RandU160();
        uint160 to = RandU160();
        int16_t weight = -37;
        int64_t bond = 0x0102030405LL;
        uint32_t ts = 0x11223344u;

        std::vector<uint8_t> blob;
        blob.insert(blob.end(), from.begin(), from.end());
        blob.insert(blob.end(), to.begin(), to.end());
        blob.push_back(weight & 0xFF);
        blob.push_back((weight >> 8) & 0xFF);
        for (int b = 0; b < 8; ++b) blob.push_back((bond >> (b * 8)) & 0xFF);
        for (int b = 0; b < 4; ++b) blob.push_back((ts >> (b * 8)) & 0xFF);
        BOOST_REQUIRE_EQUAL(blob.size(), size_t(54));

        CVM::CVMTrustEdgeData out;
        BOOST_REQUIRE(out.Deserialize(blob));
        BOOST_CHECK(out.fromAddress == from);
        BOOST_CHECK(out.toAddress == to);
        BOOST_CHECK(out.weight == weight);
        BOOST_CHECK(out.bondAmount == bond);
        BOOST_CHECK(out.timestamp == ts);
    }
}

// ===========================================================================
// 3.8 — Cluster propagation fields unchanged for unaffected (legacy) inputs.
//
// Propagating a legacy P2PKH edge still creates the expected propagated
// record(s) (edges_propagated >= 1) and the stored PropagatedTrustEdge carries
// the original target, weight, and bond unchanged. In a unit context with no
// chain-derived clusters, the target forms a single-address cluster.
// **Validates: Requirements 3.8**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_cluster_propagation_fields_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);
        CVM::WalletClusterer clusterer(db);
        CVM::TrustPropagator propagator(db, clusterer, tg);

        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]

        BOOST_REQUIRE(tg.AddTrustEdge(from, to, weight, kBond, uint256(), "clustered"));

        CVM::TrustEdge edge = MakeEdge(from, to, weight, /*slashed=*/false, "clustered");
        uint32_t propagated = propagator.PropagateTrustEdge(edge);

        BOOST_CHECK_MESSAGE(propagated >= 1,
            "3.8: propagating a legacy edge must create at least one propagated "
            "record (edges_propagated) (#" + std::to_string(i) + ")");

        // The propagated record for the target carries the original fields.
        std::vector<CVM::PropagatedTrustEdge> props =
            propagator.GetPropagatedEdgesForAddress(to);
        BOOST_REQUIRE_MESSAGE(!props.empty(),
            "3.8: propagated edge for the target must be retrievable (#" +
            std::to_string(i) + ")");

        bool matched = false;
        for (const auto& p : props) {
            if (p.originalTarget == to && p.trustWeight == weight &&
                p.bondAmount == kBond) {
                matched = true;
            }
        }
        BOOST_CHECK_MESSAGE(matched,
            "3.8: propagated edge must preserve originalTarget/weight/bond (#" +
            std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
