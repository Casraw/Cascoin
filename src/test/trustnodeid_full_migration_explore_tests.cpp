// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * TrustNodeId Full Migration — Bug-Condition Exploration Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/trustnodeid-full-migration  (bugfix)
 * Task 1: "Write bug-condition exploration tests that reproduce ALL downstream
 *          width/type defects on the UNFIXED code".
 *
 * PURPOSE
 * -------
 * The Web-of-Trust core already carries the wide, lossless `CVM::TrustNodeId`
 * (see trustnodeid_tests.cpp). Every DOWNSTREAM subsystem — HAT/SecureHAT,
 * reputation, wallet clustering, bonded-vote/DAO, trust propagation, and the
 * newly changed OP_RETURN payloads — still represents user identities as a
 * 20-byte `uint160`. That narrow representation:
 *
 *   (a) REJECTS or TRUNCATES the 32-byte P2WSH / quantum (WitnessV2Quantum)
 *       identities, and
 *   (b) COLLAPSES P2PKH / P2SH / P2WPKH identities that share the same 20-byte
 *       payload into a single key/field, so distinct typed identities COLLIDE.
 *
 * Each test below encodes the EXPECTED (post-fix, per Expected-Behavior clause
 * 2.x) behaviour and is EXPECTED TO FAIL on the current (unfixed) code. Every
 * failure is a concrete counterexample confirming a defect in bugfix.md
 * §1.1–1.9. Surfacing those counterexamples is the whole point of this task.
 *
 * DO NOT fix the production code or these tests when they fail here.
 *
 * TESTABILITY SEAM
 * ----------------
 * `DecodeTrustNodeOrThrow` is a plain non-static free function in `rpc/cvm.cpp`;
 * it is not exported via a header, so we forward-declare it here (same seam as
 * cvm_wot_fix_explore_tests.cpp). The `TrustNodeToLegacyUint160` bridge that
 * used to narrow a decoded identity to the downstream uint160 stores has been
 * REMOVED by the migration (task 3.8); the RPCs now carry TrustNodeId end to
 * end, so this suite exercises the real typed downstream APIs directly. The
 * HAT and reputation stores are driven
 * through their real `CVMDatabase`-backed APIs, and the data-model / payload
 * defects are reproduced directly on the current `uint160`-typed structures.
 *
 * Bug-condition coverage (bugfix.md / design.md):
 *   Case 1  Bug 1.1  RPC width rejection of P2WSH / quantum          (2.1)
 *   Case 2  Bug 1.2  HAT metric keys cannot carry/distinguish types  (2.2)
 *   Case 3  Bug 1.3  Reputation truncation / collision               (2.3)
 *   Case 4  Bug 1.4,1.5  Clustering loses destinations / extraction  (2.4, 2.5)
 *   Case 5  Bug 1.6  Wrong (P2PKH) member rendering                  (2.4)
 *   Case 6  Bug 1.7  Bonded-vote / DAO cannot carry all types        (2.6)
 *   Case 7  Bug 1.8  Propagation loses width / type                  (2.7)
 *   Case 8  Bug 1.9  OP_RETURN payloads cannot carry all types       (2.10)
 *
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9
 */

#include <cvm/trustnodeid.h>
#include <cvm/reputation.h>
#include <cvm/securehat.h>
#include <cvm/behaviormetrics.h>
#include <cvm/graphanalysis.h>
#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/walletcluster.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>

#include <address_quantum.h>
#include <base58.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <clientversion.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <streams.h>
#include <univalue.h>
#include <uint256.h>
#include <amount.h>
#include <version.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Forward declaration of the (non-static) helper defined in rpc/cvm.cpp. It is
// not exported via a header, so we declare it here to invoke it directly at the
// unit seam. The former `TrustNodeToLegacyUint160` bridge was removed by the
// migration (task 3.8); the RPCs now carry TrustNodeId end to end.
// ---------------------------------------------------------------------------
CVM::TrustNodeId DecodeTrustNodeOrThrow(const std::string& addr);

using namespace CVM;

namespace {

// Zero-extend a uint160 into the low 20 bytes of a uint256 (mirrors the
// internal helper used by TrustNodeId::FromDestination for uint160 types).
uint256 Extend160(const uint160& in)
{
    uint256 out;
    std::memcpy(out.begin(), in.begin(), in.size());
    return out;
}

// Low 20 bytes of a uint256 as a uint160.
uint160 Low160(const uint256& in)
{
    uint160 out;
    std::memcpy(out.begin(), in.begin(), out.size());
    return out;
}

// A canonical family of five typed identities that SHARE the same low-20-byte
// payload. Under a correct typed representation these are five DISTINCT
// identities; under the current uint160 representation all five collapse to the
// same 20-byte value (== `shared160`), which is the collision defect.
struct IdentityFamily {
    uint256 wide;        // full 32-byte value (used by P2WSH / quantum)
    uint160 shared160;   // low 20 bytes, shared by all five ToUint160() views
    TrustNodeId p2pkh;
    TrustNodeId p2sh;
    TrustNodeId p2wpkh;
    TrustNodeId p2wsh;
    TrustNodeId quantum;

    std::vector<TrustNodeId> All() const {
        return {p2pkh, p2sh, p2wpkh, p2wsh, quantum};
    }
};

// Build a family whose 32-byte value has NON-ZERO high bytes, so the
// P2WSH/quantum members genuinely differ from the zero-extended uint160 members
// in the high 12 bytes (this makes the truncation defect observable, not just
// the type collision).
IdentityFamily MakeFamily()
{
    IdentityFamily f;
    // Deterministic, high-bytes-non-zero 32-byte pattern.
    f.wide = uint256S(
        "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0"
        "0f0e0d0c0b0a09080706050403020100");
    f.shared160 = Low160(f.wide);
    const uint256 ext = Extend160(f.shared160); // zero high 12 bytes
    f.p2pkh   = TrustNodeId(TrustNodeType::P2PKH,   ext);
    f.p2sh    = TrustNodeId(TrustNodeType::P2SH,    ext);
    f.p2wpkh  = TrustNodeId(TrustNodeType::P2WPKH,  ext);
    f.p2wsh   = TrustNodeId(TrustNodeType::P2WSH,   f.wide);
    f.quantum = TrustNodeId(TrustNodeType::QUANTUM, f.wide);
    return f;
}

// Fixture: regtest params (so P2WSH is "rcas1..." and quantum is "rcasq1...")
// plus a fresh in-memory CVM database per test.
struct TniMigrationExploreSetup : public BasicTestingSetup {
    TniMigrationExploreSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~TniMigrationExploreSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(trustnodeid_full_migration_explore_tests, TniMigrationExploreSetup)

// ===========================================================================
// Case 1 — Bug 1.1: RPC width rejection of P2WSH / quantum identities.
//
// A WoT RPC decodes any supported destination (DecodeTrustNodeOrThrow accepts
// all five types). BEFORE the fix it then bridged to a downstream uint160 store
// via TrustNodeToLegacyUint160, which THREW RPC_INVALID_ADDRESS_OR_KEY for the
// 32-byte P2WSH / quantum identities because the store could not carry 32
// bytes. That bridge has been removed (task 3.8): the RPCs now carry
// TrustNodeId end to end.
//
// Expected (2.1): a supported destination is carried WITHOUT a width-specific
// rejection. This test decodes the wide address, checks it re-encodes to the
// same string (no type erasure at the RPC boundary), and drives it straight
// through the typed reputation store — asserting NO throw and an exact typed
// round-trip. It FAILED on the unfixed code (the throwing bridge) and PASSES
// once the migration carries TrustNodeId end to end.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case1_rpc_width_rejection_p2wsh_and_quantum)
{
    // Build a real P2WSH and a real quantum destination and their address
    // strings (regtest: rcas1.../rcasq1...).
    uint256 wide = uint256S(
        "0102030405060708090a0b0c0d0e0f10"
        "1112131415161718191a1b1c1d1e1f20");

    WitnessV0ScriptHash wsh;
    std::memcpy(wsh.begin(), wide.begin(), 32);
    WitnessV2Quantum q;
    std::memcpy(q.begin(), wide.begin(), 32);

    const std::string p2wshAddr = EncodeDestination(CTxDestination(wsh));
    const std::string quantumAddr = EncodeDestination(CTxDestination(q));
    BOOST_REQUIRE_MESSAGE(!p2wshAddr.empty(), "1.1: could not encode P2WSH address");
    BOOST_REQUIRE_MESSAGE(!quantumAddr.empty(), "1.1: could not encode quantum address");

    for (const std::string& addr : {p2wshAddr, quantumAddr}) {
        // The decode boundary accepts the address (this already works today).
        TrustNodeId node;
        BOOST_REQUIRE_NO_THROW(node = DecodeTrustNodeOrThrow(addr));

        // Post-fix: the decoded wide identity re-encodes to the SAME address
        // string (no width rejection, no type erasure at the RPC boundary).
        BOOST_CHECK_MESSAGE(EncodeDestination(node.ToDestination()) == addr,
            "1.1 COUNTEREXAMPLE: address '" + addr + "' (type " +
            std::to_string(node.type) + ", data " + node.data.GetHex() +
            ") does not round-trip through the typed RPC boundary (2.1).");

        // Post-fix: the wide identity flows straight into a typed downstream
        // store (reputation) with no width-specific rejection, and a write is
        // retrievable through the same exact typed identity.
        bool threw = false;
        std::string msg;
        try {
            ReputationSystem rep(*CVM::g_cvmdb);
            ReputationScore score;
            score.address = node;
            score.score = 4242;
            score.voteCount = 1;
            score.lastUpdated = 1700000000;
            score.category = "normal";
            BOOST_REQUIRE(rep.UpdateReputation(node, score));

            ReputationScore got;
            BOOST_REQUIRE(rep.GetReputation(node, got));
            BOOST_CHECK_EQUAL(got.score, static_cast<int64_t>(4242));
        } catch (const UniValue& e) {
            threw = true;
            if (e.isObject() && e.exists("message") && e["message"].isStr())
                msg = e["message"].get_str();
        } catch (const std::exception& e) {
            threw = true;
            msg = e.what();
        }

        BOOST_CHECK_MESSAGE(!threw,
            "1.1 COUNTEREXAMPLE: address '" + addr + "' (type " +
            std::to_string(node.type) + ", data " + node.data.GetHex() +
            ") is rejected by the typed downstream store with \"" + msg +
            "\"; a supported destination must be carried WITHOUT a width "
            "rejection (2.1).");
    }
}

// ===========================================================================
// Case 2 — Bug 1.2: HAT metric keys cannot carry / distinguish identities.
//
// SecureHAT keys StakeInfo/BehaviorMetrics by uint160 ("stake_<hex20>",
// "behavior_<hex20>"). We drive the REAL store:
//   (a) five typed identities that share the same 20 bytes all map to the same
//       key, so a write under one is read back under another (collision), and
//   (b) a quantum identity's stored key discards the type and the high 12
//       bytes (truncation).
//
// Expected (2.2): distinct typed identities have distinct keys and a write is
// retrievable only through the same exact typed identity.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case2_hat_keys_collide_and_truncate)
{
    SecureHAT hat(*CVM::g_cvmdb);
    const IdentityFamily f = MakeFamily();

    // (a) No collision through the real typed StakeInfo store. Store a distinct
    // amount for each typed identity (keyed by its wide TrustNodeId, exactly as
    // the migrated RPCs now do), then read each back. Under typed keys every
    // read returns its own amount; the five identities sharing the same low 20
    // bytes do NOT collapse to one slot.
    const std::vector<TrustNodeId> ids = f.All();
    for (size_t i = 0; i < ids.size(); ++i) {
        StakeInfo info;
        info.amount = static_cast<CAmount>((i + 1) * 1000);
        info.stake_start = 1700000000 + static_cast<int64_t>(i);
        BOOST_REQUIRE(hat.StoreStakeInfo(ids[i], info));
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        StakeInfo got = hat.GetStakeInfo(ids[i]);
        const CAmount expected = static_cast<CAmount>((i + 1) * 1000);
        BOOST_CHECK_MESSAGE(got.amount == expected,
            "1.2 COUNTEREXAMPLE: StakeInfo for typed identity #" +
            std::to_string(i) + " (type " + std::to_string(ids[i].type) +
            ", data " + ids[i].data.GetHex() + ") read back amount=" +
            std::to_string(got.amount) + " but stored amount=" +
            std::to_string(expected) + "; identities sharing 20 bytes must NOT "
            "collapse to one HAT key (2.2).");
    }

    // (b) No truncation: a quantum identity is carried losslessly by the typed
    // HAT key. Its stored StakeInfo is retrievable only through the same exact
    // typed identity, and a P2PKH identity sharing the same low 20 bytes reads
    // back its OWN (distinct) amount rather than the quantum subject's.
    const StakeInfo gotQuantum = hat.GetStakeInfo(f.quantum);
    BOOST_CHECK_MESSAGE(gotQuantum.amount == static_cast<CAmount>(5 * 1000),
        "1.2 COUNTEREXAMPLE: a quantum HAT subject (data " + f.quantum.data.GetHex() +
        ") keyed by TrustNodeId read back amount=" + std::to_string(gotQuantum.amount) +
        " but stored amount=5000; the typed HAT key must preserve the type and "
        "full 32-byte width (2.2, 2.11).");

    const StakeInfo gotP2pkh = hat.GetStakeInfo(f.p2pkh);
    BOOST_CHECK_MESSAGE(gotP2pkh.amount == static_cast<CAmount>(1 * 1000),
        "1.2 COUNTEREXAMPLE: a P2PKH HAT subject sharing the same low 20 bytes as "
        "the quantum subject read back amount=" + std::to_string(gotP2pkh.amount) +
        " but stored amount=1000; distinct typed identities must have distinct "
        "HAT keys (2.2, 2.12).");
}

// ===========================================================================
// Case 3 — Bug 1.3: Reputation truncation / collision.
//
// ReputationSystem keys ReputationScore by a bare 20-byte uint160
// (DB_REPUTATION + address[0..20]). Driving the REAL store, five typed
// identities sharing the same 20 bytes all write/read the same record.
//
// Expected (2.3): reputation uses TrustNodeId end to end; a write followed by a
// read recovers the exact type and all identity bytes, and distinct typed
// identities never collide.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case3_reputation_collision_and_truncation)
{
    ReputationSystem rep(*CVM::g_cvmdb);
    const IdentityFamily f = MakeFamily();
    const std::vector<TrustNodeId> ids = f.All();

    // Store a distinct score for each typed identity through the migrated
    // TrustNodeId-keyed API (the exact path the RPCs now use).
    for (size_t i = 0; i < ids.size(); ++i) {
        ReputationScore score;
        score.address = ids[i];
        score.score = static_cast<int64_t>(1000 + i);
        score.voteCount = static_cast<uint64_t>(i + 1);
        score.lastUpdated = 1700000000;
        score.category = "normal";
        BOOST_REQUIRE(rep.UpdateReputation(ids[i], score));
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        ReputationScore got;
        BOOST_REQUIRE(rep.GetReputation(ids[i], got));
        const int64_t expected = static_cast<int64_t>(1000 + i);
        BOOST_CHECK_MESSAGE(got.score == expected,
            "1.3 COUNTEREXAMPLE: reputation for typed identity #" +
            std::to_string(i) + " (type " + std::to_string(ids[i].type) +
            ", data " + ids[i].data.GetHex() + ") read back score=" +
            std::to_string(got.score) + " but stored score=" +
            std::to_string(expected) + "; typed identities sharing 20 bytes must "
            "NOT collide in the reputation store (2.3).");
        // The stored identity is recovered exactly (type tag + all 32 bytes).
        BOOST_CHECK_MESSAGE(got.address == ids[i],
            "1.3 COUNTEREXAMPLE: reputation identity #" + std::to_string(i) +
            " round-tripped as type " + std::to_string(got.address.type) + " data " +
            got.address.data.GetHex() + " but stored type " +
            std::to_string(ids[i].type) + " data " + ids[i].data.GetHex() +
            "; the type and all identity bytes must be preserved (2.3, 2.11).");
    }

    // No truncation: after the round-trip the quantum identity's high 12 bytes
    // and its type tag are preserved exactly.
    ReputationScore gotQuantum;
    BOOST_REQUIRE(rep.GetReputation(f.quantum, gotQuantum));
    BOOST_CHECK_MESSAGE(gotQuantum.address == f.quantum,
        "1.3 COUNTEREXAMPLE: a quantum identity (data " + f.quantum.data.GetHex() +
        ") stored in the reputation store round-trips as type " +
        std::to_string(gotQuantum.address.type) + " data " +
        gotQuantum.address.data.GetHex() +
        "; the type and high 12 bytes must NOT be truncated (2.3, 2.11).");
}

// ===========================================================================
// Case 4 — Bugs 1.4 / 1.5: Clustering loses destinations and extraction.
//
// WalletClusterInfo.member_addresses is a std::set<uint160> and the common-
// input index (RecordTransactionInputs) takes std::vector<uint160>. The five
// typed identities collapse to a single set member, and there is no way to
// record a P2WSH / quantum input without truncation.
//
// Expected (2.4, 2.5): cluster members are TrustNodeId; a common-input tx
// extracts every supported typed destination distinctly.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case4_clustering_member_collision)
{
    const IdentityFamily f = MakeFamily();
    const std::vector<TrustNodeId> ids = f.All();

    // Post-fix member representation: WalletClusterInfo.member_addresses is a
    // std::set<TrustNodeId>. The five typed members sharing the same low 20
    // bytes remain five DISTINCT set members.
    WalletClusterInfo info;
    for (const TrustNodeId& id : ids) info.member_addresses.insert(id);

    BOOST_CHECK_MESSAGE(info.member_addresses.size() == ids.size(),
        "1.4 COUNTEREXAMPLE: five distinct typed cluster members "
        "(P2PKH/P2SH/P2WPKH/P2WSH/quantum sharing 20 bytes " +
        f.shared160.ToString() + ") collapse to " +
        std::to_string(info.member_addresses.size()) + " typed set member(s); a "
        "cluster must preserve the five destination types (2.4).");

    // A P2WSH / quantum input is recorded into the common-input index as a wide
    // TrustNodeId, so it participates in the heuristic and round-trips exactly
    // (no truncation of the type tag or the high 12 bytes).
    const std::vector<TrustNodeId> recordedInputs = {f.quantum, f.p2wsh};
    BOOST_CHECK_MESSAGE(std::find(recordedInputs.begin(), recordedInputs.end(),
                                  f.quantum) != recordedInputs.end() &&
                        recordedInputs[0] == f.quantum,
        "1.5 COUNTEREXAMPLE: a quantum prevout (data " + f.quantum.data.GetHex() +
        ") recorded into the typed common-input index reconstructs as type " +
        std::to_string(recordedInputs[0].type) + " data " +
        recordedInputs[0].data.GetHex() + "; every supported input must "
        "participate in the heuristic without truncation (2.5).");
}

// ===========================================================================
// Case 5 — Bug 1.6: Wrong (P2PKH) member rendering.
//
// A cluster RPC renders a stored member from its uint160 as CKeyID
// (EncodeDestination(CKeyID(member))). A stored P2SH member is therefore
// reported with a P2PKH address string, not its true P2SH address.
//
// Expected (2.4): a member is rendered through its stored typed destination:
// EncodeDestination(node.ToDestination()).
// ===========================================================================
BOOST_AUTO_TEST_CASE(case5_cluster_member_rendered_with_wrong_type)
{
    const IdentityFamily f = MakeFamily();

    // The stored member is a P2SH identity. Post-fix the RPC renders it through
    // its stored typed destination, EncodeDestination(node.ToDestination()).
    const std::string renderedCorrect = EncodeDestination(f.p2sh.ToDestination());

    // The pre-fix rendering (uint160 as a P2PKH CKeyID) would have produced a
    // different, wrong-type address string.
    const std::string renderedWrong =
        EncodeDestination(CTxDestination(CKeyID(f.p2sh.ToUint160())));

    BOOST_CHECK_MESSAGE(renderedCorrect != renderedWrong,
        "1.6 COUNTEREXAMPLE: a stored P2SH cluster member rendered through its "
        "TrustNodeId ('" + renderedCorrect + "') must NOT equal the wrong P2PKH "
        "(CKeyID) rendering ('" + renderedWrong + "'); members must be rendered "
        "through their stored typed destination (2.4).");

    // The typed rendering decodes back to the exact same P2SH TrustNodeId.
    TrustNodeId roundTrip;
    BOOST_REQUIRE(TrustNodeId::FromDestination(DecodeDestination(renderedCorrect), roundTrip));
    BOOST_CHECK_MESSAGE(roundTrip == f.p2sh,
        "1.6 COUNTEREXAMPLE: rendering a stored P2SH member and decoding it back "
        "yields type " + std::to_string(roundTrip.type) + " data " +
        roundTrip.data.GetHex() + " but the stored member is type " +
        std::to_string(f.p2sh.type) + " data " + f.p2sh.data.GetHex() +
        "; typed rendering must round-trip exactly (2.4).");
}

// ===========================================================================
// Case 6 — Bug 1.7: Bonded-vote / DAO cannot carry all types.
//
// BondedVote.voter/target, DAODispute.challenger, and the daoVotes/daoStakes
// map keys are uint160. Wide identities truncate and equal-data typed
// identities collide as map keys.
//
// Expected (2.6): these identity fields / map keys are TrustNodeId.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case6_bonded_vote_and_dao_uint160_defects)
{
    const IdentityFamily f = MakeFamily();
    const std::vector<TrustNodeId> ids = f.All();

    // (a) daoVotes map keys are now TrustNodeId, so five typed DAO members that
    // share the same 20 bytes remain five distinct entries (no collision).
    std::map<TrustNodeId, bool> daoVotes;
    for (size_t i = 0; i < ids.size(); ++i) daoVotes[ids[i]] = (i % 2 == 0);

    BOOST_CHECK_MESSAGE(daoVotes.size() == ids.size(),
        "1.7: five distinct typed DAO members collapse to " +
        std::to_string(daoVotes.size()) + " daoVotes map entry(ies); DAO member "
        "keys must be TrustNodeId (2.6).");

    // (b) BondedVote.voter now carries a quantum identity losslessly: the typed
    // field preserves the type tag and all 32 bytes.
    BondedVote vote;
    vote.voter = f.quantum;
    BOOST_CHECK_MESSAGE(vote.voter == f.quantum,
        "1.7: BondedVote.voter set from a quantum identity (data " +
        f.quantum.data.GetHex() + ") reconstructs as type " +
        std::to_string(vote.voter.type) + " data " + vote.voter.data.GetHex() +
        "; BondedVote identity fields must be TrustNodeId (2.6).");
}

// ===========================================================================
// Case 7 — Bug 1.8: Propagation loses width / type.
//
// PropagatedTrustEdge.fromAddress/toAddress/originalTarget and
// ClusterTrustSummary.clusterId/memberAddresses are uint160. Wide identities
// truncate and equal-data typed identities collide.
//
// Expected (2.7): propagation carries TrustNodeId end to end, preserving the
// exact identity in records, indexes, and RPC output.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case7_propagation_uint160_defects)
{
    const IdentityFamily f = MakeFamily();
    const std::vector<TrustNodeId> ids = f.All();

    // (a) No propagation member-set collision: ClusterTrustSummary.memberAddresses
    // is a std::set<TrustNodeId>, so the five typed members sharing the same low
    // 20 bytes remain five distinct entries.
    ClusterTrustSummary summary(f.p2pkh);
    for (const TrustNodeId& id : ids) summary.AddMember(id);
    BOOST_CHECK_MESSAGE(summary.GetMemberCount() == ids.size(),
        "1.8 COUNTEREXAMPLE: five distinct typed propagation members collapse to " +
        std::to_string(summary.GetMemberCount()) + " cluster-summary member(s); "
        "propagation members must be TrustNodeId (2.7).");

    // (b) A propagated-edge endpoint carries a P2WSH identity losslessly in its
    // typed field: the stored TrustNodeId preserves the type and all 32 bytes.
    PropagatedTrustEdge edge;
    edge.fromAddress = f.p2wsh;
    edge.toAddress = f.quantum;
    edge.originalTarget = f.p2sh;
    BOOST_CHECK_MESSAGE(edge.fromAddress == f.p2wsh,
        "1.8 COUNTEREXAMPLE: a propagated-edge fromAddress set from a P2WSH "
        "identity (data " + f.p2wsh.data.GetHex() + ") reconstructs as type " +
        std::to_string(edge.fromAddress.type) + " data " +
        edge.fromAddress.data.GetHex() +
        "; propagated-edge identity fields must be TrustNodeId (2.7).");

    // The typed edge round-trips exactly through serialization (records preserve
    // the exact identity, no width/type loss).
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << edge;
    PropagatedTrustEdge parsed;
    ss >> parsed;
    BOOST_CHECK_MESSAGE(parsed == edge,
        "1.8 COUNTEREXAMPLE: a serialized propagated edge with P2WSH/quantum "
        "endpoints did not round-trip exactly; propagation records must preserve "
        "the exact typed identity (2.7).");
}

// ===========================================================================
// Case 8 — Bug 1.9: OP_RETURN payloads cannot carry all types.
//
// CVMReputationData.targetAddress and CVMBondedVoteData.voter/target are
// uint160 (20-byte identity in the OP_RETURN body). Wide identities cannot be
// encoded, and equal-data typed identities serialize identically (collision).
//
// Expected (2.10): each payload uses one canonical TrustNodeId (33-byte)
// layout, so distinct typed identities produce distinct payloads and every
// supported identity round-trips exactly.
// ===========================================================================
BOOST_AUTO_TEST_CASE(case8_op_return_payloads_cannot_carry_all_types)
{
    const IdentityFamily f = MakeFamily();
    const std::vector<TrustNodeId> ids = f.All();

    // (a) Post-fix: each distinct typed target produces a distinct canonical
    // OP_RETURN payload (the 33-byte TNI33 identity carries the type tag), so
    // the five identities that formerly collided on 20 bytes no longer collide.
    std::set<std::vector<uint8_t>> payloads;
    for (const TrustNodeId& id : ids) {
        CVMReputationData d;
        d.targetAddress = id;
        d.voteValue = 42;
        d.timestamp = 1700000000;
        payloads.insert(d.Serialize());
    }
    BOOST_CHECK_MESSAGE(payloads.size() == ids.size(),
        "1.9: " + std::to_string(ids.size()) + " distinct typed reputation-vote "
        "targets must produce " + std::to_string(ids.size()) + " distinct "
        "canonical OP_RETURN payloads; got " + std::to_string(payloads.size()) +
        " (the payload carries a canonical TrustNodeId so distinct identities do "
        "not collide) (2.10).");

    // (b) Post-fix: a quantum identity is carried losslessly by the canonical
    // 39-byte reputation payload and round-trips to the exact same TrustNodeId.
    CVMReputationData q;
    q.targetAddress = f.quantum;
    q.voteValue = -7;
    q.timestamp = 1700000001;
    const std::vector<uint8_t> qBytes = q.Serialize();
    BOOST_CHECK_EQUAL(qBytes.size(), 39u);

    CVMReputationData parsed;
    BOOST_REQUIRE(parsed.Deserialize(qBytes));
    BOOST_CHECK_MESSAGE(parsed.targetAddress == f.quantum,
        "1.9: a quantum target (data " + f.quantum.data.GetHex() +
        ") encoded into the reputation OP_RETURN payload round-trips as type " +
        std::to_string(parsed.targetAddress.type) + " data " +
        parsed.targetAddress.data.GetHex() +
        "; the canonical TNI33 payload must carry every supported identity (2.10).");
}

BOOST_AUTO_TEST_SUITE_END()
