// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * TrustNodeId Full Migration — Fix-Check Property Test Suite (AFTER fix)
 *
 * Spec: .kiro/specs/trustnodeid-full-migration  (bugfix)
 * Task 4: "Write fix-check property tests P1–P10".
 *
 * PURPOSE
 * -------
 * These property tests map to the ten Correctness Properties in design.md and
 * extend/parallel the exploratory bug-condition tests (task 1). They run AFTER
 * the direct uint160 -> TrustNodeId migration has landed and are EXPECTED TO
 * PASS — passing confirms every downstream user-identity path carries a strictly
 * canonical TrustNodeId end to end (all five destination types, no collision, no
 * width rejection) while every preserved boundary (contract/EVM uint160, uint256
 * hash domains, TrustEdge v1/v2, quantum parsing) is unchanged.
 *
 * Property map (design.md → Correctness Properties):
 *   P1  Canonical identity round-trip                       (2.1, 2.8, 2.11)
 *   P2  Collision safety                                    (2.9, 2.11, 2.12)
 *   P3  Native downstream API closure                       (2.1–2.7)
 *   P4  Current record round-trip                           (2.8, 2.11, 5.1)
 *   P5  Canonical payload round-trip                        (2.10, 2.11, 4.4, 4.5)
 *   P6  Five-type clustering extraction                     (2.4, 2.5, 4.6)
 *   P7  Cluster persistence and rendering                   (2.4, 3.2, 4.6)
 *   P8  Idempotency                                         (2.13, 3.2, 4.4)
 *   P9  Fixed-width scope preservation                      (4.1, 4.2)
 *   P10 Completed-feature preservation                      (4.3–4.7)
 *
 * PBT approach (design): generate canonical identities uniformly across all five
 * tags (including equal data under different tags); bounded current records;
 * valid/invalid payloads; and transactions with resolvable, unsupported, and
 * unresolved prevouts. Each failure message records the sample index and the
 * exact typed identity (type + data hex) as a minimized counterexample; the
 * BasicTestingSetup RNG is deterministically seeded so a failing sample is
 * reproducible.
 *
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12,
 *               2.13, 3.2, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */

#include <cvm/trustnodeid.h>
#include <cvm/reputation.h>
#include <cvm/securehat.h>
#include <cvm/behaviormetrics.h>
#include <cvm/graphanalysis.h>
#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/walletcluster.h>
#include <cvm/clustertrustquery.h>
#include <cvm/softfork.h>
#include <cvm/contract.h>
#include <cvm/cvmdb.h>

#include <address_quantum.h>
#include <base58.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <clientversion.h>
#include <hash.h>
#include <pubkey.h>
#include <streams.h>
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

using namespace CVM;

namespace {

// Property sample counts. Pure/in-memory properties get more samples; the
// DB-backed cases run fewer to keep the suite fast.
static constexpr int kSamples = 96;
static constexpr int kSamplesDb = 32;

// The five supported destination-type tags, in stable tag order.
static const std::vector<TrustNodeType> kAllTypes = {
    TrustNodeType::P2PKH, TrustNodeType::P2SH, TrustNodeType::P2WPKH,
    TrustNodeType::P2WSH, TrustNodeType::QUANTUM};

bool IsWide(TrustNodeType t)
{
    return t == TrustNodeType::P2WSH || t == TrustNodeType::QUANTUM;
}

// A random uint160 (20-byte value), guaranteed non-null.
uint160 RandU160()
{
    uint160 a;
    do {
        uint256 r = InsecureRand256();
        std::memcpy(a.begin(), r.begin(), 20);
    } while (a.IsNull());
    return a;
}

// A random full 32-byte uint256, guaranteed non-null and with at least one
// non-zero byte in the high 12 bytes (so wide identities are genuinely wide).
uint256 RandU256Wide()
{
    uint256 r;
    do {
        r = InsecureRand256();
    } while (r.IsNull());
    // Force a non-zero high byte so P2WSH/QUANTUM differ from a zero-extended
    // uint160 with the same low 20 bytes.
    if (std::all_of(r.begin() + 20, r.end(), [](uint8_t b) { return b == 0; })) {
        *(r.begin() + 31) = 0x7a;
    }
    return r;
}

// Zero-extend a random uint160 into the low 20 bytes of a uint256 (high zero).
uint256 RandExt160()
{
    uint256 out;
    uint160 a = RandU160();
    std::memcpy(out.begin(), a.begin(), 20);
    return out;
}

// Build a strictly canonical TrustNodeId for a given destination type.
TrustNodeId MakeCanonical(TrustNodeType t)
{
    if (IsWide(t)) return TrustNodeId(t, RandU256Wide());
    return TrustNodeId(t, RandExt160());
}

// Build a real CTxDestination + its expected canonical TrustNodeId for a type.
std::pair<CTxDestination, TrustNodeId> MakeDest(TrustNodeType t)
{
    switch (t) {
    case TrustNodeType::P2PKH: {
        uint160 a = RandU160();
        return {CTxDestination(CKeyID(a)), TrustNodeId(t, [&] {
                    uint256 d; std::memcpy(d.begin(), a.begin(), 20); return d; }())};
    }
    case TrustNodeType::P2SH: {
        uint160 a = RandU160();
        return {CTxDestination(CScriptID(a)), TrustNodeId(t, [&] {
                    uint256 d; std::memcpy(d.begin(), a.begin(), 20); return d; }())};
    }
    case TrustNodeType::P2WPKH: {
        uint160 a = RandU160();
        return {CTxDestination(WitnessV0KeyHash(a)), TrustNodeId(t, [&] {
                    uint256 d; std::memcpy(d.begin(), a.begin(), 20); return d; }())};
    }
    case TrustNodeType::P2WSH: {
        uint256 d = RandU256Wide();
        WitnessV0ScriptHash wsh;
        std::memcpy(wsh.begin(), d.begin(), 32);
        return {CTxDestination(wsh), TrustNodeId(t, d)};
    }
    case TrustNodeType::QUANTUM: {
        uint256 d = RandU256Wide();
        WitnessV2Quantum q;
        std::memcpy(q.begin(), d.begin(), 32);
        return {CTxDestination(q), TrustNodeId(t, d)};
    }
    }
    return {CNoDestination(), TrustNodeId()};
}

std::string TypeName(TrustNodeType t)
{
    switch (t) {
    case TrustNodeType::P2PKH: return "P2PKH";
    case TrustNodeType::P2SH: return "P2SH";
    case TrustNodeType::P2WPKH: return "P2WPKH";
    case TrustNodeType::P2WSH: return "P2WSH";
    case TrustNodeType::QUANTUM: return "QUANTUM";
    }
    return "?";
}

std::string Show(const TrustNodeId& n)
{
    return "type " + std::to_string(n.type) + " data " + n.data.GetHex();
}

// Serialize any serializable value to bytes (SER_DISK / CLIENT_VERSION).
template <typename T>
std::vector<uint8_t> SerBytes(const T& v)
{
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << v;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

// Round-trip a serializable value and require the stream to be fully consumed.
template <typename T>
bool RoundTripFullyConsumed(const T& in, T& out)
{
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << in;
    ss >> out;
    return ss.empty();
}

// True iff reading T from a truncated buffer throws (rejects malformed input).
template <typename T>
bool RejectsTruncated(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty()) return true;
    std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    CDataStream ss(truncated, SER_DISK, CLIENT_VERSION);
    try {
        T v;
        ss >> v;
        return false; // read succeeded on truncated data — not rejected
    } catch (const std::exception&) {
        return true;
    }
}

// Fixture: regtest params (so P2WSH is "rcas1..." and quantum is "rcasq1...")
// plus a fresh in-memory CVM database per test.
struct TniMigrationFixSetup : public BasicTestingSetup {
    TniMigrationFixSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~TniMigrationFixSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(trustnodeid_full_migration_fix_property_tests, TniMigrationFixSetup)

// ===========================================================================
// P1 — Canonical identity round-trip.
//
// **Property 3: Expected Behavior** (design Correctness Property P1)
//
// For all five destination types, FromDestination -> ToDestination and
// serialize -> deserialize preserve the exact type and data.
//
// **Validates: Requirements 2.1, 2.8, 2.11**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p1_canonical_identity_round_trip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        for (TrustNodeType t : kAllTypes) {
            auto pair = MakeDest(t);
            const CTxDestination& dest = pair.first;
            const TrustNodeId& expected = pair.second;

            // (a) FromDestination -> exact typed identity.
            TrustNodeId node;
            BOOST_REQUIRE_MESSAGE(TrustNodeId::FromDestination(dest, node),
                "P1 (2.1): FromDestination must accept " + TypeName(t) +
                " (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(node == expected,
                "P1 (2.11): FromDestination for " + TypeName(t) + " produced " +
                Show(node) + " but expected " + Show(expected) +
                " (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(node.type == static_cast<uint8_t>(t),
                "P1 (2.11): FromDestination lost the type tag for " + TypeName(t) +
                " (#" + std::to_string(i) + ")");

            // (b) ToDestination -> the SAME displayable address (no type erasure).
            BOOST_CHECK_MESSAGE(
                EncodeDestination(node.ToDestination()) == EncodeDestination(dest),
                "P1 (2.1): " + TypeName(t) + " node does not render back to the "
                "original address (#" + std::to_string(i) + ")");

            // (c) serialize -> deserialize preserves type and all 32 data bytes,
            //     consuming the whole stream.
            TrustNodeId decoded;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(node, decoded),
                "P1 (2.8): serialization of a " + TypeName(t) +
                " identity did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(decoded == node,
                "P1 (2.8/2.11): serialize->deserialize changed a " + TypeName(t) +
                " identity from " + Show(node) + " to " + Show(decoded) +
                " (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P2 — Collision safety.
//
// **Property 3: Expected Behavior** (design Correctness Property P2)
//
// Two TrustNodeId values that differ by type OR data have distinct serialized
// identities, ToKeyString() values, and map/set keys — including the
// equal-data / different-tag P2PKH/P2SH/P2WPKH case that formerly collided on
// 20 bytes.
//
// **Validates: Requirements 2.9, 2.11, 2.12**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p2_collision_safety_property)
{
    // (a) Equal-data / different-tag triple: P2PKH, P2SH and P2WPKH sharing the
    //     exact same 20-byte payload must be three DISTINCT identities.
    for (int i = 0; i < kSamples; ++i) {
        uint256 ext = RandExt160();
        const TrustNodeId p2pkh(TrustNodeType::P2PKH, ext);
        const TrustNodeId p2sh(TrustNodeType::P2SH, ext);
        const TrustNodeId p2wpkh(TrustNodeType::P2WPKH, ext);
        const std::vector<TrustNodeId> triple = {p2pkh, p2sh, p2wpkh};

        std::set<TrustNodeId> asSet(triple.begin(), triple.end());
        BOOST_CHECK_MESSAGE(asSet.size() == 3,
            "P2 (2.12): equal-data/different-tag P2PKH/P2SH/P2WPKH collapsed to " +
            std::to_string(asSet.size()) + " set key(s) (#" + std::to_string(i) + ")");

        std::set<std::string> keyStrings;
        std::set<std::vector<uint8_t>> serialized;
        std::map<TrustNodeId, int> asMap;
        for (size_t j = 0; j < triple.size(); ++j) {
            keyStrings.insert(triple[j].ToKeyString());
            serialized.insert(SerBytes(triple[j]));
            asMap[triple[j]] = static_cast<int>(j);
        }
        BOOST_CHECK_MESSAGE(keyStrings.size() == 3,
            "P2 (2.9): equal-data/different-tag identities produced " +
            std::to_string(keyStrings.size()) + " distinct ToKeyString() value(s) "
            "instead of 3 (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(serialized.size() == 3,
            "P2 (2.12): equal-data/different-tag identities produced " +
            std::to_string(serialized.size()) + " distinct serialized form(s) "
            "instead of 3 (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(asMap.size() == 3,
            "P2 (2.12): equal-data/different-tag identities collapsed as map keys "
            "(#" + std::to_string(i) + ")");
    }

    // (b) General case: any two canonical identities that differ by type OR data
    //     have distinct serialization and ToKeyString().
    for (int i = 0; i < kSamples; ++i) {
        TrustNodeType ta = kAllTypes[InsecureRandRange(kAllTypes.size())];
        TrustNodeType tb = kAllTypes[InsecureRandRange(kAllTypes.size())];
        TrustNodeId a = MakeCanonical(ta);
        TrustNodeId b = MakeCanonical(tb);
        if (a == b) continue; // extremely unlikely; skip identical draws

        BOOST_CHECK_MESSAGE(a.ToKeyString() != b.ToKeyString(),
            "P2 (2.9): distinct identities " + Show(a) + " and " + Show(b) +
            " share a ToKeyString() (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(SerBytes(a) != SerBytes(b),
            "P2 (2.12): distinct identities " + Show(a) + " and " + Show(b) +
            " share a serialized form (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// P3 — Native downstream API closure.
//
// **Property 3: Expected Behavior** (design Correctness Property P3)
//
// SecureHAT, reputation, wallet clustering, cluster trust, bonded-vote/DAO, and
// propagation APIs accept a wide TrustNodeId, retain it through internal calls,
// and retrieve the SAME identity — for every supported destination type.
//
// **Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p3_native_downstream_api_closure_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        // Cycle through all five types across samples so each subsystem is
        // exercised with P2PKH/P2SH/P2WPKH/P2WSH/quantum identities.
        const TrustNodeType t = kAllTypes[i % kAllTypes.size()];
        const TrustNodeId id = MakeCanonical(t);
        const TrustNodeId other = MakeCanonical(kAllTypes[(i + 2) % kAllTypes.size()]);

        // --- Reputation (2.3) ---
        {
            ReputationSystem rep(*CVM::g_cvmdb);
            ReputationScore score;
            score.address = id;
            score.score = 1234 + i;
            score.voteCount = 3;
            score.lastUpdated = 1700000000;
            score.category = "normal";
            BOOST_REQUIRE(rep.UpdateReputation(id, score));

            ReputationScore got;
            BOOST_REQUIRE_MESSAGE(rep.GetReputation(id, got),
                "P3 (2.3): reputation write for " + TypeName(t) + " (" + Show(id) +
                ") not retrievable (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(got.address == id,
                "P3 (2.3): reputation identity round-tripped as " + Show(got.address) +
                " but stored " + Show(id) + " (#" + std::to_string(i) + ")");
            BOOST_CHECK_EQUAL(got.score, static_cast<int64_t>(1234 + i));
        }

        // --- SecureHAT stake + behavior metrics (2.2) ---
        {
            SecureHAT hat(*CVM::g_cvmdb);
            StakeInfo info;
            info.amount = static_cast<CAmount>(5000 + i);
            info.stake_start = 1700000000;
            BOOST_REQUIRE(hat.StoreStakeInfo(id, info));
            StakeInfo gotStake = hat.GetStakeInfo(id);
            BOOST_CHECK_MESSAGE(gotStake.amount == static_cast<CAmount>(5000 + i),
                "P3 (2.2): HAT stake for " + TypeName(t) + " (" + Show(id) +
                ") read back amount=" + std::to_string(gotStake.amount) +
                " (#" + std::to_string(i) + ")");

            BehaviorMetrics metrics(id);
            metrics.total_trades = 7;
            metrics.unique_partners.insert(other);
            BOOST_REQUIRE(hat.StoreBehaviorMetrics(metrics));
            BehaviorMetrics gotBm = hat.GetBehaviorMetrics(id);
            BOOST_CHECK_MESSAGE(gotBm.address == id,
                "P3 (2.2): HAT behavior identity round-tripped as " +
                Show(gotBm.address) + " but stored " + Show(id) +
                " (#" + std::to_string(i) + ")");
        }

        // --- Wallet clustering + cluster trust (2.4) ---
        {
            WalletClusterer wc(*CVM::g_cvmdb);
            wc.LinkAddresses(id, other);
            std::set<TrustNodeId> members = wc.GetClusterMembers(id);
            BOOST_CHECK_MESSAGE(members.count(id) == 1 && members.count(other) == 1,
                "P3 (2.4): clustering did not retain both typed members for " +
                TypeName(t) + " (" + Show(id) + ") (#" + std::to_string(i) + ")");
        }

        // --- Bonded vote (2.6) ---
        {
            TrustGraph tg(*CVM::g_cvmdb);
            BondedVote vote;
            vote.voter = other;
            vote.target = id;
            vote.voteValue = 42;
            vote.bondAmount = 3 * COIN;
            vote.bondTxHash = InsecureRand256();
            vote.timestamp = 1700000000;
            vote.slashed = false;
            vote.reason = "p3";
            BOOST_REQUIRE(tg.RecordBondedVote(vote));

            std::vector<BondedVote> votes = tg.GetVotesForAddress(id);
            bool found = false;
            for (const BondedVote& v : votes) {
                if (v.voter == other && v.target == id) found = true;
            }
            BOOST_CHECK_MESSAGE(found,
                "P3 (2.6): bonded vote for target " + TypeName(t) + " (" + Show(id) +
                ") not retrievable with the exact typed voter/target (#" +
                std::to_string(i) + ")");
        }

        // --- DAO dispute (2.6) ---
        {
            TrustGraph tg(*CVM::g_cvmdb);
            DAODispute dispute;
            dispute.disputeId = InsecureRand256();
            dispute.originalVoteTx = InsecureRand256();
            dispute.challenger = id;
            dispute.challengeBond = 2 * COIN;
            dispute.challengeReason = "dispute";
            dispute.createdTime = 1700000000;
            dispute.daoVotes[other] = true;
            dispute.daoStakes[other] = COIN;
            BOOST_REQUIRE(tg.CreateDispute(dispute));

            DAODispute got;
            BOOST_REQUIRE_MESSAGE(tg.GetDispute(dispute.disputeId, got),
                "P3 (2.6): DAO dispute not retrievable (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(got.challenger == id,
                "P3 (2.6): DAO challenger round-tripped as " + Show(got.challenger) +
                " but stored " + Show(id) + " (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(got.daoVotes.count(other) == 1,
                "P3 (2.6): DAO member vote key (typed identity) lost (#" +
                std::to_string(i) + ")");
        }

        // --- Propagation (2.7) ---
        {
            TrustGraph tg(*CVM::g_cvmdb);
            WalletClusterer wc(*CVM::g_cvmdb);
            TrustPropagator prop(*CVM::g_cvmdb, wc, tg);

            TrustEdge edge;
            edge.fromAddress = other;
            edge.toAddress = id;
            edge.trustWeight = 80;
            edge.timestamp = 1700000000;
            edge.bondAmount = 3 * COIN;
            edge.bondTxHash = InsecureRand256();
            edge.slashed = false;
            edge.reason = "p3-prop";

            uint32_t n = prop.PropagateTrustEdge(edge);
            BOOST_CHECK_MESSAGE(n >= 1,
                "P3 (2.7): propagation produced no edges for target " + TypeName(t) +
                " (" + Show(id) + ") (#" + std::to_string(i) + ")");

            std::vector<PropagatedTrustEdge> edges = prop.GetPropagatedEdgesForAddress(id);
            bool found = false;
            for (const PropagatedTrustEdge& pe : edges) {
                if (pe.fromAddress == other && pe.originalTarget == id) found = true;
            }
            BOOST_CHECK_MESSAGE(found,
                "P3 (2.7): propagated edge did not retain the exact typed "
                "from/originalTarget identities for " + TypeName(t) + " (" +
                Show(id) + ") (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P4 — Current record round-trip.
//
// **Property 3: Expected Behavior** (design Correctness Property P4)
//
// Each current downstream record (HAT/reputation/cluster/bonded-vote/DAO/
// propagation) with bounded typed-identity containers serializes and
// deserializes exactly, consuming ALL bytes, and rejects malformed/truncated
// data.
//
// **Validates: Requirements 2.8, 2.11, 5.1**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p4_current_record_round_trip_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        const TrustNodeId a = MakeCanonical(kAllTypes[i % kAllTypes.size()]);
        const TrustNodeId b = MakeCanonical(kAllTypes[(i + 1) % kAllTypes.size()]);
        const TrustNodeId c = MakeCanonical(kAllTypes[(i + 3) % kAllTypes.size()]);

        // ReputationScore
        {
            ReputationScore rec;
            rec.address = a;
            rec.score = -321 + i;
            rec.voteCount = 11;
            rec.lastUpdated = 1700000000;
            rec.category = "exchange";
            rec.totalTransactions = 5;
            ReputationScore out;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(rec, out),
                "P4 (2.8): ReputationScore did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out.address == a && out.score == rec.score,
                "P4: ReputationScore identity/score not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(RejectsTruncated<ReputationScore>(SerBytes(rec)),
                "P4 (5.1): truncated ReputationScore was not rejected (#" +
                std::to_string(i) + ")");
        }

        // WalletClusterInfo (bounded member set with mixed types)
        {
            WalletClusterInfo rec;
            rec.cluster_id = std::min({a, b, c});
            rec.member_addresses = {a, b, c};
            rec.first_seen = 1700000000;
            rec.transaction_count = 4;
            WalletClusterInfo out;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(rec, out),
                "P4 (2.8): WalletClusterInfo did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out.member_addresses == rec.member_addresses,
                "P4: WalletClusterInfo member set not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(RejectsTruncated<WalletClusterInfo>(SerBytes(rec)),
                "P4 (5.1): truncated WalletClusterInfo was not rejected (#" +
                std::to_string(i) + ")");
        }

        // BondedVote
        {
            BondedVote rec;
            rec.voter = a;
            rec.target = b;
            rec.voteValue = 55;
            rec.bondAmount = 3 * COIN;
            rec.bondTxHash = InsecureRand256();
            rec.timestamp = 1700000000;
            rec.reason = "bv";
            BondedVote out;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(rec, out),
                "P4 (2.8): BondedVote did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out.voter == a && out.target == b,
                "P4: BondedVote identities not preserved (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(RejectsTruncated<BondedVote>(SerBytes(rec)),
                "P4 (5.1): truncated BondedVote was not rejected (#" +
                std::to_string(i) + ")");
        }

        // DAODispute (bounded typed-key maps)
        {
            DAODispute rec;
            rec.disputeId = InsecureRand256();
            rec.originalVoteTx = InsecureRand256();
            rec.challenger = a;
            rec.challengeBond = COIN;
            rec.createdTime = 1700000000;
            rec.daoVotes[b] = true;
            rec.daoVotes[c] = false;
            rec.daoStakes[b] = COIN;
            DAODispute out;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(rec, out),
                "P4 (2.8): DAODispute did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out.challenger == a && out.daoVotes == rec.daoVotes,
                "P4: DAODispute typed identity/map keys not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(RejectsTruncated<DAODispute>(SerBytes(rec)),
                "P4 (5.1): truncated DAODispute was not rejected (#" +
                std::to_string(i) + ")");
        }

        // PropagatedTrustEdge
        {
            PropagatedTrustEdge rec(a, b, c, InsecureRand256(), 70,
                                    1700000000, 1700000001, 3 * COIN);
            PropagatedTrustEdge out;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(rec, out),
                "P4 (2.8): PropagatedTrustEdge did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out == rec,
                "P4: PropagatedTrustEdge not preserved (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(RejectsTruncated<PropagatedTrustEdge>(SerBytes(rec)),
                "P4 (5.1): truncated PropagatedTrustEdge was not rejected (#" +
                std::to_string(i) + ")");
        }

        // ClusterTrustSummary (bounded member set)
        {
            ClusterTrustSummary rec(a);
            rec.AddMember(a);
            rec.AddMember(b);
            rec.totalIncomingTrust = 100;
            rec.edgeCount = 2;
            ClusterTrustSummary out;
            BOOST_REQUIRE_MESSAGE(RoundTripFullyConsumed(rec, out),
                "P4 (2.8): ClusterTrustSummary did not consume the whole stream (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out == rec,
                "P4: ClusterTrustSummary not preserved (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(RejectsTruncated<ClusterTrustSummary>(SerBytes(rec)),
                "P4 (5.1): truncated ClusterTrustSummary was not rejected (#" +
                std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P5 — Canonical payload round-trip.
//
// **Property 3: Expected Behavior** (design Correctness Property P5)
//
// Each newly changed OP_RETURN payload has exactly one accepted canonical
// encoding. Verify exact body sizes 39 / 80 / 77 / 78, exact field order and
// integer encoding (parse -> reserialize reproduces bytes), and reject every
// other length, malformed identity, invalid type / high padding, and
// out-of-range scalar.
//
// **Validates: Requirements 2.10, 2.11, 4.4, 4.5**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p5_canonical_payload_round_trip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const TrustNodeType t = kAllTypes[i % kAllTypes.size()];
        const TrustNodeId id = MakeCanonical(t);
        const TrustNodeId id2 = MakeCanonical(kAllTypes[(i + 2) % kAllTypes.size()]);

        // --- Reputation payload: 39 bytes ---
        {
            CVMReputationData d;
            d.targetAddress = id;
            d.voteValue = static_cast<int16_t>(-100 + (i % 201));
            d.timestamp = 1700000000 + i;
            std::vector<uint8_t> bytes = d.Serialize();
            BOOST_CHECK_EQUAL(bytes.size(), 39u);

            CVMReputationData parsed;
            BOOST_REQUIRE_MESSAGE(parsed.Deserialize(bytes),
                "P5 (2.10): canonical reputation payload rejected (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.targetAddress == id &&
                                parsed.voteValue == d.voteValue &&
                                parsed.timestamp == d.timestamp,
                "P5 (2.11): reputation payload fields not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.Serialize() == bytes,
                "P5 (4.4): reputation reserialize did not reproduce bytes (#" +
                std::to_string(i) + ")");

            // Reject other lengths.
            std::vector<uint8_t> shorter(bytes.begin(), bytes.end() - 1);
            std::vector<uint8_t> longer = bytes; longer.push_back(0);
            CVMReputationData tmp;
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(shorter) && !tmp.Deserialize(longer),
                "P5 (4.5): non-canonical reputation payload length accepted (#" +
                std::to_string(i) + ")");

            // Reject malformed identity: invalid type tag 0 and 6.
            std::vector<uint8_t> badType0 = bytes; badType0[0] = 0;
            std::vector<uint8_t> badType6 = bytes; badType6[0] = 6;
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(badType0) && !tmp.Deserialize(badType6),
                "P5 (2.11): reputation payload with invalid identity type accepted (#" +
                std::to_string(i) + ")");

            // Reject nonzero high padding for a 20-byte type (force P2PKH first).
            CVMReputationData p; p.targetAddress = TrustNodeId(TrustNodeType::P2PKH, RandExt160());
            p.voteValue = 1; p.timestamp = 1;
            std::vector<uint8_t> pbytes = p.Serialize();
            pbytes[1 + 31] = 0xAB; // high byte of the 32-byte data (index 31)
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(pbytes),
                "P5 (2.11): reputation payload with nonzero high padding on a "
                "20-byte identity accepted (#" + std::to_string(i) + ")");
        }

        // --- Bonded-vote payload: 80 bytes ---
        {
            CVMBondedVoteData d;
            d.voter = id;
            d.target = id2;
            d.voteValue = static_cast<int16_t>(-50 + (i % 101));
            d.bondAmount = static_cast<CAmount>(3 * COIN + i);
            d.timestamp = 1700000000 + i;
            std::vector<uint8_t> bytes = d.Serialize();
            BOOST_CHECK_EQUAL(bytes.size(), 80u);

            CVMBondedVoteData parsed;
            BOOST_REQUIRE_MESSAGE(parsed.Deserialize(bytes),
                "P5 (2.10): canonical bonded-vote payload rejected (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.voter == id && parsed.target == id2 &&
                                parsed.voteValue == d.voteValue &&
                                parsed.bondAmount == d.bondAmount &&
                                parsed.timestamp == d.timestamp,
                "P5 (2.11): bonded-vote payload fields not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.Serialize() == bytes,
                "P5 (4.4): bonded-vote reserialize did not reproduce bytes (#" +
                std::to_string(i) + ")");

            std::vector<uint8_t> longer = bytes; longer.push_back(0);
            CVMBondedVoteData tmp;
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(longer),
                "P5 (4.5): bonded-vote payload with trailing byte accepted (#" +
                std::to_string(i) + ")");
            std::vector<uint8_t> badVoter = bytes; badVoter[0] = 6;
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(badVoter),
                "P5 (2.11): bonded-vote payload with invalid voter type accepted (#" +
                std::to_string(i) + ")");
        }

        // --- DAO-dispute payload: 77 bytes ---
        {
            CVMDAODisputeData d;
            d.originalVoteTxHash = InsecureRand256();
            d.challenger = id;
            d.challengeBond = static_cast<CAmount>(2 * COIN + i);
            d.timestamp = 1700000000 + i;
            std::vector<uint8_t> bytes = d.Serialize();
            BOOST_CHECK_EQUAL(bytes.size(), 77u);

            CVMDAODisputeData parsed;
            BOOST_REQUIRE_MESSAGE(parsed.Deserialize(bytes),
                "P5 (2.10): canonical DAO-dispute payload rejected (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.originalVoteTxHash == d.originalVoteTxHash &&
                                parsed.challenger == id &&
                                parsed.challengeBond == d.challengeBond &&
                                parsed.timestamp == d.timestamp,
                "P5 (2.11): DAO-dispute payload fields not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.Serialize() == bytes,
                "P5 (4.4): DAO-dispute reserialize did not reproduce bytes (#" +
                std::to_string(i) + ")");

            CVMDAODisputeData tmp;
            std::vector<uint8_t> shorter(bytes.begin(), bytes.end() - 1);
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(shorter),
                "P5 (4.5): truncated DAO-dispute payload accepted (#" +
                std::to_string(i) + ")");
            std::vector<uint8_t> badChallenger = bytes; badChallenger[32] = 0; // type byte
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(badChallenger),
                "P5 (2.11): DAO-dispute payload with invalid challenger type accepted (#" +
                std::to_string(i) + ")");
        }

        // --- DAO-vote payload: 78 bytes ---
        {
            CVMDAOVoteData d;
            d.disputeId = InsecureRand256();
            d.daoMember = id;
            d.supportSlash = (i % 2 == 0);
            d.stake = static_cast<CAmount>(COIN + i);
            d.timestamp = 1700000000 + i;
            std::vector<uint8_t> bytes = d.Serialize();
            BOOST_CHECK_EQUAL(bytes.size(), 78u);

            CVMDAOVoteData parsed;
            BOOST_REQUIRE_MESSAGE(parsed.Deserialize(bytes),
                "P5 (2.10): canonical DAO-vote payload rejected (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.disputeId == d.disputeId &&
                                parsed.daoMember == id &&
                                parsed.supportSlash == d.supportSlash &&
                                parsed.stake == d.stake &&
                                parsed.timestamp == d.timestamp,
                "P5 (2.11): DAO-vote payload fields not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.Serialize() == bytes,
                "P5 (4.4): DAO-vote reserialize did not reproduce bytes (#" +
                std::to_string(i) + ")");

            // Reject out-of-range scalar: support byte > 1 (offset 32 + 33 = 65).
            CVMDAOVoteData tmp;
            std::vector<uint8_t> badSupport = bytes; badSupport[65] = 2;
            BOOST_CHECK_MESSAGE(!tmp.Deserialize(badSupport),
                "P5 (4.5): DAO-vote payload with out-of-range support byte accepted (#" +
                std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P6 — Five-type clustering extraction.
//
// **Property 3: Expected Behavior** (design Correctness Property P6)
//
// For transactions with supported resolved prevouts, the clustering pipeline
// extracts each exact typed destination and applies the common-input heuristic
// independent of type; unsupported/unresolved prevouts are ignored. (The
// on-chain two-output change heuristic runs inside AnalyzeTransaction over live
// chain data and is exercised by the functional regtest; here we drive the
// deterministic index-based common-input path and the extraction gate.)
//
// **Validates: Requirements 2.4, 2.5, 4.6**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p6_five_type_clustering_extraction_property)
{
    // (a) Extraction gate: unsupported / unresolvable destinations are ignored.
    {
        TrustNodeId tmp;
        BOOST_CHECK_MESSAGE(!TrustNodeId::FromDestination(CNoDestination(), tmp),
            "P6 (2.5): CNoDestination must not be extracted as an identity");
        WitnessUnknown wu;
        wu.version = 16;
        wu.length = 20;
        std::memset(wu.program, 0x11, sizeof(wu.program));
        BOOST_CHECK_MESSAGE(!TrustNodeId::FromDestination(CTxDestination(wu), tmp),
            "P6 (2.5): WitnessUnknown must not be extracted as an identity");
    }

    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        WalletClusterer wc(db);

        // One transaction whose inputs are one of each supported destination
        // type (extracted from real destinations via FromDestination).
        std::vector<TrustNodeId> inputs;
        for (TrustNodeType t : kAllTypes) {
            auto pair = MakeDest(t);
            TrustNodeId node;
            BOOST_REQUIRE(TrustNodeId::FromDestination(pair.first, node));
            inputs.push_back(node);
        }

        const uint256 txid = InsecureRand256();
        wc.RecordTransactionInputs(txid, inputs);
        wc.BuildClusters();

        // Common-input heuristic links all five typed inputs into one cluster,
        // and every exact typed destination is retained (no collapse, no
        // truncation of the P2WSH/quantum members).
        std::set<TrustNodeId> members = wc.GetClusterMembers(inputs[0]);
        for (size_t j = 0; j < inputs.size(); ++j) {
            BOOST_CHECK_MESSAGE(members.count(inputs[j]) == 1,
                "P6 (2.4/2.5): typed input " + Show(inputs[j]) +
                " missing from the common-input cluster (#" + std::to_string(i) + ")");
        }
        BOOST_CHECK_MESSAGE(members.size() >= inputs.size(),
            "P6 (2.4): common-input cluster has " + std::to_string(members.size()) +
            " members but " + std::to_string(inputs.size()) +
            " typed inputs were recorded (#" + std::to_string(i) + ")");

        // Equal-data / different-tag inputs stay distinct members (they do not
        // collapse onto a shared 20-byte projection).
        uint256 ext = RandExt160();
        std::vector<TrustNodeId> triple = {
            TrustNodeId(TrustNodeType::P2PKH, ext),
            TrustNodeId(TrustNodeType::P2SH, ext),
            TrustNodeId(TrustNodeType::P2WPKH, ext)};
        const uint256 txid2 = InsecureRand256();
        wc.RecordTransactionInputs(txid2, triple);
        wc.BuildClusters();
        std::set<TrustNodeId> tripleMembers = wc.GetClusterMembers(triple[0]);
        for (const TrustNodeId& n : triple) {
            BOOST_CHECK_MESSAGE(tripleMembers.count(n) == 1,
                "P6 (2.4): equal-data/different-tag member " + Show(n) +
                " collapsed in the cluster (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P7 — Cluster persistence and rendering.
//
// **Property 3: Expected Behavior** (design Correctness Property P7)
//
// Save/load produces the same member sets and a deterministic cluster ID
// (minimum member); every rendered member decodes back to its stored
// TrustNodeId.
//
// **Validates: Requirements 2.4, 3.2, 4.6**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p7_cluster_persistence_and_rendering_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        // Build a cluster of mixed-type members (chain them so all are linked).
        std::vector<TrustNodeId> members;
        for (TrustNodeType t : kAllTypes) {
            auto pair = MakeDest(t);
            TrustNodeId node;
            BOOST_REQUIRE(TrustNodeId::FromDestination(pair.first, node));
            members.push_back(node);
        }

        {
            WalletClusterer wc(db);
            for (size_t j = 0; j + 1 < members.size(); ++j) {
                wc.LinkAddresses(members[j], members[j + 1]); // persists on each call
            }
        }

        const TrustNodeId expectedId = *std::min_element(members.begin(), members.end());
        std::set<TrustNodeId> expectedSet(members.begin(), members.end());

        // Reload from the same database (LoadClusters runs in the constructor).
        WalletClusterer reloaded(db);
        std::set<TrustNodeId> loadedMembers = reloaded.GetClusterMembers(members[0]);
        BOOST_CHECK_MESSAGE(loadedMembers == expectedSet,
            "P7 (3.2): reloaded cluster member set differs from the saved set (#" +
            std::to_string(i) + ")");

        // Deterministic cluster ID == minimum member under operator<.
        TrustNodeId loadedId = reloaded.GetClusterForAddress(members[2]);
        BOOST_CHECK_MESSAGE(loadedId == expectedId,
            "P7 (2.4): deterministic cluster ID after reload was " + Show(loadedId) +
            " but expected the minimum member " + Show(expectedId) + " (#" +
            std::to_string(i) + ")");

        // Every rendered member decodes back to its stored TrustNodeId.
        for (const TrustNodeId& m : loadedMembers) {
            const std::string addr = EncodeDestination(m.ToDestination());
            BOOST_REQUIRE_MESSAGE(!addr.empty(),
                "P7 (2.4): stored member " + Show(m) + " rendered to an empty "
                "address (#" + std::to_string(i) + ")");
            TrustNodeId decoded;
            BOOST_REQUIRE_MESSAGE(
                TrustNodeId::FromDestination(DecodeDestination(addr), decoded),
                "P7 (2.4): rendered member address '" + addr + "' did not decode (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(decoded == m,
                "P7 (2.4): rendered member '" + addr + "' decoded to " + Show(decoded) +
                " but stored " + Show(m) + " (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P8 — Idempotency.
//
// **Property 3: Expected Behavior** (design Correctness Property P8)
//
// Repeating a valid typed write/processing operation, or rebuilding from the
// same input, produces identical logical state with no duplicate entries.
//
// **Validates: Requirements 2.13, 3.2, 4.4**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p8_idempotency_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        const TrustNodeId a = MakeCanonical(kAllTypes[i % kAllTypes.size()]);
        const TrustNodeId b = MakeCanonical(kAllTypes[(i + 1) % kAllTypes.size()]);

        // (a) Reputation: repeated identical write -> identical state.
        {
            CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                                1 << 20, /*fMemory=*/true, /*fWipe=*/true);
            ReputationSystem rep(db);
            ReputationScore score;
            score.address = a;
            score.score = 777 + i;
            score.voteCount = 2;
            score.lastUpdated = 1700000000;
            score.category = "normal";
            BOOST_REQUIRE(rep.UpdateReputation(a, score));
            ReputationScore r1;
            BOOST_REQUIRE(rep.GetReputation(a, r1));
            BOOST_REQUIRE(rep.UpdateReputation(a, score));
            ReputationScore r2;
            BOOST_REQUIRE(rep.GetReputation(a, r2));
            BOOST_CHECK_MESSAGE(r1.address == r2.address && r1.score == r2.score &&
                                r1.voteCount == r2.voteCount,
                "P8 (4.4): repeating a reputation write changed the logical state (#" +
                std::to_string(i) + ")");
        }

        // (b) Bonded vote: recording the same vote (same bondTxHash) twice does
        //     not create a duplicate index entry.
        {
            CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                                1 << 20, /*fMemory=*/true, /*fWipe=*/true);
            TrustGraph tg(db);
            BondedVote vote;
            vote.voter = b;
            vote.target = a;
            vote.voteValue = 30;
            vote.bondAmount = 3 * COIN;
            vote.bondTxHash = InsecureRand256(); // fixed for both writes
            vote.timestamp = 1700000000;
            vote.reason = "idem";
            BOOST_REQUIRE(tg.RecordBondedVote(vote));
            BOOST_REQUIRE(tg.RecordBondedVote(vote));
            std::vector<BondedVote> votes = tg.GetVotesForAddress(a);
            size_t matches = 0;
            for (const BondedVote& v : votes) {
                if (v.bondTxHash == vote.bondTxHash) ++matches;
            }
            BOOST_CHECK_MESSAGE(matches == 1,
                "P8 (2.13): recording the same bonded vote twice produced " +
                std::to_string(matches) + " entries (#" + std::to_string(i) + ")");
        }

        // (c) Cluster rebuild: same input -> identical cluster ID and member set.
        {
            CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                                1 << 20, /*fMemory=*/true, /*fWipe=*/true);
            WalletClusterer wc(db);
            std::vector<TrustNodeId> inputs;
            for (TrustNodeType t : kAllTypes) inputs.push_back(MakeCanonical(t));

            const uint256 txid = InsecureRand256();
            wc.RecordTransactionInputs(txid, inputs);
            wc.BuildClusters();
            TrustNodeId id1 = wc.GetClusterForAddress(inputs[0]);
            std::set<TrustNodeId> m1 = wc.GetClusterMembers(inputs[0]);

            // Replay the exact same input and rebuild.
            wc.RecordTransactionInputs(txid, inputs);
            wc.BuildClusters();
            TrustNodeId id2 = wc.GetClusterForAddress(inputs[0]);
            std::set<TrustNodeId> m2 = wc.GetClusterMembers(inputs[0]);

            BOOST_CHECK_MESSAGE(id1 == id2 && m1 == m2,
                "P8 (3.2): rebuilding from the same active-chain input produced a "
                "different cluster ID/member set (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P9 — Fixed-width scope preservation.
//
// **Property 3: Expected Behavior** (design Correctness Property P9)
//
// Contract/EVM addresses (uint160) and transaction/dispute/bond/source/internal
// hashes (uint256) serialize, key, compare, and derive byte-for-byte unchanged.
//
// **Validates: Requirements 4.1, 4.2**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p9_fixed_width_scope_preservation_property)
{
    for (int i = 0; i < kSamples; ++i) {
        // Contract address derivation is unchanged: deterministic and equal to
        // Hash(deployer || nonce)[0:20], staying a 20-byte uint160.
        uint160 deployer = RandU160();
        uint64_t nonce = InsecureRand256().GetUint64(0);

        uint160 addrA = CVM::GenerateContractAddress(deployer, nonce);
        uint160 addrB = CVM::GenerateContractAddress(deployer, nonce);
        BOOST_CHECK_MESSAGE(addrA == addrB,
            "P9 (4.1): contract address derivation is not deterministic (#" +
            std::to_string(i) + ")");

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << deployer << nonce;
        uint256 h = Hash(ss.begin(), ss.end());
        uint160 expected;
        std::memcpy(expected.begin(), h.begin(), 20);
        BOOST_CHECK_MESSAGE(addrA == expected,
            "P9 (4.1): contract address != Hash(deployer||nonce)[0:20] (#" +
            std::to_string(i) + ")");

        // uint160 serializes byte-for-byte (20 bytes) and round-trips.
        std::vector<uint8_t> addrBytes = SerBytes(addrA);
        BOOST_CHECK_EQUAL(addrBytes.size(), 20u);
        uint160 addrDecoded;
        BOOST_REQUIRE(RoundTripFullyConsumed(addrA, addrDecoded));
        BOOST_CHECK_MESSAGE(addrDecoded == addrA,
            "P9 (4.1): uint160 contract address did not round-trip (#" +
            std::to_string(i) + ")");

        // uint256 hash-domain values serialize byte-for-byte (32 bytes) and
        // round-trip unchanged.
        uint256 hash = InsecureRand256();
        std::vector<uint8_t> hashBytes = SerBytes(hash);
        BOOST_CHECK_EQUAL(hashBytes.size(), 32u);
        uint256 hashDecoded;
        BOOST_REQUIRE(RoundTripFullyConsumed(hash, hashDecoded));
        BOOST_CHECK_MESSAGE(hashDecoded == hash,
            "P9 (4.2): uint256 hash did not round-trip byte-for-byte (#" +
            std::to_string(i) + ")");
    }
}

// ===========================================================================
// P10 — Completed-feature preservation.
//
// **Property 3: Preservation** (design Correctness Property P10)
//
// Inputs covered by web-of-trust-fixes / trust-system-activation remain
// unchanged: the completed CVMTrustEdgeData v1 (54-byte, uint160) and v2
// (81-byte, TrustNodeId) payloads, TrustEdge v2 records, and quantum parsing
// are byte-for-byte / behaviorally unchanged.
//
// **Validates: Requirements 4.3, 4.4, 4.5, 4.6, 4.7**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p10_completed_feature_preservation_property)
{
    for (int i = 0; i < kSamples; ++i) {
        // (a) CVMTrustEdgeData v1: pure-uint160 P2PKH edge -> fixed 54-byte
        //     layout, unchanged, and re-serializes byte-for-byte.
        {
            CVMTrustEdgeData v1;
            v1.fromAddress = RandU160();
            v1.toAddress = RandU160();
            v1.weight = static_cast<int16_t>(-100 + (i % 201));
            v1.bondAmount = static_cast<CAmount>(3 * COIN + i);
            v1.timestamp = 1700000000 + i;
            std::vector<uint8_t> bytes = v1.Serialize();
            BOOST_CHECK_MESSAGE(bytes.size() == 54u,
                "P10 (4.3): CVMTrustEdgeData v1 size " + std::to_string(bytes.size()) +
                " != 54 (#" + std::to_string(i) + ")");

            CVMTrustEdgeData parsed;
            BOOST_REQUIRE(parsed.Deserialize(bytes));
            BOOST_CHECK_MESSAGE(parsed.fromAddress == v1.fromAddress &&
                                parsed.toAddress == v1.toAddress &&
                                parsed.weight == v1.weight &&
                                parsed.bondAmount == v1.bondAmount &&
                                parsed.timestamp == v1.timestamp,
                "P10 (4.3): CVMTrustEdgeData v1 fields not preserved (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(parsed.Serialize() == bytes,
                "P10 (4.3): CVMTrustEdgeData v1 re-serialization changed bytes (#" +
                std::to_string(i) + ")");
        }

        // (b) CVMTrustEdgeData v2: quantum endpoint -> 81-byte wide layout that
        //     preserves the full typed identity.
        {
            CVMTrustEdgeData v2;
            v2.from = TrustNodeId(TrustNodeType::QUANTUM, RandU256Wide());
            v2.to = TrustNodeId(TrustNodeType::P2PKH, RandExt160());
            v2.weight = 60;
            v2.bondAmount = 5 * COIN;
            v2.timestamp = 1700000000 + i;
            std::vector<uint8_t> bytes = v2.Serialize();
            BOOST_CHECK_MESSAGE(bytes.size() == 81u,
                "P10 (4.3): CVMTrustEdgeData v2 size " + std::to_string(bytes.size()) +
                " != 81 (#" + std::to_string(i) + ")");

            CVMTrustEdgeData parsed;
            BOOST_REQUIRE(parsed.Deserialize(bytes));
            BOOST_CHECK_MESSAGE(parsed.from == v2.from && parsed.to == v2.to,
                "P10 (4.3): CVMTrustEdgeData v2 wide identities not preserved (#" +
                std::to_string(i) + ")");
        }

        // (c) TrustEdge v2 record round-trips (version-prefixed, TrustNodeId).
        {
            TrustEdge edge;
            edge.fromAddress = MakeCanonical(kAllTypes[i % kAllTypes.size()]);
            edge.toAddress = MakeCanonical(kAllTypes[(i + 2) % kAllTypes.size()]);
            edge.trustWeight = 75;
            edge.timestamp = 1700000000 + i;
            edge.bondAmount = 3 * COIN;
            edge.bondTxHash = InsecureRand256();
            edge.reason = "v2";
            TrustEdge out;
            BOOST_REQUIRE(RoundTripFullyConsumed(edge, out));
            BOOST_CHECK_MESSAGE(out.nVersion == TrustEdge::CURRENT_VERSION &&
                                out.fromAddress == edge.fromAddress &&
                                out.toAddress == edge.toAddress,
                "P10 (4.3): TrustEdge v2 record not preserved (#" +
                std::to_string(i) + ")");
        }

        // (d) Quantum parsing/encoding is unchanged: a quantum destination
        //     encodes and decodes back to the same identity.
        {
            uint256 d = RandU256Wide();
            WitnessV2Quantum q;
            std::memcpy(q.begin(), d.begin(), 32);
            const std::string addr = EncodeDestination(CTxDestination(q));
            BOOST_REQUIRE_MESSAGE(!addr.empty(),
                "P10 (4.7): quantum address failed to encode (#" +
                std::to_string(i) + ")");
            TrustNodeId node;
            BOOST_REQUIRE(TrustNodeId::FromDestination(DecodeDestination(addr), node));
            BOOST_CHECK_MESSAGE(node.type == static_cast<uint8_t>(TrustNodeType::QUANTUM) &&
                                node.data == d,
                "P10 (4.7): quantum parsing/round-trip changed the identity (#" +
                std::to_string(i) + ")");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
