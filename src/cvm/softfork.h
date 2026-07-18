// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_SOFTFORK_H
#define CASCOIN_CVM_SOFTFORK_H

#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <cvm/bytecode_detector.h>
#include <cvm/trustnodeid.h>
#include <vector>

namespace CVM {

/**
 * CVM Soft Fork Implementation
 * 
 * CVM uses OP_RETURN to store contract data in a soft-fork compatible way:
 * - Old nodes: See OP_RETURN → Accept block (valid, just unspendable)
 * - New nodes: Parse OP_RETURN → Validate CVM rules
 * 
 * This prevents chain splits and allows gradual upgrade.
 */

// CVM Magic bytes for OP_RETURN identification
// "CVM1" = 0x43564d31
static const std::vector<uint8_t> CVM_MAGIC = {0x43, 0x56, 0x4d, 0x31};

// Maximum OP_RETURN size for CVM transactions (supports contracts up to MAX_CONTRACT_SIZE)
static const size_t MAX_OP_RETURN_SIZE = 25000;

/**
 * CVM Transaction Types (in OP_RETURN)
 */
enum class CVMOpType : uint8_t {
    CONTRACT_DEPLOY = 0x01,  // Deploy contract (CVM or EVM)
    CONTRACT_CALL = 0x02,    // Call contract (CVM or EVM)
    REPUTATION_VOTE = 0x03,  // Simple reputation vote (no bond)
    TRUST_EDGE = 0x04,       // Web-of-Trust: Add trust relationship (bonded)
    BONDED_VOTE = 0x05,      // Web-of-Trust: Bonded reputation vote
    DAO_DISPUTE = 0x06,      // Web-of-Trust: Create DAO dispute
    DAO_VOTE = 0x07,         // Web-of-Trust: Vote on DAO dispute
    EVM_DEPLOY = 0x08,       // Deploy EVM contract (explicit EVM format)
    EVM_CALL = 0x09,         // Call EVM contract (explicit EVM format)
    NONE = 0x00
};

// Forward declare BytecodeFormat from bytecode_detector.h
// We'll use the existing enum instead of defining a new one

/**
 * Build OP_RETURN script with CVM data
 * Format: OP_RETURN <CVM_MAGIC> <OpType> <Data>
 */
CScript BuildCVMOpReturn(CVMOpType opType, const std::vector<uint8_t>& data);

/**
 * Check if transaction output contains CVM OP_RETURN
 */
bool IsCVMOpReturn(const CTxOut& txout);

/**
 * Parse CVM data from OP_RETURN output
 * Returns true if valid CVM OP_RETURN found
 */
bool ParseCVMOpReturn(const CTxOut& txout, CVMOpType& opType, std::vector<uint8_t>& data);

/**
 * Find CVM OP_RETURN output in transaction
 * Returns output index, or -1 if not found
 */
int FindCVMOpReturn(const CTransaction& tx);

/**
 * Create contract deployment transaction with OP_RETURN
 * 
 * Structure:
 * - Input: Funding from deployer
 * - Output 0: OP_RETURN with contract bytecode hash + metadata
 * - Output 1: Contract address (P2SH of contract)
 * - Output 2: Change back to deployer
 * 
 * The actual bytecode is stored off-chain or in witness data
 */
struct CVMDeployData {
    uint256 codeHash;              // Hash of contract bytecode
    uint64_t gasLimit;             // Gas limit for deployment
    BytecodeFormat format;         // Bytecode format (CVM/EVM/AUTO)
    std::vector<uint8_t> metadata; // Additional metadata (max 32 bytes)
    
    // Extended fields (not in OP_RETURN, stored separately)
    std::vector<uint8_t> bytecode;        // Full contract bytecode
    std::vector<uint8_t> constructorData; // Constructor parameters
    
    CVMDeployData() : gasLimit(0), format(BytecodeFormat::UNKNOWN) {}
    
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Contract call data in OP_RETURN
 */
struct CVMCallData {
    uint160 contractAddress;       // Target contract
    uint64_t gasLimit;             // Gas limit
    BytecodeFormat format;         // Expected contract format (CVM/EVM/AUTO)
    std::vector<uint8_t> callData; // Function call data (max 32 bytes)
    
    CVMCallData() : gasLimit(0), format(BytecodeFormat::UNKNOWN) {}
    
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Reputation vote data in OP_RETURN (Simple, no bond)
 */
struct CVMReputationData {
    uint160 targetAddress;         // Address being voted on
    int16_t voteValue;             // Vote value (-100 to +100)
    uint32_t timestamp;            // Vote timestamp
    
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Web-of-Trust: Trust Edge data in OP_RETURN
 *
 * Represents "from trusts to with weight X"
 * Must be accompanied by bond output in same transaction
 *
 * Versioned, backward-compatible payload:
 *   - v1 (legacy): a fixed 54-byte layout with NO version byte,
 *     `from(20) to(20) weight(2) bond(8) ts(4)`, where from/to are bare
 *     uint160 P2PKH identifiers. Historical OP_RETURNs use this layout and
 *     MUST keep decoding identically.
 *   - v2: a version byte (>= VERSION_V2) followed by `from`/`to` encoded as a
 *     wide, lossless TrustNodeId (type + 32-byte value), able to carry
 *     P2WSH / quantum (WitnessV2Quantum) identifiers that do not fit in
 *     uint160.
 *
 * v2 is emitted ONLY when a P2WSH or quantum node is involved; pure-uint160
 * edges continue to be emitted as v1 so existing behavior/bytes are unchanged.
 */
struct CVMTrustEdgeData {
    // Legacy uint160 identifiers. Retained for backward compatibility with the
    // v1 path and with callers/consumers that operate purely in uint160 space.
    // When the wide `from`/`to` are not explicitly set (type == 0) these drive
    // v1 emission; on read they are always populated (from the wide identifier
    // for v2, directly for v1) so uint160-only consumers keep working.
    uint160 fromAddress;           // Who establishes trust (legacy uint160 view)
    uint160 toAddress;             // Who is trusted (legacy uint160 view)

    // Canonical wide identifiers (v2). When either identifier is a P2WSH or
    // quantum node the payload is emitted as v2 so the full 32-byte identifier
    // is preserved on-chain. A default-constructed value (type == 0) means
    // "unset": the legacy uint160 fields are used to derive a P2PKH node.
    TrustNodeId from;
    TrustNodeId to;

    int16_t weight;                // Trust weight (-100 to +100)
    CAmount bondAmount;            // CAS bonded (locked in output)
    uint32_t timestamp;            // When established

    // On-chain payload version byte for the v2 (wide) layout. v1 has no
    // version byte and is a fixed 54-byte layout.
    static const uint8_t VERSION_V2 = 2;

    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Web-of-Trust: Bonded Vote data in OP_RETURN
 * 
 * Similar to CVMReputationData but with bond tracking
 * Bond locked in separate output, can be slashed by DAO
 */
struct CVMBondedVoteData {
    uint160 voter;                 // Who is voting
    uint160 target;                // Who is being voted on
    int16_t voteValue;             // Vote value (-100 to +100)
    CAmount bondAmount;            // CAS bonded (locked in output)
    uint32_t timestamp;            // When vote was cast
    
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Web-of-Trust: DAO Dispute data in OP_RETURN
 * 
 * Challenge a bonded vote as malicious
 * DAO members will vote to slash or keep the bond
 */
struct CVMDAODisputeData {
    uint256 originalVoteTxHash;    // Vote being disputed
    uint160 challenger;            // Who challenges
    CAmount challengeBond;         // Challenger's bond
    std::string reason;            // Challenge reason (max 64 chars)
    uint32_t timestamp;
    
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Web-of-Trust: DAO Vote data in OP_RETURN
 * 
 * DAO member votes on a dispute
 * Stake-weighted voting
 */
struct CVMDAOVoteData {
    uint256 disputeId;             // Dispute being voted on
    uint160 daoMember;             // DAO member voting
    bool supportSlash;             // true = slash, false = keep
    CAmount stake;                 // Amount staked
    uint32_t timestamp;
    
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
};

/**
 * Soft Fork Validation
 * 
 * This validates CVM transactions for NEW nodes only.
 * Old nodes will not call this - they just see normal transactions.
 * 
 * Returns:
 * - true: Valid CVM transaction
 * - false: Invalid (but only enforced on new nodes)
 */
bool ValidateCVMSoftFork(const CTransaction& tx, int height, 
                         const class Consensus::Params& params,
                         std::string& error);

/**
 * Check if we should enforce CVM rules (soft fork active)
 */
bool IsCVMSoftForkActive(int height, const class Consensus::Params& params);

/**
 * Validate EVM contract deployment transaction
 * 
 * Checks:
 * - Valid OP_RETURN format
 * - Bytecode format is EVM or AUTO
 * - Gas limit is reasonable
 * - Deployer has sufficient reputation (if required)
 * 
 * @param tx Transaction to validate
 * @param deployData Parsed deployment data
 * @param height Block height
 * @param error Error message if validation fails
 * @return true if valid
 */
bool ValidateEVMDeployment(const CTransaction& tx, 
                          const CVMDeployData& deployData,
                          int height,
                          std::string& error);

/**
 * Validate EVM contract call transaction
 * 
 * Checks:
 * - Valid OP_RETURN format
 * - Contract exists
 * - Gas limit is reasonable
 * - Call data is valid
 * 
 * @param tx Transaction to validate
 * @param callData Parsed call data
 * @param height Block height
 * @param error Error message if validation fails
 * @return true if valid
 */
bool ValidateEVMCall(const CTransaction& tx,
                    const CVMCallData& callData,
                    int height,
                    std::string& error);

/**
 * Check if transaction is an EVM contract transaction
 * 
 * @param tx Transaction to check
 * @return true if EVM contract deployment or call
 */
bool IsEVMTransaction(const CTransaction& tx);

/**
 * Get bytecode format from transaction
 * 
 * @param tx Transaction
 * @return Bytecode format (CVM, EVM, or AUTO)
 */
BytecodeFormat GetTransactionBytecodeFormat(const CTransaction& tx);

/**
 * Parse deployment data from raw bytes
 * 
 * @param data Raw data from OP_RETURN
 * @param deployData Output deployment data structure
 * @return true if successfully parsed
 */
bool ParseCVMDeployData(const std::vector<uint8_t>& data, CVMDeployData& deployData);

/**
 * Parse call data from raw bytes
 * 
 * @param data Raw data from OP_RETURN
 * @param callData Output call data structure
 * @return true if successfully parsed
 */
bool ParseCVMCallData(const std::vector<uint8_t>& data, CVMCallData& callData);

/**
 * Extract deployer address from contract deployment transaction
 * 
 * Uses the same logic as sender extraction in consensus_validator.cpp.
 * Parses P2PKH and P2WPKH scripts to extract the deployer address
 * from the first input of the transaction.
 * 
 * Requirements: 11.1
 * 
 * @param tx Transaction to extract deployer from
 * @param deployerOut Output parameter for the extracted deployer address
 * @return true if deployer was successfully extracted
 */
bool ExtractDeployerAddress(const CTransaction& tx, uint160& deployerOut);

/**
 * Check if deployer has sufficient reputation for contract deployment
 * 
 * Verifies that the deployer's reputation meets the minimum threshold.
 * Uses HAT v2 (SecureHAT) if available, falls back to ASRS.
 * 
 * Requirements: 11.2
 * 
 * @param deployer Address of the contract deployer
 * @param minReputation Minimum reputation threshold (default: 50.0)
 * @return true if deployer meets reputation requirement
 */
bool CheckDeployerReputation(const uint160& deployer, double minReputation = 50.0);

/**
 * Verify contract format byte matches expected format
 * 
 * Checks the format byte at offset 0 of the contract data to ensure
 * it matches the expected bytecode format.
 * 
 * Requirements: 11.3
 * 
 * @param contractData Contract bytecode data
 * @param expectedFormat Expected bytecode format
 * @return true if format matches or is compatible
 */
bool VerifyContractFormat(const std::vector<uint8_t>& contractData, BytecodeFormat expectedFormat);

} // namespace CVM

#endif // CASCOIN_CVM_SOFTFORK_H

