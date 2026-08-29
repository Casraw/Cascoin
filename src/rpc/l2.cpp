// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2.cpp
 * @brief RPC commands for Cascoin Layer 2 functionality
 * 
 * This file implements RPC commands for interacting with the L2 system:
 * - Basic queries (balance, transaction count, blocks)
 * - L2 chain deployment and management
 * - L2 chain registry operations
 * - Sequencer operations
 * - Token info and supply queries (Requirements 8.1, 8.2, 8.3, 8.4)
 * - Token transfers (Requirements 2.5, 7.3)
 * - Faucet operations (Requirements 5.1, 5.5)
 * - Legacy bridge deprecation (Requirements 6.1, 6.3, 9.4, 9.5)
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 2.5, 2.6, 4.1, 4.2, 5.1, 5.5, 6.1, 6.3, 7.3, 8.1, 8.2, 8.3, 8.4, 9.4, 9.5, 11.7, 40.1
 */

#include <rpc/server.h>
#include <rpc/util.h>
#include <core_io.h>
#include <l2/l2_common.h>
#include <l2/l2_chainparams.h>
#include <l2/l2_block.h>
#include <l2/l2_registry.h>
#include <l2/state_manager.h>
#include <l2/l2_globals.h>
#include <l2/fraud_proof.h>
#include <l2/sequencer_discovery.h>
#include <l2/leader_election.h>
#include <l2/bridge_contract.h>
#include <l2/account_state.h>
#include <l2/l2_token.h>
#include <l2/l2_token_manager.h>
#include <l2/l2_faucet.h>
#include <validation.h>
#include <chain.h>
#include <util.h>
#include <base58.h>
#include <utilstrencodings.h>
#include <utilmoneystr.h>
#include <univalue.h>
#include <key.h>
#include <random.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#include <wallet/coincontrol.h>
#include <cvm/validator_keys.h>
#include <net.h>
#include <consensus/validation.h>

#include <memory>
#include <chrono>

// Global L2 components (initialized during node startup)
namespace {
    std::unique_ptr<l2::L2StateManager> g_l2StateManager;
    std::unique_ptr<l2::BridgeContract> g_bridgeContract;
    std::unique_ptr<l2::L2TokenManager> g_l2TokenManager;
    std::unique_ptr<l2::L2Faucet> g_l2Faucet;
    std::map<uint64_t, std::string> g_l2ChainRegistry;  // chainId -> name
    std::vector<l2::L2Block> g_l2Blocks;  // Simple in-memory block storage for now
    CCriticalSection cs_l2;
}

// Helper function to check if L2 is enabled
static void EnsureL2Enabled()
{
    if (!l2::IsL2Enabled()) {
        throw JSONRPCError(RPC_MISC_ERROR, "L2 is not enabled. Start node with -l2 flag.");
    }
}

// Helper function to get the shared L2 state manager.
// Delegates to the process-wide singleton so that balances minted by the
// block-connection code (ProcessConnectedBlockForBurns) are visible here.
static l2::L2StateManager& GetL2StateManager()
{
    return l2::GetGlobalStateManager();
}

// Helper function to get or create bridge contract
static l2::BridgeContract& GetBridgeContract()
{
    LOCK(cs_l2);
    if (!g_bridgeContract) {
        g_bridgeContract = std::make_unique<l2::BridgeContract>(l2::GetL2ChainId());
    }
    return *g_bridgeContract;
}

// Helper function to get or create token manager
static l2::L2TokenManager& GetL2TokenManager()
{
    LOCK(cs_l2);
    if (!g_l2TokenManager) {
        // Create default token config
        l2::L2TokenConfig config("L2Token", "L2T");
        g_l2TokenManager = std::make_unique<l2::L2TokenManager>(l2::GetL2ChainId(), config);
    }
    return *g_l2TokenManager;
}

// Helper function to get or create faucet
static l2::L2Faucet& GetL2Faucet()
{
    LOCK(cs_l2);
    if (!g_l2Faucet) {
        g_l2Faucet = std::make_unique<l2::L2Faucet>(GetL2TokenManager());
    }
    return *g_l2Faucet;
}

// Helper to parse address from hex or base58
static uint160 ParseL2Address(const std::string& addressStr)
{
    std::string addrHex = addressStr;
    
    // Handle 0x prefix
    if (addrHex.substr(0, 2) == "0x") {
        addrHex = addrHex.substr(2);
    }
    
    // If it's 40 hex chars, parse directly.
    // L2 addresses are displayed as "0x" + uint160::GetHex(), which emits the
    // internal bytes in reversed (big-endian) order. Parse them back with
    // SetHex() so the string<->uint160 round-trip is consistent. Using
    // uint160(ParseHex(...)) here would NOT reverse and produce a mirrored
    // address, causing balance lookups to miss the key the minter wrote.
    if (addrHex.length() == 40 && IsHex(addrHex)) {
        uint160 addr;
        addr.SetHex(addrHex);
        return addr;
    }
    
    // Try base58 decode
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address format");
    }
    
    if (const CKeyID* keyId = boost::get<CKeyID>(&dest)) {
        return uint160(*keyId);
    } else if (const CScriptID* scriptId = boost::get<CScriptID>(&dest)) {
        return uint160(*scriptId);
    }
    
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unsupported address type");
}

// ============================================================================
// Task 18.1: Basic L2 RPC Commands
// Requirements: 11.7, 40.1
// ============================================================================

UniValue l2_getbalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_getbalance \"address\"\n"
            "\nGet the L2 balance for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) L2 address (hex or base58)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",      (string) The address\n"
            "  \"balance\": n,           (numeric) Balance in satoshis\n"
            "  \"balance_cas\": \"x.xx\", (string) Balance in CAS\n"
            "  \"nonce\": n              (numeric) Transaction count (nonce)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getbalance", "\"0xa1b2c3d4e5f6...\"")
            + HelpExampleRpc("l2_getbalance", "\"0xa1b2c3d4e5f6...\"")
        );

    EnsureL2Enabled();

    std::string addressStr = request.params[0].get_str();
    uint160 address = ParseL2Address(addressStr);
    
    // Convert to uint256 key for state manager
    uint256 addressKey;
    memcpy(addressKey.begin(), address.begin(), 20);
    
    l2::L2StateManager& stateManager = GetL2StateManager();
    l2::AccountState state = stateManager.GetAccountState(addressKey);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", "0x" + address.GetHex());
    result.pushKV("balance", state.balance);
    result.pushKV("balance_cas", ValueFromAmount(state.balance).write());
    result.pushKV("nonce", (int64_t)state.nonce);
    
    return result;
}

UniValue l2_gettransactioncount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_gettransactioncount \"address\"\n"
            "\nGet the transaction count (nonce) for an L2 address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) L2 address (hex or base58)\n"
            "\nResult:\n"
            "n    (numeric) The transaction count (nonce)\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_gettransactioncount", "\"0xa1b2c3d4e5f6...\"")
            + HelpExampleRpc("l2_gettransactioncount", "\"0xa1b2c3d4e5f6...\"")
        );

    EnsureL2Enabled();

    std::string addressStr = request.params[0].get_str();
    uint160 address = ParseL2Address(addressStr);
    
    uint256 addressKey;
    memcpy(addressKey.begin(), address.begin(), 20);
    
    l2::L2StateManager& stateManager = GetL2StateManager();
    l2::AccountState state = stateManager.GetAccountState(addressKey);
    
    return (int64_t)state.nonce;
}

UniValue l2_getblockbynumber(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "l2_getblockbynumber blocknumber ( verbose )\n"
            "\nGet an L2 block by its number.\n"
            "\nArguments:\n"
            "1. blocknumber    (numeric, required) The L2 block number\n"
            "2. verbose        (boolean, optional, default=true) Include full transaction data\n"
            "\nResult (verbose=true):\n"
            "{\n"
            "  \"number\": n,              (numeric) Block number\n"
            "  \"hash\": \"xxx\",           (string) Block hash\n"
            "  \"parentHash\": \"xxx\",     (string) Parent block hash\n"
            "  \"stateRoot\": \"xxx\",      (string) State root\n"
            "  \"transactionsRoot\": \"xxx\",(string) Transactions Merkle root\n"
            "  \"sequencer\": \"xxx\",      (string) Sequencer address\n"
            "  \"timestamp\": n,           (numeric) Block timestamp\n"
            "  \"gasLimit\": n,            (numeric) Gas limit\n"
            "  \"gasUsed\": n,             (numeric) Gas used\n"
            "  \"l1AnchorBlock\": n,       (numeric) L1 anchor block number\n"
            "  \"transactionCount\": n,    (numeric) Number of transactions\n"
            "  \"signatureCount\": n,      (numeric) Number of sequencer signatures\n"
            "  \"isFinalized\": bool       (boolean) Whether block is finalized\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getblockbynumber", "100")
            + HelpExampleCli("l2_getblockbynumber", "100 false")
            + HelpExampleRpc("l2_getblockbynumber", "100, true")
        );

    EnsureL2Enabled();

    uint64_t blockNumber = request.params[0].get_int64();
    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    
    // Read the block from the persistent L2 block store.
    l2::L2Block block;
    if (!l2::GetL2BlockByNumber(blockNumber, block)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block not found");
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("number", (int64_t)block.header.blockNumber);
    result.pushKV("hash", block.GetHash().GetHex());
    result.pushKV("parentHash", block.header.parentHash.GetHex());
    result.pushKV("stateRoot", block.header.stateRoot.GetHex());
    result.pushKV("transactionsRoot", block.header.transactionsRoot.GetHex());
    result.pushKV("receiptsRoot", block.header.receiptsRoot.GetHex());
    result.pushKV("sequencer", "0x" + block.header.sequencer.GetHex());
    result.pushKV("timestamp", (int64_t)block.header.timestamp);
    result.pushKV("gasLimit", (int64_t)block.header.gasLimit);
    result.pushKV("gasUsed", (int64_t)block.header.gasUsed);
    result.pushKV("l2ChainId", (int64_t)block.header.l2ChainId);
    result.pushKV("l1AnchorBlock", (int64_t)block.header.l1AnchorBlock);
    result.pushKV("l1AnchorHash", block.header.l1AnchorHash.GetHex());
    result.pushKV("slotNumber", (int64_t)block.header.slotNumber);
    result.pushKV("transactionCount", (int64_t)block.transactions.size());
    result.pushKV("signatureCount", (int64_t)block.signatures.size());
    result.pushKV("isFinalized", block.isFinalized);
    
    if (verbose && !block.transactions.empty()) {
        UniValue txArray(UniValue::VARR);
        for (const auto& tx : block.transactions) {
            UniValue txObj(UniValue::VOBJ);
            txObj.pushKV("hash", tx.GetHash().GetHex());
            txObj.pushKV("type", l2::L2TxTypeToString(tx.type));
            txObj.pushKV("from", "0x" + tx.from.GetHex());
            txObj.pushKV("to", "0x" + tx.to.GetHex());
            txObj.pushKV("value", tx.value);
            txObj.pushKV("nonce", (int64_t)tx.nonce);
            txObj.pushKV("gasLimit", (int64_t)tx.gasLimit);
            txObj.pushKV("gasPrice", tx.gasPrice);
            txArray.push_back(txObj);
        }
        result.pushKV("transactions", txArray);
    }
    
    if (!block.signatures.empty()) {
        UniValue sigArray(UniValue::VARR);
        for (const auto& sig : block.signatures) {
            UniValue sigObj(UniValue::VOBJ);
            sigObj.pushKV("sequencer", "0x" + sig.sequencerAddress.GetHex());
            sigObj.pushKV("timestamp", (int64_t)sig.timestamp);
            sigArray.push_back(sigObj);
        }
        result.pushKV("signatures", sigArray);
    }
    
    return result;
}


// ============================================================================
// Task 18.2: L2 Deployment RPC Commands
// Requirements: 1.1, 1.5
// ============================================================================

UniValue l2_deploy(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "l2_deploy \"name\" ( blocktime gaslimit challengeperiod )\n"
            "\nDeploy a new L2 chain instance.\n"
            "\nArguments:\n"
            "1. \"name\"           (string, required) Name for the L2 chain\n"
            "2. blocktime         (numeric, optional, default=500) Target block time in ms\n"
            "3. gaslimit          (numeric, optional, default=30000000) Max gas per block\n"
            "4. challengeperiod   (numeric, optional, default=604800) Challenge period in seconds\n"
            "\nResult:\n"
            "{\n"
            "  \"chainId\": n,           (numeric) Unique L2 chain ID\n"
            "  \"name\": \"xxx\",         (string) Chain name\n"
            "  \"blockTime\": n,         (numeric) Target block time (ms)\n"
            "  \"gasLimit\": n,          (numeric) Max gas per block\n"
            "  \"challengePeriod\": n,   (numeric) Challenge period (seconds)\n"
            "  \"genesisHash\": \"xxx\",  (string) Genesis block hash\n"
            "  \"status\": \"xxx\"        (string) Deployment status\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_deploy", "\"MyL2Chain\"")
            + HelpExampleCli("l2_deploy", "\"MyL2Chain\" 500 30000000 604800")
            + HelpExampleRpc("l2_deploy", "\"MyL2Chain\", 500, 30000000, 604800")
        );

    EnsureL2Enabled();

    std::string chainName = request.params[0].get_str();
    
    uint32_t blockTime = 500;  // 500ms default
    if (request.params.size() > 1) {
        blockTime = request.params[1].get_int();
    }
    
    uint64_t gasLimit = 30000000;  // 30M gas default
    if (request.params.size() > 2) {
        gasLimit = request.params[2].get_int64();
    }
    
    uint64_t challengePeriod = 604800;  // 7 days default
    if (request.params.size() > 3) {
        challengePeriod = request.params[3].get_int64();
    }
    
    LOCK(cs_l2);
    
    // Generate unique chain ID based on name and timestamp
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    CHashWriter ss(SER_GETHASH, 0);
    ss << chainName;
    ss << timestamp;
    uint256 hash = ss.GetHash();
    uint64_t chainId = (hash.GetUint64(0) % 1000000) + 1000;  // Range: 1000-1000999
    
    // Ensure unique
    while (g_l2ChainRegistry.count(chainId) > 0) {
        chainId++;
    }
    
    // Register the chain
    g_l2ChainRegistry[chainId] = chainName;
    
    // Create genesis block
    uint160 genesisSequencer;  // Zero address for genesis
    l2::L2Block genesis = l2::CreateGenesisBlock(chainId, timestamp, genesisSequencer);
    
    // Store genesis block
    if (g_l2Blocks.empty()) {
        g_l2Blocks.push_back(genesis);
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("chainId", (int64_t)chainId);
    result.pushKV("name", chainName);
    result.pushKV("blockTime", (int)blockTime);
    result.pushKV("gasLimit", (int64_t)gasLimit);
    result.pushKV("challengePeriod", (int64_t)challengePeriod);
    result.pushKV("genesisHash", genesis.GetHash().GetHex());
    result.pushKV("status", "deployed");
    
    return result;
}

UniValue l2_getchaininfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "l2_getchaininfo ( chainid )\n"
            "\nGet information about an L2 chain.\n"
            "\nArguments:\n"
            "1. chainid    (numeric, optional) L2 chain ID (default: current chain)\n"
            "\nResult:\n"
            "{\n"
            "  \"chainId\": n,              (numeric) L2 chain ID\n"
            "  \"name\": \"xxx\",            (string) Chain name\n"
            "  \"enabled\": bool,           (boolean) Whether L2 is enabled\n"
            "  \"mode\": \"xxx\",            (string) Node mode (FULL_NODE, LIGHT_CLIENT, DISABLED)\n"
            "  \"blockHeight\": n,          (numeric) Current L2 block height\n"
            "  \"stateRoot\": \"xxx\",       (string) Current state root\n"
            "  \"sequencerCount\": n,       (numeric) Number of known sequencers\n"
            "  \"eligibleSequencers\": n,   (numeric) Number of eligible sequencers\n"
            "  \"params\": {...}            (object) Chain parameters\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getchaininfo", "")
            + HelpExampleCli("l2_getchaininfo", "1001")
            + HelpExampleRpc("l2_getchaininfo", "1001")
        );

    EnsureL2Enabled();

    uint64_t chainId = l2::GetL2ChainId();
    if (request.params.size() > 0) {
        chainId = request.params[0].get_int64();
    }
    
    LOCK(cs_l2);
    
    l2::L2StateManager& stateManager = GetL2StateManager();
    const l2::L2Params& params = l2::GetL2Params();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("chainId", (int64_t)chainId);
    
    // Get chain name from registry
    std::string chainName = "default";
    if (g_l2ChainRegistry.count(chainId) > 0) {
        chainName = g_l2ChainRegistry[chainId];
    }
    result.pushKV("name", chainName);
    
    result.pushKV("enabled", l2::IsL2Enabled());
    
    std::string modeStr;
    switch (l2::GetL2NodeMode()) {
        case l2::L2NodeMode::FULL_NODE: modeStr = "FULL_NODE"; break;
        case l2::L2NodeMode::LIGHT_CLIENT: modeStr = "LIGHT_CLIENT"; break;
        default: modeStr = "DISABLED"; break;
    }
    result.pushKV("mode", modeStr);
    
    result.pushKV("blockHeight", (int64_t)stateManager.GetBlockNumber());
    result.pushKV("stateRoot", stateManager.GetStateRoot().GetHex());
    result.pushKV("accountCount", (int64_t)stateManager.GetAccountCount());
    
    // Sequencer info
    size_t sequencerCount = 0;
    size_t eligibleCount = 0;
    if (l2::IsSequencerDiscoveryInitialized()) {
        l2::SequencerDiscovery& discovery = l2::GetSequencerDiscovery();
        sequencerCount = discovery.GetSequencerCount();
        eligibleCount = discovery.GetEligibleCount();
    }
    result.pushKV("sequencerCount", (int64_t)sequencerCount);
    result.pushKV("eligibleSequencers", (int64_t)eligibleCount);
    
    // Chain parameters
    UniValue paramsObj(UniValue::VOBJ);
    paramsObj.pushKV("minSequencerHATScore", (int)params.nMinSequencerHATScore);
    paramsObj.pushKV("minSequencerStake", ValueFromAmount(params.nMinSequencerStake));
    paramsObj.pushKV("blocksPerLeader", (int)params.nBlocksPerLeader);
    paramsObj.pushKV("leaderTimeoutSeconds", (int)params.nLeaderTimeoutSeconds);
    paramsObj.pushKV("targetBlockTimeMs", (int)params.nTargetBlockTimeMs);
    paramsObj.pushKV("maxBlockGas", (int64_t)params.nMaxBlockGas);
    paramsObj.pushKV("standardChallengePeriod", (int64_t)params.nStandardChallengePeriod);
    paramsObj.pushKV("fastChallengePeriod", (int64_t)params.nFastChallengePeriod);
    paramsObj.pushKV("fastWithdrawalHATThreshold", (int)params.nFastWithdrawalHATThreshold);
    paramsObj.pushKV("consensusThresholdPercent", (int)params.nConsensusThresholdPercent);
    result.pushKV("params", paramsObj);
    
    return result;
}

UniValue l2_listchains(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_listchains\n"
            "\nList all registered L2 chains.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"chainId\": n,      (numeric) L2 chain ID\n"
            "    \"name\": \"xxx\"     (string) Chain name\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_listchains", "")
            + HelpExampleRpc("l2_listchains", "")
        );

    EnsureL2Enabled();

    LOCK(cs_l2);
    
    UniValue result(UniValue::VARR);
    
    // Add default chain if not in registry
    if (g_l2ChainRegistry.empty()) {
        UniValue chainObj(UniValue::VOBJ);
        chainObj.pushKV("chainId", (int64_t)l2::GetL2ChainId());
        chainObj.pushKV("name", "default");
        result.push_back(chainObj);
    }
    
    for (const auto& entry : g_l2ChainRegistry) {
        UniValue chainObj(UniValue::VOBJ);
        chainObj.pushKV("chainId", (int64_t)entry.first);
        chainObj.pushKV("name", entry.second);
        result.push_back(chainObj);
    }
    
    return result;
}


// ============================================================================
// Task 18.3: Sequencer RPC Commands
// Requirements: 2.5, 2.6
// ============================================================================

UniValue l2_announcesequencer(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "l2_announcesequencer stake hatscore ( \"endpoint\" )\n"
            "\nAnnounce this node as an L2 sequencer candidate.\n"
            "\nRequires wallet to be unlocked for signing.\n"
            "\nArguments:\n"
            "1. stake        (numeric, required) Stake amount in CAS\n"
            "2. hatscore     (numeric, required) HAT v2 score (0-100)\n"
            "3. \"endpoint\"   (string, optional) Public endpoint for connectivity\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": bool,         (boolean) Whether announcement succeeded\n"
            "  \"address\": \"xxx\",       (string) Sequencer address\n"
            "  \"stake\": n,              (numeric) Stake amount\n"
            "  \"hatScore\": n,           (numeric) HAT v2 score\n"
            "  \"eligible\": bool,        (boolean) Whether eligible to sequence\n"
            "  \"message\": \"xxx\"        (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_announcesequencer", "100 75")
            + HelpExampleCli("l2_announcesequencer", "100 75 \"192.168.1.1:8333\"")
            + HelpExampleRpc("l2_announcesequencer", "100, 75, \"192.168.1.1:8333\"")
        );

    EnsureL2Enabled();

    // Get wallet for signing
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    CAmount stake = AmountFromValue(request.params[0]);
    uint32_t hatScore = request.params[1].get_int();
    
    std::string endpoint = "";
    if (request.params.size() > 2) {
        endpoint = request.params[2].get_str();
    }
    
    // Validate parameters
    const l2::L2Params& params = l2::GetL2Params();
    
    if (stake < params.nMinSequencerStake) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Stake must be at least %s CAS", FormatMoney(params.nMinSequencerStake)));
    }
    
    if (hatScore < params.nMinSequencerHATScore) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("HAT score must be at least %d", params.nMinSequencerHATScore));
    }
    
    if (hatScore > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "HAT score cannot exceed 100");
    }
    
    // Get a key from wallet for signing
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    
    CKey signingKey;
    if (!pwallet->GetKey(newKey.GetID(), signingKey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Could not get signing key");
    }
    
    // Initialize sequencer discovery if needed
    if (!l2::IsSequencerDiscoveryInitialized()) {
        l2::InitSequencerDiscovery(l2::GetL2ChainId());
    }
    
    l2::SequencerDiscovery& discovery = l2::GetSequencerDiscovery();
    
    bool success = discovery.AnnounceAsSequencer(signingKey, stake, hatScore, endpoint);
    
    uint160 sequencerAddr = discovery.GetLocalSequencerAddress();
    bool eligible = discovery.IsEligibleSequencer(sequencerAddr);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("address", "0x" + sequencerAddr.GetHex());
    result.pushKV("stake", ValueFromAmount(stake));
    result.pushKV("hatScore", (int)hatScore);
    result.pushKV("eligible", eligible);
    
    if (success) {
        result.pushKV("message", "Sequencer announcement broadcast successfully");
    } else {
        result.pushKV("message", "Failed to announce as sequencer");
    }
    
    return result;
}

UniValue l2_getsequencers(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "l2_getsequencers ( eligibleonly )\n"
            "\nGet list of known L2 sequencers.\n"
            "\nArguments:\n"
            "1. eligibleonly    (boolean, optional, default=false) Only show eligible sequencers\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",       (string) Sequencer address\n"
            "    \"stake\": n,              (numeric) Verified stake amount\n"
            "    \"hatScore\": n,           (numeric) Verified HAT v2 score\n"
            "    \"peerCount\": n,          (numeric) Connected peer count\n"
            "    \"endpoint\": \"xxx\",      (string) Public endpoint\n"
            "    \"isVerified\": bool,      (boolean) Whether eligibility is verified\n"
            "    \"isEligible\": bool,      (boolean) Whether currently eligible\n"
            "    \"blocksProduced\": n,     (numeric) Blocks produced\n"
            "    \"blocksMissed\": n,       (numeric) Blocks missed\n"
            "    \"uptimePercent\": n,      (numeric) Uptime percentage\n"
            "    \"weight\": n,             (numeric) Election weight\n"
            "    \"lastAnnouncement\": n    (numeric) Last announcement timestamp\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getsequencers", "")
            + HelpExampleCli("l2_getsequencers", "true")
            + HelpExampleRpc("l2_getsequencers", "true")
        );

    EnsureL2Enabled();

    bool eligibleOnly = false;
    if (request.params.size() > 0) {
        eligibleOnly = request.params[0].get_bool();
    }
    
    if (!l2::IsSequencerDiscoveryInitialized()) {
        return UniValue(UniValue::VARR);  // Empty array if not initialized
    }
    
    l2::SequencerDiscovery& discovery = l2::GetSequencerDiscovery();
    
    std::vector<l2::SequencerInfo> sequencers;
    if (eligibleOnly) {
        sequencers = discovery.GetEligibleSequencers();
    } else {
        sequencers = discovery.GetAllSequencers();
    }
    
    UniValue result(UniValue::VARR);
    
    for (const auto& seq : sequencers) {
        UniValue seqObj(UniValue::VOBJ);
        seqObj.pushKV("address", "0x" + seq.address.GetHex());
        seqObj.pushKV("stake", ValueFromAmount(seq.verifiedStake));
        seqObj.pushKV("hatScore", (int)seq.verifiedHatScore);
        seqObj.pushKV("peerCount", (int)seq.peerCount);
        seqObj.pushKV("endpoint", seq.publicEndpoint);
        seqObj.pushKV("isVerified", seq.isVerified);
        seqObj.pushKV("isEligible", seq.isEligible);
        seqObj.pushKV("blocksProduced", (int64_t)seq.blocksProduced);
        seqObj.pushKV("blocksMissed", (int64_t)seq.blocksMissed);
        seqObj.pushKV("uptimePercent", seq.GetUptimePercent());
        seqObj.pushKV("weight", (int64_t)seq.GetWeight());
        seqObj.pushKV("lastAnnouncement", (int64_t)seq.lastAnnouncement);
        seqObj.pushKV("attestationCount", (int)seq.attestationCount);
        result.push_back(seqObj);
    }
    
    return result;
}

UniValue l2_getleader(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_getleader\n"
            "\nGet information about the current L2 sequencer leader.\n"
            "\nResult:\n"
            "{\n"
            "  \"hasLeader\": bool,         (boolean) Whether there is an active leader\n"
            "  \"address\": \"xxx\",         (string) Leader address\n"
            "  \"slotNumber\": n,           (numeric) Current slot number\n"
            "  \"validUntilBlock\": n,      (numeric) Block height until leader is valid\n"
            "  \"isLocalNode\": bool,       (boolean) Whether this node is the leader\n"
            "  \"backupCount\": n,          (numeric) Number of backup sequencers\n"
            "  \"failoverInProgress\": bool (boolean) Whether failover is in progress\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getleader", "")
            + HelpExampleRpc("l2_getleader", "")
        );

    EnsureL2Enabled();

    UniValue result(UniValue::VOBJ);
    
    if (!l2::IsLeaderElectionInitialized()) {
        result.pushKV("hasLeader", false);
        result.pushKV("message", "Leader election not initialized");
        return result;
    }
    
    l2::LeaderElection& election = l2::GetLeaderElection();
    l2::LeaderElectionResult currentElection = election.GetCurrentElection();
    
    result.pushKV("hasLeader", currentElection.isValid);
    
    if (currentElection.isValid) {
        result.pushKV("address", "0x" + currentElection.leaderAddress.GetHex());
        result.pushKV("slotNumber", (int64_t)currentElection.slotNumber);
        result.pushKV("validUntilBlock", (int64_t)currentElection.validUntilBlock);
        result.pushKV("electionSeed", currentElection.electionSeed.GetHex());
        result.pushKV("electionTimestamp", (int64_t)currentElection.electionTimestamp);
        result.pushKV("isLocalNode", election.IsCurrentLeader());
        result.pushKV("backupCount", (int64_t)currentElection.backupSequencers.size());
        result.pushKV("failoverInProgress", election.IsFailoverInProgress());
        
        // Include backup sequencers
        if (!currentElection.backupSequencers.empty()) {
            UniValue backups(UniValue::VARR);
            for (const auto& backup : currentElection.backupSequencers) {
                backups.push_back("0x" + backup.GetHex());
            }
            result.pushKV("backupSequencers", backups);
        }
    }
    
    return result;
}


// ============================================================================
// Task 8.1: Token Info RPC Commands
// Requirements: 8.1, 8.2, 8.3, 8.4
// ============================================================================

UniValue l2_gettokeninfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_gettokeninfo\n"
            "\nGet information about the L2 token.\n"
            "\nResult:\n"
            "{\n"
            "  \"tokenName\": \"xxx\",       (string) Token name\n"
            "  \"tokenSymbol\": \"xxx\",     (string) Token symbol\n"
            "  \"chainId\": n,              (numeric) L2 chain ID\n"
            "  \"sequencerReward\": \"x.xx\",(string) Sequencer reward per block\n"
            "  \"mintingFee\": \"x.xx\",     (string) Minting fee in CAS\n"
            "  \"minTransferFee\": \"x.xx\", (string) Minimum transfer fee\n"
            "  \"maxGenesisSupply\": \"x.xx\"(string) Maximum genesis supply\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_gettokeninfo", "")
            + HelpExampleRpc("l2_gettokeninfo", "")
        );

    EnsureL2Enabled();

    l2::L2TokenManager& tokenManager = GetL2TokenManager();
    const l2::L2TokenConfig& config = tokenManager.GetConfig();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("tokenName", config.tokenName);
    result.pushKV("tokenSymbol", config.tokenSymbol);
    result.pushKV("chainId", (int64_t)tokenManager.GetChainId());
    result.pushKV("sequencerReward", ValueFromAmount(config.sequencerReward));
    result.pushKV("mintingFee", ValueFromAmount(config.mintingFee));
    result.pushKV("minTransferFee", ValueFromAmount(config.minTransferFee));
    result.pushKV("maxGenesisSupply", ValueFromAmount(config.maxGenesisSupply));
    
    return result;
}

UniValue l2_gettokensupply(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_gettokensupply\n"
            "\nGet the current L2 token supply information.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalSupply\": \"x.xx\",      (string) Total token supply\n"
            "  \"genesisSupply\": \"x.xx\",    (string) Tokens from genesis distribution\n"
            "  \"mintedSupply\": \"x.xx\",     (string) Tokens minted via sequencer rewards\n"
            "  \"burnedSupply\": \"x.xx\",     (string) Tokens burned (fees, etc.)\n"
            "  \"totalBlocksRewarded\": n,    (numeric) Number of blocks that received rewards\n"
            "  \"invariantValid\": bool       (boolean) Whether supply invariant holds\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_gettokensupply", "")
            + HelpExampleRpc("l2_gettokensupply", "")
        );

    EnsureL2Enabled();

    l2::L2TokenManager& tokenManager = GetL2TokenManager();
    const l2::L2TokenSupply& supply = tokenManager.GetSupply();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("totalSupply", ValueFromAmount(supply.totalSupply));
    result.pushKV("genesisSupply", ValueFromAmount(supply.genesisSupply));
    result.pushKV("mintedSupply", ValueFromAmount(supply.mintedSupply));
    result.pushKV("burnedSupply", ValueFromAmount(supply.burnedSupply));
    result.pushKV("totalBlocksRewarded", (int64_t)supply.totalBlocksRewarded);
    result.pushKV("invariantValid", supply.VerifyInvariant());
    
    // Also include token symbol for clarity
    result.pushKV("tokenSymbol", tokenManager.GetTokenSymbol());
    
    return result;
}

UniValue l2_getgenesisdistribution(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_getgenesisdistribution\n"
            "\nGet the genesis token distribution for the L2 chain.\n"
            "\nResult:\n"
            "{\n"
            "  \"applied\": bool,             (boolean) Whether genesis has been applied\n"
            "  \"totalDistributed\": \"x.xx\", (string) Total tokens distributed at genesis\n"
            "  \"recipientCount\": n,         (numeric) Number of recipients\n"
            "  \"distribution\": [            (array) Distribution details\n"
            "    {\n"
            "      \"address\": \"xxx\",       (string) Recipient address\n"
            "      \"amount\": \"x.xx\"        (string) Amount received\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getgenesisdistribution", "")
            + HelpExampleRpc("l2_getgenesisdistribution", "")
        );

    EnsureL2Enabled();

    l2::L2TokenManager& tokenManager = GetL2TokenManager();
    std::vector<std::pair<uint160, CAmount>> distribution = tokenManager.GetGenesisDistribution();
    
    CAmount totalDistributed = 0;
    UniValue distArray(UniValue::VARR);
    
    for (const auto& entry : distribution) {
        UniValue distObj(UniValue::VOBJ);
        distObj.pushKV("address", "0x" + entry.first.GetHex());
        distObj.pushKV("amount", ValueFromAmount(entry.second));
        distArray.push_back(distObj);
        totalDistributed += entry.second;
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("applied", tokenManager.IsGenesisApplied());
    result.pushKV("totalDistributed", ValueFromAmount(totalDistributed));
    result.pushKV("recipientCount", (int64_t)distribution.size());
    result.pushKV("distribution", distArray);
    result.pushKV("tokenSymbol", tokenManager.GetTokenSymbol());
    
    return result;
}

UniValue l2_getmintinghistory(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "l2_getmintinghistory ( fromblock toblock )\n"
            "\nGet the minting history (sequencer rewards) for a block range.\n"
            "\nArguments:\n"
            "1. fromblock    (numeric, optional, default=0) Start block (inclusive)\n"
            "2. toblock      (numeric, optional, default=current) End block (inclusive)\n"
            "\nResult:\n"
            "{\n"
            "  \"fromBlock\": n,              (numeric) Start block\n"
            "  \"toBlock\": n,                (numeric) End block\n"
            "  \"recordCount\": n,            (numeric) Number of minting records\n"
            "  \"totalMinted\": \"x.xx\",      (string) Total tokens minted in range\n"
            "  \"records\": [                 (array) Minting records\n"
            "    {\n"
            "      \"l2BlockNumber\": n,      (numeric) L2 block number\n"
            "      \"l2BlockHash\": \"xxx\",   (string) L2 block hash\n"
            "      \"sequencer\": \"xxx\",     (string) Sequencer address\n"
            "      \"rewardAmount\": \"x.xx\", (string) Reward amount\n"
            "      \"l1TxHash\": \"xxx\",      (string) L1 fee transaction hash\n"
            "      \"l1BlockNumber\": n,      (numeric) L1 block number\n"
            "      \"feePaid\": \"x.xx\",      (string) CAS fee paid on L1\n"
            "      \"timestamp\": n           (numeric) Minting timestamp\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getmintinghistory", "")
            + HelpExampleCli("l2_getmintinghistory", "0 100")
            + HelpExampleRpc("l2_getmintinghistory", "0, 100")
        );

    EnsureL2Enabled();

    uint64_t fromBlock = 0;
    uint64_t toBlock = UINT64_MAX;
    
    if (request.params.size() > 0) {
        fromBlock = request.params[0].get_int64();
    }
    if (request.params.size() > 1) {
        toBlock = request.params[1].get_int64();
    }
    
    l2::L2TokenManager& tokenManager = GetL2TokenManager();
    std::vector<l2::MintingRecord> records = tokenManager.GetMintingHistory(fromBlock, toBlock);
    
    CAmount totalMinted = 0;
    UniValue recordsArray(UniValue::VARR);
    
    for (const auto& record : records) {
        UniValue recordObj(UniValue::VOBJ);
        recordObj.pushKV("l2BlockNumber", (int64_t)record.l2BlockNumber);
        recordObj.pushKV("l2BlockHash", record.l2BlockHash.GetHex());
        recordObj.pushKV("sequencer", "0x" + record.sequencerAddress.GetHex());
        recordObj.pushKV("rewardAmount", ValueFromAmount(record.rewardAmount));
        recordObj.pushKV("l1TxHash", record.l1TxHash.GetHex());
        recordObj.pushKV("l1BlockNumber", (int64_t)record.l1BlockNumber);
        recordObj.pushKV("feePaid", ValueFromAmount(record.feePaid));
        recordObj.pushKV("timestamp", (int64_t)record.timestamp);
        recordsArray.push_back(recordObj);
        totalMinted += record.rewardAmount;
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("fromBlock", (int64_t)fromBlock);
    result.pushKV("toBlock", (int64_t)(toBlock == UINT64_MAX ? 0 : toBlock));
    result.pushKV("recordCount", (int64_t)records.size());
    result.pushKV("totalMinted", ValueFromAmount(totalMinted));
    result.pushKV("records", recordsArray);
    result.pushKV("tokenSymbol", tokenManager.GetTokenSymbol());
    
    return result;
}

UniValue l2_getsequencerrewards(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_getsequencerrewards\n"
            "\nGet total sequencer rewards paid out.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalRewards\": \"x.xx\",     (string) Total rewards paid to sequencers\n"
            "  \"totalBlocksRewarded\": n,    (numeric) Number of blocks that received rewards\n"
            "  \"currentRewardPerBlock\": \"x.xx\", (string) Current reward per block\n"
            "  \"tokenSymbol\": \"xxx\"        (string) Token symbol\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getsequencerrewards", "")
            + HelpExampleRpc("l2_getsequencerrewards", "")
        );

    EnsureL2Enabled();

    l2::L2TokenManager& tokenManager = GetL2TokenManager();
    const l2::L2TokenSupply& supply = tokenManager.GetSupply();
    const l2::L2TokenConfig& config = tokenManager.GetConfig();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("totalRewards", ValueFromAmount(tokenManager.GetTotalSequencerRewards()));
    result.pushKV("totalBlocksRewarded", (int64_t)supply.totalBlocksRewarded);
    result.pushKV("currentRewardPerBlock", ValueFromAmount(config.sequencerReward));
    result.pushKV("tokenSymbol", tokenManager.GetTokenSymbol());
    
    return result;
}


// ============================================================================
// Task 8.3: Transfer RPC Commands
// Requirements: 2.5, 7.3
// ============================================================================

UniValue l2_transfer(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "l2_transfer \"from\" \"to\" amount ( fee )\n"
            "\nTransfer L2 tokens between addresses.\n"
            "\nArguments:\n"
            "1. \"from\"    (string, required) Sender L2 address\n"
            "2. \"to\"      (string, required) Recipient L2 address\n"
            "3. amount     (numeric, required) Amount to transfer\n"
            "4. fee        (numeric, optional) Transfer fee (default: minimum fee)\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": bool,        (boolean) Whether transfer succeeded\n"
            "  \"txHash\": \"xxx\",       (string) Transaction hash\n"
            "  \"from\": \"xxx\",         (string) Sender address\n"
            "  \"to\": \"xxx\",           (string) Recipient address\n"
            "  \"amount\": \"x.xx\",      (string) Amount transferred\n"
            "  \"fee\": \"x.xx\",         (string) Fee paid\n"
            "  \"newStateRoot\": \"xxx\", (string) New state root after transfer\n"
            "  \"message\": \"xxx\"       (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_transfer", "\"0xa1b2c3...\" \"0xd4e5f6...\" 10")
            + HelpExampleCli("l2_transfer", "\"0xa1b2c3...\" \"0xd4e5f6...\" 10 0.001")
            + HelpExampleRpc("l2_transfer", "\"0xa1b2c3...\", \"0xd4e5f6...\", 10")
        );

    EnsureL2Enabled();

    std::string fromStr = request.params[0].get_str();
    std::string toStr = request.params[1].get_str();
    uint160 from = ParseL2Address(fromStr);
    uint160 to = ParseL2Address(toStr);
    
    CAmount amount = AmountFromValue(request.params[2]);
    
    l2::L2TokenManager& tokenManager = GetL2TokenManager();
    const l2::L2TokenConfig& config = tokenManager.GetConfig();
    
    // Validate amount
    if (amount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }
    // Note: the optional fee parameter is accepted for backward compatibility
    // but not charged in the current phase (transfers use gasPrice 0).
    
    l2::L2StateManager& stateManager = GetL2StateManager();
    
    // Optional fee (in CAS), charged to the sender and credited to the
    // sequencer. The fee is expressed on-chain as gasPrice * gasLimit(21000),
    // so the effective fee is floored to a multiple of the gas limit.
    CAmount feeReq = 0;
    if (request.params.size() > 3) {
        feeReq = AmountFromValue(request.params[3]);
        if (feeReq < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee cannot be negative");
        }
    }
    CAmount gasPrice = (feeReq > 0) ? (feeReq / (CAmount)l2::MIN_TX_GAS_LIMIT) : 0;
    CAmount effectiveFee = gasPrice * (CAmount)l2::MIN_TX_GAS_LIMIT;
    
    // Soft balance check for immediate feedback (amount + fee). The
    // authoritative check is done when the tx is applied in an L2 block.
    l2::AccountState fromState = stateManager.GetAccountState(from);
    if (fromState.balance < amount + effectiveFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient L2 balance: have %s, need %s",
                      FormatMoney(fromState.balance), FormatMoney(amount + effectiveFee)));
    }
    
    // Build a native L2 transfer transaction and enqueue it for the sequencer.
    // The nonce accounts for transfers from this sender already pending in the
    // pool so several queued transfers get consecutive nonces.
    uint64_t nonce = fromState.nonce + l2::GetPendingCountFromSender(from);
    l2::L2Transaction tx = l2::CreateTransferTx(from, to, amount, nonce,
                                                gasPrice, l2::GetL2ChainId());
    
    // Authenticate the transfer: sign with the sender's private key from the
    // wallet. The L2 address is the Hash160 of the sender's public key (== its
    // CKeyID), so the wallet must control that key to authorize the transfer.
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "A wallet is required to sign L2 transfers");
    }
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        EnsureWalletIsUnlocked(pwallet);
        CKey key;
        CKeyID keyid(from);
        if (!pwallet->GetKey(keyid, key)) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                "No private key for the sender L2 address in this wallet (cannot authorize transfer)");
        }
        if (!tx.Sign(key)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign L2 transaction");
        }
    }
    
    std::string err;
    if (!l2::SubmitL2Transaction(tx, err)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "L2 transaction rejected: " + err);
    }
    // Broadcast to NODE_L2 peers so a (possibly remote) sequencer can include it.
    l2::BroadcastL2Transaction(tx);
    
    UniValue response(UniValue::VOBJ);
    response.pushKV("success", true);
    response.pushKV("txHash", tx.GetHash().GetHex());
    response.pushKV("from", "0x" + from.GetHex());
    response.pushKV("to", "0x" + to.GetHex());
    response.pushKV("amount", ValueFromAmount(amount));
    response.pushKV("fee", ValueFromAmount(effectiveFee));
    response.pushKV("nonce", (int64_t)nonce);
    response.pushKV("status", "pending");
    response.pushKV("tokenSymbol", config.tokenSymbol);
    response.pushKV("rawtx", HexStr(tx.Serialize()));
    response.pushKV("message", "Transfer queued; will be included in the next L2 block");
    return response;
}

UniValue l2_gettransfer(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_gettransfer \"txhash\"\n"
            "\nGet the status of a transfer transaction.\n"
            "\nArguments:\n"
            "1. \"txhash\"    (string, required) Transaction hash\n"
            "\nResult:\n"
            "{\n"
            "  \"found\": bool,          (boolean) Whether transaction was found\n"
            "  \"txHash\": \"xxx\",       (string) Transaction hash\n"
            "  \"status\": \"xxx\",       (string) Transaction status\n"
            "  \"message\": \"xxx\"       (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_gettransfer", "\"abc123...\"")
            + HelpExampleRpc("l2_gettransfer", "\"abc123...\"")
        );

    EnsureL2Enabled();

    uint256 txHash = ParseHashV(request.params[0], "txhash");
    
    // For now, return a simple response
    // Full implementation would query transaction storage
    UniValue result(UniValue::VOBJ);
    result.pushKV("found", false);
    result.pushKV("txHash", txHash.GetHex());
    result.pushKV("status", "unknown");
    result.pushKV("message", "Use l2_getblockbynumber to find an included transfer");
    
    return result;
}

UniValue l2_sendtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_sendtransaction \"hexstring\"\n"
            "\nSubmit a raw (serialized) L2 transaction to the pending pool.\n"
            "\nThe transaction is included by the sequencer in the next L2 block.\n"
            "\nArguments:\n"
            "1. \"hexstring\"   (string, required) Hex-encoded serialized L2Transaction\n"
            "\nResult:\n"
            "{\n"
            "  \"txHash\": \"xxx\",     (string) L2 transaction hash\n"
            "  \"from\": \"xxx\",       (string) Sender address\n"
            "  \"to\": \"xxx\",         (string) Recipient address\n"
            "  \"value\": n,           (numeric) Value\n"
            "  \"nonce\": n,           (numeric) Nonce\n"
            "  \"status\": \"pending\"  (string) Pool status\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_sendtransaction", "\"0100...\"")
            + HelpExampleRpc("l2_sendtransaction", "\"0100...\"")
        );

    EnsureL2Enabled();

    if (!request.params[0].isStr() || !IsHex(request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "hexstring must be a hex string");
    }
    std::vector<unsigned char> data = ParseHex(request.params[0].get_str());
    l2::L2Transaction tx;
    if (!tx.Deserialize(data)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to decode L2 transaction");
    }

    std::string err;
    if (!l2::SubmitL2Transaction(tx, err)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "L2 transaction rejected: " + err);
    }
    // Broadcast to NODE_L2 peers so a (possibly remote) sequencer can include it.
    l2::BroadcastL2Transaction(tx);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txHash", tx.GetHash().GetHex());
    result.pushKV("from", "0x" + tx.from.GetHex());
    result.pushKV("to", "0x" + tx.to.GetHex());
    result.pushKV("value", ValueFromAmount(tx.value));
    result.pushKV("nonce", (int64_t)tx.nonce);
    result.pushKV("status", "pending");
    result.pushKV("message", "Transaction queued; will be included in the next L2 block");
    return result;
}


// ============================================================================
// L1 anchoring RPCs (Phase 1e): L2COMMIT commitments
// ============================================================================

UniValue l2_createcommitment(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "l2_createcommitment ( blocknumber \"staterootoverride\" )\n"
            "\nBuild an unsigned L1 transaction that anchors an L2 state-root\n"
            "commitment (OP_RETURN \"L2COMMIT\") for the given L2 block.\n"
            "Fund it with fundrawtransaction, sign it with signrawtransaction,\n"
            "then broadcast with sendrawtransaction.\n"
            "\nArguments:\n"
            "1. blocknumber        (numeric, optional) L2 block to commit (default: latest)\n"
            "2. staterootoverride  (string, optional) TEST ONLY: commit this state root\n"
            "                       instead of the block's real one (to simulate fraud)\n"
            "\nResult:\n"
            "{\n"
            "  \"hex\": \"xxx\",           (string) Unsigned raw tx with the commitment output\n"
            "  \"l2BlockNumber\": n,      (numeric) Committed L2 block number\n"
            "  \"stateRoot\": \"xxx\",     (string) Committed L2 state root\n"
            "  \"commitScript\": \"xxx\"   (string) The OP_RETURN script (hex)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_createcommitment", "")
            + HelpExampleCli("l2_createcommitment", "5")
        );

    EnsureL2Enabled();

    size_t blockCount = l2::GetL2BlockCount();
    if (blockCount == 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "No L2 blocks have been produced yet");
    }

    uint64_t blockNumber = blockCount - 1;  // latest
    if (request.params.size() > 0) {
        blockNumber = request.params[0].get_int64();
    }

    l2::L2Block block;
    if (!l2::GetL2BlockByNumber(blockNumber, block)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "L2 block not found");
    }

    uint256 committedRoot = block.header.stateRoot;
    if (request.params.size() > 1) {
        // TEST ONLY: allow committing an arbitrary (possibly wrong) state root
        // so a fraud proof can be demonstrated.
        committedRoot.SetHex(request.params[1].get_str());
    }

    uint32_t chainId = static_cast<uint32_t>(l2::GetL2ChainId());
    CScript commitScript = l2::BuildL2CommitScript(chainId, blockNumber, committedRoot);

    // Build a raw transaction with a single 0-value OP_RETURN output. The caller
    // funds it (adds inputs + change + fee), signs, and broadcasts.
    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, commitScript));

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(rawTx));
    result.pushKV("l2BlockNumber", (int64_t)blockNumber);
    result.pushKV("stateRoot", committedRoot.GetHex());
    result.pushKV("honestStateRoot", block.header.stateRoot.GetHex());
    result.pushKV("commitScript", HexStr(commitScript.begin(), commitScript.end()));
    result.pushKV("message", "Fund with fundrawtransaction, sign with signrawtransaction, then sendrawtransaction");
    return result;
}

UniValue l2_getcommitment(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "l2_getcommitment ( blocknumber )\n"
            "\nGet the L1-anchored state-root commitment for an L2 block.\n"
            "\nArguments:\n"
            "1. blocknumber   (numeric, optional) L2 block number (default: latest committed)\n"
            "\nResult:\n"
            "{\n"
            "  \"found\": bool,\n"
            "  \"l2BlockNumber\": n,\n"
            "  \"stateRoot\": \"xxx\",\n"
            "  \"l1Height\": n,\n"
            "  \"l1TxHash\": \"xxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getcommitment", "")
            + HelpExampleCli("l2_getcommitment", "5")
        );

    EnsureL2Enabled();

    l2::L2Commitment c;
    bool found = false;
    if (request.params.size() > 0) {
        found = l2::GetL2Commitment(request.params[0].get_int64(), c);
    } else {
        found = l2::GetLatestL2Commitment(c);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("found", found);
    if (found) {
        result.pushKV("l2BlockNumber", (int64_t)c.l2BlockNumber);
        result.pushKV("stateRoot", c.stateRoot.GetHex());
        result.pushKV("l1Height", (int64_t)c.l1Height);
        result.pushKV("l1TxHash", c.l1TxHash.GetHex());
        // Challenge-window / finalization status.
        l2::FraudProofSystem& fps = l2::GetGlobalFraudProofSystem();
        uint64_t now = (uint64_t)GetTime();
        result.pushKV("challengeDeadline", (int64_t)fps.GetChallengeDeadline(c.stateRoot));
        result.pushKV("finalized", fps.IsStateRootFinalized(c.stateRoot, now));
    } else {
        result.pushKV("message", "No commitment recorded");
    }
    return result;
}

UniValue l2_registersequencer(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "l2_registersequencer stake ( \"action\" )\n"
            "\nRegister (or deregister) THIS node as an L2 sequencer on-chain.\n"
            "Broadcasts an L1 transaction with an L2SEQREG OP_RETURN output whose\n"
            "value is CAS burned as stake (Sybil resistance; never returned).\n"
            "The node's sequencer (validator) key authorises the action.\n"
            "\nArguments:\n"
            "1. stake     (numeric) CAS to burn as stake (ignored for deregister)\n"
            "2. action    (string, optional) \"register\" (default) or \"deregister\"\n"
            "\nResult:\n"
            "{ \"txid\": \"xxx\", \"address\": \"0x..\", \"stake\": n, \"action\": \"register\" }\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_registersequencer", "10")
            + HelpExampleCli("l2_registersequencer", "0 \"deregister\"")
        );

    EnsureL2Enabled();

    if (!CVM::g_validatorKeys || !CVM::g_validatorKeys->HasValidatorKey()) {
        throw JSONRPCError(RPC_MISC_ERROR,
            "This node has no sequencer key (start with -l2sequencer=1)");
    }

    l2::L2SeqRegAction action = l2::L2SeqRegAction::REGISTER;
    if (request.params.size() > 1) {
        std::string a = request.params[1].get_str();
        if (a == "deregister") action = l2::L2SeqRegAction::DEREGISTER;
        else if (a != "register") throw JSONRPCError(RPC_INVALID_PARAMETER, "action must be register or deregister");
    }

    CAmount stake = 0;
    if (action == l2::L2SeqRegAction::REGISTER) {
        stake = AmountFromValue(request.params[0]);
        CAmount minStake = gArgs.GetArg("-l2minsequencerstake", 1);
        if (stake < minStake) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("Stake below minimum (%s)", FormatMoney(minStake)));
        }
    }

    const uint32_t chainId = static_cast<uint32_t>(l2::GetL2ChainId());
    const uint160 address = CVM::g_validatorKeys->GetValidatorAddress();
    const CPubKey pubkey = CVM::g_validatorKeys->GetValidatorPubKey();

    const uint256 sighash = l2::GetL2SeqRegSigHash(chainId, action, address);
    std::vector<unsigned char> sig;
    if (!CVM::g_validatorKeys->SignCompact(sighash, sig)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to sign registration");
    }

    const CScript regScript = l2::BuildL2SeqRegScript(chainId, action, address, pubkey, sig);

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "A wallet is required to fund the registration");
    }

    CWalletTx wtxNew;
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        EnsureWalletIsUnlocked(pwallet);
        std::vector<CRecipient> vecSend;
        // The OP_RETURN output carries the staked (burned) value.
        vecSend.push_back({regScript, stake, false});
        CReserveKey reservekey(pwallet);
        CAmount nFeeRequired = 0;
        int nChangePosRet = -1;
        std::string strError;
        CCoinControl coin_control;
        if (!pwallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired,
                                        nChangePosRet, strError, coin_control)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
        CValidationState state;
        if (!pwallet->CommitTransaction(wtxNew, reservekey, g_connman.get(), state)) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                "Registration tx rejected: " + state.GetRejectReason());
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtxNew.GetHash().GetHex());
    result.pushKV("address", "0x" + address.GetHex());
    result.pushKV("stake", ValueFromAmount(stake));
    result.pushKV("action", action == l2::L2SeqRegAction::REGISTER ? "register" : "deregister");
    result.pushKV("message", "Broadcast; becomes effective once confirmed on L1");
    return result;
}

UniValue l2_listsequencers(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "l2_listsequencers\n"
            "\nList the active on-chain-registered L2 sequencers.\n"
            "\nResult:\n"
            "[ { \"address\": \"0x..\", \"stake\": n, \"active\": bool,\n"
            "    \"excluded\": bool, \"l1Height\": n } , ... ]\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_listsequencers", "")
        );

    EnsureL2Enabled();

    std::vector<l2::SequencerRegistration> seqs = l2::GetActiveSequencers();
    UniValue arr(UniValue::VARR);
    for (const auto& s : seqs) {
        UniValue o(UniValue::VOBJ);
        o.pushKV("address", "0x" + s.address.GetHex());
        o.pushKV("stake", ValueFromAmount(s.stake));
        o.pushKV("active", s.active);
        o.pushKV("excluded", s.excluded);
        o.pushKV("l1Height", (int64_t)s.l1Height);
        arr.push_back(o);
    }
    return arr;
}

UniValue l2_getdatastatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "l2_getdatastatus\n"
            "\nReport the L2 data-availability posting status of this node.\n"
            "\nResult:\n"
            "{\n"
            "  \"l2Tip\": n,          (numeric) Highest local L2 block number\n"
            "  \"nextDataPost\": n,   (numeric) Next L2 block whose data will be posted to L1\n"
            "  \"dataComplete\": bool (boolean) True if data for every block up to the tip is posted\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getdatastatus", "")
        );

    EnsureL2Enabled();

    size_t blockCount = l2::GetL2BlockCount();
    int64_t tip = blockCount == 0 ? -1 : (int64_t)(blockCount - 1);
    uint64_t next = l2::GetNextL2DataPostBlock();

    UniValue result(UniValue::VOBJ);
    result.pushKV("l2Tip", tip);
    result.pushKV("nextDataPost", (int64_t)next);
    result.pushKV("dataComplete", tip < 0 || (int64_t)next > tip);
    return result;
}

UniValue l2_submitfraudproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "l2_submitfraudproof blocknumber ( \"challengeraddress\" )\n"
            "\nChallenge the L1-anchored state-root commitment for an L2 block.\n"
            "The node compares the committed state root (from L1) against the\n"
            "honest state root it computed for that block. If they differ, the\n"
            "fraud proof is valid and the sequencer is slashed.\n"
            "\nArguments:\n"
            "1. blocknumber         (numeric, required) Committed L2 block number to challenge\n"
            "2. challengeraddress   (string, optional) Challenger L2 address (0x...)\n"
            "\nResult:\n"
            "{\n"
            "  \"blockNumber\": n,\n"
            "  \"committedStateRoot\": \"xxx\",   (from L1)\n"
            "  \"honestStateRoot\": \"xxx\",      (recomputed locally)\n"
            "  \"fraudProven\": bool,\n"
            "  \"sequencer\": \"xxx\",\n"
            "  \"slashedAmount\": n,             (if fraud proven)\n"
            "  \"challengerReward\": n           (if fraud proven)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_submitfraudproof", "1")
            + HelpExampleCli("l2_submitfraudproof", "1 \"0xabc...\"")
        );

    EnsureL2Enabled();

    uint64_t blockNumber = request.params[0].get_int64();

    // The committed (claimed) state root must be anchored on L1.
    l2::L2Commitment commit;
    if (!l2::GetL2Commitment(blockNumber, commit)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "No L1 commitment recorded for that L2 block (nothing to challenge)");
    }

    // The honest state root for that block. This node derived it by INDEPENDENT
    // re-execution: IngestL2Block re-applies every block's transactions and
    // rejects any block whose recomputed state root does not equal the header's,
    // and the producer computes the root from real state. Minted-account state is
    // now path-independent (mint lastActivity is stamped with the burn's L1
    // height), so this root is identical on every honest full node.
    l2::L2Block block;
    if (!l2::GetL2BlockByNumber(blockNumber, block)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "L2 block not found locally");
    }
    const uint256 honestRoot = block.header.stateRoot;

    // Data availability: a fraud proof is only trustless if the disputed block's
    // data is on L1, so any node can independently re-derive the honest root.
    const bool dataAvailable = (l2::GetNextL2DataPostBlock() > blockNumber);

    // Previous block's state root (proof structure requires it).
    uint256 prevRoot;
    if (blockNumber > 0) {
        l2::L2Block prev;
        if (l2::GetL2BlockByNumber(blockNumber - 1, prev)) {
            prevRoot = prev.header.stateRoot;
        }
    }
    if (prevRoot.IsNull()) prevRoot = honestRoot;  // fallback so structure validates

    uint160 challenger;
    if (request.params.size() > 1) {
        challenger = ParseL2Address(request.params[1].get_str());
    } else {
        challenger.SetHex("1111111111111111111111111111111111111111");
    }

    l2::FraudProofSystem& fps = l2::GetGlobalFraudProofSystem();
    uint64_t now = (uint64_t)GetTime();

    l2::FraudProof proof;
    proof.type = l2::FraudProofType::INVALID_STATE_TRANSITION;
    proof.disputedStateRoot = commit.stateRoot;
    proof.disputedBlockNumber = blockNumber;
    proof.previousStateRoot = prevRoot;
    proof.l2ChainId = l2::GetL2ChainId();
    proof.challengerAddress = challenger;
    proof.challengeBond = l2::FRAUD_PROOF_CHALLENGE_BOND;
    proof.submittedAt = now;
    proof.sequencerAddress = block.header.sequencer;

    fps.SubmitFraudProof(proof, now);

    // Fraud condition: the L1-committed root differs from the honest root.
    bool fraudProven = (commit.stateRoot != honestRoot);

    UniValue result(UniValue::VOBJ);
    result.pushKV("blockNumber", (int64_t)blockNumber);
    result.pushKV("committedStateRoot", commit.stateRoot.GetHex());
    result.pushKV("honestStateRoot", honestRoot.GetHex());
    result.pushKV("fraudProven", fraudProven);
    result.pushKV("dataAvailable", dataAvailable);
    result.pushKV("sequencer", "0x" + block.header.sequencer.GetHex());

    if (fraudProven) {
        l2::SlashingRecord rec = fps.SlashSequencer(block.header.sequencer, proof, now);
        result.pushKV("slashedAmount", ValueFromAmount(rec.slashedAmount));
        result.pushKV("challengerReward", ValueFromAmount(rec.challengerReward));
        // Exclusion-model consequence (M4): a sequencer that anchored a state
        // root differing from the independently re-executed honest root is
        // removed from the active set on-chain. It can no longer produce or
        // finalize blocks (and its burned stake is gone - burn-only, no payout).
        l2::ExcludeSequencer(block.header.sequencer,
            strprintf("fraudulent state-root commitment at L2 block %llu",
                      (unsigned long long)blockNumber));
        result.pushKV("sequencerExcluded", true);
        result.pushKV("message", "Fraud proven: committed state root does not match "
                                 "independent re-execution; sequencer slashed and excluded");
    } else {
        result.pushKV("sequencerExcluded", false);
        result.pushKV("message", "No fraud: committed state root matches the honest re-execution");
    }
    return result;
}

UniValue l2_createforcedtx(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "l2_createforcedtx \"from\" \"to\" amount ( nonce )\n"
            "\nBuild an unsigned L1 transaction that forces an L2 transfer via an\n"
            "OP_RETURN \"L2FORCE\" output (censorship resistance). Fund it with\n"
            "fundrawtransaction, sign with signrawtransaction, then sendrawtransaction.\n"
            "Every node queues the transfer into the L2 pool on block connect, so\n"
            "the sequencer cannot censor it.\n"
            "\nArguments:\n"
            "1. \"from\"    (string, required) Sender L2 address (0x...)\n"
            "2. \"to\"      (string, required) Recipient L2 address (0x...)\n"
            "3. amount     (numeric, required) Amount to transfer\n"
            "4. nonce      (numeric, optional) Sender nonce (default: current account nonce)\n"
            "\nResult:\n"
            "{\n"
            "  \"hex\": \"xxx\",        (string) Unsigned raw tx with the L2FORCE output\n"
            "  \"from\": \"xxx\", \"to\": \"xxx\", \"amount\": n, \"nonce\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_createforcedtx", "\"0xabc...\" \"0xdef...\" 5")
        );

    EnsureL2Enabled();

    uint160 from = ParseL2Address(request.params[0].get_str());
    uint160 to = ParseL2Address(request.params[1].get_str());
    CAmount amount = AmountFromValue(request.params[2]);
    if (amount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    uint64_t nonce;
    if (request.params.size() > 3) {
        nonce = request.params[3].get_int64();
    } else {
        nonce = l2::GetGlobalStateManager().GetAccountState(from).nonce
                + l2::GetPendingCountFromSender(from);
    }

    // Build and SIGN the forced transfer with the sender's wallet key. The
    // forced tx is authenticated by its own signature; the L1 posting is only
    // the censorship-resistant delivery channel.
    l2::L2Transaction ftx = l2::CreateTransferTx(from, to, amount, nonce,
                                                 /*gasPrice=*/0, l2::GetL2ChainId());
    {
        CWallet* pwallet = GetWalletForJSONRPCRequest(request);
        if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "A wallet is required to sign the forced transfer");
        }
        LOCK2(cs_main, pwallet->cs_wallet);
        EnsureWalletIsUnlocked(pwallet);
        CKey key;
        CKeyID keyid(from);
        if (!pwallet->GetKey(keyid, key)) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                "No private key for the sender L2 address in this wallet");
        }
        if (!ftx.Sign(key)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign forced L2 transaction");
        }
    }

    CScript forceScript = l2::BuildL2ForceScript(ftx);
    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, forceScript));

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(rawTx));
    result.pushKV("from", "0x" + from.GetHex());
    result.pushKV("to", "0x" + to.GetHex());
    result.pushKV("amount", ValueFromAmount(amount));
    result.pushKV("nonce", (int64_t)nonce);
    result.pushKV("forceScript", HexStr(forceScript.begin(), forceScript.end()));
    result.pushKV("message", "Fund with fundrawtransaction, sign, then sendrawtransaction to force inclusion");
    return result;
}


// ============================================================================
// Task 8.4: Faucet RPC Commands
// Requirements: 5.1, 5.5
// ============================================================================

UniValue l2_faucet(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "l2_faucet \"address\" ( amount )\n"
            "\nRequest test tokens from the L2 faucet (testnet/regtest only).\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) L2 recipient address\n"
            "2. amount        (numeric, optional, default=100) Amount to request (max 100)\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": bool,        (boolean) Whether request succeeded\n"
            "  \"txHash\": \"xxx\",       (string) Transaction hash\n"
            "  \"recipient\": \"xxx\",    (string) Recipient address\n"
            "  \"amount\": \"x.xx\",      (string) Amount distributed\n"
            "  \"tokenSymbol\": \"xxx\",  (string) Token symbol\n"
            "  \"message\": \"xxx\"       (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_faucet", "\"0xa1b2c3...\"")
            + HelpExampleCli("l2_faucet", "\"0xa1b2c3...\" 50")
            + HelpExampleRpc("l2_faucet", "\"0xa1b2c3...\", 50")
        );

    EnsureL2Enabled();

    // Check if faucet is enabled (testnet/regtest only)
    if (!l2::L2Faucet::IsEnabled()) {
        throw JSONRPCError(RPC_MISC_ERROR, 
            "Faucet is only available on testnet/regtest. "
            "On mainnet, obtain tokens through sequencer rewards or transfers.");
    }

    std::string addressStr = request.params[0].get_str();
    uint160 recipient = ParseL2Address(addressStr);
    
    CAmount requestedAmount = l2::MAX_FAUCET_AMOUNT;
    if (request.params.size() > 1) {
        requestedAmount = AmountFromValue(request.params[1]);
    }
    
    l2::L2Faucet& faucet = GetL2Faucet();
    l2::L2StateManager& stateManager = GetL2StateManager();
    
    l2::FaucetResult result = faucet.RequestTokens(recipient, requestedAmount, stateManager);
    
    UniValue response(UniValue::VOBJ);
    response.pushKV("success", result.success);
    
    if (result.success) {
        response.pushKV("txHash", result.txHash.GetHex());
        response.pushKV("recipient", "0x" + recipient.GetHex());
        response.pushKV("amount", ValueFromAmount(result.amount));
        response.pushKV("tokenSymbol", faucet.GetTokenManager().GetTokenSymbol());
        response.pushKV("message", "Test tokens distributed successfully");
        response.pushKV("note", "These are test tokens with no real value");
    } else {
        response.pushKV("error", result.error);
        if (result.cooldownRemaining > 0) {
            response.pushKV("cooldownRemaining", (int64_t)result.cooldownRemaining);
            response.pushKV("cooldownMinutes", (int64_t)(result.cooldownRemaining / 60));
        }
        response.pushKV("message", result.error);
    }
    
    return response;
}

UniValue l2_getfaucetstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "l2_getfaucetstatus ( \"address\" )\n"
            "\nGet the status of the L2 faucet.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, optional) Check cooldown for specific address\n"
            "\nResult:\n"
            "{\n"
            "  \"enabled\": bool,            (boolean) Whether faucet is enabled\n"
            "  \"network\": \"xxx\",          (string) Current network\n"
            "  \"maxAmount\": \"x.xx\",       (string) Maximum tokens per request\n"
            "  \"cooldownSeconds\": n,       (numeric) Cooldown period in seconds\n"
            "  \"totalDistributed\": \"x.xx\",(string) Total tokens distributed\n"
            "  \"uniqueRecipients\": n,      (numeric) Number of unique recipients\n"
            "  \"tokenSymbol\": \"xxx\",      (string) Token symbol\n"
            "  \"canRequest\": bool,         (boolean) Whether address can request (if provided)\n"
            "  \"cooldownRemaining\": n      (numeric) Seconds until can request (if provided)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getfaucetstatus", "")
            + HelpExampleCli("l2_getfaucetstatus", "\"0xa1b2c3...\"")
            + HelpExampleRpc("l2_getfaucetstatus", "\"0xa1b2c3...\"")
        );

    EnsureL2Enabled();

    bool enabled = l2::L2Faucet::IsEnabled();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("enabled", enabled);
    result.pushKV("network", enabled ? "testnet/regtest" : "mainnet");
    result.pushKV("maxAmount", ValueFromAmount(l2::MAX_FAUCET_AMOUNT));
    result.pushKV("cooldownSeconds", (int64_t)l2::COOLDOWN_SECONDS);
    
    if (enabled) {
        l2::L2Faucet& faucet = GetL2Faucet();
        result.pushKV("totalDistributed", ValueFromAmount(faucet.GetTotalDistributed()));
        result.pushKV("uniqueRecipients", (int64_t)faucet.GetUniqueRecipientCount());
        result.pushKV("tokenSymbol", faucet.GetTokenManager().GetTokenSymbol());
        
        // Check specific address if provided
        if (request.params.size() > 0) {
            std::string addressStr = request.params[0].get_str();
            uint160 address = ParseL2Address(addressStr);
            
            result.pushKV("address", "0x" + address.GetHex());
            result.pushKV("canRequest", faucet.CanRequest(address));
            result.pushKV("cooldownRemaining", (int64_t)faucet.GetCooldownRemaining(address));
        }
    } else {
        result.pushKV("message", "Faucet is disabled on mainnet");
    }
    
    return result;
}


// ============================================================================
// Task 12: Legacy Bridge RPC Commands REMOVED
// Requirements: 11.1, 11.4 - l2_deposit and l2_withdraw have been completely removed
// The new burn-and-mint model uses l2_createburntx, l2_sendburntx, etc. from l2_burn.cpp
// ============================================================================

UniValue l2_getwithdrawalstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_getwithdrawalstatus \"withdrawalid\"\n"
            "\nGet the status of a withdrawal request.\n"
            "\nArguments:\n"
            "1. \"withdrawalid\"    (string, required) Withdrawal identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"found\": bool,              (boolean) Whether withdrawal was found\n"
            "  \"withdrawalId\": \"xxx\",     (string) Withdrawal identifier\n"
            "  \"l2Sender\": \"xxx\",         (string) L2 sender address\n"
            "  \"l1Recipient\": \"xxx\",      (string) L1 recipient address\n"
            "  \"amount\": n,                (numeric) Withdrawal amount\n"
            "  \"status\": \"xxx\",           (string) Current status\n"
            "  \"l2BlockNumber\": n,         (numeric) L2 block where initiated\n"
            "  \"stateRoot\": \"xxx\",        (string) State root at withdrawal\n"
            "  \"initiatedAt\": n,           (numeric) Initiation timestamp\n"
            "  \"challengeDeadline\": n,     (numeric) Challenge period end\n"
            "  \"isFastWithdrawal\": bool,   (boolean) Whether fast withdrawal\n"
            "  \"canFinalize\": bool,        (boolean) Whether can be finalized now\n"
            "  \"timeRemaining\": n          (numeric) Seconds until can finalize\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getwithdrawalstatus", "\"abc123...\"")
            + HelpExampleRpc("l2_getwithdrawalstatus", "\"abc123...\"")
        );

    EnsureL2Enabled();

    uint256 withdrawalId = ParseHashV(request.params[0], "withdrawalid");
    
    l2::BridgeContract& bridge = GetBridgeContract();
    
    std::optional<l2::WithdrawalRequest> withdrawal = bridge.GetWithdrawal(withdrawalId);
    
    UniValue result(UniValue::VOBJ);
    
    if (!withdrawal.has_value()) {
        result.pushKV("found", false);
        result.pushKV("withdrawalId", withdrawalId.GetHex());
        result.pushKV("message", "Withdrawal not found");
        return result;
    }
    
    const l2::WithdrawalRequest& w = withdrawal.value();
    
    uint64_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    result.pushKV("found", true);
    result.pushKV("withdrawalId", w.withdrawalId.GetHex());
    result.pushKV("l2Sender", "0x" + w.l2Sender.GetHex());
    result.pushKV("l1Recipient", "0x" + w.l1Recipient.GetHex());
    result.pushKV("amount", ValueFromAmount(w.amount));
    result.pushKV("status", l2::WithdrawalStatusToString(w.status));
    result.pushKV("l2BlockNumber", (int64_t)w.l2BlockNumber);
    result.pushKV("stateRoot", w.stateRoot.GetHex());
    result.pushKV("initiatedAt", (int64_t)w.initiatedAt);
    result.pushKV("challengeDeadline", (int64_t)w.challengeDeadline);
    result.pushKV("isFastWithdrawal", w.isFastWithdrawal);
    result.pushKV("hatScore", (int)w.hatScore);
    
    bool canFinalize = w.CanFinalize(currentTime);
    result.pushKV("canFinalize", canFinalize);
    
    if (currentTime < w.challengeDeadline) {
        result.pushKV("timeRemaining", (int64_t)(w.challengeDeadline - currentTime));
    } else {
        result.pushKV("timeRemaining", 0);
    }
    
    // Include challenger info if challenged
    if (w.status == l2::WithdrawalStatus::CHALLENGED) {
        result.pushKV("challenger", "0x" + w.challenger.GetHex());
        result.pushKV("challengeBond", ValueFromAmount(w.challengeBond));
    }
    
    return result;
}


// ============================================================================
// Task 21.1: L2 Registry RPC Commands
// Requirements: 1.1, 1.2, 1.3, 1.4, 1.5
// ============================================================================

UniValue l2_registerchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 7)
        throw std::runtime_error(
            "l2_registerchain \"name\" stake ( blocktime gaslimit challengeperiod minseqstake minseqhatscore )\n"
            "\nRegister a new L2 chain in the global registry.\n"
            "\nRequires wallet to be unlocked for signing.\n"
            "\nArguments:\n"
            "1. \"name\"           (string, required) Name for the L2 chain\n"
            "2. stake             (numeric, required) Deployer stake in CAS (min 1000)\n"
            "3. blocktime         (numeric, optional, default=500) Target block time in ms\n"
            "4. gaslimit          (numeric, optional, default=30000000) Max gas per block\n"
            "5. challengeperiod   (numeric, optional, default=604800) Challenge period in seconds\n"
            "6. minseqstake       (numeric, optional, default=100) Min sequencer stake in CAS\n"
            "7. minseqhatscore    (numeric, optional, default=70) Min sequencer HAT score\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": bool,           (boolean) Whether registration succeeded\n"
            "  \"chainId\": n,              (numeric) Unique L2 chain ID\n"
            "  \"name\": \"xxx\",            (string) Chain name\n"
            "  \"deployer\": \"xxx\",        (string) Deployer address\n"
            "  \"stake\": n,                (numeric) Deployer stake\n"
            "  \"status\": \"xxx\",          (string) Chain status\n"
            "  \"params\": {...},           (object) Deployment parameters\n"
            "  \"message\": \"xxx\"          (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_registerchain", "\"MyL2Chain\" 1000")
            + HelpExampleCli("l2_registerchain", "\"MyL2Chain\" 1000 500 30000000 604800 100 70")
            + HelpExampleRpc("l2_registerchain", "\"MyL2Chain\", 1000")
        );

    EnsureL2Enabled();

    // Get wallet for signing
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    std::string chainName = request.params[0].get_str();
    CAmount stake = AmountFromValue(request.params[1]);
    
    // Build deployment parameters
    l2::L2DeploymentParams params;
    
    if (request.params.size() > 2) {
        params.blockTimeMs = request.params[2].get_int();
    }
    if (request.params.size() > 3) {
        params.gasLimit = request.params[3].get_int64();
    }
    if (request.params.size() > 4) {
        params.challengePeriod = request.params[4].get_int64();
    }
    if (request.params.size() > 5) {
        params.minSequencerStake = AmountFromValue(request.params[5]);
    }
    if (request.params.size() > 6) {
        params.minSequencerHatScore = request.params[6].get_int();
    }
    
    // Validate parameters
    l2::ValidationResult paramsValidation = l2::L2Registry::ValidateDeploymentParams(params);
    if (!paramsValidation.isValid) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, paramsValidation.errorMessage);
    }
    
    l2::ValidationResult stakeValidation = l2::L2Registry::ValidateDeployerStake(stake);
    if (!stakeValidation.isValid) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, stakeValidation.errorMessage);
    }
    
    l2::ValidationResult nameValidation = l2::L2Registry::ValidateChainName(chainName);
    if (!nameValidation.isValid) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, nameValidation.errorMessage);
    }
    
    // Get a key from wallet for deployer address
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    
    uint160 deployer(newKey.GetID());
    
    // Initialize registry if needed
    if (!l2::IsL2RegistryInitialized()) {
        l2::InitL2Registry();
    }
    
    l2::L2Registry& registry = l2::GetL2Registry();
    
    // Check if name already exists
    if (registry.ChainNameExists(chainName)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Chain name already exists");
    }
    
    // Register the chain
    uint64_t l1BlockNumber = chainActive.Height();
    uint64_t chainId = registry.RegisterL2Chain(chainName, deployer, stake, params, l1BlockNumber);
    
    if (chainId == 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to register L2 chain");
    }
    
    // Get the registered chain info
    std::optional<l2::L2ChainInfo> chainInfo = registry.GetL2ChainInfo(chainId);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("chainId", (int64_t)chainId);
    result.pushKV("name", chainName);
    result.pushKV("deployer", "0x" + deployer.GetHex());
    result.pushKV("stake", ValueFromAmount(stake));
    result.pushKV("status", l2::L2ChainStatusToString(l2::L2ChainStatus::BOOTSTRAPPING));
    result.pushKV("deploymentBlock", (int64_t)l1BlockNumber);
    
    // Include parameters
    UniValue paramsObj(UniValue::VOBJ);
    paramsObj.pushKV("blockTimeMs", (int)params.blockTimeMs);
    paramsObj.pushKV("gasLimit", (int64_t)params.gasLimit);
    paramsObj.pushKV("challengePeriod", (int64_t)params.challengePeriod);
    paramsObj.pushKV("minSequencerStake", ValueFromAmount(params.minSequencerStake));
    paramsObj.pushKV("minSequencerHatScore", (int)params.minSequencerHatScore);
    paramsObj.pushKV("l1AnchorInterval", (int)params.l1AnchorInterval);
    result.pushKV("params", paramsObj);
    
    result.pushKV("message", "L2 chain registered successfully");
    
    return result;
}

UniValue l2_getregisteredchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_getregisteredchain chainid|\"name\"\n"
            "\nGet information about a registered L2 chain.\n"
            "\nArguments:\n"
            "1. chainid|\"name\"    (numeric or string, required) Chain ID or name\n"
            "\nResult:\n"
            "{\n"
            "  \"found\": bool,              (boolean) Whether chain was found\n"
            "  \"chainId\": n,               (numeric) L2 chain ID\n"
            "  \"name\": \"xxx\",             (string) Chain name\n"
            "  \"deployer\": \"xxx\",         (string) Deployer address\n"
            "  \"deploymentBlock\": n,       (numeric) L1 block when deployed\n"
            "  \"deploymentTime\": n,        (numeric) Deployment timestamp\n"
            "  \"status\": \"xxx\",           (string) Chain status\n"
            "  \"stake\": n,                 (numeric) Deployer stake\n"
            "  \"bridgeContract\": \"xxx\",   (string) Bridge contract address\n"
            "  \"latestStateRoot\": \"xxx\",  (string) Latest state root\n"
            "  \"latestL2Block\": n,         (numeric) Latest L2 block number\n"
            "  \"latestL1Anchor\": n,        (numeric) Latest L1 anchor block\n"
            "  \"totalValueLocked\": n,      (numeric) TVL in CAS\n"
            "  \"sequencerCount\": n,        (numeric) Number of sequencers\n"
            "  \"params\": {...}             (object) Deployment parameters\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getregisteredchain", "1001")
            + HelpExampleCli("l2_getregisteredchain", "\"MyL2Chain\"")
            + HelpExampleRpc("l2_getregisteredchain", "1001")
        );

    EnsureL2Enabled();

    if (!l2::IsL2RegistryInitialized()) {
        l2::InitL2Registry();
    }
    
    l2::L2Registry& registry = l2::GetL2Registry();
    
    std::optional<l2::L2ChainInfo> chainInfo;
    
    // Try to parse as chain ID first
    if (request.params[0].isNum()) {
        uint64_t chainId = request.params[0].get_int64();
        chainInfo = registry.GetL2ChainInfo(chainId);
    } else {
        // Try as chain name
        std::string chainName = request.params[0].get_str();
        chainInfo = registry.GetL2ChainInfoByName(chainName);
    }
    
    UniValue result(UniValue::VOBJ);
    
    if (!chainInfo.has_value()) {
        result.pushKV("found", false);
        result.pushKV("message", "Chain not found");
        return result;
    }
    
    const l2::L2ChainInfo& info = chainInfo.value();
    
    result.pushKV("found", true);
    result.pushKV("chainId", (int64_t)info.chainId);
    result.pushKV("name", info.name);
    result.pushKV("deployer", "0x" + info.deployer.GetHex());
    result.pushKV("deploymentBlock", (int64_t)info.deploymentBlock);
    result.pushKV("deploymentTime", (int64_t)info.deploymentTime);
    result.pushKV("status", l2::L2ChainStatusToString(info.status));
    result.pushKV("stake", ValueFromAmount(info.deployerStake));
    result.pushKV("bridgeContract", "0x" + info.bridgeContract.GetHex());
    result.pushKV("genesisHash", info.genesisHash.GetHex());
    result.pushKV("latestStateRoot", info.latestStateRoot.GetHex());
    result.pushKV("latestL2Block", (int64_t)info.latestL2Block);
    result.pushKV("latestL1Anchor", (int64_t)info.latestL1Anchor);
    result.pushKV("totalValueLocked", ValueFromAmount(info.totalValueLocked));
    result.pushKV("sequencerCount", (int)info.sequencerCount);
    result.pushKV("isActive", info.IsActive());
    result.pushKV("acceptsDeposits", info.AcceptsDeposits());
    result.pushKV("allowsWithdrawals", info.AllowsWithdrawals());
    
    // Include parameters
    UniValue paramsObj(UniValue::VOBJ);
    paramsObj.pushKV("blockTimeMs", (int)info.params.blockTimeMs);
    paramsObj.pushKV("gasLimit", (int64_t)info.params.gasLimit);
    paramsObj.pushKV("challengePeriod", (int64_t)info.params.challengePeriod);
    paramsObj.pushKV("minSequencerStake", ValueFromAmount(info.params.minSequencerStake));
    paramsObj.pushKV("minSequencerHatScore", (int)info.params.minSequencerHatScore);
    paramsObj.pushKV("l1AnchorInterval", (int)info.params.l1AnchorInterval);
    result.pushKV("params", paramsObj);
    
    return result;
}

UniValue l2_listregisteredchains(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "l2_listregisteredchains ( activeonly )\n"
            "\nList all registered L2 chains.\n"
            "\nArguments:\n"
            "1. activeonly    (boolean, optional, default=false) Only show active chains\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"chainId\": n,           (numeric) L2 chain ID\n"
            "    \"name\": \"xxx\",         (string) Chain name\n"
            "    \"status\": \"xxx\",       (string) Chain status\n"
            "    \"deployer\": \"xxx\",     (string) Deployer address\n"
            "    \"stake\": n,             (numeric) Deployer stake\n"
            "    \"latestL2Block\": n,     (numeric) Latest L2 block\n"
            "    \"tvl\": n,               (numeric) Total value locked\n"
            "    \"sequencerCount\": n     (numeric) Number of sequencers\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_listregisteredchains", "")
            + HelpExampleCli("l2_listregisteredchains", "true")
            + HelpExampleRpc("l2_listregisteredchains", "true")
        );

    EnsureL2Enabled();

    bool activeOnly = false;
    if (request.params.size() > 0) {
        activeOnly = request.params[0].get_bool();
    }
    
    if (!l2::IsL2RegistryInitialized()) {
        l2::InitL2Registry();
    }
    
    l2::L2Registry& registry = l2::GetL2Registry();
    
    std::vector<l2::L2ChainInfo> chains;
    if (activeOnly) {
        chains = registry.GetActiveChains();
    } else {
        chains = registry.GetAllChains();
    }
    
    UniValue result(UniValue::VARR);
    
    for (const auto& info : chains) {
        UniValue chainObj(UniValue::VOBJ);
        chainObj.pushKV("chainId", (int64_t)info.chainId);
        chainObj.pushKV("name", info.name);
        chainObj.pushKV("status", l2::L2ChainStatusToString(info.status));
        chainObj.pushKV("deployer", "0x" + info.deployer.GetHex());
        chainObj.pushKV("stake", ValueFromAmount(info.deployerStake));
        chainObj.pushKV("deploymentBlock", (int64_t)info.deploymentBlock);
        chainObj.pushKV("latestL2Block", (int64_t)info.latestL2Block);
        chainObj.pushKV("tvl", ValueFromAmount(info.totalValueLocked));
        chainObj.pushKV("sequencerCount", (int)info.sequencerCount);
        chainObj.pushKV("isActive", info.IsActive());
        result.push_back(chainObj);
    }
    
    return result;
}

UniValue l2_updatechainstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "l2_updatechainstatus chainid \"status\"\n"
            "\nUpdate the status of a registered L2 chain.\n"
            "\nArguments:\n"
            "1. chainid    (numeric, required) L2 chain ID\n"
            "2. \"status\"   (string, required) New status: BOOTSTRAPPING, ACTIVE, PAUSED, EMERGENCY, DEPRECATED\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": bool,     (boolean) Whether update succeeded\n"
            "  \"chainId\": n,        (numeric) L2 chain ID\n"
            "  \"oldStatus\": \"xxx\", (string) Previous status\n"
            "  \"newStatus\": \"xxx\", (string) New status\n"
            "  \"message\": \"xxx\"    (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_updatechainstatus", "1001 \"ACTIVE\"")
            + HelpExampleRpc("l2_updatechainstatus", "1001, \"ACTIVE\"")
        );

    EnsureL2Enabled();

    uint64_t chainId = request.params[0].get_int64();
    std::string statusStr = request.params[1].get_str();
    
    // Parse status
    l2::L2ChainStatus newStatus;
    if (statusStr == "BOOTSTRAPPING") {
        newStatus = l2::L2ChainStatus::BOOTSTRAPPING;
    } else if (statusStr == "ACTIVE") {
        newStatus = l2::L2ChainStatus::ACTIVE;
    } else if (statusStr == "PAUSED") {
        newStatus = l2::L2ChainStatus::PAUSED;
    } else if (statusStr == "EMERGENCY") {
        newStatus = l2::L2ChainStatus::EMERGENCY;
    } else if (statusStr == "DEPRECATED") {
        newStatus = l2::L2ChainStatus::DEPRECATED;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "Invalid status. Must be one of: BOOTSTRAPPING, ACTIVE, PAUSED, EMERGENCY, DEPRECATED");
    }
    
    if (!l2::IsL2RegistryInitialized()) {
        l2::InitL2Registry();
    }
    
    l2::L2Registry& registry = l2::GetL2Registry();
    
    // Get current chain info
    std::optional<l2::L2ChainInfo> chainInfo = registry.GetL2ChainInfo(chainId);
    if (!chainInfo.has_value()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Chain not found");
    }
    
    l2::L2ChainStatus oldStatus = chainInfo->status;
    
    // Update status
    bool success = registry.UpdateChainStatus(chainId, newStatus);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("chainId", (int64_t)chainId);
    result.pushKV("oldStatus", l2::L2ChainStatusToString(oldStatus));
    result.pushKV("newStatus", l2::L2ChainStatusToString(newStatus));
    
    if (success) {
        result.pushKV("message", "Chain status updated successfully");
    } else {
        result.pushKV("message", "Failed to update chain status");
    }
    
    return result;
}


// ============================================================================
// RPC Command Registration
// ============================================================================

static const CRPCCommand commands[] =
{
    //  category              name                      actor (function)         argNames
    //  --------------------- ------------------------- ------------------------ ----------
    
    // Task 18.1: Basic L2 RPC Commands (Requirements: 11.7, 40.1)
    { "l2",                   "l2_getbalance",          &l2_getbalance,          {"address"} },
    { "l2",                   "l2_gettransactioncount", &l2_gettransactioncount, {"address"} },
    { "l2",                   "l2_getblockbynumber",    &l2_getblockbynumber,    {"blocknumber", "verbose"} },
    
    // Task 18.2: L2 Deployment RPC (Requirements: 1.1, 1.5)
    { "l2",                   "l2_deploy",              &l2_deploy,              {"name", "blocktime", "gaslimit", "challengeperiod"} },
    { "l2",                   "l2_getchaininfo",        &l2_getchaininfo,        {"chainid"} },
    { "l2",                   "l2_listchains",          &l2_listchains,          {} },
    
    // Task 18.3: Sequencer RPC (Requirements: 2.5, 2.6)
    { "l2",                   "l2_announcesequencer",   &l2_announcesequencer,   {"stake", "hatscore", "endpoint"} },
    { "l2",                   "l2_getsequencers",       &l2_getsequencers,       {"eligibleonly"} },
    { "l2",                   "l2_getleader",           &l2_getleader,           {} },
    
    // Task 8.1: Token Info RPC (Requirements: 8.1, 8.2, 8.3, 8.4)
    { "l2",                   "l2_gettokeninfo",        &l2_gettokeninfo,        {} },
    { "l2",                   "l2_gettokensupply",      &l2_gettokensupply,      {} },
    { "l2",                   "l2_getgenesisdistribution", &l2_getgenesisdistribution, {} },
    { "l2",                   "l2_getmintinghistory",   &l2_getmintinghistory,   {"fromblock", "toblock"} },
    { "l2",                   "l2_getsequencerrewards", &l2_getsequencerrewards, {} },
    
    // Task 8.3: Transfer RPC (Requirements: 2.5, 7.3)
    { "l2",                   "l2_transfer",            &l2_transfer,            {"from", "to", "amount", "fee"} },
    { "l2",                   "l2_sendtransaction",     &l2_sendtransaction,     {"hexstring"} },
    { "l2",                   "l2_gettransfer",         &l2_gettransfer,         {"txhash"} },
    
    // Task 8.4: Faucet RPC (Requirements: 5.1, 5.5)
    { "l2",                   "l2_faucet",              &l2_faucet,              {"address", "amount"} },
    { "l2",                   "l2_getfaucetstatus",     &l2_getfaucetstatus,     {"address"} },
    
    // Task 12: Legacy Bridge RPC REMOVED (Requirements: 11.1, 11.4)
    // l2_deposit and l2_withdraw have been completely removed
    // Use the new burn-and-mint model: l2_createburntx, l2_sendburntx, l2_getburnstatus
    // See src/rpc/l2_burn.cpp for the new implementation
    
    { "l2",                   "l2_getwithdrawalstatus", &l2_getwithdrawalstatus, {"withdrawalid"} },
    
    // Phase 1e: L1 anchoring commitments
    { "l2",                   "l2_createcommitment",    &l2_createcommitment,    {"blocknumber", "staterootoverride"} },
    { "l2",                   "l2_getcommitment",       &l2_getcommitment,       {"blocknumber"} },
    { "l2",                   "l2_getdatastatus",       &l2_getdatastatus,       {} },
    { "l2",                   "l2_registersequencer",   &l2_registersequencer,   {"stake", "action"} },
    { "l2",                   "l2_listsequencers",      &l2_listsequencers,      {} },
    
    // Phase 3: fraud proofs + forced inclusion
    { "l2",                   "l2_submitfraudproof",    &l2_submitfraudproof,    {"blocknumber", "challengeraddress"} },
    { "l2",                   "l2_createforcedtx",      &l2_createforcedtx,      {"from", "to", "amount", "nonce"} },
    
    // Task 21.1: L2 Registry RPC (Requirements: 1.1, 1.2, 1.3, 1.4, 1.5)
    { "l2",                   "l2_registerchain",       &l2_registerchain,       {"name", "stake", "blocktime", "gaslimit", "challengeperiod", "minseqstake", "minseqhatscore"} },
    { "l2",                   "l2_getregisteredchain",  &l2_getregisteredchain,  {"chainid"} },
    { "l2",                   "l2_listregisteredchains",&l2_listregisteredchains,{"activeonly"} },
    { "l2",                   "l2_updatechainstatus",   &l2_updatechainstatus,   {"chainid", "status"} },
};

void RegisterL2RPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}


