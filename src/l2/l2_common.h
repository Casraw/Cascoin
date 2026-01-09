// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_COMMON_H
#define CASCOIN_L2_COMMON_H

/**
 * @file l2_common.h
 * @brief Common definitions and types for the Cascoin L2 system
 * 
 * This file contains shared definitions, constants, and types used across
 * all L2 components. The L2 system provides a native Layer-2 scaling solution
 * for Cascoin that leverages the HAT v2 reputation system.
 */

#include <uint256.h>
#include <amount.h>
#include <serialize.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace l2 {

/** L2 Protocol Version */
static constexpr uint32_t L2_PROTOCOL_VERSION = 1;

/** Default L2 Chain ID for mainnet */
static constexpr uint64_t DEFAULT_L2_CHAIN_ID = 1;

/** L2 Node operation modes */
enum class L2NodeMode {
    DISABLED = 0,       // No L2 participation, L1 only
    LIGHT_CLIENT = 1,   // Only verifies state roots, minimal storage
    FULL_NODE = 2       // Validates all L2 transactions, stores L2 state (default)
};

/** L2 Transaction types */
enum class L2TxType : uint8_t {
    TRANSFER = 0,           // Standard value transfer
    CONTRACT_DEPLOY = 1,    // Deploy new contract
    CONTRACT_CALL = 2,      // Call existing contract
    DEPOSIT = 3,            // L1 -> L2 deposit
    WITHDRAWAL = 4,         // L2 -> L1 withdrawal
    CROSS_LAYER_MSG = 5,    // Cross-layer message
    SEQUENCER_ANNOUNCE = 6, // Sequencer announcement
    FORCED_INCLUSION = 7    // Forced transaction from L1
};

/** Withdrawal status */
enum class WithdrawalStatus : uint8_t {
    PENDING = 0,        // Waiting for challenge period
    CHALLENGED = 1,     // Under dispute
    READY = 2,          // Challenge period passed, ready to claim
    COMPLETED = 3,      // Successfully withdrawn
    CANCELLED = 4       // Cancelled due to valid challenge
};

/** Message status for cross-layer messaging */
enum class MessageStatus : uint8_t {
    PENDING = 0,
    EXECUTED = 1,
    FAILED = 2,
    CHALLENGED = 3,
    FINALIZED = 4
};

/** Consensus state for sequencer coordination */
enum class ConsensusState : uint8_t {
    WAITING_FOR_PROPOSAL = 0,
    COLLECTING_VOTES = 1,
    CONSENSUS_REACHED = 2,
    CONSENSUS_FAILED = 3,
    FAILOVER_IN_PROGRESS = 4
};

/** Vote type for sequencer consensus */
enum class VoteType : uint8_t {
    ACCEPT = 0,
    REJECT = 1,
    ABSTAIN = 2
};

/** Fraud proof types */
enum class FraudProofType : uint8_t {
    INVALID_STATE_TRANSITION = 0,
    INVALID_TRANSACTION = 1,
    INVALID_SIGNATURE = 2,
    DATA_WITHHOLDING = 3,
    TIMESTAMP_MANIPULATION = 4,
    DOUBLE_SPEND = 5
};

/**
 * Check if L2 is enabled globally
 * @return true if L2 functionality is enabled
 */
bool IsL2Enabled();

/**
 * Get the current L2 node mode
 * @return Current L2NodeMode
 */
L2NodeMode GetL2NodeMode();

/**
 * Get the configured L2 chain ID
 * @return L2 chain ID
 */
uint64_t GetL2ChainId();

/**
 * Convert L2TxType to string for logging
 */
std::string L2TxTypeToString(L2TxType type);

/**
 * Convert WithdrawalStatus to string for logging
 */
std::string WithdrawalStatusToString(WithdrawalStatus status);

/**
 * Stream output operators for enum types (needed for Boost.Test)
 */
inline std::ostream& operator<<(std::ostream& os, L2TxType type) {
    return os << L2TxTypeToString(type);
}

inline std::ostream& operator<<(std::ostream& os, WithdrawalStatus status) {
    return os << WithdrawalStatusToString(status);
}

inline std::ostream& operator<<(std::ostream& os, L2NodeMode mode) {
    switch (mode) {
        case L2NodeMode::DISABLED: return os << "DISABLED";
        case L2NodeMode::LIGHT_CLIENT: return os << "LIGHT_CLIENT";
        case L2NodeMode::FULL_NODE: return os << "FULL_NODE";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, ConsensusState state) {
    switch (state) {
        case ConsensusState::WAITING_FOR_PROPOSAL: return os << "WAITING_FOR_PROPOSAL";
        case ConsensusState::COLLECTING_VOTES: return os << "COLLECTING_VOTES";
        case ConsensusState::CONSENSUS_REACHED: return os << "CONSENSUS_REACHED";
        case ConsensusState::CONSENSUS_FAILED: return os << "CONSENSUS_FAILED";
        case ConsensusState::FAILOVER_IN_PROGRESS: return os << "FAILOVER_IN_PROGRESS";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, VoteType vote) {
    switch (vote) {
        case VoteType::ACCEPT: return os << "ACCEPT";
        case VoteType::REJECT: return os << "REJECT";
        case VoteType::ABSTAIN: return os << "ABSTAIN";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, MessageStatus status) {
    switch (status) {
        case MessageStatus::PENDING: return os << "PENDING";
        case MessageStatus::EXECUTED: return os << "EXECUTED";
        case MessageStatus::FAILED: return os << "FAILED";
        case MessageStatus::CHALLENGED: return os << "CHALLENGED";
        case MessageStatus::FINALIZED: return os << "FINALIZED";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, FraudProofType type) {
    switch (type) {
        case FraudProofType::INVALID_STATE_TRANSITION: return os << "INVALID_STATE_TRANSITION";
        case FraudProofType::INVALID_TRANSACTION: return os << "INVALID_TRANSACTION";
        case FraudProofType::INVALID_SIGNATURE: return os << "INVALID_SIGNATURE";
        case FraudProofType::DATA_WITHHOLDING: return os << "DATA_WITHHOLDING";
        case FraudProofType::TIMESTAMP_MANIPULATION: return os << "TIMESTAMP_MANIPULATION";
        case FraudProofType::DOUBLE_SPEND: return os << "DOUBLE_SPEND";
        default: return os << "UNKNOWN";
    }
}

} // namespace l2

#endif // CASCOIN_L2_COMMON_H
