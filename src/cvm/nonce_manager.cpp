// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/nonce_manager.h>
#include <cvm/cvmdb.h>
#include <cvm/softfork.h>
#include <hash.h>
#include <util.h>
#include <script/standard.h>
#include <coins.h>

namespace CVM {

std::unique_ptr<NonceManager> g_nonceManager;

NonceManager::NonceManager(CVMDatabase* db) : m_db(db) {}

NonceManager::~NonceManager() {
    Flush();
}

uint64_t NonceManager::GetNonce(const uint160& address) {
    // Check cache first
    auto it = m_nonceCache.find(address);
    if (it != m_nonceCache.end()) {
        return it->second;
    }
    
    // Read from database
    uint64_t nonce = 0;
    if (m_db && m_db->ReadNonce(address, nonce)) {
        m_nonceCache[address] = nonce;
        return nonce;
    }
    
    // No nonce found, return 0
    return 0;
}

uint64_t NonceManager::GetNextNonce(const uint160& address) {
    return GetNonce(address) + 1;
}

bool NonceManager::IncrementNonce(const uint160& address) {
    uint64_t currentNonce = GetNonce(address);
    uint64_t newNonce = currentNonce + 1;
    
    // Update cache
    m_nonceCache[address] = newNonce;
    
    // Write to database
    if (m_db) {
        return m_db->WriteNonce(address, newNonce);
    }
    
    return false;
}

bool NonceManager::SetNonce(const uint160& address, uint64_t nonce) {
    // Update cache
    m_nonceCache[address] = nonce;
    
    // Write to database
    if (m_db) {
        return m_db->WriteNonce(address, nonce);
    }
    
    return false;
}

bool NonceManager::DecrementNonce(const uint160& address) {
    uint64_t currentNonce = GetNonce(address);
    if (currentNonce == 0) {
        LogPrint(BCLog::CVM, "NonceManager: Cannot decrement nonce for %s (already 0)\n",
                 address.ToString());
        return false;
    }
    
    uint64_t newNonce = currentNonce - 1;
    
    // Update cache
    m_nonceCache[address] = newNonce;
    
    // Write to database
    if (m_db) {
        return m_db->WriteNonce(address, newNonce);
    }
    
    return false;
}

uint160 NonceManager::GenerateContractAddress(const uint160& sender, uint64_t nonce) {
    // Ethereum CREATE: address = keccak256(rlp([sender, nonce]))[12:]
    // Simplified implementation using Hash160
    
    std::vector<uint8_t> data;
    data.insert(data.end(), sender.begin(), sender.end());
    
    // Append nonce (big-endian)
    for (int i = 7; i >= 0; i--) {
        data.push_back((nonce >> (i * 8)) & 0xFF);
    }
    
    // Hash and take last 20 bytes
    uint160 contractAddr = Hash160(data.begin(), data.end());
    
    LogPrint(BCLog::CVM, "NonceManager: Generated contract address %s from sender %s nonce %d\n",
             contractAddr.ToString(), sender.ToString(), nonce);
    
    return contractAddr;
}

uint160 NonceManager::GenerateCreate2Address(const uint160& sender, const uint256& salt,
                                             const std::vector<uint8_t>& initCode) {
    // Ethereum CREATE2: address = keccak256(0xff ++ sender ++ salt ++ keccak256(init_code))[12:]
    
    std::vector<uint8_t> data;
    data.push_back(0xff);
    data.insert(data.end(), sender.begin(), sender.end());
    data.insert(data.end(), salt.begin(), salt.end());
    
    // Hash init code
    uint256 initCodeHash = Hash(initCode.begin(), initCode.end());
    data.insert(data.end(), initCodeHash.begin(), initCodeHash.end());
    
    // Hash and take last 20 bytes
    uint160 contractAddr = Hash160(data.begin(), data.end());
    
    LogPrint(BCLog::CVM, "NonceManager: Generated CREATE2 address %s from sender %s\n",
             contractAddr.ToString(), sender.ToString());
    
    return contractAddr;
}

uint160 NonceManager::GetTransactionSender(const CTransaction& tx) {
    // Extract sender from first input
    if (tx.vin.empty()) {
        return uint160();
    }
    
    // For CVM/EVM transactions, sender is extracted from scriptSig or witness
    // This is a simplified implementation
    // In production, you'd need to verify signatures and extract pubkey
    
    // For now, return empty - this will be properly implemented
    // when integrating with transaction validation
    return uint160();
}

bool NonceManager::UpdateNoncesForBlock(const CBlock& block, int height) {
    if (!m_db) {
        return false;
    }
    
    // Process all transactions in block
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check if this is a CVM/EVM transaction
        if (!IsEVMTransaction(*tx) && FindCVMOpReturn(*tx) < 0) {
            continue;
        }
        
        // Get sender address
        uint160 sender = GetTransactionSender(*tx);
        if (sender.IsNull()) {
            // Cannot determine sender, skip
            continue;
        }
        
        // Increment nonce for sender
        IncrementNonce(sender);
        
        LogPrint(BCLog::CVM, "NonceManager: Incremented nonce for %s in block %d\n",
                 sender.ToString(), height);
    }
    
    return true;
}

bool NonceManager::RevertNoncesForBlock(const CBlock& block, int height) {
    if (!m_db) {
        return false;
    }
    
    // Process all transactions in block (in reverse)
    for (auto it = block.vtx.rbegin(); it != block.vtx.rend(); ++it) {
        const CTransaction& tx = **it;
        
        // Skip coinbase
        if (tx.IsCoinBase()) {
            continue;
        }
        
        // Check if this is a CVM/EVM transaction
        if (!IsEVMTransaction(tx) && FindCVMOpReturn(tx) < 0) {
            continue;
        }
        
        // Get sender address
        uint160 sender = GetTransactionSender(tx);
        if (sender.IsNull()) {
            continue;
        }
        
        // Decrement nonce for sender
        DecrementNonce(sender);
        
        LogPrint(BCLog::CVM, "NonceManager: Decremented nonce for %s in block %d\n",
                 sender.ToString(), height);
    }
    
    return true;
}

bool NonceManager::Flush() {
    if (m_db) {
        return m_db->Flush();
    }
    return true;
}

// Global functions
bool InitNonceManager(CVMDatabase* db) {
    if (!db) {
        LogPrintf("NonceManager: Cannot initialize without database\n");
        return false;
    }
    
    g_nonceManager = std::make_unique<NonceManager>(db);
    LogPrintf("NonceManager: Initialized\n");
    return true;
}

void ShutdownNonceManager() {
    if (g_nonceManager) {
        g_nonceManager->Flush();
        g_nonceManager.reset();
        LogPrintf("NonceManager: Shutdown complete\n");
    }
}

} // namespace CVM
