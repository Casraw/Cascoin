// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file burn_parser.cpp
 * @brief Implementation of OP_RETURN Burn Transaction Parser
 * 
 * Requirements: 1.2, 1.3, 1.4, 2.1
 */

#include <l2/burn_parser.h>
#include <crypto/common.h>
#include <util.h>

#include <cstring>

namespace l2 {

// ============================================================================
// BurnData Implementation
// ============================================================================

std::optional<BurnData> BurnData::Parse(const CScript& script)
{
    // Check if script starts with OP_RETURN
    if (script.empty() || script[0] != OP_RETURN) {
        return std::nullopt;
    }
    
    // Extract the payload from the OP_RETURN script
    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    
    // Skip OP_RETURN
    if (!script.GetOp(pc, opcode)) {
        return std::nullopt;
    }
    
    if (opcode != OP_RETURN) {
        return std::nullopt;
    }
    
    // Get the pushed data
    if (!script.GetOp(pc, opcode, data)) {
        return std::nullopt;
    }
    
    // Verify payload size
    if (data.size() != BURN_DATA_SIZE) {
        return std::nullopt;
    }
    
    // Verify burn marker
    if (memcmp(data.data(), BURN_MARKER, BURN_MARKER_SIZE) != 0) {
        return std::nullopt;
    }
    
    // Parse chain ID (4 bytes, little-endian)
    size_t offset = BURN_MARKER_SIZE;
    uint32_t chainId = ReadLE32(&data[offset]);
    offset += CHAIN_ID_SIZE;
    
    // Parse recipient public key (33 bytes)
    CPubKey recipientPubKey;
    recipientPubKey.Set(data.begin() + offset, data.begin() + offset + PUBKEY_SIZE);
    offset += PUBKEY_SIZE;
    
    // Parse amount (8 bytes, little-endian)
    CAmount amount = static_cast<CAmount>(ReadLE64(&data[offset]));
    
    // Create and validate BurnData
    BurnData burnData(chainId, recipientPubKey, amount);
    
    if (!burnData.IsValid()) {
        return std::nullopt;
    }
    
    return burnData;
}

// ============================================================================
// BurnTransactionParser Implementation
// ============================================================================

std::optional<BurnData> BurnTransactionParser::ParseBurnTransaction(const CTransaction& tx)
{
    // Scan all outputs for a valid burn output
    for (const auto& output : tx.vout) {
        if (ValidateBurnFormat(output.scriptPubKey)) {
            auto burnData = BurnData::Parse(output.scriptPubKey);
            if (burnData) {
                return burnData;
            }
        }
    }
    
    return std::nullopt;
}

bool BurnTransactionParser::ValidateBurnFormat(const CScript& script)
{
    // Must start with OP_RETURN
    if (script.empty() || script[0] != OP_RETURN) {
        return false;
    }
    
    // Extract payload
    std::vector<unsigned char> payload = ExtractPayload(script);
    
    // Check payload size
    if (payload.size() != BURN_DATA_SIZE) {
        return false;
    }
    
    // Check burn marker
    if (memcmp(payload.data(), BURN_MARKER, BURN_MARKER_SIZE) != 0) {
        return false;
    }
    
    // Validate chain ID (must be non-zero)
    uint32_t chainId = ReadLE32(&payload[BURN_MARKER_SIZE]);
    if (chainId == 0) {
        return false;
    }
    
    // Validate public key format (first byte must be 0x02 or 0x03 for compressed)
    size_t pubkeyOffset = BURN_MARKER_SIZE + CHAIN_ID_SIZE;
    unsigned char pubkeyPrefix = payload[pubkeyOffset];
    if (pubkeyPrefix != 0x02 && pubkeyPrefix != 0x03) {
        return false;
    }
    
    // Validate amount (must be positive)
    size_t amountOffset = pubkeyOffset + PUBKEY_SIZE;
    CAmount amount = static_cast<CAmount>(ReadLE64(&payload[amountOffset]));
    if (amount <= 0) {
        return false;
    }
    
    return true;
}

CAmount BurnTransactionParser::CalculateBurnedAmount(const CTransaction& tx)
{
    // Note: This method calculates the burned amount from the transaction structure.
    // In practice, the amount encoded in the OP_RETURN should be used for validation.
    // This method is provided for verification purposes.
    
    // For a burn transaction, the burned amount is the value that goes to
    // the OP_RETURN output (which is always 0 in terms of nValue) plus
    // any value that is not accounted for in other outputs.
    
    // Since OP_RETURN outputs have nValue = 0, the burned amount is
    // effectively: sum(inputs) - sum(spendable_outputs) - fee
    
    // However, without access to the UTXO set, we cannot determine input values.
    // Instead, we return the amount encoded in the OP_RETURN.
    
    auto burnData = ParseBurnTransaction(tx);
    if (burnData) {
        return burnData->amount;
    }
    
    return 0;
}

CScript BurnTransactionParser::CreateBurnScript(uint32_t chainId, const CPubKey& recipient, CAmount amount)
{
    // Validate inputs
    if (chainId == 0) {
        return CScript();
    }
    
    if (!recipient.IsValid() || !recipient.IsCompressed()) {
        return CScript();
    }
    
    if (amount < MIN_BURN_AMOUNT || amount > MAX_BURN_AMOUNT) {
        return CScript();
    }
    
    // Build the payload
    std::vector<unsigned char> payload;
    payload.reserve(BURN_DATA_SIZE);
    
    // Add burn marker "L2BURN"
    payload.insert(payload.end(), BURN_MARKER, BURN_MARKER + BURN_MARKER_SIZE);
    
    // Add chain ID (4 bytes, little-endian)
    unsigned char chainIdBytes[4];
    WriteLE32(chainIdBytes, chainId);
    payload.insert(payload.end(), chainIdBytes, chainIdBytes + 4);
    
    // Add recipient public key (33 bytes)
    payload.insert(payload.end(), recipient.begin(), recipient.end());
    
    // Add amount (8 bytes, little-endian)
    unsigned char amountBytes[8];
    WriteLE64(amountBytes, static_cast<uint64_t>(amount));
    payload.insert(payload.end(), amountBytes, amountBytes + 8);
    
    // Create OP_RETURN script
    CScript script;
    script << OP_RETURN << payload;
    
    return script;
}

bool BurnTransactionParser::IsBurnTransaction(const CTransaction& tx)
{
    return GetBurnOutputIndex(tx) >= 0;
}

int BurnTransactionParser::GetBurnOutputIndex(const CTransaction& tx)
{
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        if (ValidateBurnFormat(tx.vout[i].scriptPubKey)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string BurnTransactionParser::ExtractBurnMarker(const CScript& script)
{
    std::vector<unsigned char> payload = ExtractPayload(script);
    
    if (payload.size() < BURN_MARKER_SIZE) {
        return "";
    }
    
    return std::string(reinterpret_cast<const char*>(payload.data()), BURN_MARKER_SIZE);
}

std::vector<unsigned char> BurnTransactionParser::ExtractPayload(const CScript& script)
{
    if (script.empty() || script[0] != OP_RETURN) {
        return {};
    }
    
    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    
    // Skip OP_RETURN
    if (!script.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return {};
    }
    
    // Get the pushed data
    if (!script.GetOp(pc, opcode, data)) {
        return {};
    }
    
    return data;
}

} // namespace l2
