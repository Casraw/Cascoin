// Copyright (c) 2014-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>
#include <net.h>

#include <test/test_bitcoin.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

// Cascoin: Test block subsidy with Cascoin-specific rules
// - Block 0 returns 0
// - Block 1 returns 50 COIN
// - Subsidy halves every nSubsidyHalvingInterval blocks
// - No subsidy past totalMoneySupplyHeight
static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    CAmount nInitialSubsidy = 50 * COIN;

    // Cascoin: Block 0 returns 0
    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, consensusParams), 0);
    
    // Cascoin: Block 1 returns 50 COIN
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, consensusParams), nInitialSubsidy);

    // Test halving at each interval boundary (starting from interval 1)
    // Skip interval 0 since block 0 has special handling
    CAmount nExpectedSubsidy = nInitialSubsidy;
    for (int nHalvings = 1; nHalvings < 10; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        
        // Skip if past money supply height
        if (nHeight >= consensusParams.totalMoneySupplyHeight) {
            BOOST_CHECK_EQUAL(GetBlockSubsidy(nHeight, consensusParams), 0);
            continue;
        }
        
        // Calculate expected subsidy after halvings
        nExpectedSubsidy = nInitialSubsidy >> nHalvings;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nExpectedSubsidy);
    }
    
    // Test that subsidy is 0 past totalMoneySupplyHeight
    BOOST_CHECK_EQUAL(GetBlockSubsidy(consensusParams.totalMoneySupplyHeight, consensusParams), 0);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(consensusParams.totalMoneySupplyHeight + 1, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params mainConsensusParams = chainParams->GetConsensus();
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    consensusParams.lastScryptBlock = mainConsensusParams.lastScryptBlock;
    consensusParams.slowStartBlocks = mainConsensusParams.slowStartBlocks;
    consensusParams.totalMoneySupplyHeight = mainConsensusParams.totalMoneySupplyHeight;
    consensusParams.premineAmount = mainConsensusParams.premineAmount;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    TestBlockSubsidyHalvings(chainParams->GetConsensus()); // As in main
    TestBlockSubsidyHalvings(150); // As in regtest
    TestBlockSubsidyHalvings(1000); // Just another interval
}

BOOST_AUTO_TEST_CASE(block_subsidy_money_limit)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params consensusParams = chainParams->GetConsensus();
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight < 6215971; nHeight += 1) {  // Test a few blocks past money limit block
        CAmount nSubsidy = GetBlockSubsidy(nHeight, chainParams->GetConsensus());
        nSum += nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
    }
    // Cascoin: Total money supply based on actual subsidy schedule
    // Block 0: 0, Block 1: 50 CAS, then standard halving every 840000 blocks
    // until totalMoneySupplyHeight (6215968)
    // Note: COIN = 10000000 due to COIN_SCALE = 10 (10:1 coinswap)
    BOOST_CHECK_EQUAL(nSum, 834749375000000ULL);
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}
BOOST_AUTO_TEST_SUITE_END()
