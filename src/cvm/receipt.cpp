// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/receipt.h>
#include <cvm/keccak256.h>
#include <univalue.h>
#include <utilstrencodings.h>
#include <tinyformat.h>

#include <array>

namespace CVM {

namespace {

// Ethereum log bloom is a 2048-bit (256-byte) filter.
static constexpr size_t kBloomByteLength = 256;

// Fold one item (a log address or a topic) into the bloom filter using the
// standard Ethereum algorithm: hash the item with keccak256, then for each of
// the three low-order 11-bit indices taken from byte pairs (0,1), (2,3), (4,5)
// set the corresponding bit.
void BloomAdd(std::array<uint8_t, kBloomByteLength>& bloom, const uint8_t* item, size_t len)
{
    uint8_t hash[32];
    Keccak256(item, len, hash);

    for (int i = 0; i < 3; ++i) {
        unsigned int bit = ((static_cast<unsigned int>(hash[2 * i]) << 8) |
                            static_cast<unsigned int>(hash[2 * i + 1])) & 0x7FF;
        // Byte index measured from the most-significant end of the 2048-bit field.
        size_t byteIndex = kBloomByteLength - 1 - (bit >> 3);
        uint8_t bitMask = static_cast<uint8_t>(1u << (bit & 0x7));
        bloom[byteIndex] |= bitMask;
    }
}

} // anonymous namespace

UniValue TransactionReceipt::ToJSON() const
{
    UniValue result(UniValue::VOBJ);
    
    // Ethereum-compatible fields
    result.pushKV("transactionHash", transactionHash.GetHex());
    result.pushKV("transactionIndex", strprintf("0x%x", transactionIndex));
    result.pushKV("blockHash", blockHash.GetHex());
    result.pushKV("blockNumber", strprintf("0x%x", blockNumber));
    result.pushKV("from", from.GetHex());
    result.pushKV("to", to.IsNull() ? "" : to.GetHex());
    result.pushKV("contractAddress", contractAddress.IsNull() ? "" : contractAddress.GetHex());
    result.pushKV("gasUsed", strprintf("0x%llx", (unsigned long long)gasUsed));
    result.pushKV("cumulativeGasUsed", strprintf("0x%llx", (unsigned long long)cumulativeGasUsed));
    result.pushKV("status", strprintf("0x%x", status));
    
    // Logs array
    UniValue logsArray(UniValue::VARR);
    for (size_t i = 0; i < logs.size(); i++) {
        const LogEntry& log = logs[i];
        UniValue logObj(UniValue::VOBJ);
        
        logObj.pushKV("address", log.address.GetHex());
        logObj.pushKV("logIndex", strprintf("0x%x", (unsigned int)i));
        logObj.pushKV("transactionIndex", strprintf("0x%x", transactionIndex));
        logObj.pushKV("transactionHash", transactionHash.GetHex());
        logObj.pushKV("blockHash", blockHash.GetHex());
        logObj.pushKV("blockNumber", strprintf("0x%x", blockNumber));
        logObj.pushKV("data", "0x" + HexStr(log.data));
        
        UniValue topicsArray(UniValue::VARR);
        for (const auto& topic : log.topics) {
            topicsArray.push_back(topic.GetHex());
        }
        logObj.pushKV("topics", topicsArray);
        
        logsArray.push_back(logObj);
    }
    result.pushKV("logs", logsArray);
    
    // Logs bloom filter (2048-bit / 256-byte) computed from every log's address
    // and topics. Empty logs yield an all-zero bloom.
    std::array<uint8_t, kBloomByteLength> bloom;
    bloom.fill(0);
    for (const LogEntry& log : logs) {
        // The emitting contract address (20 big-endian bytes). uint160 stores
        // bytes little-endian, so reverse into display/big-endian order first.
        uint8_t addrBE[20];
        for (int i = 0; i < 20; ++i) {
            addrBE[i] = log.address.begin()[19 - i];
        }
        BloomAdd(bloom, addrBE, sizeof(addrBE));

        // Each topic (32 big-endian bytes).
        for (const uint256& topic : log.topics) {
            uint8_t topicBE[32];
            for (int i = 0; i < 32; ++i) {
                topicBE[i] = topic.begin()[31 - i];
            }
            BloomAdd(bloom, topicBE, sizeof(topicBE));
        }
    }
    result.pushKV("logsBloom", "0x" + HexStr(bloom.begin(), bloom.end()));
    
    // Revert reason if failed
    if (status == 0 && !revertReason.empty()) {
        result.pushKV("revertReason", revertReason);
    }
    
    // Cascoin-specific fields
    UniValue cascoinFields(UniValue::VOBJ);
    cascoinFields.pushKV("senderReputation", (int)senderReputation);
    cascoinFields.pushKV("reputationDiscount", (uint64_t)reputationDiscount);
    cascoinFields.pushKV("usedFreeGas", usedFreeGas);
    result.pushKV("cascoin", cascoinFields);
    
    return result;
}

} // namespace CVM
