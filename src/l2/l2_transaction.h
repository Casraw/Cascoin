// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_TRANSACTION_H
#define CASCOIN_L2_TRANSACTION_H

/**
 * @file l2_transaction.h
 * @brief L2 Transaction structure for Cascoin Layer 2
 * 
 * This file defines the L2Transaction structure that represents a transaction
 * on the L2 chain. It extends the concept of L1 transactions with L2-specific
 * fields including transaction type, encrypted payload support for MEV
 * protection, and L2 chain identification.
 * 
 * Requirements: 8.1, 16.1
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <amount.h>
#include <key.h>
#include <pubkey.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>

#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace l2 {

/**
 * @brief L2 Transaction Type enumeration
 * 
 * Note: L2TxType is defined in l2_common.h. This file uses that definition.
 * The types include:
 * - TRANSFER: Standard value transfer between accounts
 * - CONTRACT_DEPLOY: Deploy a new smart contract
 * - CONTRACT_CALL: Call an existing smart contract
 * - DEPOSIT: L1 -> L2 deposit (processed from bridge)
 * - WITHDRAWAL: L2 -> L1 withdrawal initiation
 * - CROSS_LAYER_MSG: Cross-layer message (L2 -> L1)
 * - SEQUENCER_ANNOUNCE: Sequencer announcement transaction
 * - FORCED_INCLUSION: Forced inclusion from L1 (censorship resistance)
 */

/**
 * @brief Access list entry for EIP-2930 style access lists
 */
struct AccessListEntry {
    /** Contract address being accessed */
    uint160 address;
    
    /** Storage keys being accessed */
    std::vector<uint256> storageKeys;

    AccessListEntry() = default;
    
    AccessListEntry(const uint160& addr, const std::vector<uint256>& keys)
        : address(addr), storageKeys(keys) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(storageKeys);
    }

    bool operator==(const AccessListEntry& other) const {
        return address == other.address && storageKeys == other.storageKeys;
    }
};

/**
 * @brief Encrypted transaction data for MEV protection
 * 
 * When MEV protection is enabled, the transaction payload is encrypted
 * using threshold encryption. It can only be decrypted when 2/3+ of
 * sequencers provide their decryption shares.
 * 
 * Requirement: 16.1
 */
struct EncryptedPayload {
    /** Encrypted transaction data */
    std::vector<unsigned char> ciphertext;
    
    /** Commitment hash for ordering (hash of plaintext) */
    uint256 commitmentHash;
    
    /** Encryption nonce/IV */
    std::vector<unsigned char> nonce;
    
    /** Encryption scheme version */
    uint8_t schemeVersion = 1;

    EncryptedPayload() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ciphertext);
        READWRITE(commitmentHash);
        READWRITE(nonce);
        READWRITE(schemeVersion);
    }

    bool IsEmpty() const {
        return ciphertext.empty();
    }

    bool operator==(const EncryptedPayload& other) const {
        return ciphertext == other.ciphertext &&
               commitmentHash == other.commitmentHash &&
               nonce == other.nonce &&
               schemeVersion == other.schemeVersion;
    }
};


/**
 * @brief L2 Transaction structure
 * 
 * Represents a transaction on the L2 chain. Extends L1 transaction concepts
 * with L2-specific fields including:
 * - Transaction type (transfer, contract call, withdrawal, etc.)
 * - L2 chain ID for replay protection
 * - Encrypted payload support for MEV protection
 * - Access lists for gas optimization
 * 
 * Requirements: 8.1, 16.1
 */
struct L2Transaction {
    // =========================================================================
    // Core Transaction Fields
    // =========================================================================
    
    /** Sender address */
    uint160 from;
    
    /** Recipient address (null for contract deployment) */
    uint160 to;
    
    /** Value to transfer in satoshis */
    CAmount value = 0;
    
    /** Transaction nonce (for replay protection) */
    uint64_t nonce = 0;
    
    /** Maximum gas units for this transaction */
    uint64_t gasLimit = 21000;
    
    /** Gas price in satoshis per gas unit */
    CAmount gasPrice = 0;
    
    /** Maximum fee per gas (EIP-1559 style) */
    CAmount maxFeePerGas = 0;
    
    /** Maximum priority fee per gas (EIP-1559 style) */
    CAmount maxPriorityFeePerGas = 0;
    
    /** Transaction data (contract call data or deployment bytecode) */
    std::vector<unsigned char> data;
    
    // =========================================================================
    // L2-Specific Fields
    // =========================================================================
    
    /** Transaction type */
    L2TxType type = L2TxType::TRANSFER;
    
    /** L2 chain ID for replay protection */
    uint64_t l2ChainId = DEFAULT_L2_CHAIN_ID;
    
    /** Access list for gas optimization */
    std::vector<AccessListEntry> accessList;
    
    // =========================================================================
    // MEV Protection (Requirement 16.1)
    // =========================================================================
    
    /** Whether this transaction is encrypted for MEV protection */
    bool isEncrypted = false;
    
    /** Encrypted payload (when isEncrypted is true) */
    EncryptedPayload encryptedPayload;
    
    // =========================================================================
    // Signature
    // =========================================================================
    
    /** ECDSA signature (r, s, v encoded) */
    std::vector<unsigned char> signature;
    
    /** Recovery ID for signature (0 or 1) */
    uint8_t recoveryId = 0;
    
    // =========================================================================
    // Execution Results (filled after execution)
    // =========================================================================
    
    /** Gas actually used (set after execution) */
    uint64_t gasUsed = 0;
    
    /** Whether execution succeeded */
    bool success = false;
    
    /** Return data from execution */
    std::vector<unsigned char> returnData;
    
    /** Error message if execution failed */
    std::string errorMessage;
    
    // =========================================================================
    // Cross-Layer Fields
    // =========================================================================
    
    /** L1 transaction hash (for deposits and forced inclusions) */
    uint256 l1TxHash;
    
    /** L1 block number (for deposits and forced inclusions) */
    uint64_t l1BlockNumber = 0;

    L2Transaction() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(to);
        READWRITE(value);
        READWRITE(nonce);
        READWRITE(gasLimit);
        READWRITE(gasPrice);
        READWRITE(maxFeePerGas);
        READWRITE(maxPriorityFeePerGas);
        READWRITE(data);
        
        uint8_t typeVal = static_cast<uint8_t>(type);
        READWRITE(typeVal);
        if (ser_action.ForRead()) {
            type = static_cast<L2TxType>(typeVal);
        }
        
        READWRITE(l2ChainId);
        READWRITE(accessList);
        READWRITE(isEncrypted);
        READWRITE(encryptedPayload);
        READWRITE(signature);
        READWRITE(recoveryId);
        READWRITE(gasUsed);
        READWRITE(success);
        READWRITE(returnData);
        READWRITE(errorMessage);
        READWRITE(l1TxHash);
        READWRITE(l1BlockNumber);
    }

    /**
     * @brief Compute the transaction hash
     * @return SHA256 hash of the transaction
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << from;
        ss << to;
        ss << value;
        ss << nonce;
        ss << gasLimit;
        ss << gasPrice;
        ss << maxFeePerGas;
        ss << maxPriorityFeePerGas;
        ss << data;
        ss << static_cast<uint8_t>(type);
        ss << l2ChainId;
        ss << accessList;
        ss << isEncrypted;
        if (isEncrypted) {
            ss << encryptedPayload;
        }
        return ss.GetHash();
    }

    /**
     * @brief Get the hash for signing (excludes signature)
     * @return Hash to be signed
     */
    uint256 GetSigningHash() const {
        return GetHash();
    }

    /**
     * @brief Sign the transaction with a private key
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key);

    /**
     * @brief Verify the transaction signature
     * @return true if signature is valid and matches sender
     */
    bool VerifySignature() const;

    /**
     * @brief Recover the sender address from signature
     * @return Recovered address, or null if recovery failed
     */
    std::optional<uint160> RecoverSender() const;

    /**
     * @brief Validate the basic structure of this transaction
     * 
     * Checks:
     * - Sender address is set
     * - Gas limit is reasonable
     * - Value is non-negative
     * - Data size is within limits
     * - Type-specific validation
     * 
     * @return true if structure is valid
     */
    bool ValidateStructure() const;

    /**
     * @brief Alias for ValidateStructure() for P2P message handling
     * @return true if basic validation passes
     */
    bool ValidateBasic() const {
        return ValidateStructure();
    }

    /**
     * @brief Check if this is a contract deployment
     * @return true if type is CONTRACT_DEPLOY
     */
    bool IsContractDeploy() const {
        return type == L2TxType::CONTRACT_DEPLOY;
    }

    /**
     * @brief Check if this is a contract call
     * @return true if type is CONTRACT_CALL
     */
    bool IsContractCall() const {
        return type == L2TxType::CONTRACT_CALL;
    }

    /**
     * @brief Check if this is a simple transfer
     * @return true if type is TRANSFER
     */
    bool IsTransfer() const {
        return type == L2TxType::TRANSFER;
    }

    /**
     * @brief Check if this is a withdrawal
     * @return true if type is WITHDRAWAL
     */
    bool IsWithdrawal() const {
        return type == L2TxType::WITHDRAWAL;
    }

    /**
     * @brief Check if this is a deposit
     * @return true if type is DEPOSIT
     */
    bool IsDeposit() const {
        return type == L2TxType::DEPOSIT;
    }

    /**
     * @brief Check if this is a forced inclusion
     * @return true if type is FORCED_INCLUSION
     */
    bool IsForcedInclusion() const {
        return type == L2TxType::FORCED_INCLUSION;
    }

    /**
     * @brief Calculate the maximum fee for this transaction
     * @return Maximum fee in satoshis
     */
    CAmount GetMaxFee() const {
        if (maxFeePerGas > 0) {
            return maxFeePerGas * gasLimit;
        }
        return gasPrice * gasLimit;
    }

    /**
     * @brief Calculate the effective gas price
     * @param baseFee Current base fee
     * @return Effective gas price in satoshis
     */
    CAmount GetEffectiveGasPrice(CAmount baseFee) const {
        if (maxFeePerGas > 0) {
            CAmount priorityFee = std::min(maxPriorityFeePerGas, maxFeePerGas - baseFee);
            return baseFee + priorityFee;
        }
        return gasPrice;
    }

    /**
     * @brief Serialize transaction to bytes
     * @return Serialized transaction data
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /**
     * @brief Deserialize transaction from bytes
     * @param data Serialized transaction data
     * @return true if deserialization succeeded
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) {
            return false;
        }
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /**
     * @brief Get a string representation for logging
     * @return String description of the transaction
     */
    std::string ToString() const;

    bool operator==(const L2Transaction& other) const {
        return from == other.from &&
               to == other.to &&
               value == other.value &&
               nonce == other.nonce &&
               gasLimit == other.gasLimit &&
               gasPrice == other.gasPrice &&
               maxFeePerGas == other.maxFeePerGas &&
               maxPriorityFeePerGas == other.maxPriorityFeePerGas &&
               data == other.data &&
               type == other.type &&
               l2ChainId == other.l2ChainId &&
               accessList == other.accessList &&
               isEncrypted == other.isEncrypted &&
               encryptedPayload == other.encryptedPayload &&
               signature == other.signature &&
               recoveryId == other.recoveryId;
    }

    bool operator!=(const L2Transaction& other) const {
        return !(*this == other);
    }
};

/** Maximum transaction data size (128 KB) */
static constexpr size_t MAX_TX_DATA_SIZE = 128 * 1024;

/** Maximum access list entries */
static constexpr size_t MAX_ACCESS_LIST_SIZE = 256;

/** Maximum storage keys per access list entry */
static constexpr size_t MAX_STORAGE_KEYS_PER_ENTRY = 256;

/** Minimum gas limit for any transaction */
static constexpr uint64_t MIN_TX_GAS_LIMIT = 21000;

/** Maximum gas limit for any transaction */
static constexpr uint64_t MAX_TX_GAS_LIMIT = 30000000;  // 30M gas

/**
 * @brief Create a simple transfer transaction
 * @param from Sender address
 * @param to Recipient address
 * @param value Amount to transfer
 * @param nonce Transaction nonce
 * @param gasPrice Gas price
 * @param chainId L2 chain ID
 * @return Transfer transaction
 */
L2Transaction CreateTransferTx(
    const uint160& from,
    const uint160& to,
    CAmount value,
    uint64_t nonce,
    CAmount gasPrice,
    uint64_t chainId);

/**
 * @brief Create a contract deployment transaction
 * @param from Deployer address
 * @param bytecode Contract bytecode
 * @param nonce Transaction nonce
 * @param gasLimit Gas limit
 * @param gasPrice Gas price
 * @param chainId L2 chain ID
 * @return Deployment transaction
 */
L2Transaction CreateDeployTx(
    const uint160& from,
    const std::vector<unsigned char>& bytecode,
    uint64_t nonce,
    uint64_t gasLimit,
    CAmount gasPrice,
    uint64_t chainId);

/**
 * @brief Create a contract call transaction
 * @param from Caller address
 * @param to Contract address
 * @param calldata Call data
 * @param value Value to send
 * @param nonce Transaction nonce
 * @param gasLimit Gas limit
 * @param gasPrice Gas price
 * @param chainId L2 chain ID
 * @return Call transaction
 */
L2Transaction CreateCallTx(
    const uint160& from,
    const uint160& to,
    const std::vector<unsigned char>& calldata,
    CAmount value,
    uint64_t nonce,
    uint64_t gasLimit,
    CAmount gasPrice,
    uint64_t chainId);

/**
 * @brief Create a withdrawal transaction
 * 
 * *** DEPRECATED - Task 12: Legacy Bridge Code ***
 * 
 * This function is DEPRECATED. The old withdrawal system has been replaced
 * by the burn-and-mint model. L2 tokens cannot be converted back to L1 CAS.
 * 
 * Transactions created by this function will be rejected by the validation system.
 * 
 * @param from L2 sender address
 * @param l1Recipient L1 recipient address
 * @param amount Amount to withdraw
 * @param nonce Transaction nonce
 * @param gasPrice Gas price
 * @param chainId L2 chain ID
 * @return Withdrawal transaction (will be rejected)
 */
[[deprecated("Withdrawals are no longer supported - use burn-and-mint model")]]
L2Transaction CreateWithdrawalTx(
    const uint160& from,
    const uint160& l1Recipient,
    CAmount amount,
    uint64_t nonce,
    CAmount gasPrice,
    uint64_t chainId);

/**
 * @brief Create a burn-and-mint transaction
 * 
 * NEW - Task 12: Burn-and-Mint Token Model
 * 
 * Creates a transaction to mint L2 tokens after a burn has been validated
 * and consensus has been reached. This is a system transaction that does
 * not require a sender signature.
 * 
 * @param l1BurnTxHash The L1 burn transaction hash (OP_RETURN)
 * @param recipient The L2 address to receive minted tokens
 * @param amount The amount to mint (must match burned amount)
 * @param chainId The L2 chain ID
 * @return L2Transaction for minting
 */
L2Transaction CreateBurnMintTx(
    const uint256& l1BurnTxHash,
    const uint160& recipient,
    CAmount amount,
    uint64_t chainId);

} // namespace l2

#endif // CASCOIN_L2_TRANSACTION_H
