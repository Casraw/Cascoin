// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/util.h>
#include <cvm/cvm.h>
#include <cvm/cvmdb.h>
#include <cvm/contract.h>
#include <cvm/reputation.h>
#include <cvm/softfork.h>
#include <cvm/txbuilder.h>
#include <cvm/blockprocessor.h>
#include <cvm/trustgraph.h>
#include <cvm/reward_types.h>
#include <cvm/reward_distributor.h>
#include <cvm/commit_reveal.h>
#include <cvm/behaviormetrics.h>
#include <cvm/graphanalysis.h>
#include <cvm/securehat.h>
#include <cvm/walletcluster.h>
#include <cvm/gas_allowance.h>
#include <cvm/sustainable_gas.h>
#include <cvm/trust_context.h>
#include <cvm/tx_priority.h>
#include <cvm/gas_subsidy.h>
#include <cvm/resource_manager.h>
#include <cvm/cleanup_manager.h>
#include <cvm/evm_rpc.h>
#include <cvm/bytecode_detector.h>
#include <cvm/enhanced_vm.h>
#include <cvm/validator_keys.h>
#include <cvm/hat_consensus.h>
#include <cvm/validator_compensation.h>
#include <validation.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <util.h>
#include <base58.h>
#include <utilstrencodings.h>
#include <univalue.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <hash.h>
#include <core_io.h>
#include <policy/policy.h>
#include <net.h>
#include <chain.h>
#include <script/standard.h>

// Global connection manager
extern std::unique_ptr<CConnman> g_connman;

/**
 * RPC commands for CVM (Cascoin Virtual Machine) and Reputation System
 */

UniValue deploycontract(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "deploycontract \"bytecode\" ( gaslimit \"initdata\" )\n"
            "\nDeploy a smart contract to the Cascoin Virtual Machine.\n"
            "\nSupports both CVM and EVM bytecode with automatic format detection.\n"
            "\nArguments:\n"
            "1. \"bytecode\"      (string, required) Contract bytecode in hex format (CVM or EVM)\n"
            "2. gaslimit        (numeric, optional, default=1000000) Gas limit for deployment\n"
            "3. \"initdata\"      (string, optional) Initialization data in hex format\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",           (string) Transaction ID\n"
            "  \"bytecode_size\": n,        (numeric) Size of bytecode in bytes\n"
            "  \"gas_limit\": n,            (numeric) Gas limit for deployment\n"
            "  \"fee\": n,                  (numeric) Transaction fee\n"
            "  \"format\": \"xxx\",          (string) Detected format (EVM, CVM, HYBRID)\n"
            "  \"format_confidence\": n,    (numeric) Detection confidence (0-100)\n"
            "  \"format_reason\": \"xxx\"     (string) Detection reasoning\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("deploycontract", "\"0x6001600201\"")
            + HelpExampleCli("deploycontract", "\"0x60806040...\" 2000000")
            + HelpExampleRpc("deploycontract", "\"0x6001600201\", 1000000")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Get wallet
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Parse bytecode
    std::string bytecodeHex = request.params[0].get_str();
    if (bytecodeHex.substr(0, 2) == "0x") {
        bytecodeHex = bytecodeHex.substr(2);
    }
    
    std::vector<uint8_t> bytecode = ParseHex(bytecodeHex);
    
    // Detect bytecode format (EVM vs CVM)
    CVM::BytecodeDetector detector;
    CVM::BytecodeDetectionResult detection = detector.DetectFormat(bytecode);
    
    // Get gas limit
    uint64_t gasLimit = CVM::MAX_GAS_PER_TX;
    if (request.params.size() > 1) {
        gasLimit = request.params[1].get_int64();
    }
    
    // Get init data
    std::vector<uint8_t> initData;
    if (request.params.size() > 2) {
        std::string initDataHex = request.params[2].get_str();
        if (initDataHex.substr(0, 2) == "0x") {
            initDataHex = initDataHex.substr(2);
        }
        initData = ParseHex(initDataHex);
    }
    
    // Create and broadcast transaction using wallet
    CWalletTx wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    CCoinControl coin_control;
    
    bool success = pwallet->CreateContractDeploymentTransaction(bytecode, gasLimit, initData, wtx,
                                                                reservekey, nFeeRequired, strError, &coin_control);
    
    if (!success) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    
    // Broadcast transaction
    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, reservekey, g_connman.get(), state)) {
        strError = strprintf("Transaction commit failed: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx.GetHash().GetHex());
    result.pushKV("bytecode_size", (int64_t)bytecode.size());
    result.pushKV("gas_limit", (int64_t)gasLimit);
    result.pushKV("fee", ValueFromAmount(nFeeRequired));
    
    // Add bytecode format detection info
    std::string formatStr;
    switch (detection.format) {
        case CVM::BytecodeFormat::EVM_BYTECODE:
            formatStr = "EVM";
            break;
        case CVM::BytecodeFormat::CVM_NATIVE:
            formatStr = "CVM";
            break;
        case CVM::BytecodeFormat::HYBRID:
            formatStr = "HYBRID";
            break;
        default:
            formatStr = "UNKNOWN";
            break;
    }
    result.pushKV("format", formatStr);
    result.pushKV("format_confidence", detection.confidence);
    result.pushKV("format_reason", detection.reason);
    
    return result;
}

UniValue callcontract(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "callcontract \"contractaddress\" ( \"data\" gaslimit value )\n"
            "\nCall a smart contract function.\n"
            "\nSupports both CVM and EVM contracts with automatic format detection.\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) Contract address (hex format, 40 characters)\n"
            "2. \"data\"            (string, optional) Call data in hex format (function selector + args)\n"
            "3. gaslimit          (numeric, optional, default=1000000) Gas limit\n"
            "4. value             (numeric, optional, default=0) Amount to send in satoshis\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",        (string) Transaction ID\n"
            "  \"gas_limit\": n,        (numeric) Gas limit\n"
            "  \"value\": n,            (numeric) Value sent\n"
            "  \"fee\": n               (numeric) Transaction fee\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("callcontract", "\"a1b2c3d4...\" \"0x12345678\"")
            + HelpExampleCli("callcontract", "\"a1b2c3d4...\" \"0x70a08231000000000000000000000000...\" 500000")
            + HelpExampleRpc("callcontract", "\"a1b2c3d4...\", \"0x12345678\", 500000")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Get wallet
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Parse contract address
    std::string addressStr = request.params[0].get_str();
    if (addressStr.substr(0, 2) == "0x") {
        addressStr = addressStr.substr(2);
    }
    
    if (addressStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract address length (expected 40 hex characters)");
    }
    
    std::vector<uint8_t> addressBytes = ParseHex(addressStr);
    uint160 contractAddr(addressBytes);
    
    // Get call data
    std::vector<uint8_t> callData;
    if (request.params.size() > 1) {
        std::string callDataHex = request.params[1].get_str();
        if (callDataHex.substr(0, 2) == "0x") {
            callDataHex = callDataHex.substr(2);
        }
        callData = ParseHex(callDataHex);
    }
    
    // Get gas limit
    uint64_t gasLimit = CVM::MAX_GAS_PER_TX;
    if (request.params.size() > 2) {
        gasLimit = request.params[2].get_int64();
    }
    
    // Get value
    CAmount value = 0;
    if (request.params.size() > 3) {
        value = AmountFromValue(request.params[3]);
    }
    
    // Create and broadcast transaction using wallet
    CWalletTx wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    CCoinControl coin_control;
    
    bool success = pwallet->CreateContractCallTransaction(contractAddr, callData, gasLimit, value, wtx,
                                                          reservekey, nFeeRequired, strError, &coin_control);
    
    if (!success) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    
    // Broadcast transaction
    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, reservekey, g_connman.get(), state)) {
        strError = strprintf("Transaction commit failed: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx.GetHash().GetHex());
    result.pushKV("gas_limit", (int64_t)gasLimit);
    result.pushKV("value", ValueFromAmount(value));
    result.pushKV("fee", ValueFromAmount(nFeeRequired));
    
    return result;
}

UniValue getcontractinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getcontractinfo \"contractaddress\"\n"
            "\nGet information about a deployed contract.\n"
            "\nReturns contract metadata including bytecode format (CVM/EVM) and opcodes.\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) Contract address (hex format, 40 characters)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",        (string) Contract address\n"
            "  \"bytecode\": \"xxx\",       (string) Contract bytecode\n"
            "  \"bytecode_size\": n,        (numeric) Bytecode size in bytes\n"
            "  \"format\": \"xxx\",          (string) Bytecode format (EVM, CVM, HYBRID)\n"
            "  \"format_confidence\": n,    (numeric) Detection confidence (0-100)\n"
            "  \"format_reason\": \"xxx\",   (string) Detection reasoning\n"
            "  \"is_valid\": bool,          (boolean) Bytecode validity\n"
            "  \"deploy_height\": n,        (numeric) Deployment block height\n"
            "  \"deploy_txid\": \"xxx\",     (string) Deployment transaction ID\n"
            "  \"deployer\": \"xxx\",        (string) Deployer address\n"
            "  \"opcodes\": [...],          (array) List of opcodes (EVM contracts only)\n"
            "  \"opcode_count\": n,         (numeric) Number of opcodes\n"
            "  \"features\": [...]          (array) Detected features\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getcontractinfo", "\"a1b2c3d4e5f6...\"")
            + HelpExampleRpc("getcontractinfo", "\"a1b2c3d4e5f6...\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse contract address
    std::string addressStr = request.params[0].get_str();
    if (addressStr.substr(0, 2) == "0x") {
        addressStr = addressStr.substr(2);
    }
    
    if (addressStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract address length (expected 40 hex characters)");
    }
    
    std::vector<uint8_t> addressBytes = ParseHex(addressStr);
    uint160 contractAddr(addressBytes);
    
    // Get contract bytecode from database
    std::vector<uint8_t> bytecode;
    if (!CVM::g_cvmdb->GetContractCode(contractAddr, bytecode)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract not found");
    }
    
    // Detect bytecode format
    CVM::BytecodeDetector detector;
    CVM::BytecodeDetectionResult detection = detector.DetectFormat(bytecode);
    
    // Get contract metadata
    CVM::Contract contract;
    bool hasMetadata = CVM::g_cvmdb->GetContractMetadata(contractAddr, contract);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", "0x" + addressStr);
    result.pushKV("bytecode", "0x" + HexStr(bytecode));
    result.pushKV("bytecode_size", (int64_t)bytecode.size());
    
    // Add format detection info
    std::string formatStr;
    switch (detection.format) {
        case CVM::BytecodeFormat::EVM_BYTECODE:
            formatStr = "EVM";
            break;
        case CVM::BytecodeFormat::CVM_NATIVE:
            formatStr = "CVM";
            break;
        case CVM::BytecodeFormat::HYBRID:
            formatStr = "HYBRID";
            break;
        default:
            formatStr = "UNKNOWN";
            break;
    }
    result.pushKV("format", formatStr);
    result.pushKV("format_confidence", detection.confidence);
    result.pushKV("format_reason", detection.reason);
    result.pushKV("is_valid", detection.is_valid);
    
    // Add metadata if available
    if (hasMetadata) {
        result.pushKV("deploy_height", (int64_t)contract.deploymentHeight);
        result.pushKV("deploy_txid", contract.deploymentTx.GetHex());
        result.pushKV("deployer", "0x" + contract.address.GetHex());
    }
    
    // Add opcode analysis for EVM contracts
    if (detection.format == CVM::BytecodeFormat::EVM_BYTECODE) {
        CVM::BytecodeAnalysis analysis = detector.AnalyzeBytecode(bytecode);
        
        UniValue opcodes(UniValue::VARR);
        for (const auto& opcode : analysis.opcodes) {
            opcodes.push_back(opcode);
        }
        result.pushKV("opcodes", opcodes);
        result.pushKV("opcode_count", (int64_t)analysis.opcodes.size());
        
        UniValue features(UniValue::VARR);
        for (const auto& feature : analysis.features) {
            features.push_back(feature);
        }
        result.pushKV("features", features);
    }
    
    return result;
}

UniValue getreputation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getreputation \"address\"\n"
            "\nGet reputation score for an address.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) Cascoin address\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"score\": n,                  (numeric) Reputation score (-10000 to +10000)\n"
            "  \"level\": \"xxx\",             (string) Reputation level\n"
            "  \"votecount\": n,              (numeric) Number of votes received\n"
            "  \"category\": \"xxx\",          (string) Address category\n"
            "  \"transactions\": n,           (numeric) Total transactions\n"
            "  \"volume\": n,                 (numeric) Total volume\n"
            "  \"suspicious\": n,             (numeric) Suspicious pattern count\n"
            "  \"lastupdated\": n,            (numeric) Last update timestamp\n"
            "  \"shouldwarn\": true|false     (boolean) Should trigger warning\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getreputation", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("getreputation", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    
    // Parse address
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    // Get address hash - handle both P2PKH and P2SH
    uint160 address;
    if (boost::get<CKeyID>(&dest)) {
        address = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        address = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported for reputation");
    }
    
    // Get reputation score
    CVM::ReputationSystem repSystem(*CVM::g_cvmdb);
    CVM::ReputationScore score;
    repSystem.GetReputation(address, score);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("score", score.score);
    result.pushKV("level", score.GetReputationLevel());
    result.pushKV("votecount", (int64_t)score.voteCount);
    result.pushKV("category", score.category);
    result.pushKV("transactions", (int64_t)score.totalTransactions);
    result.pushKV("volume", (int64_t)score.totalVolume);
    result.pushKV("suspicious", (int64_t)score.suspiciousPatterns);
    result.pushKV("lastupdated", score.lastUpdated);
    result.pushKV("shouldwarn", score.ShouldWarn());
    
    return result;
}

UniValue votereputation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "votereputation \"address\" vote \"reason\" ( \"proof\" )\n"
            "\nVote on an address's reputation.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) Address to vote on\n"
            "2. vote          (numeric, required) Vote value (-100 to +100)\n"
            "3. \"reason\"      (string, required) Reason for vote\n"
            "4. \"proof\"       (string, optional) Proof/evidence in hex format\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",        (string) Transaction ID\n"
            "  \"vote\": n,             (numeric) Vote value\n"
            "  \"reason\": \"xxx\"       (string) Vote reason\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("votereputation", "\"DXG7Yx...\" -50 \"Suspected scam\"")
            + HelpExampleRpc("votereputation", "\"DXG7Yx...\", -50, \"Suspected scam\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    // Handle vote value - can be int or string
    int64_t voteValue;
    if (request.params[1].isNum()) {
        voteValue = request.params[1].get_int64();
    } else {
        voteValue = atoi64(request.params[1].get_str());
    }
    std::string reason = request.params[2].get_str();
    
    // Parse address
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    // Get address hash - handle both P2PKH and P2SH
    uint160 targetAddress;
    if (boost::get<CKeyID>(&dest)) {
        targetAddress = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        targetAddress = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported for reputation");
    }
    
    // Create reputation vote data (Soft Fork compatible with OP_RETURN)
    CVM::CVMReputationData repData;
    repData.targetAddress = targetAddress;
    repData.voteValue = static_cast<int16_t>(voteValue);
    repData.timestamp = static_cast<uint32_t>(GetTime());
    
    // Build OP_RETURN output with reputation data (Soft Fork!)
    std::vector<uint8_t> repBytes = repData.Serialize();
    CScript cvmScript = CVM::BuildCVMOpReturn(CVM::CVMOpType::REPUTATION_VOTE, repBytes);
    
    // For now, just store vote directly in database (simulated on-chain)
    // In production, this would create a real transaction
    if (CVM::g_cvmdb) {
        CVM::ReputationSystem repSystem(*CVM::g_cvmdb);
        CVM::ReputationScore score;
        repSystem.GetReputation(targetAddress, score);
        
        // Update score
        score.score += voteValue;
        score.voteCount++;
        score.lastUpdated = repData.timestamp;
        
        // Store updated score
        repSystem.UpdateReputation(targetAddress, score);
        
        LogPrintf("CVM: Reputation vote recorded for %s: %+d (new score: %d)\n",
                  addressStr, voteValue, score.score);
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "Vote recorded (Soft Fork OP_RETURN)");
    result.pushKV("address", addressStr);
    result.pushKV("vote", voteValue);
    result.pushKV("reason", reason);
    result.pushKV("timestamp", (int64_t)repData.timestamp);
    result.pushKV("op_return_script", HexStr(cvmScript.begin(), cvmScript.end()));
    result.pushKV("softfork_compatible", true);
    
    // Note: In production, this would create a transaction with:
    // - Input: Small amount from voter
    // - Output 0: OP_RETURN (cvmScript)
    // - Output 1: Change back
    
    return result;
}

UniValue listreputations(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "listreputations ( threshold count )\n"
            "\nList addresses with reputation scores.\n"
            "\nArguments:\n"
            "1. threshold    (numeric, optional) Score threshold\n"
            "2. count        (numeric, optional, default=100) Maximum results\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",    (string) Address\n"
            "    \"score\": n,           (numeric) Reputation score\n"
            "    \"level\": \"xxx\"       (string) Reputation level\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listreputations", "-5000 50")
            + HelpExampleRpc("listreputations", "-5000, 50")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    UniValue result(UniValue::VARR);
    
    // Note: Would need to iterate database to list reputations
    // Simplified for now
    
    return result;
}

UniValue getgasallowance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getgasallowance \"address\"\n"
            "\nGet free gas allowance information for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"reputation\": n,             (numeric) Current reputation score\n"
            "  \"eligible\": true|false,      (boolean) Eligible for free gas (80+ reputation)\n"
            "  \"dailyallowance\": n,         (numeric) Daily gas allowance\n"
            "  \"used\": n,                   (numeric) Gas used today\n"
            "  \"remaining\": n,              (numeric) Gas remaining today\n"
            "  \"lastrenewal\": n             (numeric) Block height of last renewal\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getgasallowance", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("getgasallowance", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    
    // Convert to uint160
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get current block height
    int64_t currentHeight = chainActive.Height();
    
    // Initialize trust context
    auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    
    // Create gas allowance tracker (temporary instance for query)
    CVM::GasAllowanceTracker tracker;
    
    // Get allowance state
    auto state = tracker.GetAllowanceState(address, *trust_ctx, currentHeight);
    
    // Check eligibility
    cvm::SustainableGasSystem gasSystem;
    bool eligible = gasSystem.IsEligibleForFreeGas(state.reputation);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("reputation", (int)state.reputation);
    result.pushKV("eligible", eligible);
    result.pushKV("dailyallowance", (int64_t)state.dailyAllowance);
    result.pushKV("used", (int64_t)state.usedToday);
    result.pushKV("remaining", (int64_t)(state.dailyAllowance - state.usedToday));
    result.pushKV("lastrenewal", state.lastRenewalBlock);
    
    return result;
}

UniValue getnetworkcongestion(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getnetworkcongestion\n"
            "\nGet current network congestion level and transaction prioritization info.\n"
            "\nResult:\n"
            "{\n"
            "  \"congestion\": n,              (numeric) Network congestion level (0-100)\n"
            "  \"mempoolsize\": n,             (numeric) Current mempool size\n"
            "  \"maxmempoolsize\": n,          (numeric) Maximum mempool size\n"
            "  \"prioritization\": \"xxx\",      (string) Current prioritization mode\n"
            "  \"highrepthreshold\": n         (numeric) Reputation threshold for high priority\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getnetworkcongestion", "")
            + HelpExampleRpc("getnetworkcongestion", "")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Create priority manager
    CVM::TransactionPriorityManager priorityMgr;
    
    // Get mempool size
    size_t mempoolSize = mempool.size();
    size_t maxMempoolSize = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    
    // Update congestion
    priorityMgr.UpdateNetworkCongestion(mempoolSize, maxMempoolSize);
    uint8_t congestion = priorityMgr.GetNetworkCongestion();
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("congestion", (int)congestion);
    result.pushKV("mempoolsize", (int64_t)mempoolSize);
    result.pushKV("maxmempoolsize", (int64_t)maxMempoolSize);
    
    // Determine prioritization mode based on congestion
    std::string prioritizationMode;
    if (congestion >= 80) {
        prioritizationMode = "strict";  // Only high-reputation transactions
    } else if (congestion >= 50) {
        prioritizationMode = "moderate";  // Prioritize high-reputation
    } else {
        prioritizationMode = "normal";  // All transactions accepted
    }
    result.pushKV("prioritization", prioritizationMode);
    result.pushKV("highrepthreshold", 70);  // 70+ reputation for high priority
    
    return result;
}

UniValue getgassubsidies(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getgassubsidies \"address\"\n"
            "\nGet gas subsidy information for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"totalsubsidies\": n,         (numeric) Total subsidies received\n"
            "  \"pendingrebates\": n          (numeric) Pending rebates\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getgassubsidies", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("getgassubsidies", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    
    // Convert to uint160
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Create subsidy tracker (temporary instance for query)
    CVM::GasSubsidyTracker tracker;
    
    // Get subsidy info
    uint64_t totalSubsidies = tracker.GetTotalSubsidies(address);
    uint64_t pendingRebates = tracker.GetPendingRebates(address);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("totalsubsidies", (int64_t)totalSubsidies);
    result.pushKV("pendingrebates", (int64_t)pendingRebates);
    
    return result;
}

UniValue creategaspool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "creategaspool \"poolid\" amount ( minreputation )\n"
            "\nCreate a community gas pool.\n"
            "\nArguments:\n"
            "1. \"poolid\"        (string, required) Pool identifier\n"
            "2. amount          (numeric, required) Initial amount\n"
            "3. minreputation   (numeric, optional, default=30) Minimum reputation to use pool\n"
            "\nResult:\n"
            "{\n"
            "  \"poolid\": \"xxx\",            (string) Pool identifier\n"
            "  \"amount\": n,                (numeric) Initial amount\n"
            "  \"minreputation\": n          (numeric) Minimum reputation\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("creategaspool", "\"public-good\" 1000000 30")
            + HelpExampleRpc("creategaspool", "\"public-good\", 1000000, 30")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse parameters
    std::string poolId = request.params[0].get_str();
    uint64_t amount = request.params[1].get_int64();
    uint8_t minReputation = request.params.size() > 2 ? request.params[2].get_int() : 30;
    
    // Get current block height
    int64_t currentHeight = chainActive.Height();
    
    // Create subsidy tracker (temporary instance)
    CVM::GasSubsidyTracker tracker;
    
    // Create pool
    tracker.CreateGasPool(poolId, amount, minReputation, currentHeight);
    
    // Save to database
    tracker.SaveToDatabase(*CVM::g_cvmdb);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("poolid", poolId);
    result.pushKV("amount", (int64_t)amount);
    result.pushKV("minreputation", (int)minReputation);
    
    return result;
}

UniValue getgaspoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getgaspoolinfo \"poolid\"\n"
            "\nGet information about a gas pool.\n"
            "\nArguments:\n"
            "1. \"poolid\"    (string, required) Pool identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"poolid\": \"xxx\",            (string) Pool identifier\n"
            "  \"totalcontributed\": n,      (numeric) Total contributed\n"
            "  \"totalused\": n,             (numeric) Total used\n"
            "  \"remaining\": n,             (numeric) Remaining balance\n"
            "  \"minreputation\": n,         (numeric) Minimum reputation\n"
            "  \"createdheight\": n          (numeric) Block height created\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getgaspoolinfo", "\"public-good\"")
            + HelpExampleRpc("getgaspoolinfo", "\"public-good\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse pool ID
    std::string poolId = request.params[0].get_str();
    
    // Create subsidy tracker (temporary instance)
    CVM::GasSubsidyTracker tracker;
    tracker.LoadFromDatabase(*CVM::g_cvmdb);
    
    // Get pool info
    CVM::GasSubsidyTracker::GasPool pool;
    if (!tracker.GetPoolInfo(poolId, pool)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Gas pool not found");
    }
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("poolid", poolId);
    result.pushKV("totalcontributed", (int64_t)pool.totalContributed);
    result.pushKV("totalused", (int64_t)pool.totalUsed);
    result.pushKV("remaining", (int64_t)pool.remaining);
    result.pushKV("minreputation", (int)pool.minReputation);
    result.pushKV("createdheight", pool.createdHeight);
    
    return result;
}

UniValue estimategascost(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "estimategascost gaslimit ( \"address\" )\n"
            "\nEstimate gas cost with reputation-based discounts.\n"
            "\nArguments:\n"
            "1. gaslimit      (numeric, required) Estimated gas limit\n"
            "2. \"address\"     (string, optional) Address for reputation discount\n"
            "\nResult:\n"
            "{\n"
            "  \"gaslimit\": n,              (numeric) Gas limit\n"
            "  \"basecost\": n,              (numeric) Base cost without discount\n"
            "  \"reputation\": n,            (numeric) Address reputation (if provided)\n"
            "  \"discount\": n,              (numeric) Discount percentage\n"
            "  \"finalcost\": n,             (numeric) Final cost with discount\n"
            "  \"freegaseligible\": bool     (boolean) Eligible for free gas\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("estimategascost", "100000")
            + HelpExampleCli("estimategascost", "100000 \"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("estimategascost", "100000, \"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse gas limit
    uint64_t gasLimit = request.params[0].get_int64();
    
    // Initialize gas system
    cvm::SustainableGasSystem gasSystem;
    
    // Default values
    uint8_t reputation = 0;
    bool hasAddress = false;
    
    // Parse address if provided
    if (request.params.size() > 1) {
        std::string addressStr = request.params[1].get_str();
        
        // Convert to uint160
        std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
        if (addressData.size() == 25) {
            uint160 address;
            memcpy(address.begin(), &addressData[1], 20);
            
            // Get reputation
            auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
            reputation = static_cast<uint8_t>(trust_ctx->GetCallerReputation());
            hasAddress = true;
        }
    }
    
    // Calculate base cost (simplified - using base gas price)
    uint64_t baseCost = gasLimit * 10000000; // 0.01 gwei per gas
    
    // Calculate discount
    double discountMultiplier = 1.0 - (reputation / 200.0); // 0.5 to 1.0
    uint64_t finalCost = static_cast<uint64_t>(baseCost * discountMultiplier);
    uint64_t discount = static_cast<uint64_t>((1.0 - discountMultiplier) * 100);
    
    // Check free gas eligibility
    bool freeGasEligible = gasSystem.IsEligibleForFreeGas(reputation);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("gaslimit", (int64_t)gasLimit);
    result.pushKV("basecost", (int64_t)baseCost);
    
    if (hasAddress) {
        result.pushKV("reputation", (int)reputation);
        result.pushKV("discount", (int)discount);
    }
    
    result.pushKV("finalcost", (int64_t)finalCost);
    result.pushKV("freegaseligible", freeGasEligible);
    
    return result;
}

UniValue createpriceguarantee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "createpriceguarantee \"address\" guaranteedprice duration ( minreputation )\n"
            "\nCreate a price guarantee for a business address.\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) Business address\n"
            "2. guaranteedprice   (numeric, required) Guaranteed gas price\n"
            "3. duration          (numeric, required) Duration in blocks\n"
            "4. minreputation     (numeric, optional, default=50) Minimum reputation required\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Business address\n"
            "  \"guaranteedprice\": n,       (numeric) Guaranteed price\n"
            "  \"duration\": n,              (numeric) Duration in blocks\n"
            "  \"expiration\": n,            (numeric) Expiration block height\n"
            "  \"minreputation\": n          (numeric) Minimum reputation\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("createpriceguarantee", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\" 5000000 100000 50")
            + HelpExampleRpc("createpriceguarantee", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\", 5000000, 100000, 50")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse parameters
    std::string addressStr = request.params[0].get_str();
    uint64_t guaranteedPrice = request.params[1].get_int64();
    uint64_t duration = request.params[2].get_int64();
    uint8_t minReputation = request.params.size() > 3 ? request.params[3].get_int() : 50;
    
    // Convert to uint160
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get current block height
    int64_t currentHeight = chainActive.Height();
    int64_t expiration = currentHeight + duration;
    
    // Create price guarantee
    cvm::SustainableGasSystem gasSystem;
    gasSystem.CreatePriceGuarantee(address, guaranteedPrice, expiration, minReputation);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("guaranteedprice", (int64_t)guaranteedPrice);
    result.pushKV("duration", (int64_t)duration);
    result.pushKV("expiration", expiration);
    result.pushKV("minreputation", (int)minReputation);
    
    return result;
}

UniValue getpriceguarantee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getpriceguarantee \"address\"\n"
            "\nGet price guarantee information for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"hasguarantee\": bool,       (boolean) Has active guarantee\n"
            "  \"guaranteedprice\": n,       (numeric) Guaranteed price (if active)\n"
            "  \"expiration\": n,            (numeric) Expiration height (if active)\n"
            "  \"blocksremaining\": n        (numeric) Blocks until expiration (if active)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getpriceguarantee", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("getpriceguarantee", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    
    // Convert to uint160
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get current block height
    int64_t currentHeight = chainActive.Height();
    
    // Check for price guarantee with expiration check
    cvm::SustainableGasSystem gasSystem;
    cvm::PriceGuarantee guarantee;
    bool hasGuarantee = gasSystem.GetPriceGuaranteeInfo(address, guarantee);
    
    // Check if expired
    if (hasGuarantee && currentHeight >= static_cast<int64_t>(guarantee.expirationBlock)) {
        hasGuarantee = false;
    }
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("hasguarantee", hasGuarantee);
    
    if (hasGuarantee) {
        result.pushKV("guaranteedprice", (int64_t)guarantee.guaranteedPrice);
        result.pushKV("expiration", (int64_t)guarantee.expirationBlock);
        result.pushKV("blocksremaining", (int64_t)(guarantee.expirationBlock - currentHeight));
        result.pushKV("minreputation", (int)guarantee.minReputation);
    }
    
    return result;
}

/**
 * sendcvmvote - Send reputation vote transaction to network
 * 
 * Creates, signs, and broadcasts a real transaction with vote in OP_RETURN.
 * This will go into mempool and be mined into a block.
 */
UniValue sendcvmvote(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 3)
        throw std::runtime_error(
            "sendcvmvote \"address\" vote \"reason\"\n"
            "\nSend reputation vote transaction (broadcasts to network).\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) Address to vote on\n"
            "2. vote          (numeric, required) Vote value (-100 to +100)\n"
            "3. \"reason\"      (string, required) Reason for vote\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",     (string) Transaction ID\n"
            "  \"fee\": n,            (numeric) Transaction fee\n"
            "  \"mempool\": true      (boolean) In mempool\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("sendcvmvote", "\"Qi9hi...\" 100 \"Trusted user\"")
            + HelpExampleRpc("sendcvmvote", "\"Qi9hi...\", 100, \"Trusted user\"")
        );
    
    // Get wallet
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available");
    }
    
    EnsureWalletIsUnlocked(pwallet);
    
    // Parse parameters
    std::string addressStr = request.params[0].get_str();
    int64_t voteValue;
    if (request.params[1].isNum()) {
        voteValue = request.params[1].get_int64();
    } else {
        voteValue = atoi64(request.params[1].get_str());
    }
    std::string reason = request.params[2].get_str();
    
    // Validate vote range
    if (voteValue < -100 || voteValue > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Vote value must be between -100 and +100");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    // Get address hash
    uint160 targetAddress;
    if (boost::get<CKeyID>(&dest)) {
        targetAddress = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        targetAddress = uint160(boost::get<CScriptID>(dest));
    } else if (boost::get<WitnessV0KeyHash>(&dest)) {
        // Support bech32 addresses (tcas1q... / cas1q...)
        const WitnessV0KeyHash& wkh = boost::get<WitnessV0KeyHash>(dest);
        targetAddress = uint160(wkh);
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Build transaction
    CAmount fee;
    std::string error;
    CMutableTransaction mtx = CVM::CVMTransactionBuilder::BuildVoteTransaction(
        pwallet, targetAddress, static_cast<int16_t>(voteValue), reason, fee, error
    );
    
    if (mtx.vin.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + error);
    }
    
    // Sign transaction
    if (!CVM::CVMTransactionBuilder::SignTransaction(pwallet, mtx, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction: " + error);
    }
    
    // Broadcast transaction
    CTransactionRef tx = MakeTransactionRef(std::move(mtx));
    uint256 txid;
    if (!CVM::CVMTransactionBuilder::BroadcastTransaction(*tx, txid, error)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Failed to broadcast transaction: " + error);
    }
    
    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("mempool", true);
    result.pushKV("address", addressStr);
    result.pushKV("vote", voteValue);
    result.pushKV("reason", reason);
    
    return result;
}

/**
 * sendcvmcontract - Deploy contract transaction to network
 * 
 * Creates, signs, and broadcasts a real transaction with contract in OP_RETURN.
 */
UniValue sendcvmcontract(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendcvmcontract \"bytecode\" ( gaslimit )\n"
            "\nDeploy smart contract transaction (broadcasts to network).\n"
            "\nSupports both CVM and EVM bytecode with automatic format detection.\n"
            "\nArguments:\n"
            "1. \"bytecode\"    (string, required) Contract bytecode in hex (CVM or EVM)\n"
            "2. gaslimit      (numeric, optional) Gas limit (default: 1000000)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",         (string) Transaction ID\n"
            "  \"fee\": n,                (numeric) Transaction fee\n"
            "  \"bytecode_hash\": \"xxx\", (string) Bytecode hash\n"
            "  \"bytecode_size\": n,      (numeric) Bytecode size in bytes\n"
            "  \"gas_limit\": n,          (numeric) Gas limit\n"
            "  \"mempool\": true,         (boolean) In mempool\n"
            "  \"format\": \"xxx\",        (string) Detected format (EVM, CVM, HYBRID)\n"
            "  \"format_confidence\": n   (numeric) Detection confidence (0-100)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("sendcvmcontract", "\"6001600201\" 1000000")
            + HelpExampleCli("sendcvmcontract", "\"0x60806040...\" 2000000")
            + HelpExampleRpc("sendcvmcontract", "\"6001600201\", 1000000")
        );
    
    // Get wallet
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not available");
    }
    
    EnsureWalletIsUnlocked(pwallet);
    
    // Parse bytecode
    std::string bytecodeHex = request.params[0].get_str();
    if (bytecodeHex.substr(0, 2) == "0x") {
        bytecodeHex = bytecodeHex.substr(2);
    }
    std::vector<uint8_t> bytecode = ParseHex(bytecodeHex);
    
    // Validate bytecode
    std::string validationError;
    if (!CVM::ValidateContractCode(bytecode, validationError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid bytecode: " + validationError);
    }
    
    // Get gas limit
    uint64_t gasLimit = 1000000;
    if (request.params.size() > 1) {
        gasLimit = request.params[1].get_int64();
    }
    
    // Build transaction
    CAmount fee;
    std::string error;
    CMutableTransaction mtx = CVM::CVMTransactionBuilder::BuildDeployTransaction(
        pwallet, bytecode, gasLimit, fee, error
    );
    
    if (mtx.vin.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + error);
    }
    
    // Sign transaction
    if (!CVM::CVMTransactionBuilder::SignTransaction(pwallet, mtx, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction: " + error);
    }
    
    // Broadcast transaction
    CTransactionRef tx = MakeTransactionRef(std::move(mtx));
    uint256 txid;
    if (!CVM::CVMTransactionBuilder::BroadcastTransaction(*tx, txid, error)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Failed to broadcast transaction: " + error);
    }
    
    // Calculate bytecode hash
    uint256 codeHash = Hash(bytecode.begin(), bytecode.end());
    
    // Detect bytecode format (EVM vs CVM)
    CVM::BytecodeDetector detector;
    CVM::BytecodeDetectionResult detection = detector.DetectFormat(bytecode);
    
    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("bytecode_hash", codeHash.GetHex());
    result.pushKV("bytecode_size", (int64_t)bytecode.size());
    result.pushKV("gas_limit", (int64_t)gasLimit);
    result.pushKV("mempool", true);
    
    // Add bytecode format detection info
    std::string formatStr;
    switch (detection.format) {
        case CVM::BytecodeFormat::EVM_BYTECODE:
            formatStr = "EVM";
            break;
        case CVM::BytecodeFormat::CVM_NATIVE:
            formatStr = "CVM";
            break;
        case CVM::BytecodeFormat::HYBRID:
            formatStr = "HYBRID";
            break;
        default:
            formatStr = "UNKNOWN";
            break;
    }
    result.pushKV("format", formatStr);
    result.pushKV("format_confidence", detection.confidence);
    
    return result;
}

/**
 * addtrust - Add trust relationship (Web-of-Trust)
 * 
 * Creates a trust edge from caller to target address.
 * Requires bonding CAS to prevent spam.
 */
UniValue addtrust(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "addtrust \"address\" weight ( bond \"reason\" )\n"
            "\nAdd trust relationship in Web-of-Trust graph.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) Address to trust\n"
            "2. weight        (numeric, required) Trust weight (-100 to +100)\n"
            "3. bond          (numeric, optional) Amount to bond (default: calculated)\n"
            "4. \"reason\"      (string, optional) Reason for trust\n"
            "\nResult:\n"
            "{\n"
            "  \"from\": \"xxx\",         (string) Your address\n"
            "  \"to\": \"xxx\",           (string) Trusted address\n"
            "  \"weight\": n,           (numeric) Trust weight\n"
            "  \"bond\": n,             (numeric) Bonded amount\n"
            "  \"required_bond\": n     (numeric) Required bond\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("addtrust", "\"Qi9hi...\" 80 1.5 \"Trusted user\"")
            + HelpExampleRpc("addtrust", "\"Qi9hi...\", 80, 1.5, \"Trusted user\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse parameters
    std::string addressStr = request.params[0].get_str();
    int64_t weight;
    if (request.params[1].isNum()) {
        weight = request.params[1].get_int64();
    } else {
        weight = atoi64(request.params[1].get_str());
    }
    
    // Validate weight
    if (weight < -100 || weight > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Weight must be between -100 and +100");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 toAddress;
    if (boost::get<CKeyID>(&dest)) {
        toAddress = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        toAddress = uint160(boost::get<CScriptID>(dest));
    } else if (boost::get<WitnessV0KeyHash>(&dest)) {
        // Support bech32 addresses (tcas1q... / cas1q...)
        const WitnessV0KeyHash& wkh = boost::get<WitnessV0KeyHash>(dest);
        toAddress = uint160(wkh);
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Get caller's address (would need wallet integration)
    uint160 fromAddress; // Placeholder
    
    // Calculate required bond
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    CAmount requiredBond = CVM::g_wotConfig.minBondAmount + 
                          (CVM::g_wotConfig.bondPerVotePoint * std::abs(weight));
    
    // Get bond amount
    CAmount bondAmount = requiredBond;
    if (request.params.size() > 2) {
        bondAmount = AmountFromValue(request.params[2]);
    }
    
    if (bondAmount < requiredBond) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Insufficient bond: have %d, need %d", 
                     bondAmount, requiredBond));
    }
    
    // Get reason
    std::string reason = "";
    if (request.params.size() > 3) {
        reason = request.params[3].get_str();
    }
    
    // Placeholder bond transaction (in production, would create real TX)
    uint256 bondTx;
    
    // Add trust edge
    if (!trustGraph.AddTrustEdge(fromAddress, toAddress, weight, bondAmount, bondTx, reason)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to add trust edge");
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("from", fromAddress.ToString());
    result.pushKV("to", addressStr);
    result.pushKV("weight", weight);
    result.pushKV("bond", ValueFromAmount(bondAmount));
    result.pushKV("required_bond", ValueFromAmount(requiredBond));
    result.pushKV("reason", reason);
    
    return result;
}

/**
 * getweightedreputation - Get reputation from viewer's perspective (Web-of-Trust)
 * 
 * Returns personalized reputation based on viewer's trust graph.
 */
UniValue getweightedreputation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "getweightedreputation \"target\" ( \"viewer\" maxdepth )\n"
            "\nGet weighted reputation from viewer's perspective.\n"
            "\nArguments:\n"
            "1. \"target\"      (string, required) Target address\n"
            "2. \"viewer\"      (string, optional) Viewer address (default: caller)\n"
            "3. maxdepth      (numeric, optional) Max trust path depth (default: 3)\n"
            "\nResult:\n"
            "{\n"
            "  \"target\": \"xxx\",       (string) Target address\n"
            "  \"viewer\": \"xxx\",       (string) Viewer address\n"
            "  \"reputation\": n,       (numeric) Weighted reputation score\n"
            "  \"paths_found\": n,      (numeric) Number of trust paths found\n"
            "  \"max_depth\": n         (numeric) Maximum depth searched\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getweightedreputation", "\"Qi9hi...\"")
            + HelpExampleRpc("getweightedreputation", "\"Qi9hi...\", \"Qj8gh...\", 3")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse target address
    std::string targetStr = request.params[0].get_str();
    CTxDestination targetDest = DecodeDestination(targetStr);
    if (!IsValidDestination(targetDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid target address");
    }
    
    uint160 targetAddress;
    if (boost::get<CKeyID>(&targetDest)) {
        targetAddress = uint160(boost::get<CKeyID>(targetDest));
    } else if (boost::get<CScriptID>(&targetDest)) {
        targetAddress = uint160(boost::get<CScriptID>(targetDest));
    } else if (boost::get<WitnessV0KeyHash>(&targetDest)) {
        // Support bech32 addresses (tcas1q... / cas1q...)
        const WitnessV0KeyHash& wkh = boost::get<WitnessV0KeyHash>(targetDest);
        targetAddress = uint160(wkh);
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Parse viewer address
    uint160 viewerAddress = targetAddress; // Default: self-view
    if (request.params.size() > 1) {
        std::string viewerStr = request.params[1].get_str();
        CTxDestination viewerDest = DecodeDestination(viewerStr);
        if (!IsValidDestination(viewerDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewer address");
        }
        
        if (boost::get<CKeyID>(&viewerDest)) {
            viewerAddress = uint160(boost::get<CKeyID>(viewerDest));
        } else if (boost::get<CScriptID>(&viewerDest)) {
            viewerAddress = uint160(boost::get<CScriptID>(viewerDest));
        } else if (boost::get<WitnessV0KeyHash>(&viewerDest)) {
            // Support bech32 addresses (tcas1q... / cas1q...)
            const WitnessV0KeyHash& wkh = boost::get<WitnessV0KeyHash>(viewerDest);
            viewerAddress = uint160(wkh);
        }
    }
    
    // Get max depth
    int maxDepth = 3;
    if (request.params.size() > 2) {
        maxDepth = request.params[2].get_int();
        if (maxDepth < 1 || maxDepth > 10) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Max depth must be between 1 and 10");
        }
    }
    
    // Calculate weighted reputation
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    double reputation = trustGraph.GetWeightedReputation(viewerAddress, targetAddress, maxDepth);
    
    // Find trust paths
    std::vector<CVM::TrustPath> paths = trustGraph.FindTrustPaths(viewerAddress, targetAddress, maxDepth);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("target", targetStr);
    result.pushKV("viewer", viewerAddress.ToString());
    result.pushKV("reputation", reputation);
    result.pushKV("paths_found", (int64_t)paths.size());
    result.pushKV("max_depth", maxDepth);
    
    // Add path details
    UniValue pathsArray(UniValue::VARR);
    for (const auto& path : paths) {
        if (pathsArray.size() >= 10) break; // Limit to first 10 paths
        
        UniValue pathObj(UniValue::VOBJ);
        pathObj.pushKV("length", (int64_t)path.Length());
        pathObj.pushKV("weight", path.totalWeight);
        
        UniValue hopsArray(UniValue::VARR);
        for (size_t i = 0; i < path.addresses.size(); i++) {
            UniValue hop(UniValue::VOBJ);
            hop.pushKV("address", path.addresses[i].ToString());
            hop.pushKV("trust_weight", path.weights[i]);
            hopsArray.push_back(hop);
        }
        pathObj.pushKV("hops", hopsArray);
        pathsArray.push_back(pathObj);
    }
    result.pushKV("paths", pathsArray);
    
    return result;
}

/**
 * listtrustrelations - List all trust relationships in the graph
 */
UniValue listtrustrelations(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "listtrustrelations [max_count]\n"
            "\nList all trust relationships in the Web-of-Trust graph.\n"
            "\nArguments:\n"
            "1. max_count    (numeric, optional, default=100) Maximum number to return\n"
            "\nResult:\n"
            "{\n"
            "  \"edges\": [              (array) Trust edges\n"
            "    {\n"
            "      \"from\": \"address\",\n"
            "      \"to\": \"address\",\n"
            "      \"weight\": n,\n"
            "      \"bond_amount\": n,\n"
            "      \"timestamp\": n\n"
            "    }, ...\n"
            "  ],\n"
            "  \"count\": n              (numeric) Number of edges returned\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("listtrustrelations", "")
            + HelpExampleCli("listtrustrelations", "50")
            + HelpExampleRpc("listtrustrelations", "50")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    int maxCount = 100;
    if (request.params.size() > 0) {
        maxCount = request.params[0].get_int();
        if (maxCount < 1 || maxCount > 1000) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Max count must be between 1 and 1000");
        }
    }
    
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    
    // Get all trust edges using existing infrastructure
    std::vector<std::string> keys = CVM::g_cvmdb->ListKeysWithPrefix("trust_");
    
    UniValue edgesArray(UniValue::VARR);
    std::set<std::string> addresses;
    int count = 0;
    
    for (const auto& key : keys) {
        // Skip reverse index keys
        if (key.find("trust_in_") != std::string::npos) continue;
        
        // Read edge
        std::vector<uint8_t> data;
        if (CVM::g_cvmdb->ReadGeneric(key, data)) {
            try {
                CVM::TrustEdge edge;
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ss >> edge;
                
                // Convert uint160 to readable Cascoin addresses
                std::string fromAddr = EncodeDestination(CKeyID(edge.fromAddress));
                std::string toAddr = EncodeDestination(CKeyID(edge.toAddress));
                
                UniValue edgeObj(UniValue::VOBJ);
                edgeObj.pushKV("from", fromAddr);
                edgeObj.pushKV("to", toAddr);
                edgeObj.pushKV("weight", edge.trustWeight);
                edgeObj.pushKV("bond_amount", ValueFromAmount(edge.bondAmount));
                edgeObj.pushKV("timestamp", (int64_t)edge.timestamp);
                edgeObj.pushKV("reason", edge.reason);
                edgeObj.pushKV("slashed", edge.slashed);
                
                edgesArray.push_back(edgeObj);
                
                // Collect addresses for reputation lookup (use readable format)
                addresses.insert(fromAddr);
                addresses.insert(toAddr);
                
                count++;
                if (count >= maxCount) break;
                
            } catch (const std::exception& e) {
                LogPrintf("listtrustrelations: Failed to deserialize edge for key %s: %s\n", 
                         key, e.what());
            }
        }
    }
    
    // Get reputations for all addresses involved
    UniValue reputations(UniValue::VOBJ);
    for (const auto& addrStr : addresses) {
        CTxDestination dest = DecodeDestination(addrStr);
        if (IsValidDestination(dest)) {
            uint160 addr;
            if (boost::get<CKeyID>(&dest)) {
                addr = uint160(boost::get<CKeyID>(dest));
            } else if (boost::get<CScriptID>(&dest)) {
                addr = uint160(boost::get<CScriptID>(dest));
            } else {
                continue;
            }
            
            // Get reputation (self-view = average of incoming trust)
            double rep = trustGraph.GetWeightedReputation(addr, addr, 1);
            reputations.pushKV(addrStr, rep);
        }
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("edges", edgesArray);
    result.pushKV("reputations", reputations);
    result.pushKV("count", count);
    
    return result;
}

/**
 * gettrustgraphstats - Get Web-of-Trust graph statistics
 */
UniValue gettrustgraphstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "gettrustgraphstats\n"
            "\nGet Web-of-Trust graph statistics.\n"
            "\nResult:\n"
            "{\n"
            "  \"total_trust_edges\": n,  (numeric) Total trust relationships\n"
            "  \"total_votes\": n,        (numeric) Total bonded votes\n"
            "  \"total_disputes\": n,     (numeric) Total DAO disputes\n"
            "  \"active_disputes\": n,    (numeric) Active disputes\n"
            "  \"slashed_votes\": n       (numeric) Slashed votes\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettrustgraphstats", "")
            + HelpExampleRpc("gettrustgraphstats", "")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    std::map<std::string, uint64_t> stats = trustGraph.GetGraphStats();
    
    UniValue result(UniValue::VOBJ);
    for (const auto& stat : stats) {
        result.pushKV(stat.first, (int64_t)stat.second);
    }
    
    // Add configuration
    result.pushKV("min_bond_amount", ValueFromAmount(CVM::g_wotConfig.minBondAmount));
    result.pushKV("bond_per_vote_point", ValueFromAmount(CVM::g_wotConfig.bondPerVotePoint));
    result.pushKV("max_trust_path_depth", CVM::g_wotConfig.maxTrustPathDepth);
    result.pushKV("min_dao_votes", CVM::g_wotConfig.minDAOVotesForResolution);
    
    return result;
}

/**
 * sendtrustrelation - Broadcast trust relationship to network (Phase 3: On-Chain)
 */
UniValue sendtrustrelation(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "sendtrustrelation \"address\" weight ( bond \"reason\" )\n"
            "\nBroadcast a trust relationship to the network (on-chain).\n"
            "\nArguments:\n"
            "1. \"address\"       (string, required) Address to trust\n"
            "2. weight           (numeric, required) Trust weight (-100 to +100)\n"
            "3. bond             (numeric, optional, default=1.0) CAS to bond\n"
            "4. \"reason\"        (string, optional) Reason for trust\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",       (string) Transaction ID\n"
            "  \"fee\": n.nnnnn,       (numeric) Transaction fee\n"
            "  \"bond\": n.nnnnn       (numeric) Bonded amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtrustrelation", "\"QAddress...\" 80 1.5 \"Friend\"")
            + HelpExampleRpc("sendtrustrelation", "\"QAddress...\", 80, 1.5, \"Friend\"")
        );
    
    LOCK2(cs_main, pwallet->cs_wallet);
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address");
    }
    
    uint160 toAddress;
    if (dest.type() == typeid(CKeyID)) {
        toAddress = uint160(boost::get<CKeyID>(dest));
    } else if (dest.type() == typeid(CScriptID)) {
        toAddress = uint160(boost::get<CScriptID>(dest));
    } else if (dest.type() == typeid(WitnessV0KeyHash)) {
        // Support bech32 addresses (tcas1q... / cas1q...)
        const WitnessV0KeyHash& wkh = boost::get<WitnessV0KeyHash>(dest);
        toAddress = uint160(wkh);
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Parse weight
    int64_t weightInt = 0;
    if (request.params[1].isNum()) {
        weightInt = request.params[1].get_int64();
    } else {
        weightInt = atoi64(request.params[1].get_str());
    }
    
    if (weightInt < -100 || weightInt > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Weight must be between -100 and +100");
    }
    int16_t weight = static_cast<int16_t>(weightInt);
    
    // Parse bond amount
    CAmount bondAmount = 1 * COIN; // Default 1 CAS
    if (request.params.size() > 2) {
        bondAmount = AmountFromValue(request.params[2]);
    }
    
    if (bondAmount < COIN / 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Bond amount must be at least 0.01 CAS");
    }
    
    // Parse reason
    std::string reason = "";
    if (request.params.size() > 3) {
        reason = request.params[3].get_str();
    }
    
    // Build transaction
    std::string error;
    CAmount fee;
    CMutableTransaction mtx = CVM::CVMTransactionBuilder::BuildTrustTransaction(
        pwallet, toAddress, weight, bondAmount, reason, fee, error
    );
    
    if (mtx.vin.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + error);
    }
    
    // Sign transaction
    if (!CVM::CVMTransactionBuilder::SignTransaction(pwallet, mtx, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction: " + error);
    }
    
    // Broadcast to network
    CTransactionRef tx = MakeTransactionRef(mtx);
    uint256 txid;
    if (!CVM::CVMTransactionBuilder::BroadcastTransaction(*tx, txid, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to broadcast transaction: " + error);
    }
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.ToString());
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("bond", ValueFromAmount(bondAmount));
    result.pushKV("weight", weight);
    result.pushKV("to_address", request.params[0].get_str());
    
    return result;
}

/**
 * sendbondedvote - Broadcast bonded reputation vote to network (Phase 3: On-Chain)
 */
UniValue sendbondedvote(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "sendbondedvote \"address\" vote ( bond \"reason\" )\n"
            "\nBroadcast a bonded reputation vote to the network (on-chain).\n"
            "\nArguments:\n"
            "1. \"address\"       (string, required) Address to vote on\n"
            "2. vote             (numeric, required) Vote value (-100 to +100)\n"
            "3. bond             (numeric, optional, default=1.0) CAS to bond\n"
            "4. \"reason\"        (string, optional) Reason for vote\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",       (string) Transaction ID\n"
            "  \"fee\": n.nnnnn,       (numeric) Transaction fee\n"
            "  \"bond\": n.nnnnn       (numeric) Bonded amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("sendbondedvote", "\"QAddress...\" 100 1.5 \"Trustworthy\"")
            + HelpExampleRpc("sendbondedvote", "\"QAddress...\", 100, 1.5, \"Trustworthy\"")
        );
    
    LOCK2(cs_main, pwallet->cs_wallet);
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address");
    }
    
    uint160 targetAddress;
    if (dest.type() == typeid(CKeyID)) {
        targetAddress = uint160(boost::get<CKeyID>(dest));
    } else if (dest.type() == typeid(CScriptID)) {
        targetAddress = uint160(boost::get<CScriptID>(dest));
    } else if (dest.type() == typeid(WitnessV0KeyHash)) {
        // Support bech32 addresses (tcas1q... / cas1q...)
        const WitnessV0KeyHash& wkh = boost::get<WitnessV0KeyHash>(dest);
        targetAddress = uint160(wkh);
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Parse vote value
    int64_t voteInt = 0;
    if (request.params[1].isNum()) {
        voteInt = request.params[1].get_int64();
    } else {
        voteInt = atoi64(request.params[1].get_str());
    }
    
    if (voteInt < -100 || voteInt > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Vote value must be between -100 and +100");
    }
    int16_t voteValue = static_cast<int16_t>(voteInt);
    
    // Parse bond amount
    CAmount bondAmount = 1 * COIN; // Default 1 CAS
    if (request.params.size() > 2) {
        bondAmount = AmountFromValue(request.params[2]);
    }
    
    if (bondAmount < COIN / 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Bond amount must be at least 0.01 CAS");
    }
    
    // Parse reason
    std::string reason = "";
    if (request.params.size() > 3) {
        reason = request.params[3].get_str();
    }
    
    // Build transaction
    std::string error;
    CAmount fee;
    CMutableTransaction mtx = CVM::CVMTransactionBuilder::BuildBondedVoteTransaction(
        pwallet, targetAddress, voteValue, bondAmount, reason, fee, error
    );
    
    if (mtx.vin.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + error);
    }
    
    // Sign transaction
    if (!CVM::CVMTransactionBuilder::SignTransaction(pwallet, mtx, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction: " + error);
    }
    
    // Broadcast to network
    CTransactionRef tx = MakeTransactionRef(mtx);
    uint256 txid;
    if (!CVM::CVMTransactionBuilder::BroadcastTransaction(*tx, txid, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to broadcast transaction: " + error);
    }
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.ToString());
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("bond", ValueFromAmount(bondAmount));
    result.pushKV("vote", voteValue);
    result.pushKV("target_address", request.params[0].get_str());
    
    return result;
}

// ═══════════════════════════════════════════════════════════════
// HAT v2 (Hybrid Adaptive Trust) RPC Commands
// ═══════════════════════════════════════════════════════════════

UniValue getbehaviormetrics(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getbehaviormetrics \"address\"\n"
            "\nGet behavior metrics for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The Cascoin address\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",                (string) Address\n"
            "  \"total_trades\": n,                (numeric) Total trades\n"
            "  \"successful_trades\": n,           (numeric) Successful trades\n"
            "  \"disputed_trades\": n,             (numeric) Disputed trades\n"
            "  \"total_volume\": n.nn,             (numeric) Total trade volume in CAS\n"
            "  \"unique_partners\": n,             (numeric) Number of unique trade partners\n"
            "  \"diversity_score\": n.nn,          (numeric) Partner diversity score (0-1)\n"
            "  \"volume_score\": n.nn,             (numeric) Volume score (0-1)\n"
            "  \"pattern_score\": n.nn,            (numeric) Pattern score (0.5 if suspicious, 1.0 if normal)\n"
            "  \"base_reputation\": n,             (numeric) Base reputation (0-100)\n"
            "  \"final_reputation\": n             (numeric) Final reputation with penalties (0-100)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getbehaviormetrics", "\"QAddress...\"")
            + HelpExampleRpc("getbehaviormetrics", "\"QAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address");
    }
    
    uint160 address;
    if (dest.type() == typeid(CKeyID)) {
        address = uint160(boost::get<CKeyID>(dest));
    } else if (dest.type() == typeid(CScriptID)) {
        address = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Get metrics
    CVM::SecureHAT hat(*CVM::g_cvmdb);
    CVM::BehaviorMetrics metrics = hat.GetBehaviorMetrics(address);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", request.params[0].get_str());
    result.pushKV("total_trades", (uint64_t)metrics.total_trades);
    result.pushKV("successful_trades", (uint64_t)metrics.successful_trades);
    result.pushKV("disputed_trades", (uint64_t)metrics.disputed_trades);
    result.pushKV("total_volume", ValueFromAmount(metrics.total_volume));
    result.pushKV("unique_partners", (uint64_t)metrics.unique_partners.size());
    result.pushKV("diversity_score", metrics.CalculateDiversityScore());
    result.pushKV("volume_score", metrics.CalculateVolumeScore());
    result.pushKV("pattern_score", metrics.DetectSuspiciousPattern());
    result.pushKV("base_reputation", (int)metrics.CalculateBaseReputation());
    result.pushKV("final_reputation", (int)metrics.CalculateFinalReputation());
    
    return result;
}

UniValue getgraphmetrics(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getgraphmetrics \"address\"\n"
            "\nGet graph analysis metrics for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The Cascoin address\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",                   (string) Address\n"
            "  \"in_suspicious_cluster\": true|false, (boolean) If in suspicious cluster\n"
            "  \"mutual_trust_ratio\": n.nn,          (numeric) Mutual trust ratio (0-1)\n"
            "  \"betweenness_centrality\": n.nn,      (numeric) Betweenness centrality (0-1)\n"
            "  \"degree_centrality\": n.nn,           (numeric) Degree centrality (0-1)\n"
            "  \"closeness_centrality\": n.nn         (numeric) Closeness centrality (0-1)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getgraphmetrics", "\"QAddress...\"")
            + HelpExampleRpc("getgraphmetrics", "\"QAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address");
    }
    
    uint160 address;
    if (dest.type() == typeid(CKeyID)) {
        address = uint160(boost::get<CKeyID>(dest));
    } else if (dest.type() == typeid(CScriptID)) {
        address = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Get metrics
    CVM::SecureHAT hat(*CVM::g_cvmdb);
    CVM::GraphMetrics metrics = hat.GetGraphMetrics(address);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", request.params[0].get_str());
    result.pushKV("in_suspicious_cluster", metrics.in_suspicious_cluster);
    result.pushKV("mutual_trust_ratio", metrics.mutual_trust_ratio);
    result.pushKV("betweenness_centrality", metrics.betweenness_centrality);
    result.pushKV("degree_centrality", metrics.degree_centrality);
    result.pushKV("closeness_centrality", metrics.closeness_centrality);
    
    return result;
}

UniValue getsecuretrust(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getsecuretrust \"target\" ( \"viewer\" )\n"
            "\nGet secure HAT v2 trust score.\n"
            "\nArguments:\n"
            "1. \"target\"     (string, required) Target address to evaluate\n"
            "2. \"viewer\"     (string, optional) Viewer address (for WoT personalization)\n"
            "\nResult:\n"
            "{\n"
            "  \"target\": \"xxx\",         (string) Target address\n"
            "  \"viewer\": \"xxx\",         (string) Viewer address\n"
            "  \"trust_score\": n          (numeric) Final trust score (0-100)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getsecuretrust", "\"QAddress...\"")
            + HelpExampleCli("getsecuretrust", "\"QTarget...\" \"QViewer...\"")
            + HelpExampleRpc("getsecuretrust", "\"QTarget...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse target address
    CTxDestination targetDest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(targetDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid target address");
    }
    
    uint160 targetAddress;
    if (targetDest.type() == typeid(CKeyID)) {
        targetAddress = uint160(boost::get<CKeyID>(targetDest));
    } else if (targetDest.type() == typeid(CScriptID)) {
        targetAddress = uint160(boost::get<CScriptID>(targetDest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Target address type not supported");
    }
    
    // Parse viewer address (optional)
    uint160 viewerAddress = targetAddress; // Default to self
    std::string viewerStr = request.params[0].get_str();
    
    if (request.params.size() > 1) {
        CTxDestination viewerDest = DecodeDestination(request.params[1].get_str());
        if (!IsValidDestination(viewerDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewer address");
        }
        
        if (viewerDest.type() == typeid(CKeyID)) {
            viewerAddress = uint160(boost::get<CKeyID>(viewerDest));
        } else if (viewerDest.type() == typeid(CScriptID)) {
            viewerAddress = uint160(boost::get<CScriptID>(viewerDest));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Viewer address type not supported");
        }
        
        viewerStr = request.params[1].get_str();
    }
    
    // Calculate trust
    CVM::SecureHAT hat(*CVM::g_cvmdb);
    int16_t trustScore = hat.CalculateFinalTrust(targetAddress, viewerAddress);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("target", request.params[0].get_str());
    result.pushKV("viewer", viewerStr);
    result.pushKV("trust_score", (int)trustScore);
    
    return result;
}

UniValue gettrustbreakdown(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "gettrustbreakdown \"target\" ( \"viewer\" )\n"
            "\nGet detailed breakdown of HAT v2 trust calculation.\n"
            "\nArguments:\n"
            "1. \"target\"     (string, required) Target address to evaluate\n"
            "2. \"viewer\"     (string, optional) Viewer address (for WoT personalization)\n"
            "\nResult:\n"
            "{\n"
            "  \"target\": \"xxx\",                   (string) Target address\n"
            "  \"viewer\": \"xxx\",                   (string) Viewer address\n"
            "  \"behavior\": {                       (object) Behavior component (40%)\n"
            "    \"base\": n.nn,\n"
            "    \"diversity_penalty\": n.nn,\n"
            "    \"volume_penalty\": n.nn,\n"
            "    \"pattern_penalty\": n.nn,\n"
            "    \"secure_score\": n.nn\n"
            "  },\n"
            "  \"wot\": {                            (object) Web-of-Trust component (30%)\n"
            "    \"base\": n.nn,\n"
            "    \"cluster_penalty\": n.nn,\n"
            "    \"centrality_bonus\": n.nn,\n"
            "    \"secure_score\": n.nn\n"
            "  },\n"
            "  \"economic\": {                       (object) Economic component (20%)\n"
            "    \"base\": n.nn,\n"
            "    \"stake_time_weight\": n.nn,\n"
            "    \"secure_score\": n.nn\n"
            "  },\n"
            "  \"temporal\": {                       (object) Temporal component (10%)\n"
            "    \"base\": n.nn,\n"
            "    \"activity_penalty\": n.nn,\n"
            "    \"secure_score\": n.nn\n"
            "  },\n"
            "  \"final_score\": n                    (numeric) Final trust score (0-100)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettrustbreakdown", "\"QAddress...\"")
            + HelpExampleCli("gettrustbreakdown", "\"QTarget...\" \"QViewer...\"")
            + HelpExampleRpc("gettrustbreakdown", "\"QTarget...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse target address
    CTxDestination targetDest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(targetDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid target address");
    }
    
    uint160 targetAddress;
    if (targetDest.type() == typeid(CKeyID)) {
        targetAddress = uint160(boost::get<CKeyID>(targetDest));
    } else if (targetDest.type() == typeid(CScriptID)) {
        targetAddress = uint160(boost::get<CScriptID>(targetDest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Target address type not supported");
    }
    
    // Parse viewer address (optional)
    uint160 viewerAddress = targetAddress; // Default to self
    std::string viewerStr = request.params[0].get_str();
    
    if (request.params.size() > 1) {
        CTxDestination viewerDest = DecodeDestination(request.params[1].get_str());
        if (!IsValidDestination(viewerDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewer address");
        }
        
        if (viewerDest.type() == typeid(CKeyID)) {
            viewerAddress = uint160(boost::get<CKeyID>(viewerDest));
        } else if (viewerDest.type() == typeid(CScriptID)) {
            viewerAddress = uint160(boost::get<CScriptID>(viewerDest));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Viewer address type not supported");
        }
        
        viewerStr = request.params[1].get_str();
    }
    
    // Calculate trust with breakdown
    CVM::SecureHAT hat(*CVM::g_cvmdb);
    CVM::TrustBreakdown breakdown = hat.CalculateWithBreakdown(targetAddress, viewerAddress);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("target", request.params[0].get_str());
    result.pushKV("viewer", viewerStr);
    
    // Behavior component
    UniValue behavior(UniValue::VOBJ);
    behavior.pushKV("base", breakdown.behavior_base);
    behavior.pushKV("diversity_penalty", breakdown.diversity_penalty);
    behavior.pushKV("volume_penalty", breakdown.volume_penalty);
    behavior.pushKV("pattern_penalty", breakdown.pattern_penalty);
    behavior.pushKV("secure_score", breakdown.secure_behavior);
    result.pushKV("behavior", behavior);
    
    // WoT component
    UniValue wot(UniValue::VOBJ);
    wot.pushKV("base", breakdown.wot_base);
    wot.pushKV("cluster_penalty", breakdown.cluster_penalty);
    wot.pushKV("centrality_bonus", breakdown.centrality_bonus);
    wot.pushKV("secure_score", breakdown.secure_wot);
    result.pushKV("wot", wot);
    
    // Economic component
    UniValue economic(UniValue::VOBJ);
    economic.pushKV("base", breakdown.economic_base);
    economic.pushKV("stake_time_weight", breakdown.stake_time_weight);
    economic.pushKV("secure_score", breakdown.secure_economic);
    result.pushKV("economic", economic);
    
    // Temporal component
    UniValue temporal(UniValue::VOBJ);
    temporal.pushKV("base", breakdown.temporal_base);
    temporal.pushKV("activity_penalty", breakdown.activity_penalty);
    temporal.pushKV("secure_score", breakdown.secure_temporal);
    result.pushKV("temporal", temporal);
    
    // Final score
    result.pushKV("final_score", (int)breakdown.final_score);
    
    return result;
}

UniValue buildwalletclusters(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "buildwalletclusters\n"
            "\nAnalyze blockchain and build wallet clusters based on transaction patterns.\n"
            "This links addresses that belong to the same wallet.\n"
            "\nResult:\n"
            "{\n"
            "  \"total_clusters\": n,                (numeric) Number of identified wallet clusters\n"
            "  \"largest_cluster\": n,               (numeric) Size of largest cluster\n"
            "  \"analyzed_transactions\": n          (numeric) Number of transactions analyzed\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("buildwalletclusters", "")
            + HelpExampleRpc("buildwalletclusters", "")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    CVM::WalletClusterer clusterer(*CVM::g_cvmdb);
    clusterer.BuildClusters();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("total_clusters", (uint64_t)clusterer.GetTotalClusters());
    result.pushKV("largest_cluster", (uint64_t)clusterer.GetLargestClusterSize());
    result.pushKV("status", "Wallet clusters built successfully");
    
    return result;
}

UniValue getwalletcluster(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getwalletcluster \"address\"\n"
            "\nGet all addresses in the same wallet cluster as the given address.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) The address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"cluster_id\": \"address\",          (string) Primary address of cluster\n"
            "  \"member_count\": n,                  (numeric) Number of addresses in cluster\n"
            "  \"members\": [                        (array) All addresses in cluster\n"
            "    \"address\",\n"
            "    ...\n"
            "  ],\n"
            "  \"shared_reputation\": n.n,           (numeric) Minimum reputation across cluster\n"
            "  \"shared_hat_score\": n.n             (numeric) Minimum HAT v2 score across cluster\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getwalletcluster", "\"QAddress...\"")
            + HelpExampleRpc("getwalletcluster", "\"QAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address must be a pubkey hash");
    }
    
    uint160 address(*keyID);
    
    CVM::WalletClusterer clusterer(*CVM::g_cvmdb);
    uint160 cluster_id = clusterer.GetClusterForAddress(address);
    std::set<uint160> members = clusterer.GetClusterMembers(address);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("cluster_id", EncodeDestination(CKeyID(cluster_id)));
    result.pushKV("member_count", (uint64_t)members.size());
    
    UniValue members_arr(UniValue::VARR);
    for (const uint160& member : members) {
        members_arr.push_back(EncodeDestination(CKeyID(member)));
    }
    result.pushKV("members", members_arr);
    
    result.pushKV("shared_reputation", clusterer.GetEffectiveReputation(address));
    result.pushKV("shared_hat_score", clusterer.GetEffectiveHATScore(address));
    
    return result;
}

UniValue geteffectivetrust(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "geteffectivetrust \"target\" ( \"viewer\" )\n"
            "\nGet effective HAT v2 trust score considering wallet clustering.\n"
            "This returns the MINIMUM score across all addresses in the wallet cluster.\n"
            "\nArguments:\n"
            "1. \"target\"     (string, required) The address to evaluate\n"
            "2. \"viewer\"     (string, optional) Viewer address for personalized trust\n"
            "\nResult:\n"
            "{\n"
            "  \"target\": \"address\",              (string) Target address\n"
            "  \"cluster_id\": \"address\",          (string) Wallet cluster ID\n"
            "  \"cluster_size\": n,                  (numeric) Number of addresses in cluster\n"
            "  \"individual_score\": n.n,            (numeric) Score for this address alone\n"
            "  \"effective_score\": n.n,             (numeric) Minimum score across cluster\n"
            "  \"penalty_applied\": true|false       (boolean) Whether cluster penalty was applied\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("geteffectivetrust", "\"QAddress...\"")
            + HelpExampleRpc("geteffectivetrust", "\"QAddress...\", \"QViewer...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Parse target
    CTxDestination target_dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(target_dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid target address");
    }
    
    CKeyID* target_keyID = boost::get<CKeyID>(&target_dest);
    if (!target_keyID) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Target must be a pubkey hash");
    }
    
    uint160 target(*target_keyID);
    uint160 viewer;
    
    if (request.params.size() > 1) {
        CTxDestination viewer_dest = DecodeDestination(request.params[1].get_str());
        if (!IsValidDestination(viewer_dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewer address");
        }
        CKeyID* viewer_keyID = boost::get<CKeyID>(&viewer_dest);
        if (!viewer_keyID) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Viewer must be a pubkey hash");
        }
        viewer = uint160(*viewer_keyID);
    }
    
    CVM::WalletClusterer clusterer(*CVM::g_cvmdb);
    CVM::SecureHAT hat(*CVM::g_cvmdb);
    
    uint160 cluster_id = clusterer.GetClusterForAddress(target);
    std::set<uint160> members = clusterer.GetClusterMembers(target);
    
    double individual_score = hat.CalculateFinalTrust(target, viewer);
    double effective_score = clusterer.GetEffectiveHATScore(target);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("target", EncodeDestination(CKeyID(target)));
    result.pushKV("cluster_id", EncodeDestination(CKeyID(cluster_id)));
    result.pushKV("cluster_size", (uint64_t)members.size());
    result.pushKV("individual_score", individual_score);
    result.pushKV("effective_score", effective_score);
    result.pushKV("penalty_applied", effective_score < individual_score);
    
    // Show which address in cluster has lowest score
    if (members.size() > 1) {
        uint160 worst_address = target;
        double worst_score = individual_score;
        
        for (const uint160& member : members) {
            double member_score = hat.CalculateFinalTrust(member, viewer);
            if (member_score < worst_score) {
                worst_score = member_score;
                worst_address = member;
            }
        }
        
        result.pushKV("worst_address_in_cluster", EncodeDestination(CKeyID(worst_address)));
        result.pushKV("worst_score", worst_score);
    }
    
    return result;
}

UniValue detectclusters(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "detectclusters\n"
            "\nDetect suspicious clusters in the trust graph.\n"
            "\nResult:\n"
            "{\n"
            "  \"suspicious_addresses\": [         (array) Array of suspicious addresses\n"
            "    \"address\",                      (string) Address in suspicious cluster\n"
            "    ...\n"
            "  ],\n"
            "  \"count\": n                        (numeric) Number of suspicious addresses\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("detectclusters", "")
            + HelpExampleRpc("detectclusters", "")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    // Detect clusters
    CVM::GraphAnalyzer analyzer(*CVM::g_cvmdb);
    std::set<uint160> suspicious = analyzer.DetectSuspiciousClusters();
    
    // Build result
    UniValue result(UniValue::VOBJ);
    UniValue addresses(UniValue::VARR);
    
    for (const auto& addr : suspicious) {
        CKeyID keyID(addr);
        addresses.push_back(EncodeDestination(keyID));
    }
    
    result.pushKV("suspicious_addresses", addresses);
    result.pushKV("count", (uint64_t)suspicious.size());
    
    return result;
}

/**
 * listdisputes - List all active DAO disputes
 */
UniValue listdisputes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "listdisputes [status]\n"
            "\nList all DAO disputes.\n"
            "\nArguments:\n"
            "1. status    (string, optional) Filter: 'active', 'resolved', 'all' (default: 'active')\n"
            "\nResult:\n"
            "{\n"
            "  \"disputes\": [              (array) List of disputes\n"
            "    {\n"
            "      \"dispute_id\": \"hash\",\n"
            "      \"original_vote_tx\": \"hash\",\n"
            "      \"challenger\": \"address\",\n"
            "      \"challenge_bond\": n,\n"
            "      \"created_time\": n,\n"
            "      \"resolved\": true|false,\n"
            "      \"slash_decision\": true|false,\n"
            "      \"dao_votes\": n,\n"
            "      \"total_stake_support\": n,\n"
            "      \"total_stake_oppose\": n\n"
            "    }, ...\n"
            "  ],\n"
            "  \"count\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("listdisputes", "")
            + HelpExampleCli("listdisputes", "\"active\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    std::string status = "active";
    if (request.params.size() > 0) {
        status = request.params[0].get_str();
    }
    
    UniValue result(UniValue::VOBJ);
    UniValue disputesArray(UniValue::VARR);
    
    // Get all dispute keys from database (prefix scan)
    std::vector<std::string> keys = CVM::g_cvmdb->ListKeysWithPrefix("dispute_");
    
    int count = 0;
    for (const auto& key : keys) {
        // Skip secondary index entries
        if (key.compare(0, std::string("dispute_by_vote_").size(), "dispute_by_vote_") == 0) {
            continue;
        }
        std::vector<uint8_t> data;
        if (CVM::g_cvmdb->ReadGeneric(key, data)) {
            try {
                CVM::DAODispute dispute;
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ss >> dispute;
                
                // Filter by status
                if (status == "active" && dispute.resolved) continue;
                if (status == "resolved" && !dispute.resolved) continue;
                
                UniValue disputeObj(UniValue::VOBJ);
                disputeObj.pushKV("dispute_id", dispute.disputeId.ToString());
                disputeObj.pushKV("original_vote_tx", dispute.originalVoteTx.ToString());
                disputeObj.pushKV("challenger", EncodeDestination(CKeyID(dispute.challenger)));
                disputeObj.pushKV("challenge_bond", ValueFromAmount(dispute.challengeBond));
                disputeObj.pushKV("challenge_reason", dispute.challengeReason);
                disputeObj.pushKV("created_time", (int64_t)dispute.createdTime);
                disputeObj.pushKV("resolved", dispute.resolved);
                
                if (dispute.resolved) {
                    disputeObj.pushKV("slash_decision", dispute.slashDecision);
                    disputeObj.pushKV("resolved_time", (int64_t)dispute.resolvedTime);
                }
                
                // Calculate vote totals
                CAmount totalStakeSupport = 0;
                CAmount totalStakeOppose = 0;
                for (const auto& vote : dispute.daoVotes) {
                    CAmount stake = dispute.daoStakes.at(vote.first);
                    if (vote.second) {
                        totalStakeSupport += stake;
                    } else {
                        totalStakeOppose += stake;
                    }
                }
                
                disputeObj.pushKV("dao_votes", (int)dispute.daoVotes.size());
                disputeObj.pushKV("total_stake_support", ValueFromAmount(totalStakeSupport));
                disputeObj.pushKV("total_stake_oppose", ValueFromAmount(totalStakeOppose));
                
                disputesArray.push_back(disputeObj);
                count++;
                
            } catch (const std::exception& e) {
                LogPrintf("listdisputes: Failed to deserialize dispute: %s\n", e.what());
            }
        }
    }
    
    result.pushKV("disputes", disputesArray);
    result.pushKV("count", count);
    
    return result;
}

/**
 * getdispute - Get details of a specific dispute
 */
UniValue getdispute(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getdispute \"dispute_id\"\n"
            "\nGet detailed information about a dispute.\n"
            "\nArguments:\n"
            "1. dispute_id    (string, required) The dispute ID (transaction hash)\n"
            "\nResult:\n"
            "{\n"
            "  \"dispute_id\": \"hash\",\n"
            "  \"original_vote_tx\": \"hash\",\n"
            "  \"challenger\": \"address\",\n"
            "  \"dao_votes\": [...],\n"
            "  ...\n"
            "}\n"
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    uint256 disputeId = ParseHashV(request.params[0], "dispute_id");
    
    std::string key = "dispute_" + disputeId.ToString();
    std::vector<uint8_t> data;
    
    if (!CVM::g_cvmdb->ReadGeneric(key, data)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Dispute not found");
    }
    
    CVM::DAODispute dispute;
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> dispute;
    } catch (const std::exception& e) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Failed to deserialize dispute: %s", e.what()));
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("dispute_id", dispute.disputeId.ToString());
    result.pushKV("original_vote_tx", dispute.originalVoteTx.ToString());
    result.pushKV("challenger", EncodeDestination(CKeyID(dispute.challenger)));
    result.pushKV("challenge_bond", ValueFromAmount(dispute.challengeBond));
    result.pushKV("challenge_reason", dispute.challengeReason);
    result.pushKV("created_time", (int64_t)dispute.createdTime);
    result.pushKV("resolved", dispute.resolved);
    
    if (dispute.resolved) {
        result.pushKV("slash_decision", dispute.slashDecision);
        result.pushKV("resolved_time", (int64_t)dispute.resolvedTime);
    }
    
    // DAO votes details
    UniValue votesArray(UniValue::VARR);
    CAmount totalStakeSupport = 0;
    CAmount totalStakeOppose = 0;
    
    for (const auto& vote : dispute.daoVotes) {
        UniValue voteObj(UniValue::VOBJ);
        voteObj.pushKV("dao_member", EncodeDestination(CKeyID(vote.first)));
        voteObj.pushKV("support_slash", vote.second);
        
        CAmount stake = dispute.daoStakes.at(vote.first);
        voteObj.pushKV("stake", ValueFromAmount(stake));
        
        if (vote.second) {
            totalStakeSupport += stake;
        } else {
            totalStakeOppose += stake;
        }
        
        votesArray.push_back(voteObj);
    }
    
    result.pushKV("dao_votes", votesArray);
    result.pushKV("total_stake_support", ValueFromAmount(totalStakeSupport));
    result.pushKV("total_stake_oppose", ValueFromAmount(totalStakeOppose));
    
    // Add reward distribution if dispute is resolved (Requirement 7.4)
    if (dispute.resolved && dispute.rewardsDistributed) {
        CVM::TrustGraph tg(*CVM::g_cvmdb);
        CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
        if (distributor) {
            CVM::RewardDistribution dist = distributor->GetRewardDistribution(disputeId);
            if (dist.IsValid()) {
                UniValue rewardObj(UniValue::VOBJ);
                rewardObj.pushKV("slash_decision", dist.slashDecision);
                rewardObj.pushKV("total_slashed_bond", ValueFromAmount(dist.totalSlashedBond));
                rewardObj.pushKV("challenger_bond_return", ValueFromAmount(dist.challengerBondReturn));
                rewardObj.pushKV("challenger_bounty", ValueFromAmount(dist.challengerBounty));
                rewardObj.pushKV("total_dao_voter_rewards", ValueFromAmount(dist.totalDaoVoterRewards));
                rewardObj.pushKV("burned_amount", ValueFromAmount(dist.burnedAmount));
                rewardObj.pushKV("distributed_time", (int64_t)dist.distributedTime);
                
                // Add voter rewards breakdown
                UniValue voterRewardsObj(UniValue::VOBJ);
                for (const auto& [voter, amount] : dist.voterRewards) {
                    voterRewardsObj.pushKV(EncodeDestination(CKeyID(voter)), ValueFromAmount(amount));
                }
                rewardObj.pushKV("voter_rewards", voterRewardsObj);
                
                result.pushKV("reward_distribution", rewardObj);
            }
        }
    }
    
    return result;
}

/**
 * getpendingrewards - Get all pending rewards for an address
 * 
 * Requirements: 7.1
 */
UniValue getpendingrewards(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getpendingrewards \"address\"\n"
            "\nGet all pending (unclaimed) rewards for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The address to query\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"reward_id\": \"hash\",           (string) Unique reward identifier\n"
            "    \"dispute_id\": \"hash\",          (string) Source dispute ID\n"
            "    \"amount\": n,                    (numeric) Reward amount in CAS\n"
            "    \"type\": \"xxx\",                 (string) Reward type\n"
            "    \"created_time\": n               (numeric) When reward was created\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getpendingrewards", "\"CYourAddress...\"")
            + HelpExampleRpc("getpendingrewards", "\"CYourAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    std::string addressStr = request.params[0].get_str();
    
    // Parse address
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    // Get address hash
    uint160 address;
    if (boost::get<CKeyID>(&dest)) {
        address = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        address = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Get pending rewards
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
    if (!distributor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Reward distributor not initialized");
    }
    
    std::vector<CVM::PendingReward> rewards = distributor->GetPendingRewards(address);
    
    UniValue result(UniValue::VARR);
    for (const auto& reward : rewards) {
        UniValue rewardObj(UniValue::VOBJ);
        rewardObj.pushKV("reward_id", reward.rewardId.GetHex());
        rewardObj.pushKV("dispute_id", reward.disputeId.GetHex());
        rewardObj.pushKV("amount", ValueFromAmount(reward.amount));
        rewardObj.pushKV("type", CVM::RewardTypeToString(reward.type));
        rewardObj.pushKV("created_time", (int64_t)reward.createdTime);
        result.push_back(rewardObj);
    }
    
    return result;
}

/**
 * claimreward - Claim a specific pending reward
 * 
 * Requirements: 7.2, 3.6
 */
UniValue claimreward(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "claimreward \"reward_id\" \"from_address\"\n"
            "\nClaim a specific pending reward.\n"
            "\nArguments:\n"
            "1. \"reward_id\"      (string, required) The reward ID to claim\n"
            "2. \"from_address\"   (string, required) The address claiming the reward\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,           (boolean) Whether claim succeeded\n"
            "  \"amount\": n,                     (numeric) Amount claimed\n"
            "  \"reward_id\": \"hash\",            (string) Reward ID\n"
            "  \"message\": \"xxx\"                (string) Status message\n"
            "}\n"
            "\nNote: The user pays the transaction fee for the claim.\n"
            "\nExamples:\n"
            + HelpExampleCli("claimreward", "\"abc123...\" \"CYourAddress...\"")
            + HelpExampleRpc("claimreward", "\"abc123...\", \"CYourAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    
    // Parse reward ID
    uint256 rewardId = ParseHashV(request.params[0], "reward_id");
    
    // Parse address
    std::string addressStr = request.params[1].get_str();
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    if (boost::get<CKeyID>(&dest)) {
        address = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        address = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Claim the reward
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
    if (!distributor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Reward distributor not initialized");
    }
    
    CAmount claimedAmount = distributor->ClaimReward(rewardId, address);
    
    UniValue result(UniValue::VOBJ);
    if (claimedAmount > 0) {
        result.pushKV("success", true);
        result.pushKV("amount", ValueFromAmount(claimedAmount));
        result.pushKV("reward_id", rewardId.GetHex());
        result.pushKV("message", "Reward claimed successfully");
    } else {
        result.pushKV("success", false);
        result.pushKV("amount", 0);
        result.pushKV("reward_id", rewardId.GetHex());
        result.pushKV("message", "Failed to claim reward - not found, already claimed, or not authorized");
    }
    
    return result;
}

/**
 * claimallrewards - Batch-claim all pending rewards for an address
 * 
 * Requirements: 7.2, 3.7
 */
UniValue claimallrewards(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "claimallrewards \"from_address\"\n"
            "\nBatch-claim all pending rewards for an address in a single transaction.\n"
            "\nArguments:\n"
            "1. \"from_address\"   (string, required) The address claiming the rewards\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,           (boolean) Whether any claims succeeded\n"
            "  \"total_amount\": n,               (numeric) Total amount claimed\n"
            "  \"claimed_count\": n,              (numeric) Number of rewards claimed\n"
            "  \"claimed_rewards\": [             (array) List of claimed reward IDs\n"
            "    \"reward_id\",\n"
            "    ...\n"
            "  ],\n"
            "  \"message\": \"xxx\"                (string) Status message\n"
            "}\n"
            "\nNote: The user pays a single transaction fee for all claims.\n"
            "\nExamples:\n"
            + HelpExampleCli("claimallrewards", "\"CYourAddress...\"")
            + HelpExampleRpc("claimallrewards", "\"CYourAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    
    // Parse address
    std::string addressStr = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    if (boost::get<CKeyID>(&dest)) {
        address = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        address = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Get all pending rewards
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
    if (!distributor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Reward distributor not initialized");
    }
    
    std::vector<CVM::PendingReward> pendingRewards = distributor->GetPendingRewards(address);
    
    // Claim all rewards
    CAmount totalClaimed = 0;
    int claimedCount = 0;
    UniValue claimedIds(UniValue::VARR);
    
    for (const auto& reward : pendingRewards) {
        CAmount amount = distributor->ClaimReward(reward.rewardId, address);
        if (amount > 0) {
            totalClaimed += amount;
            claimedCount++;
            claimedIds.push_back(reward.rewardId.GetHex());
        }
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", claimedCount > 0);
    result.pushKV("total_amount", ValueFromAmount(totalClaimed));
    result.pushKV("claimed_count", claimedCount);
    result.pushKV("claimed_rewards", claimedIds);
    
    if (claimedCount > 0) {
        result.pushKV("message", strprintf("Successfully claimed %d rewards", claimedCount));
    } else if (pendingRewards.empty()) {
        result.pushKV("message", "No pending rewards found for this address");
    } else {
        result.pushKV("message", "Failed to claim any rewards");
    }
    
    return result;
}

/**
 * getrewarddistribution - Get reward distribution details for a resolved dispute
 * 
 * Requirements: 7.3
 */
UniValue getrewarddistribution(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getrewarddistribution \"dispute_id\"\n"
            "\nGet the full reward distribution breakdown for a resolved dispute.\n"
            "\nArguments:\n"
            "1. \"dispute_id\"    (string, required) The dispute ID\n"
            "\nResult:\n"
            "{\n"
            "  \"dispute_id\": \"hash\",                (string) Dispute ID\n"
            "  \"slash_decision\": true|false,         (boolean) Was it a slash decision?\n"
            "  \"total_slashed_bond\": n,              (numeric) Total bond slashed/forfeited\n"
            "  \"challenger_bond_return\": n,          (numeric) Bond returned to challenger\n"
            "  \"challenger_bounty\": n,               (numeric) Bounty paid to challenger\n"
            "  \"total_dao_voter_rewards\": n,         (numeric) Total paid to DAO voters\n"
            "  \"burned_amount\": n,                   (numeric) Amount burned\n"
            "  \"voter_rewards\": {                    (object) Individual voter rewards\n"
            "    \"address\": amount,\n"
            "    ...\n"
            "  },\n"
            "  \"distributed_time\": n                 (numeric) When distribution occurred\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrewarddistribution", "\"abc123...\"")
            + HelpExampleRpc("getrewarddistribution", "\"abc123...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    uint256 disputeId = ParseHashV(request.params[0], "dispute_id");
    
    // Get reward distribution
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
    if (!distributor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Reward distributor not initialized");
    }
    
    CVM::RewardDistribution dist = distributor->GetRewardDistribution(disputeId);
    
    if (!dist.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Reward distribution not found for this dispute");
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("dispute_id", dist.disputeId.GetHex());
    result.pushKV("slash_decision", dist.slashDecision);
    result.pushKV("total_slashed_bond", ValueFromAmount(dist.totalSlashedBond));
    result.pushKV("challenger_bond_return", ValueFromAmount(dist.challengerBondReturn));
    result.pushKV("challenger_bounty", ValueFromAmount(dist.challengerBounty));
    result.pushKV("total_dao_voter_rewards", ValueFromAmount(dist.totalDaoVoterRewards));
    result.pushKV("burned_amount", ValueFromAmount(dist.burnedAmount));
    
    // Add voter rewards breakdown
    UniValue voterRewardsObj(UniValue::VOBJ);
    for (const auto& [voter, amount] : dist.voterRewards) {
        voterRewardsObj.pushKV(EncodeDestination(CKeyID(voter)), ValueFromAmount(amount));
    }
    result.pushKV("voter_rewards", voterRewardsObj);
    
    result.pushKV("distributed_time", (int64_t)dist.distributedTime);
    
    return result;
}

/**
 * commitdisputevote - Submit a vote commitment during commit phase
 * 
 * Requirements: 8.1, 8.7
 */
UniValue commitdisputevote(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() != 4)
        throw std::runtime_error(
            "commitdisputevote \"dispute_id\" \"commitment_hash\" stake \"from_address\"\n"
            "\nSubmit a vote commitment during the commit phase of a dispute.\n"
            "\nThe commitment_hash should be SHA256(vote_byte || nonce) where:\n"
            "  - vote_byte is 0x01 for slash or 0x00 for keep\n"
            "  - nonce is a random 32-byte value you must remember for reveal\n"
            "\nArguments:\n"
            "1. \"dispute_id\"        (string, required) The dispute ID\n"
            "2. \"commitment_hash\"   (string, required) Hash of (vote || nonce)\n"
            "3. stake                (numeric, required) Amount to stake on this vote\n"
            "4. \"from_address\"      (string, required) DAO member address\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,           (boolean) Whether commitment succeeded\n"
            "  \"dispute_id\": \"hash\",           (string) Dispute ID\n"
            "  \"commitment_hash\": \"hash\",      (string) Commitment hash\n"
            "  \"stake\": n,                      (numeric) Stake amount\n"
            "  \"message\": \"xxx\"                (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("commitdisputevote", "\"abc123...\" \"def456...\" 1.0 \"CYourAddress...\"")
            + HelpExampleRpc("commitdisputevote", "\"abc123...\", \"def456...\", 1.0, \"CYourAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    
    // Parse parameters
    uint256 disputeId = ParseHashV(request.params[0], "dispute_id");
    uint256 commitmentHash = ParseHashV(request.params[1], "commitment_hash");
    CAmount stake = AmountFromValue(request.params[2]);
    
    std::string addressStr = request.params[3].get_str();
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 voter;
    if (boost::get<CKeyID>(&dest)) {
        voter = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        voter = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Verify DAO membership
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    if (!tg.IsDAOMember(voter)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address is not a DAO member");
    }
    
    // Get commit-reveal manager
    CVM::CommitRevealManager* crManager = tg.GetCommitRevealManager();
    if (!crManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Commit-reveal manager not initialized");
    }
    
    // Check if in commit phase
    if (!crManager->IsCommitPhase(disputeId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Dispute is not in commit phase");
    }
    
    // Submit commitment
    bool success = crManager->SubmitCommitment(disputeId, voter, commitmentHash, stake);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("dispute_id", disputeId.GetHex());
    result.pushKV("commitment_hash", commitmentHash.GetHex());
    result.pushKV("stake", ValueFromAmount(stake));
    
    if (success) {
        result.pushKV("message", "Commitment submitted successfully");
    } else {
        result.pushKV("message", "Failed to submit commitment - may have already committed");
    }
    
    return result;
}

/**
 * revealdisputevote - Reveal a vote during reveal phase
 * 
 * Requirements: 8.4
 */
UniValue revealdisputevote(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() != 4)
        throw std::runtime_error(
            "revealdisputevote \"dispute_id\" vote \"nonce\" \"from_address\"\n"
            "\nReveal a vote during the reveal phase of a dispute.\n"
            "\nThe vote and nonce must match the commitment hash submitted earlier.\n"
            "\nArguments:\n"
            "1. \"dispute_id\"    (string, required) The dispute ID\n"
            "2. vote             (boolean, required) true=slash, false=keep\n"
            "3. \"nonce\"         (string, required) The nonce used in commitment (32-byte hex)\n"
            "4. \"from_address\"  (string, required) DAO member address\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,           (boolean) Whether reveal succeeded\n"
            "  \"dispute_id\": \"hash\",           (string) Dispute ID\n"
            "  \"vote\": true|false,              (boolean) The revealed vote\n"
            "  \"message\": \"xxx\"                (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("revealdisputevote", "\"abc123...\" true \"def456...\" \"CYourAddress...\"")
            + HelpExampleRpc("revealdisputevote", "\"abc123...\", true, \"def456...\", \"CYourAddress...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    
    // Parse parameters
    uint256 disputeId = ParseHashV(request.params[0], "dispute_id");
    bool vote = request.params[1].get_bool();
    uint256 nonce = ParseHashV(request.params[2], "nonce");
    
    std::string addressStr = request.params[3].get_str();
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 voter;
    if (boost::get<CKeyID>(&dest)) {
        voter = uint160(boost::get<CKeyID>(dest));
    } else if (boost::get<CScriptID>(&dest)) {
        voter = uint160(boost::get<CScriptID>(dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address type not supported");
    }
    
    // Get commit-reveal manager
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::CommitRevealManager* crManager = tg.GetCommitRevealManager();
    if (!crManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Commit-reveal manager not initialized");
    }
    
    // Check if in reveal phase
    if (!crManager->IsRevealPhase(disputeId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Dispute is not in reveal phase");
    }
    
    // Reveal vote
    bool success = crManager->RevealVote(disputeId, voter, vote, nonce);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("dispute_id", disputeId.GetHex());
    result.pushKV("vote", vote);
    
    if (success) {
        result.pushKV("message", "Vote revealed successfully");
    } else {
        result.pushKV("message", "Failed to reveal vote - hash mismatch, no commitment, or already revealed");
    }
    
    return result;
}

/**
 * createdispute - Create a DAO dispute to challenge a bonded vote
 */
UniValue createdispute(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "createdispute \"vote_txid\" [bond] [reason]\n"
            "\nChallenge a bonded vote as malicious. Creates a DAO dispute.\n"
            "\nArguments:\n"
            "1. vote_txid        (string, required) Transaction ID of bonded vote to challenge\n"
            "2. bond             (numeric, optional) Challenge bond amount (default: 1.0 CAS)\n"
            "3. reason           (string, optional) Reason for challenge (max 64 chars)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",\n"
            "  \"dispute_id\": \"hash\",\n"
            "  \"vote_tx\": \"hash\",\n"
            "  \"bond\": n,\n"
            "  \"fee\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("createdispute", "\"abc123...\" 2.0 \"Fake vote\"")
            + HelpExampleCli("createdispute", "\"abc123...\"")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    LOCK2(cs_main, pwallet->cs_wallet);
    
    uint256 voteTxId = ParseHashV(request.params[0], "vote_txid");
    
    // Parse bond amount
    CAmount challengeBond = COIN; // Default 1.0 CAS
    if (request.params.size() > 1 && !request.params[1].isNull()) {
        challengeBond = AmountFromValue(request.params[1]);
    }
    
    if (challengeBond < COIN / 10) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum challenge bond is 0.1 CAS");
    }
    
    // Parse reason
    std::string reason = "";
    if (request.params.size() > 2 && !request.params[2].isNull()) {
        reason = request.params[2].get_str();
    }
    
    // Build on-chain transaction
    std::string error;
    CAmount fee;
    CMutableTransaction mtx = CVM::CVMTransactionBuilder::BuildDisputeTransaction(
        pwallet, voteTxId, challengeBond, reason, fee, error
    );
    
    if (mtx.vin.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + error);
    }
    
    // Sign transaction
    if (!CVM::CVMTransactionBuilder::SignTransaction(pwallet, mtx, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction: " + error);
    }
    
    // Broadcast to network
    CTransactionRef tx = MakeTransactionRef(mtx);
    uint256 txid;
    if (!CVM::CVMTransactionBuilder::BroadcastTransaction(*tx, txid, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to broadcast transaction: " + error);
    }
    
    // The dispute ID is the transaction hash
    uint256 disputeId = txid;

    // Persist dispute immediately (so listdisputes works pre-confirmation)
    try {
        if (CVM::g_cvmdb) {
            CVM::TrustGraph tg(*CVM::g_cvmdb);
            CVM::DAODispute rec;
            rec.disputeId = disputeId;
            rec.originalVoteTx = voteTxId;
            // Best effort: we don't resolve challenger here; the block processor will fill authoritative values
            rec.challenger = uint160();
            rec.challengeBond = challengeBond;
            rec.challengeReason = reason;
            rec.createdTime = GetTime();
            rec.resolved = false;
            tg.CreateDispute(rec);
        }
    } catch (...) {
        // Non-fatal: persistence will still occur on block connect
    }
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.ToString());
    result.pushKV("dispute_id", disputeId.ToString());
    result.pushKV("vote_tx", voteTxId.ToString());
    result.pushKV("bond", ValueFromAmount(challengeBond));
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("status", "Dispute created and broadcast to network");
    
    return result;
}

/**
 * votedispute - Vote on a DAO dispute as a DAO member
 */
UniValue votedispute(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "votedispute \"dispute_id\" support_slash [from_address] [stake]\n"
            "\nVote on a DAO dispute as a DAO member.\n"
            "\nArguments:\n"
            "1. dispute_id       (string, required) The dispute ID\n"
            "2. support_slash    (boolean, required) true/false (also accepts 1/0, yes/no)\n"
            "3. from_address     (string, optional) Any standard address (P2PKH, P2SH, P2WPKH)\n"
            "4. stake            (numeric, optional) Amount of CAS to stake (default: 1.0)\n"
            "\nResult:\n"
            "{\n"
            "  \"dispute_id\": \"hash\",\n"
            "  \"voter\": \"address\",\n"
            "  \"support_slash\": true|false,\n"
            "  \"stake\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("votedispute", "\"abc123...\" true \"CYourAddress...\"")
            + HelpExampleCli("votedispute", "\"abc123...\" false \"CYourAddress...\" 2.5")
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    LOCK2(cs_main, pwallet->cs_wallet);
    
    uint256 disputeId = ParseHashV(request.params[0], "dispute_id");
    // Accept flexible boolean formats: true/false, 1/0, "true"/"false"/"yes"/"no"
    bool supportSlash = false;
    if (request.params[1].isBool()) {
        supportSlash = request.params[1].get_bool();
    } else if (request.params[1].isNum()) {
        supportSlash = request.params[1].get_int() != 0;
    } else if (request.params[1].isStr()) {
        std::string s = request.params[1].get_str();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        supportSlash = (s == "true" || s == "1" || s == "yes" || s == "y");
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "support_slash must be boolean/0-1/yes-no");
    }
    
    // Optional parameters: from_address and stake
    CTxDestination voterDest; bool haveDest = false;
    CAmount stake = COIN;
    if (request.params.size() >= 3 && !request.params[2].isNull()) {
        if (request.params[2].isStr()) {
            std::string s = request.params[2].get_str();
            voterDest = DecodeDestination(s);
            if (IsValidDestination(voterDest)) {
                haveDest = true;
            } else {
                // If not a valid address, accept numeric-as-string stake (e.g. "0.5")
                try {
                    stake = AmountFromValue(request.params[2]);
                } catch (...) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address or stake");
                }
            }
        } else if (request.params[2].isNum()) {
            stake = AmountFromValue(request.params[2]);
        }
    }
    if (request.params.size() >= 4 && !request.params[3].isNull()) {
        stake = AmountFromValue(request.params[3]);
    }
    
    if (stake < COIN / 10) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum stake is 0.1 CAS");
    }
    
    uint160 voterAddress;
    if (haveDest) {
        const CKeyID* keyID = boost::get<CKeyID>(&voterDest);
        const CScriptID* scid = boost::get<CScriptID>(&voterDest);
#if defined(ENABLE_WALLET)
        const WitnessV0KeyHash* wkh = boost::get<WitnessV0KeyHash>(&voterDest);
#else
        const void* wkh = nullptr;
#endif
        if (keyID)       voterAddress = uint160(*keyID);
        else if (scid)   voterAddress = uint160(*scid);
        else if (wkh)    voterAddress = uint160(*wkh);
        else             throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unsupported address type for identity (use P2PKH/P2SH/P2WPKH)");
    } else {
        CPubKey fresh;
        if (!pwallet->GetKeyFromPool(fresh)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get key from wallet");
        }
        voterAddress = fresh.GetID();
        voterDest = CKeyID(fresh.GetID());
    }
    
    // Build on-chain transaction
    std::string error;
    CAmount fee;
    CMutableTransaction mtx = CVM::CVMTransactionBuilder::BuildDisputeVoteTransaction(
        pwallet, disputeId, supportSlash, stake, fee, error
    );
    
    if (mtx.vin.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + error);
    }
    
    // Sign transaction
    if (!CVM::CVMTransactionBuilder::SignTransaction(pwallet, mtx, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction: " + error);
    }
    
    // Broadcast to network
    CTransactionRef tx = MakeTransactionRef(mtx);
    uint256 txid;
    if (!CVM::CVMTransactionBuilder::BroadcastTransaction(*tx, txid, error)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to broadcast transaction: " + error);
    }
    
    // Also record in database (for fast lookups)
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    trustGraph.VoteOnDispute(disputeId, voterAddress, supportSlash, stake);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.ToString());
    result.pushKV("dispute_id", disputeId.ToString());
    result.pushKV("voter", EncodeDestination(voterDest));
    result.pushKV("support_slash", supportSlash);
    result.pushKV("stake", ValueFromAmount(stake));
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("status", "Vote broadcast to network");
    
    return result;
}

/**
 * Get resource statistics for an address
 */
UniValue getresourcestats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getresourcestats \"address\"\n"
            "\nGet resource allocation statistics for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The Cascoin address\n"
            "\nResult:\n"
            "{\n"
            "  \"reputation\": n,                    (numeric) Reputation score (0-100)\n"
            "  \"execution_priority\": {             (object) Execution priority info\n"
            "    \"priority\": n,                    (numeric) Priority score (0-100)\n"
            "    \"can_preempt\": true|false,        (boolean) Can preempt other executions\n"
            "    \"max_execution_time_ms\": n        (numeric) Max execution time in milliseconds\n"
            "  },\n"
            "  \"rate_limit\": {                     (object) Rate limiting info\n"
            "    \"calls_per_minute\": n,            (numeric) Allowed calls per minute\n"
            "    \"current_calls\": n,               (numeric) Current calls in window\n"
            "    \"is_throttled\": true|false        (boolean) Currently throttled\n"
            "  },\n"
            "  \"storage\": {                        (object) Storage quota info\n"
            "    \"quota_bytes\": n,                 (numeric) Storage quota in bytes\n"
            "    \"usage_bytes\": n,                 (numeric) Current usage in bytes\n"
            "    \"available_bytes\": n,             (numeric) Available storage\n"
            "    \"usage_percent\": n                (numeric) Usage percentage\n"
            "  },\n"
            "  \"statistics\": {                     (object) Execution statistics\n"
            "    \"total_executions\": n,            (numeric) Total contract executions\n"
            "    \"total_execution_time_ms\": n,     (numeric) Total execution time\n"
            "    \"avg_execution_time_ms\": n        (numeric) Average execution time\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getresourcestats", "\"CXXXaddress\"")
            + HelpExampleRpc("getresourcestats", "\"CXXXaddress\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address");
    }

    uint160 address = uint160(std::vector<unsigned char>(20, 0));
    if (dest.type() == typeid(CKeyID)) {
        CKeyID keyID = boost::get<CKeyID>(dest);
        memcpy(address.begin(), keyID.begin(), 20);
    } else if (dest.type() == typeid(CScriptID)) {
        CScriptID scriptID = boost::get<CScriptID>(dest);
        memcpy(address.begin(), scriptID.begin(), 20);
    }

    // Get resource manager (create if needed)
    static CVM::ResourceManager resourceManager;
    static bool initialized = false;
    if (!initialized) {
        resourceManager.Initialize(CVM::g_cvmdb.get(), nullptr);  // Storage will be added when available
        initialized = true;
    }

    return resourceManager.GetResourceStats(address);
}

/**
 * Get global resource statistics
 */
UniValue getglobalresourcestats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getglobalresourcestats\n"
            "\nGet global resource allocation statistics.\n"
            "\nResult:\n"
            "{\n"
            "  \"rate_limiting\": {                  (object) Rate limiting statistics\n"
            "    \"total_addresses\": n,             (numeric) Total addresses tracked\n"
            "    \"throttled_addresses\": n,         (numeric) Currently throttled addresses\n"
            "    \"total_calls_current_window\": n   (numeric) Total calls in current window\n"
            "  },\n"
            "  \"execution\": {                      (object) Execution statistics\n"
            "    \"total_executions\": n,            (numeric) Total contract executions\n"
            "    \"total_execution_time_ms\": n,     (numeric) Total execution time\n"
            "    \"avg_execution_time_ms\": n        (numeric) Average execution time\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getglobalresourcestats", "")
            + HelpExampleRpc("getglobalresourcestats", "")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Get resource manager (create if needed)
    static CVM::ResourceManager resourceManager;
    static bool initialized = false;
    if (!initialized) {
        resourceManager.Initialize(CVM::g_cvmdb.get(), nullptr);
        initialized = true;
    }

    return resourceManager.GetGlobalResourceStats();
}

/**
 * Run garbage collection manually
 */
UniValue rungarbagecollection(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "rungarbagecollection\n"
            "\nManually trigger garbage collection for contract cleanup.\n"
            "\nResult:\n"
            "{\n"
            "  \"total_contracts_cleaned\": n,       (numeric) Total contracts cleaned\n"
            "  \"total_storage_cleaned\": n,         (numeric) Total storage entries cleaned\n"
            "  \"total_bytes_reclaimed\": n,         (numeric) Total bytes reclaimed\n"
            "  \"last_cleanup_block\": n,            (numeric) Block height of cleanup\n"
            "  \"low_reputation_cleanups\": n,       (numeric) Low-reputation cleanups\n"
            "  \"inactive_cleanups\": n              (numeric) Inactive contract cleanups\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("rungarbagecollection", "")
            + HelpExampleRpc("rungarbagecollection", "")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Get cleanup manager (create if needed)
    static CVM::CleanupManager cleanupManager;
    static bool initialized = false;
    if (!initialized) {
        cleanupManager.Initialize(CVM::g_cvmdb.get(), nullptr);
        initialized = true;
    }

    // Get current block height
    int64_t currentHeight = chainActive.Height();
    
    // Run garbage collection
    auto stats = cleanupManager.RunGarbageCollection(currentHeight);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("total_contracts_cleaned", stats.totalContractsCleaned);
    result.pushKV("total_storage_cleaned", stats.totalStorageCleaned);
    result.pushKV("total_bytes_reclaimed", stats.totalBytesReclaimed);
    result.pushKV("last_cleanup_block", stats.lastCleanupBlock);
    result.pushKV("low_reputation_cleanups", stats.lowReputationCleanups);
    result.pushKV("inactive_cleanups", stats.inactiveCleanups);
    
    return result;
}

/**
 * Get cleanup statistics
 */
UniValue getcleanupstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getcleanupstats\n"
            "\nGet cleanup statistics.\n"
            "\nResult:\n"
            "{\n"
            "  \"total_contracts_cleaned\": n,       (numeric) Total contracts cleaned\n"
            "  \"total_storage_cleaned\": n,         (numeric) Total storage entries cleaned\n"
            "  \"total_bytes_reclaimed\": n,         (numeric) Total bytes reclaimed\n"
            "  \"last_cleanup_block\": n,            (numeric) Block height of last cleanup\n"
            "  \"low_reputation_cleanups\": n,       (numeric) Low-reputation cleanups\n"
            "  \"inactive_cleanups\": n,             (numeric) Inactive contract cleanups\n"
            "  \"min_reputation_threshold\": n,      (numeric) Minimum reputation threshold\n"
            "  \"cleanup_interval_blocks\": n        (numeric) Blocks between cleanups\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getcleanupstats", "")
            + HelpExampleRpc("getcleanupstats", "")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Get cleanup manager (create if needed)
    static CVM::CleanupManager cleanupManager;
    static bool initialized = false;
    if (!initialized) {
        cleanupManager.Initialize(CVM::g_cvmdb.get(), nullptr);
        initialized = true;
    }

    auto stats = cleanupManager.GetStats();
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("total_contracts_cleaned", stats.totalContractsCleaned);
    result.pushKV("total_storage_cleaned", stats.totalStorageCleaned);
    result.pushKV("total_bytes_reclaimed", stats.totalBytesReclaimed);
    result.pushKV("last_cleanup_block", stats.lastCleanupBlock);
    result.pushKV("low_reputation_cleanups", stats.lowReputationCleanups);
    result.pushKV("inactive_cleanups", stats.inactiveCleanups);
    result.pushKV("min_reputation_threshold", (int)CVM::CleanupManager::GetMinReputationThreshold());
    result.pushKV("cleanup_interval_blocks", 1000);  // Default interval
    
    return result;
}

/**
 * Get contract cleanup info
 */
UniValue getcontractcleanupinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getcontractcleanupinfo \"contractaddress\"\n"
            "\nGet cleanup information for a contract.\n"
            "\nArguments:\n"
            "1. \"contractaddress\"    (string, required) The contract address\n"
            "\nResult:\n"
            "{\n"
            "  \"contract_address\": \"xxx\",        (string) Contract address\n"
            "  \"deployer\": \"xxx\",                (string) Deployer address\n"
            "  \"deployer_reputation\": n,          (numeric) Deployer reputation\n"
            "  \"deployment_block\": n,             (numeric) Deployment block height\n"
            "  \"last_access_block\": n,            (numeric) Last access block height\n"
            "  \"storage_size\": n,                 (numeric) Storage size in bytes\n"
            "  \"is_marked_for_cleanup\": bool,     (boolean) Marked for cleanup\n"
            "  \"inactivity_blocks\": n,            (numeric) Blocks since last access\n"
            "  \"inactivity_threshold\": n          (numeric) Inactivity threshold for cleanup\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getcontractcleanupinfo", "\"CXXXcontractaddress\"")
            + HelpExampleRpc("getcontractcleanupinfo", "\"CXXXcontractaddress\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse contract address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid contract address");
    }

    uint160 contractAddr = uint160(std::vector<unsigned char>(20, 0));
    if (dest.type() == typeid(CKeyID)) {
        CKeyID keyID = boost::get<CKeyID>(dest);
        memcpy(contractAddr.begin(), keyID.begin(), 20);
    } else if (dest.type() == typeid(CScriptID)) {
        CScriptID scriptID = boost::get<CScriptID>(dest);
        memcpy(contractAddr.begin(), scriptID.begin(), 20);
    }

    // Get cleanup manager (create if needed)
    static CVM::CleanupManager cleanupManager;
    static bool initialized = false;
    if (!initialized) {
        cleanupManager.Initialize(CVM::g_cvmdb.get(), nullptr);
        initialized = true;
    }

    auto info = cleanupManager.GetContractInfo(contractAddr);
    int64_t currentHeight = chainActive.Height();
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("contract_address", request.params[0].get_str());
    result.pushKV("deployer", EncodeDestination(CTxDestination(CKeyID(info.deployer))));
    result.pushKV("deployer_reputation", (int)info.deployerReputation);
    result.pushKV("deployment_block", info.deploymentBlock);
    result.pushKV("last_access_block", info.lastAccessBlock);
    result.pushKV("storage_size", info.storageSize);
    result.pushKV("is_marked_for_cleanup", info.isMarkedForCleanup);
    result.pushKV("inactivity_blocks", currentHeight - info.lastAccessBlock);
    result.pushKV("inactivity_threshold", CVM::CleanupManager::GetInactivityThreshold(info.deployerReputation));
    
    return result;
}

/**
 * Trust-Aware RPC Methods (Cascoin-specific)
 * These methods expose reputation and trust features for developers
 */

UniValue cas_getReputationDiscount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_getReputationDiscount \"address\"\n"
            "\nGet gas discount percentage based on address reputation.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"reputation\": n,             (numeric) Current reputation score (0-100)\n"
            "  \"discount_percent\": n,       (numeric) Gas discount percentage (0-50)\n"
            "  \"multiplier\": n,             (numeric) Gas cost multiplier (0.5-1.0)\n"
            "  \"free_gas_eligible\": bool    (boolean) Eligible for free gas (80+ reputation)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getReputationDiscount", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("cas_getReputationDiscount", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get reputation
    auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    uint32_t reputation = trust_ctx->GetReputation(address);
    
    // Calculate discount
    cvm::SustainableGasSystem gasSystem;
    double multiplier = gasSystem.CalculateReputationMultiplier(reputation);
    double discountPercent = (1.0 - multiplier) * 100.0;
    bool freeGasEligible = gasSystem.IsEligibleForFreeGas(reputation);
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("reputation", (int)reputation);
    result.pushKV("discount_percent", discountPercent);
    result.pushKV("multiplier", multiplier);
    result.pushKV("free_gas_eligible", freeGasEligible);
    
    return result;
}

UniValue cas_getFreeGasAllowance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_getFreeGasAllowance \"address\"\n"
            "\nGet detailed free gas allowance information for an address.\n"
            "\nThis complements the existing getgasallowance RPC method with additional details.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"reputation\": n,             (numeric) Current reputation score (0-100)\n"
            "  \"eligible\": bool,            (boolean) Eligible for free gas (80+ reputation)\n"
            "  \"daily_allowance\": n,        (numeric) Daily gas allowance\n"
            "  \"used_today\": n,             (numeric) Gas used today\n"
            "  \"remaining\": n,              (numeric) Gas remaining today\n"
            "  \"renewal_block\": n,          (numeric) Block height of next renewal\n"
            "  \"blocks_until_renewal\": n    (numeric) Blocks until renewal\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getFreeGasAllowance", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("cas_getFreeGasAllowance", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get current block height
    int64_t currentHeight = chainActive.Height();
    
    // Initialize trust context
    auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    
    // Create gas allowance tracker
    CVM::GasAllowanceTracker tracker;
    
    // Get allowance state
    auto state = tracker.GetAllowanceState(address, *trust_ctx, currentHeight);
    
    // Check eligibility
    cvm::SustainableGasSystem gasSystem;
    bool eligible = gasSystem.IsEligibleForFreeGas(state.reputation);
    
    // Calculate renewal info
    int64_t blocksUntilRenewal = (state.lastRenewalBlock + 576) - currentHeight;
    if (blocksUntilRenewal < 0) blocksUntilRenewal = 0;
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("reputation", (int)state.reputation);
    result.pushKV("eligible", eligible);
    result.pushKV("daily_allowance", (int64_t)state.dailyAllowance);
    result.pushKV("used_today", (int64_t)state.usedToday);
    result.pushKV("remaining", (int64_t)(state.dailyAllowance - state.usedToday));
    result.pushKV("renewal_block", state.lastRenewalBlock + 576);
    result.pushKV("blocks_until_renewal", blocksUntilRenewal);
    
    return result;
}

UniValue cas_getGasSubsidy(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "cas_getGasSubsidy \"address\" ( \"poolid\" )\n"
            "\nGet gas subsidy eligibility and available subsidies for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "2. \"poolid\"     (string, optional) Specific pool ID to check\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"reputation\": n,             (numeric) Current reputation score\n"
            "  \"total_subsidies\": n,        (numeric) Total subsidies received\n"
            "  \"pending_rebates\": n,        (numeric) Pending rebates\n"
            "  \"eligible_pools\": [          (array) Pools address is eligible for\n"
            "    {\n"
            "      \"pool_id\": \"xxx\",       (string) Pool identifier\n"
            "      \"remaining\": n,          (numeric) Remaining pool balance\n"
            "      \"min_reputation\": n      (numeric) Minimum reputation required\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getGasSubsidy", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("cas_getGasSubsidy", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\", \"public-good\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get reputation
    auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    uint32_t reputation = trust_ctx->GetReputation(address);
    
    // Create subsidy tracker
    CVM::GasSubsidyTracker tracker;
    tracker.LoadFromDatabase(*CVM::g_cvmdb);
    
    // Get subsidy info
    uint64_t totalSubsidies = tracker.GetTotalSubsidies(address);
    uint64_t pendingRebates = tracker.GetPendingRebates(address);
    
    // Get eligible pools
    UniValue eligiblePools(UniValue::VARR);
    auto allPools = tracker.GetAllPools();
    for (const auto& poolPair : allPools) {
        const auto& pool = poolPair.second;
        if (reputation >= pool.minReputation) {
            UniValue poolObj(UniValue::VOBJ);
            poolObj.pushKV("pool_id", poolPair.first);
            poolObj.pushKV("remaining", (int64_t)pool.remaining);
            poolObj.pushKV("min_reputation", (int)pool.minReputation);
            eligiblePools.push_back(poolObj);
        }
    }
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("reputation", (int)reputation);
    result.pushKV("total_subsidies", (int64_t)totalSubsidies);
    result.pushKV("pending_rebates", (int64_t)pendingRebates);
    result.pushKV("eligible_pools", eligiblePools);
    
    return result;
}

UniValue cas_getTrustContext(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_getTrustContext \"address\"\n"
            "\nGet comprehensive trust context information for an address.\n"
            "\nThis includes reputation, trust paths, and historical data.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) Address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address\n"
            "  \"reputation\": n,             (numeric) Current reputation score (0-100)\n"
            "  \"raw_score\": n,              (numeric) Raw reputation score (-10000 to +10000)\n"
            "  \"trust_paths\": n,            (numeric) Number of trust paths\n"
            "  \"last_updated\": n,           (numeric) Last update timestamp\n"
            "  \"activity_score\": n,         (numeric) Activity-based score\n"
            "  \"decay_applied\": bool,       (boolean) Whether decay has been applied\n"
            "  \"cross_chain\": bool          (boolean) Has cross-chain attestations\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getTrustContext", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("cas_getTrustContext", "\"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse address
    std::string addressStr = request.params[0].get_str();
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get trust context
    auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    uint32_t reputation = trust_ctx->GetReputation(address);
    
    // Get raw reputation score from database
    CVM::ReputationSystem repSystem(*CVM::g_cvmdb);
    CVM::ReputationScore score;
    repSystem.GetReputation(address, score);
    
    // Get trust graph info
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    auto trustPaths = trustGraph.FindTrustPaths(address, address, 3);  // Max depth 3
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addressStr);
    result.pushKV("reputation", (int)reputation);
    result.pushKV("raw_score", score.score);
    result.pushKV("trust_paths", (int)trustPaths.size());
    result.pushKV("last_updated", score.lastUpdated);
    result.pushKV("activity_score", (int)score.totalTransactions);
    result.pushKV("decay_applied", score.lastUpdated < (GetTime() - 86400));  // 1 day
    result.pushKV("cross_chain", false);  // TODO: Implement when cross-chain is ready
    
    return result;
}

UniValue cas_estimateGasWithReputation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "cas_estimateGasWithReputation gaslimit \"address\" ( \"operation\" )\n"
            "\nEstimate gas cost with reputation-based discounts applied.\n"
            "\nArguments:\n"
            "1. gaslimit      (numeric, required) Estimated gas limit\n"
            "2. \"address\"     (string, required) Address for reputation discount\n"
            "3. \"operation\"   (string, optional) Operation type (\"deployment\", \"call\", \"storage\")\n"
            "\nResult:\n"
            "{\n"
            "  \"gas_limit\": n,              (numeric) Gas limit\n"
            "  \"base_cost\": n,              (numeric) Base cost without discount (satoshis)\n"
            "  \"reputation\": n,             (numeric) Address reputation\n"
            "  \"discount_percent\": n,       (numeric) Discount percentage\n"
            "  \"discounted_cost\": n,        (numeric) Cost with discount (satoshis)\n"
            "  \"free_gas_eligible\": bool,   (boolean) Eligible for free gas\n"
            "  \"final_cost\": n              (numeric) Final cost (0 if free gas eligible)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_estimateGasWithReputation", "100000 \"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\"")
            + HelpExampleRpc("cas_estimateGasWithReputation", "100000, \"DXG7YxPgz8vPKpEj9ZfU5nQRh6oM2\", \"deployment\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse gas limit
    uint64_t gasLimit = request.params[0].get_int64();
    
    // Parse address
    std::string addressStr = request.params[1].get_str();
    std::vector<unsigned char> addressData;
    if (!DecodeBase58(addressStr.c_str(), addressData) || addressData.size() != 25) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    uint160 address;
    memcpy(address.begin(), &addressData[1], 20);
    
    // Get reputation
    auto trust_ctx = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    uint32_t reputation = trust_ctx->GetReputation(address);
    
    // Calculate costs
    cvm::SustainableGasSystem gasSystem;
    
    // Base gas price (0.2 gwei = 200000000 wei = 20 satoshis per gas unit)
    uint64_t baseGasPrice = 20;  // satoshis per gas unit
    uint64_t baseCost = gasLimit * baseGasPrice;
    
    // Apply reputation discount
    double multiplier = gasSystem.CalculateReputationMultiplier(reputation);
    double discountPercent = (1.0 - multiplier) * 100.0;
    uint64_t discountedCost = baseCost * multiplier;
    
    // Check free gas eligibility
    bool freeGasEligible = gasSystem.IsEligibleForFreeGas(reputation);
    uint64_t finalCost = freeGasEligible ? 0 : discountedCost;
    
    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("gas_limit", (int64_t)gasLimit);
    result.pushKV("base_cost", (int64_t)baseCost);
    result.pushKV("reputation", (int)reputation);
    result.pushKV("discount_percent", discountPercent);
    result.pushKV("discounted_cost", (int64_t)discountedCost);
    result.pushKV("free_gas_eligible", freeGasEligible);
    result.pushKV("final_cost", (int64_t)finalCost);
    
    return result;
}

/**
 * HAT v2 Validator Key Management RPC Commands
 * 
 * IMPORTANT: There is NO manual validator registration!
 * 
 * Validators are automatically selected via deterministic random algorithm when:
 * - Reputation >= 70
 * - Bonded stake >= 1 CAS
 * - Selected by SelectRandomValidators() based on tx hash + block height
 * 
 * These commands ONLY manage the validator signing key:
 * - Generate/import/export validator keys
 * - Query validator statistics
 * - Monitor validation performance
 * 
 * To become a validator:
 * 1. Generate a validator key (generatevalidatorkey)
 * 2. Build reputation >= 70 (via trust relationships)
 * 3. Bond stake >= 1 CAS (via addtrust RPC)
 * 4. Wait to be randomly selected for validations
 */

UniValue generatevalidatorkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "generatevalidatorkey\n"
            "\nGenerate a new validator key for HAT v2 consensus participation.\n"
            "\nThis creates a new private key that will be used to sign validation responses.\n"
            "The key is automatically saved to the data directory.\n"
            "\nNOTE: This does NOT register you as a validator! Validators are automatically\n"
            "selected when they meet requirements (reputation >= 70, stake >= 1 CAS).\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",      (string) Validator address (hex)\n"
            "  \"pubkey\": \"xxx\",       (string) Validator public key (hex)\n"
            "  \"keyfile\": \"xxx\"       (string) Path to key file\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("generatevalidatorkey", "")
            + HelpExampleRpc("generatevalidatorkey", "")
        );

    if (!CVM::g_validatorKeys) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Validator key manager not initialized");
    }

    if (!CVM::g_validatorKeys->GenerateNewKey()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to generate validator key");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", CVM::g_validatorKeys->GetValidatorAddress().ToString());
    result.pushKV("pubkey", HexStr(CVM::g_validatorKeys->GetValidatorPubKey()));
    result.pushKV("keyfile", "validator.key (in data directory)");

    LogPrintf("Validator: Generated new validator key with address %s\n",
              CVM::g_validatorKeys->GetValidatorAddress().ToString());

    return result;
}

UniValue getvalidatorinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getvalidatorinfo\n"
            "\nGet information about the current validator configuration.\n"
            "\nResult:\n"
            "{\n"
            "  \"haskey\": true|false,        (boolean) Whether validator key is loaded\n"
            "  \"address\": \"xxx\",            (string) Validator address (if key loaded)\n"
            "  \"pubkey\": \"xxx\",             (string) Validator public key (if key loaded)\n"
            "  \"eligible\": true|false,      (boolean) Whether eligible to validate\n"
            "  \"reputation\": n,             (numeric) Current reputation score\n"
            "  \"stake\": n,                  (numeric) Total bonded stake (CAS)\n"
            "  \"validations\": n,            (numeric) Total validations performed\n"
            "  \"accuracy\": n.nn,            (numeric) Validation accuracy percentage\n"
            "  \"lastValidation\": n          (numeric) Block height of last validation\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getvalidatorinfo", "")
            + HelpExampleRpc("getvalidatorinfo", "")
        );

    if (!CVM::g_validatorKeys) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Validator key manager not initialized");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("haskey", CVM::g_validatorKeys->HasValidatorKey());

    if (CVM::g_validatorKeys->HasValidatorKey()) {
        uint160 validatorAddr = CVM::g_validatorKeys->GetValidatorAddress();
        result.pushKV("address", validatorAddr.ToString());
        result.pushKV("pubkey", HexStr(CVM::g_validatorKeys->GetValidatorPubKey()));

        // Get validator statistics from database
        try {
            // Create database instance
            CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
            
            // Create required components
            CVM::TrustGraph trustGraph(db);
            CVM::SecureHAT secureHAT(db);
            CVM::HATConsensusValidator validator(db, secureHAT, trustGraph);
            
            // Get reputation
            CVM::ReputationSystem repSystem(db);
            CVM::ReputationScore repScore;
            bool hasRep = repSystem.GetReputation(validatorAddr, repScore);
            result.pushKV("reputation", hasRep ? repScore.score : 0);
            result.pushKV("eligible", hasRep && repScore.score >= 50);

            // Get bonded stake
            std::vector<CVM::TrustEdge> outgoing = trustGraph.GetOutgoingTrust(validatorAddr);
            CAmount totalStake = 0;
            for (const auto& edge : outgoing) {
                if (!edge.slashed) {
                    totalStake += edge.bondAmount;
                }
            }
            result.pushKV("stake", ValueFromAmount(totalStake));

            // Get validator stats
            CVM::ValidatorStats stats = validator.GetValidatorStats(validatorAddr);
            result.pushKV("validations", (int64_t)stats.totalValidations);
            result.pushKV("accuracy", stats.accuracyRate);
            result.pushKV("lastActivity", stats.lastActivityTime);
        } catch (const std::exception& e) {
            result.pushKV("error", std::string("Failed to get validator stats: ") + e.what());
        }
    }

    return result;
}

UniValue importvalidatorkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "importvalidatorkey \"privkey\"\n"
            "\nImport a validator private key.\n"
            "\nWARNING: This will replace any existing validator key!\n"
            "\nArguments:\n"
            "1. privkey    (string, required) Private key in hex format (64 characters)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",      (string) Validator address\n"
            "  \"pubkey\": \"xxx\"        (string) Validator public key (hex)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("importvalidatorkey", "\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"")
            + HelpExampleRpc("importvalidatorkey", "\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"")
        );

    if (!CVM::g_validatorKeys) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Validator key manager not initialized");
    }

    std::string keyHex = request.params[0].get_str();

    // Validate hex format
    if (keyHex.length() != 64) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Private key must be 64 hex characters (32 bytes)");
    }

    if (!IsHex(keyHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Private key must be valid hex");
    }

    if (!CVM::g_validatorKeys->ImportKey(keyHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to import validator key (invalid key)");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", CVM::g_validatorKeys->GetValidatorAddress().ToString());
    result.pushKV("pubkey", HexStr(CVM::g_validatorKeys->GetValidatorPubKey()));

    LogPrintf("Validator: Imported validator key with address %s\n",
              CVM::g_validatorKeys->GetValidatorAddress().ToString());

    return result;
}

UniValue exportvalidatorkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "exportvalidatorkey\n"
            "\nExport the validator private key.\n"
            "\nWARNING: Keep this key secure! Anyone with this key can sign validation responses as you.\n"
            "\nResult:\n"
            "\"privkey\"    (string) Private key in hex format\n"
            "\nExamples:\n"
            + HelpExampleCli("exportvalidatorkey", "")
            + HelpExampleRpc("exportvalidatorkey", "")
        );

    if (!CVM::g_validatorKeys) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Validator key manager not initialized");
    }

    if (!CVM::g_validatorKeys->HasValidatorKey()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "No validator key loaded");
    }

    std::string keyHex;
    if (!CVM::g_validatorKeys->ExportKey(keyHex)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to export validator key");
    }

    return keyHex;
}

UniValue loadvalidatorfromwallet(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "loadvalidatorfromwallet \"address\"\n"
            "\nLoad a validator key from the wallet.\n"
            "\nThe address must exist in the wallet with its private key.\n"
            "\nArguments:\n"
            "1. address    (string, required) Address to use as validator (hex format)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",      (string) Validator address\n"
            "  \"pubkey\": \"xxx\"        (string) Validator public key (hex)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("loadvalidatorfromwallet", "\"1234567890abcdef1234567890abcdef12345678\"")
            + HelpExampleRpc("loadvalidatorfromwallet", "\"1234567890abcdef1234567890abcdef12345678\"")
        );

    if (!CVM::g_validatorKeys) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Validator key manager not initialized");
    }

    std::string addrStr = request.params[0].get_str();
    
    // Parse address
    if (addrStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be 40 hex characters (20 bytes)");
    }

    if (!IsHex(addrStr)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be valid hex");
    }

    std::vector<uint8_t> addrData = ParseHex(addrStr);
    uint160 addr;
    memcpy(addr.begin(), addrData.data(), 20);

    if (!CVM::g_validatorKeys->LoadFromWallet(addr)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to load key from wallet (address not found or no private key)");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", CVM::g_validatorKeys->GetValidatorAddress().ToString());
    result.pushKV("pubkey", HexStr(CVM::g_validatorKeys->GetValidatorPubKey()));

    LogPrintf("Validator: Loaded validator key from wallet with address %s\n",
              CVM::g_validatorKeys->GetValidatorAddress().ToString());

    return result;
}

UniValue listvalidators(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "listvalidators ( minreputation )\n"
            "\nList all eligible validators in the network.\n"
            "\nValidators are automatically eligible when they meet requirements:\n"
            "- Reputation >= 70\n"
            "- Bonded stake >= 1 CAS\n"
            "- Have a validator key configured\n"
            "\nArguments:\n"
            "1. minreputation    (numeric, optional, default=70) Minimum reputation to include\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",        (string) Validator address\n"
            "    \"reputation\": n,         (numeric) Reputation score\n"
            "    \"stake\": n,              (numeric) Bonded stake (CAS)\n"
            "    \"validations\": n,        (numeric) Total validations\n"
            "    \"accuracy\": n.nn,        (numeric) Accuracy percentage\n"
            "    \"eligible\": true|false   (boolean) Currently eligible\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listvalidators", "")
            + HelpExampleCli("listvalidators", "80")
            + HelpExampleRpc("listvalidators", "80")
        );

    int16_t minReputation = 70;
    if (request.params.size() > 0) {
        minReputation = request.params[0].get_int();
    }

    // Create database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
    
    // Create required components
    CVM::TrustGraph trustGraph(db);
    CVM::SecureHAT secureHAT(db);
    CVM::HATConsensusValidator validator(db, secureHAT, trustGraph);
    CVM::ReputationSystem repSystem(db);

    // Get all addresses with reputation >= minReputation
    // Note: GetAddressesAboveReputation doesn't exist, so we'll get all addresses with reputation
    std::vector<uint160> addresses;
    // TODO: Implement proper address enumeration from reputation system
    // For now, return empty list
    
    UniValue result(UniValue::VARR);

    for (const auto& addr : addresses) {
        UniValue validatorInfo(UniValue::VOBJ);
        validatorInfo.pushKV("address", addr.ToString());

        // Get reputation
        CVM::ReputationScore repScore;
        bool hasRep = repSystem.GetReputation(addr, repScore);
        if (!hasRep || repScore.score < minReputation) {
            continue;
        }
        validatorInfo.pushKV("reputation", repScore.score);

        // Get bonded stake
        std::vector<CVM::TrustEdge> outgoing = trustGraph.GetOutgoingTrust(addr);
        CAmount totalStake = 0;
        for (const auto& edge : outgoing) {
            if (!edge.slashed) {
                totalStake += edge.bondAmount;
            }
        }
        validatorInfo.pushKV("stake", ValueFromAmount(totalStake));

        // Get validator stats
        CVM::ValidatorStats stats = validator.GetValidatorStats(addr);
        validatorInfo.pushKV("validations", (int64_t)stats.totalValidations);
        validatorInfo.pushKV("accuracy", stats.accuracyRate);

        validatorInfo.pushKV("eligible", repScore.score >= 50);

        result.push_back(validatorInfo);
    }

    return result;
}

UniValue getvalidatorearnings(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getvalidatorearnings \"address\" ( blockcount )\n"
            "\nGet earnings information for a validator.\n"
            "\nShows the validator's share of gas fees from the 70/30 split:\n"
            "- 70% of gas fees go to miners\n"
            "- 30% of gas fees are split among validators\n"
            "\nArguments:\n"
            "1. address      (string, required) Validator address (hex)\n"
            "2. blockcount   (numeric, optional, default=1000) Number of recent blocks to analyze\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",                (string) Validator address\n"
            "  \"totalEarnings\": n,              (numeric) Total earnings (CAS)\n"
            "  \"transactionsValidated\": n,      (numeric) Number of transactions validated\n"
            "  \"blocksParticipated\": n,         (numeric) Number of blocks participated in\n"
            "  \"averageEarningsPerBlock\": n,    (numeric) Average earnings per block\n"
            "  \"averageEarningsPerTx\": n,       (numeric) Average earnings per transaction\n"
            "  \"recentEarnings\": [              (array) Recent earnings by block\n"
            "    {\n"
            "      \"blockHeight\": n,            (numeric) Block height\n"
            "      \"blockHash\": \"xxx\",          (string) Block hash\n"
            "      \"earnings\": n,               (numeric) Earnings in this block\n"
            "      \"txCount\": n                 (numeric) Transactions validated\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getvalidatorearnings", "\"1234567890abcdef1234567890abcdef12345678\"")
            + HelpExampleCli("getvalidatorearnings", "\"1234567890abcdef1234567890abcdef12345678\" 500")
            + HelpExampleRpc("getvalidatorearnings", "\"1234567890abcdef1234567890abcdef12345678\", 500")
        );

    std::string addrStr = request.params[0].get_str();
    int blockCount = 1000;
    if (request.params.size() > 1) {
        blockCount = request.params[1].get_int();
        if (blockCount < 1 || blockCount > 10000) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block count must be between 1 and 10000");
        }
    }

    // Parse address
    if (addrStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be 40 hex characters");
    }

    if (!IsHex(addrStr)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be valid hex");
    }

    std::vector<uint8_t> addrData = ParseHex(addrStr);
    uint160 validatorAddr;
    memcpy(validatorAddr.begin(), addrData.data(), 20);

    // Get database instance
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addrStr);

    CAmount totalEarnings = 0;
    int transactionsValidated = 0;
    int blocksParticipated = 0;
    UniValue recentEarnings(UniValue::VARR);

    // Scan recent blocks for validator participation
    LOCK(cs_main);
    
    int currentHeight = chainActive.Height();
    int startHeight = std::max(0, currentHeight - blockCount);

    for (int height = currentHeight; height >= startHeight; height--) {
        CBlockIndex* pindex = chainActive[height];
        if (!pindex) continue;

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            continue;
        }

        CAmount blockEarnings = 0;
        int blockTxCount = 0;

        // Check each transaction in the block for validator participation
        for (size_t i = 1; i < block.vtx.size(); i++) {
            const CTransaction& tx = *block.vtx[i];
            
            CVM::TransactionValidationRecord record;
            if (CVM::g_cvmdb->GetValidatorParticipation(tx.GetHash(), record)) {
                // Check if this validator participated
                for (const uint160& validator : record.validators) {
                    if (validator == validatorAddr) {
                        // Calculate this validator's share
                        // Using placeholder gas values - in production, get from receipt
                        uint64_t gasUsed = 21000;
                        CAmount gasPrice = 1000;
                        GasFeeDistribution dist = CalculateGasFeeDistribution(
                            gasUsed, gasPrice, record.validators);
                        
                        blockEarnings += dist.perValidatorShare;
                        blockTxCount++;
                        transactionsValidated++;
                        break;
                    }
                }
            }
        }

        if (blockEarnings > 0) {
            blocksParticipated++;
            totalEarnings += blockEarnings;

            // Add to recent earnings (limit to 100 entries)
            if (recentEarnings.size() < 100) {
                UniValue blockInfo(UniValue::VOBJ);
                blockInfo.pushKV("blockHeight", height);
                blockInfo.pushKV("blockHash", pindex->GetBlockHash().GetHex());
                blockInfo.pushKV("earnings", ValueFromAmount(blockEarnings));
                blockInfo.pushKV("txCount", blockTxCount);
                recentEarnings.push_back(blockInfo);
            }
        }
    }

    result.pushKV("totalEarnings", ValueFromAmount(totalEarnings));
    result.pushKV("transactionsValidated", transactionsValidated);
    result.pushKV("blocksParticipated", blocksParticipated);
    
    if (blocksParticipated > 0) {
        result.pushKV("averageEarningsPerBlock", ValueFromAmount(totalEarnings / blocksParticipated));
    } else {
        result.pushKV("averageEarningsPerBlock", ValueFromAmount(0));
    }
    
    if (transactionsValidated > 0) {
        result.pushKV("averageEarningsPerTx", ValueFromAmount(totalEarnings / transactionsValidated));
    } else {
        result.pushKV("averageEarningsPerTx", ValueFromAmount(0));
    }
    
    result.pushKV("recentEarnings", recentEarnings);

    return result;
}

UniValue getnetworkvalidatorstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getnetworkvalidatorstats ( blockcount )\n"
            "\nGet network-wide validator statistics.\n"
            "\nShows aggregate statistics for all validators in the network.\n"
            "\nArguments:\n"
            "1. blockcount   (numeric, optional, default=1000) Number of recent blocks to analyze\n"
            "\nResult:\n"
            "{\n"
            "  \"totalValidators\": n,            (numeric) Total number of validators\n"
            "  \"activeValidators\": n,           (numeric) Validators active in period\n"
            "  \"totalGasFees\": n,               (numeric) Total gas fees collected\n"
            "  \"minerShare\": n,                 (numeric) Total miner share (70%)\n"
            "  \"validatorShare\": n,             (numeric) Total validator share (30%)\n"
            "  \"transactionsValidated\": n,      (numeric) Total transactions validated\n"
            "  \"averageValidatorsPerTx\": n.nn,  (numeric) Average validators per transaction\n"
            "  \"topValidators\": [               (array) Top validators by earnings\n"
            "    {\n"
            "      \"address\": \"xxx\",            (string) Validator address\n"
            "      \"earnings\": n,               (numeric) Total earnings\n"
            "      \"txCount\": n                 (numeric) Transactions validated\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getnetworkvalidatorstats", "")
            + HelpExampleCli("getnetworkvalidatorstats", "500")
            + HelpExampleRpc("getnetworkvalidatorstats", "500")
        );

    int blockCount = 1000;
    if (request.params.size() > 0) {
        blockCount = request.params[0].get_int();
        if (blockCount < 1 || blockCount > 10000) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block count must be between 1 and 10000");
        }
    }

    // Get database instance
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    UniValue result(UniValue::VOBJ);

    std::map<uint160, CAmount> validatorEarnings;
    std::map<uint160, int> validatorTxCounts;
    std::set<uint160> activeValidators;
    CAmount totalGasFees = 0;
    CAmount totalMinerShare = 0;
    CAmount totalValidatorShare = 0;
    int totalTransactionsValidated = 0;
    int totalValidatorParticipations = 0;

    // Scan recent blocks for validator participation
    LOCK(cs_main);
    
    int currentHeight = chainActive.Height();
    int startHeight = std::max(0, currentHeight - blockCount);

    for (int height = currentHeight; height >= startHeight; height--) {
        CBlockIndex* pindex = chainActive[height];
        if (!pindex) continue;

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            continue;
        }

        // Check each transaction in the block for validator participation
        for (size_t i = 1; i < block.vtx.size(); i++) {
            const CTransaction& tx = *block.vtx[i];
            
            CVM::TransactionValidationRecord record;
            if (CVM::g_cvmdb->GetValidatorParticipation(tx.GetHash(), record)) {
                // Using placeholder gas values - in production, get from receipt
                uint64_t gasUsed = 21000;
                CAmount gasPrice = 1000;
                GasFeeDistribution dist = CalculateGasFeeDistribution(
                    gasUsed, gasPrice, record.validators);
                
                totalGasFees += dist.totalGasFee;
                totalMinerShare += dist.minerShare;
                totalValidatorShare += dist.validatorShare;
                totalTransactionsValidated++;
                totalValidatorParticipations += record.validators.size();

                // Track per-validator earnings
                for (const uint160& validator : record.validators) {
                    activeValidators.insert(validator);
                    validatorEarnings[validator] += dist.perValidatorShare;
                    validatorTxCounts[validator]++;
                }
            }
        }
    }

    result.pushKV("totalValidators", (int)activeValidators.size());
    result.pushKV("activeValidators", (int)activeValidators.size());
    result.pushKV("totalGasFees", ValueFromAmount(totalGasFees));
    result.pushKV("minerShare", ValueFromAmount(totalMinerShare));
    result.pushKV("validatorShare", ValueFromAmount(totalValidatorShare));
    result.pushKV("transactionsValidated", totalTransactionsValidated);
    
    if (totalTransactionsValidated > 0) {
        result.pushKV("averageValidatorsPerTx", 
            (double)totalValidatorParticipations / totalTransactionsValidated);
    } else {
        result.pushKV("averageValidatorsPerTx", 0.0);
    }

    // Sort validators by earnings and get top 10
    std::vector<std::pair<uint160, CAmount>> sortedValidators(
        validatorEarnings.begin(), validatorEarnings.end());
    std::sort(sortedValidators.begin(), sortedValidators.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    UniValue topValidators(UniValue::VARR);
    int count = 0;
    for (const auto& [validator, earnings] : sortedValidators) {
        if (count >= 10) break;
        
        UniValue validatorInfo(UniValue::VOBJ);
        validatorInfo.pushKV("address", validator.ToString());
        validatorInfo.pushKV("earnings", ValueFromAmount(earnings));
        validatorInfo.pushKV("txCount", validatorTxCounts[validator]);
        topValidators.push_back(validatorInfo);
        count++;
    }
    result.pushKV("topValidators", topValidators);

    return result;
}

UniValue getvalidatorstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getvalidatorstats \"address\"\n"
            "\nGet detailed statistics for a specific validator.\n"
            "\nArguments:\n"
            "1. address    (string, required) Validator address (hex)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",              (string) Validator address\n"
            "  \"reputation\": n,               (numeric) Current reputation\n"
            "  \"stake\": n,                    (numeric) Total bonded stake\n"
            "  \"totalValidations\": n,         (numeric) Total validations performed\n"
            "  \"correctValidations\": n,       (numeric) Correct validations\n"
            "  \"incorrectValidations\": n,     (numeric) Incorrect validations\n"
            "  \"accuracy\": n.nn,              (numeric) Accuracy percentage\n"
            "  \"lastValidation\": n,           (numeric) Block height of last validation\n"
            "  \"fraudRecords\": n,             (numeric) Number of fraud records\n"
            "  \"slashedStake\": n,             (numeric) Total slashed stake\n"
            "  \"rewardsEarned\": n             (numeric) Total rewards earned\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getvalidatorstats", "\"1234567890abcdef1234567890abcdef12345678\"")
            + HelpExampleRpc("getvalidatorstats", "\"1234567890abcdef1234567890abcdef12345678\"")
        );

    std::string addrStr = request.params[0].get_str();
    
    // Parse address
    if (addrStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be 40 hex characters");
    }

    std::vector<uint8_t> addrData = ParseHex(addrStr);
    uint160 addr;
    memcpy(addr.begin(), addrData.data(), 20);

    // Create database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
    
    // Create required components
    CVM::TrustGraph trustGraph(db);
    CVM::SecureHAT secureHAT(db);
    CVM::HATConsensusValidator validator(db, secureHAT, trustGraph);
    CVM::ReputationSystem repSystem(db);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addr.ToString());

    // Get reputation
    CVM::ReputationScore repScore;
    bool hasRep = repSystem.GetReputation(addr, repScore);
    result.pushKV("reputation", hasRep ? repScore.score : 0);

    // Get bonded stake
    std::vector<CVM::TrustEdge> outgoing = trustGraph.GetOutgoingTrust(addr);
    CAmount totalStake = 0;
    CAmount slashedStake = 0;
    for (const auto& edge : outgoing) {
        if (edge.slashed) {
            slashedStake += edge.bondAmount;
        } else {
            totalStake += edge.bondAmount;
        }
    }
    result.pushKV("stake", ValueFromAmount(totalStake));
    result.pushKV("slashedStake", ValueFromAmount(slashedStake));

    // Get validator stats
    CVM::ValidatorStats stats = validator.GetValidatorStats(addr);
    result.pushKV("totalValidations", (int64_t)stats.totalValidations);
    result.pushKV("correctValidations", (int64_t)stats.accurateValidations);
    result.pushKV("incorrectValidations", (int64_t)stats.inaccurateValidations);
    result.pushKV("accuracy", stats.accuracyRate);
    result.pushKV("lastActivity", stats.lastActivityTime);

    // Fraud records - not implemented yet
    result.pushKV("fraudRecords", 0);

    // Calculate rewards (placeholder - would need actual reward tracking)
    result.pushKV("rewardsEarned", ValueFromAmount(0));

    return result;
}

UniValue getvalidationhistory(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getvalidationhistory \"address\" ( count )\n"
            "\nGet recent validation history for a validator.\n"
            "\nArguments:\n"
            "1. address    (string, required) Validator address (hex)\n"
            "2. count      (numeric, optional, default=10) Number of recent validations to return\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxx\",           (string) Transaction hash validated\n"
            "    \"blockheight\": n,          (numeric) Block height\n"
            "    \"vote\": \"xxx\",             (string) Vote (ACCEPT/REJECT/ABSTAIN)\n"
            "    \"correct\": true|false,     (boolean) Whether vote was correct\n"
            "    \"confidence\": n.nn         (numeric) Vote confidence\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getvalidationhistory", "\"1234567890abcdef1234567890abcdef12345678\"")
            + HelpExampleCli("getvalidationhistory", "\"1234567890abcdef1234567890abcdef12345678\" 20")
            + HelpExampleRpc("getvalidationhistory", "\"1234567890abcdef1234567890abcdef12345678\", 20")
        );

    std::string addrStr = request.params[0].get_str();
    int count = 10;
    if (request.params.size() > 1) {
        count = request.params[1].get_int();
        if (count < 1 || count > 100) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Count must be between 1 and 100");
        }
    }

    // Parse address
    if (addrStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be 40 hex characters");
    }

    std::vector<uint8_t> addrData = ParseHex(addrStr);
    uint160 addr;
    memcpy(addr.begin(), addrData.data(), 20);

    // Create database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
    
    // Create required components
    CVM::TrustGraph trustGraph(db);
    CVM::SecureHAT secureHAT(db);
    CVM::HATConsensusValidator validator(db, secureHAT, trustGraph);

    // Get validation history (would need to be implemented in HATConsensusValidator)
    UniValue result(UniValue::VARR);

    // Placeholder - actual implementation would query database for validation history
    LogPrintf("Validator: Querying validation history for %s (count: %d)\n", addr.ToString(), count);

    return result;
}

// Task 19.2.3: Sybil Attack Detection RPC Methods

UniValue detectsybilnetwork(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "detectsybilnetwork \"address\"\n"
            "\nDetect if an address is part of a Sybil attack network.\n"
            "\nArguments:\n"
            "1. address    (string, required) Address to analyze (hex)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) Address analyzed\n"
            "  \"isSybil\": true|false,       (boolean) Whether Sybil network detected\n"
            "  \"riskScore\": n.nn,           (numeric) Sybil risk score (0.0-1.0)\n"
            "  \"clusterSize\": n,            (numeric) Number of addresses in cluster\n"
            "  \"clusterAddresses\": [\"xxx\"], (array) Addresses in cluster\n"
            "  \"confidence\": n.nn,          (numeric) Clustering confidence\n"
            "  \"factors\": {                 (object) Risk factors\n"
            "    \"clusterSizeRisk\": n.nn,\n"
            "    \"creationTimeRisk\": n.nn,\n"
            "    \"patternRisk\": n.nn,\n"
            "    \"reputationRisk\": n.nn,\n"
            "    \"fraudRisk\": n.nn\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("detectsybilnetwork", "\"1234567890abcdef1234567890abcdef12345678\"")
            + HelpExampleRpc("detectsybilnetwork", "\"1234567890abcdef1234567890abcdef12345678\"")
        );

    std::string addrStr = request.params[0].get_str();

    // Parse address
    if (addrStr.length() != 40) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be 40 hex characters");
    }

    std::vector<uint8_t> addrData = ParseHex(addrStr);
    uint160 addr;
    memcpy(addr.begin(), addrData.data(), 20);

    // Create database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
    
    // Create required components
    CVM::TrustGraph trustGraph(db);
    CVM::SecureHAT secureHAT(db);
    CVM::HATConsensusValidator validator(db, secureHAT, trustGraph);

    // Detect Sybil network
    bool isSybil = validator.DetectSybilNetwork(addr);
    double riskScore = validator.CalculateSybilRiskScore(addr);
    CVM::WalletCluster cluster = validator.GetWalletCluster(addr);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", addrStr);
    result.pushKV("isSybil", isSybil);
    result.pushKV("riskScore", riskScore);
    result.pushKV("clusterSize", (int)cluster.addresses.size());
    result.pushKV("confidence", cluster.confidence);

    // Add cluster addresses
    UniValue clusterAddrs(UniValue::VARR);
    for (const auto& clusterAddr : cluster.addresses) {
        clusterAddrs.push_back(clusterAddr.ToString());
    }
    result.pushKV("clusterAddresses", clusterAddrs);

    // Add risk factors (would need to be exposed from CalculateSybilRiskScore)
    UniValue factors(UniValue::VOBJ);
    factors.pushKV("clusterSizeRisk", "See implementation for details");
    factors.pushKV("creationTimeRisk", "See implementation for details");
    factors.pushKV("patternRisk", "See implementation for details");
    factors.pushKV("reputationRisk", "See implementation for details");
    factors.pushKV("fraudRisk", "See implementation for details");
    result.pushKV("factors", factors);

    return result;
}

UniValue getsybilalerts(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getsybilalerts ( count )\n"
            "\nGet recent Sybil attack alerts.\n"
            "\nArguments:\n"
            "1. count      (numeric, optional, default=10) Number of alerts to return\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",          (string) Address flagged\n"
            "    \"riskScore\": n.nn,          (numeric) Risk score\n"
            "    \"reason\": \"xxx\",            (string) Reason for alert\n"
            "    \"timestamp\": n,             (numeric) Alert timestamp\n"
            "    \"blockHeight\": n,           (numeric) Block height\n"
            "    \"reviewed\": true|false      (boolean) Whether alert has been reviewed\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getsybilalerts", "")
            + HelpExampleCli("getsybilalerts", "20")
            + HelpExampleRpc("getsybilalerts", "20")
        );

    int count = 10;
    if (request.params.size() > 0) {
        count = request.params[0].get_int();
        if (count < 1 || count > 100) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Count must be between 1 and 100");
        }
    }

    // Create database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);

    UniValue result(UniValue::VARR);

    // Placeholder - actual implementation would query database for Sybil alerts
    LogPrintf("Querying Sybil alerts (count: %d)\n", count);

    return result;
}

// Register CVM RPC commands
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "cvm",                "deploycontract",        &deploycontract,         {"bytecode","gaslimit","initdata"} },
    { "cvm",                "callcontract",          &callcontract,           {"contractaddress","data","gaslimit","value"} },
    { "cvm",                "getcontractinfo",       &getcontractinfo,        {"contractaddress"} },
    { "cvm",                "sendcvmcontract",       &sendcvmcontract,        {"bytecode","gaslimit"} },
    { "reputation",         "getreputation",         &getreputation,          {"address"} },
    { "reputation",         "getgasallowance",       &getgasallowance,        {"address"} },
    { "reputation",         "getnetworkcongestion",  &getnetworkcongestion,   {} },
    { "reputation",         "getgassubsidies",       &getgassubsidies,        {"address"} },
    { "reputation",         "creategaspool",         &creategaspool,          {"poolid","amount","minreputation"} },
    { "reputation",         "getgaspoolinfo",        &getgaspoolinfo,         {"poolid"} },
    { "reputation",         "estimategascost",       &estimategascost,        {"gaslimit","address"} },
    { "reputation",         "createpriceguarantee",  &createpriceguarantee,   {"address","guaranteedprice","duration","minreputation"} },
    { "reputation",         "getpriceguarantee",     &getpriceguarantee,      {"address"} },
    // Trust-aware RPC methods (Cascoin-specific)
    { "cas",                "cas_getReputationDiscount", &cas_getReputationDiscount, {"address"} },
    { "cas",                "cas_getFreeGasAllowance", &cas_getFreeGasAllowance, {"address"} },
    { "cas",                "cas_getGasSubsidy",     &cas_getGasSubsidy,      {"address","poolid"} },
    { "cas",                "cas_getTrustContext",   &cas_getTrustContext,    {"address"} },
    { "cas",                "cas_estimateGasWithReputation", &cas_estimateGasWithReputation, {"gaslimit","address","operation"} },
    { "reputation",         "votereputation",        &votereputation,         {"address","vote","reason","proof"} },
    { "reputation",         "sendcvmvote",           &sendcvmvote,            {"address","vote","reason"} },
    { "reputation",         "listreputations",       &listreputations,        {"threshold","count"} },
    { "wot",                "addtrust",              &addtrust,               {"address","weight","bond","reason"} },
    { "wot",                "getweightedreputation", &getweightedreputation,  {"target","viewer","maxdepth"} },
    { "wot",                "gettrustgraphstats",    &gettrustgraphstats,     {} },
    { "wot",                "listtrustrelations",    &listtrustrelations,     {"max_count"} },
    { "wot",                "sendtrustrelation",     &sendtrustrelation,      {"address","weight","bond","reason"} },
    { "wot",                "sendbondedvote",        &sendbondedvote,         {"address","vote","bond","reason"} },
    { "hat",                "getbehaviormetrics",    &getbehaviormetrics,     {"address"} },
    { "hat",                "getgraphmetrics",       &getgraphmetrics,        {"address"} },
    { "hat",                "getsecuretrust",        &getsecuretrust,         {"target","viewer"} },
    { "hat",                "gettrustbreakdown",     &gettrustbreakdown,      {"target","viewer"} },
    { "hat",                "detectclusters",        &detectclusters,         {} },
    { "wallet_cluster",     "buildwalletclusters",   &buildwalletclusters,    {} },
    { "wallet_cluster",     "getwalletcluster",      &getwalletcluster,       {"address"} },
    { "wallet_cluster",     "geteffectivetrust",     &geteffectivetrust,      {"target","viewer"} },
    { "dao",                "createdispute",         &createdispute,          {"vote_txid","bond","reason"} },
    { "dao",                "listdisputes",          &listdisputes,           {"status"} },
    { "dao",                "getdispute",            &getdispute,             {"dispute_id"} },
    { "dao",                "votedispute",           &votedispute,            {"dispute_id","support_slash","from_address","stake"} },
    { "dao",                "getpendingrewards",     &getpendingrewards,      {"address"} },
    { "dao",                "claimreward",           &claimreward,            {"reward_id","from_address"} },
    { "dao",                "claimallrewards",       &claimallrewards,        {"from_address"} },
    { "dao",                "getrewarddistribution", &getrewarddistribution,  {"dispute_id"} },
    { "dao",                "commitdisputevote",     &commitdisputevote,      {"dispute_id","commitment_hash","stake","from_address"} },
    { "dao",                "revealdisputevote",     &revealdisputevote,      {"dispute_id","vote","nonce","from_address"} },
    { "resource",           "getresourcestats",      &getresourcestats,       {"address"} },
    { "resource",           "getglobalresourcestats",&getglobalresourcestats, {} },
    { "cleanup",            "rungarbagecollection",  &rungarbagecollection,   {} },
    { "cleanup",            "getcleanupstats",       &getcleanupstats,        {} },
    { "cleanup",            "getcontractcleanupinfo",&getcontractcleanupinfo, {"contractaddress"} },
    // Cascoin RPC methods for CVM/EVM (primary/standard names)
    { "cas",                "cas_sendTransaction",   &eth_sendTransaction,    {"transaction"} },
    { "cas",                "cas_call",              &eth_call,               {"call","block"} },
    { "cas",                "cas_estimateGas",       &eth_estimateGas,        {"transaction"} },
    { "cas",                "cas_getCode",           &eth_getCode,            {"address","block"} },
    { "cas",                "cas_getStorageAt",      &eth_getStorageAt,       {"address","position","block"} },
    { "cas",                "cas_getTransactionReceipt", &eth_getTransactionReceipt, {"txhash"} },
    { "cas",                "cas_blockNumber",       &eth_blockNumber,        {} },
    { "cas",                "cas_getBalance",        &eth_getBalance,         {"address","block"} },
    { "cas",                "cas_getTransactionCount", &eth_getTransactionCount, {"address","block"} },
    { "cas",                "cas_gasPrice",          &eth_gasPrice,           {} },
    // Ethereum-compatible aliases for tool compatibility (MetaMask, Web3.js, etc.)
    { "eth",                "eth_sendTransaction",   &eth_sendTransaction,    {"transaction"} },
    { "eth",                "eth_call",              &eth_call,               {"call","block"} },
    { "eth",                "eth_estimateGas",       &eth_estimateGas,        {"transaction"} },
    { "eth",                "eth_getCode",           &eth_getCode,            {"address","block"} },
    { "eth",                "eth_getStorageAt",      &eth_getStorageAt,       {"address","position","block"} },
    { "eth",                "eth_getTransactionReceipt", &eth_getTransactionReceipt, {"txhash"} },
    { "eth",                "eth_blockNumber",       &eth_blockNumber,        {} },
    { "eth",                "eth_getBalance",        &eth_getBalance,         {"address","block"} },
    { "eth",                "eth_getTransactionCount", &eth_getTransactionCount, {"address","block"} },
    { "eth",                "eth_gasPrice",          &eth_gasPrice,           {} },
    // Developer tooling RPC methods
    { "debug",              "debug_traceTransaction", &debug_traceTransaction, {"txhash","options"} },
    { "debug",              "debug_traceCall",       &debug_traceCall,        {"call","block","options"} },
    { "cas",                "cas_snapshot",          &cas_snapshot,           {} },
    { "evm",                "evm_snapshot",          &evm_snapshot,           {} },
    { "cas",                "cas_revert",            &cas_revert,             {"snapshotid"} },
    { "evm",                "evm_revert",            &evm_revert,             {"snapshotid"} },
    { "cas",                "cas_mine",              &cas_mine,               {"numblocks"} },
    { "evm",                "evm_mine",              &evm_mine,               {"numblocks"} },
    { "cas",                "cas_setNextBlockTimestamp", &cas_setNextBlockTimestamp, {"timestamp"} },
    { "evm",                "evm_setNextBlockTimestamp", &evm_setNextBlockTimestamp, {"timestamp"} },
    { "cas",                "cas_increaseTime",      &cas_increaseTime,       {"seconds"} },
    { "evm",                "evm_increaseTime",      &evm_increaseTime,       {"seconds"} },
    // Validator management RPC commands
    { "validator",          "generatevalidatorkey",       &generatevalidatorkey,        {} },
    { "validator",          "getvalidatorinfo",           &getvalidatorinfo,            {} },
    { "validator",          "importvalidatorkey",         &importvalidatorkey,          {"privkey"} },
    { "validator",          "exportvalidatorkey",         &exportvalidatorkey,          {} },
    { "validator",          "loadvalidatorfromwallet",    &loadvalidatorfromwallet,     {"address"} },
    { "validator",          "listvalidators",             &listvalidators,              {"minreputation"} },
    { "validator",          "getvalidatorstats",          &getvalidatorstats,           {"address"} },
    { "validator",          "getvalidatorearnings",       &getvalidatorearnings,        {"address","blockcount"} },
    { "validator",          "getnetworkvalidatorstats",   &getnetworkvalidatorstats,    {"blockcount"} },
    { "validator",          "getvalidationhistory",       &getvalidationhistory,        {"address","count"} },
    // Sybil attack detection RPC commands (Task 19.2.3)
    { "sybil",              "detectsybilnetwork",         &detectsybilnetwork,          {"address"} },
    { "sybil",              "getsybilalerts",             &getsybilalerts,              {"count"} },
};

void RegisterCVMRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}



// Vote Manipulation Detection RPC Methods

UniValue analyzevotemanipulation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "analyzevotemanipulation \"txhash\"\n"
            "\nAnalyze a transaction for vote manipulation patterns.\n"
            "\nArguments:\n"
            "1. \"txhash\"    (string, required) The transaction hash\n"
            "\nResult:\n"
            "{\n"
            "  \"type\": \"xxx\",                    (string) Type of manipulation detected\n"
            "  \"suspicious_addresses\": [...],      (array) Addresses flagged as suspicious\n"
            "  \"suspicious_txs\": [...],            (array) Transactions flagged as suspicious\n"
            "  \"confidence\": x.xx,                 (numeric) Confidence level (0.0-1.0)\n"
            "  \"description\": \"xxx\",             (string) Description of detection\n"
            "  \"escalate_to_dao\": true|false       (boolean) Should be escalated to DAO\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("analyzevotemanipulation", "\"1234567890abcdef...\"")
            + HelpExampleRpc("analyzevotemanipulation", "\"1234567890abcdef...\"")
        );
    
    uint256 txHash = ParseHashV(request.params[0], "txhash");
    
    // Get HAT consensus validator
    // TODO: Get from global instance
    // For now, return placeholder
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("type", "none");
    result.pushKV("suspicious_addresses", UniValue(UniValue::VARR));
    result.pushKV("suspicious_txs", UniValue(UniValue::VARR));
    result.pushKV("confidence", 0.0);
    result.pushKV("description", "No manipulation detected");
    result.pushKV("escalate_to_dao", false);
    
    return result;
}

UniValue analyzeaddressreputation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "analyzeaddressreputation \"address\"\n"
            "\nAnalyze an address for reputation manipulation.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The address to analyze\n"
            "\nResult:\n"
            "{\n"
            "  \"type\": \"xxx\",                    (string) Type of manipulation detected\n"
            "  \"suspicious_addresses\": [...],      (array) Addresses flagged as suspicious\n"
            "  \"confidence\": x.xx,                 (numeric) Confidence level (0.0-1.0)\n"
            "  \"description\": \"xxx\",             (string) Description of detection\n"
            "  \"escalate_to_dao\": true|false,      (boolean) Should be escalated to DAO\n"
            "  \"is_flagged\": true|false            (boolean) Is address currently flagged\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("analyzeaddressreputation", "\"CAddress123...\"")
            + HelpExampleRpc("analyzeaddressreputation", "\"CAddress123...\"")
        );
    
    std::string addressStr = request.params[0].get_str();
    uint160 address;
    // TODO: Parse address
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("type", "none");
    result.pushKV("suspicious_addresses", UniValue(UniValue::VARR));
    result.pushKV("confidence", 0.0);
    result.pushKV("description", "No manipulation detected");
    result.pushKV("escalate_to_dao", false);
    result.pushKV("is_flagged", false);
    
    return result;
}

UniValue getflaggedaddresses(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getflaggedaddresses\n"
            "\nGet all addresses flagged for suspicious activity.\n"
            "\nResult:\n"
            "[\n"
            "  \"address\",    (string) Flagged address\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getflaggedaddresses", "")
            + HelpExampleRpc("getflaggedaddresses", "")
        );
    
    UniValue result(UniValue::VARR);
    
    // TODO: Get from vote manipulation detector
    
    return result;
}

UniValue getvotehistory(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getvotehistory \"txhash\"\n"
            "\nGet vote history for a transaction.\n"
            "\nArguments:\n"
            "1. \"txhash\"    (string, required) The transaction hash\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"validator\": \"address\",         (string) Validator address\n"
            "    \"vote\": \"accept|reject\",        (string) Vote decision\n"
            "    \"timestamp\": xxx,                 (numeric) Vote timestamp\n"
            "    \"score_difference\": x             (numeric) Score difference\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getvotehistory", "\"1234567890abcdef...\"")
            + HelpExampleRpc("getvotehistory", "\"1234567890abcdef...\"")
        );
    
    uint256 txHash = ParseHashV(request.params[0], "txhash");
    
    UniValue result(UniValue::VARR);
    
    // TODO: Get from vote manipulation detector
    
    return result;
}

UniValue getreputationhistory(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getreputationhistory \"address\" ( count )\n"
            "\nGet reputation change history for an address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The address\n"
            "2. count          (numeric, optional, default=100) Number of records to return\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"block_height\": xxx,              (numeric) Block height\n"
            "    \"old_score\": x,                   (numeric) Old reputation score\n"
            "    \"new_score\": x,                   (numeric) New reputation score\n"
            "    \"change\": x,                      (numeric) Change amount\n"
            "    \"reason\": \"xxx\"                 (string) Reason for change\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getreputationhistory", "\"CAddress123...\"")
            + HelpExampleRpc("getreputationhistory", "\"CAddress123...\" 50")
        );
    
    std::string addressStr = request.params[0].get_str();
    int count = 100;
    if (request.params.size() > 1) {
        count = request.params[1].get_int();
    }
    
    UniValue result(UniValue::VARR);
    
    // TODO: Get from vote manipulation detector
    
    return result;
}

static const CRPCCommand voteManipulationCommands[] =
{ //  category              name                            actor (function)            argNames
  //  --------------------- ------------------------        -----------------------     ----------
    { "cvm",                "analyzevotemanipulation",      &analyzevotemanipulation,   {"txhash"} },
    { "cvm",                "analyzeaddressreputation",     &analyzeaddressreputation,  {"address"} },
    { "cvm",                "getflaggedaddresses",          &getflaggedaddresses,       {} },
    { "cvm",                "getvotehistory",               &getvotehistory,            {"txhash"} },
    { "cvm",                "getreputationhistory",         &getreputationhistory,      {"address","count"} },
};

void RegisterVoteManipulationRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(voteManipulationCommands); vcidx++)
        t.appendCommand(voteManipulationCommands[vcidx].name, &voteManipulationCommands[vcidx]);
}
