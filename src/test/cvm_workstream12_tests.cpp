// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 12 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 27: "Write Workstream-12 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 12 = Graceful degradation (non-consensus, unconditional)
 * (bugfix.md clauses 1.15, 1.62).
 *
 * PURPOSE
 * -------
 * This single suite holds BOTH directions for Workstream 12:
 *
 *   Property 20 (Bug Condition / fix-property) — EXPECTED TO FAIL on unfixed:
 *     For any resource-usage check or TRUST_CONTEXT / HAT_VALIDATION fallback the
 *     fixed code SHALL measure actual memory/CPU/storage and invoke the REAL
 *     subsystems, reporting success/failure from the actual result (2.15, 2.62).
 *     On the unfixed code:
 *       - `GetReputationWithFallback` / `RunHealthCheck(REPUTATION_QUERY)` report
 *         a simulated placeholder success instead of querying the real reputation
 *         subsystem (defect 1.15). The `.cpp` says outright: "we simulate success
 *         and return a placeholder".
 *       - `CheckMemoryUsage` / `CheckCPUUsage` / `CheckStorageUsage` are empty
 *         no-op bodies that never trigger degradation, and the TRUST_CONTEXT /
 *         HAT_VALIDATION fallback paths call `RecordSubsystemSuccess` and return a
 *         genuine (non-fallback) success WITHOUT invoking the real trust-context /
 *         HAT-validation subsystems (defect 1.62).
 *     Written BEFORE the fix, each failing assertion is a counterexample
 *     confirming the defect. After the Workstream-12 fix (task 28) the SAME tests
 *     must pass unchanged.
 *
 *   Property 21 (Preservation) — EXPECTED TO PASS on unfixed and after the fix:
 *     Genuinely healthy subsystems still report success under graceful
 *     degradation (3.24). We capture the legacy behaviour of the NON-flagged
 *     graceful-degradation paths as golden vectors: the circuit-breaker health
 *     mechanism (a freshly-initialised manager reports every subsystem available
 *     and 100% system health, and recording a genuine success keeps it healthy),
 *     and the two genuinely-functional fallback computations that are NOT flagged
 *     for Workstream 12 — `CalculateGasDiscountWithFallback` (GAS_DISCOUNT) and
 *     `CheckFreeGasEligibilityWithFallback` (FREE_GAS). These paths are untouched
 *     by the 1.15 / 1.62 fixes and must remain byte-for-byte identical.
 *
 * TESTABILITY SEAM
 * ----------------
 * `GracefulDegradationManager` is a plain class with a fully public API
 * (cvm/graceful_degradation.h). We construct a local instance per test and drive
 * it directly — no running node, P2P layer, or database is required. The isolated
 * unit environment is precisely the scenario the fix-property tests need: the
 * REAL reputation / trust-context / HAT-validation subsystems are NOT wired up
 * here, so a correct (post-fix) health check / fallback that actually consults
 * those subsystems would report unavailability/failure — whereas the unfixed
 * placeholder reports success regardless. That divergence is what these tests
 * pin down.
 *
 * DEFERRAL NOTE (resource-usage checks, 1.62)
 * -------------------------------------------
 * "Measures actual memory/CPU/storage usage" has no clean, portable return-value
 * seam (the checks return void and only side-effect the degradation level). We
 * therefore assert the observable post-fix CONTRACT that fails now: with the
 * resource thresholds pinned to 0.0 (so ANY real, measured usage is over the
 * limit), running the checks MUST move the manager off the NORMAL degradation
 * level (or into emergency mode). The unfixed no-op bodies never measure anything
 * and leave the level at NORMAL, so the assertion fails — confirming 1.62.
 *
 * Requirements: 1.15, 1.62, 3.24
 */

#include <cvm/graceful_degradation.h>

#include <uint256.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace CVM;

namespace {

// Cheap, in-process properties can afford a healthy sample count.
static constexpr int kSamples = 128;

// Build a random, non-null 20-byte address from the test PRNG.
uint160 RandAddress()
{
    uint160 a;
    uint256 r = InsecureRand256();
    std::memcpy(a.begin(), r.begin(), 20);
    if (a.IsNull()) *a.begin() = 0x01;
    return a;
}

// Construct + initialise a manager ready for use.
std::unique_ptr<GracefulDegradationManager> MakeManager()
{
    auto mgr = std::unique_ptr<GracefulDegradationManager>(new GracefulDegradationManager());
    BOOST_REQUIRE(mgr->Initialize());
    return mgr;
}

// All ten subsystems the manager tracks.
const std::vector<SubsystemType>& AllSubsystems()
{
    static const std::vector<SubsystemType> kAll = {
        SubsystemType::TRUST_CONTEXT,
        SubsystemType::REPUTATION_QUERY,
        SubsystemType::HAT_VALIDATION,
        SubsystemType::GAS_DISCOUNT,
        SubsystemType::FREE_GAS,
        SubsystemType::CROSS_CHAIN_TRUST,
        SubsystemType::VALIDATOR_SELECTION,
        SubsystemType::DAO_DISPUTE,
        SubsystemType::STORAGE_RENT,
        SubsystemType::ANOMALY_DETECTION};
    return kAll;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream12_tests, BasicTestingSetup)

// ===========================================================================
// Property 20 — Bug Condition (fix-property). EXPECTED TO FAIL on unfixed code.
//
// Each case below encodes the post-fix contract from Expected-Behavior clauses
// 2.15 / 2.62. On the current (placeholder) code the assertions fail; every
// failure is a counterexample recorded against defect 1.15 / 1.62.
// ===========================================================================

// ---- 1.15: reputation health check must reflect the REAL subsystem ----------
//
// `RunHealthCheck(REPUTATION_QUERY)` on the unfixed code returns
// `circuitState == CLOSED`, i.e. it reports "healthy" for a freshly-initialised
// manager even though no real reputation subsystem has been consulted. In this
// isolated unit environment the real reputation subsystem is unavailable, so a
// correct health check (post-fix) MUST NOT report healthy. **Validates: Requirements 1.15**
BOOST_AUTO_TEST_CASE(p20_reputation_health_check_not_simulated_success)
{
    auto mgr = MakeManager();

    bool healthy = mgr->RunHealthCheck(SubsystemType::REPUTATION_QUERY);

    BOOST_CHECK_MESSAGE(!healthy,
        "1.15: RunHealthCheck(REPUTATION_QUERY) reported healthy without a real "
        "reputation subsystem available. A correct health check must query the "
        "real subsystem and report failure when it is unavailable, not return a "
        "simulated placeholder success.");
}

// ---- 1.15: reputation query must not fabricate a genuine (non-fallback) success
//
// `GetReputationWithFallback` on the unfixed code, when the circuit is closed,
// returns `Success(defaultValue)` with `usedFallback == false` after a comment
// that literally reads "we simulate success and return a placeholder". A correct
// implementation, with no real reputation subsystem available, must NOT report a
// genuine non-fallback success (it should either fall back or fail).
// **Validates: Requirements 1.15**
BOOST_AUTO_TEST_CASE(p20_reputation_query_not_simulated_success_golden)
{
    auto mgr = MakeManager();

    const uint160 addr = RandAddress();
    const uint8_t defaultValue = 50;

    FallbackResult<uint8_t> res = mgr->GetReputationWithFallback(addr, defaultValue);

    const bool genuineSuccess = res.success && !res.usedFallback;
    BOOST_CHECK_MESSAGE(!genuineSuccess,
        "1.15: GetReputationWithFallback reported a genuine (non-fallback) success "
        "returning the caller-supplied default (" + std::to_string((int)res.value) +
        ") without querying the real reputation subsystem. Post-fix, an unavailable "
        "real subsystem must yield a fallback or a failure, not a simulated success.");
}

// Property form: across random addresses and default values, an unavailable real
// reputation subsystem must never yield a genuine non-fallback success.
// **Validates: Requirements 1.15**
BOOST_AUTO_TEST_CASE(p20_reputation_query_not_simulated_success_property)
{
    auto mgr = MakeManager();

    for (int i = 0; i < kSamples; ++i) {
        const uint160 addr = RandAddress();
        const uint8_t defaultValue = static_cast<uint8_t>(InsecureRandRange(101)); // 0..100

        FallbackResult<uint8_t> res = mgr->GetReputationWithFallback(addr, defaultValue);

        const bool genuineSuccess = res.success && !res.usedFallback;
        BOOST_CHECK_MESSAGE(!genuineSuccess,
            "1.15: GetReputationWithFallback fabricated a genuine success "
            "(value=" + std::to_string((int)res.value) + ", default=" +
            std::to_string((int)defaultValue) + ") without a real subsystem "
            "(sample #" + std::to_string(i) + ").");
    }
}

// ---- 1.62: resource-usage checks must MEASURE actual usage -------------------
//
// With every threshold pinned to 0.0, any real measured usage is over the limit,
// so running the checks must move the manager off NORMAL (or into emergency
// mode). The unfixed no-op bodies measure nothing and leave the level at NORMAL.
// See the DEFERRAL NOTE in the file header. **Validates: Requirements 1.62**
BOOST_AUTO_TEST_CASE(p20_resource_checks_measure_actual_usage)
{
    auto mgr = MakeManager();

    // Any non-negative real usage exceeds a 0.0 threshold.
    mgr->SetResourceThresholds(0.0, 0.0, 0.0);

    BOOST_REQUIRE(mgr->GetDegradationLevel() == DegradationLevel::NORMAL);
    BOOST_REQUIRE(!mgr->IsInEmergencyMode());

    mgr->CheckMemoryUsage();
    mgr->CheckCPUUsage();
    mgr->CheckStorageUsage();

    const bool degraded =
        (mgr->GetDegradationLevel() != DegradationLevel::NORMAL) || mgr->IsInEmergencyMode();

    BOOST_CHECK_MESSAGE(degraded,
        "1.62: resource-usage checks left the manager at NORMAL degradation with "
        "all thresholds pinned to 0.0. The unfixed checks are empty no-ops that "
        "never measure actual memory/CPU/storage usage; post-fix, measured usage "
        "over a 0.0 threshold must trigger degradation.");
}

// ---- 1.62: TRUST_CONTEXT fallback must invoke the real subsystem -------------
//
// `InjectTrustContextWithFallback`, when the circuit is closed, calls
// `RecordSubsystemSuccess` and returns `Success(true)` (usedFallback == false)
// without invoking the real trust-context subsystem. With no real subsystem
// available here, a correct implementation must not report a genuine success.
// **Validates: Requirements 1.62**
BOOST_AUTO_TEST_CASE(p20_trust_context_fallback_not_simulated_success)
{
    auto mgr = MakeManager();

    const uint160 caller = RandAddress();
    const uint160 contract = RandAddress();

    FallbackResult<bool> res = mgr->InjectTrustContextWithFallback(caller, contract);

    const bool genuineSuccess = res.success && !res.usedFallback;
    BOOST_CHECK_MESSAGE(!genuineSuccess,
        "1.62: InjectTrustContextWithFallback reported a genuine (non-fallback) "
        "success without invoking the real trust-context subsystem. Post-fix, an "
        "unavailable real subsystem must yield a fallback or a failure.");
}

// ---- 1.62: HAT_VALIDATION fallback must invoke the real subsystem ------------
//
// `ValidateWithHATv2Fallback`, when the circuit is closed, calls
// `RecordSubsystemSuccess` and returns `Success(true)` (usedFallback == false)
// without invoking the real HAT-validation subsystem. **Validates: Requirements 1.62**
BOOST_AUTO_TEST_CASE(p20_hat_validation_fallback_not_simulated_success)
{
    auto mgr = MakeManager();

    const uint256 txHash = InsecureRand256();
    const uint160 sender = RandAddress();
    const uint8_t selfReportedScore = static_cast<uint8_t>(InsecureRandRange(101));

    FallbackResult<bool> res = mgr->ValidateWithHATv2Fallback(txHash, sender, selfReportedScore);

    const bool genuineSuccess = res.success && !res.usedFallback;
    BOOST_CHECK_MESSAGE(!genuineSuccess,
        "1.62: ValidateWithHATv2Fallback reported a genuine (non-fallback) success "
        "without invoking the real HAT-validation subsystem. Post-fix, an "
        "unavailable real subsystem must yield a fallback or a failure.");
}

// Property form: over many random senders / scores the HAT fallback must not
// fabricate a genuine non-fallback success without the real subsystem.
// **Validates: Requirements 1.62**
BOOST_AUTO_TEST_CASE(p20_hat_validation_fallback_not_simulated_success_property)
{
    auto mgr = MakeManager();

    for (int i = 0; i < kSamples; ++i) {
        const uint256 txHash = InsecureRand256();
        const uint160 sender = RandAddress();
        const uint8_t score = static_cast<uint8_t>(InsecureRandRange(101));

        FallbackResult<bool> res = mgr->ValidateWithHATv2Fallback(txHash, sender, score);

        const bool genuineSuccess = res.success && !res.usedFallback;
        BOOST_CHECK_MESSAGE(!genuineSuccess,
            "1.62: ValidateWithHATv2Fallback fabricated a genuine success without a "
            "real subsystem (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 21 — Preservation. EXPECTED TO PASS on unfixed AND after the fix.
//
// Genuinely healthy subsystems still report success under graceful degradation
// (3.24). These capture NON-flagged paths (the circuit-breaker health mechanism
// and the genuinely-functional GAS_DISCOUNT / FREE_GAS computations) that the
// 1.15 / 1.62 fixes must leave byte-for-byte identical.
// ===========================================================================

// A freshly-initialised manager reports every subsystem available and 100%
// system health — the healthy baseline that must be preserved.
// **Validates: Requirements 3.24**
BOOST_AUTO_TEST_CASE(p21_fresh_manager_all_healthy_golden)
{
    auto mgr = MakeManager();

    for (SubsystemType s : AllSubsystems()) {
        BOOST_CHECK_MESSAGE(mgr->IsSubsystemAvailable(s),
            "3.24: freshly-initialised subsystem should be available");
        BOOST_CHECK_MESSAGE(mgr->IsSubsystemEnabled(s),
            "3.24: freshly-initialised subsystem should be enabled");
    }

    BOOST_CHECK_MESSAGE(mgr->GetSystemHealth() == 1.0,
        "3.24: a fully-healthy manager must report system health 1.0, got " +
        std::to_string(mgr->GetSystemHealth()));
    BOOST_CHECK(mgr->GetDegradationLevel() == DegradationLevel::NORMAL);
    BOOST_CHECK(!mgr->IsInEmergencyMode());
}

// Recording a genuine success on a healthy subsystem keeps it healthy/available.
// **Validates: Requirements 3.24**
BOOST_AUTO_TEST_CASE(p21_recorded_success_keeps_subsystem_healthy)
{
    auto mgr = MakeManager();

    for (SubsystemType s : AllSubsystems()) {
        mgr->RecordSubsystemSuccess(s);
        BOOST_CHECK_MESSAGE(mgr->IsSubsystemAvailable(s),
            "3.24: subsystem must remain available after a recorded success");
        SubsystemStatus st = mgr->GetSubsystemStatus(s);
        BOOST_CHECK_MESSAGE(st.circuitState == CircuitState::CLOSED,
            "3.24: circuit must stay CLOSED after a recorded success");
        BOOST_CHECK_MESSAGE(st.requestsProcessed >= 1,
            "3.24: a recorded success must be counted");
    }

    BOOST_CHECK_MESSAGE(mgr->GetSystemHealth() == 1.0,
        "3.24: system health must remain 1.0 after recording successes, got " +
        std::to_string(mgr->GetSystemHealth()));
}

// GAS_DISCOUNT is a genuinely-functional (non-flagged) computation: it must keep
// returning success with the exact discount the current code computes.
// **Validates: Requirements 3.24**
BOOST_AUTO_TEST_CASE(p21_gas_discount_computation_preserved_golden)
{
    auto mgr = MakeManager();

    // reputation <= 50 -> no discount (returns baseGas unchanged).
    {
        FallbackResult<uint64_t> r = mgr->CalculateGasDiscountWithFallback(50, 1000000);
        BOOST_CHECK(r.success);
        BOOST_CHECK(!r.usedFallback);
        BOOST_CHECK_EQUAL(r.value, static_cast<uint64_t>(1000000));
    }
    // reputation 100 -> 0.5% per point above 50 => 25% discount.
    {
        const uint64_t baseGas = 1000000;
        const double discountRate = (100 - 50) * 0.005; // 0.25
        const uint64_t expected = static_cast<uint64_t>(baseGas * (1.0 - discountRate));
        FallbackResult<uint64_t> r = mgr->CalculateGasDiscountWithFallback(100, baseGas);
        BOOST_CHECK(r.success);
        BOOST_CHECK(!r.usedFallback);
        BOOST_CHECK_EQUAL(r.value, expected);
    }
}

// Property: the discount formula is reproduced exactly for random reputation and
// base-gas values (genuine, non-flagged path must be preserved).
// **Validates: Requirements 3.24**
BOOST_AUTO_TEST_CASE(p21_gas_discount_computation_preserved_property)
{
    auto mgr = MakeManager();

    for (int i = 0; i < kSamples; ++i) {
        const uint8_t reputation = static_cast<uint8_t>(InsecureRandRange(101)); // 0..100
        const uint64_t baseGas = InsecureRandRange(10000000ULL) + 1;             // 1..10,000,000

        // Reference: identical arithmetic to CalculateGasDiscountWithFallback.
        uint64_t expected = baseGas;
        if (reputation > 50) {
            double discountRate = (reputation - 50) * 0.005;
            expected = static_cast<uint64_t>(baseGas * (1.0 - discountRate));
        }

        FallbackResult<uint64_t> r = mgr->CalculateGasDiscountWithFallback(reputation, baseGas);
        BOOST_CHECK_MESSAGE(r.success && !r.usedFallback,
            "3.24: GAS_DISCOUNT must report a genuine success (sample #" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(r.value == expected,
            "3.24: GAS_DISCOUNT value mismatch — got " + std::to_string(r.value) +
            " expected " + std::to_string(expected) + " (rep=" +
            std::to_string((int)reputation) + ", baseGas=" + std::to_string(baseGas) +
            ", sample #" + std::to_string(i) + ")");
    }
}

// FREE_GAS eligibility is a genuinely-functional (non-flagged) computation:
// eligible iff reputation >= 80. Must be preserved.
// **Validates: Requirements 3.24**
BOOST_AUTO_TEST_CASE(p21_free_gas_eligibility_preserved_property)
{
    auto mgr = MakeManager();

    for (int i = 0; i < kSamples; ++i) {
        const uint160 addr = RandAddress();
        const uint8_t reputation = static_cast<uint8_t>(InsecureRandRange(101)); // 0..100
        const bool expectedEligible = reputation >= 80;

        FallbackResult<bool> r = mgr->CheckFreeGasEligibilityWithFallback(addr, reputation);
        BOOST_CHECK_MESSAGE(r.success && !r.usedFallback,
            "3.24: FREE_GAS must report a genuine success (sample #" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(r.value == expectedEligible,
            "3.24: FREE_GAS eligibility mismatch for reputation " +
            std::to_string((int)reputation) + " (sample #" + std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
