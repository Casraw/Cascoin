// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_fee_distributor_tests.cpp
 * @brief Property-based tests for L2 Fee Distributor
 * 
 * **Feature: cascoin-l2-solution, Property 18: Gas Fee Distribution**
 * **Validates: Requirements 18.2, 38.2**
 * 
 * Property 18: Gas Fee Distribution
 * *For any* L2 block, the total gas fees collected SHALL be distributed
 * according to the defined ratio (70/20/10).
 */

#include <l2/fee_distributor.h>
#include <l2/l2_transaction.h>
#include <random.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <set>
#include <numeric>

namespace {

// Local random context for tests
static FastRandomContext g_test_rand_ctx(true);

static uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

static uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

/**
 * Helper function to generate a random address
 */
static uint160 RandomAddress()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        uint32_t val = TestRand32();
        memcpy(addr.begin() + i * 4, &val, 4);
    }
    return addr;
}


/**
 * Helper function to generate random fee amount
 */
static CAmount RandomFeeAmount()
{
    // Generate fee between 1000 satoshis and 100 CAS
    return 1000 + (TestRand64() % (100 * COIN));
}

/**
 * Helper function to generate a list of random sequencer addresses
 */
static std::vector<uint160> RandomSequencerList(size_t count)
{
    std::vector<uint160> sequencers;
    std::set<uint160> seen;
    
    while (sequencers.size() < count) {
        uint160 addr = RandomAddress();
        if (seen.find(addr) == seen.end()) {
            sequencers.push_back(addr);
            seen.insert(addr);
        }
    }
    return sequencers;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_fee_distributor_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_fee_distributor)
{
    l2::FeeDistributor distributor;
    
    BOOST_CHECK_EQUAL(distributor.GetSequencerCount(), 0u);
    BOOST_CHECK_EQUAL(distributor.GetTotalBurned(), 0);
}

BOOST_AUTO_TEST_CASE(basic_fee_split)
{
    CAmount totalFees = 1000000;  // 1M satoshis
    CAmount blockProducerAmount, sharedPoolAmount, burnAmount;
    
    l2::FeeDistributor::CalculateSplit(totalFees, blockProducerAmount, sharedPoolAmount, burnAmount);
    
    // Verify 70/20/10 split
    BOOST_CHECK_EQUAL(blockProducerAmount, 700000);  // 70%
    BOOST_CHECK_EQUAL(sharedPoolAmount, 200000);     // 20%
    BOOST_CHECK_EQUAL(burnAmount, 100000);           // 10%
    
    // Verify total
    BOOST_CHECK_EQUAL(blockProducerAmount + sharedPoolAmount + burnAmount, totalFees);
}

BOOST_AUTO_TEST_CASE(fee_split_with_rounding)
{
    // Test with amount that doesn't divide evenly
    CAmount totalFees = 1000001;
    CAmount blockProducerAmount, sharedPoolAmount, burnAmount;
    
    l2::FeeDistributor::CalculateSplit(totalFees, blockProducerAmount, sharedPoolAmount, burnAmount);
    
    // Verify total equals input (remainder goes to burn)
    BOOST_CHECK_EQUAL(blockProducerAmount + sharedPoolAmount + burnAmount, totalFees);
}


BOOST_AUTO_TEST_CASE(distribute_fees_single_sequencer)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    std::vector<uint160> sequencers = {producer};
    CAmount totalFees = 1000000;
    uint64_t blockNumber = 1000;
    
    l2::FeeDistributionResult result = distributor.DistributeFees(
        blockNumber, totalFees, producer, sequencers);
    
    BOOST_CHECK_EQUAL(result.blockNumber, blockNumber);
    BOOST_CHECK_EQUAL(result.totalFees, totalFees);
    BOOST_CHECK(result.blockProducer == producer);
    BOOST_CHECK_EQUAL(result.sequencerCount, 1u);
    BOOST_CHECK(result.IsValid());
    
    // Single sequencer gets both block producer share and shared pool
    CAmount expectedProducerShare = (totalFees * 70) / 100;
    CAmount expectedSharedPool = (totalFees * 20) / 100;
    
    // Check rewards
    auto rewards = distributor.GetSequencerRewards(producer);
    BOOST_CHECK(rewards.has_value());
    BOOST_CHECK_EQUAL(rewards->totalRewards, expectedProducerShare + expectedSharedPool);
}

BOOST_AUTO_TEST_CASE(distribute_fees_multiple_sequencers)
{
    l2::FeeDistributor distributor;
    
    std::vector<uint160> sequencers = RandomSequencerList(5);
    uint160 producer = sequencers[0];
    CAmount totalFees = 1000000;
    uint64_t blockNumber = 1000;
    
    l2::FeeDistributionResult result = distributor.DistributeFees(
        blockNumber, totalFees, producer, sequencers);
    
    BOOST_CHECK(result.IsValid());
    BOOST_CHECK_EQUAL(result.sequencerCount, 5u);
    
    // Block producer gets 70%
    CAmount expectedProducerShare = (totalFees * 70) / 100;
    auto producerRewards = distributor.GetSequencerRewards(producer);
    BOOST_CHECK(producerRewards.has_value());
    BOOST_CHECK_EQUAL(producerRewards->blockProductionRewards, expectedProducerShare);
    
    // Other sequencers share 20% (4 sequencers)
    CAmount sharedPool = (totalFees * 20) / 100;
    CAmount perSequencerShare = sharedPool / 4;
    
    for (size_t i = 1; i < sequencers.size(); ++i) {
        auto rewards = distributor.GetSequencerRewards(sequencers[i]);
        BOOST_CHECK(rewards.has_value());
        BOOST_CHECK_EQUAL(rewards->sharedPoolRewards, perSequencerShare);
    }
}

BOOST_AUTO_TEST_CASE(fee_burning)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    std::vector<uint160> sequencers = {producer};
    CAmount totalFees = 1000000;
    
    distributor.DistributeFees(1000, totalFees, producer, sequencers);
    
    // 10% should be burned
    CAmount expectedBurn = (totalFees * 10) / 100;
    BOOST_CHECK_EQUAL(distributor.GetTotalBurned(), expectedBurn);
    
    // Distribute more fees
    distributor.DistributeFees(1001, totalFees, producer, sequencers);
    BOOST_CHECK_EQUAL(distributor.GetTotalBurned(), expectedBurn * 2);
}


BOOST_AUTO_TEST_CASE(claim_rewards)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    std::vector<uint160> sequencers = {producer};
    CAmount totalFees = 1000000;
    
    distributor.DistributeFees(1000, totalFees, producer, sequencers);
    
    CAmount unclaimed = distributor.GetUnclaimedRewards(producer);
    BOOST_CHECK(unclaimed > 0);
    
    // Claim half
    CAmount claimed = distributor.ClaimRewards(producer, unclaimed / 2);
    BOOST_CHECK_EQUAL(claimed, unclaimed / 2);
    
    // Check remaining
    CAmount remaining = distributor.GetUnclaimedRewards(producer);
    BOOST_CHECK_EQUAL(remaining, unclaimed - claimed);
    
    // Claim all remaining
    CAmount claimedAll = distributor.ClaimRewards(producer, 0);
    BOOST_CHECK_EQUAL(claimedAll, remaining);
    BOOST_CHECK_EQUAL(distributor.GetUnclaimedRewards(producer), 0);
}

BOOST_AUTO_TEST_CASE(uptime_bonus)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    distributor.RegisterSequencer(producer, 80, 100 * COIN);
    
    // Record many blocks produced (enough for uptime bonus)
    for (uint64_t i = 0; i < l2::MIN_BLOCKS_FOR_UPTIME_BONUS; ++i) {
        distributor.RecordBlockProduced(producer, i);
    }
    
    BOOST_CHECK(distributor.QualifiesForUptimeBonus(producer));
    BOOST_CHECK_EQUAL(distributor.GetUptimePermille(producer), 1000u);  // 100%
}

BOOST_AUTO_TEST_CASE(missed_block_penalty)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    distributor.RegisterSequencer(producer, 80, 100 * COIN);
    
    uint64_t currentBlock = 1000;
    
    // Record a missed block
    distributor.RecordMissedBlock(producer, currentBlock);
    
    // Should be penalized
    BOOST_CHECK(distributor.IsPenalized(producer, currentBlock));
    BOOST_CHECK(distributor.IsPenalized(producer, currentBlock + 1000));
    
    // Penalty should expire eventually
    BOOST_CHECK(!distributor.IsPenalized(producer, currentBlock + l2::MISSED_BLOCK_PENALTY_DURATION + 1));
}

BOOST_AUTO_TEST_CASE(penalty_reduces_rewards)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    std::vector<uint160> sequencers = {producer};
    CAmount totalFees = 1000000;
    uint64_t currentBlock = 1000;
    
    // Apply penalty
    distributor.ApplyPenalty(producer, currentBlock, 1000);
    
    // Distribute fees while penalized
    l2::FeeDistributionResult result = distributor.DistributeFees(
        currentBlock, totalFees, producer, sequencers);
    
    BOOST_CHECK(result.penaltyApplied);
    
    // Block producer amount should be reduced by 50%
    CAmount normalAmount = (totalFees * 70) / 100;
    CAmount expectedReduced = l2::FeeDistributor::ApplyPenaltyReduction(normalAmount);
    BOOST_CHECK_EQUAL(result.blockProducerAmount, expectedReduced);
}


BOOST_AUTO_TEST_CASE(sequencer_management)
{
    l2::FeeDistributor distributor;
    
    uint160 seq1 = RandomAddress();
    uint160 seq2 = RandomAddress();
    
    distributor.RegisterSequencer(seq1, 80, 100 * COIN);
    distributor.RegisterSequencer(seq2, 70, 50 * COIN);
    
    BOOST_CHECK_EQUAL(distributor.GetSequencerCount(), 2u);
    
    auto allSeq = distributor.GetAllSequencers();
    BOOST_CHECK_EQUAL(allSeq.size(), 2u);
    
    distributor.RemoveSequencer(seq1);
    BOOST_CHECK_EQUAL(distributor.GetSequencerCount(), 1u);
}

BOOST_AUTO_TEST_CASE(distribution_history)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    std::vector<uint160> sequencers = {producer};
    CAmount totalFees = 1000000;
    
    // Distribute fees for multiple blocks
    for (uint64_t i = 0; i < 10; ++i) {
        distributor.DistributeFees(1000 + i, totalFees, producer, sequencers);
    }
    
    // Get recent distributions
    auto recent = distributor.GetRecentDistributions(5);
    BOOST_CHECK_EQUAL(recent.size(), 5u);
    
    // Get specific distribution
    auto dist = distributor.GetDistribution(1005);
    BOOST_CHECK(dist.has_value());
    BOOST_CHECK_EQUAL(dist->blockNumber, 1005u);
}

BOOST_AUTO_TEST_CASE(burn_summary)
{
    l2::FeeDistributor distributor;
    
    uint160 producer = RandomAddress();
    std::vector<uint160> sequencers = {producer};
    CAmount totalFees = 1000000;
    
    distributor.DistributeFees(1000, totalFees, producer, sequencers);
    
    l2::BurnSummary summary = distributor.GetBurnSummary();
    BOOST_CHECK_EQUAL(summary.totalBurned, (totalFees * 10) / 100);
    BOOST_CHECK_EQUAL(summary.lastBurnBlock, 1000u);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 18: Gas Fee Distribution**
 * 
 * *For any* L2 block, the total gas fees collected SHALL be distributed
 * according to the defined ratio (70/20/10).
 * 
 * **Validates: Requirements 18.2, 38.2**
 */
BOOST_AUTO_TEST_CASE(property_fee_distribution_ratio)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::FeeDistributor distributor;
        
        CAmount totalFees = RandomFeeAmount();
        uint32_t sequencerCount = 1 + (TestRand32() % 10);  // 1-10 sequencers
        std::vector<uint160> sequencers = RandomSequencerList(sequencerCount);
        uint160 producer = sequencers[0];
        uint64_t blockNumber = 1000 + iteration;
        
        l2::FeeDistributionResult result = distributor.DistributeFees(
            blockNumber, totalFees, producer, sequencers);
        
        // Verify the distribution is valid
        BOOST_CHECK_MESSAGE(result.IsValid(),
            "Distribution invalid for iteration " << iteration <<
            " (totalFees=" << totalFees << ", distributed=" <<
            (result.blockProducerAmount + result.sharedPoolAmount + result.burnedAmount) << ")");
        
        // Verify 70% to block producer (within rounding tolerance)
        CAmount expected70 = (totalFees * 70) / 100;
        // Note: May have uptime bonus or penalty applied
        if (!result.penaltyApplied && result.uptimeBonusApplied == 0) {
            BOOST_CHECK_MESSAGE(result.blockProducerAmount == expected70,
                "Block producer amount wrong for iteration " << iteration <<
                " (expected=" << expected70 << ", actual=" << result.blockProducerAmount << ")");
        }
        
        // Verify 20% to shared pool
        CAmount expected20 = (totalFees * 20) / 100;
        BOOST_CHECK_MESSAGE(result.sharedPoolAmount == expected20,
            "Shared pool amount wrong for iteration " << iteration <<
            " (expected=" << expected20 << ", actual=" << result.sharedPoolAmount << ")");
        
        // Verify 10% burned (may include rounding remainder)
        CAmount expected10 = (totalFees * 10) / 100;
        BOOST_CHECK_MESSAGE(result.burnedAmount >= expected10,
            "Burn amount wrong for iteration " << iteration <<
            " (expected>=" << expected10 << ", actual=" << result.burnedAmount << ")");
    }
}


/**
 * **Property: Fee Conservation**
 * 
 * *For any* fee distribution, the sum of all distributed amounts SHALL
 * equal the total fees collected.
 * 
 * **Validates: Requirements 18.2**
 */
BOOST_AUTO_TEST_CASE(property_fee_conservation)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        CAmount totalFees = RandomFeeAmount();
        CAmount blockProducerAmount, sharedPoolAmount, burnAmount;
        
        l2::FeeDistributor::CalculateSplit(totalFees, blockProducerAmount, sharedPoolAmount, burnAmount);
        
        CAmount distributed = blockProducerAmount + sharedPoolAmount + burnAmount;
        
        BOOST_CHECK_MESSAGE(distributed == totalFees,
            "Fee conservation violated for iteration " << iteration <<
            " (totalFees=" << totalFees << ", distributed=" << distributed << ")");
    }
}

/**
 * **Property: Reward Accumulation**
 * 
 * *For any* sequence of fee distributions, the total rewards for each
 * sequencer SHALL equal the sum of their individual distributions.
 * 
 * **Validates: Requirements 38.2**
 */
BOOST_AUTO_TEST_CASE(property_reward_accumulation)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        std::vector<uint160> sequencers = RandomSequencerList(5);
        uint32_t numBlocks = 10 + (TestRand32() % 20);  // 10-30 blocks
        
        // Track expected rewards
        std::map<uint160, CAmount> expectedRewards;
        for (const auto& seq : sequencers) {
            expectedRewards[seq] = 0;
        }
        
        for (uint32_t block = 0; block < numBlocks; ++block) {
            CAmount totalFees = RandomFeeAmount();
            uint160 producer = sequencers[TestRand32() % sequencers.size()];
            
            l2::FeeDistributionResult result = distributor.DistributeFees(
                1000 + block, totalFees, producer, sequencers);
            
            // Calculate expected rewards
            expectedRewards[producer] += result.blockProducerAmount;
            
            if (sequencers.size() > 1) {
                for (const auto& seq : sequencers) {
                    if (seq != producer) {
                        expectedRewards[seq] += result.perSequencerShare;
                    }
                }
            } else {
                expectedRewards[producer] += result.sharedPoolAmount;
            }
        }
        
        // Verify accumulated rewards
        for (const auto& seq : sequencers) {
            auto rewards = distributor.GetSequencerRewards(seq);
            BOOST_CHECK_MESSAGE(rewards.has_value(),
                "Missing rewards for sequencer in iteration " << iteration);
            
            if (rewards.has_value()) {
                BOOST_CHECK_MESSAGE(rewards->totalRewards == expectedRewards[seq],
                    "Reward accumulation mismatch for iteration " << iteration <<
                    " (expected=" << expectedRewards[seq] << ", actual=" << rewards->totalRewards << ")");
            }
        }
    }
}

/**
 * **Property: Burn Accumulation**
 * 
 * *For any* sequence of fee distributions, the total burned amount SHALL
 * equal the sum of all individual burn amounts.
 * 
 * **Validates: Requirements 18.2**
 */
BOOST_AUTO_TEST_CASE(property_burn_accumulation)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint160 producer = RandomAddress();
        std::vector<uint160> sequencers = {producer};
        
        uint32_t numBlocks = 10 + (TestRand32() % 20);
        CAmount expectedTotalBurn = 0;
        
        for (uint32_t block = 0; block < numBlocks; ++block) {
            CAmount totalFees = RandomFeeAmount();
            
            l2::FeeDistributionResult result = distributor.DistributeFees(
                1000 + block, totalFees, producer, sequencers);
            
            expectedTotalBurn += result.burnedAmount;
        }
        
        BOOST_CHECK_MESSAGE(distributor.GetTotalBurned() == expectedTotalBurn,
            "Burn accumulation mismatch for iteration " << iteration <<
            " (expected=" << expectedTotalBurn << ", actual=" << distributor.GetTotalBurned() << ")");
    }
}


/**
 * **Property: Uptime Bonus Qualification**
 * 
 * *For any* sequencer with uptime >= 99.9% and sufficient blocks produced,
 * they SHALL qualify for the uptime bonus.
 * 
 * **Validates: Requirements 38.3**
 */
BOOST_AUTO_TEST_CASE(property_uptime_bonus_qualification)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint160 sequencer = RandomAddress();
        distributor.RegisterSequencer(sequencer, 80, 100 * COIN);
        
        // Generate random number of blocks produced and missed
        uint64_t blocksProduced = l2::MIN_BLOCKS_FOR_UPTIME_BONUS + (TestRand64() % 10000);
        uint64_t blocksMissed = TestRand64() % (blocksProduced / 10);  // Up to 10% missed
        
        // Record blocks
        for (uint64_t i = 0; i < blocksProduced; ++i) {
            distributor.RecordBlockProduced(sequencer, i);
        }
        for (uint64_t i = 0; i < blocksMissed; ++i) {
            distributor.RecordMissedBlock(sequencer, blocksProduced + i);
        }
        
        uint32_t uptimePermille = distributor.GetUptimePermille(sequencer);
        bool qualifies = distributor.QualifiesForUptimeBonus(sequencer);
        
        // Verify qualification logic
        bool shouldQualify = (uptimePermille >= l2::UPTIME_BONUS_THRESHOLD_PERMILLE) &&
                            (blocksProduced >= l2::MIN_BLOCKS_FOR_UPTIME_BONUS);
        
        BOOST_CHECK_MESSAGE(qualifies == shouldQualify,
            "Uptime bonus qualification mismatch for iteration " << iteration <<
            " (uptime=" << uptimePermille << ", blocks=" << blocksProduced <<
            ", expected=" << shouldQualify << ", actual=" << qualifies << ")");
    }
}

/**
 * **Property: Penalty Application**
 * 
 * *For any* penalized sequencer, their block production rewards SHALL be
 * reduced by the penalty percentage.
 * 
 * **Validates: Requirements 38.4**
 */
BOOST_AUTO_TEST_CASE(property_penalty_application)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint160 producer = RandomAddress();
        std::vector<uint160> sequencers = {producer};
        CAmount totalFees = RandomFeeAmount();
        uint64_t currentBlock = 1000;
        
        // Apply penalty
        uint64_t penaltyDuration = 100 + (TestRand64() % 1000);
        distributor.ApplyPenalty(producer, currentBlock, penaltyDuration);
        
        // Distribute fees while penalized
        l2::FeeDistributionResult result = distributor.DistributeFees(
            currentBlock, totalFees, producer, sequencers);
        
        BOOST_CHECK_MESSAGE(result.penaltyApplied,
            "Penalty not applied for iteration " << iteration);
        
        // Verify reduced amount
        CAmount normalAmount = (totalFees * 70) / 100;
        CAmount expectedReduced = l2::FeeDistributor::ApplyPenaltyReduction(normalAmount);
        
        BOOST_CHECK_MESSAGE(result.blockProducerAmount == expectedReduced,
            "Penalty reduction wrong for iteration " << iteration <<
            " (expected=" << expectedReduced << ", actual=" << result.blockProducerAmount << ")");
    }
}

/**
 * **Property: Claim Consistency**
 * 
 * *For any* sequence of claims, the total claimed SHALL never exceed
 * the total rewards earned.
 * 
 * **Validates: Requirements 38.5**
 */
BOOST_AUTO_TEST_CASE(property_claim_consistency)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint160 producer = RandomAddress();
        std::vector<uint160> sequencers = {producer};
        
        // Distribute fees for multiple blocks
        CAmount totalEarned = 0;
        uint32_t numBlocks = 5 + (TestRand32() % 10);
        
        for (uint32_t block = 0; block < numBlocks; ++block) {
            CAmount totalFees = RandomFeeAmount();
            l2::FeeDistributionResult result = distributor.DistributeFees(
                1000 + block, totalFees, producer, sequencers);
            totalEarned += result.blockProducerAmount + result.sharedPoolAmount;
        }
        
        // Try to claim more than earned
        CAmount claimed = distributor.ClaimRewards(producer, totalEarned * 2);
        
        BOOST_CHECK_MESSAGE(claimed <= totalEarned,
            "Claimed more than earned for iteration " << iteration <<
            " (earned=" << totalEarned << ", claimed=" << claimed << ")");
        
        // Verify unclaimed is correct
        CAmount unclaimed = distributor.GetUnclaimedRewards(producer);
        BOOST_CHECK_MESSAGE(unclaimed == totalEarned - claimed,
            "Unclaimed amount wrong for iteration " << iteration <<
            " (expected=" << (totalEarned - claimed) << ", actual=" << unclaimed << ")");
    }
}

/**
 * **Property: Shared Pool Distribution Fairness**
 * 
 * *For any* fee distribution with multiple sequencers, the shared pool
 * SHALL be distributed equally among non-producing sequencers.
 * 
 * **Validates: Requirements 18.2**
 */
BOOST_AUTO_TEST_CASE(property_shared_pool_fairness)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint32_t sequencerCount = 2 + (TestRand32() % 9);  // 2-10 sequencers
        std::vector<uint160> sequencers = RandomSequencerList(sequencerCount);
        uint160 producer = sequencers[0];
        CAmount totalFees = RandomFeeAmount();
        
        l2::FeeDistributionResult result = distributor.DistributeFees(
            1000, totalFees, producer, sequencers);
        
        // Calculate expected per-sequencer share
        CAmount sharedPool = (totalFees * 20) / 100;
        CAmount expectedShare = sharedPool / (sequencerCount - 1);
        
        // Verify each non-producer got the same share
        for (size_t i = 1; i < sequencers.size(); ++i) {
            auto rewards = distributor.GetSequencerRewards(sequencers[i]);
            BOOST_CHECK_MESSAGE(rewards.has_value(),
                "Missing rewards for sequencer " << i << " in iteration " << iteration);
            
            if (rewards.has_value()) {
                BOOST_CHECK_MESSAGE(rewards->sharedPoolRewards == expectedShare,
                    "Unfair shared pool distribution for iteration " << iteration <<
                    " (expected=" << expectedShare << ", actual=" << rewards->sharedPoolRewards << ")");
            }
        }
    }
}

// ============================================================================
// Burn-and-Mint Model Property Tests (Requirements 6.1-6.6)
// ============================================================================

/**
 * Helper function to create a random L2 transaction with fees
 */
static l2::L2Transaction CreateRandomTransaction()
{
    l2::L2Transaction tx;
    tx.from = RandomAddress();
    tx.to = RandomAddress();
    tx.value = TestRand64() % (10 * COIN);
    tx.nonce = TestRand64() % 1000;
    tx.gasLimit = 21000 + (TestRand64() % 100000);
    tx.gasPrice = 1000 + (TestRand64() % 10000);  // 1000-11000 satoshis per gas
    tx.gasUsed = tx.gasLimit / 2 + (TestRand64() % (tx.gasLimit / 2));  // 50-100% of limit
    tx.type = l2::L2TxType::TRANSFER;
    return tx;
}

/**
 * Helper function to create a list of random transactions
 */
static std::vector<l2::L2Transaction> CreateRandomTransactions(size_t count)
{
    std::vector<l2::L2Transaction> txs;
    txs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        txs.push_back(CreateRandomTransaction());
    }
    return txs;
}

/**
 * **Property 10: Fee-Only Sequencer Rewards**
 * 
 * *For any* L2 block, the sequencer reward SHALL equal exactly the sum of
 * transaction fees in that block. No new tokens SHALL be minted as block rewards.
 * 
 * **Validates: Requirements 6.1, 6.2, 6.3**
 * 
 * This property ensures that:
 * 1. Sequencer rewards come ONLY from transaction fees (Requirement 6.1)
 * 2. NO new tokens are minted for block rewards (Requirement 6.2)
 * 3. Block producer receives the transaction fees (Requirement 6.3)
 */
BOOST_AUTO_TEST_CASE(property_fee_only_sequencer_rewards)
{
    // Run 100 iterations as required for property-based tests
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::FeeDistributor distributor;
        
        // Generate random sequencer and transactions
        uint160 sequencer = RandomAddress();
        uint32_t txCount = TestRand32() % 20;  // 0-19 transactions
        std::vector<l2::L2Transaction> transactions = CreateRandomTransactions(txCount);
        uint64_t blockNumber = 1000 + iteration;
        
        // Calculate expected fees (sum of gasUsed * gasPrice for all transactions)
        CAmount expectedFees = 0;
        for (const auto& tx : transactions) {
            uint64_t gasUsed = tx.gasUsed > 0 ? tx.gasUsed : tx.gasLimit;
            CAmount gasPrice = tx.gasPrice > 0 ? tx.gasPrice : tx.maxFeePerGas;
            expectedFees += static_cast<CAmount>(gasUsed) * gasPrice;
        }
        
        // Get initial state
        CAmount initialFeesEarned = distributor.GetTotalFeesEarned(sequencer);
        
        // Distribute block fees
        bool success = distributor.DistributeBlockFees(blockNumber, sequencer, transactions);
        BOOST_CHECK_MESSAGE(success, 
            "DistributeBlockFees failed for iteration " << iteration);
        
        // Verify: Sequencer reward equals exactly the sum of transaction fees
        CAmount feesEarned = distributor.GetTotalFeesEarned(sequencer);
        CAmount actualReward = feesEarned - initialFeesEarned;
        
        BOOST_CHECK_MESSAGE(actualReward == expectedFees,
            "Fee-only reward violated for iteration " << iteration <<
            " (expected=" << expectedFees << ", actual=" << actualReward << 
            ", txCount=" << txCount << ")");
        
        // Verify: CalculateBlockFees returns the same value
        CAmount calculatedFees = distributor.CalculateBlockFees(transactions);
        BOOST_CHECK_MESSAGE(calculatedFees == expectedFees,
            "CalculateBlockFees mismatch for iteration " << iteration <<
            " (expected=" << expectedFees << ", calculated=" << calculatedFees << ")");
        
        // Verify: Empty block means zero reward (Requirement 6.5)
        if (txCount == 0) {
            BOOST_CHECK_MESSAGE(actualReward == 0,
                "Non-zero reward for empty block in iteration " << iteration <<
                " (reward=" << actualReward << ")");
        }
    }
}

/**
 * **Property: Fee Accumulation Across Blocks**
 * 
 * *For any* sequence of blocks produced by a sequencer, the total fees earned
 * SHALL equal the sum of fees from all blocks.
 * 
 * **Validates: Requirements 6.3, 6.4**
 */
BOOST_AUTO_TEST_CASE(property_fee_accumulation_across_blocks)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint160 sequencer = RandomAddress();
        uint32_t numBlocks = 5 + (TestRand32() % 10);  // 5-14 blocks
        
        CAmount expectedTotalFees = 0;
        
        for (uint32_t block = 0; block < numBlocks; ++block) {
            uint32_t txCount = 1 + (TestRand32() % 10);  // 1-10 transactions
            std::vector<l2::L2Transaction> transactions = CreateRandomTransactions(txCount);
            
            // Calculate expected fees for this block
            CAmount blockFees = distributor.CalculateBlockFees(transactions);
            expectedTotalFees += blockFees;
            
            // Distribute fees
            distributor.DistributeBlockFees(1000 + block, sequencer, transactions);
        }
        
        // Verify total fees earned
        CAmount actualTotalFees = distributor.GetTotalFeesEarned(sequencer);
        BOOST_CHECK_MESSAGE(actualTotalFees == expectedTotalFees,
            "Fee accumulation mismatch for iteration " << iteration <<
            " (expected=" << expectedTotalFees << ", actual=" << actualTotalFees << ")");
    }
}

/**
 * **Property: Minimum Fee Validation**
 * 
 * *For any* transaction, the system SHALL reject it if the fee is below
 * the minimum transaction fee.
 * 
 * **Validates: Requirement 6.6**
 */
BOOST_AUTO_TEST_CASE(property_minimum_fee_validation)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::L2Transaction tx;
        tx.from = RandomAddress();
        tx.to = RandomAddress();
        tx.gasLimit = 21000;  // Standard transfer gas limit
        
        // Generate random gas price
        CAmount gasPrice = TestRand64() % 200;  // 0-199 satoshis per gas
        tx.gasPrice = gasPrice;
        
        CAmount maxFee = static_cast<CAmount>(tx.gasLimit) * gasPrice;
        bool shouldBeValid = maxFee >= l2::MIN_TRANSACTION_FEE;
        
        bool isValid = l2::FeeDistributor::ValidateMinimumFee(tx);
        
        BOOST_CHECK_MESSAGE(isValid == shouldBeValid,
            "Minimum fee validation mismatch for iteration " << iteration <<
            " (gasPrice=" << gasPrice << ", maxFee=" << maxFee << 
            ", minFee=" << l2::MIN_TRANSACTION_FEE <<
            ", expected=" << shouldBeValid << ", actual=" << isValid << ")");
    }
}

/**
 * **Property: Fee History Consistency**
 * 
 * *For any* sequence of fee distributions, the fee history SHALL accurately
 * reflect all distributions within the queried block range.
 * 
 * **Validates: Requirement 6.4**
 */
BOOST_AUTO_TEST_CASE(property_fee_history_consistency)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::FeeDistributor distributor;
        
        uint160 sequencer = RandomAddress();
        uint32_t numBlocks = 10 + (TestRand32() % 20);  // 10-29 blocks
        uint64_t startBlock = 1000;
        
        std::vector<CAmount> expectedFees;
        
        for (uint32_t block = 0; block < numBlocks; ++block) {
            uint32_t txCount = 1 + (TestRand32() % 5);
            std::vector<l2::L2Transaction> transactions = CreateRandomTransactions(txCount);
            
            CAmount blockFees = distributor.CalculateBlockFees(transactions);
            expectedFees.push_back(blockFees);
            
            distributor.DistributeBlockFees(startBlock + block, sequencer, transactions);
        }
        
        // Query fee history
        auto history = distributor.GetFeeHistory(sequencer, startBlock, startBlock + numBlocks - 1);
        
        BOOST_CHECK_MESSAGE(history.size() == numBlocks,
            "Fee history size mismatch for iteration " << iteration <<
            " (expected=" << numBlocks << ", actual=" << history.size() << ")");
        
        // Verify each entry
        for (size_t i = 0; i < history.size() && i < expectedFees.size(); ++i) {
            BOOST_CHECK_MESSAGE(history[i].totalFees == expectedFees[i],
                "Fee history entry mismatch for iteration " << iteration <<
                ", block " << i <<
                " (expected=" << expectedFees[i] << ", actual=" << history[i].totalFees << ")");
            
            BOOST_CHECK_MESSAGE(history[i].blockNumber == startBlock + i,
                "Block number mismatch in fee history for iteration " << iteration);
            
            BOOST_CHECK_MESSAGE(history[i].sequencerAddress == sequencer,
                "Sequencer address mismatch in fee history for iteration " << iteration);
        }
    }
}

/**
 * **Property: No Minting in Fee Distribution**
 * 
 * *For any* fee distribution, the total L2 token supply SHALL NOT increase.
 * Sequencer rewards come from existing fees, not new token creation.
 * 
 * **Validates: Requirements 6.1, 6.2**
 * 
 * Note: This test verifies that DistributeBlockFees only credits existing fees
 * and does not create new tokens. The actual supply tracking is done by L2TokenMinter.
 */
BOOST_AUTO_TEST_CASE(property_no_minting_in_fee_distribution)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::FeeDistributor distributor;
        
        // Create multiple sequencers
        std::vector<uint160> sequencers = RandomSequencerList(5);
        uint32_t numBlocks = 10;
        
        CAmount totalFeesDistributed = 0;
        CAmount totalFeesEarnedBySequencers = 0;
        
        for (uint32_t block = 0; block < numBlocks; ++block) {
            uint160 producer = sequencers[TestRand32() % sequencers.size()];
            uint32_t txCount = 1 + (TestRand32() % 10);
            std::vector<l2::L2Transaction> transactions = CreateRandomTransactions(txCount);
            
            CAmount blockFees = distributor.CalculateBlockFees(transactions);
            totalFeesDistributed += blockFees;
            
            distributor.DistributeBlockFees(1000 + block, producer, transactions);
        }
        
        // Sum up all fees earned by all sequencers
        for (const auto& seq : sequencers) {
            totalFeesEarnedBySequencers += distributor.GetTotalFeesEarned(seq);
        }
        
        // Verify: Total fees earned equals total fees distributed
        // This ensures no new tokens were created
        BOOST_CHECK_MESSAGE(totalFeesEarnedBySequencers == totalFeesDistributed,
            "Fee conservation violated for iteration " << iteration <<
            " (distributed=" << totalFeesDistributed << 
            ", earned=" << totalFeesEarnedBySequencers << ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
