// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/receipt.h>
#include <univalue.h>
#include <utilstrencodings.h>

namespace CVM {

UniValue TransactionReceipt::ToJSON() const
{
    UniValue result(UniValue::VOBJ);
    
    // Ethereum-compatible fields
    result.pushKV("transactionHash", transactionHash.GetHex());
    result.pushKV("transactionIndex", HexStr(transactionIndex));
    result.pushKV("blockHash", blockHash.GetHex());
    result.pushKV("blockNumber", HexStr(blockNumber));
    result.pushKV("from", from.GetHex());
    result.pushKV("to", to.IsNull() ? "" : to.GetHex());
    result.pushKV("contractAddress", contractAddress.IsNull() ? "" : contractAddress.GetHex());
    result.pushKV("gasUsed", HexStr(gasUsed));
    result.pushKV("cumulativeGasUsed", HexStr(cumulativeGasUsed));
    result.pushKV("status", HexStr(status));
    
    // Logs array
    UniValue logsArray(UniValue::VARR);
    for (size_t i = 0; i < logs.size(); i++) {
        const LogEntry& log = logs[i];
        UniValue logObj(UniValue::VOBJ);
        
        logObj.pushKV("address", log.address.GetHex());
        logObj.pushKV("logIndex", HexStr(i));
        logObj.pushKV("transactionIndex", HexStr(transactionIndex));
        logObj.pushKV("transactionHash", transactionHash.GetHex());
        logObj.pushKV("blockHash", blockHash.GetHex());
        logObj.pushKV("blockNumber", HexStr(blockNumber));
        logObj.pushKV("data", HexStr(log.data));
        
        UniValue topicsArray(UniValue::VARR);
        for (const auto& topic : log.topics) {
            topicsArray.push_back(topic.GetHex());
        }
        logObj.pushKV("topics", topicsArray);
        
        logsArray.push_back(logObj);
    }
    result.pushKV("logs", logsArray);
    
    // Bloom filter (simplified - just empty for now)
    result.pushKV("logsBloom", "0x" + std::string(512, '0'));
    
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
