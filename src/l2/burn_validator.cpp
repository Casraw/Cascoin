// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file burn_validator.cpp
 * @brief Implementation of Burn Transaction Validator
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4
 */

#include <l2/burn_validator.h>
#include <util.h>

namespace l2 {

// ============================================================================
// BurnValidator Implementation
// ============================================================================

BurnValidator::BurnValidator(uint32_t chainId)
    : chainId_(chainId)
    , txFetcher_(nullptr)
    , confGetter_(nullptr)
    , blockInfoGetter_(nullptr)
    , processedChecker_(nullptr)
{
}

BurnValidator::BurnValidator(
    uint32_t chainId,
    TxFetcher txFetcher,
    ConfirmationGetter confGetter,
    BlockInfoGetter blockInfoGetter,
    ProcessedChecker processedChecker)
    : chainId_(chainId)
    , txFetcher_(std::move(txFetcher))
    , confGetter_(std::move(confGetter))
    , blockInfoGetter_(std::move(blockInfoGetter))
    , processedChecker_(std::move(processedChecker))
{
}

BurnValidationResult BurnValidator::ValidateBurn(const uint256& l1TxHash)
{
    LOCK(cs_validator_);
    
    // Check if already processed (Requirement 2.4)
    if (IsAlreadyProcessed(l1TxHash)) {
        LogPrintf("BurnValidator: Burn %s already processed\n", l1TxHash.ToString());
        return BurnValidationResult::Failure("Burn transaction already processed");
    }
    
    // Fetch the L1 transaction
    auto txOpt = FetchL1Transaction(l1TxHash);
    if (!txOpt) {
        LogPrintf("BurnValidator: Could not fetch transaction %s\n", l1TxHash.ToString());
        return BurnValidationResult::Failure("Could not fetch L1 transaction");
    }
    
    const CTransaction& tx = *txOpt;
    
    // Validate OP_RETURN format and parse burn data (Requirement 2.1)
    auto burnDataOpt = BurnTransactionParser::ParseBurnTransaction(tx);
    if (!burnDataOpt) {
        LogPrintf("BurnValidator: Invalid burn format for %s\n", l1TxHash.ToString());
        return BurnValidationResult::Failure("Invalid OP_RETURN burn format");
    }
    
    const BurnData& burnData = *burnDataOpt;
    
    // Validate burn data
    if (!burnData.IsValid()) {
        LogPrintf("BurnValidator: Invalid burn data for %s\n", l1TxHash.ToString());
        return BurnValidationResult::Failure("Invalid burn data");
    }
    
    // Check chain ID (Requirement 2.3)
    if (!MatchesChainId(burnData)) {
        LogPrintf("BurnValidator: Chain ID mismatch for %s (expected %u, got %u)\n",
                  l1TxHash.ToString(), chainId_, burnData.chainId);
        return BurnValidationResult::Failure("Chain ID mismatch");
    }
    
    // Check confirmations (Requirement 2.2)
    int confirmations = GetConfirmationCount(l1TxHash);
    if (confirmations < REQUIRED_CONFIRMATIONS) {
        LogPrintf("BurnValidator: Insufficient confirmations for %s (%d < %d)\n",
                  l1TxHash.ToString(), confirmations, REQUIRED_CONFIRMATIONS);
        return BurnValidationResult::Failure(
            strprintf("Insufficient confirmations: %d < %d required",
                      confirmations, REQUIRED_CONFIRMATIONS));
    }
    
    // Get block info
    uint256 blockHash;
    uint64_t blockNumber = 0;
    
    auto blockInfoOpt = GetBlockInfo(l1TxHash);
    if (blockInfoOpt) {
        blockHash = blockInfoOpt->first;
        blockNumber = blockInfoOpt->second;
    }
    
    LogPrintf("BurnValidator: Validated burn %s - amount: %lld, recipient: %s, confirmations: %d\n",
              l1TxHash.ToString(),
              burnData.amount,
              burnData.GetRecipientAddress().ToString(),
              confirmations);
    
    return BurnValidationResult::Success(burnData, confirmations, blockHash, blockNumber);
}

bool BurnValidator::HasSufficientConfirmations(const uint256& l1TxHash) const
{
    int confirmations = GetConfirmationCount(l1TxHash);
    return confirmations >= REQUIRED_CONFIRMATIONS;
}

bool BurnValidator::MatchesChainId(const BurnData& data) const
{
    return data.chainId == chainId_;
}

bool BurnValidator::IsAlreadyProcessed(const uint256& l1TxHash) const
{
    // First check external callback if available
    if (processedChecker_) {
        return processedChecker_(l1TxHash);
    }
    
    // Fall back to internal tracking
    LOCK(cs_validator_);
    return processedBurns_.count(l1TxHash) > 0;
}

void BurnValidator::MarkAsProcessed(const uint256& l1TxHash)
{
    LOCK(cs_validator_);
    processedBurns_.insert(l1TxHash);
}

void BurnValidator::ClearProcessed()
{
    LOCK(cs_validator_);
    processedBurns_.clear();
}

size_t BurnValidator::GetProcessedCount() const
{
    LOCK(cs_validator_);
    return processedBurns_.size();
}

int BurnValidator::GetConfirmationCount(const uint256& txHash) const
{
    if (confGetter_) {
        return confGetter_(txHash);
    }
    
    // Default: return 0 if no callback set
    return 0;
}

std::optional<CTransaction> BurnValidator::FetchL1Transaction(const uint256& txHash) const
{
    if (txFetcher_) {
        return txFetcher_(txHash);
    }
    
    // Default: return nullopt if no callback set
    return std::nullopt;
}

std::optional<std::pair<uint256, uint64_t>> BurnValidator::GetBlockInfo(const uint256& txHash) const
{
    if (blockInfoGetter_) {
        return blockInfoGetter_(txHash);
    }
    
    // Default: return nullopt if no callback set
    return std::nullopt;
}

} // namespace l2
