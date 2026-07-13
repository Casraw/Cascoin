// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 8 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 19: "Write Workstream-8 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 8 = Distributed-consensus signatures & state sync
 * (bugfix.md clauses 1.37, 1.38, 1.39; preservation 3.15).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 15 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.37, 2.38,
 *       2.39). Written BEFORE the fix, they are EXPECTED TO FAIL on the current
 *       (unfixed) code — every failure is a counterexample confirming a defect.
 *       After the Workstream-8 fix lands (task 20) the SAME tests must pass.
 *       Prefixed `p15_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clause 3.15). These
 *       capture behaviour Workstream 8 must NOT change: a cross-chain
 *       attestation carrying a genuinely valid signature over genuinely
 *       committed state is still accepted. They are EXPECTED TO PASS on the
 *       unfixed code (baseline behaviour to preserve). Prefixed `preserve_`.
 *
 * ---------------------------------------------------------------------------
 * Defects under test and their testability:
 *
 *   1.37  consensus_safety.cpp ConsensusSafetyValidator::VerifyAttestationSignature
 *         only checks the signature length is between 64 and 128 bytes and does
 *         NOT verify it against the attestor's public key, so a FORGED signature
 *         of valid length is accepted.
 *         COVERED: VerifyAttestationSignature is a public method — a forged
 *         signature of valid length (with a real attestor pubkey) must be
 *         REJECTED after the fix.
 *
 *   1.38  consensus_safety.cpp ConsensusSafetyValidator::GetTrustGraphDelta
 *         returns an empty delta and does not query the database for actual
 *         changes (the DB-iteration body is a placeholder).
 *         COVERED: after real trust-graph changes are committed, a delta
 *         computed since an earlier block must be NON-empty after the fix.
 *         PARTIAL/DEFERRED: the "request the delta from a peer" half of 2.38
 *         is a P2P path with no unit-level seam (needs a live CConnman/CNode);
 *         it is covered by the Workstream-8 fix (task 20) and multi-node
 *         integration (task 30). No un-compilable code is emitted for it here.
 *
 *   1.39  trust_graph_sync.cpp TrustGraphSyncManager::VerifyState / ApplyDelta
 *         return false when no ConsensusSafetyValidator is configured, so
 *         trust-graph synchronization silently fails.
 *         COVERED: with a real DB + trust graph but NO configured validator,
 *         verify/apply must operate against real committed state (ApplyDelta
 *         succeeds; a self-consistent VerifyState succeeds) after the fix.
 *
 * Expected-Behavior targets: 2.37, 2.38, 2.39
 * Preservation: 3.15
 * Requirements: 1.37, 1.38, 1.39, 3.15
 */

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <cvm/consensus_safety.h>
#include <cvm/trust_graph_sync.h>
#include <cvm/trustgraph.h>
#include <cvm/trust_attestation.h>
#include <cvm/cvmdb.h>

#include <amount.h>
#include <fs.h>
#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <utiltime.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

static constexpr int kSamples = 32;

// Fresh in-memory CVM database for a single test.
std::unique_ptr<CVM::CVMDatabase> MakeTempDb()
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    return std::unique_ptr<CVM::CVMDatabase>(
        new CVM::CVMDatabase(testPath, 8 << 20, /*fMemory=*/true, /*fWipe=*/true));
}

// Build a random non-null 20-byte address.
uint160 RandAddress()
{
    uint160 addr;
    uint256 r = InsecureRand256();
    std::memcpy(addr.begin(), r.begin(), 20);
    if (addr.IsNull()) *addr.begin() = 0x01;
    return addr;
}

// The exact message hash that ConsensusSafetyValidator::VerifyAttestationSignature
// computes over (address, trustScore, timestamp, sourceChainId). Signing over
// this hash yields a signature a real ECDSA verification would accept.
uint256 AttestationMessageHash(const CVM::TrustAttestation& att)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << att.address;
    ss << att.trustScore;
    ss << att.timestamp;
    ss << att.sourceChainId;
    return ss.GetHash();
}

// A fresh, in-range attestation (no signature material yet).
CVM::TrustAttestation MakeBaseAttestation(const uint160& address, int16_t score)
{
    CVM::TrustAttestation att;
    att.address = address;
    att.trustScore = score;                     // in [0,100]
    att.source = CVM::AttestationSource::OTHER;
    att.sourceChainId.SetHex(
        "0x0000000000000000000000000000000000000000000000000000000000000001");
    att.timestamp = static_cast<uint64_t>(GetTime());
    return att;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream8_tests, BasicTestingSetup)

// ###########################################################################
// #  FIX-PROPERTY TESTS (Property 15) — EXPECTED TO FAIL on unfixed code     #
// ###########################################################################

// ===========================================================================
// Property 15 (1.37) — attestation signature verified vs. attestor pubkey.
//
// Expected (2.37): VerifyAttestationSignature SHALL verify the signature against
// the attestor's public key and reject signatures that do not verify. A
// necessary condition: a FORGED signature of valid length (64..128 bytes),
// accompanied by a genuine attestor public key, must be REJECTED.
//
// UNFIXED: VerifyAttestationSignature only checks the signature is 64..128 bytes
// long and never verifies against attestorPubKey, so a garbage signature of
// valid length is accepted -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p15_1_37_forged_signature_rejected)
{
    CVM::ConsensusSafetyValidator validator;

    for (int i = 0; i < kSamples; ++i) {
        CKey key;
        key.MakeNewKey(/*fCompressed=*/true);
        CPubKey pub = key.GetPubKey();
        BOOST_REQUIRE(pub.IsValid());

        CVM::TrustAttestation att = MakeBaseAttestation(
            RandAddress(), static_cast<int16_t>(InsecureRandRange(101)));
        att.attestorPubKey.assign(pub.begin(), pub.end());

        // Forged signature: valid length (72 bytes, inside 64..128) but not a
        // signature by `key` over the attestation's message hash.
        std::vector<uint8_t> forged(72);
        for (auto& b : forged) b = static_cast<uint8_t>(InsecureRandRange(256));
        att.signature = forged;

        BOOST_CHECK_MESSAGE(!validator.VerifyAttestationSignature(att),
            "P15 (1.37): VerifyAttestationSignature accepted a FORGED signature "
            "of valid length (sample #" + std::to_string(i) + "); it only checks "
            "the signature length is 64..128 bytes and does not verify against "
            "the attestor's public key.");
    }
}

// ===========================================================================
// Property 15 (1.38) — trust-graph delta reflects real committed changes.
//
// Expected (2.38): GetTrustGraphDelta SHALL query the database for actual
// changes. A necessary condition: after real trust edges are committed, the
// delta since an earlier block must be NON-empty.
//
// UNFIXED: GetTrustGraphDelta's DB-iteration body is a placeholder that always
// returns an empty vector -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p15_1_38_delta_nonempty_after_changes)
{
    auto db = MakeTempDb();
    CVM::TrustGraph graph(*db);

    // Commit real trust-graph changes.
    const int kEdges = 5;
    for (int i = 0; i < kEdges; ++i) {
        uint160 from = RandAddress();
        uint160 to = RandAddress();
        BOOST_REQUIRE_MESSAGE(
            graph.AddTrustEdge(from, to, /*weight=*/50, /*bondAmount=*/COIN * 100,
                               uint256(), "workstream8-delta-test"),
            "P15 (1.38): precondition — committing a trust edge should succeed.");
    }

    CVM::ConsensusSafetyValidator validator(db.get(), /*hat=*/nullptr, &graph);

    std::vector<CVM::TrustEdge> delta = validator.GetTrustGraphDelta(/*sinceBlock=*/0);

    BOOST_CHECK_MESSAGE(!delta.empty(),
        "P15 (1.38): GetTrustGraphDelta returned an EMPTY delta after real "
        "trust-graph changes were committed; it does not query the database for "
        "actual changes.");
}

// ===========================================================================
// Property 15 (1.39a) — ApplyDelta operates against real state with no
// configured validator.
//
// Expected (2.39): trust-graph verify/apply SHALL operate against real state
// rather than failing when no consensus validator is configured. A necessary
// condition: with a real DB + trust graph but a NULL validator, ApplyDelta of a
// valid delta must succeed.
//
// UNFIXED: TrustGraphSyncManager::ApplyDelta returns false whenever
// consensusValidator == nullptr -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p15_1_39_apply_delta_without_validator)
{
    auto db = MakeTempDb();
    CVM::TrustGraph graph(*db);

    // Manager with real DB + graph, but NO configured consensus validator.
    CVM::TrustGraphSyncManager manager(db.get(), &graph, /*validator=*/nullptr);

    // A valid delta of non-slashed trust edges.
    std::vector<CVM::TrustEdge> delta;
    for (int i = 0; i < 4; ++i) {
        CVM::TrustEdge e;
        e.fromAddress = RandAddress();
        e.toAddress = RandAddress();
        e.trustWeight = 60;
        e.bondAmount = COIN * 100;
        e.slashed = false;
        e.reason = "workstream8-apply-test";
        delta.push_back(e);
    }

    BOOST_CHECK_MESSAGE(manager.ApplyDelta(delta),
        "P15 (1.39): TrustGraphSyncManager::ApplyDelta returned false with a real "
        "DB + trust graph but no configured validator; it should apply the delta "
        "against real state instead of failing when no validator is configured.");
}

// ===========================================================================
// Property 15 (1.39b) — VerifyState operates against real state with no
// configured validator (self-consistent round-trip).
//
// Expected (2.39): with a real DB + trust graph but a NULL validator, verifying
// the manager's own current state hash must succeed.
//
// UNFIXED: TrustGraphSyncManager::VerifyState (and GetCurrentState) return
// false / a default state whenever consensusValidator == nullptr, so the
// round-trip fails -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p15_1_39_verify_state_without_validator)
{
    auto db = MakeTempDb();
    CVM::TrustGraph graph(*db);

    // Commit some real state so a computed state hash is meaningful.
    graph.AddTrustEdge(RandAddress(), RandAddress(), 40, COIN * 100, uint256(),
                       "workstream8-verify-test");

    CVM::TrustGraphSyncManager manager(db.get(), &graph, /*validator=*/nullptr);

    // Self-consistent round-trip: verifying the manager's own reported state
    // hash must hold once verify/apply operate against real state.
    uint256 currentHash = manager.GetCurrentState().stateHash;

    BOOST_CHECK_MESSAGE(manager.VerifyState(currentHash),
        "P15 (1.39): TrustGraphSyncManager::VerifyState rejected the manager's "
        "own current state hash with a real DB + trust graph but no configured "
        "validator; it should verify against real state instead of failing when "
        "no validator is configured.");
}

// NOTE — DEFERRED fix-property clause in this suite (documented in the header):
//   1.38 (request-delta-from-peer half) is a P2P path with no unit-level seam
//   (needs a live CConnman/CNode). It is covered by the Workstream-8 fix
//   (task 20) and multi-node integration (task 30). No un-compilable code is
//   emitted for it here.

// ###########################################################################
// #  PRESERVATION TESTS (Property 21 / 3.15) — EXPECTED TO PASS on unfixed    #
// ###########################################################################

// ===========================================================================
// Preservation 3.15 — genuinely valid attestation with committed state still
// accepted.
//
// A cross-chain attestation carrying a genuinely valid secp256k1 signature over
// its own message hash, with a fresh timestamp and a non-null address (genuinely
// committed state), must be accepted by ValidateCrossChainAttestation. This is
// the behaviour Workstream 8 must preserve: real signature verification accepts
// genuine attestations. EXPECTED: PASS on unfixed code (length check passes) and
// after the fix (real ECDSA verification passes).
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_15_valid_attestation_accepted)
{
    CVM::ConsensusSafetyValidator validator;

    for (int i = 0; i < 16; ++i) {
        CKey key;
        key.MakeNewKey(/*fCompressed=*/true);
        CPubKey pub = key.GetPubKey();
        BOOST_REQUIRE(pub.IsValid());

        CVM::TrustAttestation att = MakeBaseAttestation(
            RandAddress(), static_cast<int16_t>(InsecureRandRange(101)));
        att.attestorPubKey.assign(pub.begin(), pub.end());

        // Genuine signature over the exact message hash the validator verifies.
        uint256 msgHash = AttestationMessageHash(att);
        std::vector<uint8_t> sig;
        BOOST_REQUIRE(key.Sign(msgHash, sig));
        // A DER ECDSA signature is ~70-72 bytes, within the 64..128 length band.
        BOOST_REQUIRE(sig.size() >= 64 && sig.size() <= 128);
        att.signature = sig;

        CVM::CrossChainAttestationResult result =
            validator.ValidateCrossChainAttestation(att);

        BOOST_CHECK_MESSAGE(result.isValid,
            "Preservation 3.15: a genuinely valid attestation (real signature, "
            "fresh timestamp, committed address) was rejected (sample #" +
            std::to_string(i) + ").");
        BOOST_CHECK_MESSAGE(result.isConsensusSafe,
            "Preservation 3.15: a genuinely valid attestation was not reported "
            "consensus-safe (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Preservation 3.15 — a validly-signed attestation also passes the
// signature-verification seam directly.
//
// Complements the ValidateCrossChainAttestation test: the genuine signature
// must satisfy VerifyAttestationSignature itself. This is the accept-genuine
// half of the real-verification behaviour Workstream 8 must preserve.
// EXPECTED: PASS on unfixed code and after the fix.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_15_valid_signature_verifies)
{
    CVM::ConsensusSafetyValidator validator;

    for (int i = 0; i < 16; ++i) {
        CKey key;
        key.MakeNewKey(/*fCompressed=*/true);
        CPubKey pub = key.GetPubKey();
        BOOST_REQUIRE(pub.IsValid());

        CVM::TrustAttestation att = MakeBaseAttestation(
            RandAddress(), static_cast<int16_t>(InsecureRandRange(101)));
        att.attestorPubKey.assign(pub.begin(), pub.end());

        uint256 msgHash = AttestationMessageHash(att);
        std::vector<uint8_t> sig;
        BOOST_REQUIRE(key.Sign(msgHash, sig));
        att.signature = sig;

        BOOST_CHECK_MESSAGE(validator.VerifyAttestationSignature(att),
            "Preservation 3.15: a genuinely valid attestation signature was "
            "rejected by VerifyAttestationSignature (sample #" +
            std::to_string(i) + ").");
    }
}

BOOST_AUTO_TEST_SUITE_END()
