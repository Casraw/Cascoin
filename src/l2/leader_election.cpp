// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file leader_election.cpp
 * @brief Implementation of Leader Election and Failover Management
 * 
 * Requirements: 2a.1, 2a.2, 2b.1, 2b.2, 2b.5
 */

#include <l2/leader_election.h>
#include <l2/l2_chainparams.h>
#include <l2/sequencer_discovery.h>
#include <util.h>
#include <validation.h>
#include <hash.h>
#include <arith_uint256.h>

#include <algorithm>
#include <chrono>
#include <numeric>

namespace l2 {

// Global leader election instance
static std::unique_ptr<LeaderElection> g_leader_election;
static bool g_leader_election_initialized = false;

LeaderElection::LeaderElection(uint64_t chainId)
    : chainId_(chainId)
    , currentBlockHeight_(0)
    , isLocalSequencer_(false)
    , failoverInProgress_(false)
    , currentFailoverPosition_(0)
    , blocksPerLeader_(10)  // Default: rotate every 10 blocks
    , leaderTimeoutMs_(3000) // Default: 3 second timeout
{
    lastBlockTime_ = std::chrono::steady_clock::now();
}

LeaderElectionResult LeaderElection::ElectLeader(
    uint64_t slotNumber,
    const std::vector<SequencerInfo>& eligibleSequencers,
    const uint256& randomSeed)
{
    LeaderElectionResult result;
    result.slotNumber = slotNumber;
    result.electionSeed = randomSeed;
    result.electionTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Need at least one eligible sequencer
    if (eligibleSequencers.empty()) {
        LogPrint(BCLog::L2, "LeaderElection: No eligible sequencers for slot %lu\n", slotNumber);
        result.isValid = false;
        return result;
    }

    // If only one sequencer, they are the leader
    if (eligibleSequencers.size() == 1) {
        result.leaderAddress = eligibleSequencers[0].address;
        result.validUntilBlock = (slotNumber + 1) * blocksPerLeader_;
        result.isValid = true;
        
        LogPrint(BCLog::L2, "LeaderElection: Single sequencer %s elected for slot %lu\n",
                 result.leaderAddress.ToString(), slotNumber);
        return result;
    }

    // Perform weighted random selection for leader
    result.leaderAddress = WeightedRandomSelect(eligibleSequencers, randomSeed);
    result.validUntilBlock = (slotNumber + 1) * blocksPerLeader_;

    // Build backup sequencer list (excluding the leader)
    // Sort by weight descending for deterministic failover order
    std::vector<SequencerInfo> sortedSequencers = eligibleSequencers;
    std::sort(sortedSequencers.begin(), sortedSequencers.end(),
              [](const SequencerInfo& a, const SequencerInfo& b) {
                  if (a.GetWeight() != b.GetWeight()) {
                      return a.GetWeight() > b.GetWeight();
                  }
                  // Tie-breaker: use address for determinism
                  return a.address < b.address;
              });

    result.backupSequencers.reserve(std::min(sortedSequencers.size() - 1, MAX_BACKUP_SEQUENCERS));
    for (const auto& seq : sortedSequencers) {
        if (seq.address != result.leaderAddress) {
            result.backupSequencers.push_back(seq.address);
            if (result.backupSequencers.size() >= MAX_BACKUP_SEQUENCERS) {
                break;
            }
        }
    }

    result.isValid = true;

    LogPrint(BCLog::L2, "LeaderElection: Elected leader %s for slot %lu with %zu backups\n",
             result.leaderAddress.ToString(), slotNumber, result.backupSequencers.size());

    return result;
}

uint256 LeaderElection::GenerateElectionSeed(uint64_t slotNumber) const
{
    // Generate deterministic seed from:
    // 1. Slot number
    // 2. L1 block hash at a specific height
    // 3. Chain ID
    
    // Use L1 block that was finalized before this slot started
    // This ensures the seed is unpredictable before the L1 block is mined
    // but deterministic once it's known
    uint64_t seedBlockHeight = slotNumber * blocksPerLeader_;
    
    // Get L1 block hash (use a block that's sufficiently confirmed)
    uint256 l1BlockHash = GetL1BlockHash(seedBlockHeight > 6 ? seedBlockHeight - 6 : 0);
    
    // Combine all inputs into a deterministic seed
    CHashWriter ss(SER_GETHASH, 0);
    ss << slotNumber;
    ss << l1BlockHash;
    ss << chainId_;
    ss << std::string("CASCOIN_L2_ELECTION_SEED_V1");
    
    return ss.GetHash();
}

uint160 LeaderElection::WeightedRandomSelect(
    const std::vector<SequencerInfo>& sequencers,
    const uint256& seed) const
{
    if (sequencers.empty()) {
        return uint160();
    }

    if (sequencers.size() == 1) {
        return sequencers[0].address;
    }

    // Calculate total weight
    uint64_t totalWeight = CalculateTotalWeight(sequencers);
    
    if (totalWeight == 0) {
        // Fallback to uniform selection if all weights are zero
        // Use seed to select index deterministically
        arith_uint256 seedNum = UintToArith256(seed);
        arith_uint256 divisor(sequencers.size());
        arith_uint256 quotient = seedNum / divisor;
        arith_uint256 remainder = seedNum - (quotient * divisor);
        uint64_t index = remainder.GetLow64();
        return sequencers[index].address;
    }

    // Use seed to generate a random value in [0, totalWeight)
    arith_uint256 seedNum = UintToArith256(seed);
    arith_uint256 divisor(totalWeight);
    arith_uint256 quotient = seedNum / divisor;
    arith_uint256 remainder = seedNum - (quotient * divisor);
    uint64_t randomValue = remainder.GetLow64();

    // Select sequencer based on cumulative weight
    uint64_t cumulativeWeight = 0;
    for (const auto& seq : sequencers) {
        cumulativeWeight += seq.GetWeight();
        if (randomValue < cumulativeWeight) {
            return seq.address;
        }
    }

    // Fallback (should not reach here)
    return sequencers.back().address;
}

uint64_t LeaderElection::CalculateTotalWeight(const std::vector<SequencerInfo>& sequencers) const
{
    uint64_t total = 0;
    for (const auto& seq : sequencers) {
        total += seq.GetWeight();
    }
    return total;
}

bool LeaderElection::IsCurrentLeader() const
{
    LOCK(cs_election_);
    
    if (!isLocalSequencer_ || !currentElection_.isValid) {
        return false;
    }

    return currentElection_.leaderAddress == localSequencerAddress_;
}

std::optional<SequencerInfo> LeaderElection::GetCurrentLeader() const
{
    LOCK(cs_election_);

    if (!currentElection_.isValid) {
        return std::nullopt;
    }

    // Get sequencer info from discovery
    if (IsSequencerDiscoveryInitialized()) {
        return GetSequencerDiscovery().GetSequencerInfo(currentElection_.leaderAddress);
    }

    return std::nullopt;
}

LeaderElectionResult LeaderElection::GetCurrentElection() const
{
    LOCK(cs_election_);
    return currentElection_;
}


void LeaderElection::HandleLeaderTimeout(uint64_t slotNumber)
{
    LOCK(cs_election_);

    LogPrint(BCLog::L2, "LeaderElection: Leader timeout for slot %lu\n", slotNumber);

    // Check if this is for the current slot
    if (currentElection_.slotNumber != slotNumber) {
        LogPrint(BCLog::L2, "LeaderElection: Timeout for different slot, ignoring\n");
        return;
    }

    // Mark failover in progress
    failoverInProgress_ = true;

    // Move to next backup sequencer
    if (currentFailoverPosition_ < currentElection_.backupSequencers.size()) {
        uint160 previousLeader = currentElection_.leaderAddress;
        currentElection_.leaderAddress = currentElection_.backupSequencers[currentFailoverPosition_];
        currentFailoverPosition_++;

        LogPrint(BCLog::L2, "LeaderElection: Failover from %s to %s (position %u)\n",
                 previousLeader.ToString(),
                 currentElection_.leaderAddress.ToString(),
                 currentFailoverPosition_);

        // Update sequencer metrics for the failed leader
        if (IsSequencerDiscoveryInitialized()) {
            GetSequencerDiscovery().UpdateSequencerMetrics(previousLeader, false);
        }

        // Notify callbacks
        NotifyLeaderChange(currentElection_);

        // Reset timeout tracking
        lastBlockTime_ = std::chrono::steady_clock::now();
    } else {
        // No more backups available
        LogPrintf("LeaderElection: No more backup sequencers available for slot %lu\n", slotNumber);
        currentElection_.isValid = false;
        failoverInProgress_ = false;
    }
}

bool LeaderElection::ClaimLeadership(const CKey& signingKey)
{
    LOCK(cs_election_);

    if (!isLocalSequencer_) {
        LogPrint(BCLog::L2, "LeaderElection: Cannot claim leadership - not a sequencer\n");
        return false;
    }

    // Check if we're in the failover list
    int position = GetFailoverPosition(localSequencerAddress_);
    if (position < 0) {
        LogPrint(BCLog::L2, "LeaderElection: Cannot claim leadership - not in failover list\n");
        return false;
    }

    // Check if it's our turn (based on failover position)
    if (static_cast<uint32_t>(position) > currentFailoverPosition_) {
        LogPrint(BCLog::L2, "LeaderElection: Cannot claim leadership - not our turn yet\n");
        return false;
    }

    // Create leadership claim
    LeadershipClaim claim;
    claim.claimantAddress = localSequencerAddress_;
    claim.slotNumber = currentElection_.slotNumber;
    claim.failoverPosition = static_cast<uint32_t>(position);
    claim.claimTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    claim.previousLeader = currentElection_.leaderAddress;
    claim.claimReason = "timeout";

    // Sign the claim
    uint256 hash = claim.GetSigningHash();
    if (!signingKey.Sign(hash, claim.signature)) {
        LogPrint(BCLog::L2, "LeaderElection: Failed to sign leadership claim\n");
        return false;
    }

    // Process our own claim
    if (!ProcessLeadershipClaim(claim)) {
        return false;
    }

    LogPrint(BCLog::L2, "LeaderElection: Successfully claimed leadership for slot %lu\n",
             claim.slotNumber);

    return true;
}

bool LeaderElection::ProcessLeadershipClaim(const LeadershipClaim& claim)
{
    LOCK(cs_election_);

    // Validate the claim
    if (!ValidateLeadershipClaim(claim)) {
        LogPrint(BCLog::L2, "LeaderElection: Invalid leadership claim from %s\n",
                 claim.claimantAddress.ToString());
        return false;
    }

    // Check for conflicting claims
    for (const auto& existing : pendingClaims_) {
        if (existing.slotNumber == claim.slotNumber &&
            existing.claimantAddress != claim.claimantAddress) {
            // Resolve conflict
            std::vector<LeadershipClaim> conflicts = {existing, claim};
            LeadershipClaim winner = ResolveConflictingClaims(conflicts);
            
            if (winner.claimantAddress != claim.claimantAddress) {
                LogPrint(BCLog::L2, "LeaderElection: Claim from %s lost to %s\n",
                         claim.claimantAddress.ToString(),
                         winner.claimantAddress.ToString());
                return false;
            }
        }
    }

    // Accept the claim
    pendingClaims_.push_back(claim);

    // Update current election
    currentElection_.leaderAddress = claim.claimantAddress;
    failoverInProgress_ = false;
    lastBlockTime_ = std::chrono::steady_clock::now();

    // Notify callbacks
    NotifyLeaderChange(currentElection_);

    LogPrint(BCLog::L2, "LeaderElection: Accepted leadership claim from %s for slot %lu\n",
             claim.claimantAddress.ToString(), claim.slotNumber);

    return true;
}

bool LeaderElection::ValidateLeadershipClaim(const LeadershipClaim& claim) const
{
    // Check slot number matches current
    if (claim.slotNumber != currentElection_.slotNumber) {
        return false;
    }

    // Check claimant is in the failover list
    int position = GetFailoverPosition(claim.claimantAddress);
    if (position < 0 && claim.claimantAddress != currentElection_.leaderAddress) {
        return false;
    }

    // Check failover position is valid
    if (claim.failoverPosition > currentFailoverPosition_ + 1) {
        return false;
    }

    // Check timestamp is reasonable (not too far in future)
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (claim.claimTimestamp > now + 60) {
        return false;
    }

    // Verify signature if we have the public key
    if (IsSequencerDiscoveryInitialized()) {
        auto seqInfo = GetSequencerDiscovery().GetSequencerInfo(claim.claimantAddress);
        if (seqInfo && seqInfo->pubkey.IsValid()) {
            uint256 hash = claim.GetSigningHash();
            if (!seqInfo->pubkey.Verify(hash, claim.signature)) {
                return false;
            }
        }
    }

    return true;
}

LeadershipClaim LeaderElection::ResolveConflictingClaims(
    const std::vector<LeadershipClaim>& claims) const
{
    if (claims.empty()) {
        return LeadershipClaim();
    }

    if (claims.size() == 1) {
        return claims[0];
    }

    // Resolution rules (Requirements 2b.7):
    // 1. Lower failover position wins
    // 2. If same position, higher reputation wins
    // 3. If same reputation, earlier timestamp wins
    // 4. If same timestamp, lower address wins (deterministic tie-breaker)

    LeadershipClaim winner = claims[0];

    for (size_t i = 1; i < claims.size(); ++i) {
        const LeadershipClaim& challenger = claims[i];

        // Rule 1: Lower failover position
        if (challenger.failoverPosition < winner.failoverPosition) {
            winner = challenger;
            continue;
        }
        if (challenger.failoverPosition > winner.failoverPosition) {
            continue;
        }

        // Rule 2: Higher reputation
        uint32_t winnerRep = 0, challengerRep = 0;
        if (IsSequencerDiscoveryInitialized()) {
            auto winnerInfo = GetSequencerDiscovery().GetSequencerInfo(winner.claimantAddress);
            auto challengerInfo = GetSequencerDiscovery().GetSequencerInfo(challenger.claimantAddress);
            if (winnerInfo) winnerRep = winnerInfo->verifiedHatScore;
            if (challengerInfo) challengerRep = challengerInfo->verifiedHatScore;
        }

        if (challengerRep > winnerRep) {
            winner = challenger;
            continue;
        }
        if (challengerRep < winnerRep) {
            continue;
        }

        // Rule 3: Earlier timestamp
        if (challenger.claimTimestamp < winner.claimTimestamp) {
            winner = challenger;
            continue;
        }
        if (challenger.claimTimestamp > winner.claimTimestamp) {
            continue;
        }

        // Rule 4: Lower address (deterministic)
        if (challenger.claimantAddress < winner.claimantAddress) {
            winner = challenger;
        }
    }

    return winner;
}

uint64_t LeaderElection::GetSlotForBlock(uint64_t blockHeight) const
{
    return blockHeight / blocksPerLeader_;
}

uint64_t LeaderElection::GetCurrentSlot() const
{
    LOCK(cs_election_);
    return GetSlotForBlock(currentBlockHeight_);
}

void LeaderElection::UpdateBlockHeight(uint64_t blockHeight)
{
    LOCK(cs_election_);

    uint64_t oldSlot = GetSlotForBlock(currentBlockHeight_);
    uint64_t newSlot = GetSlotForBlock(blockHeight);

    currentBlockHeight_ = blockHeight;
    lastBlockTime_ = std::chrono::steady_clock::now();

    // Check if we need a new election
    if (newSlot != oldSlot) {
        LogPrint(BCLog::L2, "LeaderElection: Slot changed from %lu to %lu at block %lu\n",
                 oldSlot, newSlot, blockHeight);

        // Reset failover state
        failoverInProgress_ = false;
        currentFailoverPosition_ = 0;
        pendingClaims_.clear();

        // Trigger new election if we have sequencer discovery
        if (IsSequencerDiscoveryInitialized()) {
            auto eligibleSequencers = GetSequencerDiscovery().GetEligibleSequencers();
            uint256 seed = GenerateElectionSeed(newSlot);
            currentElection_ = ElectLeader(newSlot, eligibleSequencers, seed);
            
            NotifyLeaderChange(currentElection_);
        }
    }
}

void LeaderElection::SetLocalSequencerAddress(const uint160& address)
{
    LOCK(cs_election_);
    localSequencerAddress_ = address;
    isLocalSequencer_ = !address.IsNull();
}

void LeaderElection::RegisterLeaderChangeCallback(LeaderChangeCallback callback)
{
    LOCK(cs_election_);
    leaderChangeCallbacks_.push_back(std::move(callback));
}

bool LeaderElection::IsFailoverInProgress() const
{
    LOCK(cs_election_);
    return failoverInProgress_;
}

int LeaderElection::GetFailoverPosition(const uint160& address) const
{
    LOCK(cs_election_);

    // Check if it's the current leader
    if (address == currentElection_.leaderAddress) {
        return 0;
    }

    // Check backup list
    for (size_t i = 0; i < currentElection_.backupSequencers.size(); ++i) {
        if (currentElection_.backupSequencers[i] == address) {
            return static_cast<int>(i + 1);
        }
    }

    return -1;  // Not in list
}

void LeaderElection::NotifyLeaderChange(const LeaderElectionResult& result)
{
    // Note: Must be called with cs_election_ held
    for (const auto& callback : leaderChangeCallbacks_) {
        try {
            callback(result);
        } catch (const std::exception& e) {
            LogPrint(BCLog::L2, "LeaderElection: Callback error: %s\n", e.what());
        }
    }
}

uint256 LeaderElection::GetL1BlockHash(uint64_t height) const
{
    LOCK(cs_main);
    
    if (chainActive.Height() < static_cast<int>(height)) {
        // Use tip if requested height is beyond current chain
        if (chainActive.Tip()) {
            return chainActive.Tip()->GetBlockHash();
        }
        return uint256();
    }

    CBlockIndex* pindex = chainActive[static_cast<int>(height)];
    if (pindex) {
        return pindex->GetBlockHash();
    }

    return uint256();
}

void LeaderElection::Clear()
{
    LOCK(cs_election_);
    currentElection_ = LeaderElectionResult();
    currentBlockHeight_ = 0;
    failoverInProgress_ = false;
    currentFailoverPosition_ = 0;
    pendingClaims_.clear();
    leaderChangeCallbacks_.clear();
}

// Global instance management

LeaderElection& GetLeaderElection()
{
    assert(g_leader_election_initialized);
    return *g_leader_election;
}

void InitLeaderElection(uint64_t chainId)
{
    g_leader_election = std::make_unique<LeaderElection>(chainId);
    g_leader_election_initialized = true;
    LogPrintf("LeaderElection: Initialized for chain %lu\n", chainId);
}

bool IsLeaderElectionInitialized()
{
    return g_leader_election_initialized;
}

} // namespace l2
