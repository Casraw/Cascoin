// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Global Bug-Condition Exploration Test Suite
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 1: "Write global bug-condition exploration test suite"
 * Property 1: Bug Condition — Representative CVM defect counterexamples.
 *
 * PURPOSE
 * -------
 * These tests are written BEFORE any fix and are EXPECTED TO FAIL on the
 * current (unfixed) code. Each failing assertion is a *counterexample* that
 * confirms the corresponding root-cause hypothesis from design.md (Phase A).
 * A test that unexpectedly PASSES means the root cause for that clause is
 * refuted and must be re-analysed.
 *
 * Each case asserts the EXPECTED (post-fix, per Expected-Behavior clause 2.x)
 * behaviour, so it fails against the placeholder / hardcoded implementation.
 * The authoritative post-fix fix-property tests live in the per-workstream
 * tasks (tasks 3, 6, ...). This suite only needs to surface the defects.
 *
 * Representative cases (10), mapped to bugfix.md clauses:
 *   1.1  Block with subsidies exceeding per-block max is accepted.
 *   1.2  Deploy with gasPrice != 1 charged 1:1.
 *   1.16 cvmtx deploy produces txHash[0:20] address != canonical address.
 *   1.17 Deploy/call in block processing leaves contract un-executed.
 *   1.18 Reputation vote attributed to the zero address.
 *   1.21 Failed-block state not rolled back — leaked writes remain.
 *   1.23 OP_VERIFY_SIG with a forged signature pushes 1.
 *   1.24 OP_BALANCE pushes 0 for a funded account.
 *   1.6  Reputation signature of >=64 bytes but invalid passes verification.
 *   1.59 getvalidatorstats_security returns the static message, not real stats.
 *
 * Requirements: 1.1, 1.2, 1.6, 1.16, 1.17, 1.18, 1.21, 1.23, 1.24, 1.59
 */

#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>
#include <cvm/contract.h>
#include <cvm/cvmtx.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/reputation_signature.h>
#include <cvm/consensus_validator.h>
#include <cvm/block_validator.h>
#include <cvm/softfork.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/params.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>
#include <arith_uint256.h>
#include <fs.h>

#include <univalue.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>

// Forward declaration of the (non-static) RPC handler defined in
// cvm/security_rpc.cpp. It is not exported via a header, so we declare it here
// to invoke it directly (bug 1.59).
UniValue getvalidatorstats_security(const JSONRPCRequest& request);

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

// Build a raw OP_RETURN script exactly as the CVM parsers expect:
// [OP_RETURN][raw marker+version+type+payload...]. The parsers read every byte
// after the leading OP_RETURN opcode, so we append raw bytes (no pushdata).
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

// Reputation-vote transaction using the reputation.cpp encoding
// ("REP" + 0x01 + serialized ReputationVoteTx) — consumed by
// CVM::UpdateReputationScores / ParseReputationVoteTx.
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

// Contract-deploy transaction using the softfork.cpp encoding (CVMDeployData) —
// consumed by ConsensusValidator::ExtractGasInfo / ParseCVMOpReturn.
CTransactionRef MakeSoftforkDeployTx(uint64_t gasLimit)
{
    CVM::CVMDeployData d;
    d.gasLimit = gasLimit;
    d.codeHash = InsecureRand256();
    d.format = CVM::BytecodeFormat::CVM_NATIVE;
    std::vector<uint8_t> data = d.Serialize();

    CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, data);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, script);
    return MakeTransactionRef(std::move(mtx));
}

// Append a PUSH<size> immediate to bytecode.
void EmitPush1(std::vector<uint8_t>& code, uint8_t value)
{
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_PUSH));
    code.push_back(0x01);   // size = 1 byte
    code.push_back(value);
}

// Replicate the deployer-address derivation used by the block-processing deploy
// path (cvmtx.cpp ResolveDeployerAddress) when the first-input UTXO cannot be
// resolved: Hash(SER_GETHASH, first-input prevout)[0:20]. In these unit tests
// the deploy tx spends a random prevout that is absent from the (empty) UTXO
// set, so this fallback is always taken.
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

BOOST_FIXTURE_TEST_SUITE(cvm_functional_fix_explore_tests, BasicTestingSetup)

// ===========================================================================
// 1.1  Per-block subsidy maximum is not enforced during block processing.
//      Expected (2.1): a block whose CVM transactions' subsidies exceed the
//      per-block subsidy maximum SHALL be rejected.
//      Defect: block processing accumulates only gas and enforces only the gas
//      cap; there is no subsidy accumulation or subsidy-max check, so the block
//      is accepted regardless of subsidy total.
//      Counterexample: two deploys (1,000,000 gas each = 2,000,000 gas, under
//      the 10,000,000 gas cap) whose implied subsidies exceed any per-block
//      subsidy maximum are accepted (ExecuteCVMBlock returns true).
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_1_no_per_block_subsidy_max)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex index;
    index.nHeight = params.cvmActivationHeight + 1;
    index.nTime = 1700000000;

    CCoinsView backing;
    CCoinsViewCache view(&backing);

    CBlock block;
    block.vtx.push_back(MakeContractDeployTx({static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, 1000000));
    block.vtx.push_back(MakeContractDeployTx({static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, 1000000));

    bool accepted = CVM::ExecuteCVMBlock(block, &index, view, params);

    // EXPECTED (post-fix): the block is REJECTED because accumulated subsidies
    // exceed the per-block subsidy maximum. UNFIXED code has no subsidy
    // accounting at all, so it accepts the block.
    BOOST_CHECK_MESSAGE(
        !accepted,
        "Bug 1.1 confirmed: block processing enforces no per-block subsidy "
        "maximum; a block whose subsidies exceed the max is accepted "
        "(ExecuteCVMBlock returned true).");

    TeardownCVMDB();
}

// ===========================================================================
// 1.2  Gas cost extracted at a fixed 1:1 (1 satoshi-per-gas) rate.
//      Expected (2.2): gasCost = gasUsed * gasPrice, with the real gas price.
//      Defect: ConsensusValidator::ExtractGasInfo sets gasCost = gasUsed.
//      Counterexample: deploy with gasLimit=500000 yields gasCost=500000.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_2_gas_cost_one_to_one)
{
    const uint64_t kGasLimit = 500000;
    CTransactionRef tx = MakeSoftforkDeployTx(kGasLimit);

    uint64_t gasUsed = 0;
    CAmount gasCost = 0;
    bool ok = CVM::ConsensusValidator::ExtractGasInfo(*tx, gasUsed, gasCost);
    BOOST_REQUIRE_MESSAGE(ok, "ExtractGasInfo failed to parse the deploy tx");

    BOOST_CHECK_EQUAL(gasUsed, kGasLimit);

    // EXPECTED (post-fix): gasCost reflects gasUsed * gasPrice with the real
    // (non-1:1) gas price, so it must differ from the raw gas amount.
    // UNFIXED code returns gasCost == gasUsed (1 satoshi per gas).
    BOOST_CHECK_MESSAGE(
        gasCost != static_cast<CAmount>(gasUsed),
        "Bug 1.2 confirmed: gas cost is extracted at a fixed 1:1 rate "
        "(gasCost == gasUsed == " + std::to_string(gasUsed) +
        "); the actual gas price is ignored.");
}

// ===========================================================================
// 1.16 cvmtx block processing derives the contract address from txHash[0:20]
//      instead of the canonical GenerateContractAddress(deployer, nonce).
//      Expected (2.16): the contract is stored at the canonical deployer+nonce
//      address; nothing is stored at the raw-tx-hash address.
//      Counterexample: after ExecuteCVMBlock, a contract IS found at
//      txHash[0:20].
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_16_contract_address_from_txhash)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex index;
    index.nHeight = params.cvmActivationHeight + 1;
    index.nTime = 1700000000;

    CCoinsView backing;
    CCoinsViewCache view(&backing);

    CTransactionRef deployTx =
        MakeContractDeployTx({static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, 100000);

    CBlock block;
    block.vtx.push_back(deployTx);

    BOOST_REQUIRE(CVM::ExecuteCVMBlock(block, &index, view, params));

    // The buggy derivation: first 20 bytes of the transaction hash.
    uint256 txHash = deployTx->GetHash();
    uint160 txHashAddr;
    std::memcpy(txHashAddr.begin(), txHash.begin(), 20);

    CVM::Contract stored;
    bool foundAtTxHashAddr = CVM::g_cvmdb->ReadContract(txHashAddr, stored);

    // EXPECTED (post-fix): the canonical scheme is used, so NO contract is
    // stored at the raw-tx-hash address. UNFIXED code stores it there.
    BOOST_CHECK_MESSAGE(
        !foundAtTxHashAddr,
        "Bug 1.16 confirmed: deploy in ExecuteCVMBlock stores the contract at "
        "txHash[0:20] (" + txHashAddr.ToString() +
        ") instead of the canonical GenerateContractAddress(deployer, nonce).");

    TeardownCVMDB();
}

// ===========================================================================
// 1.17 Deploy/call in block processing is a no-op: the constructor/contract
//      code is never executed and gas is not really accounted.
//      Expected (2.17): the deploy path executes real work — here observed via
//      the stored contract's deployer being populated from the transaction.
//      Defect: ExecuteCVMBlock stores a Contract with an unset (null) deployer
//      and never runs the constructor.
//      Counterexample: stored contract has a null deployer.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_17_deploy_not_executed)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    CBlockIndex index;
    index.nHeight = params.cvmActivationHeight + 1;
    index.nTime = 1700000000;

    CCoinsView backing;
    CCoinsViewCache view(&backing);

    CTransactionRef deployTx =
        MakeContractDeployTx({static_cast<uint8_t>(CVM::OpCode::OP_STOP)}, 100000);

    CBlock block;
    block.vtx.push_back(deployTx);

    BOOST_REQUIRE(CVM::ExecuteCVMBlock(block, &index, view, params));

    // Read the contract at the CANONICAL address the production code
    // (ExecuteCVMBlock via cvmtx.cpp) now derives for this deploy — the same
    // GenerateContractAddress(deployer, nonce) scheme every CVM path uses
    // (bugfix 2.16/2.17), NOT the old txHash[0:20] location:
    //   deployer = ResolveDeployerAddress(tx). This deploy tx spends a random
    //     prevout that is absent from the (empty) UTXO set, so the address
    //     cannot be resolved and ResolveDeployerAddress falls back to
    //     Hash(SER_GETHASH, first-input prevout)[0:20] == BlockProcessorDeployer.
    //   nonce    = g_cvmdb->GetNextNonce(deployer) == 1 for a fresh deployer
    //     (the stored nonce starts at 0, is incremented, persisted and returned).
    //   addr     = GenerateContractAddress(deployer, 1).
    uint160 deployer = BlockProcessorDeployer(deployTx);
    uint160 canonicalAddr = CVM::GenerateContractAddress(deployer, 1);

    CVM::Contract stored;
    BOOST_REQUIRE_MESSAGE(
        CVM::g_cvmdb->ReadContract(canonicalAddr, stored),
        "deploy did not store a contract at the canonical address "
        "GenerateContractAddress(deployer, 1) to inspect");

    // EXPECTED (post-fix): a proper deploy resolves and records the deployer
    // (and runs the constructor). UNFIXED code leaves the deployer null and
    // performs no execution.
    BOOST_CHECK_MESSAGE(
        !stored.deployer.IsNull(),
        "Bug 1.17 confirmed: block-processing deploy path does no real work — "
        "the stored contract has a null deployer and the constructor is never "
        "executed (gas is merely accumulated as gasLimit).");

    TeardownCVMDB();
}

// ===========================================================================
// 1.18 Reputation votes are applied with a default-constructed (zero) voter.
//      Expected (2.18): the real voter is resolved before applying the vote; a
//      vote from an unresolved (zero) address must not be applied.
//      Defect: UpdateReputationScores passes uint160() to ApplyVote, and
//      ApplyVote accepts the zero address as a valid voter.
//      Counterexample: ApplyVote(zeroAddr, ...) succeeds.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_18_vote_attributed_to_zero_address)
{
    InstallInMemoryCVMDB();
    const Consensus::Params& params = Params().GetConsensus();

    uint160 target;
    target.SetHex("00112233445566778899aabbccddeeff00112233");

    // Exercise the real block-processing path (applies the vote with a zero
    // voter internally).
    CBlockIndex index;
    index.nHeight = params.asrsActivationHeight + 1;
    index.nTime = 1700000000;

    CBlock block;
    block.vtx.push_back(MakeReputationVoteTx(target, 50, "explore-1.18"));
    CVM::UpdateReputationScores(block, &index, params);

    // Direct demonstration of the defect: the zero address is accepted as a
    // voter and the vote is applied.
    CVM::ReputationSystem repSystem(*CVM::g_cvmdb);
    CVM::ReputationVoteTx vote;
    vote.targetAddress = target;
    vote.voteValue = 50;
    vote.reason = "explore-1.18";

    uint160 zeroVoter; // default-constructed => null / zero address
    bool applied = repSystem.ApplyVote(zeroVoter, vote, index.nTime);

    // EXPECTED (post-fix): a vote from an unresolved (zero) voter is NOT applied
    // — the real voter must be resolved first. UNFIXED code applies it.
    BOOST_CHECK_MESSAGE(
        !applied,
        "Bug 1.18 confirmed: a reputation vote from the zero address is "
        "accepted and applied; votes are attributed to the zero address "
        "instead of a resolved voter.");

    TeardownCVMDB();
}

// ===========================================================================
// 1.21 Failed-block contract state is not rolled back.
//      Expected (2.21): RollbackContractState reverts contract-state writes
//      made during the block.
//      Defect: RollbackContractState is a log-only no-op.
//      Counterexample: a contract written during the block still exists after
//      RollbackContractState.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_21_failed_block_state_not_rolled_back)
{
    CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                        1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    CVM::BlockValidator validator;
    validator.Initialize(&db);

    // Simulate a contract-state write performed during block execution.
    uint160 addr;
    addr.SetHex("aabbccddeeff00112233445566778899aabbccdd");
    CVM::Contract c;
    c.address = addr;
    c.code = {static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
    c.deploymentHeight = 1;
    BOOST_REQUIRE(db.WriteContract(addr, c));

    CVM::Contract before;
    BOOST_REQUIRE_MESSAGE(db.ReadContract(addr, before),
                          "precondition: contract write should be present");

    // Block validation failed — roll back the in-block writes.
    validator.RollbackContractState();

    CVM::Contract after;
    bool stillPresent = db.ReadContract(addr, after);

    // EXPECTED (post-fix): the in-block write is reverted, so the contract is
    // gone. UNFIXED code performs a log-only no-op, so it remains.
    BOOST_CHECK_MESSAGE(
        !stillPresent,
        "Bug 1.21 confirmed: RollbackContractState is a no-op; contract-state "
        "writes from a failed block are not reverted (leaked write remains).");
}

// ===========================================================================
// 1.23 OP_VERIFY_SIG pushes 1 (valid) for a forged signature.
//      Expected (2.23): real verification; push 1 only for a genuinely valid
//      signature.
//      Defect: HandleCrypto hardcodes verifyResult = true.
//      Counterexample: garbage message/signature/pubkey => result 1.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_23_verify_sig_accepts_forged)
{
    std::vector<uint8_t> code;
    EmitPush1(code, 0x11);  // message   (garbage)
    EmitPush1(code, 0x0A);  // signature (size indicator 10 => ECDSA path)
    EmitPush1(code, 0x22);  // pubkey    (garbage)
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_VERIFY_SIG));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    CVM::CVM vm;
    CVM::VMState state;
    state.SetGasLimit(1000000);
    bool ok = vm.Execute(code, state, /*storage=*/nullptr);
    BOOST_REQUIRE_MESSAGE(ok, "bytecode execution did not complete");
    BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

    arith_uint256 result = state.Peek(0);

    // EXPECTED (post-fix): forged inputs do not verify => result 0.
    // UNFIXED code hardcodes verifyResult = true => result 1.
    BOOST_CHECK_MESSAGE(
        result == arith_uint256(),
        "Bug 1.23 confirmed: OP_VERIFY_SIG pushed " + result.GetHex() +
        " (1) for a forged signature; no real secp256k1/FALCON verification is "
        "performed.");
}

// ===========================================================================
// 1.24 OP_BALANCE pushes 0 regardless of the account's balance.
//      Expected (2.24): push the account's actual balance.
//      Defect: HandleContext hardcodes value = 0 for OP_BALANCE.
//      Counterexample: OP_BALANCE result is always 0.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_24_balance_always_zero)
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_BALANCE));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    CVM::CVM vm;
    CVM::VMState state;
    state.SetGasLimit(1000000);
    // Give the executing contract a concrete address (a "funded" account in the
    // post-fix world would report a non-zero balance).
    uint160 addr;
    addr.SetHex("0123456789abcdef0123456789abcdef01234567");
    state.SetContractAddress(addr);
    // Fund the account through the VM's balance API (wired by the Workstream-2
    // fix). A funded account must report a non-zero balance from OP_BALANCE.
    // The funding is part of the test SETUP; the assertion below (balance != 0)
    // is unchanged.
    const uint64_t funded = 1234567;
    state.SetBalance(addr, funded);

    bool ok = vm.Execute(code, state, /*storage=*/nullptr);
    BOOST_REQUIRE_MESSAGE(ok, "bytecode execution did not complete");
    BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

    arith_uint256 balance = state.Peek(0);

    // EXPECTED (post-fix): a funded account reports a non-zero balance.
    // UNFIXED code always pushes 0.
    BOOST_CHECK_MESSAGE(
        balance != arith_uint256(),
        "Bug 1.24 confirmed: OP_BALANCE pushes 0 unconditionally, ignoring the "
        "account's actual balance.");
}

// ===========================================================================
// 1.6  Reputation signature verification only checks length (>=64 bytes).
//      Expected (2.6): ECDSA verification against the signer's public key;
//      reject signatures that do not verify even if of valid length.
//      Defect: ReputationSignature::Verify performs no ECDSA check.
//      Counterexample: a 64-byte forged signature verifies successfully.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_6_reputation_signature_length_only)
{
    CVM::ReputationSignature sig;
    sig.ecdsa_signature = std::vector<uint8_t>(64, 0xAB); // forged, but >= 64 bytes
    sig.signer_address.SetHex("1111111111111111111111111111111111111111");
    sig.signer_reputation = 50;
    sig.signature_timestamp = 1700000000;
    sig.reputation_proof_hash = InsecureRand256();

    uint256 messageHash = InsecureRand256();
    bool verified = sig.Verify(messageHash);

    // EXPECTED (post-fix): a forged signature fails ECDSA verification.
    // UNFIXED code accepts any signature that is >= 64 bytes.
    BOOST_CHECK_MESSAGE(
        !verified,
        "Bug 1.6 confirmed: a 64-byte forged reputation signature passes "
        "verification; only the length is checked, no ECDSA verification is "
        "performed against a public key.");
}

// ===========================================================================
// 1.59 getvalidatorstats_security returns a static placeholder message.
//      Expected (2.59): return real per-validator statistics documented in the
//      RPC help text (total/accurate/inaccurate validations, abstentions,
//      accuracy rate, reputation, last activity).
//      Defect: the handler returns a single "message" field.
//      Counterexample: the documented "total_validations" field is absent.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_1_59_validator_stats_placeholder)
{
    JSONRPCRequest request;
    request.params = UniValue(UniValue::VARR); // no arguments
    request.fHelp = false;

    UniValue result = getvalidatorstats_security(request);

    // EXPECTED (post-fix): the result contains the documented statistics
    // fields. UNFIXED code returns only a static "message".
    BOOST_CHECK_MESSAGE(
        result.exists("total_validations"),
        "Bug 1.59 confirmed: getvalidatorstats_security returns a static "
        "placeholder ('" +
        (result.exists("message") ? result["message"].get_str() : std::string("<no message>")) +
        "') instead of the documented per-validator statistics.");
}

BOOST_AUTO_TEST_SUITE_END()
