// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <l2/l2_common.h>
#include <util.h>

namespace l2 {

// Global L2 state - initialized from configuration
static bool g_l2_enabled = true;  // Default: enabled
static L2NodeMode g_l2_mode = L2NodeMode::FULL_NODE;  // Default: full node
static uint64_t g_l2_chain_id = DEFAULT_L2_CHAIN_ID;

bool IsL2Enabled()
{
    return g_l2_enabled;
}

L2NodeMode GetL2NodeMode()
{
    return g_l2_mode;
}

uint64_t GetL2ChainId()
{
    return g_l2_chain_id;
}

std::string L2TxTypeToString(L2TxType type)
{
    switch (type) {
        case L2TxType::TRANSFER:           return "TRANSFER";
        case L2TxType::CONTRACT_DEPLOY:    return "CONTRACT_DEPLOY";
        case L2TxType::CONTRACT_CALL:      return "CONTRACT_CALL";
        case L2TxType::DEPOSIT:            return "DEPOSIT";
        case L2TxType::WITHDRAWAL:         return "WITHDRAWAL";
        case L2TxType::CROSS_LAYER_MSG:    return "CROSS_LAYER_MSG";
        case L2TxType::SEQUENCER_ANNOUNCE: return "SEQUENCER_ANNOUNCE";
        case L2TxType::FORCED_INCLUSION:   return "FORCED_INCLUSION";
        default:                           return "UNKNOWN";
    }
}

std::string WithdrawalStatusToString(WithdrawalStatus status)
{
    switch (status) {
        case WithdrawalStatus::PENDING:    return "PENDING";
        case WithdrawalStatus::CHALLENGED: return "CHALLENGED";
        case WithdrawalStatus::READY:      return "READY";
        case WithdrawalStatus::COMPLETED:  return "COMPLETED";
        case WithdrawalStatus::CANCELLED:  return "CANCELLED";
        default:                           return "UNKNOWN";
    }
}

// Internal functions to set L2 configuration (called from init.cpp)
void SetL2Enabled(bool enabled)
{
    g_l2_enabled = enabled;
}

void SetL2NodeMode(L2NodeMode mode)
{
    g_l2_mode = mode;
}

void SetL2ChainId(uint64_t chainId)
{
    g_l2_chain_id = chainId;
}

} // namespace l2
