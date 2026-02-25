// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_registry.cpp
 * @brief Implementation of L2 Chain Registry
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5
 */

#include <l2/l2_registry.h>
#include <hash.h>
#include <util.h>
#include <utilstrencodings.h>
#include <utilmoneystr.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <regex>

namespace l2 {

// Global registry instance
static std::unique_ptr<L2Registry> g_l2Registry;
static bool g_l2RegistryInitialized = false;

std::string L2ChainStatusToString(L2ChainStatus status) {
    switch (status) {
        case L2ChainStatus::BOOTSTRAPPING: return "BOOTSTRAPPING";
        case L2ChainStatus::ACTIVE: return "ACTIVE";
        case L2ChainStatus::PAUSED: return "PAUSED";
        case L2ChainStatus::EMERGENCY: return "EMERGENCY";
        case L2ChainStatus::DEPRECATED: return "DEPRECATED";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// L2Registry Implementation
// ============================================================================

L2Registry::L2Registry() : m_chainIdCounter(CHAIN_ID_RANGE_START) {
}

uint64_t L2Registry::RegisterL2Chain(const L2RegistrationRequest& request,
                                      uint64_t l1BlockNumber) {
    // Validate the request
    ValidationResult validation = ValidateRegistrationRequest(request);
    if (!validation.isValid) {
        LogPrintf("L2Registry: Registration failed - %s\n", validation.errorMessage);
        return 0;
    }
    
    return RegisterL2Chain(request.name, request.deployer, request.stake,
                           request.params, l1BlockNumber);
}

uint64_t L2Registry::RegisterL2Chain(const std::string& name,
                                      const uint160& deployer,
                                      CAmount stake,
                                      const L2DeploymentParams& params,
                                      uint64_t l1BlockNumber) {
    LOCK(cs_registry);
    
    // Validate name
    ValidationResult nameValidation = ValidateChainName(name);
    if (!nameValidation.isValid) {
        LogPrintf("L2Registry: Invalid chain name - %s\n", nameValidation.errorMessage);
        return 0;
    }
    
    // Check if name already exists
    if (m_nameToChainId.count(name) > 0) {
        LogPrintf("L2Registry: Chain name '%s' already exists\n", name);
        return 0;
    }
    
    // Validate stake
    ValidationResult stakeValidation = ValidateDeployerStake(stake);
    if (!stakeValidation.isValid) {
        LogPrintf("L2Registry: Invalid stake - %s\n", stakeValidation.errorMessage);
        return 0;
    }
    
    // Validate deployment parameters
    ValidationResult paramsValidation = ValidateDeploymentParams(params);
    if (!paramsValidation.isValid) {
        LogPrintf("L2Registry: Invalid parameters - %s\n", paramsValidation.errorMessage);
        return 0;
    }
    
    // Generate unique chain ID
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t chainId = GenerateChainId(name, deployer, timestamp);
    
    // Create chain info
    L2ChainInfo info;
    info.chainId = chainId;
    info.name = name;
    info.deployer = deployer;
    info.deploymentBlock = l1BlockNumber;
    info.deploymentTime = timestamp;
    info.params = params;
    info.status = L2ChainStatus::BOOTSTRAPPING;
    info.deployerStake = stake;
    info.latestL2Block = 0;
    info.latestL1Anchor = l1BlockNumber;
    info.totalValueLocked = 0;
    info.sequencerCount = 0;
    
    // Add to registry
    AddChainInternal(info);
    
    LogPrintf("L2Registry: Registered new L2 chain '%s' with ID %lu\n", 
              name, chainId);
    
    return chainId;
}

std::optional<L2ChainInfo> L2Registry::GetL2ChainInfo(uint64_t chainId) const {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it != m_chains.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<L2ChainInfo> L2Registry::GetL2ChainInfoByName(const std::string& name) const {
    LOCK(cs_registry);
    
    auto it = m_nameToChainId.find(name);
    if (it != m_nameToChainId.end()) {
        auto chainIt = m_chains.find(it->second);
        if (chainIt != m_chains.end()) {
            return chainIt->second;
        }
    }
    return std::nullopt;
}

std::vector<L2ChainInfo> L2Registry::GetAllChains() const {
    LOCK(cs_registry);
    
    std::vector<L2ChainInfo> result;
    result.reserve(m_chains.size());
    
    for (const auto& pair : m_chains) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<L2ChainInfo> L2Registry::GetActiveChains() const {
    LOCK(cs_registry);
    
    std::vector<L2ChainInfo> result;
    
    for (const auto& pair : m_chains) {
        if (pair.second.IsActive()) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool L2Registry::ChainExists(uint64_t chainId) const {
    LOCK(cs_registry);
    return m_chains.count(chainId) > 0;
}

bool L2Registry::ChainNameExists(const std::string& name) const {
    LOCK(cs_registry);
    return m_nameToChainId.count(name) > 0;
}

size_t L2Registry::GetChainCount() const {
    LOCK(cs_registry);
    return m_chains.size();
}

ValidationResult L2Registry::ValidateDeploymentParams(const L2DeploymentParams& params) {
    // Validate block time
    if (params.blockTimeMs < MIN_BLOCK_TIME_MS) {
        return ValidationResult::Failure(
            strprintf("Block time must be at least %u ms", MIN_BLOCK_TIME_MS));
    }
    if (params.blockTimeMs > MAX_BLOCK_TIME_MS) {
        return ValidationResult::Failure(
            strprintf("Block time cannot exceed %u ms", MAX_BLOCK_TIME_MS));
    }
    
    // Validate gas limit
    if (params.gasLimit < MIN_GAS_LIMIT) {
        return ValidationResult::Failure(
            strprintf("Gas limit must be at least %lu", MIN_GAS_LIMIT));
    }
    if (params.gasLimit > MAX_GAS_LIMIT) {
        return ValidationResult::Failure(
            strprintf("Gas limit cannot exceed %lu", MAX_GAS_LIMIT));
    }
    
    // Validate challenge period
    if (params.challengePeriod < MIN_CHALLENGE_PERIOD) {
        return ValidationResult::Failure(
            strprintf("Challenge period must be at least %lu seconds", MIN_CHALLENGE_PERIOD));
    }
    if (params.challengePeriod > MAX_CHALLENGE_PERIOD) {
        return ValidationResult::Failure(
            strprintf("Challenge period cannot exceed %lu seconds", MAX_CHALLENGE_PERIOD));
    }
    
    // Validate sequencer stake
    if (params.minSequencerStake < MIN_SEQUENCER_STAKE) {
        return ValidationResult::Failure(
            strprintf("Minimum sequencer stake must be at least %s CAS", 
                      FormatMoney(MIN_SEQUENCER_STAKE)));
    }
    
    // Validate sequencer HAT score
    if (params.minSequencerHatScore < MIN_SEQUENCER_HAT_SCORE) {
        return ValidationResult::Failure(
            strprintf("Minimum sequencer HAT score must be at least %u", 
                      MIN_SEQUENCER_HAT_SCORE));
    }
    if (params.minSequencerHatScore > MAX_SEQUENCER_HAT_SCORE) {
        return ValidationResult::Failure(
            strprintf("Minimum sequencer HAT score cannot exceed %u", 
                      MAX_SEQUENCER_HAT_SCORE));
    }
    
    // Validate L1 anchor interval
    if (params.l1AnchorInterval == 0) {
        return ValidationResult::Failure("L1 anchor interval cannot be zero");
    }
    if (params.l1AnchorInterval > 10000) {
        return ValidationResult::Failure("L1 anchor interval cannot exceed 10000 blocks");
    }
    
    return ValidationResult::Success();
}

ValidationResult L2Registry::ValidateDeployerStake(CAmount stake) {
    if (stake < MIN_DEPLOYER_STAKE) {
        return ValidationResult::Failure(
            strprintf("Deployer stake must be at least %s CAS", 
                      FormatMoney(MIN_DEPLOYER_STAKE)));
    }
    return ValidationResult::Success();
}

ValidationResult L2Registry::ValidateChainName(const std::string& name) {
    // Check length
    if (name.empty()) {
        return ValidationResult::Failure("Chain name cannot be empty");
    }
    if (name.length() > MAX_CHAIN_NAME_LENGTH) {
        return ValidationResult::Failure(
            strprintf("Chain name cannot exceed %zu characters", MAX_CHAIN_NAME_LENGTH));
    }
    
    // Check for valid characters (alphanumeric, underscore, hyphen)
    std::regex validPattern("^[a-zA-Z][a-zA-Z0-9_-]*$");
    if (!std::regex_match(name, validPattern)) {
        return ValidationResult::Failure(
            "Chain name must start with a letter and contain only alphanumeric characters, underscores, and hyphens");
    }
    
    return ValidationResult::Success();
}

ValidationResult L2Registry::ValidateRegistrationRequest(const L2RegistrationRequest& request) const {
    // Validate name
    ValidationResult nameResult = ValidateChainName(request.name);
    if (!nameResult.isValid) {
        return nameResult;
    }
    
    // Check if name already exists
    if (ChainNameExists(request.name)) {
        return ValidationResult::Failure("Chain name already exists");
    }
    
    // Validate stake
    ValidationResult stakeResult = ValidateDeployerStake(request.stake);
    if (!stakeResult.isValid) {
        return stakeResult;
    }
    
    // Validate deployment parameters
    ValidationResult paramsResult = ValidateDeploymentParams(request.params);
    if (!paramsResult.isValid) {
        return paramsResult;
    }
    
    // Validate deployer address
    if (request.deployer.IsNull()) {
        return ValidationResult::Failure("Deployer address cannot be null");
    }
    
    return ValidationResult::Success();
}

uint64_t L2Registry::GenerateChainId(const std::string& name,
                                      const uint160& deployer,
                                      uint64_t timestamp) {
    // Generate hash from inputs
    CHashWriter ss(SER_GETHASH, 0);
    ss << name;
    ss << deployer;
    ss << timestamp;
    ss << m_chainIdCounter;
    
    uint256 hash = ss.GetHash();
    
    // Extract chain ID from hash
    uint64_t baseId = hash.GetUint64(0);
    
    // Ensure it's in valid range
    uint64_t chainId = CHAIN_ID_RANGE_START + (baseId % (CHAIN_ID_RANGE_END - CHAIN_ID_RANGE_START));
    
    // Ensure uniqueness
    while (m_usedChainIds.count(chainId) > 0) {
        chainId++;
        if (chainId > CHAIN_ID_RANGE_END) {
            chainId = CHAIN_ID_RANGE_START;
        }
    }
    
    // Mark as used
    m_usedChainIds.insert(chainId);
    m_chainIdCounter++;
    
    return chainId;
}

bool L2Registry::UpdateChainState(uint64_t chainId,
                                   const uint256& stateRoot,
                                   uint64_t l2BlockNumber,
                                   uint64_t l1AnchorBlock) {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it == m_chains.end()) {
        return false;
    }
    
    it->second.latestStateRoot = stateRoot;
    it->second.latestL2Block = l2BlockNumber;
    it->second.latestL1Anchor = l1AnchorBlock;
    
    return true;
}

bool L2Registry::UpdateChainStatus(uint64_t chainId, L2ChainStatus status) {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it == m_chains.end()) {
        return false;
    }
    
    L2ChainStatus oldStatus = it->second.status;
    it->second.status = status;
    
    LogPrintf("L2Registry: Chain %lu status changed from %s to %s\n",
              chainId, L2ChainStatusToString(oldStatus), L2ChainStatusToString(status));
    
    return true;
}

bool L2Registry::UpdateChainTVL(uint64_t chainId, CAmount tvl) {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it == m_chains.end()) {
        return false;
    }
    
    it->second.totalValueLocked = tvl;
    return true;
}

bool L2Registry::UpdateSequencerCount(uint64_t chainId, uint32_t count) {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it == m_chains.end()) {
        return false;
    }
    
    it->second.sequencerCount = count;
    return true;
}

bool L2Registry::SetGenesisHash(uint64_t chainId, const uint256& genesisHash) {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it == m_chains.end()) {
        return false;
    }
    
    it->second.genesisHash = genesisHash;
    return true;
}

bool L2Registry::SetBridgeContract(uint64_t chainId, const uint160& bridgeContract) {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it == m_chains.end()) {
        return false;
    }
    
    it->second.bridgeContract = bridgeContract;
    return true;
}

std::optional<uint160> L2Registry::GetBridgeContract(uint64_t chainId) const {
    LOCK(cs_registry);
    
    auto it = m_chains.find(chainId);
    if (it != m_chains.end()) {
        return it->second.bridgeContract;
    }
    return std::nullopt;
}

void L2Registry::AddChainInternal(const L2ChainInfo& info) {
    // Add to main registry
    m_chains[info.chainId] = info;
    
    // Add to name lookup
    m_nameToChainId[info.name] = info.chainId;
    
    // Mark chain ID as used
    m_usedChainIds.insert(info.chainId);
}

// ============================================================================
// Global Registry Access
// ============================================================================

L2Registry& GetL2Registry() {
    if (!g_l2Registry) {
        InitL2Registry();
    }
    return *g_l2Registry;
}

void InitL2Registry() {
    if (!g_l2RegistryInitialized) {
        g_l2Registry = std::make_unique<L2Registry>();
        g_l2RegistryInitialized = true;
        LogPrintf("L2Registry: Initialized\n");
    }
}

bool IsL2RegistryInitialized() {
    return g_l2RegistryInitialized;
}

} // namespace l2
