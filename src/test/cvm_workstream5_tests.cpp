// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 5 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 13: "Write Workstream-5 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 5 = Fee / gas / subsidy accounting (consensus-adjacent)
 * (bugfix.md clauses 1.10, 1.40–1.46; preservation 3.1, 3.8, 3.16).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 12 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.10, 2.40–2.46).
 *       Written BEFORE the fix, they are EXPECTED TO FAIL on the current
 *       (unfixed) code — every failure is a counterexample confirming a defect.
 *       After the Workstream-5 fix lands (task 14) the SAME tests must pass.
 *       Prefixed `p12_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clauses 3.1, 3.8, 3.16).
 *       These capture behaviour that Workstream 5 must NOT change: the free-gas
 *       zero-fee path (3.8), the deterministic 1:1-equivalent split/tier rules
 *       (3.1), and the fact that an operation eligible under BOTH the old and
 *       the new checks still receives its subsidy (3.16). They are EXPECTED TO
 *       PASS on the unfixed code (baseline behaviour to preserve). Prefixed
 *       `preserve_`.
 *
 * Defects under test (design.md Fix Implementation → Workstream 5):
 *   1.10 mempool_manager.cpp ValidateTransaction: for a non-free-gas tx the
 *        subsidy check is a TODO ("Check for applicable gas subsidies" / "For
 *        now, calculate normal fee with reputation discount"), so applicable
 *        subsidies are skipped before the effective fee is set.
 *   1.40 fee_calculator.cpp CalculateGasSubsidy: `bool isBeneficial =
 *        (reputation >= 80); // Simplified check` — subsidy eligibility is a
 *        pure reputation gate, not a real network-benefit assessment, and the
 *        subsidy pipeline yields nothing.
 *   1.41 fee_calculator.cpp GetSenderAddress: returns an empty uint160() for
 *        every transaction ("return empty - this will be filled in by
 *        validation.cpp").
 *   1.42 fee_calculator.cpp GetNetworkLoad: returns a hardcoded/heuristic
 *        moderate load derived from a fixed price ratio (not live mempool
 *        state); GetGasToSatoshiRate returns a fixed default rate.
 *   1.43 sustainable_gas.cpp IsNetworkBeneficialOperation: `return
 *        callerReputation >= 70;` — ignores the opcode/operation, so the benefit
 *        classification is a pure reputation threshold, not a real assessment.
 *   1.44 gas_subsidy.cpp DistributePendingRebates increments counters without
 *        transferring/crediting funds; SaveToDatabase serializes subsidy records
 *        as EMPTY data and LoadFromDatabase is a no-op (round-trip loses data).
 *   1.45 gas_allowance.cpp LoadFromDatabase does not iterate the database, so
 *        persisted bulk allowance state is not restored at startup.
 *   1.46 mempool_priority.cpp: the reputation-priority comparator is not
 *        initialized with the CVM database ("TODO: Initialize with CVM database
 *        when available"), so priority decisions run without DB context.
 *
 * Expected-Behavior targets: 2.10, 2.40, 2.41, 2.42, 2.43, 2.44, 2.45, 2.46
 * Preservation: 3.1, 3.8, 3.16
 * Requirements: 1.10, 1.40, 1.41, 1.42, 1.43, 1.44, 1.45, 1.46, 3.1, 3.8, 3.16
 *
 * Testable-seam notes (per task guidance — "get as close as possible and
 * clearly document" when a stub is hard to exercise directly):
 *   - 1.40 is exercised through FeeCalculator::CalculateGasSubsidy (the fee
 *     subsidy pipeline): a genuinely beneficial, high-reputation operation must
 *     receive a non-zero subsidy; the unfixed reputation-only gate yields none.
 *   - 1.42 is exercised through FeeCalculator::GetNetworkLoad against an empty
 *     mempool: a live-state-derived load would be low/zero for an empty mempool,
 *     but the unfixed heuristic returns a constant moderate load. The
 *     gas-to-satoshi *rate* half of 1.42 is behind a private, unused accessor
 *     (GetGasToSatoshiRate) with no public observable and is covered only by the
 *     same root-cause fix (documented, not directly asserted).
 *   - 1.44's rebate *transfer/credit* half has no seam in the current
 *     GasSubsidyTracker API (DistributePendingRebates takes neither a database
 *     nor a credit target), so only the serialization round-trip half is
 *     asserted here; the transfer half is documented.
 *   - 1.46 is not directly observable at unit level: the comparator constructor
 *     takes no database and exposes no DB accessor, and its only behavioural
 *     effect (reputation-differentiated priority) is additionally gated by the
 *     1.41 empty-sender defect (all CVM txs resolve to the default reputation).
 *     It is documented here and covered by the fix + integration coverage.
 */

#include <cvm/fee_calculator.h>
#include <cvm/sustainable_gas.h>
#include <cvm/gas_subsidy.h>
#include <cvm/gas_allowance.h>
#include <cvm/trust_context.h>
#include <cvm/cvmdb.h>
#include <cvm/opcodes.h>
#include <cvm/softfork.h>

#include <amount.h>
#include <coins.h>
#include <fs.h>
#include <hash.h>
#include <key.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <uint256.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

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

// Contract-deploy transaction using the softfork.cpp encoding (CVMDeployData),
// consumed by the CVM/EVM fee-calculation paths.
CTransactionRef MakeSoftforkDeployTx(uint64_t gasLimit)
{
    CVM::CVMDeployData d;
    d.gasLimit = gasLimit;
    d.format = CVM::BytecodeFormat::CVM_NATIVE;
    d.codeHash = InsecureRand256();
    std::vector<uint8_t> data = d.Serialize();

    CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, script);
    return MakeTransactionRef(std::move(mtx));
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream5_tests, BasicTestingSetup)

// ###########################################################################
// #  FIX-PROPERTY TESTS (Property 12) — EXPECTED TO FAIL on unfixed code     #
// ###########################################################################

// ===========================================================================
// Property 12 (1.41) — ExtractSenderAddress must resolve a real sender.
//
// Expected (2.41): FeeCalculator::GetSenderAddress SHALL resolve the real
// sender address from the transaction inputs (via the UTXO set), not return an
// empty uint160.
//
// UNFIXED: GetSenderAddress always returns uint160() ("return empty - this will
// be filled in by validation.cpp"). We build CVM/EVM transactions (each with a
// funded input) and assert the resolved sender is non-empty. On unfixed code it
// is always null -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p12_1_41_sender_resolved_nonempty)
{
    auto db = MakeTempDb();
    CVM::FeeCalculator feeCalc;
    feeCalc.Initialize(db.get());

    // Per 2.41 the real sender is resolved from the transaction's inputs via the
    // validation UTXO set. Provide a coins view (the seam validation supplies)
    // and fund each transaction's input with a standard P2PKH output so the fee
    // calculator can resolve the real spending address. (Test SETUP only — the
    // non-empty-sender assertion below is unchanged.)
    CCoinsView dummyBacking;
    CCoinsViewCache coinsView(&dummyBacking);
    feeCalc.SetCoinsView(&coinsView);

    for (int i = 0; i < 16; ++i) {
        uint64_t gasLimit = 100000 + InsecureRandRange(400000);
        CTransactionRef tx = MakeSoftforkDeployTx(gasLimit);

        // Fund the transaction's input with a known P2PKH coin.
        CKey key;
        key.MakeNewKey(true);
        CKeyID keyid = key.GetPubKey().GetID();
        CScript spk = GetScriptForDestination(CTxDestination(keyid));
        coinsView.AddCoin(tx->vin[0].prevout, Coin(CTxOut(100000, spk), 1, false), false);

        uint160 sender = feeCalc.GetSenderAddress(*tx);

        // EXPECTED (post-fix): a real, non-empty sender is resolved.
        // UNFIXED: always the empty/null uint160.
        BOOST_CHECK_MESSAGE(!sender.IsNull(),
            "P12 (1.41): FeeCalculator::GetSenderAddress returned the empty "
            "uint160 for a CVM transaction; the real sender is never resolved "
            "from the tx inputs/UTXO set (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 12 (1.42) — Network load must derive from live mempool state.
//
// Expected (2.42): FeeCalculator::GetNetworkLoad SHALL derive load from the
// actual mempool state (and the conversion rate from the configured pricing
// source), not a hardcoded/heuristic constant.
//
// The test runs with an EMPTY mempool (BasicTestingSetup), so a load derived
// from live state must be low (≈0). UNFIXED: GetNetworkLoad ignores the mempool
// entirely and returns a constant moderate load computed from a fixed price
// ratio (GetPredictableGasPrice(50,50) vs base price) -> a non-low value -> the
// property FAILS (counterexample).
//
// NOTE: the gas-to-satoshi *rate* half of 1.42 (GetGasToSatoshiRate returning a
// fixed DEFAULT_GAS_TO_SATOSHI_RATE) is a private, unused accessor with no
// public observable; it shares this root cause and is fixed alongside the load,
// but is not directly asserted here.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p12_1_42_network_load_from_live_state)
{
    auto db = MakeTempDb();
    CVM::FeeCalculator feeCalc;
    feeCalc.Initialize(db.get());

    uint64_t load = feeCalc.GetNetworkLoad();

    // EXPECTED (post-fix): an EMPTY mempool yields zero live load.
    // UNFIXED: a non-zero constant derived from a fixed price ratio, independent
    // of the (empty) mempool.
    BOOST_CHECK_MESSAGE(load == 0,
        "P12 (1.42): FeeCalculator::GetNetworkLoad returned load=" +
        std::to_string(load) + " for an EMPTY mempool (expected 0); load is a "
        "hardcoded/heuristic constant (from a fixed price ratio) rather than "
        "derived from live mempool state.");
}

// ===========================================================================
// Property 12 (1.43) — Beneficial-operation classification must assess the
// actual operation, not be a pure reputation threshold.
//
// Expected (2.43): SustainableGasSystem::IsNetworkBeneficialOperation SHALL
// assess actual network benefit rather than reputation alone. Because the
// function is given the opcode, a real assessment must depend on WHICH
// operation is performed — so at a fixed reputation the classification cannot be
// identical across every opcode.
//
// UNFIXED: the body is `return callerReputation >= 70;`, ignoring the opcode
// entirely, so at a fixed (high) reputation EVERY opcode is classified the same
// (all beneficial) -> the classification is constant across opcodes -> the
// property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p12_1_43_benefit_depends_on_operation)
{
    cvm::SustainableGasSystem gas;

    CVM::TrustContext trust;
    trust.SetCallerReputation(90); // fixed high reputation

    // A representative spread of opcodes: cheap arithmetic, storage read/write,
    // a contract call, a balance query, and a halt.
    const std::vector<uint8_t> opcodes = {
        static_cast<uint8_t>(CVM::OpCode::OP_ADD),
        static_cast<uint8_t>(CVM::OpCode::OP_MUL),
        static_cast<uint8_t>(CVM::OpCode::OP_SLOAD),
        static_cast<uint8_t>(CVM::OpCode::OP_SSTORE),
        static_cast<uint8_t>(CVM::OpCode::OP_CALL),
        static_cast<uint8_t>(CVM::OpCode::OP_BALANCE),
        static_cast<uint8_t>(CVM::OpCode::OP_STOP),
    };

    std::set<bool> classifications;
    for (uint8_t op : opcodes) {
        classifications.insert(gas.IsNetworkBeneficialOperation(op, trust));
    }

    // EXPECTED (post-fix): a real benefit assessment distinguishes at least some
    // operations at a fixed reputation (not all-identical). UNFIXED: identical
    // for every opcode (pure reputation>=70 gate).
    BOOST_CHECK_MESSAGE(classifications.size() > 1,
        "P12 (1.43): IsNetworkBeneficialOperation returned the SAME benefit "
        "classification for every sampled opcode at a fixed reputation; the "
        "decision ignores the operation and is a pure `callerReputation >= 70` "
        "threshold rather than a real network-benefit assessment.");
}

// ===========================================================================
// Property 12 (1.40) — Fee subsidy eligibility must be benefit-assessed and
// actually produce a subsidy for a genuinely beneficial, high-reputation op.
//
// Expected (2.40): FeeCalculator's subsidy path SHALL assess actual network
// benefit (not a bare reputation gate) and grant the applicable subsidy. A
// clearly beneficial, high-reputation deploy must receive a non-zero gas
// subsidy.
//
// UNFIXED: CalculateGasSubsidy gates on `reputation >= 80` (a simplified check)
// and the subsidy pipeline yields nothing (the internal trust context carries
// no reputation), so the computed subsidy is always 0 -> the property FAILS
// (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p12_1_40_beneficial_op_receives_subsidy)
{
    auto db = MakeTempDb();
    CVM::FeeCalculator feeCalc;
    feeCalc.Initialize(db.get());

    for (int i = 0; i < 8; ++i) {
        uint64_t gasLimit = 100000 + InsecureRandRange(400000);
        CTransactionRef tx = MakeSoftforkDeployTx(gasLimit);
        uint160 sender = RandAddress();

        // A high-reputation caller performing a (beneficial) contract deploy.
        const uint8_t highReputation = 90;
        CAmount subsidy = feeCalc.CalculateGasSubsidy(*tx, sender, gasLimit, highReputation);

        // EXPECTED (post-fix): a beneficial, high-reputation operation receives a
        // non-zero subsidy from a real benefit assessment. UNFIXED: the
        // reputation-only gate + unwired pipeline yields 0.
        BOOST_CHECK_MESSAGE(subsidy > 0,
            "P12 (1.40): FeeCalculator::CalculateGasSubsidy returned 0 for a "
            "beneficial, high-reputation (90) operation; subsidy eligibility is a "
            "hardcoded `reputation >= 80` gate that performs no real benefit "
            "assessment and grants no subsidy (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 12 (1.44) — Subsidy records must serialize completely (round-trip).
//
// Expected (2.44): GasSubsidyTracker SHALL serialize all subsidy record fields
// so persisted subsidy state survives a save/load round-trip (and SHALL
// transfer/credit rebate amounts — see NOTE).
//
// We apply a subsidy for an address, persist via SaveToDatabase, then load into
// a FRESH tracker via LoadFromDatabase and assert the address's total subsidy is
// restored.
//
// UNFIXED: SaveToDatabase writes subsidy records as EMPTY data ("would properly
// serialize all records") and LoadFromDatabase is a no-op, so the fresh tracker
// reports 0 -> the property FAILS (counterexample).
//
// NOTE: the rebate *transfer/credit* half of 1.44 has no seam in the current
// GasSubsidyTracker API (DistributePendingRebates takes neither a database nor a
// credit target and only increments a counter), so it is documented rather than
// asserted here.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p12_1_44_subsidy_record_serialization_roundtrip)
{
    auto db = MakeTempDb();

    uint160 addr = RandAddress();

    CVM::TrustContext trust;
    trust.SetCallerReputation(90);

    CVM::GasSubsidyTracker tracker;
    const uint64_t gasUsed = 250000;
    const uint64_t subsidy = 90000;
    tracker.ApplySubsidy(InsecureRand256(), addr, gasUsed, subsidy, trust, /*blockHeight=*/1000);

    uint64_t before = tracker.GetTotalSubsidies(addr);
    BOOST_REQUIRE_MESSAGE(before == subsidy,
        "P12 (1.44): precondition — applied subsidy should be tracked in-memory.");

    tracker.SaveToDatabase(*db);

    // Fresh tracker: everything must come from the database.
    CVM::GasSubsidyTracker restored;
    restored.LoadFromDatabase(*db);
    uint64_t after = restored.GetTotalSubsidies(addr);

    // EXPECTED (post-fix): the subsidy record round-trips completely.
    // UNFIXED: records are serialized as empty data / not loaded -> 0.
    BOOST_CHECK_MESSAGE(after == before,
        "P12 (1.44): subsidy records do not survive a save/load round-trip "
        "(restored total=" + std::to_string(after) + ", expected=" +
        std::to_string(before) + "); SaveToDatabase serializes subsidy records "
        "as empty data and LoadFromDatabase does not restore them.");
}

// ===========================================================================
// Property 12 (1.45) — Allowance state must be restored from the database.
//
// Expected (2.45): GasAllowanceTracker::LoadFromDatabase SHALL iterate the
// database and restore persisted allowance state at startup.
//
// We establish an allowance for a high-reputation address, consume part of it
// (usedToday > 0), persist via SaveToDatabase, then load into a FRESH tracker
// and read the allowance state. A restored state reflects the prior usage; a
// freshly-created state has usedToday == 0.
//
// UNFIXED: LoadFromDatabase is a no-op ("load on-demand"), so the fresh tracker
// synthesizes a brand-new state with usedToday == 0 -> the property FAILS
// (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p12_1_45_allowance_state_restored_from_db)
{
    auto db = MakeTempDb();

    uint160 addr = RandAddress();

    CVM::TrustContext trust;
    trust.SetCallerReputation(90); // free-gas eligible => non-zero daily allowance

    CVM::GasAllowanceTracker tracker;
    const int64_t block = 1000;
    const uint64_t used = 500000;

    // Establish the allowance state and consume part of it.
    BOOST_REQUIRE_MESSAGE(tracker.HasSufficientAllowance(addr, used, trust, block),
        "P12 (1.45): precondition — high-reputation address should have allowance.");
    BOOST_REQUIRE_MESSAGE(tracker.DeductGas(addr, used, block),
        "P12 (1.45): precondition — gas deduction should succeed.");

    auto savedState = tracker.GetAllowanceState(addr, trust, block);
    BOOST_REQUIRE_EQUAL(savedState.usedToday, used);

    tracker.SaveToDatabase(*db);

    // Fresh tracker: state must come from the database.
    CVM::GasAllowanceTracker restored;
    restored.LoadFromDatabase(*db);
    // Read one block later (no daily renewal boundary crossed).
    auto restoredState = restored.GetAllowanceState(addr, trust, block + 1);

    // EXPECTED (post-fix): the persisted usage is restored.
    // UNFIXED: LoadFromDatabase is a no-op -> fresh state with usedToday == 0.
    BOOST_CHECK_MESSAGE(restoredState.usedToday == used,
        "P12 (1.45): allowance state was not restored from the database "
        "(restored usedToday=" + std::to_string(restoredState.usedToday) +
        ", expected=" + std::to_string(used) + "); LoadFromDatabase does not "
        "iterate the database and relies on on-demand creation.");
}

// ###########################################################################
// #  PRESERVATION TESTS (Property 21) — EXPECTED TO PASS on unfixed code     #
// ###########################################################################

// ===========================================================================
// Preservation 3.8 — Free-gas eligibility yields a zero effective fee.
//
// The free-gas path (reputation >= 80 => eligible => zero gas cost) is NOT a
// flagged path and must behave identically after the Workstream-5 fix. We pin
// the eligibility threshold and the zero-cost outcome as golden behaviour.
// EXPECTED: PASS on unfixed code (baseline to preserve).
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_8_free_gas_zero_fee)
{
    cvm::SustainableGasSystem gas;
    const uint8_t opcode = static_cast<uint8_t>(CVM::OpCode::OP_SSTORE);

    // High-reputation caller: free-gas eligible => zero gas cost.
    CVM::TrustContext trustHigh;
    trustHigh.SetCallerReputation(85);
    BOOST_CHECK_MESSAGE(gas.IsEligibleForFreeGas(85),
        "Preservation 3.8: reputation 85 must be free-gas eligible.");
    BOOST_CHECK_MESSAGE(gas.CalculateGasCost(opcode, trustHigh) == 0,
        "Preservation 3.8: a free-gas-eligible caller must incur zero gas cost.");
    BOOST_CHECK_MESSAGE(gas.GetFreeGasAllowance(85) > 0,
        "Preservation 3.8: a free-gas-eligible caller must have a non-zero "
        "free-gas allowance.");

    // Just-below-threshold caller: NOT free-gas eligible => non-zero cost.
    CVM::TrustContext trustLow;
    trustLow.SetCallerReputation(79);
    BOOST_CHECK_MESSAGE(!gas.IsEligibleForFreeGas(79),
        "Preservation 3.8: reputation 79 must NOT be free-gas eligible.");
    BOOST_CHECK_MESSAGE(gas.CalculateGasCost(opcode, trustLow) > 0,
        "Preservation 3.8: a non-free-gas caller must incur a non-zero gas cost.");
}

// Property 21 (3.8): the free-gas eligibility threshold is exactly >= 80 across
// the whole reputation range, and eligibility implies a zero gas cost.
BOOST_AUTO_TEST_CASE(preserve_3_8_free_gas_threshold_property)
{
    cvm::SustainableGasSystem gas;
    const uint8_t opcode = static_cast<uint8_t>(CVM::OpCode::OP_ADD);

    for (int rep = 0; rep <= 255; ++rep) {
        CVM::TrustContext trust;
        trust.SetCallerReputation(static_cast<uint32_t>(rep));

        bool eligible = gas.IsEligibleForFreeGas(static_cast<uint8_t>(rep));
        BOOST_REQUIRE_EQUAL(eligible, rep >= 80);

        if (eligible) {
            BOOST_REQUIRE_MESSAGE(gas.CalculateGasCost(opcode, trust) == 0,
                "Preservation 3.8 (property): free-gas-eligible reputation " +
                std::to_string(rep) + " must incur zero gas cost.");
        }
    }
}

// ===========================================================================
// Preservation 3.1 — 1:1-equivalent split / deterministic tier rules unchanged.
//
// The reputation-multiplier tiers and the reputation-scaled subsidy percentage
// are deterministic split rules (NOT flagged — Workstream 5 changes benefit
// assessment, sender resolution, load, transfer, and persistence, not these
// arithmetic tiers). We pin them as golden vectors so they cannot silently
// change. EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_1_reputation_multiplier_tiers_golden)
{
    CVM::FeeCalculator feeCalc;

    // Reputation multiplier tiers (lower multiplier = larger discount):
    //   >=90: 0.5, >=80: 0.6, >=70: 0.7, >=60: 0.8, >=50: 0.9, <50: 1.0
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(95), 0.5);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(90), 0.5);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(85), 0.6);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(80), 0.6);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(75), 0.7);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(70), 0.7);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(65), 0.8);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(60), 0.8);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(55), 0.9);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(50), 0.9);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(49), 1.0);
    BOOST_CHECK_EQUAL(feeCalc.GetReputationMultiplier(0), 1.0);
}

// Property 21 (3.1): the reputation-scaled subsidy percentage (subsidy =
// gasUsed * (reputation/2) / 100) for a beneficial operation is a deterministic
// split rule and is reproduced exactly.
BOOST_AUTO_TEST_CASE(preserve_3_1_subsidy_percentage_formula_property)
{
    CVM::GasSubsidyTracker tracker;
    const uint64_t gasUsed = 1000000;

    for (int rep = 0; rep <= 100; rep += 5) {
        CVM::TrustContext trust;
        trust.SetCallerReputation(static_cast<uint32_t>(rep));

        uint64_t subsidy = tracker.CalculateSubsidy(gasUsed, trust, /*isBeneficial=*/true);

        // Independent re-derivation of the documented formula.
        uint64_t expected = (gasUsed * (static_cast<uint64_t>(rep) / 2)) / 100;
        BOOST_REQUIRE_MESSAGE(subsidy == expected,
            "Preservation 3.1 (property): subsidy percentage formula changed at "
            "reputation " + std::to_string(rep) + " (got " + std::to_string(subsidy) +
            ", expected " + std::to_string(expected) + ").");

        // A non-beneficial operation always yields zero (unchanged).
        BOOST_REQUIRE_EQUAL(tracker.CalculateSubsidy(gasUsed, trust, /*isBeneficial=*/false), 0u);
    }
}

// ===========================================================================
// Preservation 3.16 — A doubly-eligible operation still gets its subsidy.
//
// An operation eligible under BOTH the old check (reputation >= 70) AND a real
// benefit assessment (a genuinely beneficial, high-reputation contract call)
// must continue to receive its subsidy after the fix. We assert the old-check
// eligibility and the resulting non-zero subsidy on the unfixed code; this is
// the behaviour the fix must preserve for matching inputs.
// EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_16_dual_eligible_subsidy_granted)
{
    CVM::GasSubsidyTracker tracker;

    CVM::TrustContext trust;
    trust.SetCallerReputation(90); // clearly eligible under the old >=70 check

    // Old-path eligibility: a high-reputation operation is classified beneficial.
    bool beneficial = tracker.IsBeneficialOperation(trust);
    BOOST_CHECK_MESSAGE(beneficial,
        "Preservation 3.16: a high-reputation (90) operation must be eligible "
        "under the existing benefit check.");

    // The subsidy for such a doubly-eligible operation is non-zero and matches
    // the deterministic reputation-scaled formula.
    const uint64_t gasUsed = 400000;
    uint64_t subsidy = tracker.CalculateSubsidy(gasUsed, trust, beneficial);
    uint64_t expected = (gasUsed * (90 / 2)) / 100; // 45% at reputation 90

    BOOST_CHECK_MESSAGE(subsidy > 0,
        "Preservation 3.16: a doubly-eligible (old + new) operation must still "
        "receive a non-zero subsidy.");
    BOOST_CHECK_MESSAGE(subsidy == expected,
        "Preservation 3.16: the granted subsidy (" + std::to_string(subsidy) +
        ") must match the preserved reputation-scaled amount (" +
        std::to_string(expected) + ").");
}

BOOST_AUTO_TEST_SUITE_END()
