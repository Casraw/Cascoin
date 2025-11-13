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
#include <validation.h>
#include <chainparams.h>
#include <util.h>
#include <utilstrencodings.h>
#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <rpc/util.h>
#include <core_io.h>

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

    // TODO: Requires wallet integration
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "cas_sendTransaction requires wallet integration (not yet implemented)");
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
