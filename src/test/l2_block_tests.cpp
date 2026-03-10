// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <l2/l2_block.h>
#include <l2/l2_transaction.h>
#include <l2/l2_block_validator.h>
#include <key.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(l2_block_tests, BasicTestingSetup)

// ============================================================================
// L2Transaction Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(l2_transaction_basic)
{
    // Create a simple transfer transaction
    uint160 from, to;
    from.SetHex("1234567890abcdef1234567890abcdef12345678");
    to.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::L2Transaction tx = l2::CreateTransferTx(from, to, 1000000, 0, 1000, 1);
    
    BOOST_CHECK_EQUAL(tx.type, l2::L2TxType::TRANSFER);
    BOOST_CHECK(tx.from == from);
    BOOST_CHECK(tx.to == to);
    BOOST_CHECK_EQUAL(tx.value, 1000000);
    BOOST_CHECK_EQUAL(tx.nonce, 0);
    BOOST_CHECK_EQUAL(tx.gasPrice, 1000);
    BOOST_CHECK_EQUAL(tx.l2ChainId, 1);
    BOOST_CHECK(tx.IsTransfer());
    BOOST_CHECK(!tx.IsContractDeploy());
    BOOST_CHECK(!tx.IsWithdrawal());
}

BOOST_AUTO_TEST_CASE(l2_transaction_contract_deploy)
{
    uint160 from;
    from.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    std::vector<unsigned char> bytecode = {0x60, 0x80, 0x60, 0x40, 0x52};
    
    l2::L2Transaction tx = l2::CreateDeployTx(from, bytecode, 1, 100000, 2000, 1);
    
    BOOST_CHECK_EQUAL(tx.type, l2::L2TxType::CONTRACT_DEPLOY);
    BOOST_CHECK(tx.from == from);
    BOOST_CHECK(tx.to.IsNull());
    BOOST_CHECK_EQUAL(tx.data.size(), bytecode.size());
    BOOST_CHECK(tx.IsContractDeploy());
    BOOST_CHECK(!tx.IsTransfer());
}

BOOST_AUTO_TEST_CASE(l2_transaction_withdrawal)
{
    uint160 from, l1Recipient;
    from.SetHex("1234567890abcdef1234567890abcdef12345678");
    l1Recipient.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::L2Transaction tx = l2::CreateWithdrawalTx(from, l1Recipient, 5000000, 2, 1500, 1);
    
    BOOST_CHECK_EQUAL(tx.type, l2::L2TxType::WITHDRAWAL);
    BOOST_CHECK(tx.from == from);
    BOOST_CHECK(tx.to == l1Recipient);
    BOOST_CHECK_EQUAL(tx.value, 5000000);
    BOOST_CHECK(tx.IsWithdrawal());
}

BOOST_AUTO_TEST_CASE(l2_transaction_hash)
{
    uint160 from, to;
    from.SetHex("1234567890abcdef1234567890abcdef12345678");
    to.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::L2Transaction tx1 = l2::CreateTransferTx(from, to, 1000000, 0, 1000, 1);
    l2::L2Transaction tx2 = l2::CreateTransferTx(from, to, 1000000, 0, 1000, 1);
    l2::L2Transaction tx3 = l2::CreateTransferTx(from, to, 2000000, 0, 1000, 1);
    
    // Same transactions should have same hash
    BOOST_CHECK(tx1.GetHash() == tx2.GetHash());
    
    // Different transactions should have different hash
    BOOST_CHECK(tx1.GetHash() != tx3.GetHash());
}

BOOST_AUTO_TEST_CASE(l2_transaction_serialization)
{
    uint160 from, to;
    from.SetHex("1234567890abcdef1234567890abcdef12345678");
    to.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::L2Transaction tx1 = l2::CreateTransferTx(from, to, 1000000, 5, 1000, 1);
    
    // Serialize
    std::vector<unsigned char> data = tx1.Serialize();
    BOOST_CHECK(!data.empty());
    
    // Deserialize
    l2::L2Transaction tx2;
    BOOST_CHECK(tx2.Deserialize(data));
    
    // Verify equality
    BOOST_CHECK(tx1 == tx2);
    BOOST_CHECK(tx1.GetHash() == tx2.GetHash());
}

BOOST_AUTO_TEST_CASE(l2_transaction_validate_structure)
{
    uint160 from, to;
    from.SetHex("1234567890abcdef1234567890abcdef12345678");
    to.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    // Valid transfer
    l2::L2Transaction validTx = l2::CreateTransferTx(from, to, 1000000, 0, 1000, 1);
    BOOST_CHECK(validTx.ValidateStructure());
    
    // Invalid: transfer without recipient
    l2::L2Transaction invalidTx1;
    invalidTx1.type = l2::L2TxType::TRANSFER;
    invalidTx1.from = from;
    invalidTx1.to.SetNull();
    invalidTx1.gasLimit = 21000;
    invalidTx1.gasPrice = 1000;
    BOOST_CHECK(!invalidTx1.ValidateStructure());
    
    // Invalid: gas limit too low
    l2::L2Transaction invalidTx2 = l2::CreateTransferTx(from, to, 1000000, 0, 1000, 1);
    invalidTx2.gasLimit = 100;  // Below minimum
    BOOST_CHECK(!invalidTx2.ValidateStructure());
    
    // Invalid: deployment without bytecode
    l2::L2Transaction invalidTx3;
    invalidTx3.type = l2::L2TxType::CONTRACT_DEPLOY;
    invalidTx3.from = from;
    invalidTx3.gasLimit = 100000;
    invalidTx3.gasPrice = 1000;
    // No data
    BOOST_CHECK(!invalidTx3.ValidateStructure());
}

// ============================================================================
// L2Block Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(l2_block_genesis)
{
    uint160 sequencer;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    l2::L2Block genesis = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    
    BOOST_CHECK_EQUAL(genesis.header.blockNumber, 0);
    BOOST_CHECK(genesis.header.parentHash.IsNull());
    BOOST_CHECK(genesis.header.sequencer == sequencer);
    BOOST_CHECK_EQUAL(genesis.header.timestamp, 1700000000);
    BOOST_CHECK_EQUAL(genesis.header.l2ChainId, 1);
    BOOST_CHECK(genesis.IsGenesis());
    BOOST_CHECK(genesis.isFinalized);
}

BOOST_AUTO_TEST_CASE(l2_block_hash)
{
    uint160 sequencer;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    l2::L2Block block1 = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    l2::L2Block block2 = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    l2::L2Block block3 = l2::CreateGenesisBlock(1, 1700000001, sequencer);
    
    // Same blocks should have same hash
    BOOST_CHECK(block1.GetHash() == block2.GetHash());
    
    // Different blocks should have different hash
    BOOST_CHECK(block1.GetHash() != block3.GetHash());
}

BOOST_AUTO_TEST_CASE(l2_block_serialization)
{
    uint160 sequencer;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    l2::L2Block block1 = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    
    // Add a transaction
    uint160 from, to;
    from.SetHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    to.SetHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    block1.transactions.push_back(l2::CreateTransferTx(from, to, 1000, 0, 100, 1));
    
    // Serialize
    std::vector<unsigned char> data = block1.Serialize();
    BOOST_CHECK(!data.empty());
    
    // Deserialize
    l2::L2Block block2;
    BOOST_CHECK(block2.Deserialize(data));
    
    // Verify equality
    BOOST_CHECK(block1 == block2);
    BOOST_CHECK(block1.GetHash() == block2.GetHash());
    BOOST_CHECK_EQUAL(block2.transactions.size(), 1);
}

BOOST_AUTO_TEST_CASE(l2_block_validate_structure)
{
    uint160 sequencer;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    // Valid genesis block
    l2::L2Block genesis = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    BOOST_CHECK(genesis.ValidateStructure());
    
    // Invalid: non-genesis with null parent
    l2::L2Block invalidBlock;
    invalidBlock.header.blockNumber = 1;
    invalidBlock.header.parentHash.SetNull();
    invalidBlock.header.sequencer = sequencer;
    invalidBlock.header.timestamp = 1700000001;
    invalidBlock.header.gasLimit = 30000000;
    BOOST_CHECK(!invalidBlock.ValidateStructure());
}

BOOST_AUTO_TEST_CASE(l2_block_transactions_root)
{
    uint160 sequencer, from, to;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    from.SetHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    to.SetHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    
    l2::L2Block block = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    
    // Add transactions
    block.transactions.push_back(l2::CreateTransferTx(from, to, 1000, 0, 100, 1));
    block.transactions.push_back(l2::CreateTransferTx(from, to, 2000, 1, 100, 1));
    
    // Compute transactions root
    uint256 root = block.ComputeTransactionsRoot();
    BOOST_CHECK(!root.IsNull());
    
    // Same transactions should produce same root
    l2::L2Block block2 = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    block2.transactions.push_back(l2::CreateTransferTx(from, to, 1000, 0, 100, 1));
    block2.transactions.push_back(l2::CreateTransferTx(from, to, 2000, 1, 100, 1));
    
    BOOST_CHECK(block.ComputeTransactionsRoot() == block2.ComputeTransactionsRoot());
    
    // Different transactions should produce different root
    l2::L2Block block3 = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    block3.transactions.push_back(l2::CreateTransferTx(from, to, 3000, 0, 100, 1));
    
    BOOST_CHECK(block.ComputeTransactionsRoot() != block3.ComputeTransactionsRoot());
}

BOOST_AUTO_TEST_CASE(l2_block_signatures)
{
    uint160 sequencer;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    l2::L2Block block = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    
    // Generate a key pair
    CKey key;
    key.MakeNewKey(true);
    
    // Sign the block
    BOOST_CHECK(block.Sign(key, sequencer));
    BOOST_CHECK_EQUAL(block.GetSignatureCount(), 1);
    BOOST_CHECK(block.HasSignature(sequencer));
    
    // Cannot sign twice with same sequencer
    BOOST_CHECK(!block.Sign(key, sequencer));
    BOOST_CHECK_EQUAL(block.GetSignatureCount(), 1);
    
    // Different sequencer can sign
    uint160 sequencer2;
    sequencer2.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    CKey key2;
    key2.MakeNewKey(true);
    
    BOOST_CHECK(block.Sign(key2, sequencer2));
    BOOST_CHECK_EQUAL(block.GetSignatureCount(), 2);
    BOOST_CHECK(block.HasSignature(sequencer2));
}

// ============================================================================
// L2BlockValidator Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(l2_block_validator_header)
{
    uint160 sequencer;
    sequencer.SetHex("1234567890abcdef1234567890abcdef12345678");
    
    l2::ValidationContext context;
    context.currentTimestamp = 1700000100;
    
    // Valid genesis header
    l2::L2Block genesis = l2::CreateGenesisBlock(1, 1700000000, sequencer);
    auto result = l2::L2BlockValidator::ValidateHeader(genesis.header, context);
    BOOST_CHECK(result.isValid);
    
    // Invalid: timestamp too far in future (need non-genesis block for timestamp check)
    // Genesis blocks skip timestamp validation, so we create a non-genesis block
    l2::L2Block futureBlock = l2::CreateGenesisBlock(1, 1700000200, sequencer);
    futureBlock.header.blockNumber = 1;  // Make it non-genesis
    futureBlock.header.parentHash = genesis.GetHash();  // Set parent hash
    result = l2::L2BlockValidator::ValidateHeader(futureBlock.header, context);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK_EQUAL(result.error, l2::ValidationError::TIMESTAMP_TOO_FAR_FUTURE);
}

BOOST_AUTO_TEST_CASE(l2_block_validator_timestamp_monotonicity)
{
    // Valid: increasing timestamps
    BOOST_CHECK(l2::L2BlockValidator::ValidateTimestampMonotonicity(100, 99));
    BOOST_CHECK(l2::L2BlockValidator::ValidateTimestampMonotonicity(1000, 500));
    
    // Invalid: same or decreasing timestamps
    BOOST_CHECK(!l2::L2BlockValidator::ValidateTimestampMonotonicity(100, 100));
    BOOST_CHECK(!l2::L2BlockValidator::ValidateTimestampMonotonicity(99, 100));
}

BOOST_AUTO_TEST_CASE(l2_block_validator_gas_limit_adjustment)
{
    uint64_t parentGasLimit = 30000000;
    uint64_t maxChange = parentGasLimit / 1024;
    
    // Valid: within bounds
    BOOST_CHECK(l2::L2BlockValidator::ValidateGasLimitAdjustment(parentGasLimit, parentGasLimit));
    BOOST_CHECK(l2::L2BlockValidator::ValidateGasLimitAdjustment(parentGasLimit + maxChange, parentGasLimit));
    BOOST_CHECK(l2::L2BlockValidator::ValidateGasLimitAdjustment(parentGasLimit - maxChange, parentGasLimit));
    
    // Invalid: exceeds bounds
    BOOST_CHECK(!l2::L2BlockValidator::ValidateGasLimitAdjustment(parentGasLimit + maxChange + 1, parentGasLimit));
    BOOST_CHECK(!l2::L2BlockValidator::ValidateGasLimitAdjustment(parentGasLimit - maxChange - 1, parentGasLimit));
}

BOOST_AUTO_TEST_CASE(l2_block_validator_consensus)
{
    uint160 seq1, seq2, seq3;
    seq1.SetHex("1111111111111111111111111111111111111111");
    seq2.SetHex("2222222222222222222222222222222222222222");
    seq3.SetHex("3333333333333333333333333333333333333333");
    
    l2::L2Block block = l2::CreateGenesisBlock(1, 1700000000, seq1);
    
    // Generate keys
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    key3.MakeNewKey(true);
    
    // Setup context with equal weights
    l2::ValidationContext context;
    context.sequencerPubkeys[seq1] = key1.GetPubKey();
    context.sequencerPubkeys[seq2] = key2.GetPubKey();
    context.sequencerPubkeys[seq3] = key3.GetPubKey();
    context.sequencerWeights[seq1] = 100;
    context.sequencerWeights[seq2] = 100;
    context.sequencerWeights[seq3] = 100;
    context.totalSequencerWeight = 300;
    // Use 0.666 threshold so that 2/3 (66.67%) passes
    context.consensusThreshold = 0.666;
    
    // No signatures - no consensus
    BOOST_CHECK(!l2::L2BlockValidator::HasConsensus(block, context));
    
    // 1/3 signatures - no consensus
    block.Sign(key1, seq1);
    double percent = l2::L2BlockValidator::CalculateWeightedSignaturePercent(block, context);
    BOOST_CHECK_CLOSE(percent, 0.333, 1.0);
    BOOST_CHECK(!l2::L2BlockValidator::HasConsensus(block, context));
    
    // 2/3 signatures - consensus reached (66.67% >= 66.6%)
    block.Sign(key2, seq2);
    percent = l2::L2BlockValidator::CalculateWeightedSignaturePercent(block, context);
    BOOST_CHECK_CLOSE(percent, 0.667, 1.0);
    BOOST_CHECK(l2::L2BlockValidator::HasConsensus(block, context));
}

BOOST_AUTO_TEST_CASE(l2_merkle_root_computation)
{
    // Empty list
    std::vector<uint256> empty;
    BOOST_CHECK(l2::ComputeMerkleRoot(empty).IsNull());
    
    // Single element
    uint256 hash1;
    hash1.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    std::vector<uint256> single = {hash1};
    BOOST_CHECK(l2::ComputeMerkleRoot(single) == hash1);
    
    // Two elements
    uint256 hash2;
    hash2.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    std::vector<uint256> two = {hash1, hash2};
    uint256 root2 = l2::ComputeMerkleRoot(two);
    BOOST_CHECK(!root2.IsNull());
    BOOST_CHECK(root2 != hash1);
    BOOST_CHECK(root2 != hash2);
    
    // Order matters
    std::vector<uint256> twoReversed = {hash2, hash1};
    BOOST_CHECK(l2::ComputeMerkleRoot(twoReversed) != root2);
}

BOOST_AUTO_TEST_SUITE_END()
