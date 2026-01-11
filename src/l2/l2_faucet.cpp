// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_faucet.cpp
 * @brief Implementation of L2 Testnet Faucet
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <l2/l2_faucet.h>
#include <chainparams.h>
#include <util.h>

#include <chrono>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

L2Faucet::L2Faucet(L2TokenManager& tokenManager)
    : tokenManager_(tokenManager)
    , totalDistributed_(0)
{
    LogPrintf("L2Faucet: Initialized for chain %lu (%s)\n",
              tokenManager_.GetChainId(),
              IsEnabled() ? "enabled" : "disabled - mainnet");
}

// ============================================================================
// Static Methods
// ============================================================================

bool L2Faucet::IsEnabled()
{
    // Requirement 5.1: Faucet available on regtest/testnet
    // Requirement 5.5: Faucet disabled on mainnet
    
    const std::string& networkId = Params().NetworkIDString();
    
    // Enable on testnet and regtest only
    return (networkId == "test" || networkId == "regtest");
}

uint64_t L2Faucet::GetCurrentTime(uint64_t providedTime)
{
    if (providedTime > 0) {
        return providedTime;
    }
    
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ============================================================================
// Request Handling (Requirements 5.2, 5.3, 5.4)
// ============================================================================

bool L2Faucet::CanRequest(const uint160& address, uint64_t currentTime) const
{
    // Requirement 5.3: 1 hour cooldown per address
    
    LOCK(cs_faucet_);
    
    auto it = lastRequest_.find(address);
    if (it == lastRequest_.end()) {
        // Never requested before
        return true;
    }
    
    uint64_t now = GetCurrentTime(currentTime);
    uint64_t lastTime = it->second;
    
    return (now >= lastTime + COOLDOWN_SECONDS);
}

uint64_t L2Faucet::GetCooldownRemaining(const uint160& address, uint64_t currentTime) const
{
    // Requirement 5.3: 1 hour cooldown per address
    
    LOCK(cs_faucet_);
    
    auto it = lastRequest_.find(address);
    if (it == lastRequest_.end()) {
        // Never requested before
        return 0;
    }
    
    uint64_t now = GetCurrentTime(currentTime);
    uint64_t lastTime = it->second;
    uint64_t cooldownEnd = lastTime + COOLDOWN_SECONDS;
    
    if (now >= cooldownEnd) {
        return 0;
    }
    
    return cooldownEnd - now;
}

FaucetResult L2Faucet::RequestTokens(
    const uint160& recipient,
    CAmount requestedAmount,
    L2StateManager& stateManager)
{
    // Requirement 5.1, 5.5: Check if faucet is enabled
    if (!IsEnabled()) {
        return FaucetResult::Failure("Faucet is only available on testnet/regtest");
    }
    
    // Validate recipient address
    if (recipient.IsNull()) {
        return FaucetResult::Failure("Invalid recipient address");
    }
    
    // Validate requested amount
    if (requestedAmount <= 0) {
        return FaucetResult::Failure("Requested amount must be greater than zero");
    }
    
    // Requirement 5.2: Cap at MAX_FAUCET_AMOUNT (100 tokens)
    CAmount actualAmount = std::min(requestedAmount, MAX_FAUCET_AMOUNT);
    
    LOCK(cs_faucet_);
    
    uint64_t currentTime = GetCurrentTime(0);
    
    // Requirement 5.3: Check cooldown
    auto it = lastRequest_.find(recipient);
    if (it != lastRequest_.end()) {
        uint64_t lastTime = it->second;
        if (currentTime < lastTime + COOLDOWN_SECONDS) {
            uint64_t remaining = (lastTime + COOLDOWN_SECONDS) - currentTime;
            uint64_t minutes = remaining / 60;
            return FaucetResult::Failure(
                "Please wait " + std::to_string(minutes) + " minutes before requesting again",
                remaining);
        }
    }
    
    // Credit tokens to recipient
    uint256 addressKey = AddressToKey(recipient);
    AccountState state = stateManager.GetAccountState(addressKey);
    
    // Check for balance overflow
    if (state.balance + actualAmount < state.balance) {
        return FaucetResult::Failure("Recipient balance overflow");
    }
    
    // Credit the tokens
    state.balance += actualAmount;
    state.lastActivity = stateManager.GetBlockNumber();
    stateManager.SetAccountState(addressKey, state);
    
    // Update last request time
    lastRequest_[recipient] = currentTime;
    
    // Requirement 5.4, 5.6: Record distribution with test token marking
    RecordDistribution(recipient, actualAmount, currentTime);
    
    // Update total distributed
    totalDistributed_ += actualAmount;
    
    // Generate a transaction hash for this faucet distribution
    CHashWriter ss(SER_GETHASH, 0);
    ss << recipient;
    ss << actualAmount;
    ss << currentTime;
    ss << tokenManager_.GetChainId();
    uint256 txHash = ss.GetHash();
    
    LogPrintf("L2Faucet: Distributed %lld %s to %s\n",
              actualAmount / COIN, tokenManager_.GetTokenSymbol(),
              recipient.ToString().substr(0, 16));
    
    return FaucetResult::Success(actualAmount, txHash);
}

// ============================================================================
// Distribution Logging (Requirements 5.4, 5.6)
// ============================================================================

void L2Faucet::RecordDistribution(const uint160& recipient, CAmount amount, uint64_t timestamp)
{
    // Requirement 5.4: Mark distributed tokens as "test tokens"
    // Requirement 5.6: Log all distributions for audit purposes
    
    // Note: cs_faucet_ should already be held by caller
    
    FaucetDistribution dist(
        recipient,
        amount,
        timestamp,
        tokenManager_.GetChainId(),
        "Faucet distribution - test tokens"
    );
    
    distributionLog_.push_back(dist);
}

std::vector<FaucetDistribution> L2Faucet::GetDistributionLog() const
{
    LOCK(cs_faucet_);
    return distributionLog_;
}

std::vector<FaucetDistribution> L2Faucet::GetDistributionLog(const uint160& address) const
{
    LOCK(cs_faucet_);
    
    std::vector<FaucetDistribution> result;
    for (const auto& dist : distributionLog_) {
        if (dist.recipient == address) {
            result.push_back(dist);
        }
    }
    
    return result;
}

// ============================================================================
// Statistics
// ============================================================================

CAmount L2Faucet::GetTotalDistributed() const
{
    LOCK(cs_faucet_);
    return totalDistributed_;
}

size_t L2Faucet::GetUniqueRecipientCount() const
{
    LOCK(cs_faucet_);
    return lastRequest_.size();
}

// ============================================================================
// Testing Support
// ============================================================================

void L2Faucet::Clear()
{
    LOCK(cs_faucet_);
    lastRequest_.clear();
    distributionLog_.clear();
    totalDistributed_ = 0;
}

} // namespace l2
