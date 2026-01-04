// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CONTRACT_H
#define CASCOIN_CVM_CONTRACT_H

#include <uint256.h>
#include <serialize.h>
#include <primitives/transaction.h>
#include <vector>
#include <string>

namespace CVM {

/**
 * Contract data stored on-chain
 */
class Contract {
public:
    uint160 address;           // Contract address
    std::vector<uint8_t> code; // Contract bytecode
    int deploymentHeight;      // Block height when deployed
    uint256 deploymentTx;      // Transaction that deployed contract
    bool isCleanedUp;          // Whether contract storage has been cleaned up
    
    Contract() : deploymentHeight(0), isCleanedUp(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(code);
        READWRITE(deploymentHeight);
        READWRITE(deploymentTx);
        READWRITE(isCleanedUp);
    }
};

/**
 * Contract transaction types
 * These are stored in transaction data (OP_RETURN style or new tx type)
 */
enum class ContractTxType : uint8_t {
    DEPLOY = 0x01,    // Deploy new contract
    CALL = 0x02,      // Call existing contract
    NONE = 0x00
};

/**
 * Contract deployment transaction data
 */
class ContractDeployTx {
public:
    std::vector<uint8_t> code;     // Contract bytecode
    uint64_t gasLimit;             // Gas limit for deployment
    std::vector<uint8_t> initData; // Constructor parameters
    
    ContractDeployTx() : gasLimit(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(code);
        READWRITE(gasLimit);
        READWRITE(initData);
    }
    
    /**
     * Serialize to vector for embedding in transaction
     */
    std::vector<uint8_t> Serialize() const;
    
    /**
     * Deserialize from transaction data
     */
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Contract call transaction data
 */
class ContractCallTx {
public:
    uint160 contractAddress;       // Contract to call
    uint64_t gasLimit;             // Gas limit for call
    uint64_t value;                // Amount to send to contract
    std::vector<uint8_t> data;     // Call data / parameters
    
    ContractCallTx() : gasLimit(0), value(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(contractAddress);
        READWRITE(gasLimit);
        READWRITE(value);
        READWRITE(data);
    }
    
    /**
     * Serialize to vector for embedding in transaction
     */
    std::vector<uint8_t> Serialize() const;
    
    /**
     * Deserialize from transaction data
     */
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Parse contract transaction type from transaction
 */
ContractTxType GetContractTxType(const CTransaction& tx);

/**
 * Extract contract deployment data from transaction
 */
bool ParseContractDeployTx(const CTransaction& tx, ContractDeployTx& deployTx);

/**
 * Extract contract call data from transaction
 */
bool ParseContractCallTx(const CTransaction& tx, ContractCallTx& callTx);

/**
 * Check if transaction is a contract transaction
 */
bool IsContractTransaction(const CTransaction& tx);

/**
 * Generate contract address from transaction
 * Uses deployer address and nonce similar to Ethereum
 */
uint160 GenerateContractAddress(const uint160& deployerAddr, uint64_t nonce);

/**
 * Validate contract bytecode
 */
bool ValidateContractCode(const std::vector<uint8_t>& code, std::string& error);

} // namespace CVM

#endif // CASCOIN_CVM_CONTRACT_H

