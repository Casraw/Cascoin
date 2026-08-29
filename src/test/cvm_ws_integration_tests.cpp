// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Cross-Workstream Integration Test Suite
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 30: "Multi-node HAT consensus + coinbase-split + cross-chain integration
 *          tests"
 *
 * SCOPE / RATIONALE
 * -----------------
 * This suite provides *integration-level* coverage for three already-fixed and
 * unit-tested workstreams, exercising them through more realistic, multi-actor
 * paths than the single-behaviour unit tests:
 *
 *   • Workstream 4 — HAT v2 distributed consensus  (Expected-Behavior 2.8, 2.47)
 *   • Workstream 1 — Coinbase 70/30 validator split (Expected-Behavior 2.22)
 *   • Workstream 7 — Cross-chain bridging & proofs   (Expected-Behavior 2.33/2.34/2.35)
 *
 * Why a C++ boost-test integration suite rather than a Python functional
 * (multi-node regtest) test?
 *
 *   The three behaviours under test are NOT reachable through any regtest RPC:
 *   a grep of the src/rpc tree shows there is no RPC that drives
 *   SendValidationChallenge / ProcessValidatorResponse (HAT challenge dispatch
 *   and response accumulation), SendTrustAttestation (cross-chain dispatch), or
 *   CheckCoinbaseValidatorPayments (coinbase-split validation). Multi-node HAT
 *   consensus requires the live P2P validator-challenge flow and cross-chain
 *   dispatch requires an external LayerZero/CCIP endpoint — neither of which a
 *   functional regtest harness can stand up. A functional test would therefore
 *   be unable to actually exercise these paths. Per the task guidance ("choose
 *   C++ boost-test integration suite(s) if the functional multi-node path is not
 *   feasible in this environment; that's acceptable and preferred over a test
 *   that can't run"), this suite drives the real subsystems in-process at a
 *   higher, multi-validator / multi-transaction / round-trip level.
 *
 * This suite exercises PRODUCTION code only; it does not modify it.
 */

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <cvm/hat_consensus.h>
#include <cvm/mempool_manager.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <cvm/cvmdb.h>

#include <cvm/validator_compensation.h>
#include <cvm/consensus_validator.h>
#include <cvm/softfork.h>

#include <cvm/cross_chain_bridge.h>
#include <cvm/trust_attestation.h>

#include <amount.h>
#include <fs.h>
#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <uint256.h>
#include <utiltime.h>
#include <version.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Shared helpers (mirroring the per-workstream unit suites so the integration
// tests drive the same real code paths).
// ---------------------------------------------------------------------------

// Fresh in-memory CVM database for a single test.
std::unique_ptr<CVM::CVMDatabase> MakeTempDb()
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    return std::unique_ptr<CVM::CVMDatabase>(
        new CVM::CVMDatabase(testPath, 8 << 20, /*fMemory=*/true, /*fWipe=*/true));
}

// Random non-null 20-byte address.
uint160 RandAddress()
{
    uint160 addr;
    uint256 r = InsecureRand256();
    std::memcpy(addr.begin(), r.begin(), 20);
    if (addr.IsNull()) *addr.begin() = 0x01;
    return addr;
}

// Minimal transaction with a stable, salt-derived hash.
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
// its (key-derived) validator address, so ValidationResponse::VerifySignature()
// succeeds standalone — exactly the wire form the P2P response path produces.
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

// Drive one full multi-validator consensus round through the MempoolManager:
// initiate a validation session, then feed `nResponses` distinct signed,
// WoT-tagged responses (all voting `vote`) exactly as the P2P response path
// would. Returns the resulting transaction state.
CVM::TransactionState RunConsensusRound(CVM::MempoolManager& mempool,
                                        CVM::HATConsensusValidator& validator,
                                        const CTransaction& tx,
                                        int nResponses,
                                        CVM::ValidationVote vote,
                                        bool hasWoT)
{
    uint160 sender = RandAddress();
    CVM::HATv2Score selfReported;
    selfReported.address = sender;
    selfReported.finalScore = 75;
    CVM::ValidationRequest request = validator.InitiateValidation(tx, selfReported);

    for (int v = 0; v < nResponses; ++v) {
        CKey key;
        key.MakeNewKey(true);
        CVM::ValidationResponse resp = MakeSignedHatResponse(
            tx.GetHash(), request.challengeNonce, key, vote,
            /*finalScore=*/75, hasWoT);
        mempool.ProcessValidatorResponse(resp);
    }

    return mempool.GetHATValidationState(tx.GetHash());
}

// Install / tear down the global CVM database (CalculateBlockValidatorPayments
// and CreateCoinbaseWithValidatorPayments read CVM::g_cvmdb for validator
// participation records).
void InstallGlobalCVMDB()
{
    CVM::g_cvmdb.reset(new CVM::CVMDatabase(
        fs::temp_directory_path() / fs::unique_path(),
        1 << 20, /*fMemory=*/true, /*fWipe=*/true));
}
void TeardownGlobalCVMDB() { CVM::g_cvmdb.reset(); }

// Contract-deploy transaction in the softfork.cpp encoding consumed by
// ConsensusValidator::ExtractGasInfo (so it is a gas-fee-bearing contract tx).
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

// A plain standard (non-CVM) transaction: no CVM OP_RETURN, so ExtractGasInfo
// returns false and no validator share is owed.
CTransactionRef MakeStandardTx(uint32_t salt)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), salt);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(1 * COIN, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(mtx));
}

// A coinbase paying 100% to the miner (single output), with total == amount.
CTransactionRef MakeMinerOnlyCoinbase(CAmount amount, int height)
{
    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    CScript scriptSig;
    scriptSig << height;
    if (scriptSig.size() < 2) scriptSig << OP_0;
    cb.vin[0].scriptSig = scriptSig;
    cb.vout.resize(1);
    cb.vout[0].nValue = amount;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;
    return MakeTransactionRef(std::move(cb));
}

// A TrustAttestation that passes ValidateAttestation (range + fresh timestamp).
CVM::TrustAttestation MakeValidAttestation(const uint160& address, int16_t score,
                                           CVM::AttestationSource source)
{
    CVM::TrustAttestation att;
    att.address = address;
    att.trustScore = score;                       // in [0,100]
    att.source = source;
    att.timestamp = static_cast<uint64_t>(GetTime());
    return att;
}

// Build a ReputationProof that verifies against a genuinely committed
// TrustStateProof leaf and carries a recoverable secp256k1 signature over its
// hash — i.e. a full valid cross-chain round-trip payload.
CVM::ReputationProof MakeCommittedReputationProof(const CKey& key,
                                                  const uint160& address,
                                                  uint8_t score)
{
    // 1) Build a committed trust-state proof (leaf + one genuine sibling folded
    //    into the state root, exactly as TrustStateProof::VerifyMerkleProof and
    //    GenerateTrustStateProof compute it).
    CVM::TrustStateProof stateProof;
    stateProof.address = address;
    stateProof.trustScore = score;
    stateProof.blockHeight = 1 + InsecureRandRange(1000000ULL);

    CHashWriter leafWriter(SER_GETHASH, 0);
    leafWriter << stateProof.address;
    leafWriter << stateProof.trustScore;
    leafWriter << stateProof.blockHeight;
    uint256 leaf = leafWriter.GetHash();

    uint256 sibling = InsecureRand256();
    CHashWriter rootWriter(SER_GETHASH, 0);
    if (leaf < sibling) {
        rootWriter << leaf << sibling;
    } else {
        rootWriter << sibling << leaf;
    }
    stateProof.stateRoot = rootWriter.GetHash();
    stateProof.merkleProof = {sibling};

    // 2) Wrap it in a ReputationProof whose claims bind to the committed leaf.
    CVM::ReputationProof proof;
    proof.address = address;
    proof.reputation = score;
    proof.timestamp = static_cast<uint64_t>(GetTime());
    proof.sourceChainSelector = 1;

    CDataStream ssProof(SER_DISK, CLIENT_VERSION);
    ssProof << stateProof;
    proof.proof.assign(ssProof.begin(), ssProof.end());

    // 3) Sign the proof hash with a recoverable (compact) secp256k1 signature.
    std::vector<uint8_t> sig;
    key.SignCompact(proof.GetHash(), sig);
    proof.signature = sig;

    return proof;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_ws_integration_tests, BasicTestingSetup)

// ###########################################################################
// #  A. MULTI-VALIDATOR HAT CONSENSUS INTEGRATION (Workstream 4; 2.8, 2.47)  #
// ###########################################################################

// ---------------------------------------------------------------------------
// A1 (2.8) — Challenge dispatch across a selected validator set reports success
// ONLY when a P2P message is actually dispatched. In this in-process harness
// there is no CConnman / connected peers, so every dispatch to every selected
// validator must report failure (nothing is transmitted). This is the
// multi-validator integration of the single-validator unit case.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hat_challenge_dispatch_reports_success_only_when_dispatched)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    CTransaction tx = MakeTx(9001);
    CVM::HATv2Score selfReported;
    selfReported.address = RandAddress();
    selfReported.finalScore = 60;
    CVM::ValidationRequest request = validator.InitiateValidation(tx, selfReported);

    // Dispatch a challenge to a whole set of selected validators. With no P2P
    // transport available, not one of them can be dispatched.
    int dispatchedCount = 0;
    const int kValidators = 12;
    for (int i = 0; i < kValidators; ++i) {
        uint160 target = RandAddress();
        if (validator.SendValidationChallenge(target, request)) {
            ++dispatchedCount;
        }
    }

    BOOST_CHECK_MESSAGE(dispatchedCount == 0,
        "2.8: SendValidationChallenge reported success for " +
        std::to_string(dispatchedCount) + "/" + std::to_string(kValidators) +
        " validators despite no P2P message being dispatched; success must be "
        "reported only when the challenge is actually transmitted.");
}

// ---------------------------------------------------------------------------
// A2 (2.47) — Accumulating a unanimous, WoT-backed ACCEPT set through the
// mempool manager drives the transaction to a VALIDATED consensus decision.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hat_response_accumulation_reaches_validated_consensus)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    CVM::MempoolManager mempool;
    mempool.Initialize(db.get());
    mempool.SetHATConsensusValidator(&validator);

    CTransaction tx = MakeTx(9100);
    CVM::TransactionState state = RunConsensusRound(
        mempool, validator, tx, /*nResponses=*/12,
        CVM::ValidationVote::ACCEPT, /*hasWoT=*/true);

    BOOST_CHECK_MESSAGE(state == CVM::TransactionState::VALIDATED,
        "2.47: a unanimous WoT-backed ACCEPT set did not drive the transaction "
        "to VALIDATED; ProcessValidatorResponse must accumulate responses and "
        "evaluate consensus.");
    BOOST_CHECK_MESSAGE(mempool.IsHATValidationComplete(tx.GetHash()),
        "2.47: HAT validation should report complete after a VALIDATED decision.");
}

// ---------------------------------------------------------------------------
// A3 (2.47) — A unanimous, WoT-backed REJECT set drives the transaction to a
// REJECTED consensus decision (the mirror of A2). Confirms the accumulated
// responses actually determine the outcome rather than defaulting to accept.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hat_response_accumulation_reaches_rejected_consensus)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    CVM::MempoolManager mempool;
    mempool.Initialize(db.get());
    mempool.SetHATConsensusValidator(&validator);

    CTransaction tx = MakeTx(9200);
    CVM::TransactionState state = RunConsensusRound(
        mempool, validator, tx, /*nResponses=*/12,
        CVM::ValidationVote::REJECT, /*hasWoT=*/true);

    BOOST_CHECK_MESSAGE(state == CVM::TransactionState::REJECTED,
        "2.47: a unanimous WoT-backed REJECT set did not drive the transaction "
        "to REJECTED; the accumulated responses must determine the decision.");
    BOOST_CHECK_MESSAGE(mempool.IsHATValidationComplete(tx.GetHash()),
        "2.47: HAT validation should report complete after a REJECTED decision.");
}

// ---------------------------------------------------------------------------
// A4 (2.47) — Below the minimum-validator quorum, no premature decision is
// made: the transaction stays PENDING_VALIDATION (responses accumulate but
// consensus is not evaluated until the quorum is met).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hat_below_quorum_stays_pending)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    CVM::MempoolManager mempool;
    mempool.Initialize(db.get());
    mempool.SetHATConsensusValidator(&validator);

    CTransaction tx = MakeTx(9300);
    // MIN_VALIDATORS is 10; feed only 9 responses.
    CVM::TransactionState state = RunConsensusRound(
        mempool, validator, tx, /*nResponses=*/9,
        CVM::ValidationVote::ACCEPT, /*hasWoT=*/true);

    BOOST_CHECK_MESSAGE(state == CVM::TransactionState::PENDING_VALIDATION,
        "2.47: consensus was decided before the validator quorum was reached; "
        "sub-quorum responses must only accumulate, not decide.");
    BOOST_CHECK_MESSAGE(!mempool.IsHATValidationComplete(tx.GetHash()),
        "2.47: HAT validation must not be complete below the validator quorum.");
}

// ---------------------------------------------------------------------------
// A5 (2.47) — With the quorum met but insufficient WoT coverage, the round is
// escalated to DISPUTED (not silently accepted). Exercises the consensus
// decision branch that requires DAO review.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hat_quorum_without_wot_coverage_is_disputed)
{
    auto db = MakeTempDb();
    CVM::SecureHAT hat(*db);
    CVM::TrustGraph graph(*db);
    CVM::HATConsensusValidator validator(*db, hat, graph);

    CVM::MempoolManager mempool;
    mempool.Initialize(db.get());
    mempool.SetHATConsensusValidator(&validator);

    CTransaction tx = MakeTx(9400);
    // Quorum met (12), but NO validator has a WoT connection -> WoT coverage
    // (>=30%) fails -> escalate to DAO review (DISPUTED).
    CVM::TransactionState state = RunConsensusRound(
        mempool, validator, tx, /*nResponses=*/12,
        CVM::ValidationVote::ACCEPT, /*hasWoT=*/false);

    BOOST_CHECK_MESSAGE(state == CVM::TransactionState::DISPUTED,
        "2.47: a quorum with insufficient WoT coverage must escalate to DISPUTED "
        "rather than reaching a VALIDATED/REJECTED decision.");
    BOOST_CHECK_MESSAGE(!mempool.IsHATValidationComplete(tx.GetHash()),
        "2.47: a DISPUTED transaction is not a completed validation.");
}

// ###########################################################################
// #  B. COINBASE 70/30 VALIDATOR SPLIT INTEGRATION (Workstream 1; 2.22)      #
// ###########################################################################

// ---------------------------------------------------------------------------
// B1 (2.22) — End-to-end: build a coinbase FROM real validator participation
// data (CreateCoinbaseWithValidatorPayments reads the participation records)
// for a block carrying multiple gas-fee-bearing contract transactions, then
// assert CheckCoinbaseValidatorPayments ACCEPTS that correctly-split coinbase.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(coinbase_split_accepts_correctly_split_block)
{
    InstallGlobalCVMDB();

    const int nHeight = 500001;
    const CAmount blockReward = 50 * COIN;

    // Two gas-fee-bearing contract deploys with participation data recorded.
    CBlock block;
    block.vtx.push_back(MakeMinerOnlyCoinbase(blockReward, nHeight)); // placeholder cb
    std::vector<CTransactionRef> contractTxs = {
        MakeSoftforkDeployTx(/*gasLimit=*/1000),
        MakeSoftforkDeployTx(/*gasLimit=*/2500),
    };
    for (const auto& ctx : contractTxs) {
        block.vtx.push_back(ctx);
        // Record validator participation (2 validators per tx) in the global DB.
        CVM::TransactionValidationRecord record;
        record.txHash = ctx->GetHash();
        record.blockHeight = nHeight;
        record.validators = {RandAddress(), RandAddress()};
        BOOST_REQUIRE(CVM::g_cvmdb->WriteValidatorParticipation(ctx->GetHash(), record));
    }

    // Build the correct coinbase (miner + validator outputs) from participation.
    CMutableTransaction coinbase;
    BOOST_REQUIRE_MESSAGE(
        CreateCoinbaseWithValidatorPayments(coinbase, block, CScript() << OP_TRUE,
                                            blockReward, nHeight, /*nFees=*/0),
        "2.22: building a coinbase with validator payments from participation "
        "data should succeed.");
    // Replace the placeholder coinbase with the properly split one.
    block.vtx[0] = MakeTransactionRef(CTransaction(coinbase));

    // The constructed coinbase pays out the validator share, so validation of
    // the 70/30 split must ACCEPT it. Total coinbase output == blockReward.
    BOOST_CHECK_MESSAGE(CheckCoinbaseValidatorPayments(block, blockReward),
        "2.22: a coinbase built with the correct 70/30 validator split (from "
        "participation data) was rejected; correctly-split coinbases must be "
        "accepted.");

    // Sanity: there is at least one validator output (the split is non-trivial).
    BOOST_CHECK_MESSAGE(block.vtx[0]->vout.size() >= 2,
        "2.22: expected the correctly-split coinbase to carry validator outputs.");

    TeardownGlobalCVMDB();
}

// ---------------------------------------------------------------------------
// B2 (2.22) — For the SAME gas-fee-bearing block content, a coinbase that pays
// 100% to the miner (no validator outputs) is REJECTED: the owed 30% validator
// share is not honoured.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(coinbase_split_rejects_underpaying_block)
{
    InstallGlobalCVMDB();

    const int nHeight = 500002;
    const CAmount blockReward = 50 * COIN;

    CBlock block;
    block.vtx.push_back(MakeMinerOnlyCoinbase(blockReward, nHeight)); // 100% to miner
    block.vtx.push_back(MakeSoftforkDeployTx(/*gasLimit=*/1000));
    block.vtx.push_back(MakeSoftforkDeployTx(/*gasLimit=*/2500));

    BOOST_CHECK_MESSAGE(!CheckCoinbaseValidatorPayments(block, blockReward),
        "2.22: a coinbase paying 100% to the miner was accepted for a block "
        "containing gas-fee-bearing contract transactions; the owed 30% "
        "validator share must be enforced.");

    TeardownGlobalCVMDB();
}

// ---------------------------------------------------------------------------
// B3 (2.22) — A block with only standard (non-contract) transactions owes NO
// validator share, so a 100%-to-miner coinbase is correctly ACCEPTED. Confirms
// the split is enforced ONLY when contract gas fees are present.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(coinbase_split_accepts_when_no_gas_fees_owed)
{
    InstallGlobalCVMDB();

    const int nHeight = 500003;
    const CAmount blockReward = 50 * COIN;

    CBlock block;
    block.vtx.push_back(MakeMinerOnlyCoinbase(blockReward, nHeight));
    block.vtx.push_back(MakeStandardTx(1));
    block.vtx.push_back(MakeStandardTx(2));

    BOOST_CHECK_MESSAGE(CheckCoinbaseValidatorPayments(block, blockReward),
        "2.22: a 100%-to-miner coinbase was rejected for a block with no "
        "gas-fee-bearing transactions; no validator share is owed in that case.");

    TeardownGlobalCVMDB();
}

// ###########################################################################
// #  C. CROSS-CHAIN PROOF ROUND-TRIP INTEGRATION (Workstream 7; 2.33/34/35)  #
// ###########################################################################

// ---------------------------------------------------------------------------
// C1 (2.33) — A ReputationProof that binds to a genuinely committed source
// state (a merkle-consistent TrustStateProof) and carries a recoverable
// signature VERIFIES; tampering with the claimed reputation (breaking the bind
// to committed state) makes verification FAIL. Full round-trip against
// committed source state.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(crosschain_reputation_proof_roundtrip_against_committed_state)
{
    for (int i = 0; i < 16; ++i) {
        CKey key;
        key.MakeNewKey(true);
        uint160 addr = RandAddress();
        uint8_t score = static_cast<uint8_t>(InsecureRandRange(101));

        CVM::ReputationProof proof = MakeCommittedReputationProof(key, addr, score);

        BOOST_CHECK_MESSAGE(proof.Verify(),
            "2.33: a reputation proof bound to genuinely committed source state "
            "failed to verify (sample #" + std::to_string(i) + ").");

        // Tamper the claimed reputation so it no longer binds to the committed
        // leaf: verification must fail.
        CVM::ReputationProof tampered = proof;
        tampered.reputation = static_cast<uint8_t>((score + 1) % 101);
        BOOST_CHECK_MESSAGE(!tampered.Verify(),
            "2.33: a reputation proof whose claim no longer binds to the "
            "committed state still verified (sample #" + std::to_string(i) + ").");
    }
}

// ---------------------------------------------------------------------------
// C2 (2.34) — A cross-chain send reports success ONLY when actually dispatched.
// A destination chain WITH a configured, active bridge endpoint dispatches (and
// reports success); the default endpoint-less chain cannot dispatch (and must
// report failure). Round-trips both branches of the dispatch decision.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(crosschain_send_reports_success_only_when_dispatched)
{
    auto db = MakeTempDb();
    CVM::CrossChainTrustBridge bridge(db.get());

    // A destination chain WITH an active bridge endpoint.
    CVM::ChainConfig configured;
    configured.chainId = 4242;
    configured.chainName = "integration-endpoint";
    configured.isActive = true;
    configured.bridgeEndpoint = "https://layerzero.example/endpoint";
    bridge.AddSupportedChain(configured);

    // The default Ethereum chain (id 1) is supported+active but endpoint-less.
    const CVM::ChainConfig* eth = bridge.GetChainConfig(1);
    BOOST_REQUIRE(eth != nullptr && eth->isActive && eth->bridgeEndpoint.empty());

    for (int i = 0; i < 16; ++i) {
        uint160 addr = RandAddress();
        CVM::TrustAttestation att = MakeValidAttestation(
            addr, static_cast<int16_t>(InsecureRandRange(101)),
            CVM::AttestationSource::OTHER);

        // Dispatched via the configured endpoint -> success.
        BOOST_CHECK_MESSAGE(bridge.SendTrustAttestation(4242, addr, att),
            "2.34: a send to a chain with an active bridge endpoint did not "
            "report success even though it was dispatched (sample #" +
            std::to_string(i) + ").");

        // No endpoint -> nothing dispatched -> failure.
        BOOST_CHECK_MESSAGE(!bridge.SendTrustAttestation(1, addr, att),
            "2.34: a send to a chain with no bridge endpoint reported success "
            "without dispatching a message (sample #" + std::to_string(i) + ").");
    }
}

// ---------------------------------------------------------------------------
// C3 (2.35) — GetAttestations returns ALL committed attestations for an address
// across multiple source chains (iterating committed state), not just cached
// ones. Commit several attestations from distinct sources, then retrieve them.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(crosschain_get_attestations_returns_all_committed)
{
    auto db = MakeTempDb();
    CVM::CrossChainTrustBridge bridge(db.get());

    uint160 addr = RandAddress();
    const std::vector<CVM::AttestationSource> sources = {
        CVM::AttestationSource::ETHEREUM_MAINNET,
        CVM::AttestationSource::POLYGON,
        CVM::AttestationSource::ARBITRUM,
    };

    for (size_t i = 0; i < sources.size(); ++i) {
        CVM::TrustAttestation att = MakeValidAttestation(
            addr, static_cast<int16_t>(40 + i * 10), sources[i]);
        // Distinct timestamps so the storage keys do not collide.
        att.timestamp = static_cast<uint64_t>(GetTime()) - i;
        BOOST_REQUIRE_MESSAGE(bridge.StoreAttestation(att),
            "2.35: committing an attestation to the database should succeed.");
    }

    std::vector<CVM::TrustAttestation> got = bridge.GetAttestations(addr);

    BOOST_CHECK_MESSAGE(got.size() == sources.size(),
        "2.35: GetAttestations returned " + std::to_string(got.size()) +
        " attestations; expected all " + std::to_string(sources.size()) +
        " committed attestations to be returned by iterating committed state.");

    // A different address has no committed attestations.
    std::vector<CVM::TrustAttestation> none = bridge.GetAttestations(RandAddress());
    BOOST_CHECK_MESSAGE(none.empty(),
        "2.35: GetAttestations returned attestations for an address with none "
        "committed.");
}

BOOST_AUTO_TEST_SUITE_END()
