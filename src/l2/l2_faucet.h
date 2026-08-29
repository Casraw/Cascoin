// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_FAUCET_H
#define CASCOIN_L2_FAUCET_H

/**
 * @file l2_faucet.h
 * @brief L2 Testnet Faucet for distributing test tokens
 * 
 * This file implements the L2Faucet class that provides a mechanism for
 * distributing test tokens on testnet/regtest networks. The faucet is
 * disabled on mainnet for security.
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <l2/l2_token_manager.h>
#include <l2/state_manager.h>
#include <uint256.h>
#include <amount.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Maximum tokens per faucet request (100 tokens) - Requirement 5.2 */
static constexpr CAmount MAX_FAUCET_AMOUNT = 100 * COIN;

/** Cooldown period between requests in seconds (1 hour) - Requirement 5.3 */
static constexpr uint64_t COOLDOWN_SECONDS = 3600;

// ============================================================================
// FaucetDistribution
// ============================================================================

/**
 * @brief Record of a faucet distribution event
 * 
 * Stores information about each faucet distribution for audit purposes.
 * 
 * Requirement 5.6: Log all distributions for audit purposes
 */
struct FaucetDistribution {
    /** Recipient address */
    uint160 recipient;
    
    /** Amount distributed */
    CAmount amount;
    
    /** Timestamp of distribution */
    uint64_t timestamp;
    
    /** L2 chain ID */
    uint64_t chainId;
    
    /** Whether this is marked as test tokens */
    bool isTestTokens;
    
    /** Optional note/reason for distribution */
    std::string note;

    /** Default constructor */
    FaucetDistribution()
        : amount(0)
        , timestamp(0)
        , chainId(0)
        , isTestTokens(true)
    {}

    /** Full constructor */
    FaucetDistribution(const uint160& addr, CAmount amt, uint64_t ts, 
                       uint64_t chain, const std::string& n = "")
        : recipient(addr)
        , amount(amt)
        , timestamp(ts)
        , chainId(chain)
        , isTestTokens(true)
        , note(n)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(recipient);
        READWRITE(amount);
        READWRITE(timestamp);
        READWRITE(chainId);
        READWRITE(isTestTokens);
        READWRITE(note);
    }

    bool operator==(const FaucetDistribution& other) const {
        return recipient == other.recipient &&
               amount == other.amount &&
               timestamp == other.timestamp &&
               chainId == other.chainId &&
               isTestTokens == other.isTestTokens &&
               note == other.note;
    }
};

// ============================================================================
// FaucetResult
// ============================================================================

/**
 * @brief Result of a faucet request
 */
struct FaucetResult {
    /** Whether the request succeeded */
    bool success;
    
    /** Error message if failed */
    std::string error;
    
    /** Amount distributed (if successful) */
    CAmount amount;
    
    /** Transaction hash (if recorded) */
    uint256 txHash;
    
    /** Cooldown remaining in seconds (if rate limited) */
    uint64_t cooldownRemaining;

    FaucetResult() : success(false), amount(0), cooldownRemaining(0) {}
    
    static FaucetResult Success(CAmount amt, const uint256& hash = uint256()) {
        FaucetResult result;
        result.success = true;
        result.amount = amt;
        result.txHash = hash;
        return result;
    }
    
    static FaucetResult Failure(const std::string& err, uint64_t cooldown = 0) {
        FaucetResult result;
        result.success = false;
        result.error = err;
        result.cooldownRemaining = cooldown;
        return result;
    }
};

// ============================================================================
// L2Faucet
// ============================================================================

/**
 * @brief L2 Testnet Faucet
 * 
 * Provides a mechanism for distributing test tokens on testnet/regtest
 * networks. The faucet enforces rate limiting and maximum amounts to
 * prevent abuse.
 * 
 * Key features:
 * - Only enabled on testnet/regtest (disabled on mainnet)
 * - Maximum 100 tokens per request
 * - 1 hour cooldown between requests per address
 * - All distributions logged for audit
 * - Tokens marked as "test tokens"
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */
class L2Faucet {
public:
    /**
     * @brief Construct a new L2 Faucet
     * @param tokenManager Reference to the L2 token manager
     * 
     * Requirement 5.1: Provide faucet RPC command on testnet/regtest
     */
    explicit L2Faucet(L2TokenManager& tokenManager);

    /**
     * @brief Check if faucet is enabled for current network
     * @return true if on testnet or regtest, false on mainnet
     * 
     * Requirement 5.1: Faucet available on regtest/testnet
     * Requirement 5.5: Faucet disabled on mainnet
     */
    static bool IsEnabled();

    /**
     * @brief Request tokens from the faucet
     * @param recipient Address to receive tokens
     * @param requestedAmount Amount requested (capped at MAX_FAUCET_AMOUNT)
     * @param stateManager State manager to update
     * @return Faucet result with success/failure info
     * 
     * Requirement 5.2: Maximum 100 tokens per request
     * Requirement 5.3: 1 hour cooldown per address
     * Requirement 5.4: Mark distributed tokens as "test tokens"
     */
    FaucetResult RequestTokens(
        const uint160& recipient,
        CAmount requestedAmount,
        L2StateManager& stateManager);

    /**
     * @brief Check if an address can request tokens
     * @param address The address to check
     * @param currentTime Current timestamp (for testing, 0 = use system time)
     * @return true if address can request (not in cooldown)
     * 
     * Requirement 5.3: 1 hour cooldown per address
     */
    bool CanRequest(const uint160& address, uint64_t currentTime = 0) const;

    /**
     * @brief Get remaining cooldown time for an address
     * @param address The address to check
     * @param currentTime Current timestamp (for testing, 0 = use system time)
     * @return Seconds remaining in cooldown (0 if can request)
     * 
     * Requirement 5.3: 1 hour cooldown per address
     */
    uint64_t GetCooldownRemaining(const uint160& address, uint64_t currentTime = 0) const;

    /**
     * @brief Get the distribution log
     * @return Vector of all faucet distributions
     * 
     * Requirement 5.6: Log all distributions for audit purposes
     */
    std::vector<FaucetDistribution> GetDistributionLog() const;

    /**
     * @brief Get distribution log for a specific address
     * @param address The address to query
     * @return Vector of distributions to that address
     */
    std::vector<FaucetDistribution> GetDistributionLog(const uint160& address) const;

    /**
     * @brief Get total tokens distributed by faucet
     * @return Total amount distributed
     */
    CAmount GetTotalDistributed() const;

    /**
     * @brief Get number of unique addresses that received tokens
     * @return Count of unique recipients
     */
    size_t GetUniqueRecipientCount() const;

    /**
     * @brief Get the token manager reference
     * @return Reference to token manager
     */
    L2TokenManager& GetTokenManager() { return tokenManager_; }

    /**
     * @brief Get the token manager reference (const)
     * @return Const reference to token manager
     */
    const L2TokenManager& GetTokenManager() const { return tokenManager_; }

    /**
     * @brief Clear all faucet state (for testing)
     */
    void Clear();

private:
    /** Reference to the L2 token manager */
    L2TokenManager& tokenManager_;

    /** Last request timestamp per address */
    std::map<uint160, uint64_t> lastRequest_;

    /** Distribution log for audit */
    std::vector<FaucetDistribution> distributionLog_;

    /** Total tokens distributed */
    CAmount totalDistributed_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_faucet_;

    /**
     * @brief Get current timestamp
     * @param providedTime Provided time (0 = use system time)
     * @return Current timestamp in seconds
     */
    static uint64_t GetCurrentTimeImpl(uint64_t providedTime);

    /**
     * @brief Record a distribution event
     * @param recipient Recipient address
     * @param amount Amount distributed
     * @param timestamp Distribution timestamp
     */
    void RecordDistribution(const uint160& recipient, CAmount amount, uint64_t timestamp);
};

} // namespace l2

#endif // CASCOIN_L2_FAUCET_H
