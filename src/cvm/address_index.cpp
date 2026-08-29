// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/address_index.h>
#include <script/standard.h>
#include <base58.h>
#include <chain.h>
#include <util.h>

extern CChain chainActive;

namespace CVM {

std::unique_ptr<AddressIndex> g_addressIndex;

AddressIndex::AddressIndex(CDBWrapper* db) : m_db(db) {}

AddressIndex::~AddressIndex() {
    Flush();
}

std::string AddressIndex::MakeUTXOKey(const uint160& address, const COutPoint& outpoint) {
    std::string key;
    key.reserve(1 + 20 + 32 + 4);
    key += DB_ADDRESS_UTXO;
    key.append((char*)address.begin(), 20);
    key.append((char*)outpoint.hash.begin(), 32);
    key.append((char*)&outpoint.n, 4);
    return key;
}

std::string AddressIndex::MakeBalanceKey(const uint160& address) {
    std::string key;
    key.reserve(1 + 20);
    key += DB_ADDRESS_BALANCE;
    key.append((char*)address.begin(), 20);
    return key;
}

bool AddressIndex::AddUTXO(const uint160& address, const COutPoint& outpoint,
                          CAmount value, int height) {
    if (!m_db) return false;
    
    AddressUTXO utxo(outpoint, value, height);
    std::string key = MakeUTXOKey(address, outpoint);
    
    if (!m_db->Write(key, utxo)) {
        return false;
    }
    
    // Invalidate cache
    InvalidateCache(address);
    
    LogPrint(BCLog::CVM, "AddressIndex: Added UTXO for %s: %s:%d = %d\n",
             address.ToString(), outpoint.hash.ToString(), outpoint.n, value);
    
    return true;
}

bool AddressIndex::RemoveUTXO(const uint160& address, const COutPoint& outpoint) {
    if (!m_db) return false;
    
    std::string key = MakeUTXOKey(address, outpoint);
    
    if (!m_db->Erase(key)) {
        return false;
    }
    
    // Invalidate cache
    InvalidateCache(address);
    
    LogPrint(BCLog::CVM, "AddressIndex: Removed UTXO for %s: %s:%d\n",
             address.ToString(), outpoint.hash.ToString(), outpoint.n);
    
    return true;
}

std::vector<AddressUTXO> AddressIndex::GetAddressUTXOs(const uint160& address) {
    std::vector<AddressUTXO> utxos;
    
    if (!m_db) return utxos;
    
    // Iterate through all UTXOs for this address
    std::string prefix;
    prefix += DB_ADDRESS_UTXO;
    prefix.append((char*)address.begin(), 20);
    
    std::unique_ptr<CDBIterator> it(m_db->NewIterator());
    it->Seek(prefix);
    
    while (it->Valid()) {
        std::pair<char, std::string> key;
        if (!it->GetKey(key) || key.first != DB_ADDRESS_UTXO) {
            break;
        }
        
        // Check if still same address
        if (key.second.size() < 20 || 
            memcmp(key.second.data(), address.begin(), 20) != 0) {
            break;
        }
        
        AddressUTXO utxo;
        if (it->GetValue(utxo)) {
            utxos.push_back(utxo);
        }
        
        it->Next();
    }
    
    return utxos;
}

CAmount AddressIndex::GetAddressBalance(const uint160& address) {
    // Check cache first
    auto it = m_balanceCache.find(address);
    if (it != m_balanceCache.end()) {
        return it->second.balance;
    }
    
    // Check database
    AddressBalance balanceInfo;
    if (GetBalanceInfo(address, balanceInfo)) {
        m_balanceCache[address] = balanceInfo;
        return balanceInfo.balance;
    }
    
    // Calculate from UTXOs
    CAmount balance = 0;
    std::vector<AddressUTXO> utxos = GetAddressUTXOs(address);
    
    for (const auto& utxo : utxos) {
        balance += utxo.value;
    }
    
    // Cache the result
    balanceInfo.address = address;
    balanceInfo.balance = balance;
    balanceInfo.utxoCount = utxos.size();
    balanceInfo.lastUpdateHeight = chainActive.Height();
    
    m_balanceCache[address] = balanceInfo;
    
    // Store in database
    std::string key = MakeBalanceKey(address);
    m_db->Write(key, balanceInfo);
    
    return balance;
}

bool AddressIndex::GetBalanceInfo(const uint160& address, AddressBalance& balance) {
    if (!m_db) return false;
    
    std::string key = MakeBalanceKey(address);
    return m_db->Read(key, balance);
}

void AddressIndex::UpdateBalanceCache(const uint160& address, CAmount balance,
                                     uint32_t utxoCount, int height) {
    AddressBalance balanceInfo;
    balanceInfo.address = address;
    balanceInfo.balance = balance;
    balanceInfo.utxoCount = utxoCount;
    balanceInfo.lastUpdateHeight = height;
    
    m_balanceCache[address] = balanceInfo;
    
    if (m_db) {
        std::string key = MakeBalanceKey(address);
        m_db->Write(key, balanceInfo);
    }
}

void AddressIndex::InvalidateCache(const uint160& address) {
    m_balanceCache.erase(address);
}

bool AddressIndex::Flush() {
    if (m_db) {
        return m_db->Flush();
    }
    return true;
}

// Batch operations
AddressIndex::Batch::Batch(AddressIndex& index)
    : m_index(index), m_batch(*index.m_db) {}

AddressIndex::Batch::~Batch() {}

void AddressIndex::Batch::AddUTXO(const uint160& address, const COutPoint& outpoint,
                                 CAmount value, int height) {
    AddressUTXO utxo(outpoint, value, height);
    std::string key = m_index.MakeUTXOKey(address, outpoint);
    m_batch.Write(key, utxo);
    
    // Track balance changes
    m_balanceChanges[address] += value;
    m_utxoCountChanges[address]++;
}

void AddressIndex::Batch::RemoveUTXO(const uint160& address, const COutPoint& outpoint) {
    std::string key = m_index.MakeUTXOKey(address, outpoint);
    
    // Get UTXO value before removing
    AddressUTXO utxo;
    if (m_index.m_db->Read(key, utxo)) {
        m_balanceChanges[address] -= utxo.value;
        m_utxoCountChanges[address]--;
    }
    
    m_batch.Erase(key);
}

bool AddressIndex::Batch::Commit() {
    // Write batch
    if (!m_index.m_db->WriteBatch(m_batch)) {
        return false;
    }
    
    // Update balance caches
    int currentHeight = chainActive.Height();
    for (const auto& change : m_balanceChanges) {
        const uint160& address = change.first;
        CAmount balanceChange = change.second;
        
        // Get current balance
        CAmount currentBalance = m_index.GetAddressBalance(address);
        CAmount newBalance = currentBalance + balanceChange;
        
        // Get UTXO count change
        int utxoCountChange = m_utxoCountChanges[address];
        AddressBalance balanceInfo;
        m_index.GetBalanceInfo(address, balanceInfo);
        uint32_t newUtxoCount = balanceInfo.utxoCount + utxoCountChange;
        
        // Update cache
        m_index.UpdateBalanceCache(address, newBalance, newUtxoCount, currentHeight);
    }
    
    return true;
}

// Global functions
bool InitAddressIndex(CDBWrapper* db) {
    if (!db) {
        LogPrintf("AddressIndex: Cannot initialize without database\n");
        return false;
    }
    
    g_addressIndex = std::make_unique<AddressIndex>(db);
    LogPrintf("AddressIndex: Initialized\n");
    return true;
}

void ShutdownAddressIndex() {
    if (g_addressIndex) {
        g_addressIndex->Flush();
        g_addressIndex.reset();
        LogPrintf("AddressIndex: Shutdown complete\n");
    }
}

bool UpdateAddressIndexForBlock(const CBlock& block, int height,
                               const CCoinsViewCache& view) {
    if (!g_addressIndex) {
        return true; // Not an error if index is disabled
    }
    
    AddressIndex::Batch batch(*g_addressIndex);
    
    // Process all transactions in block
    for (const auto& tx : block.vtx) {
        // Skip coinbase inputs
        if (!tx->IsCoinBase()) {
            // Remove spent UTXOs
            for (const auto& txin : tx->vin) {
                // Get the previous output
                Coin coin;
                if (view.GetCoin(txin.prevout, coin)) {
                    // Extract address from scriptPubKey
                    CTxDestination dest;
                    if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                        const CKeyID* keyID = boost::get<CKeyID>(&dest);
                        if (keyID) {
                            uint160 address(*keyID);
                            batch.RemoveUTXO(address, txin.prevout);
                        }
                    }
                }
            }
        }
        
        // Add new UTXOs
        for (size_t i = 0; i < tx->vout.size(); i++) {
            const CTxOut& txout = tx->vout[i];
            
            // Extract address
            CTxDestination dest;
            if (ExtractDestination(txout.scriptPubKey, dest)) {
                const CKeyID* keyID = boost::get<CKeyID>(&dest);
                if (keyID) {
                    uint160 address(*keyID);
                    COutPoint outpoint(tx->GetHash(), i);
                    batch.AddUTXO(address, outpoint, txout.nValue, height);
                }
            }
        }
    }
    
    return batch.Commit();
}

bool RevertAddressIndexForBlock(const CBlock& block, int height,
                               const CCoinsViewCache& view) {
    if (!g_addressIndex) {
        return true; // Not an error if index is disabled
    }
    
    AddressIndex::Batch batch(*g_addressIndex);
    
    // Reverse the operations from UpdateAddressIndexForBlock
    for (const auto& tx : block.vtx) {
        // Remove UTXOs that were added
        for (size_t i = 0; i < tx->vout.size(); i++) {
            const CTxOut& txout = tx->vout[i];
            
            CTxDestination dest;
            if (ExtractDestination(txout.scriptPubKey, dest)) {
                const CKeyID* keyID = boost::get<CKeyID>(&dest);
                if (keyID) {
                    uint160 address(*keyID);
                    COutPoint outpoint(tx->GetHash(), i);
                    batch.RemoveUTXO(address, outpoint);
                }
            }
        }
        
        // Re-add UTXOs that were spent
        if (!tx->IsCoinBase()) {
            for (const auto& txin : tx->vin) {
                Coin coin;
                if (view.GetCoin(txin.prevout, coin)) {
                    CTxDestination dest;
                    if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                        const CKeyID* keyID = boost::get<CKeyID>(&dest);
                        if (keyID) {
                            uint160 address(*keyID);
                            batch.AddUTXO(address, txin.prevout, coin.out.nValue, height - 1);
                        }
                    }
                }
            }
        }
    }
    
    return batch.Commit();
}

} // namespace CVM
