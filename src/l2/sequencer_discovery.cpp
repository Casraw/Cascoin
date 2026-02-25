// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file sequencer_discovery.cpp
 * @brief Implementation of Sequencer Discovery and Management
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6
 */

#include <l2/sequencer_discovery.h>
#include <l2/l2_chainparams.h>
#include <util.h>
#include <validation.h>

// Include CVM HAT v2 for reputation verification
#include <cvm/securehat.h>
#include <cvm/cvmdb.h>

#include <algorithm>
#include <chrono>

namespace l2 {

// Global sequencer discovery instance
static std::unique_ptr<SequencerDiscovery> g_sequencer_discovery;
static bool g_sequencer_discovery_initialized = false;

SequencerDiscovery::SequencerDiscovery(uint64_t chainId)
    : chainId_(chainId)
    , isLocalSequencer_(false)
{
}

bool SequencerDiscovery::AnnounceAsSequencer(
    const CKey& signingKey,
    CAmount stakeAmount,
    uint32_t hatScore,
    const std::string& publicEndpoint)
{
    // Get the L2 parameters for eligibility checking
    const L2Params& params = GetL2Params();

    // Check minimum requirements before announcing
    if (hatScore < params.nMinSequencerHATScore) {
        LogPrintf("SequencerDiscovery: HAT score %u below minimum %u\n",
                  hatScore, params.nMinSequencerHATScore);
        return false;
    }

    if (stakeAmount < params.nMinSequencerStake) {
        LogPrintf("SequencerDiscovery: Stake %lld below minimum %lld\n",
                  stakeAmount, params.nMinSequencerStake);
        return false;
    }

    // Create the announcement message
    SeqAnnounceMsg msg;
    
    // Get public key and derive address
    CPubKey pubkey = signingKey.GetPubKey();
    msg.sequencerAddress = pubkey.GetID();
    msg.stakeAmount = stakeAmount;
    msg.hatScore = hatScore;
    msg.blockHeight = GetCurrentL1BlockHeight();
    msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.publicEndpoint = publicEndpoint;
    msg.peerCount = GetCurrentPeerCount();
    msg.l2ChainId = chainId_;
    msg.protocolVersion = L2_PROTOCOL_VERSION;

    // Sign the message
    if (!msg.Sign(signingKey)) {
        LogPrintf("SequencerDiscovery: Failed to sign announcement\n");
        return false;
    }

    // Process locally first
    if (!ProcessSeqAnnounce(msg, nullptr)) {
        LogPrintf("SequencerDiscovery: Local announcement processing failed\n");
        return false;
    }

    // Mark as local sequencer
    {
        LOCK(cs_sequencers_);
        isLocalSequencer_ = true;
        localSequencerAddress_ = msg.sequencerAddress;
    }

    // Broadcast to network
    BroadcastAnnouncement(msg);

    LogPrintf("SequencerDiscovery: Announced as sequencer with address %s\n",
              msg.sequencerAddress.ToString());

    return true;
}

bool SequencerDiscovery::ProcessSeqAnnounce(const SeqAnnounceMsg& msg, CNode* pfrom)
{
    // Validate protocol version
    if (msg.protocolVersion > L2_PROTOCOL_VERSION) {
        LogPrint(BCLog::L2, "SequencerDiscovery: Announcement from future protocol version %u\n",
                 msg.protocolVersion);
        return false;
    }

    // Validate chain ID
    if (msg.l2ChainId != chainId_) {
        LogPrint(BCLog::L2, "SequencerDiscovery: Announcement for different chain %lu\n",
                 msg.l2ChainId);
        return false;
    }

    // Check timestamp validity
    if (msg.IsExpired(ANNOUNCEMENT_EXPIRY_SECONDS)) {
        LogPrint(BCLog::L2, "SequencerDiscovery: Announcement expired\n");
        return false;
    }

    if (msg.IsFromFuture(60)) {
        LogPrint(BCLog::L2, "SequencerDiscovery: Announcement from future\n");
        return false;
    }

    // Get L2 parameters
    const L2Params& params = GetL2Params();

    LOCK(cs_sequencers_);

    // Check if we already have this sequencer
    auto it = sequencerRegistry_.find(msg.sequencerAddress);
    if (it != sequencerRegistry_.end()) {
        // Update if newer announcement
        if (msg.timestamp > it->second.lastAnnouncement) {
            it->second.lastAnnouncement = msg.timestamp;
            it->second.peerCount = msg.peerCount;
            it->second.publicEndpoint = msg.publicEndpoint;
            // Note: stake and HAT score need verification, don't update directly
            LogPrint(BCLog::L2, "SequencerDiscovery: Updated sequencer %s\n",
                     msg.sequencerAddress.ToString());
        }
        return true;
    }

    // Check registry size limit
    if (sequencerRegistry_.size() >= MAX_SEQUENCERS) {
        // Remove oldest entry
        uint64_t oldestTime = UINT64_MAX;
        uint160 oldestAddr;
        for (const auto& entry : sequencerRegistry_) {
            if (entry.second.lastAnnouncement < oldestTime) {
                oldestTime = entry.second.lastAnnouncement;
                oldestAddr = entry.first;
            }
        }
        sequencerRegistry_.erase(oldestAddr);
    }

    // Create new sequencer info
    SequencerInfo info;
    info.address = msg.sequencerAddress;
    info.verifiedStake = msg.stakeAmount;  // Will be verified later
    info.verifiedHatScore = msg.hatScore;  // Will be verified later
    info.peerCount = msg.peerCount;
    info.publicEndpoint = msg.publicEndpoint;
    info.lastAnnouncement = msg.timestamp;
    info.l2ChainId = msg.l2ChainId;
    info.isVerified = false;  // Needs verification
    
    // Check basic eligibility (before verification)
    info.isEligible = (msg.hatScore >= params.nMinSequencerHATScore &&
                       msg.stakeAmount >= params.nMinSequencerStake &&
                       msg.peerCount >= params.nMinSequencerPeerCount);

    sequencerRegistry_[msg.sequencerAddress] = info;

    LogPrint(BCLog::L2, "SequencerDiscovery: Added new sequencer %s (eligible: %d)\n",
             msg.sequencerAddress.ToString(), info.isEligible);

    return true;
}

std::vector<SequencerInfo> SequencerDiscovery::GetEligibleSequencers() const
{
    LOCK(cs_sequencers_);
    
    std::vector<SequencerInfo> eligible;
    eligible.reserve(sequencerRegistry_.size());

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& entry : sequencerRegistry_) {
        const SequencerInfo& info = entry.second;
        
        // Check if announcement is still valid
        if (now - info.lastAnnouncement > ANNOUNCEMENT_EXPIRY_SECONDS) {
            continue;
        }

        // Check eligibility
        if (info.isEligible && MeetsMinimumRequirements(info)) {
            eligible.push_back(info);
        }
    }

    // Sort by weight (descending) for consistent ordering
    std::sort(eligible.begin(), eligible.end(),
              [](const SequencerInfo& a, const SequencerInfo& b) {
                  return a.GetWeight() > b.GetWeight();
              });

    return eligible;
}

bool SequencerDiscovery::IsEligibleSequencer(const uint160& address) const
{
    LOCK(cs_sequencers_);

    auto it = sequencerRegistry_.find(address);
    if (it == sequencerRegistry_.end()) {
        return false;
    }

    const SequencerInfo& info = it->second;

    // Check if announcement is still valid
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - info.lastAnnouncement > ANNOUNCEMENT_EXPIRY_SECONDS) {
        return false;
    }

    return info.isEligible && MeetsMinimumRequirements(info);
}

bool SequencerDiscovery::VerifySequencerEligibility(const uint160& address)
{
    LOCK(cs_sequencers_);

    auto it = sequencerRegistry_.find(address);
    if (it == sequencerRegistry_.end()) {
        return false;
    }

    SequencerInfo& info = it->second;
    const L2Params& params = GetL2Params();

    // Verify stake on L1
    if (!VerifyStakeOnL1(address, params.nMinSequencerStake)) {
        info.isEligible = false;
        info.isVerified = true;
        LogPrint(BCLog::L2, "SequencerDiscovery: Stake verification failed for %s\n",
                 address.ToString());
        return false;
    }

    // Verify HAT score
    if (!VerifyHatScore(address, params.nMinSequencerHATScore)) {
        info.isEligible = false;
        info.isVerified = true;
        LogPrint(BCLog::L2, "SequencerDiscovery: HAT score verification failed for %s\n",
                 address.ToString());
        return false;
    }

    // Check attestations
    auto attestIt = attestationCache_.find(address);
    if (attestIt != attestationCache_.end()) {
        const auto& attestations = attestIt->second;
        if (attestations.size() >= MIN_ATTESTATIONS_FOR_VERIFICATION) {
            // Calculate average attested values
            uint64_t totalHat = 0;
            CAmount totalStake = 0;
            for (const auto& att : attestations) {
                totalHat += att.attestedHatScore;
                totalStake += att.attestedStake;
            }
            uint32_t avgHat = static_cast<uint32_t>(totalHat / attestations.size());
            CAmount avgStake = totalStake / static_cast<CAmount>(attestations.size());

            // Update verified values
            info.verifiedHatScore = avgHat;
            info.verifiedStake = avgStake;
            info.attestationCount = static_cast<uint32_t>(attestations.size());
        }
    }

    // Final eligibility check
    info.isEligible = MeetsMinimumRequirements(info);
    info.isVerified = true;

    LogPrint(BCLog::L2, "SequencerDiscovery: Verified sequencer %s (eligible: %d)\n",
             address.ToString(), info.isEligible);

    return info.isEligible;
}

bool SequencerDiscovery::VerifyStakeOnL1(const uint160& address, CAmount minStake)
{
    LOCK(cs_sequencers_);
    auto it = sequencerRegistry_.find(address);
    if (it == sequencerRegistry_.end()) {
        return false;
    }

    // Try to get actual stake from the CVM system
    try {
        if (CVM::g_cvmdb) {
            CVM::SecureHAT hat(*CVM::g_cvmdb);
            
            // Get stake info from HAT v2 system
            CVM::StakeInfo stakeInfo = hat.GetStakeInfo(address);
            
            // Update verified stake
            it->second.verifiedStake = stakeInfo.amount;
            
            LogPrint(BCLog::L2, "SequencerDiscovery: Verified stake %lld for %s\n",
                     stakeInfo.amount, address.ToString());
            
            return it->second.verifiedStake >= minStake;
        }
    } catch (const std::exception& e) {
        LogPrint(BCLog::L2, "SequencerDiscovery: Stake verification error: %s\n", e.what());
    }

    // Fallback: check against announced stake if CVM not available
    LogPrint(BCLog::L2, "SequencerDiscovery: Using announced stake for %s (CVM unavailable)\n",
             address.ToString());
    return it->second.verifiedStake >= minStake;
}

bool SequencerDiscovery::VerifyHatScore(const uint160& address, uint32_t minScore)
{
    LOCK(cs_sequencers_);
    auto it = sequencerRegistry_.find(address);
    if (it == sequencerRegistry_.end()) {
        return false;
    }

    // Try to get actual HAT v2 score from the CVM system
    try {
        // Get the CVM database
        if (CVM::g_cvmdb) {
            CVM::SecureHAT hat(*CVM::g_cvmdb);
            
            // Calculate HAT score (using self as viewer for global score)
            int16_t hatScore = hat.CalculateFinalTrust(address, address);
            
            // Update verified score
            it->second.verifiedHatScore = static_cast<uint32_t>(std::max(0, static_cast<int>(hatScore)));
            
            LogPrint(BCLog::L2, "SequencerDiscovery: Verified HAT score %d for %s\n",
                     hatScore, address.ToString());
            
            return it->second.verifiedHatScore >= minScore;
        }
    } catch (const std::exception& e) {
        LogPrint(BCLog::L2, "SequencerDiscovery: HAT score verification error: %s\n", e.what());
    }

    // Fallback: check against announced score if CVM not available
    // This is less secure but allows operation in test environments
    LogPrint(BCLog::L2, "SequencerDiscovery: Using announced HAT score for %s (CVM unavailable)\n",
             address.ToString());
    return it->second.verifiedHatScore >= minScore;
}

bool SequencerDiscovery::ProcessAttestation(const SequencerAttestation& attestation)
{
    LOCK(cs_sequencers_);

    // Check if we know this sequencer
    auto it = sequencerRegistry_.find(attestation.sequencerAddress);
    if (it == sequencerRegistry_.end()) {
        return false;
    }

    // Get or create attestation list
    auto& attestations = attestationCache_[attestation.sequencerAddress];

    // Check for duplicate attester
    for (const auto& existing : attestations) {
        if (existing.attesterAddress == attestation.attesterAddress) {
            // Update if newer
            if (attestation.timestamp > existing.timestamp) {
                // Remove old and add new
                attestations.erase(
                    std::remove_if(attestations.begin(), attestations.end(),
                        [&](const SequencerAttestation& a) {
                            return a.attesterAddress == attestation.attesterAddress;
                        }),
                    attestations.end());
                break;
            }
            return true;  // Already have newer attestation
        }
    }

    // Limit attestations per sequencer
    if (attestations.size() >= MAX_ATTESTATIONS_PER_SEQUENCER) {
        // Remove oldest
        auto oldest = std::min_element(attestations.begin(), attestations.end(),
            [](const SequencerAttestation& a, const SequencerAttestation& b) {
                return a.timestamp < b.timestamp;
            });
        attestations.erase(oldest);
    }

    attestations.push_back(attestation);

    // Update attestation count
    it->second.attestationCount = static_cast<uint32_t>(attestations.size());

    return true;
}

std::optional<SequencerInfo> SequencerDiscovery::GetSequencerInfo(const uint160& address) const
{
    LOCK(cs_sequencers_);

    auto it = sequencerRegistry_.find(address);
    if (it == sequencerRegistry_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<SequencerInfo> SequencerDiscovery::GetAllSequencers() const
{
    LOCK(cs_sequencers_);

    std::vector<SequencerInfo> result;
    result.reserve(sequencerRegistry_.size());

    for (const auto& entry : sequencerRegistry_) {
        result.push_back(entry.second);
    }

    return result;
}

size_t SequencerDiscovery::GetSequencerCount() const
{
    LOCK(cs_sequencers_);
    return sequencerRegistry_.size();
}

size_t SequencerDiscovery::GetEligibleCount() const
{
    LOCK(cs_sequencers_);

    size_t count = 0;
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& entry : sequencerRegistry_) {
        const SequencerInfo& info = entry.second;
        if (info.isEligible && 
            MeetsMinimumRequirements(info) &&
            (now - info.lastAnnouncement <= ANNOUNCEMENT_EXPIRY_SECONDS)) {
            ++count;
        }
    }

    return count;
}

size_t SequencerDiscovery::PruneExpiredSequencers(uint64_t maxAge)
{
    LOCK(cs_sequencers_);

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    size_t removed = 0;
    auto it = sequencerRegistry_.begin();
    while (it != sequencerRegistry_.end()) {
        if (now - it->second.lastAnnouncement > maxAge) {
            // Also remove attestations
            attestationCache_.erase(it->first);
            it = sequencerRegistry_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        LogPrint(BCLog::L2, "SequencerDiscovery: Pruned %zu expired sequencers\n", removed);
    }

    return removed;
}

void SequencerDiscovery::UpdateSequencerMetrics(const uint160& address, bool producedBlock)
{
    LOCK(cs_sequencers_);

    auto it = sequencerRegistry_.find(address);
    if (it == sequencerRegistry_.end()) {
        return;
    }

    if (producedBlock) {
        it->second.blocksProduced++;
        it->second.lastBlockProduced = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    } else {
        it->second.blocksMissed++;
    }
}

void SequencerDiscovery::Clear()
{
    LOCK(cs_sequencers_);
    sequencerRegistry_.clear();
    attestationCache_.clear();
    isLocalSequencer_ = false;
    localSequencerAddress_ = uint160();
}

bool SequencerDiscovery::MeetsMinimumRequirements(const SequencerInfo& info) const
{
    const L2Params& params = GetL2Params();

    return info.verifiedHatScore >= params.nMinSequencerHATScore &&
           info.verifiedStake >= params.nMinSequencerStake &&
           info.peerCount >= params.nMinSequencerPeerCount;
}

void SequencerDiscovery::BroadcastAnnouncement(const SeqAnnounceMsg& msg)
{
    // TODO: Implement P2P broadcast via SEQANNOUNCE message
    // This will be integrated with the P2P layer in Task 19
    LogPrint(BCLog::L2, "SequencerDiscovery: Broadcasting announcement for %s\n",
             msg.sequencerAddress.ToString());
}

void SequencerDiscovery::RequestAttestations(const uint160& sequencerAddr)
{
    // TODO: Implement attestation request via P2P
    // This will be integrated with the P2P layer in Task 19
    LogPrint(BCLog::L2, "SequencerDiscovery: Requesting attestations for %s\n",
             sequencerAddr.ToString());
}

uint64_t SequencerDiscovery::GetCurrentL1BlockHeight() const
{
    // Get current chain height from validation
    LOCK(cs_main);
    return chainActive.Height();
}

uint32_t SequencerDiscovery::GetCurrentPeerCount() const
{
    // TODO: Get actual peer count from CConnman
    // For now, return a placeholder
    return 8;
}

// Global instance management

SequencerDiscovery& GetSequencerDiscovery()
{
    assert(g_sequencer_discovery_initialized);
    return *g_sequencer_discovery;
}

void InitSequencerDiscovery(uint64_t chainId)
{
    g_sequencer_discovery = std::make_unique<SequencerDiscovery>(chainId);
    g_sequencer_discovery_initialized = true;
    LogPrintf("SequencerDiscovery: Initialized for chain %lu\n", chainId);
}

bool IsSequencerDiscoveryInitialized()
{
    return g_sequencer_discovery_initialized;
}

} // namespace l2
