// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file forced_inclusion.cpp
 * @brief Implementation of Forced Transaction Inclusion System
 * 
 * Requirements: 17.1, 17.2, 17.3, 17.4, 17.5, 17.6
 */

#include <l2/forced_inclusion.h>
#include <hash.h>
#include <util.h>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

ForcedInclusionSystem::ForcedInclusionSystem(uint64_t chainId)
    : chainId_(chainId)
    , totalBondsHeld_(0)
    , totalSlashed_(0)
    , nextRequestId_(1)
{
}

// ============================================================================
// L1 Transaction Submission (Requirement 17.1)
// ============================================================================

std::optional<ForcedInclusionRequest> ForcedInclusionSystem::SubmitForcedTransaction(
    const uint256& l1TxHash,
    uint64_t l1BlockNumber,
    const uint160& submitter,
    const uint160& target,
    CAmount value,
    const std::vector<unsigned char>& data,
    uint64_t gasLimit,
    const uint256& maxGasPrice,
    uint64_t nonce,
    CAmount bondAmount,
    uint64_t currentTime)
{
    LOCK(cs_forced_);

    // Validate bond amount
    if (bondAmount < FORCED_INCLUSION_BOND) {
        return std::nullopt;
    }

    // Check per-address limit
    auto it = pendingBySubmitter_.find(submitter);
    if (it != pendingBySubmitter_.end() && it->second.size() >= MAX_FORCED_TX_PER_ADDRESS) {
        return std::nullopt;
    }

    // Check total limit
    size_t pendingCount = 0;
    for (const auto& [addr, ids] : pendingBySubmitter_) {
        pendingCount += ids.size();
    }
    if (pendingCount >= MAX_TOTAL_FORCED_TX) {
        return std::nullopt;
    }

    // Validate gas limit
    if (gasLimit == 0) {
        return std::nullopt;
    }

    // Create request
    ForcedInclusionRequest request;
    request.requestId = GenerateRequestId(submitter, l1TxHash, currentTime);
    request.l1TxHash = l1TxHash;
    request.l1BlockNumber = l1BlockNumber;
    request.submitter = submitter;
    request.target = target;
    request.value = value;
    request.data = data;
    request.gasLimit = gasLimit;
    request.maxGasPrice = maxGasPrice;
    request.nonce = nonce;
    request.bondAmount = bondAmount;
    request.submittedAt = currentTime;
    request.deadline = currentTime + FORCED_INCLUSION_DEADLINE;
    request.status = ForcedInclusionStatus::PENDING;
    request.includedInBlock = 0;
    request.l2ChainId = chainId_;

    // Store request
    requests_[request.requestId] = request;
    pendingBySubmitter_[submitter].insert(request.requestId);
    totalBondsHeld_ += bondAmount;

    return request;
}

std::optional<ForcedInclusionRequest> ForcedInclusionSystem::GetRequest(const uint256& requestId) const
{
    LOCK(cs_forced_);
    
    auto it = requests_.find(requestId);
    if (it == requests_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ForcedInclusionRequest> ForcedInclusionSystem::GetPendingRequests(const uint160& submitter) const
{
    LOCK(cs_forced_);
    
    std::vector<ForcedInclusionRequest> result;
    
    auto it = pendingBySubmitter_.find(submitter);
    if (it == pendingBySubmitter_.end()) {
        return result;
    }
    
    for (const auto& requestId : it->second) {
        auto reqIt = requests_.find(requestId);
        if (reqIt != requests_.end() && reqIt->second.status == ForcedInclusionStatus::PENDING) {
            result.push_back(reqIt->second);
        }
    }
    
    return result;
}

std::vector<ForcedInclusionRequest> ForcedInclusionSystem::GetAllPendingRequests() const
{
    LOCK(cs_forced_);
    
    std::vector<ForcedInclusionRequest> result;
    
    for (const auto& [requestId, request] : requests_) {
        if (request.status == ForcedInclusionStatus::PENDING) {
            result.push_back(request);
        }
    }
    
    return result;
}

// ============================================================================
// Inclusion Tracking (Requirement 17.2)
// ============================================================================

bool ForcedInclusionSystem::MarkAsIncluded(
    const uint256& requestId,
    uint64_t l2BlockNumber,
    const uint256& l2TxHash,
    uint64_t currentTime)
{
    LOCK(cs_forced_);
    
    auto it = requests_.find(requestId);
    if (it == requests_.end()) {
        return false;
    }
    
    ForcedInclusionRequest& request = it->second;
    
    // Can only mark pending requests as included
    if (request.status != ForcedInclusionStatus::PENDING) {
        return false;
    }
    
    // Update request
    request.status = ForcedInclusionStatus::INCLUDED;
    request.includedInBlock = l2BlockNumber;
    request.l2TxHash = l2TxHash;
    
    // Remove from pending set
    auto pendingIt = pendingBySubmitter_.find(request.submitter);
    if (pendingIt != pendingBySubmitter_.end()) {
        pendingIt->second.erase(requestId);
        if (pendingIt->second.empty()) {
            pendingBySubmitter_.erase(pendingIt);
        }
    }
    
    // Return bond to submitter
    totalBondsHeld_ -= request.bondAmount;
    
    // Update sequencer stats if assigned
    if (!request.assignedSequencer.IsNull()) {
        bool onTime = currentTime <= request.deadline;
        UpdateSequencerStats(request.assignedSequencer, onTime, 0);
    }
    
    return true;
}

bool ForcedInclusionSystem::AssignSequencer(const uint256& requestId, const uint160& sequencer)
{
    LOCK(cs_forced_);
    
    auto it = requests_.find(requestId);
    if (it == requests_.end()) {
        return false;
    }
    
    if (it->second.status != ForcedInclusionStatus::PENDING) {
        return false;
    }
    
    it->second.assignedSequencer = sequencer;
    
    // Update sequencer stats
    sequencerStats_[sequencer].totalAssigned++;
    
    return true;
}

bool ForcedInclusionSystem::IsRequestExpired(const uint256& requestId, uint64_t currentTime) const
{
    LOCK(cs_forced_);
    
    auto it = requests_.find(requestId);
    if (it == requests_.end()) {
        return false;
    }
    
    return it->second.IsExpired(currentTime);
}

uint64_t ForcedInclusionSystem::GetTimeRemaining(const uint256& requestId, uint64_t currentTime) const
{
    LOCK(cs_forced_);
    
    auto it = requests_.find(requestId);
    if (it == requests_.end()) {
        return 0;
    }
    
    const ForcedInclusionRequest& request = it->second;
    if (currentTime >= request.deadline) {
        return 0;
    }
    
    return request.deadline - currentTime;
}

// ============================================================================
// Sequencer Slashing (Requirement 17.3)
// ============================================================================

std::vector<ForcedInclusionResult> ForcedInclusionSystem::ProcessExpiredRequests(uint64_t currentTime)
{
    LOCK(cs_forced_);
    
    std::vector<ForcedInclusionResult> results;
    std::vector<uint256> expiredIds;
    
    // Find all expired requests
    for (auto& [requestId, request] : requests_) {
        if (request.IsExpired(currentTime)) {
            expiredIds.push_back(requestId);
        }
    }
    
    // Process each expired request
    for (const auto& requestId : expiredIds) {
        auto& request = requests_[requestId];
        
        ForcedInclusionResult result;
        result.requestId = requestId;
        
        // Slash assigned sequencer if any
        if (!request.assignedSequencer.IsNull()) {
            CAmount slashAmount = CalculateSlashingAmount(request.assignedSequencer);
            
            // Record censorship incident
            CensorshipIncident incident;
            incident.sequencerAddress = request.assignedSequencer;
            incident.requestId = requestId;
            incident.timestamp = currentTime;
            incident.slashedAmount = slashAmount;
            incident.wasSlashed = true;
            censorshipIncidents_[request.assignedSequencer].push_back(incident);
            
            // Update stats
            UpdateSequencerStats(request.assignedSequencer, false, slashAmount);
            
            result.slashedSequencer = request.assignedSequencer;
            result.slashedAmount = slashAmount;
            totalSlashed_ += slashAmount;
            
            request.status = ForcedInclusionStatus::SLASHED;
            result.finalStatus = ForcedInclusionStatus::SLASHED;
        } else {
            request.status = ForcedInclusionStatus::EXPIRED;
            result.finalStatus = ForcedInclusionStatus::EXPIRED;
        }
        
        // Return bond to submitter
        result.bondReturned = request.bondAmount;
        totalBondsHeld_ -= request.bondAmount;
        
        // Remove from pending set
        auto pendingIt = pendingBySubmitter_.find(request.submitter);
        if (pendingIt != pendingBySubmitter_.end()) {
            pendingIt->second.erase(requestId);
            if (pendingIt->second.empty()) {
                pendingBySubmitter_.erase(pendingIt);
            }
        }
        
        results.push_back(result);
    }
    
    // Cleanup old incidents
    CleanupOldIncidents(currentTime);
    
    return results;
}

CAmount ForcedInclusionSystem::SlashSequencer(
    const uint160& sequencer,
    const uint256& requestId,
    uint64_t currentTime)
{
    LOCK(cs_forced_);
    
    CAmount slashAmount = CalculateSlashingAmount(sequencer);
    
    // Record incident
    CensorshipIncident incident;
    incident.sequencerAddress = sequencer;
    incident.requestId = requestId;
    incident.timestamp = currentTime;
    incident.slashedAmount = slashAmount;
    incident.wasSlashed = true;
    censorshipIncidents_[sequencer].push_back(incident);
    
    // Update stats
    UpdateSequencerStats(sequencer, false, slashAmount);
    
    totalSlashed_ += slashAmount;
    
    return slashAmount;
}

SequencerInclusionStats ForcedInclusionSystem::GetSequencerStats(const uint160& sequencer) const
{
    LOCK(cs_forced_);
    
    auto it = sequencerStats_.find(sequencer);
    if (it == sequencerStats_.end()) {
        return SequencerInclusionStats();
    }
    return it->second;
}

bool ForcedInclusionSystem::IsRepeatOffender(const uint160& sequencer) const
{
    LOCK(cs_forced_);
    
    auto it = sequencerStats_.find(sequencer);
    if (it == sequencerStats_.end()) {
        return false;
    }
    return it->second.isRepeatOffender;
}

// ============================================================================
// Censorship Tracking (Requirement 17.5)
// ============================================================================

void ForcedInclusionSystem::RecordCensorshipIncident(
    const uint160& sequencer,
    const uint256& requestId,
    uint64_t currentTime)
{
    LOCK(cs_forced_);
    
    CensorshipIncident incident;
    incident.sequencerAddress = sequencer;
    incident.requestId = requestId;
    incident.timestamp = currentTime;
    incident.slashedAmount = 0;
    incident.wasSlashed = false;
    
    censorshipIncidents_[sequencer].push_back(incident);
}

std::vector<CensorshipIncident> ForcedInclusionSystem::GetCensorshipIncidents(const uint160& sequencer) const
{
    LOCK(cs_forced_);
    
    auto it = censorshipIncidents_.find(sequencer);
    if (it == censorshipIncidents_.end()) {
        return {};
    }
    return it->second;
}

uint32_t ForcedInclusionSystem::GetRecentCensorshipCount(const uint160& sequencer, uint64_t currentTime) const
{
    LOCK(cs_forced_);
    
    auto it = censorshipIncidents_.find(sequencer);
    if (it == censorshipIncidents_.end()) {
        return 0;
    }
    
    uint32_t count = 0;
    uint64_t windowStart = (currentTime > CENSORSHIP_TRACKING_WINDOW) 
                           ? currentTime - CENSORSHIP_TRACKING_WINDOW 
                           : 0;
    
    for (const auto& incident : it->second) {
        if (incident.timestamp >= windowStart) {
            count++;
        }
    }
    
    return count;
}

// ============================================================================
// Emergency Self-Sequencing (Requirement 17.6)
// ============================================================================

bool ForcedInclusionSystem::IsEmergencySelfSequencingNeeded(uint64_t currentTime) const
{
    LOCK(cs_forced_);
    
    // Count sequencers with recent censorship incidents
    uint32_t censoringSequencers = 0;
    
    for (const auto& [sequencer, incidents] : censorshipIncidents_) {
        uint32_t recentCount = 0;
        uint64_t windowStart = (currentTime > CENSORSHIP_TRACKING_WINDOW) 
                               ? currentTime - CENSORSHIP_TRACKING_WINDOW 
                               : 0;
        
        for (const auto& incident : incidents) {
            if (incident.timestamp >= windowStart) {
                recentCount++;
            }
        }
        
        if (recentCount >= REPEAT_OFFENDER_THRESHOLD) {
            censoringSequencers++;
        }
    }
    
    return censoringSequencers >= EMERGENCY_SELF_SEQUENCE_THRESHOLD;
}

std::vector<ForcedInclusionRequest> ForcedInclusionSystem::GetEmergencySelfSequenceRequests(uint64_t currentTime) const
{
    LOCK(cs_forced_);
    
    std::vector<ForcedInclusionRequest> result;
    
    // Only return requests if emergency self-sequencing is needed
    if (!IsEmergencySelfSequencingNeeded(currentTime)) {
        return result;
    }
    
    // Return all pending requests that are past 50% of their deadline
    for (const auto& [requestId, request] : requests_) {
        if (request.status == ForcedInclusionStatus::PENDING) {
            uint64_t halfDeadline = request.submittedAt + (FORCED_INCLUSION_DEADLINE / 2);
            if (currentTime >= halfDeadline) {
                result.push_back(request);
            }
        }
    }
    
    return result;
}

// ============================================================================
// Utility Methods
// ============================================================================

size_t ForcedInclusionSystem::GetPendingRequestCount() const
{
    LOCK(cs_forced_);
    
    size_t count = 0;
    for (const auto& [requestId, request] : requests_) {
        if (request.status == ForcedInclusionStatus::PENDING) {
            count++;
        }
    }
    return count;
}

size_t ForcedInclusionSystem::GetTotalRequestCount() const
{
    LOCK(cs_forced_);
    return requests_.size();
}

CAmount ForcedInclusionSystem::GetTotalBondsHeld() const
{
    LOCK(cs_forced_);
    return totalBondsHeld_;
}

CAmount ForcedInclusionSystem::GetTotalSlashed() const
{
    LOCK(cs_forced_);
    return totalSlashed_;
}

void ForcedInclusionSystem::Clear()
{
    LOCK(cs_forced_);
    
    requests_.clear();
    pendingBySubmitter_.clear();
    sequencerStats_.clear();
    censorshipIncidents_.clear();
    sequencerStakes_.clear();
    totalBondsHeld_ = 0;
    totalSlashed_ = 0;
    nextRequestId_ = 1;
}

void ForcedInclusionSystem::SetSequencerStake(const uint160& sequencer, CAmount stake)
{
    LOCK(cs_forced_);
    sequencerStakes_[sequencer] = stake;
}

CAmount ForcedInclusionSystem::GetSequencerStake(const uint160& sequencer) const
{
    LOCK(cs_forced_);
    
    auto it = sequencerStakes_.find(sequencer);
    if (it == sequencerStakes_.end()) {
        return 0;
    }
    return it->second;
}

// ============================================================================
// Private Methods
// ============================================================================

uint256 ForcedInclusionSystem::GenerateRequestId(
    const uint160& submitter,
    const uint256& l1TxHash,
    uint64_t timestamp) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << submitter << l1TxHash << timestamp << nextRequestId_ << chainId_;
    return ss.GetHash();
}

CAmount ForcedInclusionSystem::CalculateSlashingAmount(const uint160& sequencer) const
{
    // Get sequencer stake
    auto it = sequencerStakes_.find(sequencer);
    CAmount stake = (it != sequencerStakes_.end()) ? it->second : 0;
    
    // Slash minimum amount or percentage of stake, whichever is greater
    CAmount percentageSlash = stake / 10;  // 10% of stake
    
    return std::max(FORCED_INCLUSION_SLASH_AMOUNT, percentageSlash);
}

void ForcedInclusionSystem::UpdateSequencerStats(
    const uint160& sequencer,
    bool onTime,
    CAmount slashedAmount)
{
    SequencerInclusionStats& stats = sequencerStats_[sequencer];
    
    if (onTime) {
        stats.includedOnTime++;
    } else {
        stats.missedDeadlines++;
        stats.totalSlashed += slashedAmount;
        stats.lastIncidentAt = GetTime();
        
        // Check if repeat offender
        if (stats.missedDeadlines >= REPEAT_OFFENDER_THRESHOLD) {
            stats.isRepeatOffender = true;
        }
    }
}

void ForcedInclusionSystem::CleanupOldIncidents(uint64_t currentTime)
{
    uint64_t windowStart = (currentTime > CENSORSHIP_TRACKING_WINDOW) 
                           ? currentTime - CENSORSHIP_TRACKING_WINDOW 
                           : 0;
    
    for (auto& [sequencer, incidents] : censorshipIncidents_) {
        incidents.erase(
            std::remove_if(incidents.begin(), incidents.end(),
                [windowStart](const CensorshipIncident& incident) {
                    return incident.timestamp < windowStart;
                }),
            incidents.end());
    }
}

} // namespace l2
