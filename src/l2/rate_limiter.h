// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_RATE_LIMITER_H
#define CASCOIN_L2_RATE_LIMITER_H

/**
 * @file rate_limiter.h
 * @brief Rate Limiting and Spam Protection for Cascoin Layer 2
 * 
 * This file implements rate limiting and spam protection mechanisms for L2,
 * including per-address rate limiting, reputation-based limits, and adaptive
 * gas pricing to protect the network from DoS attacks and spam.
 * 
 * Key features:
 * - Per-address transaction rate limiting
 * - Reputation-based rate limit multipliers
 * - Adaptive gas pricing during congestion
 * - Per-block gas limit enforcement
 * - Minimum gas price requirements
 * 
 * Requirements: 26.1, 26.2, 26.3, 26.4, 26.5, 26.6
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <deque>
#include <chrono>
#include <optional>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Default per-block gas limit (Requirement 26.1) */
static constexpr uint64_t DEFAULT_BLOCK_GAS_LIMIT = 30000000;  // 30M gas

/** Default max transactions per block for new addresses (Requirement 26.2) */
static constexpr uint32_t DEFAULT_NEW_ADDRESS_TX_LIMIT = 100;

/** Max transactions per block for high-reputation addresses (score >70) (Requirement 26.3) */
static constexpr uint32_t HIGH_REPUTATION_TX_LIMIT = 500;

/** Reputation threshold for increased rate limits (Requirement 26.3) */
static constexpr uint32_t RATE_LIMIT_REPUTATION_THRESHOLD = 70;

/** Minimum gas price to prevent zero-fee spam (Requirement 26.6) */
static constexpr CAmount MIN_GAS_PRICE = 1;  // 1 satoshi per gas unit

/** Base gas price for EIP-1559 style pricing */
static constexpr CAmount BASE_GAS_PRICE = 10;  // 10 satoshis per gas unit

/** Maximum gas price multiplier during congestion */
static constexpr uint32_t MAX_GAS_PRICE_MULTIPLIER = 10;

/** Target block utilization for adaptive pricing (50%) */
static constexpr uint32_t TARGET_BLOCK_UTILIZATION_PERCENT = 50;

/** Gas price adjustment factor per block (12.5% like EIP-1559) */
static constexpr uint32_t GAS_PRICE_ADJUSTMENT_PERCENT = 12;

/** Number of blocks to track for rate limiting window */
static constexpr uint32_t RATE_LIMIT_WINDOW_BLOCKS = 10;

/** Cooldown period after rate limit exceeded (in blocks) */
static constexpr uint32_t RATE_LIMIT_COOLDOWN_BLOCKS = 5;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Rate limit information for a single address
 * 
 * Tracks transaction counts and timing for rate limiting decisions.
 */
struct AddressRateLimitInfo {
    /** Address being tracked */
    uint160 address;
    
    /** Transaction counts per block (recent blocks) */
    std::deque<std::pair<uint64_t, uint32_t>> txCountsPerBlock;
    
    /** Total transactions in current window */
    uint32_t totalTxInWindow;
    
    /** Last block number with activity */
    uint64_t lastActivityBlock;
    
    /** Reputation score (cached) */
    uint32_t reputationScore;
    
    /** Whether address is currently rate-limited */
    bool isRateLimited;
    
    /** Block number when rate limit expires */
    uint64_t rateLimitExpiresBlock;
    
    /** Total gas used in current block */
    uint64_t gasUsedInCurrentBlock;
    
    /** Current block number being tracked */
    uint64_t currentBlockNumber;

    /** Default constructor */
    AddressRateLimitInfo()
        : totalTxInWindow(0)
        , lastActivityBlock(0)
        , reputationScore(0)
        , isRateLimited(false)
        , rateLimitExpiresBlock(0)
        , gasUsedInCurrentBlock(0)
        , currentBlockNumber(0)
    {}

    /** Constructor with address */
    explicit AddressRateLimitInfo(const uint160& addr)
        : address(addr)
        , totalTxInWindow(0)
        , lastActivityBlock(0)
        , reputationScore(0)
        , isRateLimited(false)
        , rateLimitExpiresBlock(0)
        , gasUsedInCurrentBlock(0)
        , currentBlockNumber(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(totalTxInWindow);
        READWRITE(lastActivityBlock);
        READWRITE(reputationScore);
        READWRITE(isRateLimited);
        READWRITE(rateLimitExpiresBlock);
        READWRITE(gasUsedInCurrentBlock);
        READWRITE(currentBlockNumber);
    }

    /**
     * @brief Get the rate limit for this address based on reputation
     * @return Maximum transactions per block
     */
    uint32_t GetRateLimit() const {
        if (reputationScore >= RATE_LIMIT_REPUTATION_THRESHOLD) {
            return HIGH_REPUTATION_TX_LIMIT;
        }
        return DEFAULT_NEW_ADDRESS_TX_LIMIT;
    }

    /**
     * @brief Check if address can submit more transactions in current block
     * @param currentBlock Current block number
     * @return true if within rate limit
     */
    bool CanSubmitTransaction(uint64_t currentBlock) const {
        if (isRateLimited && currentBlock < rateLimitExpiresBlock) {
            return false;
        }
        
        // Get current block tx count
        uint32_t currentBlockTxCount = 0;
        if (!txCountsPerBlock.empty() && txCountsPerBlock.back().first == currentBlock) {
            currentBlockTxCount = txCountsPerBlock.back().second;
        }
        
        return currentBlockTxCount < GetRateLimit();
    }
};

/**
 * @brief Gas pricing information for adaptive pricing
 * 
 * Tracks block utilization and calculates dynamic gas prices.
 */
struct GasPricingInfo {
    /** Current base gas price */
    CAmount baseFee;
    
    /** Block utilization history (block number -> gas used) */
    std::deque<std::pair<uint64_t, uint64_t>> utilizationHistory;
    
    /** Current block gas limit */
    uint64_t blockGasLimit;
    
    /** Average utilization over recent blocks (0-100%) */
    uint32_t averageUtilization;
    
    /** Current gas price multiplier (100 = 1x, 200 = 2x) */
    uint32_t priceMultiplier;

    /** Default constructor */
    GasPricingInfo()
        : baseFee(BASE_GAS_PRICE)
        , blockGasLimit(DEFAULT_BLOCK_GAS_LIMIT)
        , averageUtilization(0)
        , priceMultiplier(100)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(baseFee);
        READWRITE(blockGasLimit);
        READWRITE(averageUtilization);
        READWRITE(priceMultiplier);
    }

    /**
     * @brief Get the current effective gas price
     * @return Gas price in satoshis per gas unit
     */
    CAmount GetEffectiveGasPrice() const {
        CAmount effectivePrice = (baseFee * priceMultiplier) / 100;
        return std::max(effectivePrice, MIN_GAS_PRICE);
    }

    /**
     * @brief Check if a gas price is acceptable
     * @param offeredPrice Gas price offered by transaction
     * @return true if price meets minimum requirements
     */
    bool IsGasPriceAcceptable(CAmount offeredPrice) const {
        return offeredPrice >= GetEffectiveGasPrice();
    }
};

/**
 * @brief Result of rate limit check
 */
struct RateLimitCheckResult {
    /** Whether the transaction is allowed */
    bool allowed;
    
    /** Reason if not allowed */
    std::string reason;
    
    /** Suggested wait time in blocks */
    uint32_t suggestedWaitBlocks;
    
    /** Current rate limit for the address */
    uint32_t currentRateLimit;
    
    /** Transactions used in current block */
    uint32_t txUsedInBlock;
    
    /** Effective gas price required */
    CAmount requiredGasPrice;

    RateLimitCheckResult()
        : allowed(true)
        , suggestedWaitBlocks(0)
        , currentRateLimit(0)
        , txUsedInBlock(0)
        , requiredGasPrice(0)
    {}

    static RateLimitCheckResult Allowed(uint32_t rateLimit, uint32_t txUsed, CAmount gasPrice) {
        RateLimitCheckResult result;
        result.allowed = true;
        result.currentRateLimit = rateLimit;
        result.txUsedInBlock = txUsed;
        result.requiredGasPrice = gasPrice;
        return result;
    }

    static RateLimitCheckResult Denied(const std::string& reason, uint32_t waitBlocks = 0) {
        RateLimitCheckResult result;
        result.allowed = false;
        result.reason = reason;
        result.suggestedWaitBlocks = waitBlocks;
        return result;
    }
};

// ============================================================================
// Rate Limiter Class
// ============================================================================

/**
 * @brief L2 Rate Limiter
 * 
 * Manages rate limiting and spam protection for L2 transactions.
 * Implements per-address rate limiting with reputation-based multipliers
 * and adaptive gas pricing during network congestion.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 26.1, 26.2, 26.3, 26.4, 26.5, 26.6
 */
class RateLimiter {
public:
    /**
     * @brief Construct a new Rate Limiter
     * @param blockGasLimit Per-block gas limit (default: 30M)
     */
    explicit RateLimiter(uint64_t blockGasLimit = DEFAULT_BLOCK_GAS_LIMIT);

    // =========================================================================
    // Rate Limit Checking (Requirements 26.2, 26.3)
    // =========================================================================

    /**
     * @brief Check if a transaction from an address is allowed
     * @param address Sender address
     * @param gasPrice Offered gas price
     * @param gasLimit Gas limit for the transaction
     * @param currentBlock Current block number
     * @return RateLimitCheckResult with decision and details
     * 
     * Requirements 26.2, 26.3: Per-address rate limiting with reputation
     */
    RateLimitCheckResult CheckRateLimit(
        const uint160& address,
        CAmount gasPrice,
        uint64_t gasLimit,
        uint64_t currentBlock
    );

    /**
     * @brief Record a transaction from an address
     * @param address Sender address
     * @param gasUsed Gas consumed by the transaction
     * @param currentBlock Current block number
     * 
     * Call this after a transaction is included in a block.
     */
    void RecordTransaction(
        const uint160& address,
        uint64_t gasUsed,
        uint64_t currentBlock
    );

    /**
     * @brief Get the rate limit for an address
     * @param address Address to query
     * @return Maximum transactions per block
     */
    uint32_t GetRateLimitForAddress(const uint160& address) const;

    /**
     * @brief Get transactions used by address in current block
     * @param address Address to query
     * @param currentBlock Current block number
     * @return Number of transactions in current block
     */
    uint32_t GetTxCountInBlock(const uint160& address, uint64_t currentBlock) const;

    // =========================================================================
    // Reputation Integration (Requirement 26.3)
    // =========================================================================

    /**
     * @brief Update reputation score for an address
     * @param address Address to update
     * @param reputationScore New reputation score (0-100)
     * 
     * Requirement 26.3: Increase rate limits based on reputation
     */
    void UpdateReputation(const uint160& address, uint32_t reputationScore);

    /**
     * @brief Get cached reputation score for an address
     * @param address Address to query
     * @return Reputation score (0 if not cached)
     */
    uint32_t GetCachedReputation(const uint160& address) const;

    // =========================================================================
    // Adaptive Gas Pricing (Requirements 26.4, 26.5)
    // =========================================================================

    /**
     * @brief Update gas pricing based on block utilization
     * @param blockNumber Block number
     * @param gasUsed Total gas used in the block
     * 
     * Requirements 26.4, 26.5: Adaptive gas pricing during congestion
     */
    void UpdateGasPricing(uint64_t blockNumber, uint64_t gasUsed);

    /**
     * @brief Get the current effective gas price
     * @return Gas price in satoshis per gas unit
     */
    CAmount GetEffectiveGasPrice() const;

    /**
     * @brief Get the minimum acceptable gas price
     * @return Minimum gas price
     * 
     * Requirement 26.6: Minimum gas price to prevent zero-fee spam
     */
    CAmount GetMinGasPrice() const { return MIN_GAS_PRICE; }

    /**
     * @brief Check if a gas price is acceptable
     * @param offeredPrice Gas price offered
     * @return true if acceptable
     */
    bool IsGasPriceAcceptable(CAmount offeredPrice) const;

    /**
     * @brief Get current gas pricing info
     * @return GasPricingInfo structure
     */
    GasPricingInfo GetGasPricingInfo() const;

    // =========================================================================
    // Block Gas Limit (Requirement 26.1)
    // =========================================================================

    /**
     * @brief Get the per-block gas limit
     * @return Block gas limit
     * 
     * Requirement 26.1: Enforce per-block gas limit
     */
    uint64_t GetBlockGasLimit() const { return gasPricing_.blockGasLimit; }

    /**
     * @brief Set the per-block gas limit
     * @param limit New gas limit
     */
    void SetBlockGasLimit(uint64_t limit);

    /**
     * @brief Get gas used in current block
     * @param currentBlock Current block number
     * @return Total gas used
     */
    uint64_t GetBlockGasUsed(uint64_t currentBlock) const;

    /**
     * @brief Check if block has capacity for more gas
     * @param gasNeeded Gas needed for transaction
     * @param currentBlock Current block number
     * @return true if block has capacity
     */
    bool HasBlockCapacity(uint64_t gasNeeded, uint64_t currentBlock) const;

    // =========================================================================
    // Rate Limit Management
    // =========================================================================

    /**
     * @brief Manually rate-limit an address
     * @param address Address to limit
     * @param durationBlocks Duration in blocks
     */
    void RateLimitAddress(const uint160& address, uint32_t durationBlocks);

    /**
     * @brief Remove rate limit from an address
     * @param address Address to unlimit
     */
    void RemoveRateLimit(const uint160& address);

    /**
     * @brief Check if an address is currently rate-limited
     * @param address Address to check
     * @param currentBlock Current block number
     * @return true if rate-limited
     */
    bool IsRateLimited(const uint160& address, uint64_t currentBlock) const;

    /**
     * @brief Get rate limit info for an address
     * @param address Address to query
     * @return Rate limit info (empty if not tracked)
     */
    std::optional<AddressRateLimitInfo> GetRateLimitInfo(const uint160& address) const;

    // =========================================================================
    // Block Transition
    // =========================================================================

    /**
     * @brief Called when a new block starts
     * @param blockNumber New block number
     * 
     * Cleans up old data and prepares for new block.
     */
    void OnNewBlock(uint64_t blockNumber);

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Get number of tracked addresses
     * @return Address count
     */
    size_t GetTrackedAddressCount() const;

    /**
     * @brief Clear all rate limit data (for testing)
     */
    void Clear();

    /**
     * @brief Calculate rate limit based on reputation score
     * @param reputationScore Reputation score (0-100)
     * @return Rate limit (transactions per block)
     */
    static uint32_t CalculateRateLimit(uint32_t reputationScore);

    /**
     * @brief Calculate gas price multiplier based on utilization
     * @param utilizationPercent Block utilization (0-100)
     * @return Price multiplier (100 = 1x)
     */
    static uint32_t CalculatePriceMultiplier(uint32_t utilizationPercent);

private:
    /** Per-address rate limit tracking */
    std::map<uint160, AddressRateLimitInfo> addressLimits_;

    /** Gas pricing information */
    GasPricingInfo gasPricing_;

    /** Current block number */
    uint64_t currentBlockNumber_;

    /** Total gas used in current block */
    uint64_t currentBlockGasUsed_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_ratelimit_;

    /**
     * @brief Ensure rate limit info exists for address
     * @param address Address to ensure
     * @return Reference to rate limit info
     */
    AddressRateLimitInfo& EnsureRateLimitInfo(const uint160& address);

    /**
     * @brief Clean up old entries from tracking window
     * @param currentBlock Current block number
     */
    void CleanupOldEntries(uint64_t currentBlock);

    /**
     * @brief Update average utilization from history
     */
    void UpdateAverageUtilization();

    /**
     * @brief Adjust base fee based on utilization
     */
    void AdjustBaseFee();
};

} // namespace l2

#endif // CASCOIN_L2_RATE_LIMITER_H
