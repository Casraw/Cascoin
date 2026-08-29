// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BURN_PARSER_H
#define CASCOIN_L2_BURN_PARSER_H

/**
 * @file burn_parser.h
 * @brief OP_RETURN Burn Transaction Parser for L2 Burn-and-Mint Token Model
 * 
 * This file implements the parsing and creation of OP_RETURN burn transactions
 * that are used to transfer CAS from L1 to L2. When CAS is burned on L1 via
 * OP_RETURN, corresponding L2 tokens are minted after sequencer consensus.
 * 
 * OP_RETURN Format:
 * OP_RETURN "L2BURN" <chain_id:4bytes> <recipient_pubkey:33bytes> <amount:8bytes>
 * 
 * Requirements: 1.2, 1.3, 1.4, 2.1
 */

#include <amount.h>
#include <pubkey.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>
#include <primitives/transaction.h>
#include <hash.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Burn marker string in OP_RETURN */
static constexpr const char* BURN_MARKER = "L2BURN";

/** Size of the burn marker */
static constexpr size_t BURN_MARKER_SIZE = 6;

/** Size of chain ID field (4 bytes, uint32_t) */
static constexpr size_t CHAIN_ID_SIZE = 4;

/** Size of compressed public key (33 bytes) */
static constexpr size_t PUBKEY_SIZE = 33;

/** Size of amount field (8 bytes, int64_t) */
static constexpr size_t AMOUNT_SIZE = 8;

/** Total size of burn data payload (6 + 4 + 33 + 8 = 51 bytes) */
static constexpr size_t BURN_DATA_SIZE = BURN_MARKER_SIZE + CHAIN_ID_SIZE + PUBKEY_SIZE + AMOUNT_SIZE;

/** Minimum burn amount (1 satoshi) */
static constexpr CAmount MIN_BURN_AMOUNT = 1;

/** Maximum burn amount (21 million CAS in satoshis) */
static constexpr CAmount MAX_BURN_AMOUNT = 21000000LL * COIN;

// ============================================================================
// BurnData Structure
// ============================================================================

/**
 * @brief Data extracted from an OP_RETURN burn transaction
 * 
 * This structure holds the parsed data from an L1 burn transaction's
 * OP_RETURN output. It contains all information needed to mint
 * corresponding L2 tokens.
 * 
 * Requirements: 1.2, 2.1
 */
struct BurnData {
    /** L2 Chain ID (4 bytes) - identifies which L2 chain receives the tokens */
    uint32_t chainId;
    
    /** Recipient public key (33 bytes compressed) - L2 address to receive tokens */
    CPubKey recipientPubKey;
    
    /** Amount burned in satoshis (8 bytes) */
    CAmount amount;
    
    /** Default constructor */
    BurnData() : chainId(0), amount(0) {}
    
    /** Constructor with all fields */
    BurnData(uint32_t chain, const CPubKey& pubkey, CAmount amt)
        : chainId(chain), recipientPubKey(pubkey), amount(amt) {}
    
    /**
     * @brief Check if the burn data is valid
     * @return true if all fields are valid
     * 
     * Validates:
     * - Chain ID is non-zero
     * - Recipient public key is valid
     * - Amount is within valid range
     */
    bool IsValid() const {
        // Chain ID must be non-zero
        if (chainId == 0) {
            return false;
        }
        
        // Recipient public key must be valid and compressed
        if (!recipientPubKey.IsValid() || !recipientPubKey.IsCompressed()) {
            return false;
        }
        
        // Amount must be positive and within limits
        if (amount < MIN_BURN_AMOUNT || amount > MAX_BURN_AMOUNT) {
            return false;
        }
        
        return true;
    }
    
    /**
     * @brief Parse burn data from a script
     * @param script The OP_RETURN script to parse
     * @return Parsed BurnData if valid, std::nullopt otherwise
     * 
     * Expected format:
     * OP_RETURN <push_data> where push_data contains:
     * "L2BURN" (6 bytes) + chain_id (4 bytes LE) + pubkey (33 bytes) + amount (8 bytes LE)
     */
    static std::optional<BurnData> Parse(const CScript& script);
    
    /**
     * @brief Get the recipient address (Hash160 of public key)
     * @return The recipient's address as uint160
     */
    uint160 GetRecipientAddress() const {
        if (!recipientPubKey.IsValid()) {
            return uint160();
        }
        return recipientPubKey.GetID();
    }
    
    /**
     * @brief Compute a unique hash for this burn data
     * @return SHA256 hash of the burn data
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << chainId;
        ss << recipientPubKey;
        ss << amount;
        return ss.GetHash();
    }
    
    /**
     * @brief Serialize the burn data to bytes
     * @return Serialized byte vector
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }
    
    /**
     * @brief Deserialize burn data from bytes
     * @param data The serialized data
     * @return true if deserialization succeeded
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    /** Serialization support */
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainId);
        READWRITE(recipientPubKey);
        READWRITE(amount);
    }
    
    /** Equality operator */
    bool operator==(const BurnData& other) const {
        return chainId == other.chainId &&
               recipientPubKey == other.recipientPubKey &&
               amount == other.amount;
    }
    
    bool operator!=(const BurnData& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// BurnTransactionParser Class
// ============================================================================

/**
 * @brief Parser for OP_RETURN burn transactions
 * 
 * This class provides static methods to parse, validate, and create
 * burn transactions for the L2 burn-and-mint token model.
 * 
 * Requirements: 1.2, 1.3, 1.4
 */
class BurnTransactionParser {
public:
    /**
     * @brief Parse a burn transaction from a full L1 transaction
     * @param tx The L1 transaction to parse
     * @return Parsed BurnData if valid burn transaction, std::nullopt otherwise
     * 
     * Scans all outputs for a valid OP_RETURN burn output.
     * 
     * Requirements: 1.2, 1.3
     */
    static std::optional<BurnData> ParseBurnTransaction(const CTransaction& tx);
    
    /**
     * @brief Validate that a script has the correct burn format
     * @param script The script to validate
     * @return true if the script is a valid burn script
     * 
     * Checks:
     * - Script starts with OP_RETURN
     * - Contains "L2BURN" marker
     * - Has correct payload size (51 bytes)
     * 
     * Requirements: 1.2, 2.1
     */
    static bool ValidateBurnFormat(const CScript& script);
    
    /**
     * @brief Calculate the burned amount from a transaction
     * @param tx The transaction to analyze
     * @return The amount burned (inputs - spendable outputs - fee)
     * 
     * Note: This requires access to the UTXO set to get input values.
     * For validation, use the amount encoded in the OP_RETURN.
     * 
     * Requirements: 1.4
     */
    static CAmount CalculateBurnedAmount(const CTransaction& tx);
    
    /**
     * @brief Create a burn script for an OP_RETURN output
     * @param chainId The L2 chain ID
     * @param recipient The recipient's public key (must be compressed)
     * @param amount The amount being burned
     * @return The OP_RETURN script for the burn
     * 
     * Creates a script in the format:
     * OP_RETURN <"L2BURN" + chain_id + pubkey + amount>
     * 
     * Requirements: 1.2
     */
    static CScript CreateBurnScript(uint32_t chainId, const CPubKey& recipient, CAmount amount);
    
    /**
     * @brief Check if a transaction contains a burn output
     * @param tx The transaction to check
     * @return true if the transaction has a valid burn output
     */
    static bool IsBurnTransaction(const CTransaction& tx);
    
    /**
     * @brief Get the index of the burn output in a transaction
     * @param tx The transaction to search
     * @return The output index if found, -1 otherwise
     */
    static int GetBurnOutputIndex(const CTransaction& tx);
    
    /**
     * @brief Extract the burn marker from a script
     * @param script The script to check
     * @return The marker string if found, empty string otherwise
     */
    static std::string ExtractBurnMarker(const CScript& script);
    
private:
    /**
     * @brief Extract the payload data from an OP_RETURN script
     * @param script The OP_RETURN script
     * @return The payload data if valid, empty vector otherwise
     */
    static std::vector<unsigned char> ExtractPayload(const CScript& script);
};

} // namespace l2

#endif // CASCOIN_L2_BURN_PARSER_H
