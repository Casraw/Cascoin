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
#include <cvm/behaviormetrics.h>
#include <cvm/graphanalysis.h>
#include <cvm/securehat.h>
#include <cvm/walletcluster.h>
#include <validation.h>
#include <util.h>
#include <base58.h>
#include <utilstrencodings.h>
#include <univalue.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <hash.h>
#include <core_io.h>

/**
 * RPC commands for CVM (Cascoin Virtual Machine) and Reputation System
 */

UniValue deploycontract(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "deploycontract \"bytecode\" ( gaslimit \"initdata\" )\n"
            "\nDeploy a smart contract to the Cascoin Virtual Machine.\n"
            "\nArguments:\n"
            "1. \"bytecode\"      (string, required) Contract bytecode in hex format\n"
            "2. gaslimit        (numeric, optional, default=1000000) Gas limit for deployment\n"
            "3. \"initdata\"      (string, optional) Initialization data in hex format\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",           (string) Transaction ID\n"
            "  \"contractaddress\": \"xxx\", (string) Contract address\n"
            "  \"gasused\": n              (numeric) Gas used\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("deploycontract", "\"0x6001600201\"")
            + HelpExampleRpc("deploycontract", "\"0x6001600201\", 1000000")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse bytecode
    std::string bytecodeHex = request.params[0].get_str();
    if (bytecodeHex.substr(0, 2) == "0x") {
        bytecodeHex = bytecodeHex.substr(2);
    }
    
    std::vector<uint8_t> bytecode = ParseHex(bytecodeHex);
    
    // Validate bytecode
    std::string error;
    if (!CVM::ValidateContractCode(bytecode, error)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract bytecode: " + error);
    }
    
    // Get gas limit
    uint64_t gasLimit = CVM::MAX_GAS_PER_TX;
    if (request.params.size() > 1) {
        gasLimit = request.params[1].get_int64();
        if (gasLimit > CVM::MAX_GAS_PER_TX) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Gas limit exceeds maximum");
        }
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
    
    // Create deployment data with hash of bytecode (Soft Fork compatible)
    CVM::CVMDeployData deployData;
    deployData.codeHash = Hash(bytecode.begin(), bytecode.end());
    deployData.gasLimit = gasLimit;
    
    // Store actual bytecode in CVM database (off-chain for old nodes)
    if (CVM::g_cvmdb) {
        // TODO: Store bytecode associated with hash in database
        LogPrintf("CVM: Contract bytecode hash: %s\n", deployData.codeHash.ToString());
    }
    
    // Build OP_RETURN output with CVM data (Soft Fork!)
    std::vector<uint8_t> deployBytes = deployData.Serialize();
    CScript cvmScript = CVM::BuildCVMOpReturn(CVM::CVMOpType::CONTRACT_DEPLOY, deployBytes);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "Contract deployment prepared (Soft Fork OP_RETURN)");
    result.pushKV("bytecode_size", (int64_t)bytecode.size());
    result.pushKV("bytecode_hash", deployData.codeHash.ToString());
    result.pushKV("gas_limit", (int64_t)gasLimit);
    result.pushKV("op_return_script", HexStr(cvmScript.begin(), cvmScript.end()));
    result.pushKV("softfork_compatible", true);
    
    // Note: To actually broadcast, user needs to create a transaction with:
    // - Input: Funding
    // - Output 0: OP_RETURN (cvmScript)
    // - Output 1: Change
    
    return result;
}

UniValue callcontract(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "callcontract \"contractaddress\" ( \"data\" gaslimit value )\n"
            "\nCall a smart contract function.\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) Contract address\n"
            "2. \"data\"            (string, optional) Call data in hex format\n"
            "3. gaslimit          (numeric, optional, default=1000000) Gas limit\n"
            "4. value             (numeric, optional, default=0) Amount to send\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",        (string) Transaction ID\n"
            "  \"gasused\": n,          (numeric) Gas used\n"
            "  \"result\": \"xxx\"       (string) Execution result\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("callcontract", "\"DXG7Yx...\" \"0x12345678\"")
            + HelpExampleRpc("callcontract", "\"DXG7Yx...\", \"0x12345678\", 500000")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    // Parse contract address
    std::string addressStr = request.params[0].get_str();
    uint160 contractAddr;
    // TODO: Parse address properly
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "Contract call prepared");
    
    return result;
}

UniValue getcontractinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getcontractinfo \"contractaddress\"\n"
            "\nGet information about a deployed contract.\n"
            "\nArguments:\n"
            "1. \"contractaddress\" (string, required) Contract address\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",        (string) Contract address\n"
            "  \"bytecode\": \"xxx\",       (string) Contract bytecode\n"
            "  \"deployheight\": n,        (numeric) Deployment block height\n"
            "  \"deploytxid\": \"xxx\"      (string) Deployment transaction ID\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getcontractinfo", "\"DXG7Yx...\"")
            + HelpExampleRpc("getcontractinfo", "\"DXG7Yx...\"")
        );

    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "not implemented");
    
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
            "\nArguments:\n"
            "1. \"bytecode\"    (string, required) Contract bytecode in hex\n"
            "2. gaslimit      (numeric, optional) Gas limit (default: 1000000)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",         (string) Transaction ID\n"
            "  \"fee\": n,                (numeric) Transaction fee\n"
            "  \"bytecode_hash\": \"xxx\", (string) Bytecode hash\n"
            "  \"mempool\": true          (boolean) In mempool\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("sendcvmcontract", "\"6001600201\" 1000000")
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
    
    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("fee", ValueFromAmount(fee));
    result.pushKV("bytecode_hash", codeHash.GetHex());
    result.pushKV("bytecode_size", (int64_t)bytecode.size());
    result.pushKV("gas_limit", (int64_t)gasLimit);
    result.pushKV("mempool", true);
    
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
    
    // Get all dispute keys from database
    std::vector<std::string> keys;
    CVM::g_cvmdb->GetAllKeys("dispute_", keys);
    
    int count = 0;
    for (const auto& key : keys) {
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
    
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "votedispute \"dispute_id\" support_slash \"from_address\" [stake]\n"
            "\nVote on a DAO dispute as a DAO member.\n"
            "\nArguments:\n"
            "1. dispute_id       (string, required) The dispute ID\n"
            "2. support_slash    (boolean, required) true = slash the vote, false = keep the vote\n"
            "3. from_address     (string, required) Vote from this address\n"
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
    bool supportSlash = request.params[1].get_bool();
    
    // Get voter address (required)
    CTxDestination voterDest = DecodeDestination(request.params[2].get_str());
    if (!IsValidDestination(voterDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Cascoin address");
    }
    
    // Get stake amount (optional, default 1.0 CAS)
    CAmount stake = COIN;
    if (request.params.size() > 3 && !request.params[3].isNull()) {
        stake = AmountFromValue(request.params[3]);
    }
    
    if (stake < COIN / 10) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum stake is 0.1 CAS");
    }
    
    const CKeyID* keyID = boost::get<CKeyID>(&voterDest);
    if (!keyID) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address does not refer to a key");
    }
    uint160 voterAddress(*keyID);
    
    // Vote on dispute
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    if (!trustGraph.VoteOnDispute(disputeId, voterAddress, supportSlash, stake)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to record vote");
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("dispute_id", disputeId.ToString());
    result.pushKV("voter", EncodeDestination(voterDest));
    result.pushKV("support_slash", supportSlash);
    result.pushKV("stake", ValueFromAmount(stake));
    result.pushKV("status", "Vote recorded successfully");
    
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
    { "dao",                "listdisputes",          &listdisputes,           {"status"} },
    { "dao",                "getdispute",            &getdispute,             {"dispute_id"} },
    { "dao",                "votedispute",           &votedispute,            {"dispute_id","support_slash","from_address","stake"} },
};

void RegisterCVMRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

