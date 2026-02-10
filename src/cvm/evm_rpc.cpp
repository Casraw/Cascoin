// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/evm_rpc.h>
#include <cvm/cvm.h>
#include <cvm/cvmdb.h>
#include <cvm/enhanced_vm.h>
#include <cvm/enhanced_storage.h>
#include <cvm/fee_calculator.h>
#include <cvm/softfork.h>
#include <cvm/txbuilder.h>
#include <cvm/trust_context.h>
#include <cvm/address_index.h>
#include <cvm/execution_tracer.h>
#include <cvm/reputation.h>
#include <cvm/cross_chain_bridge.h>
#include <validation.h>
#include <chainparams.h>
#include <util.h>
#include <utilstrencodings.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <rpc/util.h>
#include <core_io.h>
#include <miner.h>
#include <pow.h>
#include <timedata.h>
#include <chain.h>
#include <consensus/validation.h>
#include <net.h>
#include <txdb.h>

// External declarations
extern CTxMemPool mempool;

// Global connection manager
extern std::unique_ptr<CConnman> g_connman;

/**
 * Cascoin CVM/EVM RPC implementation
 * 
 * Primary methods use cas_* naming (Cascoin-native)
 * Ethereum-compatible aliases use eth_* naming for tool compatibility
 */

// ===== Helper Functions =====

/**
 * Convert hex string to uint160 address
 */
static uint160 ParseAddress(const std::string& hexAddr) {
    if (hexAddr.substr(0, 2) == "0x") {
        std::vector<uint8_t> data = ParseHex(hexAddr.substr(2));
        if (data.size() != 20) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address length");
        }
        uint160 addr;
        memcpy(addr.begin(), data.data(), 20);
        return addr;
    }
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must start with 0x");
}

/**
 * Convert uint160 address to hex string
 */
static std::string AddressToHex(const uint160& addr) {
    return "0x" + HexStr(addr.begin(), addr.end());
}

/**
 * Convert uint256 to hex string
 */
static std::string Uint256ToHex(const uint256& value) {
    return "0x" + value.GetHex();
}

/**
 * Parse hex string to uint256
 */
static uint256 ParseUint256(const std::string& hexStr) {
    if (hexStr.substr(0, 2) == "0x") {
        return uint256S(hexStr.substr(2));
    }
    return uint256S(hexStr);
}

// ===== Primary Cascoin Methods (cas_*) =====

UniValue cas_blockNumber(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "cas_blockNumber\n"
            "\nGet current block number.\n"
            "\nResult:\n"
            "n                           (numeric) Current block height\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_blockNumber", "")
            + HelpExampleRpc("cas_blockNumber", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue cas_gasPrice(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "cas_gasPrice\n"
            "\nGet current gas price.\n"
            "\nReturns Cascoin's sustainable gas price (100x lower than Ethereum).\n"
            "\nResult:\n"
            "n                           (numeric) Gas price in wei\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_gasPrice", "")
            + HelpExampleRpc("cas_gasPrice", "")
        );

    // Return Cascoin's base gas price (100x lower than Ethereum)
    // Typical Ethereum: 20 gwei = 20000000000 wei
    // Cascoin: 0.2 gwei = 200000000 wei (100x lower)
    
    return 200000000; // 0.2 gwei
}

UniValue cas_call(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "cas_call {\"to\":\"address\",\"data\":\"hex\"} ( \"block\" )\n"
            "\nExecute a contract call without creating a transaction (read-only).\n"
            "\nArguments:\n"
            "1. call            (object, required) Call object\n"
            "   {\n"
            "     \"to\": \"address\"       (string, required) Contract address\n"
            "     \"data\": \"hex\"         (string, required) Call data\n"
            "     \"from\": \"address\"     (string, optional) Caller address\n"
            "     \"gas\": n              (numeric, optional) Gas limit (default: 1000000)\n"
            "   }\n"
            "2. block           (string, optional) Block number or \"latest\" (default: \"latest\")\n"
            "\nResult:\n"
            "\"data\"                     (string) Return data in hex\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_call", "'{\"to\":\"0x...\",\"data\":\"0x...\"}'")
            + HelpExampleRpc("cas_call", "{\"to\":\"0x...\",\"data\":\"0x...\"}")
        );

    UniValue callObj = request.params[0].get_obj();
    
    std::string toStr = find_value(callObj, "to").get_str();
    std::string dataStr = find_value(callObj, "data").get_str();
    std::string fromStr = find_value(callObj, "from").isNull() ? "" : find_value(callObj, "from").get_str();
    uint64_t gasLimit = find_value(callObj, "gas").isNull() ? 1000000 : find_value(callObj, "gas").get_int64();
    
    if (!IsHex(dataStr)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Data must be hex string");
    }
    
    // Parse addresses
    uint160 contractAddr = ParseAddress(toStr);
    uint160 callerAddr = fromStr.empty() ? uint160() : ParseAddress(fromStr);
    
    // Parse call data
    std::vector<uint8_t> callData = ParseHex(dataStr.substr(0, 2) == "0x" ? dataStr.substr(2) : dataStr);
    
    // Get current block info
    LOCK(cs_main);
    int blockHeight = chainActive.Height();
    uint256 blockHash = chainActive.Tip()->GetBlockHash();
    int64_t timestamp = chainActive.Tip()->GetBlockTime();
    
    // Initialize EnhancedVM
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    auto trustContext = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
    CVM::EnhancedVM vm(CVM::g_cvmdb.get(), trustContext);
    
    // Execute read-only call
    CVM::EnhancedExecutionResult result = vm.CallContract(
        contractAddr,
        callData,
        gasLimit,
        callerAddr,
        0, // No value for read-only call
        blockHeight,
        blockHash,
        timestamp
    );
    
    if (!result.success) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Contract call failed: " + result.error);
    }
    
    // Return result data as hex
    return "0x" + HexStr(result.return_data);
}

UniValue cas_estimateGas(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_estimateGas {\"to\":\"address\",\"data\":\"hex\"}\n"
            "\nEstimate gas required for a transaction.\n"
            "\nThis method accounts for Cascoin's reputation-based gas discounts.\n"
            "\nArguments:\n"
            "1. transaction     (object, required) Transaction object\n"
            "   {\n"
            "     \"from\": \"address\"     (string, optional) Sender address\n"
            "     \"to\": \"address\"       (string, optional) Contract address\n"
            "     \"data\": \"hex\"         (string, required) Transaction data\n"
            "     \"value\": n            (numeric, optional) Value to send\n"
            "   }\n"
            "\nResult:\n"
            "n                           (numeric) Estimated gas amount\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_estimateGas", "'{\"to\":\"0x...\",\"data\":\"0x...\"}'")
            + HelpExampleRpc("cas_estimateGas", "{\"to\":\"0x...\",\"data\":\"0x...\"}")
        );

    UniValue txObj = request.params[0].get_obj();
    
    std::string fromStr = find_value(txObj, "from").isNull() ? "" : find_value(txObj, "from").get_str();
    std::string toStr = find_value(txObj, "to").isNull() ? "" : find_value(txObj, "to").get_str();
    std::string dataStr = find_value(txObj, "data").get_str();
    uint64_t value = find_value(txObj, "value").isNull() ? 0 : find_value(txObj, "value").get_int64();
    
    if (!IsHex(dataStr)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Data must be hex string");
    }
    
    // Parse data
    std::vector<uint8_t> data = ParseHex(dataStr.substr(0, 2) == "0x" ? dataStr.substr(2) : dataStr);
    
    // Base gas estimate
    uint64_t baseGas = 21000; // Base transaction cost
    
    // Add gas for data
    for (uint8_t byte : data) {
        baseGas += (byte == 0) ? 4 : 68; // Zero bytes cost 4 gas, non-zero cost 68
    }
    
    // If this is a contract call or deployment, add execution gas
    if (!toStr.empty() || data.size() > 0) {
        // Estimate execution gas (simplified)
        baseGas += 50000; // Base contract execution cost
        baseGas += data.size() * 100; // Additional cost for data processing
    }
    
    // Apply reputation-based discount if sender is known
    if (!fromStr.empty()) {
        try {
            uint160 senderAddr = ParseAddress(fromStr);
            
            // Get reputation
            if (CVM::g_cvmdb) {
                auto trustContext = std::make_shared<CVM::TrustContext>(CVM::g_cvmdb.get());
                uint32_t reputation = trustContext->GetReputation(senderAddr);
                
                // Apply discount (50% for 80+ reputation)
                if (reputation >= 80) {
                    baseGas = baseGas / 2;
                } else if (reputation >= 70) {
                    baseGas = baseGas * 7 / 10;
                } else if (reputation >= 60) {
                    baseGas = baseGas * 8 / 10;
                } else if (reputation >= 50) {
                    baseGas = baseGas * 9 / 10;
                }
            }
        } catch (...) {
            // If address parsing fails, use base estimate
        }
    }
    
    return baseGas;
}

UniValue cas_getCode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "cas_getCode \"address\" ( \"block\" )\n"
            "\nGet contract bytecode at an address.\n"
            "\nArguments:\n"
            "1. address         (string, required) Contract address\n"
            "2. block           (string, optional) Block number or \"latest\" (default: \"latest\")\n"
            "\nResult:\n"
            "\"bytecode\"                (string) Contract bytecode in hex\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getCode", "\"0x...\"")
            + HelpExampleRpc("cas_getCode", "\"0x...\"")
        );

    std::string addressStr = request.params[0].get_str();
    
    // Parse address
    uint160 contractAddr = ParseAddress(addressStr);
    
    // Get contract bytecode from database
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    std::vector<uint8_t> bytecode;
    if (!CVM::g_cvmdb->LoadContract(contractAddr, bytecode)) {
        // Contract not found, return empty
        return "0x";
    }
    
    // Return bytecode as hex
    return "0x" + HexStr(bytecode);
}

UniValue cas_getStorageAt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "cas_getStorageAt \"address\" \"position\" ( \"block\" )\n"
            "\nGet value from contract storage.\n"
            "\nArguments:\n"
            "1. address         (string, required) Contract address\n"
            "2. position        (string, required) Storage position (hex)\n"
            "3. block           (string, optional) Block number or \"latest\" (default: \"latest\")\n"
            "\nResult:\n"
            "\"value\"                   (string) Storage value in hex (32 bytes)\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getStorageAt", "\"0x...\" \"0x0\"")
            + HelpExampleRpc("cas_getStorageAt", "\"0x...\",\"0x0\"")
        );

    std::string addressStr = request.params[0].get_str();
    std::string positionStr = request.params[1].get_str();
    
    // Parse address and position
    uint160 contractAddr = ParseAddress(addressStr);
    uint256 position = ParseUint256(positionStr);
    
    // Get storage value from database
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    CVM::EnhancedStorage storage(CVM::g_cvmdb.get());
    uint256 value;
    
    if (!storage.Load(contractAddr, position, value)) {
        // Storage slot not found, return zero
        value = uint256();
    }
    
    // Return value as hex (32 bytes)
    return Uint256ToHex(value);
}

UniValue cas_sendTransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_sendTransaction {\"from\":\"address\",\"to\":\"address\",\"data\":\"hex\",\"gas\":n,\"gasPrice\":n,\"value\":n}\n"
            "\nSend a transaction to deploy or call a contract.\n"
            "\nArguments:\n"
            "1. transaction     (object, required) Transaction object\n"
            "   {\n"
            "     \"from\": \"address\"     (string, required) Sender address\n"
            "     \"to\": \"address\"       (string, optional) Contract address (omit for deployment)\n"
            "     \"data\": \"hex\"         (string, required) Contract bytecode or call data\n"
            "     \"gas\": n              (numeric, optional) Gas limit (default: 1000000)\n"
            "     \"gasPrice\": n         (numeric, optional) Gas price in wei\n"
            "     \"value\": n            (numeric, optional) Value to send in wei\n"
            "   }\n"
            "\nResult:\n"
            "\"hash\"                     (string) Transaction hash\n"
            "\nNote: This method integrates with Cascoin's reputation system.\n"
            "High reputation addresses receive gas discounts and may qualify for free gas.\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_sendTransaction", "'{\"from\":\"0x...\",\"data\":\"0x60806040...\"}'")
            + HelpExampleRpc("cas_sendTransaction", "{\"from\":\"0x...\",\"data\":\"0x60806040...\"}")
        );

    // Get wallet
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Parse transaction object
    UniValue txObj = request.params[0].get_obj();
    
    // Get data field (required)
    if (!txObj.exists("data")) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing required field: data");
    }
    std::string dataHex = txObj["data"].get_str();
    if (dataHex.substr(0, 2) == "0x") {
        dataHex = dataHex.substr(2);
    }
    std::vector<uint8_t> data = ParseHex(dataHex);
    
    if (data.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Data field cannot be empty");
    }
    
    // Get gas limit (optional, default 1M)
    uint64_t gasLimit = CVM::MAX_GAS_PER_TX;
    if (txObj.exists("gas")) {
        gasLimit = txObj["gas"].get_int64();
    }
    
    // Get value (optional, default 0)
    CAmount value = 0;
    if (txObj.exists("value")) {
        value = txObj["value"].get_int64();
    }
    
    // Check if this is a deployment (no "to" field) or a call
    bool isDeployment = !txObj.exists("to") || txObj["to"].isNull();
    
    CWalletTx wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    CCoinControl coin_control;
    
    bool success;
    if (isDeployment) {
        // Contract deployment
        std::vector<uint8_t> initData;  // No separate init data in this interface
        success = pwallet->CreateContractDeploymentTransaction(data, gasLimit, initData, wtx, 
                                                               reservekey, nFeeRequired, strError, &coin_control);
    } else {
        // Contract call
        std::string toAddress = txObj["to"].get_str();
        if (toAddress.substr(0, 2) == "0x") {
            toAddress = toAddress.substr(2);
        }
        
        // Parse contract address (20 bytes)
        if (toAddress.length() != 40) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid contract address length");
        }
        
        std::vector<uint8_t> addressBytes = ParseHex(toAddress);
        uint160 contractAddress(addressBytes);
        
        success = pwallet->CreateContractCallTransaction(contractAddress, data, gasLimit, value, wtx,
                                                         reservekey, nFeeRequired, strError, &coin_control);
    }
    
    if (!success) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    
    // Broadcast transaction
    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, reservekey, g_connman.get(), state)) {
        strError = strprintf("Transaction commit failed: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    
    return wtx.GetHash().GetHex();
}

UniValue cas_getTransactionReceipt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_getTransactionReceipt \"txhash\"\n"
            "\nGet transaction receipt including execution results.\n"
            "\nArguments:\n"
            "1. txhash          (string, required) Transaction hash\n"
            "\nResult:\n"
            "{\n"
            "  \"transactionHash\": \"hash\",\n"
            "  \"transactionIndex\": \"0xN\",\n"
            "  \"blockHash\": \"hash\",\n"
            "  \"blockNumber\": \"0xN\",\n"
            "  \"from\": \"address\",\n"
            "  \"to\": \"address\",\n"
            "  \"contractAddress\": \"address\",  // null if not a contract creation\n"
            "  \"gasUsed\": \"0xN\",\n"
            "  \"cumulativeGasUsed\": \"0xN\",\n"
            "  \"status\": \"0x1\",                // 1 = success, 0 = failure\n"
            "  \"logs\": [...],\n"
            "  \"logsBloom\": \"0x...\",\n"
            "  \"cascoin\": {                     // Cascoin-specific fields\n"
            "    \"senderReputation\": n,\n"
            "    \"reputationDiscount\": n,\n"
            "    \"usedFreeGas\": bool\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getTransactionReceipt", "\"0x...\"")
            + HelpExampleRpc("cas_getTransactionReceipt", "\"0x...\"")
        );

    std::string txHashStr = request.params[0].get_str();
    
    // Parse transaction hash
    uint256 txHash = ParseUint256(txHashStr);
    
    // Get receipt from database
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    CVM::TransactionReceipt receipt;
    if (!CVM::g_cvmdb->ReadReceipt(txHash, receipt)) {
        // Receipt not found - return null (Ethereum-compatible behavior)
        return NullUniValue;
    }
    
    // Convert receipt to JSON
    UniValue result(UniValue::VOBJ);
    result.pushKV("transactionHash", Uint256ToHex(receipt.transactionHash));
    result.pushKV("transactionIndex", strprintf("0x%x", receipt.transactionIndex));
    result.pushKV("blockHash", Uint256ToHex(receipt.blockHash));
    result.pushKV("blockNumber", strprintf("0x%x", receipt.blockNumber));
    result.pushKV("from", AddressToHex(receipt.from));
    result.pushKV("to", receipt.to.IsNull() ? "" : AddressToHex(receipt.to));
    result.pushKV("contractAddress", receipt.contractAddress.IsNull() ? "" : AddressToHex(receipt.contractAddress));
    result.pushKV("gasUsed", strprintf("0x%llx", (unsigned long long)receipt.gasUsed));
    result.pushKV("cumulativeGasUsed", strprintf("0x%llx", (unsigned long long)receipt.cumulativeGasUsed));
    result.pushKV("status", receipt.status ? "0x1" : "0x0");
    
    // Add logs
    UniValue logs(UniValue::VARR);
    for (const auto& log : receipt.logs) {
        UniValue logObj(UniValue::VOBJ);
        logObj.pushKV("address", AddressToHex(log.address));
        
        UniValue topics(UniValue::VARR);
        for (const auto& topic : log.topics) {
            topics.push_back(Uint256ToHex(topic));
        }
        logObj.pushKV("topics", topics);
        logObj.pushKV("data", "0x" + HexStr(log.data));
        logs.push_back(logObj);
    }
    result.pushKV("logs", logs);
    
    // Add Cascoin-specific fields
    UniValue cascoin(UniValue::VOBJ);
    cascoin.pushKV("senderReputation", receipt.senderReputation);
    cascoin.pushKV("reputationDiscount", (uint64_t)receipt.reputationDiscount);
    cascoin.pushKV("usedFreeGas", receipt.usedFreeGas);
    result.pushKV("cascoin", cascoin);
    
    return result;
}

UniValue cas_getBalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "cas_getBalance \"address\" ( \"block\" )\n"
            "\nGet CAS balance for an address.\n"
            "\nArguments:\n"
            "1. address         (string, required) Address (hex format)\n"
            "2. block           (string, optional) Block number or \"latest\" (default: \"latest\")\n"
            "\nResult:\n"
            "\"balance\"                 (string) Balance in satoshis (wei) as hex string\n"
            "\nNote: Returns balance from UTXO set. For contract balances, use contract-specific methods.\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getBalance", "\"0x...\"")
            + HelpExampleRpc("cas_getBalance", "\"0x...\"")
        );

    std::string addressStr = request.params[0].get_str();
    
    // Parse address
    uint160 address = ParseAddress(addressStr);
    
    // Check if address index is available
    if (!CVM::g_addressIndex) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Address index not initialized");
    }
    
    // Get balance from address index
    CAmount balance = CVM::g_addressIndex->GetAddressBalance(address);
    
    // Return balance as hex string (Ethereum-compatible)
    std::stringstream ss;
    ss << "0x" << std::hex << balance;
    return ss.str();
}

UniValue cas_getTransactionCount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "cas_getTransactionCount \"address\" ( \"block\" )\n"
            "\nGet transaction count (nonce) for an address.\n"
            "\nThis returns the number of transactions sent from an address.\n"
            "For contracts, this is used for CREATE2 address generation.\n"
            "\nArguments:\n"
            "1. address         (string, required) Address (hex format)\n"
            "2. block           (string, optional) Block number or \"latest\" (default: \"latest\")\n"
            "\nResult:\n"
            "\"count\"                   (string) Transaction count as hex string\n"
            "\nNote: This is the nonce value used for the next transaction from this address.\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getTransactionCount", "\"0x...\"")
            + HelpExampleRpc("cas_getTransactionCount", "\"0x...\"")
        );

    std::string addressStr = request.params[0].get_str();
    
    // Parse address
    uint160 address = ParseAddress(addressStr);
    
    // Get nonce from database
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    uint64_t nonce = 0;
    if (!CVM::g_cvmdb->ReadNonce(address, nonce)) {
        // Address has no transactions yet, nonce is 0
        nonce = 0;
    }
    
    // Return nonce as hex string (Ethereum-compatible)
    return strprintf("0x%llx", (unsigned long long)nonce);
}

// ===== Ethereum-Compatible Aliases (eth_*) =====

UniValue eth_blockNumber(const JSONRPCRequest& request)
{
    return cas_blockNumber(request);
}

UniValue eth_gasPrice(const JSONRPCRequest& request)
{
    return cas_gasPrice(request);
}

UniValue eth_call(const JSONRPCRequest& request)
{
    return cas_call(request);
}

UniValue eth_estimateGas(const JSONRPCRequest& request)
{
    return cas_estimateGas(request);
}

UniValue eth_getCode(const JSONRPCRequest& request)
{
    return cas_getCode(request);
}

UniValue eth_getStorageAt(const JSONRPCRequest& request)
{
    return cas_getStorageAt(request);
}

UniValue eth_sendTransaction(const JSONRPCRequest& request)
{
    return cas_sendTransaction(request);
}

UniValue eth_getTransactionReceipt(const JSONRPCRequest& request)
{
    return cas_getTransactionReceipt(request);
}

UniValue eth_getBalance(const JSONRPCRequest& request)
{
    return cas_getBalance(request);
}

UniValue eth_getTransactionCount(const JSONRPCRequest& request)
{
    return cas_getTransactionCount(request);
}

// ===== Developer Tooling Methods =====

// Global state for snapshots (regtest only)
static std::map<uint64_t, CBlockIndex*> g_snapshots;
static uint64_t g_nextSnapshotId = 1;
static int64_t g_timeOffset = 0;
static int64_t g_nextBlockTimestamp = 0;

UniValue debug_traceTransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "debug_traceTransaction \"txhash\" ( {\"tracer\":\"xxx\"} )\n"
            "\nTrace transaction execution with detailed opcode-level information.\n"
            "\nArguments:\n"
            "1. \"txhash\"        (string, required) Transaction hash\n"
            "2. options         (object, optional) Tracing options\n"
            "   {\n"
            "     \"tracer\": \"xxx\"     (string, optional) Tracer type (default: \"callTracer\")\n"
            "     \"timeout\": \"xxx\"    (string, optional) Timeout (default: \"5s\")\n"
            "   }\n"
            "\nResult:\n"
            "{\n"
            "  \"gas\": n,                (numeric) Gas used\n"
            "  \"failed\": bool,          (boolean) Execution failed\n"
            "  \"returnValue\": \"xxx\",    (string) Return data\n"
            "  \"structLogs\": [...]      (array) Execution trace\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("debug_traceTransaction", "\"0x...\"")
            + HelpExampleRpc("debug_traceTransaction", "\"0x...\"")
        );

    std::string txhash = request.params[0].get_str();
    
    // Parse transaction hash
    uint256 hash = ParseUint256(txhash);
    
    // Find transaction in blockchain
    CTransactionRef tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not found");
    }
    
    // Find block containing transaction
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(hashBlock);
        if (it != mapBlockIndex.end()) {
            pindex = it->second;
        }
    }
    
    if (!pindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }
    
    // Parse tracer options
    std::string tracerType = "default";
    UniValue tracerOptions(UniValue::VOBJ);
    
    if (request.params.size() > 1 && request.params[1].isObject()) {
        const UniValue& opts = request.params[1];
        if (opts.exists("tracer") && opts["tracer"].isStr()) {
            tracerType = opts["tracer"].get_str();
        }
        if (opts.exists("tracerConfig") && opts["tracerConfig"].isObject()) {
            tracerOptions = opts["tracerConfig"];
        }
    }
    
    // Create tracer
    auto tracer = CVM::TracerFactory::CreateTracer(tracerType);
    CVM::TracerFactory::ParseTracerOptions(tracer.get(), tracerOptions);
    
    // Check if this is a CVM/EVM transaction
    int cvmOutputIndex = CVM::FindCVMOpReturn(*tx);
    if (cvmOutputIndex < 0) {
        // Not a CVM transaction
        UniValue result(UniValue::VOBJ);
        result.pushKV("gas", 21000);
        result.pushKV("failed", false);
        result.pushKV("returnValue", "0x");
        result.pushKV("structLogs", UniValue(UniValue::VARR));
        return result;
    }
    
    // Parse CVM data
    CVM::CVMOpType opType;
    std::vector<uint8_t> data;
    if (!CVM::ParseCVMOpReturn(tx->vout[cvmOutputIndex], opType, data)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid CVM OP_RETURN format");
    }
    
    // Create CVM database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
    
    // Create trust context
    auto trustContext = std::make_shared<CVM::TrustContext>(&db);
    
    // Get sender address
    uint160 senderAddr;
    if (!tx->vin.empty()) {
        // Extract from first input (simplified)
        senderAddr = uint160();
    }
    
    // Set caller reputation in tracer
    CVM::ReputationSystem repSystem(db);
    CVM::ReputationScore repScore;
    int16_t reputation = 0;
    if (repSystem.GetReputation(senderAddr, repScore)) {
        reputation = repScore.score;
    }
    tracer->SetCallerReputation(reputation);
    
    // Create EnhancedVM with tracer
    CVM::EnhancedVM vm(&db, trustContext);
    
    // Start tracing
    tracer->StartTrace(hash);
    
    // Execute transaction with tracing
    CVM::EnhancedExecutionResult execResult;
    
    if (opType == CVM::CVMOpType::CONTRACT_DEPLOY || opType == CVM::CVMOpType::EVM_DEPLOY) {
        // Parse deployment data
        CVM::CVMDeployData deployData;
        if (!CVM::ParseCVMDeployData(data, deployData)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid deployment data");
        }
        
        // Execute deployment with tracing
        execResult = vm.DeployContract(
            deployData.bytecode,
            deployData.constructorData,
            deployData.gasLimit,
            senderAddr,
            0, // value
            pindex->nHeight,
            pindex->GetBlockHash(),
            pindex->nTime
        );
    } else if (opType == CVM::CVMOpType::CONTRACT_CALL || opType == CVM::CVMOpType::EVM_CALL) {
        // Parse call data
        CVM::CVMCallData callData;
        if (!CVM::ParseCVMCallData(data, callData)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid call data");
        }
        
        // Execute call with tracing
        execResult = vm.CallContract(
            callData.contractAddress,
            callData.callData,
            callData.gasLimit,
            senderAddr,
            0, // value
            pindex->nHeight,
            pindex->GetBlockHash(),
            pindex->nTime
        );
    }
    
    // Stop tracing and get trace
    CVM::ExecutionTrace trace = tracer->StopTrace();
    
    // Set trace data from execution result
    trace.totalGas = execResult.gas_used;
    trace.failed = !execResult.success;
    trace.returnValue = "0x" + HexStr(execResult.return_data);
    trace.reputationGasDiscount = execResult.reputation_gas_discount;
    trace.trustGatePassed = execResult.trust_gate_passed;
    
    // Convert to JSON
    return trace.ToJSON(tracerType);
}

UniValue debug_traceCall(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "debug_traceCall {\"to\":\"address\",\"data\":\"hex\"} ( \"block\" {\"tracer\":\"xxx\"} )\n"
            "\nTrace simulated contract call execution.\n"
            "\nArguments:\n"
            "1. call            (object, required) Call object\n"
            "   {\n"
            "     \"to\": \"address\"       (string, required) Contract address\n"
            "     \"data\": \"hex\"         (string, required) Call data\n"
            "     \"from\": \"address\"     (string, optional) Caller address\n"
            "     \"gas\": n              (numeric, optional) Gas limit\n"
            "   }\n"
            "2. block           (string, optional) Block number or \"latest\"\n"
            "3. options         (object, optional) Tracing options\n"
            "\nResult:\n"
            "{\n"
            "  \"gas\": n,                (numeric) Gas used\n"
            "  \"failed\": bool,          (boolean) Execution failed\n"
            "  \"returnValue\": \"xxx\",    (string) Return data\n"
            "  \"structLogs\": [...]      (array) Execution trace\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("debug_traceCall", "'{\"to\":\"0x...\",\"data\":\"0x...\"}'")
            + HelpExampleRpc("debug_traceCall", "{\"to\":\"0x...\",\"data\":\"0x...\"}")
        );

    // Parse call parameters
    const UniValue& callObj = request.params[0];
    
    if (!callObj.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Call parameter must be an object");
    }
    
    // Extract call parameters
    if (!callObj.exists("to") || !callObj["to"].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing 'to' address");
    }
    
    uint160 contractAddr = ParseAddress(callObj["to"].get_str());
    
    std::vector<uint8_t> callData;
    if (callObj.exists("data") && callObj["data"].isStr()) {
        std::string dataHex = callObj["data"].get_str();
        if (dataHex.substr(0, 2) == "0x") {
            dataHex = dataHex.substr(2);
        }
        callData = ParseHex(dataHex);
    }
    
    uint160 callerAddr;
    if (callObj.exists("from") && callObj["from"].isStr()) {
        callerAddr = ParseAddress(callObj["from"].get_str());
    }
    
    uint64_t gasLimit = 10000000; // Default 10M gas
    if (callObj.exists("gas") && callObj["gas"].isNum()) {
        gasLimit = callObj["gas"].get_int64();
    }
    
    // Parse tracer options
    std::string tracerType = "default";
    UniValue tracerOptions(UniValue::VOBJ);
    
    if (request.params.size() > 2 && request.params[2].isObject()) {
        const UniValue& opts = request.params[2];
        if (opts.exists("tracer") && opts["tracer"].isStr()) {
            tracerType = opts["tracer"].get_str();
        }
        if (opts.exists("tracerConfig") && opts["tracerConfig"].isObject()) {
            tracerOptions = opts["tracerConfig"];
        }
    }
    
    // Create tracer
    auto tracer = CVM::TracerFactory::CreateTracer(tracerType);
    CVM::TracerFactory::ParseTracerOptions(tracer.get(), tracerOptions);
    
    // Create CVM database instance
    CVM::CVMDatabase db(GetDataDir() / "cvm", 100 * 1024 * 1024, false, false);
    
    // Create trust context
    auto trustContext = std::make_shared<CVM::TrustContext>(&db);
    
    // Set caller reputation in tracer
    CVM::ReputationSystem repSystem(db);
    CVM::ReputationScore repScore;
    int16_t reputation = 0;
    if (repSystem.GetReputation(callerAddr, repScore)) {
        reputation = repScore.score;
    }
    tracer->SetCallerReputation(reputation);
    
    // Create EnhancedVM
    CVM::EnhancedVM vm(&db, trustContext);
    
    // Start tracing
    tracer->StartTrace();
    
    // Get current block info
    int blockHeight = chainActive.Height();
    uint256 blockHash = chainActive.Tip()->GetBlockHash();
    int64_t timestamp = chainActive.Tip()->nTime;
    
    // Execute call with tracing
    CVM::EnhancedExecutionResult execResult = vm.CallContract(
        contractAddr,
        callData,
        gasLimit,
        callerAddr,
        0, // value
        blockHeight,
        blockHash,
        timestamp
    );
    
    // Stop tracing and get trace
    CVM::ExecutionTrace trace = tracer->StopTrace();
    
    // Set trace data from execution result
    trace.totalGas = execResult.gas_used;
    trace.failed = !execResult.success;
    trace.returnValue = "0x" + HexStr(execResult.return_data);
    trace.reputationGasDiscount = execResult.reputation_gas_discount;
    trace.trustGatePassed = execResult.trust_gate_passed;
    
    // Convert to JSON
    return trace.ToJSON(tracerType);
}

UniValue cas_snapshot(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "cas_snapshot\n"
            "\nCreate a snapshot of the current blockchain state.\n"
            "\nOnly available in regtest mode for testing.\n"
            "\nResult:\n"
            "\"id\"                      (string) Snapshot ID\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_snapshot", "")
            + HelpExampleRpc("cas_snapshot", "")
        );

    if (!Params().MineBlocksOnDemand()) {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Snapshots only available in regtest mode");
    }

    LOCK(cs_main);
    
    // Store current chain tip
    uint64_t snapshotId = g_nextSnapshotId++;
    g_snapshots[snapshotId] = chainActive.Tip();
    
    return strprintf("0x%llx", (unsigned long long)snapshotId);
}

UniValue evm_snapshot(const JSONRPCRequest& request)
{
    return cas_snapshot(request);
}

UniValue cas_revert(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_revert \"snapshotid\"\n"
            "\nRevert blockchain state to a previous snapshot.\n"
            "\nOnly available in regtest mode for testing.\n"
            "\nArguments:\n"
            "1. \"snapshotid\"    (string, required) Snapshot ID from cas_snapshot\n"
            "\nResult:\n"
            "true|false           (boolean) Success\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_revert", "\"0x1\"")
            + HelpExampleRpc("cas_revert", "\"0x1\"")
        );

    if (!Params().MineBlocksOnDemand()) {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Snapshots only available in regtest mode");
    }

    std::string snapshotIdStr = request.params[0].get_str();
    uint64_t snapshotId = std::stoull(snapshotIdStr, nullptr, 16);
    
    LOCK(cs_main);
    
    // Find snapshot
    auto it = g_snapshots.find(snapshotId);
    if (it == g_snapshots.end()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Snapshot not found");
    }
    
    CBlockIndex* pindex = it->second;
    if (!pindex) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid snapshot");
    }
    
    // Revert to snapshot block
    CValidationState state;
    if (!InvalidateBlock(state, Params(), pindex->pprev)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }
    
    // Activate the snapshot block
    if (!ActivateBestChain(state, Params())) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }
    
    return true;
}

UniValue evm_revert(const JSONRPCRequest& request)
{
    return cas_revert(request);
}

UniValue cas_mine(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 0 || request.params.size() > 1)
        throw std::runtime_error(
            "cas_mine ( numblocks )\n"
            "\nMine one or more blocks immediately.\n"
            "\nOnly available in regtest mode for testing.\n"
            "\nArguments:\n"
            "1. numblocks       (numeric, optional, default=1) Number of blocks to mine\n"
            "\nResult:\n"
            "[                   (array) Block hashes\n"
            "  \"hash\",           (string) Block hash\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_mine", "")
            + HelpExampleCli("cas_mine", "5")
            + HelpExampleRpc("cas_mine", "5")
        );

    if (!Params().MineBlocksOnDemand()) {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Mining only available in regtest mode");
    }

    int numBlocks = 1;
    if (request.params.size() > 0) {
        numBlocks = request.params[0].get_int();
    }
    
    if (numBlocks < 1 || numBlocks > 1000) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Number of blocks must be between 1 and 1000");
    }
    
    // Get mining address (use first address in wallet)
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_ERROR, "No wallet available");
    }
    
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Keypool ran out");
    }
    
    CTxDestination dest = newKey.GetID();
    CScript scriptPubKey = GetScriptForDestination(dest);
    
    UniValue blockHashes(UniValue::VARR);
    
    for (int i = 0; i < numBlocks; i++) {
        // Apply time offset if set
        if (g_nextBlockTimestamp > 0) {
            SetMockTime(g_nextBlockTimestamp);
            g_nextBlockTimestamp = 0;
        } else if (g_timeOffset > 0) {
            SetMockTime(GetTime() + g_timeOffset);
        }
        
        // Generate block
        std::unique_ptr<CBlockTemplate> pblocktemplate = BlockAssembler(Params()).CreateNewBlock(scriptPubKey);
        if (!pblocktemplate) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to create block template");
        }
        
        CBlock* pblock = &pblocktemplate->block;
        
        // Mine the block
        while (!CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus())) {
            ++pblock->nNonce;
        }
        
        // Process the block
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to process block");
        }
        
        blockHashes.push_back(pblock->GetHash().GetHex());
    }
    
    return blockHashes;
}

UniValue evm_mine(const JSONRPCRequest& request)
{
    return cas_mine(request);
}

UniValue cas_setNextBlockTimestamp(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_setNextBlockTimestamp timestamp\n"
            "\nSet the timestamp for the next block to be mined.\n"
            "\nOnly available in regtest mode for testing.\n"
            "\nArguments:\n"
            "1. timestamp       (numeric, required) Unix timestamp\n"
            "\nResult:\n"
            "timestamp          (numeric) The timestamp that was set\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_setNextBlockTimestamp", "1609459200")
            + HelpExampleRpc("cas_setNextBlockTimestamp", "1609459200")
        );

    if (!Params().MineBlocksOnDemand()) {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Time manipulation only available in regtest mode");
    }

    int64_t timestamp = request.params[0].get_int64();
    
    if (timestamp < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Timestamp must be non-negative");
    }
    
    g_nextBlockTimestamp = timestamp;
    
    return timestamp;
}

UniValue evm_setNextBlockTimestamp(const JSONRPCRequest& request)
{
    return cas_setNextBlockTimestamp(request);
}

UniValue cas_increaseTime(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_increaseTime seconds\n"
            "\nIncrease blockchain time by specified seconds.\n"
            "\nOnly available in regtest mode for testing.\n"
            "\nArguments:\n"
            "1. seconds         (numeric, required) Seconds to advance\n"
            "\nResult:\n"
            "timestamp          (numeric) New timestamp\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_increaseTime", "3600")
            + HelpExampleRpc("cas_increaseTime", "3600")
        );

    if (!Params().MineBlocksOnDemand()) {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Time manipulation only available in regtest mode");
    }

    int64_t seconds = request.params[0].get_int64();
    
    if (seconds < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Seconds must be non-negative");
    }
    
    g_timeOffset += seconds;
    int64_t newTime = GetTime() + g_timeOffset;
    SetMockTime(newTime);
    
    return newTime;
}

UniValue evm_increaseTime(const JSONRPCRequest& request)
{
    return cas_increaseTime(request);
}

// ===== Cross-Chain Trust RPC Methods =====

UniValue cas_getCrossChainTrust(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_getCrossChainTrust \"address\"\n"
            "\nGet cross-chain trust scores for an address.\n"
            "\nArguments:\n"
            "1. address         (string, required) The address to query\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"0x...\",           (string) The queried address\n"
            "  \"aggregatedScore\": n,           (numeric) Aggregated trust score (0-100)\n"
            "  \"chainScores\": [                (array) Trust scores from each chain\n"
            "    {\n"
            "      \"chainId\": n,               (numeric) Chain ID\n"
            "      \"chainName\": \"...\",       (string) Chain name\n"
            "      \"trustScore\": n,            (numeric) Trust score from this chain\n"
            "      \"timestamp\": n,             (numeric) When score was recorded\n"
            "      \"verified\": true|false      (boolean) Whether score is verified\n"
            "    }\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getCrossChainTrust", "\"0x1234567890123456789012345678901234567890\"")
            + HelpExampleRpc("cas_getCrossChainTrust", "\"0x1234567890123456789012345678901234567890\"")
        );

    std::string addrStr = request.params[0].get_str();
    uint160 address = ParseAddress(addrStr);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", AddressToHex(address));
    
    if (!CVM::g_crossChainBridge) {
        result.pushKV("aggregatedScore", 0);
        result.pushKV("chainScores", UniValue(UniValue::VARR));
        result.pushKV("error", "Cross-chain bridge not initialized");
        return result;
    }
    
    // Get aggregated score
    uint8_t aggregatedScore = CVM::g_crossChainBridge->GetAggregatedTrustScore(address);
    result.pushKV("aggregatedScore", aggregatedScore);
    
    // Get individual chain scores
    auto scores = CVM::g_crossChainBridge->GetCrossChainTrustScores(address);
    UniValue chainScores(UniValue::VARR);
    
    for (const auto& score : scores) {
        UniValue scoreObj(UniValue::VOBJ);
        scoreObj.pushKV("chainId", score.chainId);
        
        // Get chain name
        const CVM::ChainConfig* config = CVM::g_crossChainBridge->GetChainConfig(score.chainId);
        if (config) {
            scoreObj.pushKV("chainName", config->chainName);
        } else {
            scoreObj.pushKV("chainName", "Unknown");
        }
        
        scoreObj.pushKV("trustScore", score.trustScore);
        scoreObj.pushKV("timestamp", (int64_t)score.timestamp);
        scoreObj.pushKV("verified", score.isVerified);
        chainScores.push_back(scoreObj);
    }
    
    result.pushKV("chainScores", chainScores);
    
    return result;
}

UniValue cas_getSupportedChains(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "cas_getSupportedChains\n"
            "\nGet list of supported cross-chain bridges.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"chainId\": n,                 (numeric) Chain ID\n"
            "    \"chainName\": \"...\",         (string) Chain name\n"
            "    \"isActive\": true|false,       (boolean) Whether bridge is active\n"
            "    \"minConfirmations\": n         (numeric) Minimum confirmations required\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getSupportedChains", "")
            + HelpExampleRpc("cas_getSupportedChains", "")
        );

    UniValue result(UniValue::VARR);
    
    if (!CVM::g_crossChainBridge) {
        return result;
    }
    
    auto chainIds = CVM::g_crossChainBridge->GetSupportedChains();
    
    for (uint16_t chainId : chainIds) {
        const CVM::ChainConfig* config = CVM::g_crossChainBridge->GetChainConfig(chainId);
        if (config) {
            UniValue chainObj(UniValue::VOBJ);
            chainObj.pushKV("chainId", config->chainId);
            chainObj.pushKV("chainName", config->chainName);
            chainObj.pushKV("isActive", config->isActive);
            chainObj.pushKV("minConfirmations", (int64_t)config->minConfirmations);
            chainObj.pushKV("maxAttestationAge", (int64_t)config->maxAttestationAge);
            result.push_back(chainObj);
        }
    }
    
    return result;
}

UniValue cas_generateTrustProof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "cas_generateTrustProof \"address\"\n"
            "\nGenerate a trust state proof for an address.\n"
            "\nThis proof can be used to verify trust scores on other chains.\n"
            "\nArguments:\n"
            "1. address         (string, required) The address to generate proof for\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"0x...\",           (string) The address\n"
            "  \"trustScore\": n,                (numeric) Trust score (0-100)\n"
            "  \"blockHeight\": n,               (numeric) Block height of proof\n"
            "  \"blockHash\": \"...\",           (string) Block hash\n"
            "  \"stateRoot\": \"...\",           (string) State root\n"
            "  \"proofHash\": \"...\"            (string) Hash of the proof\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_generateTrustProof", "\"0x1234567890123456789012345678901234567890\"")
            + HelpExampleRpc("cas_generateTrustProof", "\"0x1234567890123456789012345678901234567890\"")
        );

    std::string addrStr = request.params[0].get_str();
    uint160 address = ParseAddress(addrStr);
    
    if (!CVM::g_crossChainBridge) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Cross-chain bridge not initialized");
    }
    
    CVM::TrustStateProof proof = CVM::g_crossChainBridge->GenerateTrustStateProof(address);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", AddressToHex(proof.address));
    result.pushKV("trustScore", proof.trustScore);
    result.pushKV("blockHeight", (int64_t)proof.blockHeight);
    result.pushKV("blockHash", Uint256ToHex(proof.blockHash));
    result.pushKV("stateRoot", Uint256ToHex(proof.stateRoot));
    result.pushKV("proofHash", Uint256ToHex(proof.GetHash()));
    
    return result;
}

UniValue cas_verifyTrustProof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 4)
        throw std::runtime_error(
            "cas_verifyTrustProof \"address\" trustScore \"stateRoot\" sourceChainId\n"
            "\nVerify a trust state proof from another chain.\n"
            "\nArguments:\n"
            "1. address         (string, required) The address\n"
            "2. trustScore      (numeric, required) Claimed trust score (0-100)\n"
            "3. stateRoot       (string, required) State root from source chain\n"
            "4. sourceChainId   (numeric, required) Source chain ID\n"
            "\nResult:\n"
            "{\n"
            "  \"valid\": true|false,            (boolean) Whether proof is valid\n"
            "  \"address\": \"0x...\",           (string) The address\n"
            "  \"trustScore\": n,                (numeric) Verified trust score\n"
            "  \"sourceChain\": \"...\"          (string) Source chain name\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_verifyTrustProof", "\"0x1234...\" 75 \"0xabcd...\" 1")
            + HelpExampleRpc("cas_verifyTrustProof", "\"0x1234...\", 75, \"0xabcd...\", 1")
        );

    std::string addrStr = request.params[0].get_str();
    uint160 address = ParseAddress(addrStr);
    int trustScore = request.params[1].get_int();
    std::string stateRootStr = request.params[2].get_str();
    int sourceChainId = request.params[3].get_int();
    
    if (trustScore < 0 || trustScore > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Trust score must be 0-100");
    }
    
    if (!CVM::g_crossChainBridge) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Cross-chain bridge not initialized");
    }
    
    // Create a proof structure for verification
    CVM::TrustStateProof proof;
    proof.address = address;
    proof.trustScore = static_cast<uint8_t>(trustScore);
    proof.stateRoot = ParseUint256(stateRootStr);
    
    bool valid = CVM::g_crossChainBridge->VerifyTrustStateProof(proof, static_cast<uint16_t>(sourceChainId));
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("valid", valid);
    result.pushKV("address", AddressToHex(address));
    result.pushKV("trustScore", trustScore);
    
    const CVM::ChainConfig* config = CVM::g_crossChainBridge->GetChainConfig(static_cast<uint16_t>(sourceChainId));
    if (config) {
        result.pushKV("sourceChain", config->chainName);
    } else {
        result.pushKV("sourceChain", "Unknown");
    }
    
    return result;
}

UniValue cas_getCrossChainStats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "cas_getCrossChainStats\n"
            "\nGet cross-chain trust bridge statistics.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalAttestations\": n,         (numeric) Total attestations stored\n"
            "  \"supportedChains\": n,           (numeric) Number of supported chains\n"
            "  \"attestationsByChain\": {        (object) Attestations per chain\n"
            "    \"chainName\": n\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_getCrossChainStats", "")
            + HelpExampleRpc("cas_getCrossChainStats", "")
        );

    UniValue result(UniValue::VOBJ);
    
    if (!CVM::g_crossChainBridge) {
        result.pushKV("error", "Cross-chain bridge not initialized");
        return result;
    }
    
    result.pushKV("totalAttestations", (int64_t)CVM::g_crossChainBridge->GetAttestationCount());
    result.pushKV("supportedChains", (int64_t)CVM::g_crossChainBridge->GetSupportedChains().size());
    
    auto countsByChain = CVM::g_crossChainBridge->GetAttestationCountByChain();
    UniValue chainCounts(UniValue::VOBJ);
    
    for (const auto& pair : countsByChain) {
        const CVM::ChainConfig* config = CVM::g_crossChainBridge->GetChainConfig(pair.first);
        std::string chainName = config ? config->chainName : "Unknown";
        chainCounts.pushKV(chainName, (int64_t)pair.second);
    }
    
    result.pushKV("attestationsByChain", chainCounts);
    
    return result;
}

UniValue cas_sendTrustAttestation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "cas_sendTrustAttestation \"address\" trustScore destChainId\n"
            "\nSend a trust attestation to another chain.\n"
            "\nArguments:\n"
            "1. address         (string, required) The address to attest\n"
            "2. trustScore      (numeric, required) Trust score (0-100)\n"
            "3. destChainId     (numeric, required) Destination chain ID\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,          (boolean) Whether attestation was sent\n"
            "  \"attestationHash\": \"...\"      (string) Hash of the attestation\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cas_sendTrustAttestation", "\"0x1234...\" 75 1")
            + HelpExampleRpc("cas_sendTrustAttestation", "\"0x1234...\", 75, 1")
        );

    std::string addrStr = request.params[0].get_str();
    uint160 address = ParseAddress(addrStr);
    int trustScore = request.params[1].get_int();
    int destChainId = request.params[2].get_int();
    
    if (trustScore < 0 || trustScore > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Trust score must be 0-100");
    }
    
    if (!CVM::g_crossChainBridge) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Cross-chain bridge not initialized");
    }
    
    // Create attestation
    CVM::TrustAttestation attestation;
    attestation.address = address;
    attestation.trustScore = static_cast<int16_t>(trustScore);
    attestation.source = CVM::AttestationSource::CASCOIN_MAINNET;
    attestation.timestamp = GetTime();
    attestation.attestationHash = attestation.GetHash();
    
    // Send attestation
    bool success = CVM::g_crossChainBridge->SendTrustAttestation(
        static_cast<uint16_t>(destChainId), address, attestation);
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("attestationHash", Uint256ToHex(attestation.attestationHash));
    
    return result;
}
