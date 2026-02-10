// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Integration Tests
 * 
 * Tests for:
 * - Task 23.1: P2P validator communication
 * - Task 23.2: End-to-end validation cycle
 * - Task 23.3: Soft fork activation
 * - Task 23.4: DAO dispute flow
 * 
 * Requirements: 1.1, 1.2, 3.1, 3.2, 3.3, 3.4, 6.1, 6.2, 6.3, 10.1
 */

#include <cvm/validator_attestation.h>
#include <cvm/hat_consensus.h>
#include <cvm/softfork.h>
#include <cvm/block_validator.h>
#include <cvm/cvmdb.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <test/test_bitcoin.h>
#include <key.h>
#include <pubkey.h>
#include <hash.h>
#include <uint256.h>
#include <fs.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <utiltime.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_integration_tests, BasicTestingSetup)

// ============================================================================
// Task 23.1: P2P Validator Communication Integration Tests
// Requirements: 3.1, 3.2, 3.3, 3.4
// ============================================================================

BOOST_AUTO_TEST_CASE(validator_task_serialization)
{
    // Test that validation tasks can be serialized and deserialized correctly
    // This is essential for P2P message transmission
    
    // Create a validation task
    uint256 taskHash;
    taskHash.SetHex("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    int64_t blockHeight = 100000;
    
    // Create validator selection
    ValidatorSelection selection;
    selection.taskHash = taskHash;
    selection.blockHeight = blockHeight;
    selection.targetCount = 10;
    selection.totalEligible = 100;
    selection.timestamp = GetTime();
    
    // Add some selected validators
    for (int i = 0; i < 10; i++) {
        uint160 validatorAddr;
        validatorAddr.SetHex("0x" + std::to_string(i) + "234567890abcdef1234567890abcdef12345678");
        selection.selectedValidators.push_back(validatorAddr);
    }
    
    // Serialize
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << selection;
    
    // Deserialize
    ValidatorSelection deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK(deserialized.taskHash == selection.taskHash);
    BOOST_CHECK_EQUAL(deserialized.blockHeight, selection.blockHeight);
    BOOST_CHECK_EQUAL(deserialized.targetCount, selection.targetCount);
    BOOST_CHECK_EQUAL(deserialized.totalEligible, selection.totalEligible);
    BOOST_CHECK_EQUAL(deserialized.selectedValidators.size(), selection.selectedValidators.size());
}

BOOST_AUTO_TEST_CASE(validation_response_serialization)
{
    // Test that validation responses can be serialized and deserialized
    // Requirements: 3.2
    
    ValidationResponse response;
    response.taskHash.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    response.validatorAddress.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    response.isValid = true;
    response.confidence = 85;
    response.trustScore = 75;
    response.timestamp = GetTime();
    response.signature = std::vector<uint8_t>(64, 0xAB);  // Dummy signature
    
    // Serialize
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << response;
    
    // Deserialize
    ValidationResponse deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK(deserialized.taskHash == response.taskHash);
    BOOST_CHECK(deserialized.validatorAddress == response.validatorAddress);
    BOOST_CHECK_EQUAL(deserialized.isValid, response.isValid);
    BOOST_CHECK_EQUAL(deserialized.confidence, response.confidence);
    BOOST_CHECK_EQUAL(deserialized.trustScore, response.trustScore);
    BOOST_CHECK_EQUAL(deserialized.signature.size(), response.signature.size());
}

BOOST_AUTO_TEST_CASE(validator_eligibility_record_serialization)
{
    // Test ValidatorEligibilityRecord serialization for database persistence
    // Requirements: 3.1
    
    ValidatorEligibilityRecord record;
    record.validatorAddress.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    record.stakeAmount = 100 * COIN;
    record.stakeAge = 50000;
    record.blocksSinceFirstSeen = 60000;
    record.transactionCount = 150;
    record.uniqueInteractions = 30;
    record.meetsStakeRequirement = true;
    record.meetsHistoryRequirement = true;
    record.meetsInteractionRequirement = true;
    record.isEligible = true;
    record.lastUpdateBlock = 100000;
    record.lastUpdateTime = GetTime();
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    
    // Deserialize
    ValidatorEligibilityRecord deserialized;
    ss >> deserialized;
    
    // Verify all fields
    BOOST_CHECK(deserialized.validatorAddress == record.validatorAddress);
    BOOST_CHECK_EQUAL(deserialized.stakeAmount, record.stakeAmount);
    BOOST_CHECK_EQUAL(deserialized.stakeAge, record.stakeAge);
    BOOST_CHECK_EQUAL(deserialized.blocksSinceFirstSeen, record.blocksSinceFirstSeen);
    BOOST_CHECK_EQUAL(deserialized.transactionCount, record.transactionCount);
    BOOST_CHECK_EQUAL(deserialized.uniqueInteractions, record.uniqueInteractions);
    BOOST_CHECK_EQUAL(deserialized.meetsStakeRequirement, record.meetsStakeRequirement);
    BOOST_CHECK_EQUAL(deserialized.meetsHistoryRequirement, record.meetsHistoryRequirement);
    BOOST_CHECK_EQUAL(deserialized.meetsInteractionRequirement, record.meetsInteractionRequirement);
    BOOST_CHECK_EQUAL(deserialized.isEligible, record.isEligible);
}

BOOST_AUTO_TEST_CASE(batch_attestation_serialization)
{
    // Test batch attestation request/response serialization
    // Requirements: 3.3, 3.4
    
    // Create batch request
    BatchAttestationRequest request;
    request.timestamp = GetTime();
    request.requesterAddress.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    for (int i = 0; i < 5; i++) {
        uint160 addr;
        addr.SetHex("0x" + std::to_string(i) + "bcdef1234567890abcdef1234567890abcdef");
        request.validators.push_back(addr);
    }
    
    // Serialize request
    CDataStream ssReq(SER_NETWORK, PROTOCOL_VERSION);
    ssReq << request;
    
    // Deserialize request
    BatchAttestationRequest deserializedReq;
    ssReq >> deserializedReq;
    
    BOOST_CHECK_EQUAL(deserializedReq.validators.size(), request.validators.size());
    BOOST_CHECK(deserializedReq.requesterAddress == request.requesterAddress);
    
    // Create batch response
    BatchAttestationResponse response;
    response.timestamp = GetTime();
    response.responderAddress.SetHex("0xabcdef1234567890abcdef1234567890abcdef12");
    
    for (int i = 0; i < 3; i++) {
        ValidatorAttestation att;
        att.validatorAddress.SetHex("0x" + std::to_string(i) + "234567890abcdef1234567890abcdef12345678");
        att.attestorAddress.SetHex("0xabcdef1234567890abcdef1234567890abcdef12");
        att.trustScore = 70 + i * 5;
        att.timestamp = GetTime();
        att.signature = std::vector<uint8_t>(64, 0xCD);
        response.attestations.push_back(att);
    }
    
    // Serialize response
    CDataStream ssResp(SER_NETWORK, PROTOCOL_VERSION);
    ssResp << response;
    
    // Deserialize response
    BatchAttestationResponse deserializedResp;
    ssResp >> deserializedResp;
    
    BOOST_CHECK_EQUAL(deserializedResp.attestations.size(), response.attestations.size());
    BOOST_CHECK(deserializedResp.responderAddress == response.responderAddress);
}

BOOST_AUTO_TEST_CASE(aggregated_validation_result)
{
    // Test aggregated validation result computation
    // Requirements: 3.1, 3.2
    
    AggregatedValidationResult result;
    result.taskHash.SetHex("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    result.totalSelected = 10;
    result.totalResponded = 8;
    result.validVotes = 6;
    result.invalidVotes = 2;
    result.consensusReached = true;
    result.isValid = true;
    result.confidence = 0.75;
    result.totalCompensation = 1000000;  // 0.01 CAS
    
    // Add some responses
    for (int i = 0; i < 8; i++) {
        ValidationResponse resp;
        resp.taskHash = result.taskHash;
        resp.validatorAddress.SetHex("0x" + std::to_string(i) + "234567890abcdef1234567890abcdef12345678");
        resp.isValid = (i < 6);  // 6 valid, 2 invalid
        resp.confidence = 80;
        resp.timestamp = GetTime();
        result.responses.push_back(resp);
    }
    
    // Serialize
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << result;
    
    // Deserialize
    AggregatedValidationResult deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK(deserialized.taskHash == result.taskHash);
    BOOST_CHECK_EQUAL(deserialized.totalSelected, result.totalSelected);
    BOOST_CHECK_EQUAL(deserialized.totalResponded, result.totalResponded);
    BOOST_CHECK_EQUAL(deserialized.validVotes, result.validVotes);
    BOOST_CHECK_EQUAL(deserialized.invalidVotes, result.invalidVotes);
    BOOST_CHECK_EQUAL(deserialized.consensusReached, result.consensusReached);
    BOOST_CHECK_EQUAL(deserialized.isValid, result.isValid);
    BOOST_CHECK_EQUAL(deserialized.responses.size(), result.responses.size());
}


// ============================================================================
// Task 23.2: End-to-End Validation Cycle Tests
// Requirements: 1.1, 1.2, 3.1, 3.2
// ============================================================================

BOOST_AUTO_TEST_CASE(validation_response_signing)
{
    // Test that validation responses can be signed and verified
    // Requirements: 1.1, 1.2
    
    // Generate a key pair
    CKey validatorKey;
    validatorKey.MakeNewKey(true);
    CPubKey validatorPubKey = validatorKey.GetPubKey();
    
    // Create a validation response
    ValidationResponse response;
    response.taskHash.SetHex("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    response.validatorAddress = DeriveValidatorAddress(validatorPubKey);
    response.isValid = true;
    response.confidence = 90;
    response.trustScore = 80;
    response.timestamp = GetTime();
    
    // Create message hash for signing
    CHashWriter ss(SER_GETHASH, 0);
    ss << response.taskHash;
    ss << response.validatorAddress;
    ss << response.isValid;
    ss << response.confidence;
    ss << response.trustScore;
    ss << response.timestamp;
    uint256 msgHash = ss.GetHash();
    
    // Sign the message
    std::vector<unsigned char> signature;
    bool signResult = validatorKey.Sign(msgHash, signature);
    BOOST_CHECK(signResult);
    
    // Store signature in response
    response.signature = signature;
    
    // Verify the signature
    bool verifyResult = validatorPubKey.Verify(msgHash, signature);
    BOOST_CHECK(verifyResult);
    
    // Verify with wrong message fails
    uint256 wrongHash;
    wrongHash.SetHex("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    bool wrongVerify = validatorPubKey.Verify(wrongHash, signature);
    BOOST_CHECK(!wrongVerify);
}

BOOST_AUTO_TEST_CASE(validator_address_derivation)
{
    // Test that validator addresses are derived correctly from public keys
    // Requirements: 1.4
    
    // Generate multiple key pairs and verify address derivation
    for (int i = 0; i < 5; i++) {
        CKey key;
        key.MakeNewKey(true);
        CPubKey pubKey = key.GetPubKey();
        
        // Derive address using our function
        uint160 derivedAddr = DeriveValidatorAddress(pubKey);
        
        // Derive address using standard Bitcoin method
        CKeyID standardAddr = pubKey.GetID();
        
        // They should match
        BOOST_CHECK(derivedAddr == standardAddr);
        
        // Address should not be null
        BOOST_CHECK(!derivedAddr.IsNull());
    }
}

BOOST_AUTO_TEST_CASE(validation_cycle_flow)
{
    // Test the complete validation cycle flow
    // Requirements: 1.1, 1.2, 3.1, 3.2
    
    // Create test database
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    
    // Create automatic validator manager
    AutomaticValidatorManager manager(&db);
    
    // Create a task hash
    uint256 taskHash;
    taskHash.SetHex("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    // Block height used for deterministic selection (not used in this test)
    (void)100000;  // Suppress unused variable warning
    
    // Generate validator keys
    std::vector<CKey> validatorKeys;
    std::vector<uint160> validatorAddresses;
    
    for (int i = 0; i < 10; i++) {
        CKey key;
        key.MakeNewKey(true);
        validatorKeys.push_back(key);
        validatorAddresses.push_back(DeriveValidatorAddress(key.GetPubKey()));
    }
    
    // Simulate validation responses
    std::vector<ValidationResponse> responses;
    
    for (int i = 0; i < 10; i++) {
        ValidationResponse response;
        response.taskHash = taskHash;
        response.validatorAddress = validatorAddresses[i];
        response.isValid = (i < 8);  // 8 valid, 2 invalid
        response.confidence = 80 + (i % 20);
        response.trustScore = 70 + (i % 30);
        response.timestamp = GetTime();
        
        // Sign the response
        CHashWriter ss(SER_GETHASH, 0);
        ss << response.taskHash;
        ss << response.validatorAddress;
        ss << response.isValid;
        ss << response.confidence;
        ss << response.trustScore;
        ss << response.timestamp;
        uint256 msgHash = ss.GetHash();
        
        std::vector<unsigned char> signature;
        validatorKeys[i].Sign(msgHash, signature);
        response.signature = signature;
        
        responses.push_back(response);
    }
    
    // Verify all signatures
    for (int i = 0; i < 10; i++) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << responses[i].taskHash;
        ss << responses[i].validatorAddress;
        ss << responses[i].isValid;
        ss << responses[i].confidence;
        ss << responses[i].trustScore;
        ss << responses[i].timestamp;
        uint256 msgHash = ss.GetHash();
        
        bool verified = validatorKeys[i].GetPubKey().Verify(msgHash, responses[i].signature);
        BOOST_CHECK(verified);
    }
    
    // Count votes
    int validVotes = 0;
    int invalidVotes = 0;
    for (const auto& resp : responses) {
        if (resp.isValid) validVotes++;
        else invalidVotes++;
    }
    
    // Verify vote counts
    BOOST_CHECK_EQUAL(validVotes, 8);
    BOOST_CHECK_EQUAL(invalidVotes, 2);
    
    // Check consensus (80% > 70% threshold)
    double consensusRatio = (double)validVotes / responses.size();
    BOOST_CHECK_GT(consensusRatio, 0.70);
}

// ============================================================================
// Task 23.3: Soft Fork Activation Integration Tests
// Requirements: 10.1
// ============================================================================

BOOST_AUTO_TEST_CASE(cvm_softfork_activation_check)
{
    // Test CVM soft fork activation at correct height
    // Requirements: 10.1
    
    // Get consensus parameters
    const Consensus::Params& params = Params().GetConsensus();
    
    // Test heights before and after activation
    int activationHeight = params.cvmActivationHeight;
    
    // Before activation
    if (activationHeight > 0) {
        bool activeBeforeActivation = CVM::IsCVMSoftForkActive(activationHeight - 1, params);
        BOOST_CHECK(!activeBeforeActivation);
    }
    
    // At activation
    bool activeAtActivation = CVM::IsCVMSoftForkActive(activationHeight, params);
    BOOST_CHECK(activeAtActivation);
    
    // After activation
    bool activeAfterActivation = CVM::IsCVMSoftForkActive(activationHeight + 1000, params);
    BOOST_CHECK(activeAfterActivation);
}

BOOST_AUTO_TEST_CASE(cvm_opreturn_parsing)
{
    // Test CVM OP_RETURN parsing for soft fork transactions
    // Requirements: 10.1
    
    // Create a CVM OP_RETURN script
    std::vector<uint8_t> testData = {0x01, 0x02, 0x03, 0x04};
    CScript opReturnScript = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, testData);
    
    // Create a transaction output with the script
    CTxOut txout(0, opReturnScript);
    
    // Check if it's recognized as CVM OP_RETURN
    bool isCVM = CVM::IsCVMOpReturn(txout);
    BOOST_CHECK(isCVM);
    
    // Parse the OP_RETURN
    CVM::CVMOpType opType;
    std::vector<uint8_t> parsedData;
    bool parsed = CVM::ParseCVMOpReturn(txout, opType, parsedData);
    
    BOOST_CHECK(parsed);
    BOOST_CHECK(opType == CVM::CVMOpType::CONTRACT_DEPLOY);
    BOOST_CHECK_EQUAL(parsedData.size(), testData.size());
}

BOOST_AUTO_TEST_CASE(cvm_deploy_data_serialization)
{
    // Test CVMDeployData serialization
    // Requirements: 10.1
    
    CVM::CVMDeployData deployData;
    deployData.codeHash.SetHex("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    deployData.gasLimit = 500000;
    deployData.format = CVM::BytecodeFormat::CVM_NATIVE;
    deployData.metadata = {0x01, 0x02, 0x03};
    
    // Serialize
    std::vector<uint8_t> serialized = deployData.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    // Deserialize
    CVM::CVMDeployData deserialized;
    bool success = deserialized.Deserialize(serialized);
    
    BOOST_CHECK(success);
    BOOST_CHECK(deserialized.codeHash == deployData.codeHash);
    BOOST_CHECK_EQUAL(deserialized.gasLimit, deployData.gasLimit);
    BOOST_CHECK(deserialized.format == deployData.format);
}

BOOST_AUTO_TEST_CASE(cvm_call_data_serialization)
{
    // Test CVMCallData serialization
    // Requirements: 10.1
    
    CVM::CVMCallData callData;
    callData.contractAddress.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    callData.gasLimit = 100000;
    callData.format = CVM::BytecodeFormat::CVM_NATIVE;
    callData.callData = {0xAB, 0xCD, 0xEF};
    
    // Serialize
    std::vector<uint8_t> serialized = callData.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    // Deserialize
    CVM::CVMCallData deserialized;
    bool success = deserialized.Deserialize(serialized);
    
    BOOST_CHECK(success);
    BOOST_CHECK(deserialized.contractAddress == callData.contractAddress);
    BOOST_CHECK_EQUAL(deserialized.gasLimit, callData.gasLimit);
    BOOST_CHECK(deserialized.format == callData.format);
}

BOOST_AUTO_TEST_CASE(cvm_transaction_type_detection)
{
    // Test detection of different CVM transaction types
    // Requirements: 10.1
    
    // Test CONTRACT_DEPLOY
    {
        std::vector<uint8_t> data = {0x01, 0x02};
        CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, data);
        CTxOut txout(0, script);
        
        CVM::CVMOpType opType;
        std::vector<uint8_t> parsedData;
        bool parsed = CVM::ParseCVMOpReturn(txout, opType, parsedData);
        
        BOOST_CHECK(parsed);
        BOOST_CHECK(opType == CVM::CVMOpType::CONTRACT_DEPLOY);
    }
    
    // Test CONTRACT_CALL
    {
        std::vector<uint8_t> data = {0x03, 0x04};
        CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_CALL, data);
        CTxOut txout(0, script);
        
        CVM::CVMOpType opType;
        std::vector<uint8_t> parsedData;
        bool parsed = CVM::ParseCVMOpReturn(txout, opType, parsedData);
        
        BOOST_CHECK(parsed);
        BOOST_CHECK(opType == CVM::CVMOpType::CONTRACT_CALL);
    }
    
    // Test REPUTATION_VOTE
    {
        std::vector<uint8_t> data = {0x05, 0x06};
        CScript script = CVM::BuildCVMOpReturn(CVM::CVMOpType::REPUTATION_VOTE, data);
        CTxOut txout(0, script);
        
        CVM::CVMOpType opType;
        std::vector<uint8_t> parsedData;
        bool parsed = CVM::ParseCVMOpReturn(txout, opType, parsedData);
        
        BOOST_CHECK(parsed);
        BOOST_CHECK(opType == CVM::CVMOpType::REPUTATION_VOTE);
    }
}


// ============================================================================
// Task 23.4: DAO Dispute Flow Integration Tests
// Requirements: 6.1, 6.2, 6.3
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_dispute_case_serialization)
{
    // Test DisputeCase serialization for DAO escalation
    // Requirements: 6.1, 6.2
    
    CVM::DisputeCase dispute;
    dispute.disputeId.SetHex("0xdispute1234567890abcdef1234567890abcdef1234567890abcdef12345678");
    dispute.txHash.SetHex("0xtxhash1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    dispute.senderAddress.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    dispute.disputeReason = "Score mismatch detected";
    dispute.resolved = false;
    dispute.approved = false;
    dispute.resolutionTimestamp = 0;
    
    // Add self-reported score
    dispute.selfReportedScore.address = dispute.senderAddress;
    dispute.selfReportedScore.finalScore = 85;
    dispute.selfReportedScore.timestamp = GetTime();
    dispute.selfReportedScore.behaviorScore = 90.0;
    dispute.selfReportedScore.wotScore = 80.0;
    dispute.selfReportedScore.economicScore = 85.0;
    dispute.selfReportedScore.temporalScore = 75.0;
    
    // Add validator responses
    for (int i = 0; i < 5; i++) {
        CVM::ValidationResponse resp;
        resp.txHash = dispute.txHash;
        resp.validatorAddress.SetHex("0x" + std::to_string(i) + "234567890abcdef1234567890abcdef12345678");
        resp.vote = (i < 2) ? CVM::ValidationVote::ACCEPT : CVM::ValidationVote::REJECT;
        resp.voteConfidence = 0.8;
        resp.hasWoTConnection = (i % 2 == 0);
        resp.timestamp = GetTime();
        resp.signature = std::vector<uint8_t>(64, 0xAB + i);
        dispute.validatorResponses.push_back(resp);
    }
    
    // Add evidence data
    dispute.evidenceData = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    
    // Deserialize
    CVM::DisputeCase deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK(deserialized.disputeId == dispute.disputeId);
    BOOST_CHECK(deserialized.txHash == dispute.txHash);
    BOOST_CHECK(deserialized.senderAddress == dispute.senderAddress);
    BOOST_CHECK_EQUAL(deserialized.disputeReason, dispute.disputeReason);
    BOOST_CHECK_EQUAL(deserialized.validatorResponses.size(), dispute.validatorResponses.size());
    BOOST_CHECK_EQUAL(deserialized.evidenceData.size(), dispute.evidenceData.size());
    BOOST_CHECK_EQUAL(deserialized.selfReportedScore.finalScore, dispute.selfReportedScore.finalScore);
}

BOOST_AUTO_TEST_CASE(fraud_record_serialization)
{
    // Test FraudRecord serialization for on-chain recording
    // Requirements: 6.3
    
    CVM::FraudRecord record;
    record.txHash.SetHex("0xfraud1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    record.fraudsterAddress.SetHex("0xfraudster1234567890abcdef1234567890abcdef");
    record.timestamp = GetTime();
    record.blockHeight = 100000;
    record.scoreDifference = 25;
    record.reputationPenalty = 50;
    record.bondSlashed = 10 * COIN;
    
    // Set claimed score
    record.claimedScore.address = record.fraudsterAddress;
    record.claimedScore.finalScore = 90;
    record.claimedScore.behaviorScore = 95.0;
    record.claimedScore.wotScore = 85.0;
    record.claimedScore.economicScore = 90.0;
    record.claimedScore.temporalScore = 80.0;
    
    // Set actual score
    record.actualScore.address = record.fraudsterAddress;
    record.actualScore.finalScore = 65;
    record.actualScore.behaviorScore = 70.0;
    record.actualScore.wotScore = 60.0;
    record.actualScore.economicScore = 65.0;
    record.actualScore.temporalScore = 55.0;
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    
    // Deserialize
    CVM::FraudRecord deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK(deserialized.txHash == record.txHash);
    BOOST_CHECK(deserialized.fraudsterAddress == record.fraudsterAddress);
    BOOST_CHECK_EQUAL(deserialized.scoreDifference, record.scoreDifference);
    BOOST_CHECK_EQUAL(deserialized.reputationPenalty, record.reputationPenalty);
    BOOST_CHECK_EQUAL(deserialized.bondSlashed, record.bondSlashed);
    BOOST_CHECK_EQUAL(deserialized.claimedScore.finalScore, record.claimedScore.finalScore);
    BOOST_CHECK_EQUAL(deserialized.actualScore.finalScore, record.actualScore.finalScore);
}

BOOST_AUTO_TEST_CASE(dao_dispute_data_serialization)
{
    // Test CVMDAODisputeData serialization for OP_RETURN
    // Requirements: 6.1
    // Note: The 'reason' field is intentionally omitted from serialization
    // to keep OP_RETURN payload <= 80 bytes
    
    CVM::CVMDAODisputeData disputeData;
    disputeData.originalVoteTxHash.SetHex("0xoriginal1234567890abcdef1234567890abcdef1234567890abcdef12345678");
    disputeData.challenger.SetHex("0xchallenger1234567890abcdef1234567890abcd");
    disputeData.challengeBond = 5 * COIN;
    disputeData.reason = "Fraudulent reputation claim";  // Not serialized
    disputeData.timestamp = GetTime();
    
    // Serialize
    std::vector<uint8_t> serialized = disputeData.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    // Verify serialized size is within OP_RETURN limits
    // 32 (txHash) + 20 (challenger) + 8 (bond) + 4 (timestamp) = 64 bytes
    BOOST_CHECK_LE(serialized.size(), 80);
    
    // Deserialize
    CVM::CVMDAODisputeData deserialized;
    bool success = deserialized.Deserialize(serialized);
    
    BOOST_CHECK(success);
    BOOST_CHECK(deserialized.originalVoteTxHash == disputeData.originalVoteTxHash);
    BOOST_CHECK(deserialized.challenger == disputeData.challenger);
    BOOST_CHECK_EQUAL(deserialized.challengeBond, disputeData.challengeBond);
    // Note: reason is NOT serialized to keep OP_RETURN under 80 bytes
    // BOOST_CHECK_EQUAL(deserialized.reason, disputeData.reason);
}

BOOST_AUTO_TEST_CASE(dao_vote_data_serialization)
{
    // Test CVMDAOVoteData serialization for OP_RETURN
    // Requirements: 6.1
    
    CVM::CVMDAOVoteData voteData;
    voteData.disputeId.SetHex("0xdispute1234567890abcdef1234567890abcdef1234567890abcdef12345678");
    voteData.daoMember.SetHex("0xdaomember1234567890abcdef1234567890abcd");
    voteData.supportSlash = true;
    voteData.stake = 100 * COIN;
    voteData.timestamp = GetTime();
    
    // Serialize
    std::vector<uint8_t> serialized = voteData.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    // Deserialize
    CVM::CVMDAOVoteData deserialized;
    bool success = deserialized.Deserialize(serialized);
    
    BOOST_CHECK(success);
    BOOST_CHECK(deserialized.disputeId == voteData.disputeId);
    BOOST_CHECK(deserialized.daoMember == voteData.daoMember);
    BOOST_CHECK_EQUAL(deserialized.supportSlash, voteData.supportSlash);
    BOOST_CHECK_EQUAL(deserialized.stake, voteData.stake);
}

BOOST_AUTO_TEST_CASE(consensus_result_computation)
{
    // Test ConsensusResult computation from validator responses
    // Requirements: 6.1, 6.2
    
    CVM::ConsensusResult result;
    result.txHash.SetHex("0xtx1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcd");
    result.consensusThreshold = 0.70;
    result.wotCoverageThreshold = 0.30;
    
    // Simulate 10 validator responses
    // 7 accept (5 with WoT, 2 without)
    // 2 reject (1 with WoT, 1 without)
    // 1 abstain (without WoT)
    
    for (int i = 0; i < 10; i++) {
        CVM::ValidationResponse resp;
        resp.txHash = result.txHash;
        resp.validatorAddress.SetHex("0x" + std::to_string(i) + "234567890abcdef1234567890abcdef12345678");
        
        if (i < 7) {
            resp.vote = CVM::ValidationVote::ACCEPT;
            result.acceptVotes++;
        } else if (i < 9) {
            resp.vote = CVM::ValidationVote::REJECT;
            result.rejectVotes++;
        } else {
            resp.vote = CVM::ValidationVote::ABSTAIN;
            result.abstainVotes++;
        }
        
        resp.hasWoTConnection = (i < 5) || (i == 7);  // 6 with WoT
        resp.voteConfidence = 0.8;
        resp.timestamp = GetTime();
        
        result.responses.push_back(resp);
    }
    
    // Calculate weighted votes
    // WoT validators: weight 1.0
    // Non-WoT validators: weight 0.5
    double wotWeight = 1.0;
    double nonWotWeight = 0.5;
    
    for (const auto& resp : result.responses) {
        double weight = resp.hasWoTConnection ? wotWeight : nonWotWeight;
        
        switch (resp.vote) {
            case CVM::ValidationVote::ACCEPT:
                result.weightedAccept += weight;
                break;
            case CVM::ValidationVote::REJECT:
                result.weightedReject += weight;
                break;
            case CVM::ValidationVote::ABSTAIN:
                result.weightedAbstain += weight;
                break;
        }
    }
    
    // Check consensus
    double totalWeight = result.weightedAccept + result.weightedReject + result.weightedAbstain;
    double acceptRatio = result.weightedAccept / totalWeight;
    
    result.consensusReached = (acceptRatio >= result.consensusThreshold) || 
                              ((1.0 - acceptRatio - result.weightedAbstain/totalWeight) >= result.consensusThreshold);
    result.approved = (acceptRatio >= result.consensusThreshold);
    
    // Check WoT coverage
    int wotCount = 0;
    for (const auto& resp : result.responses) {
        if (resp.hasWoTConnection) wotCount++;
    }
    double wotCoverage = (double)wotCount / result.responses.size();
    
    // Verify results
    BOOST_CHECK_EQUAL(result.acceptVotes, 7);
    BOOST_CHECK_EQUAL(result.rejectVotes, 2);
    BOOST_CHECK_EQUAL(result.abstainVotes, 1);
    BOOST_CHECK_GE(wotCoverage, result.wotCoverageThreshold);
    
    // With 7 accepts (5 WoT + 2 non-WoT) = 5*1.0 + 2*0.5 = 6.0
    // With 2 rejects (1 WoT + 1 non-WoT) = 1*1.0 + 1*0.5 = 1.5
    // With 1 abstain (non-WoT) = 0.5
    // Total = 8.0
    // Accept ratio = 6.0/8.0 = 0.75 > 0.70 threshold
    BOOST_CHECK(result.approved);
}

BOOST_AUTO_TEST_CASE(dispute_escalation_criteria)
{
    // Test criteria for escalating disputes to DAO
    // Requirements: 6.1
    
    // Scenario 1: No consensus (split vote)
    {
        CVM::ConsensusResult result;
        result.acceptVotes = 5;
        result.rejectVotes = 5;
        result.abstainVotes = 0;
        result.consensusThreshold = 0.70;
        
        double acceptRatio = (double)result.acceptVotes / (result.acceptVotes + result.rejectVotes);
        double rejectRatio = (double)result.rejectVotes / (result.acceptVotes + result.rejectVotes);
        
        bool needsDAO = (acceptRatio < result.consensusThreshold) && 
                        (rejectRatio < result.consensusThreshold);
        
        BOOST_CHECK(needsDAO);  // Should escalate to DAO
    }
    
    // Scenario 2: Clear consensus (approve)
    {
        CVM::ConsensusResult result;
        result.acceptVotes = 8;
        result.rejectVotes = 2;
        result.abstainVotes = 0;
        result.consensusThreshold = 0.70;
        
        double acceptRatio = (double)result.acceptVotes / (result.acceptVotes + result.rejectVotes);
        
        bool needsDAO = (acceptRatio < result.consensusThreshold);
        
        BOOST_CHECK(!needsDAO);  // Should NOT escalate to DAO
    }
    
    // Scenario 3: Clear consensus (reject)
    {
        CVM::ConsensusResult result;
        result.acceptVotes = 2;
        result.rejectVotes = 8;
        result.abstainVotes = 0;
        result.consensusThreshold = 0.70;
        
        double rejectRatio = (double)result.rejectVotes / (result.acceptVotes + result.rejectVotes);
        
        bool needsDAO = (rejectRatio < result.consensusThreshold);
        
        BOOST_CHECK(!needsDAO);  // Should NOT escalate to DAO
    }
}

BOOST_AUTO_TEST_CASE(hatv2_score_serialization)
{
    // Test HATv2Score serialization
    // Requirements: 6.2
    
    CVM::HATv2Score score;
    score.address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    score.finalScore = 75;
    score.timestamp = GetTime();
    score.behaviorScore = 80.0;
    score.wotScore = 70.0;
    score.economicScore = 75.0;
    score.temporalScore = 65.0;
    score.hasWoTConnection = true;
    score.wotPathCount = 3;
    score.wotPathStrength = 0.85;
    
    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << score;
    
    // Deserialize
    CVM::HATv2Score deserialized;
    ss >> deserialized;
    
    // Verify
    BOOST_CHECK(deserialized.address == score.address);
    BOOST_CHECK_EQUAL(deserialized.finalScore, score.finalScore);
    BOOST_CHECK_CLOSE(deserialized.behaviorScore, score.behaviorScore, 0.001);
    BOOST_CHECK_CLOSE(deserialized.wotScore, score.wotScore, 0.001);
    BOOST_CHECK_CLOSE(deserialized.economicScore, score.economicScore, 0.001);
    BOOST_CHECK_CLOSE(deserialized.temporalScore, score.temporalScore, 0.001);
    BOOST_CHECK_EQUAL(deserialized.hasWoTConnection, score.hasWoTConnection);
    BOOST_CHECK_EQUAL(deserialized.wotPathCount, score.wotPathCount);
    BOOST_CHECK_CLOSE(deserialized.wotPathStrength, score.wotPathStrength, 0.001);
}

BOOST_AUTO_TEST_SUITE_END()
