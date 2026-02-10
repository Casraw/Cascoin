// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_registry_tests.cpp
 * @brief Unit tests for L2 Chain Registry
 * 
 * Tests for:
 * - L2 chain registration
 * - Chain info queries
 * - Deployment parameter validation
 * - Unique chain ID generation
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5
 */

#include <boost/test/unit_test.hpp>

#include <l2/l2_registry.h>
#include <l2/l2_common.h>
#include <hash.h>
#include <random.h>

#include <chrono>

BOOST_AUTO_TEST_SUITE(l2_registry_tests)

// ============================================================================
// Helper Functions
// ============================================================================

static uint160 GenerateRandomAddress() {
    uint160 addr;
    GetRandBytes(addr.begin(), 20);
    return addr;
}

static l2::L2DeploymentParams CreateDefaultParams() {
    l2::L2DeploymentParams params;
    params.blockTimeMs = 500;
    params.gasLimit = 30000000;
    params.challengePeriod = 604800;  // 7 days
    params.minSequencerStake = 100 * COIN;
    params.minSequencerHatScore = 70;
    params.l1AnchorInterval = 100;
    return params;
}

// ============================================================================
// Task 21.1: L2Registry CVM Contract Tests
// Requirements: 1.1, 1.5
// ============================================================================

BOOST_AUTO_TEST_CASE(registry_initialization)
{
    // Test that registry can be created
    l2::L2Registry registry;
    
    BOOST_CHECK_EQUAL(registry.GetChainCount(), 0);
    BOOST_CHECK(!registry.ChainExists(1001));
}

BOOST_AUTO_TEST_CASE(register_l2_chain_basic)
{
    l2::L2Registry registry;
    
    std::string chainName = "TestChain";
    uint160 deployer = GenerateRandomAddress();
    CAmount stake = 1000 * COIN;
    l2::L2DeploymentParams params = CreateDefaultParams();
    uint64_t l1BlockNumber = 100;
    
    // Register chain
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, stake, params, l1BlockNumber);
    
    // Verify registration succeeded
    BOOST_CHECK(chainId >= l2::CHAIN_ID_RANGE_START);
    BOOST_CHECK(chainId <= l2::CHAIN_ID_RANGE_END);
    BOOST_CHECK(registry.ChainExists(chainId));
    BOOST_CHECK(registry.ChainNameExists(chainName));
    BOOST_CHECK_EQUAL(registry.GetChainCount(), 1);
}

BOOST_AUTO_TEST_CASE(get_l2_chain_info)
{
    l2::L2Registry registry;
    
    std::string chainName = "InfoTestChain";
    uint160 deployer = GenerateRandomAddress();
    CAmount stake = 1500 * COIN;
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.blockTimeMs = 250;
    params.gasLimit = 50000000;
    uint64_t l1BlockNumber = 200;
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, stake, params, l1BlockNumber);
    BOOST_REQUIRE(chainId > 0);
    
    // Get chain info by ID
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_REQUIRE(info.has_value());
    
    BOOST_CHECK_EQUAL(info->chainId, chainId);
    BOOST_CHECK_EQUAL(info->name, chainName);
    BOOST_CHECK(info->deployer == deployer);
    BOOST_CHECK_EQUAL(info->deployerStake, stake);
    BOOST_CHECK_EQUAL(info->deploymentBlock, l1BlockNumber);
    BOOST_CHECK_EQUAL(info->params.blockTimeMs, 250);
    BOOST_CHECK_EQUAL(info->params.gasLimit, 50000000);
    BOOST_CHECK(info->status == l2::L2ChainStatus::BOOTSTRAPPING);
    
    // Get chain info by name
    std::optional<l2::L2ChainInfo> infoByName = registry.GetL2ChainInfoByName(chainName);
    BOOST_REQUIRE(infoByName.has_value());
    BOOST_CHECK_EQUAL(infoByName->chainId, chainId);
}

BOOST_AUTO_TEST_CASE(register_multiple_chains)
{
    l2::L2Registry registry;
    
    std::vector<uint64_t> chainIds;
    
    // Register multiple chains
    for (int i = 0; i < 5; i++) {
        std::string name = "Chain" + std::to_string(i);
        uint160 deployer = GenerateRandomAddress();
        CAmount stake = (1000 + i * 100) * COIN;
        l2::L2DeploymentParams params = CreateDefaultParams();
        
        uint64_t chainId = registry.RegisterL2Chain(name, deployer, stake, params, 100 + i);
        BOOST_REQUIRE(chainId > 0);
        chainIds.push_back(chainId);
    }
    
    BOOST_CHECK_EQUAL(registry.GetChainCount(), 5);
    
    // Verify all chains are unique
    std::set<uint64_t> uniqueIds(chainIds.begin(), chainIds.end());
    BOOST_CHECK_EQUAL(uniqueIds.size(), 5);
    
    // Verify all chains exist
    for (uint64_t id : chainIds) {
        BOOST_CHECK(registry.ChainExists(id));
    }
}

BOOST_AUTO_TEST_CASE(duplicate_name_rejected)
{
    l2::L2Registry registry;
    
    std::string chainName = "DuplicateTest";
    uint160 deployer1 = GenerateRandomAddress();
    uint160 deployer2 = GenerateRandomAddress();
    CAmount stake = 1000 * COIN;
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    // First registration should succeed
    uint64_t chainId1 = registry.RegisterL2Chain(chainName, deployer1, stake, params, 100);
    BOOST_CHECK(chainId1 > 0);
    
    // Second registration with same name should fail
    uint64_t chainId2 = registry.RegisterL2Chain(chainName, deployer2, stake, params, 101);
    BOOST_CHECK_EQUAL(chainId2, 0);
    (void)chainId2;  // Suppress unused variable warning
    
    BOOST_CHECK_EQUAL(registry.GetChainCount(), 1);
}

BOOST_AUTO_TEST_CASE(get_all_chains)
{
    l2::L2Registry registry;
    
    // Register some chains
    for (int i = 0; i < 3; i++) {
        std::string name = "AllChains" + std::to_string(i);
        uint160 deployer = GenerateRandomAddress();
        l2::L2DeploymentParams params = CreateDefaultParams();
        registry.RegisterL2Chain(name, deployer, 1000 * COIN, params, 100 + i);
    }
    
    std::vector<l2::L2ChainInfo> allChains = registry.GetAllChains();
    BOOST_CHECK_EQUAL(allChains.size(), 3);
}

BOOST_AUTO_TEST_CASE(get_active_chains)
{
    l2::L2Registry registry;
    
    // Register chains
    std::string name1 = "ActiveChain1";
    std::string name2 = "ActiveChain2";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId1 = registry.RegisterL2Chain(name1, deployer, 1000 * COIN, params, 100);
    uint64_t chainId2 = registry.RegisterL2Chain(name2, deployer, 1000 * COIN, params, 101);
    (void)chainId2;  // Suppress unused variable warning - we just need it registered
    
    // Initially both are BOOTSTRAPPING
    std::vector<l2::L2ChainInfo> activeChains = registry.GetActiveChains();
    BOOST_CHECK_EQUAL(activeChains.size(), 0);
    
    // Activate one chain
    registry.UpdateChainStatus(chainId1, l2::L2ChainStatus::ACTIVE);
    
    activeChains = registry.GetActiveChains();
    BOOST_CHECK_EQUAL(activeChains.size(), 1);
    BOOST_CHECK_EQUAL(activeChains[0].chainId, chainId1);
}

// ============================================================================
// Task 21.2: L2 Deployment Validation Tests
// Requirements: 1.2, 1.3, 1.4
// ============================================================================

BOOST_AUTO_TEST_CASE(validate_deployment_params_valid)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(result.isValid);
    BOOST_CHECK(result.errorMessage.empty());
}

BOOST_AUTO_TEST_CASE(validate_block_time_too_low)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.blockTimeMs = 50;  // Below minimum of 100ms
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Block time") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_block_time_too_high)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.blockTimeMs = 120000;  // Above maximum of 60000ms
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Block time") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_gas_limit_too_low)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.gasLimit = 500000;  // Below minimum of 1M
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Gas limit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_gas_limit_too_high)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.gasLimit = 200000000;  // Above maximum of 100M
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Gas limit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_challenge_period_too_short)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.challengePeriod = 1800;  // Below minimum of 3600 (1 hour)
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Challenge period") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_challenge_period_too_long)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.challengePeriod = 5000000;  // Above maximum of 2592000 (30 days)
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Challenge period") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_sequencer_stake_too_low)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.minSequencerStake = 5 * COIN;  // Below minimum of 10 CAS
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("sequencer stake") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_sequencer_hat_score_too_low)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.minSequencerHatScore = 30;  // Below minimum of 50
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("HAT score") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_sequencer_hat_score_too_high)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.minSequencerHatScore = 150;  // Above maximum of 100
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("HAT score") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_l1_anchor_interval_zero)
{
    l2::L2DeploymentParams params = CreateDefaultParams();
    params.l1AnchorInterval = 0;
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeploymentParams(params);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("anchor interval") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_deployer_stake_valid)
{
    CAmount stake = 1000 * COIN;
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeployerStake(stake);
    BOOST_CHECK(result.isValid);
}

BOOST_AUTO_TEST_CASE(validate_deployer_stake_too_low)
{
    CAmount stake = 500 * COIN;  // Below minimum of 1000 CAS
    
    l2::ValidationResult result = l2::L2Registry::ValidateDeployerStake(stake);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("stake") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_chain_name_valid)
{
    l2::ValidationResult result = l2::L2Registry::ValidateChainName("ValidChainName");
    BOOST_CHECK(result.isValid);
    
    result = l2::L2Registry::ValidateChainName("Chain_With_Underscores");
    BOOST_CHECK(result.isValid);
    
    result = l2::L2Registry::ValidateChainName("Chain-With-Hyphens");
    BOOST_CHECK(result.isValid);
    
    result = l2::L2Registry::ValidateChainName("Chain123");
    BOOST_CHECK(result.isValid);
}

BOOST_AUTO_TEST_CASE(validate_chain_name_empty)
{
    l2::ValidationResult result = l2::L2Registry::ValidateChainName("");
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("empty") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_chain_name_too_long)
{
    std::string longName(100, 'a');  // 100 characters, above max of 64
    
    l2::ValidationResult result = l2::L2Registry::ValidateChainName(longName);
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("exceed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_chain_name_invalid_chars)
{
    l2::ValidationResult result = l2::L2Registry::ValidateChainName("Chain With Spaces");
    BOOST_CHECK(!result.isValid);
    
    result = l2::L2Registry::ValidateChainName("Chain@Special");
    BOOST_CHECK(!result.isValid);
    
    result = l2::L2Registry::ValidateChainName("123StartWithNumber");
    BOOST_CHECK(!result.isValid);
}

BOOST_AUTO_TEST_CASE(generate_unique_chain_ids)
{
    l2::L2Registry registry;
    
    std::set<uint64_t> generatedIds;
    
    // Generate many chain IDs and verify uniqueness
    for (int i = 0; i < 100; i++) {
        std::string name = "UniqueChain" + std::to_string(i);
        uint160 deployer = GenerateRandomAddress();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + i;
        
        uint64_t chainId = registry.GenerateChainId(name, deployer, timestamp);
        
        BOOST_CHECK(chainId >= l2::CHAIN_ID_RANGE_START);
        BOOST_CHECK(chainId <= l2::CHAIN_ID_RANGE_END);
        BOOST_CHECK(generatedIds.find(chainId) == generatedIds.end());
        
        generatedIds.insert(chainId);
    }
    
    BOOST_CHECK_EQUAL(generatedIds.size(), 100);
}

// ============================================================================
// State Update Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(update_chain_state)
{
    l2::L2Registry registry;
    
    std::string chainName = "StateUpdateChain";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, 1000 * COIN, params, 100);
    BOOST_REQUIRE(chainId > 0);
    
    // Update state
    uint256 newStateRoot;
    GetRandBytes(newStateRoot.begin(), 32);
    uint64_t l2BlockNumber = 500;
    uint64_t l1AnchorBlock = 150;
    
    bool success = registry.UpdateChainState(chainId, newStateRoot, l2BlockNumber, l1AnchorBlock);
    BOOST_CHECK(success);
    
    // Verify update
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_REQUIRE(info.has_value());
    BOOST_CHECK(info->latestStateRoot == newStateRoot);
    BOOST_CHECK_EQUAL(info->latestL2Block, l2BlockNumber);
    BOOST_CHECK_EQUAL(info->latestL1Anchor, l1AnchorBlock);
}

BOOST_AUTO_TEST_CASE(update_chain_status)
{
    l2::L2Registry registry;
    
    std::string chainName = "StatusUpdateChain";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, 1000 * COIN, params, 100);
    BOOST_REQUIRE(chainId > 0);
    
    // Initial status should be BOOTSTRAPPING
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK(info->status == l2::L2ChainStatus::BOOTSTRAPPING);
    
    // Update to ACTIVE
    bool success = registry.UpdateChainStatus(chainId, l2::L2ChainStatus::ACTIVE);
    BOOST_CHECK(success);
    
    info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK(info->status == l2::L2ChainStatus::ACTIVE);
    BOOST_CHECK(info->IsActive());
    
    // Update to PAUSED
    success = registry.UpdateChainStatus(chainId, l2::L2ChainStatus::PAUSED);
    BOOST_CHECK(success);
    
    info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK(info->status == l2::L2ChainStatus::PAUSED);
    BOOST_CHECK(!info->IsActive());
}

BOOST_AUTO_TEST_CASE(update_chain_tvl)
{
    l2::L2Registry registry;
    
    std::string chainName = "TVLUpdateChain";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, 1000 * COIN, params, 100);
    BOOST_REQUIRE(chainId > 0);
    
    // Update TVL
    CAmount newTVL = 50000 * COIN;
    bool success = registry.UpdateChainTVL(chainId, newTVL);
    BOOST_CHECK(success);
    
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK_EQUAL(info->totalValueLocked, newTVL);
}

BOOST_AUTO_TEST_CASE(update_sequencer_count)
{
    l2::L2Registry registry;
    
    std::string chainName = "SeqCountChain";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, 1000 * COIN, params, 100);
    BOOST_REQUIRE(chainId > 0);
    
    // Update sequencer count
    bool success = registry.UpdateSequencerCount(chainId, 10);
    BOOST_CHECK(success);
    
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK_EQUAL(info->sequencerCount, 10);
}

BOOST_AUTO_TEST_CASE(set_bridge_contract)
{
    l2::L2Registry registry;
    
    std::string chainName = "BridgeChain";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, 1000 * COIN, params, 100);
    BOOST_REQUIRE(chainId > 0);
    
    // Set bridge contract
    uint160 bridgeContract = GenerateRandomAddress();
    bool success = registry.SetBridgeContract(chainId, bridgeContract);
    BOOST_CHECK(success);
    
    // Verify
    std::optional<uint160> retrievedBridge = registry.GetBridgeContract(chainId);
    BOOST_REQUIRE(retrievedBridge.has_value());
    BOOST_CHECK(retrievedBridge.value() == bridgeContract);
    
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK(info->bridgeContract == bridgeContract);
}

BOOST_AUTO_TEST_CASE(set_genesis_hash)
{
    l2::L2Registry registry;
    
    std::string chainName = "GenesisChain";
    uint160 deployer = GenerateRandomAddress();
    l2::L2DeploymentParams params = CreateDefaultParams();
    
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, 1000 * COIN, params, 100);
    BOOST_REQUIRE(chainId > 0);
    
    // Set genesis hash
    uint256 genesisHash;
    GetRandBytes(genesisHash.begin(), 32);
    bool success = registry.SetGenesisHash(chainId, genesisHash);
    BOOST_CHECK(success);
    
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(chainId);
    BOOST_CHECK(info->genesisHash == genesisHash);
}

// ============================================================================
// Chain Status Helper Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(chain_status_helpers)
{
    l2::L2ChainInfo info;
    
    // BOOTSTRAPPING
    info.status = l2::L2ChainStatus::BOOTSTRAPPING;
    BOOST_CHECK(!info.IsActive());
    BOOST_CHECK(info.AcceptsDeposits());
    BOOST_CHECK(info.AllowsWithdrawals());
    
    // ACTIVE
    info.status = l2::L2ChainStatus::ACTIVE;
    BOOST_CHECK(info.IsActive());
    BOOST_CHECK(info.AcceptsDeposits());
    BOOST_CHECK(info.AllowsWithdrawals());
    
    // PAUSED
    info.status = l2::L2ChainStatus::PAUSED;
    BOOST_CHECK(!info.IsActive());
    BOOST_CHECK(!info.AcceptsDeposits());
    BOOST_CHECK(info.AllowsWithdrawals());
    
    // EMERGENCY
    info.status = l2::L2ChainStatus::EMERGENCY;
    BOOST_CHECK(!info.IsActive());
    BOOST_CHECK(!info.AcceptsDeposits());
    BOOST_CHECK(info.AllowsWithdrawals());
    
    // DEPRECATED
    info.status = l2::L2ChainStatus::DEPRECATED;
    BOOST_CHECK(!info.IsActive());
    BOOST_CHECK(!info.AcceptsDeposits());
    BOOST_CHECK(!info.AllowsWithdrawals());
}

BOOST_AUTO_TEST_CASE(chain_status_to_string)
{
    BOOST_CHECK_EQUAL(l2::L2ChainStatusToString(l2::L2ChainStatus::BOOTSTRAPPING), "BOOTSTRAPPING");
    BOOST_CHECK_EQUAL(l2::L2ChainStatusToString(l2::L2ChainStatus::ACTIVE), "ACTIVE");
    BOOST_CHECK_EQUAL(l2::L2ChainStatusToString(l2::L2ChainStatus::PAUSED), "PAUSED");
    BOOST_CHECK_EQUAL(l2::L2ChainStatusToString(l2::L2ChainStatus::EMERGENCY), "EMERGENCY");
    BOOST_CHECK_EQUAL(l2::L2ChainStatusToString(l2::L2ChainStatus::DEPRECATED), "DEPRECATED");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(update_nonexistent_chain)
{
    l2::L2Registry registry;
    
    uint64_t nonexistentChainId = 999999;
    
    // All updates should fail for nonexistent chain
    uint256 stateRoot;
    BOOST_CHECK(!registry.UpdateChainState(nonexistentChainId, stateRoot, 100, 50));
    BOOST_CHECK(!registry.UpdateChainStatus(nonexistentChainId, l2::L2ChainStatus::ACTIVE));
    BOOST_CHECK(!registry.UpdateChainTVL(nonexistentChainId, 1000 * COIN));
    BOOST_CHECK(!registry.UpdateSequencerCount(nonexistentChainId, 5));
    BOOST_CHECK(!registry.SetGenesisHash(nonexistentChainId, stateRoot));
    
    uint160 bridgeContract;
    BOOST_CHECK(!registry.SetBridgeContract(nonexistentChainId, bridgeContract));
}

BOOST_AUTO_TEST_CASE(get_nonexistent_chain)
{
    l2::L2Registry registry;
    
    std::optional<l2::L2ChainInfo> info = registry.GetL2ChainInfo(999999);
    BOOST_CHECK(!info.has_value());
    
    info = registry.GetL2ChainInfoByName("NonexistentChain");
    BOOST_CHECK(!info.has_value());
    
    std::optional<uint160> bridge = registry.GetBridgeContract(999999);
    BOOST_CHECK(!bridge.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
