// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 4 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 11: "Write Workstream-4 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 4 = HAT v2 distributed consensus
 * (bugfix.md clauses 1.3, 1.4, 1.8, 1.9, 1.47; preservation 3.4, 3.5, 3.18).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 11 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.3, 2.4, 2.8,
 *       2.9, 2.47). Written BEFORE the fix, they are EXPECTED TO FAIL on the
 *       current (unfixed) code — every failure is a counterexample confirming a
 *       defect. After the Workstream-4 fix lands (task 12) the SAME tests must
 *       pass. Prefixed `p11_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clauses 3.4, 3.5, 3.18).
 *       These capture behaviour that Workstream 4 must NOT change: the
 *       deterministic Fisher-Yates validator selection (3.5) and the existing
 *       ECDSA sign/verify of validator responses (3.4, 3.18). They are EXPECTED
 *       TO PASS on the unfixed code (baseline behaviour to preserve). Prefixed
 *       `preserve_`.
 *
 * Defects under test (design.md Fix Implementation → Workstream 4):
 *   1.3  A selected validator generates a response that unconditionally reports
 *        isValid=true with a fixed 80% confidence, without validating the task
 *        (validator_attestation.cpp ProcessValidationTaskMessage calls
 *        GenerateValidationResponse(task, true, 80)).
 *   1.4  GenerateValidationResponse sets the reported trust score to a hardcoded
 *        50 (`response.trustScore = 50; // Neutral for now`) instead of computing
 *        it from the trust graph.
 *   1.8  HATConsensusValidator::SendValidationChallenge returns success without
 *        transmitting any P2P message (`// TODO: Implement P2P message sending`).
 *   1.9  HATConsensusValidator::CreateDisputeCase sets the self-reported score
 *        equal to the validator's calculated score
 *        (`dispute.selfReportedScore = responses[0].calculatedScore`), so the
 *        self-reported vs calculated discrepancy can never be detected.
 *   1.47 MempoolManager::ProcessValidatorResponse only logs and never evaluates
 *        consensus / advances the transaction state.
 *
 * Expected-Behavior targets: 2.3, 2.4, 2.8, 2.9, 2.47
 * Preservation: 3.4, 3.5, 3.18
 * Requirements: 1.3, 1.4, 1.8, 1.9, 1.47, 3.4, 3.5, 3.18
 *
 * Testable-seam notes (per task guidance — "get as close as possible and
 * clearly document" when a stub is hard to exercise directly):
 *   - 1.3/1.4 are exercised through AutomaticValidatorManager, which builds the
 *     spontaneous validator response. The unfixed spontaneous-response path
 *     (ProcessValidationTaskMessage) emits the double placeholder
 *     (confidence=80, trustScore=50); we assert against that placeholder.
 *   - 1.8/1.9 are exercised on HATConsensusValidator directly.
 *   - 1.47 is exercised through MempoolManager + a real validation session.
 */

#include <cvm/hat_consensus.h>
#include <cvm/validator_attestation.h>
#include <cvm/mempool_manager.h>
#include <cvm/cvmdb.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>

#include <fs.h>
#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <utiltime.h>
#include <primitives/transaction.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// Build a random non-null 20-byte address.
uint160 RandAddress()
{
    uint160 addr;
    uint256 r = InsecureRand256();
    std::memcpy(addr.begin(), r.begin(), 20);
    if (addr.IsNull()) *addr.begin() = 0x01;
    return addr;
}

// Fresh in-memory CVM database for a single test.
std::unique_ptr<CVM::CVMDatabase> MakeTempDb()
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    return std::unique_ptr<CVM::CVMDatabase>(
        new CVM::CVMDatabase(testPath, 8 << 20, /*fMemory=*/true, /*fWipe=*/true));
}

// Minimal transaction with a stable hash.
CTransaction MakeTx(uint32_t salt)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.n = salt;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 1000 + salt;
    return CTransaction(mtx);
}

// Build a fully-signed HAT ValidationResponse whose embedded public key matches
// its validator address (so CVM::ValidationResponse::VerifySignature() succeeds
// standalone). Used both for the 1.47 fix-property test and the 3.4 preservation
// round-trip.
CVM::ValidationResponse MakeSignedHatResponse(const uint256& txHash,
                                              const uint256& challengeNonce,
                                              const CKey& key,
                                              CVM::ValidationVote vote,
                                              int16_t finalScore,
                                              bool hasWoT)
{
    CVM::ValidationResponse resp;
    CPubKey pub = key.GetPubKey();
    CKeyID keyID = pub.GetID();

    resp.txHash = txHash;
    std::memcpy(resp.validatorAddress.begin(), keyID.begin(), 20);
    resp.validatorPubKey.assign(pub.begin(), pub.end());
    resp.calculatedScore.address = resp.validatorAddress;
    resp.calculatedScore.finalScore = finalScore;
    resp.vote = vote;
    resp.voteConfidence = 0.9;
    resp.hasWoTConnection = hasWoT;
    resp.challengeNonce = challengeNonce;
    resp.timestamp = GetTime();

    resp.Sign(key);
    return resp;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream4_tests, BasicTestingSetup)

// ===========================================================================
// Property 11 (1.4) — The reported trust score is NOT the hardcoded constant 50.
//
// Expected (2.4): the reported trust score SHALL be computed from the trust
// graph. UNFIXED: AutomaticValidatorManager::GenerateValidationResponse does
//   response.trustScore = 50;  // Neutral for now
// unconditionally. We assert the produced trust score is not the hardcoded 50.
// On unfixed code it is always exactly 50 -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p11_1_4_trust_score_not_hardcoded_50)
{
    auto db = MakeTempDb();
    AutomaticValidatorManager manager(db.get());

    for (int i = 0; i < 16; ++i) {
        uint256 task = InsecureRand256();
        bool verdict = (i % 2 == 0);
        uint8_t conf = static_cast<uint8_t>(50 + InsecureRandRange(50)); // [50,99]

        ::ValidationResponse resp = manager.GenerateValidationResponse(task, verdict, conf);

        // EXPECTED (post-fix): trust score derived from the trust graph, not a
        // hardcoded constant. UNFIXED: trustScore == 50 for every task.
        BOOST_CHECK_MESSAGE(resp.trustScore != 50,
            "P11 (1.4): GenerateValidationResponse reported the hardcoded trust "
            "score 50 (`response.trustScore = 50; // Neutral for now`) instead of "
            "a trust-graph-derived value (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 11 (1.3) — A spontaneous validator response reflects ACTUAL task
// validation, not the fixed (isValid=true, confidence=80) placeholder.
//
// Expected (2.3): the validator SHALL validate the task and report an isValid /
// confidence derived from that validation. UNFIXED: the only spontaneous
// response path (ProcessValidationTaskMessage) does
//   GenerateValidationResponse(taskHash, true, 80);  // Placeholder
// producing the "double placeholder" (confidence=80, trustScore=50) with no
// real validation. We reproduce that exact call and assert the response is NOT
// that unvalidated double placeholder. On unfixed code it IS -> FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p11_1_3_response_not_unvalidated_placeholder)
{
    auto db = MakeTempDb();
    AutomaticValidatorManager manager(db.get());

    for (int i = 0; i < 16; ++i) {
        uint256 task = InsecureRand256();

        // Exactly what ProcessValidationTaskMessage emits on unfixed code:
        // "assume valid with 80% confidence" without validating the task.
        ::ValidationResponse resp = manager.GenerateValidationResponse(task, true, 80);

        // The unfixed placeholder response carries BOTH the fixed 80% confidence
        // AND the hardcoded 50 trust score, and is not derived from validating
        // the task. EXPECTED (post-fix): a genuinely validated response is not
        // this fixed placeholder pair.
        bool unvalidatedPlaceholder =
            (resp.confidence == 80) && (resp.trustScore == 50);

        BOOST_CHECK_MESSAGE(!unvalidatedPlaceholder,
            "P11 (1.3): the spontaneous validator response is the fixed "
            "placeholder (isValid=true, confidence=80, trustScore=50) produced "
            "without validating the task; no real task validation is performed "
            "(sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 11 (1.8) — Sending a validation challenge reports success ONLY when a
// P2P message is actually dispatched.
//
// Expected (2.8): the challenge SHALL be transmitted as a P2P message and
// success reported only when the message is dispatched. UNFIXED:
// SendValidationChallenge is a stub (`// TODO: Implement P2P message sending`)
// that returns true unconditionally. With no P2P transport available in a unit
// test, a correct implementation cannot dispatch and must report failure. We
// assert SendValidationChallenge returns false; on unfixed it returns true ->
// FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p11_1_8_send_challenge_without_p2p_reports_failure)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    for (int i = 0; i < 8; ++i) {
        CTransaction tx = MakeTx(100 + i);

        CVM::HATv2Score selfReported;
        selfReported.address = RandAddress();
        selfReported.finalScore = static_cast<int16_t>(InsecureRandRange(101));

        CVM::ValidationRequest request = validator.InitiateValidation(tx, selfReported);
        uint160 target = RandAddress();

        // No CConnman / P2P transport is available in this unit test, so a
        // correct implementation cannot dispatch the challenge.
        bool reported = validator.SendValidationChallenge(target, request);

        BOOST_CHECK_MESSAGE(!reported,
            "P11 (1.8): SendValidationChallenge reported success with no P2P "
            "message dispatched (the send path is a TODO stub that returns true "
            "unconditionally) (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 11 (1.9) — Dispute creation records the validator's ACTUAL
// self-reported score, so self-reported != calculated is representable.
//
// Expected (2.9): the dispute SHALL use the validator's actual self-reported
// score (from the validation session) so a discrepancy with the calculated
// score can be detected. UNFIXED: CreateDisputeCase does
//   dispute.selfReportedScore = responses[0].calculatedScore;
// i.e. self-reported is forced equal to calculated. We create a session whose
// self-reported finalScore (90) differs from the validators' calculated
// finalScore (30), then assert the dispute records the real self-reported (90).
// On unfixed code it records 30 (the calculated) -> FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p11_1_9_dispute_uses_actual_self_reported_score)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    const int16_t kSelfReported = 90;  // what the sender declared
    const int16_t kCalculated   = 30;  // what validators independently computed

    for (int i = 0; i < 8; ++i) {
        CTransaction tx = MakeTx(200 + i);
        uint160 sender = RandAddress();

        // Establish a validation session carrying the sender's self-reported score.
        CVM::HATv2Score selfReported;
        selfReported.address = sender;
        selfReported.finalScore = kSelfReported;
        validator.InitiateValidation(tx, selfReported);

        // Validator responses whose CALCULATED score differs from self-reported.
        std::vector<CVM::ValidationResponse> responses;
        for (int v = 0; v < 3; ++v) {
            CVM::ValidationResponse r;
            r.txHash = tx.GetHash();
            r.validatorAddress = RandAddress();
            r.calculatedScore.address = sender;
            r.calculatedScore.finalScore = kCalculated;
            r.vote = CVM::ValidationVote::REJECT;
            responses.push_back(r);
        }

        CVM::DisputeCase dispute = validator.CreateDisputeCase(tx, responses);

        // EXPECTED (post-fix): the dispute records the real self-reported score
        // (90), NOT the calculated score (30). UNFIXED: it copies calculated.
        BOOST_CHECK_MESSAGE(dispute.selfReportedScore.finalScore == kSelfReported,
            "P11 (1.9): CreateDisputeCase recorded self-reported finalScore=" +
            std::to_string(dispute.selfReportedScore.finalScore) + " (expected the "
            "actual self-reported " + std::to_string(kSelfReported) + "); it copies "
            "the validators' calculated score, so a self-reported vs calculated "
            "discrepancy can never be detected (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 11 (1.47) — Processing validator responses through the mempool
// manager advances the transaction toward a consensus decision, not only logs.
//
// Expected (2.47): ProcessValidatorResponse SHALL accumulate responses in the
// validation session and evaluate consensus. UNFIXED:
// MempoolManager::ProcessValidatorResponse forwards to the HAT validator (which
// accumulates) but then only logs ("For now, just log the response") and never
// evaluates consensus or advances the transaction state — so the transaction
// stays PENDING_VALIDATION forever. We feed a unanimous, signed, WoT-backed set
// of responses and assert HAT validation reaches completion. On unfixed code the
// state never leaves PENDING_VALIDATION -> IsHATValidationComplete stays false
// -> FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p11_1_47_mempool_process_response_reaches_consensus)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    CVM::MempoolManager mempool;
    mempool.Initialize(db.get());
    mempool.SetHATConsensusValidator(&validator);

    CTransaction tx = MakeTx(4711);
    uint160 sender = RandAddress();

    CVM::HATv2Score selfReported;
    selfReported.address = sender;
    selfReported.finalScore = 75;
    CVM::ValidationRequest request = validator.InitiateValidation(tx, selfReported);

    // Sanity: the transaction starts out pending.
    BOOST_REQUIRE(!mempool.IsHATValidationComplete(tx.GetHash()));

    // Feed a unanimous, signed, WoT-backed set of responses (distinct validators)
    // through the mempool manager, exactly as the P2P message path would.
    const int kResponses = 12;
    for (int v = 0; v < kResponses; ++v) {
        CKey key;
        key.MakeNewKey(true);
        CVM::ValidationResponse resp = MakeSignedHatResponse(
            tx.GetHash(), request.challengeNonce, key,
            CVM::ValidationVote::ACCEPT, /*finalScore=*/75, /*hasWoT=*/true);
        mempool.ProcessValidatorResponse(resp);
    }

    // EXPECTED (post-fix): the mempool manager evaluates consensus and advances
    // the transaction state, so validation completes. UNFIXED: it only logs, so
    // the transaction is stuck in PENDING_VALIDATION.
    BOOST_CHECK_MESSAGE(mempool.IsHATValidationComplete(tx.GetHash()),
        "P11 (1.47): after processing a unanimous set of validator responses the "
        "mempool manager did NOT advance the transaction toward a consensus "
        "decision (state remained PENDING_VALIDATION); ProcessValidatorResponse "
        "only logs and never evaluates consensus.");
}

// ===========================================================================
// Preservation 3.4 / 3.18 — Existing ECDSA sign/verify of validator responses.
//
// Workstream 4 must NOT change response signing/verification. A HAT
// ValidationResponse signed with a validator key (public key embedded, address
// derived from that key) verifies via its self-contained VerifySignature(), and
// any tamper invalidates it. EXPECTED: PASS on unfixed code (baseline to preserve).
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_4_hat_response_sign_verify_roundtrip)
{
    for (int i = 0; i < 32; ++i) {
        CKey key;
        key.MakeNewKey(true);

        uint256 txHash = InsecureRand256();
        uint256 nonce  = InsecureRand256();
        CVM::ValidationResponse resp = MakeSignedHatResponse(
            txHash, nonce, key, CVM::ValidationVote::ACCEPT,
            static_cast<int16_t>(InsecureRandRange(101)), /*hasWoT=*/true);

        BOOST_CHECK_MESSAGE(resp.VerifySignature(),
            "Preservation 3.4/3.18: a genuinely signed HAT validator response "
            "must verify (sample #" + std::to_string(i) + ").");

        // Tamper with a signed field: verification must now fail.
        CVM::ValidationResponse tampered = resp;
        tampered.calculatedScore.finalScore =
            static_cast<int16_t>(tampered.calculatedScore.finalScore ^ 0x7F);
        BOOST_CHECK_MESSAGE(!tampered.VerifySignature(),
            "Preservation 3.4/3.18: a tampered validator response must NOT verify "
            "(sample #" + std::to_string(i) + ").");
    }
}

// The AutomaticValidatorManager's ValidationResponse uses the same secp256k1
// ECDSA path (Sign over the response fields; verify with the signer's public
// key). We sign with a key and verify the signature against that key's public
// key over the response hash — the core round-trip that must remain unchanged.
BOOST_AUTO_TEST_CASE(preserve_3_18_attestation_response_ecdsa_roundtrip)
{
    for (int i = 0; i < 32; ++i) {
        CKey key;
        key.MakeNewKey(true);
        CPubKey pub = key.GetPubKey();

        ::ValidationResponse resp;
        resp.taskHash = InsecureRand256();
        std::memcpy(resp.validatorAddress.begin(), pub.GetID().begin(), 20);
        resp.isValid = (i % 2 == 0);
        resp.confidence = static_cast<uint8_t>(InsecureRandRange(101));
        resp.trustScore = static_cast<uint8_t>(InsecureRandRange(101));
        resp.timestamp = GetTime();

        std::vector<uint8_t> privBytes(key.begin(), key.end()); // 32-byte secret
        BOOST_REQUIRE_EQUAL(privBytes.size(), size_t(32));

        BOOST_REQUIRE_MESSAGE(resp.Sign(privBytes),
            "Preservation 3.18: signing a validator response must succeed "
            "(sample #" + std::to_string(i) + ").");

        // The signature must verify against the signer's public key over the
        // response hash (the existing ECDSA verification math).
        BOOST_CHECK_MESSAGE(pub.Verify(resp.GetHash(), resp.signature),
            "Preservation 3.18: a genuinely signed validator response must verify "
            "against the signer's public key (sample #" + std::to_string(i) + ").");

        // Tamper: flipping a signed field must break verification.
        ::ValidationResponse tampered = resp;
        tampered.confidence = static_cast<uint8_t>(tampered.confidence ^ 0xFF);
        BOOST_CHECK_MESSAGE(!pub.Verify(tampered.GetHash(), tampered.signature),
            "Preservation 3.18: a tampered validator response must NOT verify "
            "(sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Preservation 3.5 — Deterministic Fisher-Yates validator selection.
//
// Validator selection must be deterministic: the same seed (and the same
// task/height, which derives the seed) yields the same validator set and order.
// Workstream 4 must NOT change this. We populate a real eligible-validator pool
// and assert selection is a deterministic function of the seed and of the
// task/height. EXPECTED: PASS on unfixed code (baseline to preserve).
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_5_deterministic_selection_same_seed)
{
    auto db = MakeTempDb();
    AutomaticValidatorManager manager(db.get());

    // Populate a pool of eligible validators and load it into memory.
    std::vector<uint160> pool;
    for (int i = 0; i < 25; ++i) {
        ValidatorEligibilityRecord rec;
        rec.validatorAddress = RandAddress();
        rec.stakeAmount = 100 * COIN;
        rec.isEligible = true;
        manager.StoreEligibilityRecord(rec);
        pool.push_back(rec.validatorAddress);
    }
    manager.LoadValidatorPool();
    BOOST_REQUIRE_EQUAL(manager.GetEligibleValidatorCount(), 25);

    uint256 seedA = InsecureRand256();
    uint256 seedB = InsecureRand256();

    std::vector<uint160> a1 = manager.SelectRandomValidators(seedA, 10);
    std::vector<uint160> a2 = manager.SelectRandomValidators(seedA, 10);

    // Same seed => identical validator set AND order (deterministic).
    BOOST_CHECK_MESSAGE(a1 == a2,
        "Preservation 3.5: SelectRandomValidators must be deterministic for a "
        "fixed seed (same set and order).");
    BOOST_CHECK_EQUAL(a1.size(), size_t(10));

    // Every selected validator comes from the eligible pool.
    for (const auto& v : a1) {
        bool inPool = std::find(pool.begin(), pool.end(), v) != pool.end();
        BOOST_CHECK_MESSAGE(inPool,
            "Preservation 3.5: selected validator must be from the eligible pool.");
    }

    // A different seed should (with overwhelming probability over 25 validators)
    // produce a different ordering — confirming the seed actually drives shuffle.
    std::vector<uint160> b1 = manager.SelectRandomValidators(seedB, 10);
    BOOST_CHECK_MESSAGE(b1 != a1 || seedA == seedB,
        "Preservation 3.5: distinct seeds are expected to drive distinct "
        "shuffles of the same pool.");
}

// Same task hash + block height must produce the same deterministic selection
// seed and the same selected validators (design: "same seed/height => same
// validator set/order").
BOOST_AUTO_TEST_CASE(preserve_3_5_deterministic_selection_by_task_height)
{
    auto db = MakeTempDb();
    AutomaticValidatorManager manager(db.get());

    for (int i = 0; i < 20; ++i) {
        ValidatorEligibilityRecord rec;
        rec.validatorAddress = RandAddress();
        rec.stakeAmount = 100 * COIN;
        rec.isEligible = true;
        manager.StoreEligibilityRecord(rec);
    }
    manager.LoadValidatorPool();
    BOOST_REQUIRE_EQUAL(manager.GetEligibleValidatorCount(), 20);

    uint256 task = InsecureRand256();
    int64_t height = -1;  // no chain-tip block hash mixed in (BasicTestingSetup)

    ValidatorSelection s1 = manager.SelectValidatorsForTask(task, height, 10);
    ValidatorSelection s2 = manager.SelectValidatorsForTask(task, height, 10);

    BOOST_CHECK_MESSAGE(s1.selectionSeed == s2.selectionSeed,
        "Preservation 3.5: the selection seed must be a deterministic function of "
        "task hash + block height.");
    BOOST_CHECK_MESSAGE(s1.selectedValidators == s2.selectedValidators,
        "Preservation 3.5: the same task/height must select the same validator "
        "set and order.");

    // A different task hash yields a different seed.
    uint256 otherTask = InsecureRand256();
    ValidatorSelection s3 = manager.SelectValidatorsForTask(otherTask, height, 10);
    BOOST_CHECK_MESSAGE(s3.selectionSeed != s1.selectionSeed || otherTask == task,
        "Preservation 3.5: distinct task hashes must produce distinct selection "
        "seeds.");
}

BOOST_AUTO_TEST_SUITE_END()
