// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_EVM_RPC_H
#define CASCOIN_CVM_EVM_RPC_H

#include <univalue.h>
#include <rpc/server.h>

/**
 * Ethereum-compatible RPC methods for Cascoin CVM/EVM
 * 
 * These methods provide Ethereum-compatible interfaces for interacting
 * with CVM/EVM contracts, allowing Ethereum developers to use familiar
 * tools and workflows while benefiting from Cascoin's trust-aware features.
 */

/**
 * eth_sendTransaction - Send a transaction to deploy or call a contract
 * 
 * Ethereum-compatible method that creates and broadcasts a CVM/EVM transaction.
 * Integrates with Cascoin's reputation system for gas discounts.
 * 
 * @param request JSON-RPC request with transaction parameters
 * @return Transaction hash
 */
UniValue eth_sendTransaction(const JSONRPCRequest& request);

/**
 * eth_call - Execute a contract call without creating a transaction
 * 
 * Read-only execution that doesn't modify state. Useful for querying
 * contract data without paying gas fees.
 * 
 * @param request JSON-RPC request with call parameters
 * @return Call result data
 */
UniValue eth_call(const JSONRPCRequest& request);

/**
 * eth_estimateGas - Estimate gas required for a transaction
 * 
 * Estimates gas with Cascoin's reputation-based adjustments.
 * Takes into account free gas eligibility and gas discounts.
 * 
 * @param request JSON-RPC request with transaction parameters
 * @return Estimated gas amount
 */
UniValue eth_estimateGas(const JSONRPCRequest& request);

/**
 * eth_getCode - Get contract bytecode
 * 
 * Retrieves the deployed bytecode for a contract address.
 * 
 * @param request JSON-RPC request with contract address
 * @return Contract bytecode in hex
 */
UniValue eth_getCode(const JSONRPCRequest& request);

/**
 * eth_getStorageAt - Get contract storage value
 * 
 * Retrieves a value from contract storage at a specific key.
 * 
 * @param request JSON-RPC request with address, position, and block
 * @return Storage value in hex
 */
UniValue eth_getStorageAt(const JSONRPCRequest& request);

/**
 * eth_getTransactionReceipt - Get transaction receipt
 * 
 * Returns receipt for a transaction, including execution results,
 * gas used, and contract address (for deployments).
 * 
 * @param request JSON-RPC request with transaction hash
 * @return Transaction receipt object
 */
UniValue eth_getTransactionReceipt(const JSONRPCRequest& request);

/**
 * eth_blockNumber - Get current block number
 * 
 * Returns the current blockchain height.
 * 
 * @param request JSON-RPC request (no parameters)
 * @return Current block number
 */
UniValue eth_blockNumber(const JSONRPCRequest& request);

/**
 * eth_getBalance - Get address balance
 * 
 * Returns the CAS balance for an address.
 * 
 * @param request JSON-RPC request with address and block
 * @return Balance in wei (satoshis)
 */
UniValue eth_getBalance(const JSONRPCRequest& request);

/**
 * eth_getTransactionCount - Get transaction count (nonce)
 * 
 * Returns the number of transactions sent from an address.
 * 
 * @param request JSON-RPC request with address and block
 * @return Transaction count
 */
UniValue eth_getTransactionCount(const JSONRPCRequest& request);

/**
 * eth_gasPrice - Get current gas price
 * 
 * Returns the current gas price, adjusted for network conditions
 * and Cascoin's sustainable gas system.
 * 
 * @param request JSON-RPC request (no parameters)
 * @return Gas price in wei
 */
UniValue eth_gasPrice(const JSONRPCRequest& request);

#endif // CASCOIN_CVM_EVM_RPC_H
