// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file rate_limiter.cpp
 * @brief Implementation of Rate Limiting and Spam Protection for L2
 * 
 * Requirements: 26.1, 26.2, 26.3, 26.4, 26.5, 26.6
 */

#include <l2/rate_limiter.h>
#include <util.h>

#include <algorithm>
#include <cmath>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

RateLimiter::RateLimiter(uint64_t blockGasLimit)
    : currentBlockNumber_(0)
    , currentBlockGasUsed_(0)
{
    gasPricing_.blockGasLimit = blockGasLimit;
    gasPricing_.baseFee = BASE_GAS_PRICE;
    gasPricing_.priceMultiplier = 100;  // 1x
    gasPricing_.averageUtilization = 0;
}

// ============================================================================
// Rate Limit Checking (Requirements 26.2, 26.3)
// ============================================================================

RateLimitCheckResult RateLimiter::CheckRateLimit(
    const uint160& address,
    CAmount gasPrice,
    uint64_t gasLimit,
    uint64_t currentBlock)
{
    LOCK(cs_ratelimit_);

    // Update current block if needed
    if (currentBlock > currentBlockNumber_) {
        OnNewBlock(currentBlock);
    }

    // Check minimum gas price (Requirement 26.6)
    CAmount effectiveGasPrice = GetEffectiveGasPrice();
    if (gasPrice < effectiveGasPrice) {
        return RateLimitCheckResult::Denied(
            "Gas price too low. Required: " + std::to_string(effectiveGasPrice) +
            ", offered: " + std::to_string(gasPrice),
            0
        );
    }

    // Check block gas capacity (Requirement 26.1)
    if (!HasBlockCapacity(gasLimit, currentBlock)) {
        return RateLimitCheckResult::Denied(
            "Block gas limit exceeded",
            1  // Wait for next block
        );
    }

    // Get or create rate limit info for address
    AddressRateLimitInfo& info = EnsureRateLimitInfo(address);

    // Check if address is rate-limited
    if (info.isRateLimited && currentBlock < info.rateLimitExpiresBlock) {
        uint32_t waitBlocks = static_cast<uint32_t>(info.rateLimitExpiresBlock - currentBlock);
        return RateLimitCheckResult::Denied(
            "Address is rate-limited",
            waitBlocks
        );
    } else if (info.isRateLimited && currentBlock >= info.rateLimitExpiresBlock) {
        // Rate limit expired
        info.isRateLimited = false;
        info.rateLimitExpiresBlock = 0;
    }

    // Get current block transaction count for this address
    uint32_t txInCurrentBlock = 0;
    if (!info.txCountsPerBlock.empty() && info.txCountsPerBlock.back().first == currentBlock) {
        txInCurrentBlock = info.txCountsPerBlock.back().second;
    }

    // Get rate limit based on reputation (Requirement 26.3)
    uint32_t rateLimit = CalculateRateLimit(info.reputationScore);

    // Check if within rate limit (Requirement 26.2)
    if (txInCurrentBlock >= rateLimit) {
        // Apply cooldown
        info.isRateLimited = true;
        info.rateLimitExpiresBlock = currentBlock + RATE_LIMIT_COOLDOWN_BLOCKS;
        
        return RateLimitCheckResult::Denied(
            "Rate limit exceeded for this block. Limit: " + std::to_string(rateLimit),
            1  // Wait for next block
        );
    }

    return RateLimitCheckResult::Allowed(rateLimit, txInCurrentBlock, effectiveGasPrice);
}

void RateLimiter::RecordTransaction(
    const uint160& address,
    uint64_t gasUsed,
    uint64_t currentBlock)
{
    LOCK(cs_ratelimit_);

    // Update current block if needed
    if (currentBlock > currentBlockNumber_) {
        OnNewBlock(currentBlock);
    }

    // Update block gas usage
    currentBlockGasUsed_ += gasUsed;

    // Get or create rate limit info
    AddressRateLimitInfo& info = EnsureRateLimitInfo(address);

    // Update transaction count for current block
    if (info.txCountsPerBlock.empty() || info.txCountsPerBlock.back().first != currentBlock) {
        info.txCountsPerBlock.push_back({currentBlock, 1});
    } else {
        info.txCountsPerBlock.back().second++;
    }

    // Update totals
    info.totalTxInWindow++;
    info.lastActivityBlock = currentBlock;
    info.gasUsedInCurrentBlock += gasUsed;
    info.currentBlockNumber = currentBlock;

    // Clean up old entries
    CleanupOldEntries(currentBlock);
}

uint32_t RateLimiter::GetRateLimitForAddress(const uint160& address) const
{
    LOCK(cs_ratelimit_);

    auto it = addressLimits_.find(address);
    if (it == addressLimits_.end()) {
        return DEFAULT_NEW_ADDRESS_TX_LIMIT;
    }

    return CalculateRateLimit(it->second.reputationScore);
}

uint32_t RateLimiter::GetTxCountInBlock(const uint160& address, uint64_t currentBlock) const
{
    LOCK(cs_ratelimit_);

    auto it = addressLimits_.find(address);
    if (it == addressLimits_.end()) {
        return 0;
    }

    const auto& info = it->second;
    if (info.txCountsPerBlock.empty()) {
        return 0;
    }

    if (info.txCountsPerBlock.back().first == currentBlock) {
        return info.txCountsPerBlock.back().second;
    }

    return 0;
}

// ============================================================================
// Reputation Integration (Requirement 26.3)
// ============================================================================

void RateLimiter::UpdateReputation(const uint160& address, uint32_t reputationScore)
{
    LOCK(cs_ratelimit_);

    // Clamp to valid range
    reputationScore = std::min(reputationScore, 100u);

    AddressRateLimitInfo& info = EnsureRateLimitInfo(address);
    info.reputationScore = reputationScore;
    
    // Mark as having activity to prevent premature cleanup
    if (info.lastActivityBlock == 0) {
        info.lastActivityBlock = currentBlockNumber_;
    }
}

uint32_t RateLimiter::GetCachedReputation(const uint160& address) const
{
    LOCK(cs_ratelimit_);

    auto it = addressLimits_.find(address);
    if (it == addressLimits_.end()) {
        return 0;
    }

    return it->second.reputationScore;
}

// ============================================================================
// Adaptive Gas Pricing (Requirements 26.4, 26.5)
// ============================================================================

void RateLimiter::UpdateGasPricing(uint64_t blockNumber, uint64_t gasUsed)
{
    LOCK(cs_ratelimit_);

    // Add to utilization history
    gasPricing_.utilizationHistory.push_back({blockNumber, gasUsed});

    // Keep only recent history
    while (gasPricing_.utilizationHistory.size() > RATE_LIMIT_WINDOW_BLOCKS) {
        gasPricing_.utilizationHistory.pop_front();
    }

    // Update average utilization
    UpdateAverageUtilization();

    // Adjust base fee based on utilization
    AdjustBaseFee();
}

CAmount RateLimiter::GetEffectiveGasPrice() const
{
    LOCK(cs_ratelimit_);
    return gasPricing_.GetEffectiveGasPrice();
}

bool RateLimiter::IsGasPriceAcceptable(CAmount offeredPrice) const
{
    LOCK(cs_ratelimit_);
    return gasPricing_.IsGasPriceAcceptable(offeredPrice);
}

GasPricingInfo RateLimiter::GetGasPricingInfo() const
{
    LOCK(cs_ratelimit_);
    return gasPricing_;
}

// ============================================================================
// Block Gas Limit (Requirement 26.1)
// ============================================================================

void RateLimiter::SetBlockGasLimit(uint64_t limit)
{
    LOCK(cs_ratelimit_);
    gasPricing_.blockGasLimit = limit;
}

uint64_t RateLimiter::GetBlockGasUsed(uint64_t currentBlock) const
{
    LOCK(cs_ratelimit_);

    if (currentBlock == currentBlockNumber_) {
        return currentBlockGasUsed_;
    }

    return 0;
}

bool RateLimiter::HasBlockCapacity(uint64_t gasNeeded, uint64_t currentBlock) const
{
    // Note: This is called with lock already held
    uint64_t gasUsed = (currentBlock == currentBlockNumber_) ? currentBlockGasUsed_ : 0;
    return (gasUsed + gasNeeded) <= gasPricing_.blockGasLimit;
}

// ============================================================================
// Rate Limit Management
// ============================================================================

void RateLimiter::RateLimitAddress(const uint160& address, uint32_t durationBlocks)
{
    LOCK(cs_ratelimit_);

    AddressRateLimitInfo& info = EnsureRateLimitInfo(address);
    info.isRateLimited = true;
    info.rateLimitExpiresBlock = currentBlockNumber_ + durationBlocks;
}

void RateLimiter::RemoveRateLimit(const uint160& address)
{
    LOCK(cs_ratelimit_);

    auto it = addressLimits_.find(address);
    if (it != addressLimits_.end()) {
        it->second.isRateLimited = false;
        it->second.rateLimitExpiresBlock = 0;
    }
}

bool RateLimiter::IsRateLimited(const uint160& address, uint64_t currentBlock) const
{
    LOCK(cs_ratelimit_);

    auto it = addressLimits_.find(address);
    if (it == addressLimits_.end()) {
        return false;
    }

    const auto& info = it->second;
    return info.isRateLimited && currentBlock < info.rateLimitExpiresBlock;
}

std::optional<AddressRateLimitInfo> RateLimiter::GetRateLimitInfo(const uint160& address) const
{
    LOCK(cs_ratelimit_);

    auto it = addressLimits_.find(address);
    if (it == addressLimits_.end()) {
        return std::nullopt;
    }

    return it->second;
}

// ============================================================================
// Block Transition
// ============================================================================

void RateLimiter::OnNewBlock(uint64_t blockNumber)
{
    // Note: Called with lock already held

    // Record previous block's gas usage for pricing
    if (currentBlockNumber_ > 0 && currentBlockGasUsed_ > 0) {
        UpdateGasPricing(currentBlockNumber_, currentBlockGasUsed_);
    }

    // Reset for new block
    currentBlockNumber_ = blockNumber;
    currentBlockGasUsed_ = 0;

    // Reset per-address current block tracking
    for (auto& pair : addressLimits_) {
        if (pair.second.currentBlockNumber != blockNumber) {
            pair.second.gasUsedInCurrentBlock = 0;
            pair.second.currentBlockNumber = blockNumber;
        }
    }

    // Clean up old entries
    CleanupOldEntries(blockNumber);
}

// ============================================================================
// Utility Methods
// ============================================================================

size_t RateLimiter::GetTrackedAddressCount() const
{
    LOCK(cs_ratelimit_);
    return addressLimits_.size();
}

void RateLimiter::Clear()
{
    LOCK(cs_ratelimit_);

    addressLimits_.clear();
    gasPricing_ = GasPricingInfo();
    gasPricing_.blockGasLimit = DEFAULT_BLOCK_GAS_LIMIT;
    currentBlockNumber_ = 0;
    currentBlockGasUsed_ = 0;
}

uint32_t RateLimiter::CalculateRateLimit(uint32_t reputationScore)
{
    // Requirement 26.3: Increase rate limits based on reputation
    // score >70 = 500 tx/block, otherwise 100 tx/block
    
    if (reputationScore >= 90) {
        return HIGH_REPUTATION_TX_LIMIT * 2;  // 1000 tx/block for very high rep
    } else if (reputationScore >= RATE_LIMIT_REPUTATION_THRESHOLD) {
        return HIGH_REPUTATION_TX_LIMIT;  // 500 tx/block
    } else if (reputationScore >= 50) {
        return DEFAULT_NEW_ADDRESS_TX_LIMIT * 2;  // 200 tx/block
    }
    
    return DEFAULT_NEW_ADDRESS_TX_LIMIT;  // 100 tx/block for new/low rep
}

uint32_t RateLimiter::CalculatePriceMultiplier(uint32_t utilizationPercent)
{
    // EIP-1559 style pricing adjustment
    // If utilization > target (50%), increase price
    // If utilization < target, decrease price
    
    if (utilizationPercent <= TARGET_BLOCK_UTILIZATION_PERCENT) {
        // Below target - decrease multiplier (but not below 100)
        uint32_t decrease = (TARGET_BLOCK_UTILIZATION_PERCENT - utilizationPercent) * 
                           GAS_PRICE_ADJUSTMENT_PERCENT / 100;
        return std::max(100u, 100u - decrease);
    } else {
        // Above target - increase multiplier
        uint32_t increase = (utilizationPercent - TARGET_BLOCK_UTILIZATION_PERCENT) * 
                           GAS_PRICE_ADJUSTMENT_PERCENT / 100;
        return std::min(100u * MAX_GAS_PRICE_MULTIPLIER, 100u + increase);
    }
}

// ============================================================================
// Private Methods
// ============================================================================

AddressRateLimitInfo& RateLimiter::EnsureRateLimitInfo(const uint160& address)
{
    // Note: Called with lock already held
    auto it = addressLimits_.find(address);
    if (it == addressLimits_.end()) {
        it = addressLimits_.emplace(address, AddressRateLimitInfo(address)).first;
    }
    return it->second;
}

void RateLimiter::CleanupOldEntries(uint64_t currentBlock)
{
    // Note: Called with lock already held

    uint64_t windowStart = (currentBlock > RATE_LIMIT_WINDOW_BLOCKS) ? 
                           (currentBlock - RATE_LIMIT_WINDOW_BLOCKS) : 0;

    for (auto& pair : addressLimits_) {
        auto& info = pair.second;
        
        // Remove old block entries
        while (!info.txCountsPerBlock.empty() && 
               info.txCountsPerBlock.front().first < windowStart) {
            info.totalTxInWindow -= info.txCountsPerBlock.front().second;
            info.txCountsPerBlock.pop_front();
        }
    }

    // Remove addresses with no recent activity (optional cleanup)
    // Keep addresses that are rate-limited, have recent activity, or have reputation set
    std::vector<uint160> toRemove;
    for (const auto& pair : addressLimits_) {
        const auto& info = pair.second;
        // Don't remove if:
        // - Has transactions in window
        // - Is rate-limited
        // - Has reputation set (non-zero)
        // - Has recent activity
        if (info.txCountsPerBlock.empty() && 
            !info.isRateLimited &&
            info.reputationScore == 0 &&
            (info.lastActivityBlock == 0 || info.lastActivityBlock + RATE_LIMIT_WINDOW_BLOCKS * 10 < currentBlock)) {
            toRemove.push_back(pair.first);
        }
    }

    for (const auto& addr : toRemove) {
        addressLimits_.erase(addr);
    }
}

void RateLimiter::UpdateAverageUtilization()
{
    // Note: Called with lock already held

    if (gasPricing_.utilizationHistory.empty()) {
        gasPricing_.averageUtilization = 0;
        return;
    }

    uint64_t totalGas = 0;
    for (const auto& entry : gasPricing_.utilizationHistory) {
        totalGas += entry.second;
    }

    uint64_t avgGas = totalGas / gasPricing_.utilizationHistory.size();
    
    // Calculate utilization as percentage of block gas limit
    if (gasPricing_.blockGasLimit > 0) {
        gasPricing_.averageUtilization = static_cast<uint32_t>(
            (avgGas * 100) / gasPricing_.blockGasLimit
        );
    } else {
        gasPricing_.averageUtilization = 0;
    }
}

void RateLimiter::AdjustBaseFee()
{
    // Note: Called with lock already held

    // Calculate new price multiplier based on utilization
    gasPricing_.priceMultiplier = CalculatePriceMultiplier(gasPricing_.averageUtilization);

    // Optionally adjust base fee itself (EIP-1559 style)
    // For now, we keep base fee constant and only adjust multiplier
    // This provides simpler, more predictable pricing
}

} // namespace l2
