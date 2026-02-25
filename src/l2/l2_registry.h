// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_REGISTRY_H
#define CASCOIN_L2_REGISTRY_H

/**
 * @file l2_registry.h
 * @brief L2 Chain Registry for Cascoin Layer 2
 * 
 * This file implements the L2 Registry which manages registration and
 * tracking of L2 chains on L1. It provides:
 * - L2 chain registration with validation
 * - Chain info queries
 * - Deployment parameter validation
 * - Unique chain ID generation
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5
 */

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace l2 {

/** Minimum stake required to deploy an L2 chain (in satoshis) */
static constexpr CAmount MIN_DEPLOYER_STAKE = 1000 * COIN;  // 1000 CAS

/** Maximum name length for L2 chains */
static constexpr size_t MAX_CHAIN_NAME_LENGTH = 64;

/** Minimum block time in milliseconds */
static constexpr uint32_t MIN_BLOCK_TIME_MS = 100;

/** Maximum block time in milliseconds */
static constexpr uint32_t MAX_BLOCK_TIME_MS = 60000;  // 1 minute

/** Minimum gas limit per block */
static constexpr uint64_t MIN_GAS_LIMIT = 1000000;  // 1M gas

/** Maximum gas limit per block */
static constexpr uint64_t MAX_GAS_LIMIT = 100000000;  // 100M gas

/** Minimum challenge period in seconds */
static constexpr uint64_t MIN_CHALLENGE_PERIOD = 3600;  // 1 hour

/** Maximum challenge period in seconds */
static constexpr uint64_t MAX_CHALLENGE_PERIOD = 2592000;  // 30 days

/** Minimum sequencer stake in satoshis */
static constexpr CAmount MIN_SEQUENCER_STAKE = 10 * COIN;  // 10 CAS

/** Minimum sequencer HAT score */
static constexpr uint32_t MIN_SEQUENCER_HAT_SCORE = 50;

/** Maximum sequencer HAT score */
static constexpr uint32_t MAX_SEQUENCER_HAT_SCORE = 100;

/** Chain ID range start for generated IDs */
static constexpr uint64_t CHAIN_ID_RANGE_START = 1000;

/** Chain ID range end for generated IDs */
static constexpr uint64_t CHAIN_ID_RANGE_END = 999999999;

/**
 * L2 Chain status enumeration
 */
enum class L2ChainStatus : uint8_t {
    BOOTSTRAPPING = 0,  // Initial setup phase
    ACTIVE = 1,         // Normal operation
    PAUSED = 2,         // Temporarily paused
    EMERGENCY = 3,      // Emergency mode (withdrawals only)
    DEPRECATED = 4      // Being phased out
};

/**
 * Convert L2ChainStatus to string
 */
std::string L2ChainStatusToString(L2ChainStatus status);

/**
 * L2 Chain deployment parameters
 */
struct L2DeploymentParams {
    /** Target block time in milliseconds */
    uint32_t blockTimeMs;
    
    /** Maximum gas per block */
    uint64_t gasLimit;
    
    /** Challenge period for fraud proofs in seconds */
    uint64_t challengePeriod;
    
    /** Minimum stake required for sequencers */
    CAmount minSequencerStake;
    
    /** Minimum HAT score required for sequencers */
    uint32_t minSequencerHatScore;
    
    /** L2 blocks between L1 state root submissions */
    uint32_t l1AnchorInterval;
    
    /** Constructor with defaults */
    L2DeploymentParams()
        : blockTimeMs(500)
        , gasLimit(30000000)
        , challengePeriod(604800)  // 7 days
        , minSequencerStake(100 * COIN)
        , minSequencerHatScore(70)
        , l1AnchorInterval(100)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockTimeMs);
        READWRITE(gasLimit);
        READWRITE(challengePeriod);
        READWRITE(minSequencerStake);
        READWRITE(minSequencerHatScore);
        READWRITE(l1AnchorInterval);
    }
};

/**
 * L2 Chain information stored in registry
 */
struct L2ChainInfo {
    /** Unique chain identifier */
    uint64_t chainId;
    
    /** Human-readable chain name */
    std::string name;
    
    /** Bridge contract address on L1 */
    uint160 bridgeContract;
    
    /** Address of the deployer */
    uint160 deployer;
    
    /** L1 block number when deployed */
    uint64_t deploymentBlock;
    
    /** Deployment timestamp */
    uint64_t deploymentTime;
    
    /** Deployment parameters */
    L2DeploymentParams params;
    
    /** Current chain status */
    L2ChainStatus status;
    
    /** Latest state root */
    uint256 latestStateRoot;
    
    /** Latest L2 block number */
    uint64_t latestL2Block;
    
    /** Latest L1 anchor block */
    uint64_t latestL1Anchor;
    
    /** Deployer's stake amount */
    CAmount deployerStake;
    
    /** Genesis block hash */
    uint256 genesisHash;
    
    /** Total value locked (TVL) in satoshis */
    CAmount totalValueLocked;
    
    /** Number of registered sequencers */
    uint32_t sequencerCount;
    
    /** Constructor */
    L2ChainInfo()
        : chainId(0)
        , name("")
        , deploymentBlock(0)
        , deploymentTime(0)
        , status(L2ChainStatus::BOOTSTRAPPING)
        , latestL2Block(0)
        , latestL1Anchor(0)
        , deployerStake(0)
        , totalValueLocked(0)
        , sequencerCount(0)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainId);
        READWRITE(name);
        READWRITE(bridgeContract);
        READWRITE(deployer);
        READWRITE(deploymentBlock);
        READWRITE(deploymentTime);
        READWRITE(params);
        uint8_t statusByte = static_cast<uint8_t>(status);
        READWRITE(statusByte);
        if (ser_action.ForRead()) {
            status = static_cast<L2ChainStatus>(statusByte);
        }
        READWRITE(latestStateRoot);
        READWRITE(latestL2Block);
        READWRITE(latestL1Anchor);
        READWRITE(deployerStake);
        READWRITE(genesisHash);
        READWRITE(totalValueLocked);
        READWRITE(sequencerCount);
    }
    
    /** Check if chain is active */
    bool IsActive() const { return status == L2ChainStatus::ACTIVE; }
    
    /** Check if chain accepts deposits */
    bool AcceptsDeposits() const {
        return status == L2ChainStatus::ACTIVE || 
               status == L2ChainStatus::BOOTSTRAPPING;
    }
    
    /** Check if chain allows withdrawals */
    bool AllowsWithdrawals() const {
        return status != L2ChainStatus::DEPRECATED;
    }
};

/**
 * L2 Chain registration request
 */
struct L2RegistrationRequest {
    /** Requested chain name */
    std::string name;
    
    /** Deployer address */
    uint160 deployer;
    
    /** Deployer's stake amount */
    CAmount stake;
    
    /** Deployer's HAT score */
    uint32_t deployerHatScore;
    
    /** Deployment parameters */
    L2DeploymentParams params;
    
    /** Request timestamp */
    uint64_t timestamp;
    
    /** Signature from deployer */
    std::vector<uint8_t> signature;
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(name);
        READWRITE(deployer);
        READWRITE(stake);
        READWRITE(deployerHatScore);
        READWRITE(params);
        READWRITE(timestamp);
        READWRITE(signature);
    }
};

/**
 * Validation result for deployment parameters
 */
struct ValidationResult {
    bool isValid;
    std::string errorMessage;
    
    ValidationResult() : isValid(true), errorMessage("") {}
    ValidationResult(bool valid, const std::string& msg = "") 
        : isValid(valid), errorMessage(msg) {}
    
    static ValidationResult Success() { return ValidationResult(true); }
    static ValidationResult Failure(const std::string& msg) { 
        return ValidationResult(false, msg); 
    }
};

/**
 * L2 Registry - manages L2 chain registration on L1
 * 
 * This class provides the core functionality for:
 * - Registering new L2 chains
 * - Querying chain information
 * - Validating deployment parameters
 * - Generating unique chain IDs
 * - Updating chain state
 */
class L2Registry {
public:
    /**
     * Constructor
     */
    L2Registry();
    
    /**
     * Destructor
     */
    ~L2Registry() = default;
    
    // === Registration Functions ===
    
    /**
     * Register a new L2 chain
     * @param request Registration request with all parameters
     * @param l1BlockNumber Current L1 block number
     * @return Chain ID if successful, 0 if failed
     */
    uint64_t RegisterL2Chain(const L2RegistrationRequest& request, 
                             uint64_t l1BlockNumber);
    
    /**
     * Register a new L2 chain with explicit parameters
     * @param name Chain name
     * @param deployer Deployer address
     * @param stake Deployer's stake
     * @param params Deployment parameters
     * @param l1BlockNumber Current L1 block number
     * @return Chain ID if successful, 0 if failed
     */
    uint64_t RegisterL2Chain(const std::string& name,
                             const uint160& deployer,
                             CAmount stake,
                             const L2DeploymentParams& params,
                             uint64_t l1BlockNumber);
    
    // === Query Functions ===
    
    /**
     * Get L2 chain information by chain ID
     * @param chainId Chain identifier
     * @return Chain info if found, nullopt otherwise
     */
    std::optional<L2ChainInfo> GetL2ChainInfo(uint64_t chainId) const;
    
    /**
     * Get L2 chain information by name
     * @param name Chain name
     * @return Chain info if found, nullopt otherwise
     */
    std::optional<L2ChainInfo> GetL2ChainInfoByName(const std::string& name) const;
    
    /**
     * Get all registered L2 chains
     * @return Vector of all chain infos
     */
    std::vector<L2ChainInfo> GetAllChains() const;
    
    /**
     * Get all active L2 chains
     * @return Vector of active chain infos
     */
    std::vector<L2ChainInfo> GetActiveChains() const;
    
    /**
     * Check if a chain ID exists
     * @param chainId Chain identifier
     * @return true if chain exists
     */
    bool ChainExists(uint64_t chainId) const;
    
    /**
     * Check if a chain name is taken
     * @param name Chain name
     * @return true if name is already used
     */
    bool ChainNameExists(const std::string& name) const;
    
    /**
     * Get total number of registered chains
     * @return Number of chains
     */
    size_t GetChainCount() const;
    
    // === Validation Functions ===
    
    /**
     * Validate deployment parameters
     * @param params Parameters to validate
     * @return Validation result
     */
    static ValidationResult ValidateDeploymentParams(const L2DeploymentParams& params);
    
    /**
     * Validate deployer stake
     * @param stake Stake amount
     * @return Validation result
     */
    static ValidationResult ValidateDeployerStake(CAmount stake);
    
    /**
     * Validate chain name
     * @param name Chain name
     * @return Validation result
     */
    static ValidationResult ValidateChainName(const std::string& name);
    
    /**
     * Validate full registration request
     * @param request Registration request
     * @return Validation result
     */
    ValidationResult ValidateRegistrationRequest(const L2RegistrationRequest& request) const;
    
    // === Chain ID Generation ===
    
    /**
     * Generate a unique chain ID
     * @param name Chain name (used as seed)
     * @param deployer Deployer address
     * @param timestamp Deployment timestamp
     * @return Unique chain ID
     */
    uint64_t GenerateChainId(const std::string& name,
                             const uint160& deployer,
                             uint64_t timestamp);
    
    // === State Update Functions ===
    
    /**
     * Update chain state root
     * @param chainId Chain identifier
     * @param stateRoot New state root
     * @param l2BlockNumber L2 block number
     * @param l1AnchorBlock L1 anchor block
     * @return true if update successful
     */
    bool UpdateChainState(uint64_t chainId,
                          const uint256& stateRoot,
                          uint64_t l2BlockNumber,
                          uint64_t l1AnchorBlock);
    
    /**
     * Update chain status
     * @param chainId Chain identifier
     * @param status New status
     * @return true if update successful
     */
    bool UpdateChainStatus(uint64_t chainId, L2ChainStatus status);
    
    /**
     * Update chain TVL
     * @param chainId Chain identifier
     * @param tvl New total value locked
     * @return true if update successful
     */
    bool UpdateChainTVL(uint64_t chainId, CAmount tvl);
    
    /**
     * Update sequencer count
     * @param chainId Chain identifier
     * @param count New sequencer count
     * @return true if update successful
     */
    bool UpdateSequencerCount(uint64_t chainId, uint32_t count);
    
    /**
     * Set genesis hash for a chain
     * @param chainId Chain identifier
     * @param genesisHash Genesis block hash
     * @return true if update successful
     */
    bool SetGenesisHash(uint64_t chainId, const uint256& genesisHash);
    
    // === Bridge Contract Management ===
    
    /**
     * Set bridge contract address for a chain
     * @param chainId Chain identifier
     * @param bridgeContract Bridge contract address
     * @return true if update successful
     */
    bool SetBridgeContract(uint64_t chainId, const uint160& bridgeContract);
    
    /**
     * Get bridge contract address for a chain
     * @param chainId Chain identifier
     * @return Bridge contract address if found
     */
    std::optional<uint160> GetBridgeContract(uint64_t chainId) const;
    
private:
    /** Registry of all L2 chains (chainId -> info) */
    std::map<uint64_t, L2ChainInfo> m_chains;
    
    /** Name to chain ID mapping for fast lookup */
    std::map<std::string, uint64_t> m_nameToChainId;
    
    /** Set of used chain IDs */
    std::set<uint64_t> m_usedChainIds;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_registry;
    
    /** Counter for chain ID generation */
    uint64_t m_chainIdCounter;
    
    /**
     * Internal helper to add chain to registry
     */
    void AddChainInternal(const L2ChainInfo& info);
};

// === Global Registry Access ===

/**
 * Get the global L2 registry instance
 * @return Reference to global registry
 */
L2Registry& GetL2Registry();

/**
 * Initialize the global L2 registry
 */
void InitL2Registry();

/**
 * Check if L2 registry is initialized
 * @return true if initialized
 */
bool IsL2RegistryInitialized();

} // namespace l2

#endif // CASCOIN_L2_REGISTRY_H
