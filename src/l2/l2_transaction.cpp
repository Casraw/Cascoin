// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_transaction.cpp
 * @brief Implementation of L2 Transaction structure
 * 
 * Requirements: 8.1, 16.1
 */

#include <l2/l2_transaction.h>
#include <tinyformat.h>

namespace l2 {

bool L2Transaction::Sign(const CKey& key) {
    uint256 hash = GetSigningHash();
    
    if (!key.Sign(hash, signature)) {
        return false;
    }
    
    // Determine recovery ID
    CPubKey pubkey = key.GetPubKey();
    if (!pubkey.IsValid()) {
        return false;
    }
    
    // Try both recovery IDs to find the correct one
    for (uint8_t rid = 0; rid < 2; rid++) {
        CPubKey recovered;
        if (recovered.RecoverCompact(hash, signature) && recovered == pubkey) {
            recoveryId = rid;
            return true;
        }
    }
    
    // If we can't determine recovery ID, signature is still valid
    recoveryId = 0;
    return true;
}

bool L2Transaction::VerifySignature() const {
    if (signature.empty()) {
        return false;
    }
    
    // For deposit transactions, signature may not be required
    if (type == L2TxType::DEPOSIT) {
        return true;
    }
    
    auto recovered = RecoverSender();
    if (!recovered) {
        return false;
    }
    
    return *recovered == from;
}

std::optional<uint160> L2Transaction::RecoverSender() const {
    if (signature.empty()) {
        return std::nullopt;
    }
    
    uint256 hash = GetSigningHash();
    CPubKey pubkey;
    
    if (!pubkey.RecoverCompact(hash, signature)) {
        return std::nullopt;
    }
    
    // Convert public key to address (hash160)
    uint160 address;
    CHash160().Write(pubkey.begin(), pubkey.size()).Finalize(address.begin());
    
    return address;
}

bool L2Transaction::ValidateStructure() const {
    // Sender must be set (except for deposit transactions)
    if (type != L2TxType::DEPOSIT && from.IsNull()) {
        return false;
    }
    
    // Gas limit must be within bounds
    if (gasLimit < MIN_TX_GAS_LIMIT || gasLimit > MAX_TX_GAS_LIMIT) {
        return false;
    }
    
    // Value must be non-negative
    if (value < 0) {
        return false;
    }
    
    // Data size must be within limits
    if (data.size() > MAX_TX_DATA_SIZE) {
        return false;
    }
    
    // Access list size limits
    if (accessList.size() > MAX_ACCESS_LIST_SIZE) {
        return false;
    }
    
    for (const auto& entry : accessList) {
        if (entry.storageKeys.size() > MAX_STORAGE_KEYS_PER_ENTRY) {
            return false;
        }
    }
    
    // Type-specific validation
    switch (type) {
        case L2TxType::TRANSFER:
            // Transfer must have recipient
            if (to.IsNull()) {
                return false;
            }
            break;
            
        case L2TxType::CONTRACT_DEPLOY:
            // Deployment must have bytecode
            if (data.empty()) {
                return false;
            }
            // Deployment should not have recipient
            if (!to.IsNull()) {
                return false;
            }
            break;
            
        case L2TxType::CONTRACT_CALL:
            // Call must have recipient (contract address)
            if (to.IsNull()) {
                return false;
            }
            break;
            
        case L2TxType::WITHDRAWAL:
            // Withdrawal must have recipient (L1 address)
            if (to.IsNull()) {
                return false;
            }
            // Withdrawal must have positive value
            if (value <= 0) {
                return false;
            }
            break;
            
        case L2TxType::DEPOSIT:
            // Deposit must have recipient
            if (to.IsNull()) {
                return false;
            }
            // Deposit must have positive value
            if (value <= 0) {
                return false;
            }
            // Deposit must have L1 reference
            if (l1TxHash.IsNull()) {
                return false;
            }
            break;
            
        case L2TxType::FORCED_INCLUSION:
            // Forced inclusion must have L1 reference
            if (l1TxHash.IsNull()) {
                return false;
            }
            break;
            
        case L2TxType::CROSS_LAYER_MSG:
            // Cross-layer message must have recipient
            if (to.IsNull()) {
                return false;
            }
            break;
            
        case L2TxType::SEQUENCER_ANNOUNCE:
            // No additional validation
            break;
    }
    
    // Gas price validation (at least one must be set for non-deposit tx)
    if (type != L2TxType::DEPOSIT) {
        if (gasPrice == 0 && maxFeePerGas == 0) {
            return false;
        }
    }
    
    // Encrypted payload validation
    if (isEncrypted && encryptedPayload.IsEmpty()) {
        return false;
    }
    
    return true;
}

std::string L2Transaction::ToString() const {
    return strprintf(
        "L2Tx(hash=%s, type=%s, from=%s, to=%s, value=%d, nonce=%u, "
        "gasLimit=%u, gasPrice=%d, encrypted=%s)",
        GetHash().ToString().substr(0, 16),
        L2TxTypeToString(type),
        from.ToString().substr(0, 16),
        to.IsNull() ? "null" : to.ToString().substr(0, 16),
        value,
        nonce,
        gasLimit,
        gasPrice,
        isEncrypted ? "true" : "false"
    );
}

L2Transaction CreateTransferTx(
    const uint160& from,
    const uint160& to,
    CAmount value,
    uint64_t nonce,
    CAmount gasPrice,
    uint64_t chainId)
{
    L2Transaction tx;
    tx.type = L2TxType::TRANSFER;
    tx.from = from;
    tx.to = to;
    tx.value = value;
    tx.nonce = nonce;
    tx.gasLimit = MIN_TX_GAS_LIMIT;  // 21000 for simple transfer
    tx.gasPrice = gasPrice;
    tx.l2ChainId = chainId;
    return tx;
}

L2Transaction CreateDeployTx(
    const uint160& from,
    const std::vector<unsigned char>& bytecode,
    uint64_t nonce,
    uint64_t gasLimit,
    CAmount gasPrice,
    uint64_t chainId)
{
    L2Transaction tx;
    tx.type = L2TxType::CONTRACT_DEPLOY;
    tx.from = from;
    tx.to.SetNull();  // No recipient for deployment
    tx.value = 0;
    tx.nonce = nonce;
    tx.gasLimit = gasLimit;
    tx.gasPrice = gasPrice;
    tx.data = bytecode;
    tx.l2ChainId = chainId;
    return tx;
}

L2Transaction CreateCallTx(
    const uint160& from,
    const uint160& to,
    const std::vector<unsigned char>& calldata,
    CAmount value,
    uint64_t nonce,
    uint64_t gasLimit,
    CAmount gasPrice,
    uint64_t chainId)
{
    L2Transaction tx;
    tx.type = L2TxType::CONTRACT_CALL;
    tx.from = from;
    tx.to = to;
    tx.value = value;
    tx.nonce = nonce;
    tx.gasLimit = gasLimit;
    tx.gasPrice = gasPrice;
    tx.data = calldata;
    tx.l2ChainId = chainId;
    return tx;
}

L2Transaction CreateWithdrawalTx(
    const uint160& from,
    const uint160& l1Recipient,
    CAmount amount,
    uint64_t nonce,
    CAmount gasPrice,
    uint64_t chainId)
{
    L2Transaction tx;
    tx.type = L2TxType::WITHDRAWAL;
    tx.from = from;
    tx.to = l1Recipient;
    tx.value = amount;
    tx.nonce = nonce;
    tx.gasLimit = 100000;  // Withdrawal gas cost
    tx.gasPrice = gasPrice;
    tx.l2ChainId = chainId;
    return tx;
}

} // namespace l2
