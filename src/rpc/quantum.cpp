// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file quantum.cpp
 * @brief RPC commands for the FALCON-512 Public Key Registry
 *
 * Provides RPC interface for querying and monitoring the quantum public key
 * registry used for post-quantum transaction optimization.
 *
 * Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6
 */

#include <rpc/server.h>
#include <quantum_registry.h>
#include <utilstrencodings.h>
#include <univalue.h>

/**
 * getquantumpubkey "hash"
 *
 * Returns the full FALCON-512 public key for a given hash.
 *
 * Arguments:
 * 1. hash    (string, required) The 32-byte public key hash (hex)
 *
 * Result:
 * {
 *   "pubkey": "hex",     (string) The 897-byte public key
 *   "hash": "hex"        (string) The hash (for verification)
 * }
 *
 * Requirements: 7.1, 7.2
 */
UniValue getquantumpubkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getquantumpubkey \"hash\"\n"
            "\nReturns the full FALCON-512 public key for a given hash.\n"
            "\nArguments:\n"
            "1. \"hash\"    (string, required) The 32-byte public key hash (hex, 64 characters)\n"
            "\nResult:\n"
            "{\n"
            "  \"pubkey\": \"hex\",     (string) The 897-byte FALCON-512 public key\n"
            "  \"hash\": \"hex\"        (string) The hash (for verification)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getquantumpubkey", "\"a1b2c3d4e5f6...\"")
            + HelpExampleRpc("getquantumpubkey", "\"a1b2c3d4e5f6...\"")
        );

    // Check if quantum registry is initialized
    if (!g_quantumRegistry || !g_quantumRegistry->IsInitialized()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Quantum registry not initialized");
    }

    // Parse the hash parameter
    std::string hashHex = request.params[0].get_str();
    if (hashHex.substr(0, 2) == "0x") {
        hashHex = hashHex.substr(2);
    }

    if (hashHex.length() != 64) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "Invalid hash length (expected 64 hex characters for 32-byte hash)");
    }

    uint256 hash;
    hash.SetHex(hashHex);

    // Look up the public key
    std::vector<unsigned char> pubkey;
    if (!g_quantumRegistry->LookupPubKey(hash, pubkey)) {
        // Requirements: 7.2 - Return error code -32001 when hash is not found
        throw JSONRPCError(-32001, "Quantum public key not registered");
    }

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("pubkey", HexStr(pubkey));
    result.pushKV("hash", hash.GetHex());

    return result;
}

/**
 * getquantumregistrystats
 *
 * Returns statistics about the quantum public key registry.
 *
 * Result:
 * {
 *   "total_keys": n,           (numeric) Total registered public keys
 *   "database_size": n,        (numeric) Database size in bytes
 *   "cache_hits": n,           (numeric) Cache hit count
 *   "cache_misses": n          (numeric) Cache miss count
 * }
 *
 * Requirements: 7.3, 7.4
 */
UniValue getquantumregistrystats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getquantumregistrystats\n"
            "\nReturns statistics about the quantum public key registry.\n"
            "\nResult:\n"
            "{\n"
            "  \"total_keys\": n,           (numeric) Total registered public keys\n"
            "  \"database_size\": n,        (numeric) Database size in bytes\n"
            "  \"cache_hits\": n,           (numeric) Cache hit count\n"
            "  \"cache_misses\": n,         (numeric) Cache miss count\n"
            "  \"cache_hit_rate\": n        (numeric) Cache hit rate percentage\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getquantumregistrystats", "")
            + HelpExampleRpc("getquantumregistrystats", "")
        );

    // Check if quantum registry is initialized
    if (!g_quantumRegistry || !g_quantumRegistry->IsInitialized()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Quantum registry not initialized");
    }

    // Get statistics
    QuantumRegistryStats stats = g_quantumRegistry->GetStats();

    // Calculate cache hit rate
    double hitRate = 0.0;
    uint64_t totalAccesses = stats.cacheHits + stats.cacheMisses;
    if (totalAccesses > 0) {
        hitRate = (static_cast<double>(stats.cacheHits) / totalAccesses) * 100.0;
    }

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("total_keys", static_cast<int64_t>(stats.totalKeys));
    result.pushKV("database_size", static_cast<int64_t>(stats.databaseSizeBytes));
    result.pushKV("cache_hits", static_cast<int64_t>(stats.cacheHits));
    result.pushKV("cache_misses", static_cast<int64_t>(stats.cacheMisses));
    result.pushKV("cache_hit_rate", hitRate);

    return result;
}

/**
 * isquantumpubkeyregistered "hash"
 *
 * Checks if a public key hash is registered.
 *
 * Arguments:
 * 1. hash    (string, required) The 32-byte public key hash (hex)
 *
 * Result:
 * true|false    (boolean) Whether the key is registered
 *
 * Requirements: 7.5
 */
UniValue isquantumpubkeyregistered(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "isquantumpubkeyregistered \"hash\"\n"
            "\nChecks if a public key hash is registered in the quantum registry.\n"
            "\nArguments:\n"
            "1. \"hash\"    (string, required) The 32-byte public key hash (hex, 64 characters)\n"
            "\nResult:\n"
            "true|false    (boolean) Whether the key is registered\n"
            "\nExamples:\n"
            + HelpExampleCli("isquantumpubkeyregistered", "\"a1b2c3d4e5f6...\"")
            + HelpExampleRpc("isquantumpubkeyregistered", "\"a1b2c3d4e5f6...\"")
        );

    // Check if quantum registry is initialized
    if (!g_quantumRegistry || !g_quantumRegistry->IsInitialized()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Quantum registry not initialized");
    }

    // Parse the hash parameter
    std::string hashHex = request.params[0].get_str();
    if (hashHex.substr(0, 2) == "0x") {
        hashHex = hashHex.substr(2);
    }

    if (hashHex.length() != 64) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "Invalid hash length (expected 64 hex characters for 32-byte hash)");
    }

    uint256 hash;
    hash.SetHex(hashHex);

    // Check if registered
    bool isRegistered = g_quantumRegistry->IsRegistered(hash);

    return UniValue(isRegistered);
}

// RPC command table for quantum registry
// Requirements: 7.6 - Commands categorized under "quantum" help category
static const CRPCCommand commands[] =
{
    //  category              name                          actor (function)           argNames
    //  --------------------- ---------------------------- -------------------------- ----------
    { "quantum",             "getquantumpubkey",           &getquantumpubkey,         {"hash"} },
    { "quantum",             "getquantumregistrystats",    &getquantumregistrystats,  {} },
    { "quantum",             "isquantumpubkeyregistered",  &isquantumpubkeyregistered, {"hash"} },
};

/**
 * Register quantum RPC commands
 * Requirements: 7.6
 */
void RegisterQuantumRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
