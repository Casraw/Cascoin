// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/util.h>
#include <cvm/cvm.h>
#include <cvm/cvmdb.h>
#include <cvm/contract.h>
#include <cvm/reputation.h>
#include <validation.h>
#include <util.h>
#include <base58.h>
#include <utilstrencodings.h>
#include <univalue.h>

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
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "Contract deployment prepared");
    result.pushKV("bytecode_size", (int64_t)bytecode.size());
    result.pushKV("gas_limit", (int64_t)gasLimit);
    
    // Note: Actual transaction creation would be done by wallet
    // This RPC would normally create a transaction with contract deployment data
    
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
    
    // Get address hash
    CKeyID keyID = boost::get<CKeyID>(dest);
    uint160 address(keyID);
    
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
    int64_t voteValue = request.params[1].get_int64();
    std::string reason = request.params[2].get_str();
    
    // Parse address
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    
    CKeyID keyID = boost::get<CKeyID>(dest);
    uint160 targetAddress(keyID);
    
    // Create vote transaction
    CVM::ReputationVoteTx voteTx;
    voteTx.targetAddress = targetAddress;
    voteTx.voteValue = voteValue;
    voteTx.reason = reason;
    
    if (request.params.size() > 3) {
        std::string proofHex = request.params[3].get_str();
        if (proofHex.substr(0, 2) == "0x") {
            proofHex = proofHex.substr(2);
        }
        voteTx.proof = ParseHex(proofHex);
    }
    
    // Validate vote
    std::string error;
    if (!voteTx.IsValid(error)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote: " + error);
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "Vote prepared");
    result.pushKV("vote", voteValue);
    result.pushKV("reason", reason);
    
    // Note: Actual transaction creation would be done by wallet
    
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

// Register CVM RPC commands
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "cvm",                "deploycontract",        &deploycontract,         {"bytecode","gaslimit","initdata"} },
    { "cvm",                "callcontract",          &callcontract,           {"contractaddress","data","gaslimit","value"} },
    { "cvm",                "getcontractinfo",       &getcontractinfo,        {"contractaddress"} },
    { "reputation",         "getreputation",         &getreputation,          {"address"} },
    { "reputation",         "votereputation",        &votereputation,         {"address","vote","reason","proof"} },
    { "reputation",         "listreputations",       &listreputations,        {"threshold","count"} },
};

void RegisterCVMRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

