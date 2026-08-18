// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 1 Exploratory Fix-Property Test Suite
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 3: "Write Workstream-1 exploratory fix-property tests (BEFORE fix)"
 *
 * Workstream 1 = Consensus-critical accounting & block processing
 * (bugfix.md clauses 1.1, 1.2, 1.16–1.22, 1.60, 1.61).
 *
 * PURPOSE
 * -------
 * These are the authoritative *fix-property* tests for Workstream 1. Each test
 * encodes the EXPECTED (post-fix, per Expected-Behavior clause 2.x) behaviour as
 * a property, primarily property-based (randomised) per the design's Scoped PBT
 * approach. Written BEFORE the fix, they are EXPECTED TO FAIL on the current
 * (unfixed) code — every failure is a counterexample confirming the defect. After
 * the Workstream-1 fixes land (task 5.8), the SAME tests must pass unchanged.
 *
 * Properties covered (design.md Correctness Properties):
 *   P1  Bug Condition — Per-block subsidy limit enforced                  (1.1, 1.19)
 *   P2  Bug Condition — Gas cost = gasUsed * gasPrice                     (1.2, 1.20)
 *   P3  Bug Condition — cvmtx address == canonical GenerateContractAddress(1.16)
 *   P4  Bug Condition — Constructor/contract code executed in block proc  (1.17)
 *   P5  Bug Condition — Reputation vote attributed to real voter          (1.18)
 *   P6  Bug Condition — Failed block state rolled back atomically         (1.21)
 *   P7  Bug Condition — Coinbase 70/30 validator split enforced           (1.22)
 *   P19 Bug Condition — Primary-path value/block-hash + commit flush      (1.60, 1.61)
 *
 * Scoped PBT approach (design):
 *   - random (deployer, nonce)      -> cvmtx address == canonical address (P3)
 *   - random subsidized blocks      -> reject-iff-over-max                (P1)
 *   - random (gasUsed, gasPrice)    -> cost equality                      (P2)
 *   - random forced-fail blocks     -> committed state == pre-block state (P6)
 *
 * Requirements: 1.1, 1.2, 1.16, 1.17, 1.18, 1.19, 1.20, 1.21, 1.22, 1.60, 1.61
 */

#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>
#include <cvm/contract.h>
#include <cvm/cvmtx.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/consensus_validator.h>
#include <cvm/block_validator.h>
#include <cvm/validator_compensation.h>
#include <cvm/softfork.h>
#include <cvm/blockprocessor.h>

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
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace {

// Sample counts: cheaper properties get more samples, heavier ones (that run a
// full block-processing pass) get fewer to keep the suite fast.
static constexpr int kSamplesCheap = 256;
static constexpr int kSamplesBlock = 48;

// Install an in-memory global CVM database (used by ExecuteCVMBlock /
// UpdateReputationScores, which read CVM::g_cvmdb).
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

// Build a raw OP_RETURN script exactly as the contract.cpp / reputation.cpp
// parsers expect: [OP_RETURN][raw marker+version+type+payload...] (raw bytes,
// no pushdata).
CScript MakeRawOpReturn(const std::vector<uint8_t>& raw)
{
    CScript script;
    script << OP_RETURN;
    script.insert(script.end(), raw.begin(), raw.end());
    return script;
}

// Contract-deploy transaction using the contract.cpp encoding
// ("CVM" + 0x01 + DEPLOY + serialized ContractDeployTx) — consumed by
// CVM::ExecuteCVMBlock / GetContractTxType.
CTransactionRef MakeContractDeployTx(const std::vector<uint8_t>& code, uint64_t gasLimit)
{
    CVM::ContractDeployTx d;
    d.code = code;
    d.gasLimit = gasLimit;
    std::vector<uint8_t> payload = d.Serialize();

    std::vector<uint8_t> raw = {'C', 'V', 'M', 0x01,
                                static_cast<uint8_t>(CVM::ContractTxType::DEPLOY)};
    raw.insert(raw.end(), payload.begin(), payload.end());

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, MakeRawOpReturn(raw));
    return MakeTransactionRef(std::move(mtx));
}

// Contract-deploy transaction using the softfork.cpp encoding (CVMDeployData) —
// consumed by ConsensusValidator::ExtractGasInfo / ParseCVMOpReturn and by
// CVMBlockProcessor::ProcessBlock.
CTransactionRef MakeSoftforkDeployTx(uint64_t gasLimit,
                                     const std::vector<uint8_t>& bytecode = {})
{
    CVM::CVMDeployData d;
    d.gasLimit = gasLimit;
    d.format = CVM::BytecodeFormat::CVM_NATIVE;
    d.bytecode = bytecode;
    d.codeHash = bytecode.empty() ? InsecureRand256()
                                  : Hash(bytecode.begin(), bytecode.end());
    std::vector<uint8_t> data = d.Serialize();

    CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, script);
    return MakeTransactionRef(std::move(mtx));
}

// The (buggy) address derivation used by cvmtx.cpp ProcessCVMBlock:
// first 20 bytes of the transaction hash.
uint160 TxHashAddr(const CTransactionRef& tx)
{
    uint256 txHash = tx->GetHash();
    uint160 addr;
    std::memcpy(addr.begin(), txHash.begin(), 20);
    return addr;
}

// Replicate the deployer-address derivation used by CVMBlockProcessor::
// ProcessDeploy (Hash(SER_GETHASH, first-input prevout)[0:20]).
uint160 BlockProcessorDeployer(const CTransactionRef& tx)
{
    uint160 deployer;
    if (!tx->vin.empty()) {
        CHashWriter hw(SER_GETHASH, 0);
        hw << tx->vin[0].prevout;
        uint256 h = hw.GetHash();
        std::memcpy(deployer.begin(), h.begin(), 20);
    }
    return deployer;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream1_fix_property_tests, BasicTestingSetup)

// ===========================================================================
// Property 1 — Per-block subsidy maximum is enforced.               (1.1, 1.19)
//
// Expected (2.1/2.19): block processing SHALL accumulate the actual
// per-transaction subsidies and REJECT the block when the accumulated subsidy
// exceeds the per-block subsidy maximum.
//
// Scoped PBT: random subsidized blocks -> reject-iff-over-max. Each sample is a
// block carrying several subsidy-eligible CVM deploys whose accumulated subsidy
// is meant to exceed the per-block subsidy maximum, while the raw gas total is
// kept UNDER cvmMaxGasPerBlock so the pre-existing gas-cap check cannot be the
// cause of any rejection — a rejection can only come from real subsidy
// accounting.
//
// UNFIXED: ExecuteCVMBlock performs NO subsidy accounting at all (the
// accumulation loop is a TODO and the total is always 0); it enforces only the
// gas cap, so these under-gas-cap blocks are accepted -> the property FAILS,
// confirming the missing per-block subsidy-maximum enforcement (1.1) and the
// hardcoded benefit/gas accounting (1.19).
//
// NOTE: the exact per-block subsidy maximum (relative to the gas cap) is set by
// the Workstream-1 fix (task 5.1); this exploratory test only needs to surface
// that no subsidy accounting exists on the unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p1_per_block_subsidy_max_enforced_property)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    for (int i = 0; i < kSamplesBlock; ++i) {
        CBlockIndex index;
        index.nHeight = params.cvmActivationHeight + 1;
        index.nTime = 1700000000 + i;

        CCoinsView backing;
        CCoinsViewCache view(&backing);

        // Several 1M-gas subsidy-eligible deploys. Keep the count in [2, 9] so
        // the raw gas total (nDeploys * 1M) stays strictly below the 10M gas cap
        // (so the gas-cap check never fires) while the block still carries a
        // large aggregate subsidy.
        const uint64_t perTxGas = params.cvmMaxGasPerTx; // 1M
        int nDeploys = 2 + static_cast<int>(InsecureRandRange(8)); // [2, 9]
        uint64_t totalGas = static_cast<uint64_t>(nDeploys) * perTxGas;
        BOOST_REQUIRE_MESSAGE(totalGas < params.cvmMaxGasPerBlock,
            "test setup: subsidy block must stay under the gas cap");

        CBlock block;
        for (int d = 0; d < nDeploys; ++d) {
            block.vtx.push_back(MakeContractDeployTx(
                {static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, perTxGas));
        }

        bool accepted = CVM::ExecuteCVMBlock(block, &index, view, params);

        // EXPECTED (post-fix): rejected because the accumulated subsidy exceeds
        // the per-block subsidy maximum. UNFIXED: no subsidy accounting exists,
        // and the block is under the gas cap -> accepted.
        BOOST_CHECK_MESSAGE(!accepted,
            "P1 (1.1/1.19): a block carrying " + std::to_string(nDeploys) +
            " subsidy-eligible deploys (total gas " + std::to_string(totalGas) +
            ", under the " + std::to_string(params.cvmMaxGasPerBlock) +
            " gas cap) was accepted; no per-block subsidy accounting/maximum is "
            "enforced (sample #" + std::to_string(i) + ").");
    }

    TeardownCVMDB();
}

// ===========================================================================
// Property 2 — Gas cost = gasUsed * gasPrice.                       (1.2, 1.20)
//
// Expected (2.2): gas cost SHALL be computed from the actual gas used and the
// transaction's gas price, not a hardcoded 1:1 (1 satoshi-per-gas) rate.
//
// Scoped PBT: random gas amounts. ConsensusValidator::ExtractGasInfo currently
// sets gasCost = gasUsed (price == 1). The fixed code applies the real gas
// price (!= 1), so gasCost must differ from the raw gas amount for gasUsed > 0.
//
// UNFIXED: gasCost == gasUsed -> the property FAILS (bug confirmed).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p2_gas_cost_uses_real_gas_price_property)
{
    const Consensus::Params& params = Params().GetConsensus();

    for (int i = 0; i < kSamplesCheap; ++i) {
        // Random non-zero gas amount within the per-tx cap.
        uint64_t gasLimit = 1 + InsecureRandRange(params.cvmMaxGasPerTx);
        CTransactionRef tx = MakeSoftforkDeployTx(gasLimit);

        uint64_t gasUsed = 0;
        CAmount gasCost = 0;
        bool ok = CVM::ConsensusValidator::ExtractGasInfo(*tx, gasUsed, gasCost);
        BOOST_REQUIRE_MESSAGE(ok,
            "P2: ExtractGasInfo failed to parse deploy tx #" + std::to_string(i));
        BOOST_REQUIRE_EQUAL(gasUsed, gasLimit);

        // EXPECTED (post-fix): gasCost = gasUsed * gasPrice with a real gas price
        // (!= 1 satoshi/gas), so it must not equal the raw gas amount.
        // UNFIXED: gasCost == gasUsed (fixed 1:1 rate).
        BOOST_CHECK_MESSAGE(
            gasCost != static_cast<CAmount>(gasUsed),
            "P2 (1.2/1.20): gas cost is extracted at a fixed 1:1 rate "
            "(gasCost == gasUsed == " + std::to_string(gasUsed) +
            "); the actual gas price is ignored (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 3 — cvmtx contract address == canonical address.             (1.16)
//
// Expected (2.16): ProcessCVMBlock (cvmtx.cpp) SHALL derive the contract address
// via the canonical GenerateContractAddress(deployer, nonce) scheme, so it
// equals the address produced by every other CVM path for the same deployer and
// nonce — and NOTHING is stored at the raw-tx-hash address.
//
// Scoped PBT: for random deploys, assert (a) the canonical scheme is
// deterministic and matches Hash(deployer||nonce)[0:20] for random
// (deployer, nonce) [baseline], and (b) after ExecuteCVMBlock, NO contract is
// stored at txHash[0:20].
//
// UNFIXED: ExecuteCVMBlock stores the contract at txHash[0:20] -> (b) FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p3_contract_address_canonical_property)
{
    // (a) Canonical scheme baseline — deterministic & matches the formula for
    //     random (deployer, nonce). This is the address cvmtx MUST use.
    for (int i = 0; i < kSamplesCheap; ++i) {
        uint160 deployer;
        uint256 r = InsecureRand256();
        std::memcpy(deployer.begin(), r.begin(), 20);
        uint64_t nonce = InsecureRand256().GetUint64(0);

        uint160 a = CVM::GenerateContractAddress(deployer, nonce);
        uint160 b = CVM::GenerateContractAddress(deployer, nonce);
        BOOST_REQUIRE_MESSAGE(a == b,
            "P3: canonical address must be deterministic (#" + std::to_string(i) + ")");

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << deployer << nonce;
        uint256 h = Hash(ss.begin(), ss.end());
        uint160 expected;
        std::memcpy(expected.begin(), h.begin(), 20);
        BOOST_REQUIRE_MESSAGE(a == expected,
            "P3: canonical address must equal Hash(deployer||nonce)[0:20] (#" +
            std::to_string(i) + ")");
    }

    // (b) Bug-condition: the block-processing path must NOT store the contract at
    //     txHash[0:20].
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    for (int i = 0; i < kSamplesBlock; ++i) {
        CBlockIndex index;
        index.nHeight = params.cvmActivationHeight + 1;
        index.nTime = 1700000000 + i;

        CCoinsView backing;
        CCoinsViewCache view(&backing);

        uint64_t gasLimit = 1 + InsecureRandRange(params.cvmMaxGasPerTx / 2);
        CTransactionRef deployTx = MakeContractDeployTx(
            {static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, gasLimit);

        CBlock block;
        block.vtx.push_back(deployTx);
        BOOST_REQUIRE(CVM::ExecuteCVMBlock(block, &index, view, params));

        CVM::Contract stored;
        bool foundAtTxHashAddr = CVM::g_cvmdb->ReadContract(TxHashAddr(deployTx), stored);

        // EXPECTED (post-fix): canonical scheme used -> nothing at txHash[0:20].
        // UNFIXED: contract stored at txHash[0:20].
        BOOST_CHECK_MESSAGE(!foundAtTxHashAddr,
            "P3 (1.16): deploy in ExecuteCVMBlock stored the contract at "
            "txHash[0:20] (" + TxHashAddr(deployTx).ToString() + ") instead of "
            "the canonical GenerateContractAddress(deployer, nonce) "
            "(sample #" + std::to_string(i) + ").");
    }

    TeardownCVMDB();
}

// ===========================================================================
// Property 4 — Constructor/contract code executed during block processing.(1.17)
//
// Expected (2.17): the deploy path SHALL execute the constructor and account for
// the deployer/real work — observed here via the stored contract having a
// populated (non-null) deployer.
//
// UNFIXED: ExecuteCVMBlock only accumulates gasLimit and stores a Contract with
// a null deployer (no execution) -> the property FAILS (bug confirmed).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p4_contract_executed_in_block_processing_property)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    for (int i = 0; i < kSamplesBlock; ++i) {
        CBlockIndex index;
        index.nHeight = params.cvmActivationHeight + 1;
        index.nTime = 1700000000 + i;

        CCoinsView backing;
        CCoinsViewCache view(&backing);

        uint64_t gasLimit = 1 + InsecureRandRange(params.cvmMaxGasPerTx / 2);
        CTransactionRef deployTx = MakeContractDeployTx(
            {static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, gasLimit);

        CBlock block;
        block.vtx.push_back(deployTx);
        BOOST_REQUIRE(CVM::ExecuteCVMBlock(block, &index, view, params));

        // Inspect the contract at the CANONICAL address the production code
        // (ExecuteCVMBlock, cvmtx.cpp) actually derives for this deploy — the
        // same address every other CVM path uses (bugfix 2.16/2.17), NOT the old
        // txHash[0:20] location (which P3 asserts is never written):
        //   deployer = ResolveDeployerAddress(tx). This test's deploy tx spends a
        //     random prevout that is absent from the (empty) UTXO set, so
        //     ExtractDeployerAddress cannot resolve it and ResolveDeployerAddress
        //     falls back to Hash(SER_GETHASH, first-input prevout)[0:20], i.e.
        //     exactly BlockProcessorDeployer(deployTx).
        //   nonce    = g_cvmdb->GetNextNonce(deployer). GetNextNonce reads the
        //     stored nonce (0 for a never-seen deployer), increments it, persists
        //     the result and returns it — so the FIRST deploy for a fresh deployer
        //     uses nonce 1. Every iteration builds a tx with a fresh random
        //     prevout, hence a unique deployer, so each deploy deterministically
        //     uses nonce 1.
        //   addr     = GenerateContractAddress(deployer, nonce).
        uint160 deployer = BlockProcessorDeployer(deployTx);
        uint160 canonicalAddr = CVM::GenerateContractAddress(deployer, 1);

        CVM::Contract stored;
        BOOST_REQUIRE_MESSAGE(CVM::g_cvmdb->ReadContract(canonicalAddr, stored),
            "P4: deploy did not store a contract at the canonical address "
            "GenerateContractAddress(deployer, 1) to inspect (#" +
            std::to_string(i) + ")");

        // EXPECTED (post-fix): a real deploy resolves and records the deployer
        // (and executes the constructor). UNFIXED: deployer is null, no execution.
        BOOST_CHECK_MESSAGE(!stored.deployer.IsNull(),
            "P4 (1.17): block-processing deploy path does no real work — the "
            "stored contract has a null deployer and the constructor is never "
            "executed (gas merely accumulated as gasLimit) (sample #" +
            std::to_string(i) + ").");
    }

    TeardownCVMDB();
}

// ===========================================================================
// Property 5 — Reputation vote attributed to the real voter.            (1.18)
//
// Expected (2.18): UpdateReputationScores SHALL resolve the real voter before
// applying a vote and never attribute it to the zero address. A vote from an
// unresolved (zero) voter must NOT be applied.
//
// Scoped PBT: random (target, voteValue). ApplyVote(zeroVoter, vote) must fail.
//
// UNFIXED: ApplyVote accepts the zero address and applies the vote -> the
// property FAILS (bug confirmed).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p5_vote_attributed_to_real_voter_property)
{
    InstallInMemoryCVMDB();
    CVM::ReputationSystem repSystem(*CVM::g_cvmdb);

    for (int i = 0; i < kSamplesCheap; ++i) {
        uint160 target;
        uint256 r = InsecureRand256();
        std::memcpy(target.begin(), r.begin(), 20);
        // Ensure a non-null target so the vote itself is well-formed.
        if (target.IsNull()) *target.begin() = 0x01;

        int64_t voteValue = static_cast<int64_t>(InsecureRandRange(201)) - 100; // [-100,100]

        CVM::ReputationVoteTx vote;
        vote.targetAddress = CVM::TrustNodeId::FromLegacyUint160(target);
        vote.voteValue = voteValue;
        vote.reason = "p5-" + std::to_string(i);

        uint160 zeroVoter; // default-constructed => zero / null address
        bool applied = repSystem.ApplyVote(zeroVoter, vote, 1700000000 + i);

        // EXPECTED (post-fix): a vote from an unresolved (zero) voter is rejected.
        // UNFIXED: it is accepted and applied.
        BOOST_CHECK_MESSAGE(!applied,
            "P5 (1.18): a reputation vote from the zero address was accepted and "
            "applied; votes must be attributed to a resolved voter, not the zero "
            "address (sample #" + std::to_string(i) + ").");
    }

    TeardownCVMDB();
}

// ===========================================================================
// Property 6 — Failed block state rolled back atomically.               (1.21)
//
// Expected (2.21): when a block is rejected, RollbackContractState SHALL revert
// all contract-state writes made during the block.
//
// Scoped PBT: random forced-fail blocks -> committed state == pre-block state.
// We write a random set of contracts (simulating in-block writes), then roll
// back, and assert none of them survive.
//
// UNFIXED: RollbackContractState is a log-only no-op -> writes remain -> the
// property FAILS (bug confirmed).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p6_failed_block_state_rolled_back_property)
{
    for (int i = 0; i < kSamplesBlock; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        CVM::BlockValidator validator;
        validator.Initialize(&db);

        // Simulate a random set of contract-state writes performed during the
        // (about-to-fail) block's execution.
        int nWrites = 1 + static_cast<int>(InsecureRandRange(8));
        std::vector<uint160> written;
        for (int w = 0; w < nWrites; ++w) {
            uint160 addr;
            uint256 r = InsecureRand256();
            std::memcpy(addr.begin(), r.begin(), 20);

            CVM::Contract c;
            c.address = addr;
            c.code = {static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            c.deploymentHeight = 1;
            BOOST_REQUIRE(db.WriteContract(addr, c));
            written.push_back(addr);
        }

        // Precondition: all writes are visible before rollback.
        for (const uint160& addr : written) {
            CVM::Contract tmp;
            BOOST_REQUIRE_MESSAGE(db.ReadContract(addr, tmp),
                "P6: precondition — in-block write should be present (#" +
                std::to_string(i) + ")");
        }

        // Block validation failed — revert the in-block writes.
        validator.RollbackContractState();

        // EXPECTED (post-fix): every in-block write is reverted.
        // UNFIXED: RollbackContractState is a no-op -> writes remain.
        for (const uint160& addr : written) {
            CVM::Contract tmp;
            bool stillPresent = db.ReadContract(addr, tmp);
            BOOST_CHECK_MESSAGE(!stillPresent,
                "P6 (1.21): RollbackContractState did not revert contract-state "
                "writes from a failed block — leaked write at " + addr.ToString() +
                " remains (sample #" + std::to_string(i) + ").");
        }
    }
}

// ===========================================================================
// Property 7 — Coinbase 70/30 validator split enforced.                 (1.22)
//
// Expected (2.22): CheckCoinbaseValidatorPayments SHALL enforce the 70/30
// validator payment split. A coinbase for a block that contains contract
// (gas-fee-bearing) transactions but pays 100% to the miner (no validator
// outputs) must be rejected, even when its total output matches the expected
// block reward + fees.
//
// UNFIXED: the 70/30 split validation is a TODO — the function only checks the
// total output and returns success -> the property FAILS (bug confirmed).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p7_coinbase_validator_split_enforced_property)
{
    for (int i = 0; i < kSamplesBlock; ++i) {
        const CAmount blockReward = (10 + static_cast<CAmount>(InsecureRandRange(90))) * COIN;

        // Coinbase that pays EVERYTHING to the miner (no validator outputs),
        // with total == blockReward.
        CMutableTransaction coinbase;
        coinbase.vin.resize(1);
        coinbase.vin[0].prevout.SetNull();
        CScript scriptSig;
        scriptSig << (i + 1); // BIP34-style height placeholder
        if (scriptSig.size() < 2) scriptSig << OP_0;
        coinbase.vin[0].scriptSig = scriptSig;
        coinbase.vout.resize(1);
        coinbase.vout[0].nValue = blockReward;
        coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

        // A contract deploy transaction implies gas fees whose 30% share is owed
        // to validators — so the coinbase above under-pays validators.
        uint64_t gasLimit = 1 + InsecureRandRange(500000ULL);
        CTransactionRef contractTx = MakeSoftforkDeployTx(gasLimit);

        CBlock block;
        block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
        block.vtx.push_back(contractTx);

        bool accepted = CheckCoinbaseValidatorPayments(block, blockReward);

        // EXPECTED (post-fix): rejected because the 70/30 validator split is not
        // honoured. UNFIXED: only the total is checked -> accepted.
        BOOST_CHECK_MESSAGE(!accepted,
            "P7 (1.22): CheckCoinbaseValidatorPayments accepted a coinbase that "
            "pays 100% to the miner for a block containing gas-fee-bearing "
            "contract transactions; the 70/30 validator split is not enforced "
            "(sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 19 — Primary-path value/block-hash + CommitExecutionState flush.
//                                                                 (1.60, 1.61)
//
// Expected (2.60/2.61): a deploy/call processed through the primary
// blockprocessor.cpp path SHALL pass the actual transaction value and the real
// block hash to the Enhanced VM (so BLOCKHASH/CALLVALUE are correct), and the
// resulting contract-state writes SHALL be durably persisted.
//
// We deploy a CVM-native constructor that stores the block hash into storage
// slot 0 (OP_BLOCKHASH; OP_PUSH 0; OP_SSTORE) through CVMBlockProcessor::
// ProcessBlock, then read slot 0 back from the database at the canonical deploy
// address. The stored value reflects whatever block hash the primary path
// supplied to the VM.
//
// UNFIXED: ProcessDeploy passes uint256() as the block hash (and 0 as the
// value), so slot 0 is the null hash (or is never written) -> the property
// FAILS (bug confirmed). NOTE: full dual-path (cvmtx vs blockprocessor)
// value/block-hash + durability reconciliation is additionally covered by the
// functional integration test (task 29).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p19_primary_path_blockhash_context_property)
{
    for (int i = 0; i < kSamplesBlock; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);

        // Constructor: store the block hash into storage slot 0.
        //   OP_BLOCKHASH            -> pushes block hash (value)
        //   OP_PUSH 1 0x00          -> pushes key 0
        //   OP_SSTORE               -> storage[0] = block hash
        //   OP_STOP
        std::vector<uint8_t> code;
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_BLOCKHASH));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_PUSH));
        code.push_back(0x01);
        code.push_back(0x00);
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_SSTORE));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

        uint64_t gasLimit = 100000;
        CTransactionRef deployTx = MakeSoftforkDeployTx(gasLimit, code);

        CBlock block;
        block.vtx.push_back(deployTx);

        int height = 1000 + i;
        CVM::CVMBlockProcessor::ProcessBlock(block, height, db);

        // Canonical deploy address = GenerateContractAddress(deployer, nonce=0),
        // deployer derived exactly as the current ProcessDeploy does.
        //
        // ProcessDeploy resolves the deployer from the tx inputs via the UTXO
        // set (ResolveInputSenderAddress, bugfix 2.56). In this in-memory unit
        // test pcoinsTip does not contain the (random) prevout UTXO, so the
        // resolution fails and the deployer stays NULL (uint160()); the nonce
        // read for a fresh/null deployer is 0. The contract is therefore stored
        // at GenerateContractAddress(uint160(), 0). Mirror that here.
        uint160 deployer; // null — matches production resolution in this harness
        uint160 contractAddr = CVM::GenerateContractAddress(deployer, 0);

        // Read storage slot 0 (key == 0).
        uint256 storedBlockHash; // defaults to null
        db.Load(contractAddr, uint256(), storedBlockHash);

        // EXPECTED (post-fix): the primary path passes the REAL (non-null) block
        // hash to the VM, so the stored value is non-null. UNFIXED: it passes
        // uint256() (null) -> the stored value is null (or nothing is written).
        BOOST_CHECK_MESSAGE(!storedBlockHash.IsNull(),
            "P19 (1.60/1.61): the primary block-processing path supplied an empty "
            "block hash (uint256()) to the Enhanced VM, so a BLOCKHASH-storing "
            "constructor persisted the null hash instead of the real block hash "
            "(sample #" + std::to_string(i) + ").");
    }
}

BOOST_AUTO_TEST_SUITE_END()
