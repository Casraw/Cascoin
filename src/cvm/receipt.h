// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_RECEIPT_H
#define CASCOIN_CVM_RECEIPT_H

#include <uint256.h>
#include <serialize.h>
#include <univalue.h>
#include <vector>
#include <string>

namespace CVM {

/**
 * Log entry emitted by a contract during execution
 */
struct LogEntry {
    uint160 address;                    // Contract address that emitted the log
    std::vector<uint256> topics;        // Indexed log topics (up to 4)
    std::vector<uint8_t> data;          // Non-indexed log data
    
    LogEntry() = default;
    LogEntry(const uint160& addr, const std::vector<uint256>& topics_, const std::vector<uint8_t>& data_)
        : address(addr), topics(topics_), data(data_) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(topics);
        READWRITE(data);
    }
};

/**
 * Transaction receipt for EVM/CVM contract execution
 * Compatible with Ethereum receipt format
 */
struct TransactionReceipt {
    uint256 transactionHash;            // Hash of the transaction
    uint32_t transactionIndex;          // Index of transaction in block
    uint256 blockHash;                  // Hash of the block containing this transaction
    uint32_t blockNumber;               // Block number
    uint160 from;                       // Sender address
    uint160 to;                         // Recipient address (empty for contract creation)
    uint160 contractAddress;            // Address of created contract (empty if not a creation)
    uint64_t gasUsed;                   // Gas used by this transaction
    uint64_t cumulativeGasUsed;         // Total gas used in the block up to this transaction
    std::vector<LogEntry> logs;         // Log entries emitted during execution
    uint8_t status;                     // 1 = success, 0 = failure
    std::string revertReason;           // Reason for revert (if status = 0)
    
    // Cascoin-specific fields
    uint8_t senderReputation;           // Reputation of sender at execution time
    uint64_t reputationDiscount;        // Gas discount applied due to reputation
    bool usedFreeGas;                   // Whether free gas allowance was used
    
    TransactionReceipt() 
        : transactionIndex(0), blockNumber(0), gasUsed(0), 
          cumulativeGasUsed(0), status(0), senderReputation(50),
          reputationDiscount(0), usedFreeGas(false) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(transactionHash);
        READWRITE(transactionIndex);
        READWRITE(blockHash);
        READWRITE(blockNumber);
        READWRITE(from);
        READWRITE(to);
        READWRITE(contractAddress);
        READWRITE(gasUsed);
        READWRITE(cumulativeGasUsed);
        READWRITE(logs);
        READWRITE(status);
        READWRITE(revertReason);
        READWRITE(senderReputation);
        READWRITE(reputationDiscount);
        READWRITE(usedFreeGas);
    }
    
    /**
     * Convert receipt to JSON format (Ethereum-compatible)
     */
    UniValue ToJSON() const;
    
    /**
     * Check if this is a contract creation receipt
     */
    bool IsContractCreation() const {
        return !contractAddress.IsNull();
    }
    
    /**
     * Check if execution was successful
     */
    bool IsSuccess() const {
        return status == 1;
    }
};

} // namespace CVM

#endif // CASCOIN_CVM_RECEIPT_H
