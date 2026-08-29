// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 11 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 25: "Write Workstream-11 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 11 = Security monitoring RPC (non-consensus, unconditional)
 * (bugfix.md clause 1.59).
 *
 * PURPOSE
 * -------
 * This single suite holds BOTH directions for Workstream 11:
 *
 *   Property 18 (Bug Condition / fix-property) — EXPECTED TO FAIL on unfixed:
 *     `getvalidatorstats_security` SHALL return the per-validator statistics
 *     documented in its own help text (total/accurate/inaccurate validations,
 *     abstentions, accuracy rate, reputation, last activity) for a requested
 *     validator address, NOT merely a static `message` placeholder. Written
 *     BEFORE the fix, each failure is a counterexample confirming defect 1.59.
 *     After the Workstream-11 fix (task 26) the SAME tests must pass unchanged.
 *
 *   Property 21 (Preservation) — EXPECTED TO PASS on unfixed and after the fix:
 *     other security-monitoring RPCs are unchanged (3.21). We capture the
 *     legacy behaviour of representative sibling RPCs in `security_rpc.cpp`
 *     (their help/argument-validation contracts and the deterministic
 *     `setsecurityconfig` echo path) as golden vectors. These paths do not touch
 *     the flagged `getvalidatorstats_security` body, so they must remain
 *     byte-for-byte identical after the fix.
 *
 * TESTABILITY SEAM
 * ----------------
 * The security RPC handlers are plain non-static free functions in
 * `cvm/security_rpc.cpp` taking a `const JSONRPCRequest&`. They are not exported
 * via a header, so we forward-declare the three we exercise and invoke them
 * directly with a hand-built `JSONRPCRequest` — no running node, HTTP layer, or
 * RPC dispatch table is required. `getvalidatorstats_security` in particular
 * ignores all node context on the unfixed code (it returns a constant), so it is
 * fully unit-testable at this seam. No deferral to integration is necessary for
 * the defect itself; end-to-end RPC dispatch remains covered by the functional
 * integration test (task 29).
 *
 * Requirements: 1.59, 3.21
 */

#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Forward declarations of the (non-static) RPC handlers defined in
// cvm/security_rpc.cpp. They are not exported via a header, so we declare them
// here to invoke them directly at the unit seam.
// ---------------------------------------------------------------------------
UniValue getvalidatorstats_security(const JSONRPCRequest& request);
UniValue getsecuritymetrics(const JSONRPCRequest& request);
UniValue setsecurityconfig(const JSONRPCRequest& request);

namespace {

static constexpr int kSamples = 128;

// Build a fresh, well-formed request carrying the given positional params.
JSONRPCRequest MakeRequest(const UniValue& params, bool fHelp = false)
{
    JSONRPCRequest request;
    request.params = params;
    request.fHelp = fHelp;
    return request;
}

UniValue EmptyParams()
{
    return UniValue(UniValue::VARR);
}

UniValue OneStringParam(const std::string& s)
{
    UniValue arr(UniValue::VARR);
    arr.push_back(s);
    return arr;
}

// The per-validator statistics fields documented in the RPC's own help text.
// Presence of these (rather than just a "message" placeholder) is the property.
const std::vector<std::string>& DocumentedStatFields()
{
    static const std::vector<std::string> kFields = {
        "total_validations",
        "accurate_validations",
        "inaccurate_validations",
        "abstentions",
        "accuracy_rate",
        "reputation",
        "last_activity",
    };
    return kFields;
}

// A random 40-hex-char (20-byte) validator address string, matching the
// uint160::SetHex parsing the sibling security RPCs use for address arguments.
std::string RandomHexAddress()
{
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(40);
    for (int i = 0; i < 40; ++i) {
        s.push_back(hex[InsecureRandRange(16)]);
    }
    return s;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream11_tests, BasicTestingSetup)

// ===========================================================================
// Property 18 — Security RPC returns real per-validator stats.          (1.59)
//
// Expected (2.59): `getvalidatorstats_security` SHALL return the documented
// per-validator statistics fields, NOT merely a static `message` placeholder.
//
// UNFIXED: the handler ignores its argument and returns
//   { "message": "Validator stats available through HAT consensus system" }
// so the documented fields are absent -> these tests FAIL (bug confirmed).
// ===========================================================================

// Golden vector: with no argument, the result must expose the documented stat
// fields and must not be a bare `message` placeholder.
BOOST_AUTO_TEST_CASE(p18_validator_stats_documented_fields_golden)
{
    UniValue result = getvalidatorstats_security(MakeRequest(EmptyParams()));

    BOOST_REQUIRE_MESSAGE(result.isObject(),
        "P18 (1.59): getvalidatorstats_security must return a JSON object");

    // A bare placeholder object carrying only "message" is the defect.
    const bool onlyMessage = result.exists("message") &&
                             !result.exists("total_validations");

    for (const std::string& field : DocumentedStatFields()) {
        BOOST_CHECK_MESSAGE(result.exists(field),
            "P18 (1.59): getvalidatorstats_security is missing the documented "
            "field '" + field + "' — it returns a static placeholder (" +
            (onlyMessage ? "'" + result["message"].get_str() + "'"
                         : std::string("<no documented stats>")) +
            ") instead of real per-validator statistics.");
    }
}

// Golden vector: with an explicit validator address argument the RPC must still
// return the documented per-validator statistics (populated for that address).
BOOST_AUTO_TEST_CASE(p18_validator_stats_documented_fields_with_address_golden)
{
    UniValue result = getvalidatorstats_security(
        MakeRequest(OneStringParam("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa")));

    BOOST_REQUIRE_MESSAGE(result.isObject(),
        "P18 (1.59): getvalidatorstats_security must return a JSON object");

    BOOST_CHECK_MESSAGE(result.exists("total_validations"),
        "P18 (1.59): getvalidatorstats_security for a requested validator "
        "returns a static placeholder ('" +
        (result.exists("message") ? result["message"].get_str()
                                   : std::string("<no message>")) +
        "') instead of the documented per-validator statistics.");
}

// Property: for any requested validator address, the RPC must return the
// documented statistics fields (and not a bare `message` placeholder).
// **Validates: Requirements 1.59**
BOOST_AUTO_TEST_CASE(p18_validator_stats_documented_fields_property)
{
    for (int i = 0; i < kSamples; ++i) {
        UniValue result = getvalidatorstats_security(
            MakeRequest(OneStringParam(RandomHexAddress())));

        BOOST_REQUIRE_MESSAGE(result.isObject(),
            "P18 (1.59): result must be an object (#" + std::to_string(i) + ")");

        // Core statistic that the unfixed placeholder never populates.
        BOOST_CHECK_MESSAGE(result.exists("total_validations"),
            "P18 (1.59): getvalidatorstats_security ignored the requested "
            "validator and returned a static placeholder instead of documented "
            "per-validator statistics (sample #" + std::to_string(i) + ").");

        // And it must not be *merely* a message placeholder.
        const bool onlyMessage = result.exists("message") &&
                                 !result.exists("total_validations");
        BOOST_CHECK_MESSAGE(!onlyMessage,
            "P18 (1.59): getvalidatorstats_security returned only a 'message' "
            "field (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 21 — Other security-monitoring RPCs unchanged.               (3.21)
//
// The fix touches ONLY the body of getvalidatorstats_security. The sibling
// security RPCs' help/argument-validation contracts and deterministic echo
// paths must remain identical. These tests capture that legacy behaviour and
// are EXPECTED TO PASS on the unfixed code (and after the fix). They avoid any
// dependence on optional global subsystems (g_securityAudit / g_anomalyDetector)
// being initialised, so they are order-independent and robust.
// ===========================================================================

// getsecuritymetrics: the help/usage contract is unchanged (fHelp throws the
// documented usage runtime_error naming the RPC).
BOOST_AUTO_TEST_CASE(p21_getsecuritymetrics_help_unchanged)
{
    JSONRPCRequest request = MakeRequest(EmptyParams(), /*fHelp=*/true);

    bool threw = false;
    std::string what;
    try {
        getsecuritymetrics(request);
    } catch (const std::runtime_error& e) {
        threw = true;
        what = e.what();
    }

    BOOST_CHECK_MESSAGE(threw,
        "3.21: getsecuritymetrics(fHelp) must throw its usage runtime_error");
    BOOST_CHECK_MESSAGE(what.find("getsecuritymetrics") != std::string::npos,
        "3.21: getsecuritymetrics help text must name the RPC (got: '" + what + "')");
}

// setsecurityconfig: the help/usage contract is unchanged.
BOOST_AUTO_TEST_CASE(p21_setsecurityconfig_help_unchanged)
{
    JSONRPCRequest request = MakeRequest(EmptyParams(), /*fHelp=*/true);

    bool threw = false;
    std::string what;
    try {
        setsecurityconfig(request);
    } catch (const std::runtime_error& e) {
        threw = true;
        what = e.what();
    }

    BOOST_CHECK_MESSAGE(threw,
        "3.21: setsecurityconfig(fHelp) must throw its usage runtime_error");
    BOOST_CHECK_MESSAGE(what.find("setsecurityconfig") != std::string::npos,
        "3.21: setsecurityconfig help text must name the RPC (got: '" + what + "')");
}

// setsecurityconfig: an unknown setting is rejected with a JSONRPCError
// (thrown as a UniValue) — unchanged argument-validation behaviour.
BOOST_AUTO_TEST_CASE(p21_setsecurityconfig_unknown_setting_rejected)
{
    UniValue params(UniValue::VARR);
    params.push_back("definitely_not_a_real_setting");
    params.push_back(UniValue(1)); // any second arg to pass the arity check

    bool threw = false;
    try {
        setsecurityconfig(MakeRequest(params));
    } catch (const UniValue& e) {
        threw = true;
        // The error object carries the RPC error code/message.
        BOOST_CHECK_MESSAGE(e.isObject() && e.exists("code"),
            "3.21: setsecurityconfig unknown-setting error must be a JSONRPCError object");
    } catch (const std::exception&) {
        // Any well-defined rejection is acceptable; record that it threw.
        threw = true;
    }

    BOOST_CHECK_MESSAGE(threw,
        "3.21: setsecurityconfig must reject an unknown setting");
}

// Property: setsecurityconfig echoes back a valid threshold setting with
// success=true and the exact value provided — deterministic, unchanged path.
// **Validates: Requirements 3.21**
BOOST_AUTO_TEST_CASE(p21_setsecurityconfig_threshold_echo_property)
{
    static const char* kThresholdSettings[] = {
        "reputation_threshold", "validator_threshold", "coordination_threshold"};

    for (int i = 0; i < kSamples; ++i) {
        const char* setting = kThresholdSettings[InsecureRandRange(3)];
        // A value in [0.5, 5.0] that round-trips exactly through UniValue.
        double value = 0.5 + static_cast<double>(InsecureRandRange(4501)) / 1000.0;

        UniValue params(UniValue::VARR);
        params.push_back(setting);
        params.push_back(UniValue(value));

        UniValue result = setsecurityconfig(MakeRequest(params));

        BOOST_REQUIRE_MESSAGE(result.isObject(),
            "3.21: setsecurityconfig must return an object (#" + std::to_string(i) + ")");
        // A valid threshold setting reports success. The stored value is truthy;
        // we compare its serialised form rather than assuming a specific JSON
        // type so the check is robust to the codebase's UniValue encoding.
        BOOST_REQUIRE_MESSAGE(result.exists("success"),
            "3.21: setsecurityconfig must report a 'success' field for a valid "
            "threshold setting (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(result["success"].getValStr() != "false" &&
                                !result["success"].isNull(),
            "3.21: setsecurityconfig must report success=true for a valid "
            "threshold setting (#" + std::to_string(i) + "), got '" +
            result["success"].getValStr() + "'");
        BOOST_CHECK_MESSAGE(result.exists("setting") &&
                                result["setting"].get_str() == setting,
            "3.21: setsecurityconfig must echo the setting name (#" +
            std::to_string(i) + ")");
        BOOST_REQUIRE_MESSAGE(result.exists("value"),
            "3.21: setsecurityconfig must echo the value (#" + std::to_string(i) + ")");
        BOOST_CHECK_CLOSE(result["value"].get_real(), value, 1e-9);
    }
}

BOOST_AUTO_TEST_SUITE_END()
