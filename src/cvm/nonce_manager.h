// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_NONCE_MANAGER_H
#define CASCOIN_CVM_NONCE_MANAGER_H

#include <uint256.h>
#include <primitives/transaction.h>
#include <map>
#include <memory>

class CBlock;

namespace CVM {

// Forward declaration
class CVMDatabase;

/**
 * Nonce Manager - Tracks transaction counts per address
 * 
 * Manages nonces for:
 * - Contract deployment address generation (CREATE, CREATE2)
 * - Transaction ordering and replay protection
 * - Ethereum-compatible nonce behavior
 */
class NonceManager {
public:
    NonceManager(CVMDatabase* db);
    ~NonceManager();
    
    /**
     * Get current nonce for address
     * Returns 0 if address has no transactions
     */
    uint64_t GetNonce(const uint160& address);
    
    /**
     * Get next nonce for address (current + 1)
     * Used for new transactions
     */
    uint64_t GetNextNonce(const uint160& address);
    
    /**
     * Increment nonce for address
     * Called after transaction is included in block
     */
    bool IncrementNonce(const uint160& address);
    
    /**
     * Set nonce for address
     * Used during block processing
     */
    bool SetNonce(const uint160& address, uint64_t nonce);
    
    /**
     * Decrement nonce for address
     * Called during block disconnection (reorg)
     */
    bool DecrementNonce(const uint160& address);
    
    /**
     * Generate contract address using CREATE
     * address = keccak256(rlp([sender, nonce]))[12:]
     */
    uint160 GenerateContractAddress(const uint160& sender, uint64_t nonce);
    
    /**
     * Generate contract address using CREATE2
     * address = keccak256(0xff ++ sender ++ salt ++ keccak256(init_code))[12:]
     */
    uint160 GenerateCreate2Address(const uint160& sender, const uint256& salt,
                                   const std::vector<uint8_t>& initCode);
    
    /**
     * Update nonces for block
     * Called from ConnectBlock()
     */
    bool UpdateNoncesForBlock(const CBlock& block, int height);
    
    /**
     * Revert nonces for block
     * Called from DisconnectBlock()
     */
    bool RevertNoncesForBlock(const CBlock& block, int height);
    
    /**
     * Flush to database
     */
    bool Flush();
    
private:
    CVMDatabase* m_db;
    
    // Cache for frequently accessed nonces
    std::map<uint160, uint64_t> m_nonceCache;
    
    // Helper to extract sender from transaction
    uint160 GetTransactionSender(const CTransaction& tx);
};

/**
 * Global nonce manager instance
 */
extern std::unique_ptr<NonceManager> g_nonceManager;

/**
 * Initialize nonce manager
 */
bool InitNonceManager(CVMDatabase* db);

/**
 * Shutdown nonce manager
 */
void ShutdownNonceManager();

} // namespace CVM

#endif // CASCOIN_CVM_NONCE_MANAGER_H
