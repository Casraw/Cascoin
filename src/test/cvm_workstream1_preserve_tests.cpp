// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 1 Preservation Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 4: "Write Workstream-1 preservation tests (BEFORE fix)"
 *
 * Workstream 1 = Consensus-critical accounting & block processing
 * (bugfix.md clauses 1.1, 1.2, 1.16–1.22, 1.60, 1.61).
 *
 * PURPOSE
 * -------
 * Property 21 (Preservation): ¬isBugCondition ⇒ F'(x) = F(x). Following the
 * *observation-first* methodology from design.md (Testing Strategy →
 * Preservation Checking), these tests capture the legacy behaviour of the
 * NON-flagged Workstream-1 code paths on the CURRENT (unfixed) code as golden
 * vectors. They are EXPECTED TO PASS on the unfixed code (baseline behaviour to
 * preserve) and MUST still pass unchanged after the Workstream-1 fixes land
 * (task 5.9).
 *
 * A test that FAILS on the unfixed code would mean the captured case actually
 * reaches a flagged path and must be re-classified.
 *
 * Captured behaviours, mapped to Unchanged-Behavior clauses (3.x):
 *   3.1  A transaction whose (1:1-equivalent) rate produces a cost equal to the
 *        previous fixed rate keeps the same fee/subsidy split. Captured via the
 *        deterministic consensus split rules (reputation-discount tiers,
 *        free-gas eligibility, max-subsidy) — which are NOT flagged — plus the
 *        WoT "no gas-fee split" (100% to miner) and the unchanged gas-USED
 *        extraction basis.
 *   3.2  A block with no CVM/EVM transactions and no gas subsidies validates
 *        exactly as before (ExecuteCVMBlock accepts it, nothing is written).
 *   3.17 A block with no gas subsidies and no failed contract executions is
 *        validated, saved, and finalized exactly as before.
 *   3.14 The canonical deployer+nonce contract-address scheme is unchanged.
 *   3.22 A zero-value deploy/call exposes CALLVALUE = 0.
 *
 * Property-based (Property 21): random non-flagged inputs (standard/WoT blocks,
 * random reputations, random deployer/nonce pairs) produce identical results
 * before and after the fix.
 *
 * Requirements: 3.1, 3.2, 3.14, 3.17, 3.22
 */

#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>
#include <cvm/contract.h>
#include <cvm/cvmtx.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/consensus_validator.h>
#include <cvm/softfork.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/params.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <uint256.h>
#include <arith_uint256.h>
#include <amount.h>
#include <key.h>
#include <pubkey.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace {

static constexpr int kSamples = 256;      // cheap, deterministic properties
static constexpr int kSamplesBlock = 48;  // full block-processing passes

// Install an in-memory global CVM database (ExecuteCVMBlock reads CVM::g_cvmdb).
void InstallInMemoryCVMDB()
{
    CVM::g_cvmdb.reset(new CVM::CVMDatabase(
        fs::temp_directory_path() / fs::unique_path(),
        1 << 20, /*fMemory=*/true, /*fWipe=*/true));
}

void TeardownCVMDB()
{
    CVM::g_cvmdb.reset();
}

// Raw OP_RETURN builder ([OP_RETURN][raw bytes...], no pushdata) as the CVM /
// reputation parsers expect.
CScript MakeRawOpReturn(const std::vector<uint8_t>& raw)
{
    CScript script;
    script << OP_RETURN;
    script.insert(script.end(), raw.begin(), raw.end());
    return script;
}

// Standard P2PKH transaction (no CVM/WoT marker at all).
CTransactionRef MakeStandardP2PKHTx(const CKeyID& payee, CAmount value)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0); // non-null (not coinbase)
    mtx.vin[0].scriptSig = CScript() << OP_1;
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(value, GetScriptForDestination(payee));
    return MakeTransactionRef(std::move(mtx));
}

// Web-of-Trust reputation-vote transaction ("REP" + 0x01 + serialized vote),
// matching the reputation.cpp encoding used elsewhere in the CVM.
CTransactionRef MakeReputationVoteTx(const uint160& target, int64_t voteValue,
                                     const std::string& reason)
{
    CVM::ReputationVoteTx v;
    v.targetAddress = target;
    v.voteValue = voteValue;
    v.reason = reason;
    std::vector<uint8_t> payload = v.Serialize();

    std::vector<uint8_t> raw = {'R', 'E', 'P', 0x01};
    raw.insert(raw.end(), payload.begin(), payload.end());

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, MakeRawOpReturn(raw));
    return MakeTransactionRef(std::move(mtx));
}

// Web-of-Trust reputation vote encoded as a CVM OP_RETURN (CVM1 magic +
// REPUTATION_VOTE op type) so it reaches the WoT branch of
// ConsensusValidator::ExtractGasInfo (which must return false => 100% fee to
// the miner, no gas-fee split).
CTransactionRef MakeCVMWoTVoteTx(const uint160& target, int16_t voteValue)
{
    CVM::CVMReputationData d;
    d.targetAddress = target;
    d.voteValue = voteValue;
    d.timestamp = 1700000000;
    std::vector<uint8_t> data = d.Serialize();

    CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::REPUTATION_VOTE, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, script);
    return MakeTransactionRef(std::move(mtx));
}

// Contract-deploy transaction using the softfork.cpp encoding (CVMDeployData),
// consumed by ConsensusValidator::ExtractGasInfo.
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

BOOST_FIXTURE_TEST_SUITE(cvm_workstream1_preserve_tests, BasicTestingSetup)

// ===========================================================================
// 3.1  A transaction whose (1:1-equivalent) rate produces a cost equal to the
//      previous fixed rate keeps the SAME fee/subsidy split.
//
// The fee/subsidy split is governed by the DETERMINISTIC consensus rules in
// ConsensusValidator (reputation-discount tiers, free-gas eligibility, and the
// max-subsidy calculation). These are NOT flagged paths (Workstream 5 changes
// the *benefit assessment* and sender/load resolution, not these split rules),
// so they must reproduce identical results after the fix. We pin them as golden
// vectors here.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_1_reputation_discount_split_golden)
{
    using CVM::ConsensusValidator;
    const uint64_t base = 1000000; // base gas cost

    // Discount tiers: <50 => 0%, [50,70) => 0%, [70,80) => 25%, [80,90) => 50%,
    // >=90 => 75%. (Tier-1 threshold is 50 but its percentage is 0%.)
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(0,   base), 0u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(49,  base), 0u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(50,  base), 0u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(69,  base), 0u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(70,  base), 250000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(79,  base), 250000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(80,  base), 500000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(89,  base), 500000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(90,  base), 750000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetConsensusReputationDiscount(100, base), 750000u);

    // Free-gas eligibility threshold (>= 80) is a deterministic split rule.
    BOOST_CHECK(!ConsensusValidator::IsEligibleForFreeGas(79));
    BOOST_CHECK(ConsensusValidator::IsEligibleForFreeGas(80));
    BOOST_CHECK(ConsensusValidator::IsEligibleForFreeGas(100));

    // Max subsidy: <50 => 0, else (gasUsed*rep)/100 capped at 100000.
    BOOST_CHECK_EQUAL(ConsensusValidator::GetMaxAllowedSubsidy(49, 100000), 0u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetMaxAllowedSubsidy(50, 100000), 50000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetMaxAllowedSubsidy(80, 100000), 80000u);
    BOOST_CHECK_EQUAL(ConsensusValidator::GetMaxAllowedSubsidy(100, 100000), 100000u); // cap
}

// Property 21 (3.1): the deterministic split rules are stable across the whole
// reputation range — discount never exceeds the base, is monotonic
// non-decreasing in reputation, and equals a re-derivation of the tier table.
BOOST_AUTO_TEST_CASE(preserve_3_1_reputation_discount_split_property)
{
    using CVM::ConsensusValidator;

    uint64_t prev = 0;
    for (int rep = 0; rep <= 255; ++rep) {
        const uint64_t base = 1000000;
        uint64_t d = ConsensusValidator::GetConsensusReputationDiscount(
            static_cast<uint8_t>(rep), base);

        // Independent re-derivation of the documented tier table.
        uint64_t pct = 0;
        if (rep >= 90)      pct = 75;
        else if (rep >= 80) pct = 50;
        else if (rep >= 70) pct = 25;
        else                pct = 0;
        uint64_t expected = (base * pct) / 100;

        BOOST_REQUIRE_MESSAGE(d == expected,
            "Preservation 3.1 (property): discount mismatch at reputation " +
            std::to_string(rep));
        BOOST_REQUIRE_MESSAGE(d <= base,
            "Preservation 3.1 (property): discount exceeds base at reputation " +
            std::to_string(rep));
        BOOST_REQUIRE_MESSAGE(d >= prev,
            "Preservation 3.1 (property): discount not monotonic at reputation " +
            std::to_string(rep));
        prev = d;
    }

    // Free-gas eligibility is exactly the >= 80 threshold across the range.
    for (int rep = 0; rep <= 255; ++rep) {
        bool eligible = ConsensusValidator::IsEligibleForFreeGas(static_cast<uint8_t>(rep));
        BOOST_REQUIRE_EQUAL(eligible, rep >= 80);
    }
}

// 3.1 (WoT no-split): a Web-of-Trust operation carries NO gas-fee split — 100%
// of the fee goes to the miner. ConsensusValidator::ExtractGasInfo returns false
// for a WoT (REPUTATION_VOTE) transaction, both before and after the fix.
BOOST_AUTO_TEST_CASE(preserve_3_1_wot_tx_has_no_gas_fee_split)
{
    uint160 target;
    target.SetHex("00112233445566778899aabbccddeeff00112233");
    CTransactionRef tx = MakeCVMWoTVoteTx(target, 50);

    uint64_t gasUsed = 123; // sentinels; must be reset to 0 by ExtractGasInfo
    CAmount gasCost = 456;
    bool isContractGas = CVM::ConsensusValidator::ExtractGasInfo(*tx, gasUsed, gasCost);

    BOOST_CHECK_MESSAGE(!isContractGas,
        "Preservation 3.1: a WoT vote must NOT be treated as a gas-fee-bearing "
        "contract tx (100% fee to miner, no 70/30 split).");
    BOOST_CHECK_EQUAL(gasUsed, 0u);
    BOOST_CHECK_EQUAL(gasCost, CAmount(0));
}

// 3.1 (gas-USED basis unchanged): the gas *used* extracted from a deploy tx is
// the tx's gas limit — this accounting basis is unchanged by the fix (only the
// gas *cost* rate changes). For the 1:1-equivalent rate (price = 1), the cost is
// exactly gasUsed, which we document as the preserved baseline for that input.
BOOST_AUTO_TEST_CASE(preserve_3_1_gas_used_extraction_basis)
{
    const Consensus::Params& params = Params().GetConsensus();
    for (int i = 0; i < 32; ++i) {
        uint64_t gasLimit = 1 + InsecureRandRange(params.cvmMaxGasPerTx);
        CTransactionRef tx = MakeSoftforkDeployTx(gasLimit);

        uint64_t gasUsed = 0;
        CAmount gasCost = 0;
        bool ok = CVM::ConsensusValidator::ExtractGasInfo(*tx, gasUsed, gasCost);
        BOOST_REQUIRE_MESSAGE(ok,
            "Preservation 3.1: deploy tx gas info should be extractable (#" +
            std::to_string(i) + ")");

        // Preserved basis: gas used == gas limit.
        BOOST_CHECK_EQUAL(gasUsed, gasLimit);

        // 1:1-equivalent cost (price = 1) equals gas used; this is the split
        // baseline preserved for that rate.
        CAmount oneToOneCost = static_cast<CAmount>(gasUsed);
        BOOST_CHECK_EQUAL(oneToOneCost, static_cast<CAmount>(gasLimit));
    }
}

// ===========================================================================
// 3.2 / 3.17  A block with no CVM/EVM transactions and no gas subsidies (and no
// failed contract executions) validates, saves state, and finalizes EXACTLY as
// before: ExecuteCVMBlock accepts it and writes nothing to the contract DB.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_2_block_without_cvm_validates_unchanged)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex index;
    index.nHeight = params.cvmActivationHeight + 1; // CVM active
    index.nTime = 1700000000;

    CCoinsView backing;
    CCoinsViewCache view(&backing);

    // A block containing only standard + WoT transactions (no CVM/EVM content).
    CBlock block;
    std::vector<uint160> txAddrs;
    for (int i = 0; i < 4; ++i) {
        CKey key;
        key.MakeNewKey(true);
        CTransactionRef stdTx = MakeStandardP2PKHTx(key.GetPubKey().GetID(), 50 * COIN);
        block.vtx.push_back(stdTx);

        uint160 a;
        std::memcpy(a.begin(), stdTx->GetHash().begin(), 20);
        txAddrs.push_back(a);
    }
    uint160 voteTarget;
    voteTarget.SetHex("0123456789abcdef0123456789abcdef01234567");
    block.vtx.push_back(MakeReputationVoteTx(voteTarget, 10, "preserve-3.2"));

    // Accepted, exactly as before.
    bool accepted = CVM::ExecuteCVMBlock(block, &index, view, params);
    BOOST_CHECK_MESSAGE(accepted,
        "Preservation 3.2/3.17: a block with no CVM/EVM transactions and no "
        "subsidies must validate (be accepted) exactly as before.");

    // Nothing was written to the contract DB (no state change / finalize side
    // effects for a non-CVM block).
    for (const uint160& a : txAddrs) {
        CVM::Contract stored;
        BOOST_CHECK_MESSAGE(!CVM::g_cvmdb->ReadContract(a, stored),
            "Preservation 3.2/3.17: a non-CVM block must not write any contract "
            "state (unexpected write at " + a.ToString() + ").");
    }

    TeardownCVMDB();
}

// Property 21 (3.2/3.17): random blocks of standard/WoT transactions are always
// accepted by ExecuteCVMBlock and never write contract state.
BOOST_AUTO_TEST_CASE(preserve_3_2_block_without_cvm_property)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    for (int i = 0; i < kSamplesBlock; ++i) {
        CBlockIndex index;
        index.nHeight = params.cvmActivationHeight + 1;
        index.nTime = 1700000000 + i;

        CCoinsView backing;
        CCoinsViewCache view(&backing);

        CBlock block;
        std::vector<uint160> txAddrs;
        int nStd = 1 + static_cast<int>(InsecureRandRange(5)); // [1,5]
        for (int j = 0; j < nStd; ++j) {
            CKey key;
            key.MakeNewKey(InsecureRandBool());
            CAmount value = static_cast<CAmount>(InsecureRandRange(1000ULL * COIN));
            CTransactionRef stdTx = MakeStandardP2PKHTx(key.GetPubKey().GetID(), value);
            block.vtx.push_back(stdTx);

            uint160 a;
            std::memcpy(a.begin(), stdTx->GetHash().begin(), 20);
            txAddrs.push_back(a);
        }
        // Optionally include a WoT vote (still non-contract, no subsidy).
        if (InsecureRandBool()) {
            uint160 t;
            uint256 r = InsecureRand256();
            std::memcpy(t.begin(), r.begin(), 20);
            block.vtx.push_back(MakeReputationVoteTx(t, 5, "p" + std::to_string(i)));
        }

        bool accepted = CVM::ExecuteCVMBlock(block, &index, view, params);
        BOOST_REQUIRE_MESSAGE(accepted,
            "Preservation 3.2/3.17 (property): non-CVM block #" +
            std::to_string(i) + " was not accepted.");

        for (const uint160& a : txAddrs) {
            CVM::Contract stored;
            BOOST_REQUIRE_MESSAGE(!CVM::g_cvmdb->ReadContract(a, stored),
                "Preservation 3.2/3.17 (property): non-CVM block #" +
                std::to_string(i) + " wrote contract state at " + a.ToString());
        }
    }

    TeardownCVMDB();
}

// ===========================================================================
// 3.14  The canonical deployer+nonce contract-address scheme (contract.cpp,
//       Hash(deployer||nonce)[0:20]) is deterministic and unchanged.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_14_canonical_address_scheme)
{
    uint160 deployer;
    deployer.SetHex("0123456789abcdef0123456789abcdef01234567");
    const uint64_t nonce = 7;

    uint160 addr1 = CVM::GenerateContractAddress(deployer, nonce);
    uint160 addr2 = CVM::GenerateContractAddress(deployer, nonce);

    BOOST_CHECK_MESSAGE(addr1 == addr2,
        "Preservation 3.14: address generation must be deterministic.");

    // Matches the documented formula Hash(deployer || nonce)[0:20].
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << deployer << nonce;
    uint256 h = Hash(ss.begin(), ss.end());
    uint160 expected;
    std::memcpy(expected.begin(), h.begin(), 20);
    BOOST_CHECK_MESSAGE(addr1 == expected,
        "Preservation 3.14: address must equal Hash(deployer||nonce)[0:20]; got "
        + addr1.ToString() + " expected " + expected.ToString());

    // Golden vector: pin the numeric address so the scheme cannot silently
    // change (captured on the unfixed code; the fixed code must reproduce it).
    BOOST_CHECK_MESSAGE(
        addr1.ToString() == "a56cb9a042b34a3cd12fdb28af77ae338be1c5bc",
        "Preservation 3.14: canonical address golden vector changed; got " +
        addr1.ToString());

    // Distinct inputs => distinct addresses.
    BOOST_CHECK(CVM::GenerateContractAddress(deployer, nonce + 1) != addr1);
    uint160 deployer2 = deployer;
    *deployer2.begin() ^= 0xFF;
    BOOST_CHECK(CVM::GenerateContractAddress(deployer2, nonce) != addr1);
}

// Property 21 (3.14): for random (deployer, nonce) the scheme is deterministic
// and equals the documented formula.
BOOST_AUTO_TEST_CASE(preserve_3_14_canonical_address_property)
{
    for (int i = 0; i < kSamples; ++i) {
        uint160 deployer;
        uint256 r = InsecureRand256();
        std::memcpy(deployer.begin(), r.begin(), 20);
        uint64_t nonce = InsecureRand256().GetUint64(0);

        uint160 a = CVM::GenerateContractAddress(deployer, nonce);
        uint160 b = CVM::GenerateContractAddress(deployer, nonce);
        BOOST_REQUIRE_MESSAGE(a == b,
            "Preservation 3.14 (property): non-deterministic address at #" +
            std::to_string(i));

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << deployer << nonce;
        uint256 h = Hash(ss.begin(), ss.end());
        uint160 expected;
        std::memcpy(expected.begin(), h.begin(), 20);
        BOOST_REQUIRE_MESSAGE(a == expected,
            "Preservation 3.14 (property): formula mismatch at #" +
            std::to_string(i));
    }
}

// ===========================================================================
// 3.22  A zero-value deploy/call still exposes CALLVALUE = 0. The corrected
//       block-processing path passes the actual (zero) value, so this input is
//       unchanged. Verified directly against the OP_CALLVALUE handler.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_22_zero_value_callvalue_is_zero)
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_CALLVALUE));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    // Default-constructed state (and an explicit zero) both mean CALLVALUE == 0.
    for (bool explicitZero : {false, true}) {
        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        if (explicitZero) state.SetCallValue(0);

        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok, "bytecode execution did not complete");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        arith_uint256 callValue = state.Peek(0);
        BOOST_CHECK_MESSAGE(callValue == arith_uint256(),
            "Preservation 3.22: a zero-value call must expose CALLVALUE = 0; got "
            + callValue.GetHex());
    }
}

BOOST_AUTO_TEST_SUITE_END()
