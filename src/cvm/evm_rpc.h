// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_EVM_RPC_H
#define CASCOIN_CVM_EVM_RPC_H

#include <univalue.h>
#include <rpc/server.h>

/**
 * Cascoin CVM/EVM RPC methods with Ethereum compatibility
 * 
 * Primary methods use cas_* naming (Cascoin-native)
 * Ethereum-compatible aliases use eth_* naming for tool compatibility
 * 
 * All methods integrate with Cascoin's trust-aware features and reputation system.
 */

// ===== Primary Cascoin Methods (cas_*) =====

/**
 * cas_sendTransaction - Send a transaction to deploy or call a contract
 * 
 * Creates and broadcasts a CVM/EVM transaction.
 * Integrates with Cascoin's reputation system for gas discounts.
 * 
 * @param request JSON-RPC request with transaction parameters
 * @return Transaction hash
 */
UniValue cas_sendTransaction(const JSONRPCRequest& request);

/**
 * cas_call - Execute a contract call without creating a transaction
 * 
 * Read-only execution that doesn't modify state. Useful for querying
 * contract data without paying gas fees.
 * 
 * @param request JSON-RPC request with call parameters
 * @return Call result data
 */
UniValue cas_call(const JSONRPCRequest& request);

/**
 * cas_estimateGas - Estimate gas required for a transaction
 * 
 * Estimates gas with Cascoin's reputation-based adjustments.
 * Takes into account free gas eligibility and gas discounts.
 * 
 * @param request JSON-RPC request with transaction parameters
 * @return Estimated gas amount
 */
UniValue cas_estimateGas(const JSONRPCRequest& request);

/**
 * cas_getCode - Get contract bytecode
 * 
 * Retrieves the deployed bytecode for a contract address.
 * 
 * @param request JSON-RPC request with contract address
 * @return Contract bytecode in hex
 */
UniValue cas_getCode(const JSONRPCRequest& request);

/**
 * cas_getStorageAt - Get contract storage value
 * 
 * Retrieves a value from contract storage at a specific key.
 * 
 * @param request JSON-RPC request with address, position, and block
 * @return Storage value in hex
 */
UniValue cas_getStorageAt(const JSONRPCRequest& request);

/**
 * cas_getTransactionReceipt - Get transaction receipt
 * 
 * Returns receipt for a transaction, including execution results,
 * gas used, and contract address (for deployments).
 * 
 * @param request JSON-RPC request with transaction hash
 * @return Transaction receipt object
 */
UniValue cas_getTransactionReceipt(const JSONRPCRequest& request);

/**
 * cas_blockNumber - Get current block number
 * 
 * Returns the current blockchain height.
 * 
 * @param request JSON-RPC request (no parameters)
 * @return Current block number
 */
UniValue cas_blockNumber(const JSONRPCRequest& request);

/**
 * cas_getBalance - Get address balance
 * 
 * Returns the CAS balance for an address.
 * 
 * @param request JSON-RPC request with address and block
 * @return Balance in wei (satoshis)
 */
UniValue cas_getBalance(const JSONRPCRequest& request);

/**
 * cas_getTransactionCount - Get transaction count (nonce)
 * 
 * Returns the number of transactions sent from an address.
 * 
 * @param request JSON-RPC request with address and block
 * @return Transaction count
 */
UniValue cas_getTransactionCount(const JSONRPCRequest& request);

/**
 * cas_gasPrice - Get current gas price
 * 
 * Returns the current gas price, adjusted for network conditions
 * and Cascoin's sustainable gas system.
 * 
 * @param request JSON-RPC request (no parameters)
 * @return Gas price in wei
 */
UniValue cas_gasPrice(const JSONRPCRequest& request);

// ===== Ethereum-Compatible Aliases (eth_*) =====

/**
 * eth_sendTransaction - Ethereum-compatible alias for cas_sendTransaction
 */
UniValue eth_sendTransaction(const JSONRPCRequest& request);

/**
 * eth_call - Ethereum-compatible alias for cas_call
 */
UniValue eth_call(const JSONRPCRequest& request);

/**
 * eth_estimateGas - Ethereum-compatible alias for cas_estimateGas
 */
UniValue eth_estimateGas(const JSONRPCRequest& request);

/**
 * eth_getCode - Ethereum-compatible alias for cas_getCode
 */
UniValue eth_getCode(const JSONRPCRequest& request);

/**
 * eth_getStorageAt - Ethereum-compatible alias for cas_getStorageAt
 */
UniValue eth_getStorageAt(const JSONRPCRequest& request);

/**
 * eth_getTransactionReceipt - Ethereum-compatible alias for cas_getTransactionReceipt
 */
UniValue eth_getTransactionReceipt(const JSONRPCRequest& request);

/**
 * eth_blockNumber - Ethereum-compatible alias for cas_blockNumber
 */
UniValue eth_blockNumber(const JSONRPCRequest& request);

/**
 * eth_getBalance - Ethereum-compatible alias for cas_getBalance
 */
UniValue eth_getBalance(const JSONRPCRequest& request);

/**
 * eth_getTransactionCount - Ethereum-compatible alias for cas_getTransactionCount
 */
UniValue eth_getTransactionCount(const JSONRPCRequest& request);

/**
 * eth_gasPrice - Ethereum-compatible alias for cas_gasPrice
 */
UniValue eth_gasPrice(const JSONRPCRequest& request);

// ===== Developer Tooling Methods =====

/**
 * debug_traceTransaction - Trace transaction execution
 * 
 * Provides detailed execution trace for a transaction, including
 * opcodes executed, gas costs, and state changes.
 * 
 * @param request JSON-RPC request with transaction hash and options
 * @return Execution trace
 */
UniValue debug_traceTransaction(const JSONRPCRequest& request);

/**
 * debug_traceCall - Trace simulated call execution
 * 
 * Simulates a contract call and returns detailed execution trace
 * without creating a transaction.
 * 
 * @param request JSON-RPC request with call parameters and options
 * @return Execution trace
 */
UniValue debug_traceCall(const JSONRPCRequest& request);

/**
 * cas_snapshot - Create state snapshot
 * 
 * Creates a snapshot of the current blockchain state for testing.
 * Can be reverted to later with cas_revert.
 * 
 * @param request JSON-RPC request (no parameters)
 * @return Snapshot ID
 */
UniValue cas_snapshot(const JSONRPCRequest& request);

/**
 * evm_snapshot - Ethereum-compatible alias for cas_snapshot
 */
UniValue evm_snapshot(const JSONRPCRequest& request);

/**
 * cas_revert - Revert to snapshot
 * 
 * Reverts blockchain state to a previously created snapshot.
 * 
 * @param request JSON-RPC request with snapshot ID
 * @return Success boolean
 */
UniValue cas_revert(const JSONRPCRequest& request);

/**
 * evm_revert - Ethereum-compatible alias for cas_revert
 */
UniValue evm_revert(const JSONRPCRequest& request);

/**
 * cas_mine - Mine blocks manually
 * 
 * Mines one or more blocks immediately (regtest only).
 * Useful for testing time-dependent contracts.
 * 
 * @param request JSON-RPC request with number of blocks
 * @return Array of mined block hashes
 */
UniValue cas_mine(const JSONRPCRequest& request);

/**
 * evm_mine - Ethereum-compatible alias for cas_mine
 */
UniValue evm_mine(const JSONRPCRequest& request);

/**
 * cas_setNextBlockTimestamp - Set next block timestamp
 * 
 * Sets the timestamp for the next block to be mined (regtest only).
 * 
 * @param request JSON-RPC request with timestamp
 * @return Success boolean
 */
UniValue cas_setNextBlockTimestamp(const JSONRPCRequest& request);

/**
 * evm_setNextBlockTimestamp - Ethereum-compatible alias
 */
UniValue evm_setNextBlockTimestamp(const JSONRPCRequest& request);

/**
 * cas_increaseTime - Increase blockchain time
 * 
 * Advances blockchain time by specified seconds (regtest only).
 * 
 * @param request JSON-RPC request with seconds to advance
 * @return New timestamp
 */
UniValue cas_increaseTime(const JSONRPCRequest& request);

/**
 * evm_increaseTime - Ethereum-compatible alias
 */
UniValue evm_increaseTime(const JSONRPCRequest& request);

#endif // CASCOIN_CVM_EVM_RPC_H
