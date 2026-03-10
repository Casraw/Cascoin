// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/commit_reveal.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <streams.h>
#include <validation.h>

namespace CVM {

// Database key prefixes for commit-reveal storage
static const std::string DB_COMMITMENT_PREFIX = "commitment_";
static const std::string DB_COMMITMENTS_BY_DISPUTE_PREFIX = "commitments_dispute_";

CommitRevealManager::CommitRevealManager(CVMDatabase& db, const WoTConfig& cfg)
    : database(db)
    , config(cfg)
{
}

uint256 CommitRevealManager::CalculateCommitmentHash(bool vote, const uint256& nonce)
{
    // Create hash of (vote_byte || nonce_bytes)
    // vote_byte: 0x01 for slash (true), 0x00 for keep (false)
    CSHA256 hasher;
    
    unsigned char voteByte = vote ? 0x01 : 0x00;
    hasher.Write(&voteByte, 1);
    hasher.Write(nonce.begin(), 32);
    
    uint256 result;
    hasher.Finalize(result.begin());
    
    return result;
}

bool CommitRevealManager::VerifyCommitment(const uint256& commitmentHash,
                                            bool vote, const uint256& nonce)
{
    uint256 calculatedHash = CalculateCommitmentHash(vote, nonce);
    return calculatedHash == commitmentHash;
}

bool CommitRevealManager::SubmitCommitment(const uint256& disputeId,
                                            const uint160& voter,
                                            const uint256& commitmentHash,
                                            CAmount stake)
{
    // Validate inputs
    if (disputeId.IsNull() || voter.IsNull() || commitmentHash.IsNull() || stake <= 0) {
        return false;
    }
    
    // Check if we're in commit phase
    if (!IsCommitPhase(disputeId)) {
        return false;
    }
    
    // Check if voter already committed
    VoteCommitment existing;
    if (GetCommitment(disputeId, voter, existing)) {
        return false;  // Already committed
    }
    
    // Create and store commitment
    VoteCommitment commitment(
        disputeId,
        voter,
        commitmentHash,
        stake,
        GetCurrentBlockHeight()
    );
    
    return StoreCommitment(commitment);
}

bool CommitRevealManager::RevealVote(const uint256& disputeId,
                                      const uint160& voter,
                                      bool vote,
                                      const uint256& nonce)
{
    // Validate inputs
    if (disputeId.IsNull() || voter.IsNull()) {
        return false;
    }
    
    // Check if we're in reveal phase
    if (!IsRevealPhase(disputeId)) {
        return false;
    }
    
    // Get existing commitment
    VoteCommitment commitment;
    if (!GetCommitment(disputeId, voter, commitment)) {
        return false;  // No commitment found
    }
    
    // Check if can reveal
    if (!commitment.CanReveal()) {
        return false;  // Already revealed or forfeited
    }
    
    // Verify the hash matches
    if (!VerifyCommitment(commitment.commitmentHash, vote, nonce)) {
        return false;  // Hash mismatch
    }
    
    // Update commitment with revealed data
    commitment.revealed = true;
    commitment.vote = vote;
    commitment.nonce = nonce;
    commitment.revealTime = GetCurrentBlockHeight();
    
    return UpdateCommitment(commitment);
}

bool CommitRevealManager::IsCommitPhase(const uint256& disputeId) const
{
    uint32_t commitPhaseStart = 0;
    uint32_t revealPhaseStart = 0;
    bool useCommitReveal = false;
    
    if (!GetDisputeInfo(disputeId, commitPhaseStart, revealPhaseStart, useCommitReveal)) {
        return false;
    }
    
    if (!useCommitReveal) {
        return false;  // This dispute doesn't use commit-reveal
    }
    
    uint32_t currentHeight = GetCurrentBlockHeight();
    
    // Commit phase: from commitPhaseStart until commitPhaseDuration blocks pass
    uint32_t commitPhaseEnd = commitPhaseStart + config.commitPhaseDuration;
    
    return currentHeight >= commitPhaseStart && currentHeight < commitPhaseEnd;
}

bool CommitRevealManager::IsRevealPhase(const uint256& disputeId) const
{
    uint32_t commitPhaseStart = 0;
    uint32_t revealPhaseStart = 0;
    bool useCommitReveal = false;
    
    if (!GetDisputeInfo(disputeId, commitPhaseStart, revealPhaseStart, useCommitReveal)) {
        return false;
    }
    
    if (!useCommitReveal) {
        return false;  // This dispute doesn't use commit-reveal
    }
    
    uint32_t currentHeight = GetCurrentBlockHeight();
    
    // Reveal phase starts after commit phase ends
    uint32_t commitPhaseEnd = commitPhaseStart + config.commitPhaseDuration;
    uint32_t revealPhaseEnd = commitPhaseEnd + config.revealPhaseDuration;
    
    return currentHeight >= commitPhaseEnd && currentHeight < revealPhaseEnd;
}

std::vector<VoteCommitment> CommitRevealManager::GetCommitments(const uint256& disputeId) const
{
    std::vector<VoteCommitment> result;
    
    // Get list of voters who committed to this dispute
    std::string indexKey = DB_COMMITMENTS_BY_DISPUTE_PREFIX + disputeId.GetHex();
    std::vector<uint8_t> indexData;
    
    if (!database.ReadGeneric(indexKey, indexData)) {
        return result;  // No commitments for this dispute
    }
    
    // Deserialize list of voter addresses
    CDataStream ss(indexData, SER_DISK, CLIENT_VERSION);
    std::vector<uint160> voters;
    ss >> voters;
    
    // Load each commitment
    for (const auto& voter : voters) {
        VoteCommitment commitment;
        if (GetCommitment(disputeId, voter, commitment)) {
            result.push_back(commitment);
        }
    }
    
    return result;
}

bool CommitRevealManager::GetCommitment(const uint256& disputeId, const uint160& voter,
                                         VoteCommitment& commitment) const
{
    std::string key = DB_COMMITMENT_PREFIX + disputeId.GetHex() + "_" + voter.GetHex();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        return false;
    }
    
    CDataStream ss(data, SER_DISK, CLIENT_VERSION);
    ss >> commitment;
    
    return true;
}

CAmount CommitRevealManager::ForfeitUnrevealedStakes(const uint256& disputeId)
{
    CAmount totalForfeited = 0;
    
    std::vector<VoteCommitment> commitments = GetCommitments(disputeId);
    
    for (auto& commitment : commitments) {
        if (!commitment.revealed && !commitment.forfeited) {
            // Mark as forfeited
            commitment.forfeited = true;
            totalForfeited += commitment.stake;
            
            // Update in database
            UpdateCommitment(commitment);
        }
    }
    
    return totalForfeited;
}

uint32_t CommitRevealManager::GetCurrentBlockHeight() const
{
    // Get current chain height from validation
    LOCK(cs_main);
    return chainActive.Height();
}

bool CommitRevealManager::StoreCommitment(const VoteCommitment& commitment)
{
    // Store the commitment
    std::string key = DB_COMMITMENT_PREFIX + commitment.disputeId.GetHex() + 
                      "_" + commitment.voter.GetHex();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << commitment;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    if (!database.WriteGeneric(key, data)) {
        return false;
    }
    
    // Update the index of voters for this dispute
    std::string indexKey = DB_COMMITMENTS_BY_DISPUTE_PREFIX + commitment.disputeId.GetHex();
    std::vector<uint8_t> indexData;
    std::vector<uint160> voters;
    
    if (database.ReadGeneric(indexKey, indexData)) {
        CDataStream indexSs(indexData, SER_DISK, CLIENT_VERSION);
        indexSs >> voters;
    }
    
    // Add voter if not already in list
    bool found = false;
    for (const auto& v : voters) {
        if (v == commitment.voter) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        voters.push_back(commitment.voter);
        
        CDataStream newIndexSs(SER_DISK, CLIENT_VERSION);
        newIndexSs << voters;
        std::vector<uint8_t> newIndexData(newIndexSs.begin(), newIndexSs.end());
        
        if (!database.WriteGeneric(indexKey, newIndexData)) {
            return false;
        }
    }
    
    return true;
}

bool CommitRevealManager::UpdateCommitment(const VoteCommitment& commitment)
{
    std::string key = DB_COMMITMENT_PREFIX + commitment.disputeId.GetHex() + 
                      "_" + commitment.voter.GetHex();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << commitment;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    return database.WriteGeneric(key, data);
}

bool CommitRevealManager::GetDisputeInfo(const uint256& disputeId, 
                                          uint32_t& commitPhaseStart,
                                          uint32_t& revealPhaseStart,
                                          bool& useCommitReveal) const
{
    // Read dispute from database
    std::string key = "dispute_" + disputeId.GetHex();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        return false;
    }
    
    // Deserialize dispute
    CDataStream ss(data, SER_DISK, CLIENT_VERSION);
    DAODispute dispute;
    ss >> dispute;
    
    // For now, use createdTime as commitPhaseStart
    // The extended DAODispute struct will have explicit fields
    commitPhaseStart = dispute.createdTime;
    revealPhaseStart = commitPhaseStart + config.commitPhaseDuration;
    
    // Check if this dispute uses commit-reveal
    // For backward compatibility, assume new disputes use it if config enables it
    useCommitReveal = config.enableCommitReveal;
    
    return true;
}

} // namespace CVM
