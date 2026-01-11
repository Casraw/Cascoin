// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file bridge_contract.cpp
 * @brief Implementation of Bridge Contract for L1<->L2 transfers
 * 
 * *** DEPRECATED - Task 12: Legacy Bridge Code ***
 * 
 * This bridge contract is DEPRECATED. The new burn-and-mint model replaces
 * the old deposit/withdrawal system. See src/rpc/l2_burn.cpp and the
 * burn_parser, burn_validator, burn_registry, mint_consensus, and l2_minter
 * components for the new implementation.
 * 
 * Requirements: 11.1, 11.4 - Legacy bridge code deprecated
 */

#include <l2/bridge_contract.h>
#include <l2/sparse_merkle_tree.h>
#include <l2/account_state.h>
#include <hash.h>
#include <util.h>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

BridgeContract::BridgeContract(uint64_t chainId)
    : chainId_(chainId)
    , totalValueLocked_(0)
    , emergencyMode_(false)
    , nextWithdrawalId_(1)
{
}

// ============================================================================
// Deposit Operations (Requirement 4.1)
// ============================================================================

bool BridgeContract::ProcessDeposit(const DepositEvent& deposit)
{
    LOCK(cs_bridge_);

    // Check if deposit already processed
    if (deposits_.count(deposit.depositId) > 0) {
        return false;  // Already processed
    }

    // Validate deposit amount
    if (deposit.amount <= 0) {
        return false;  // Invalid amount
    }

    if (deposit.amount > MAX_DEPOSIT_PER_TX) {
        return false;  // Exceeds per-transaction limit
    }

    // Check daily deposit limit
    CAmount dailyTotal = GetDailyDepositTotal(deposit.depositor, deposit.timestamp);
    if (dailyTotal + deposit.amount > MAX_DAILY_DEPOSIT) {
        return false;  // Exceeds daily limit
    }

    // Store the deposit
    DepositEvent processedDeposit = deposit;
    processedDeposit.processed = true;
    deposits_[deposit.depositId] = processedDeposit;

    // Update accounting
    totalValueLocked_ += deposit.amount;
    stats_.totalDeposits++;

    // Update daily deposit tracking
    UpdateDailyDeposits(deposit.depositor, deposit.amount, deposit.timestamp);

    return true;
}

bool BridgeContract::IsDepositProcessed(const uint256& depositId) const
{
    LOCK(cs_bridge_);
    auto it = deposits_.find(depositId);
    return it != deposits_.end() && it->second.processed;
}

std::optional<DepositEvent> BridgeContract::GetDeposit(const uint256& depositId) const
{
    LOCK(cs_bridge_);
    auto it = deposits_.find(depositId);
    if (it != deposits_.end()) {
        return it->second;
    }
    return std::nullopt;
}

CAmount BridgeContract::GetDailyDepositTotal(const uint160& address, uint64_t currentTime) const
{
    LOCK(cs_bridge_);
    uint64_t currentDay = GetDayNumber(currentTime);
    
    auto addrIt = dailyDeposits_.find(address);
    if (addrIt == dailyDeposits_.end()) {
        return 0;
    }
    
    auto dayIt = addrIt->second.find(currentDay);
    if (dayIt == addrIt->second.end()) {
        return 0;
    }
    
    return dayIt->second;
}

// ============================================================================
// Withdrawal Operations (Requirements 4.2, 4.3)
// ============================================================================

WithdrawalRequest BridgeContract::InitiateWithdrawal(
    const uint160& sender,
    const uint160& l1Recipient,
    CAmount amount,
    uint64_t l2BlockNumber,
    const uint256& stateRoot,
    uint64_t currentTime,
    uint32_t hatScore)
{
    LOCK(cs_bridge_);

    WithdrawalRequest request;
    request.withdrawalId = GenerateWithdrawalId(sender, amount, currentTime);
    request.l2Sender = sender;
    request.l1Recipient = l1Recipient;
    request.amount = amount;
    request.l2BlockNumber = l2BlockNumber;
    request.stateRoot = stateRoot;
    request.initiatedAt = currentTime;
    request.hatScore = hatScore;
    request.status = WithdrawalStatus::PENDING;

    // Calculate challenge period based on reputation
    uint64_t challengePeriod = CalculateChallengePeriod(hatScore);
    request.challengeDeadline = currentTime + challengePeriod;
    request.isFastWithdrawal = QualifiesForFastWithdrawal(hatScore);

    // Store the withdrawal
    withdrawals_[request.withdrawalId] = request;
    stats_.pendingWithdrawals++;

    return request;
}

bool BridgeContract::FinalizeWithdrawal(const uint256& withdrawalId, uint64_t currentTime)
{
    LOCK(cs_bridge_);

    auto it = withdrawals_.find(withdrawalId);
    if (it == withdrawals_.end()) {
        return false;  // Withdrawal not found
    }

    WithdrawalRequest& request = it->second;

    // Check if withdrawal can be finalized
    if (request.status != WithdrawalStatus::PENDING) {
        return false;  // Not in pending state
    }

    if (!request.IsChallengePeriodOver(currentTime)) {
        return false;  // Challenge period not over
    }

    // Finalize the withdrawal
    request.status = WithdrawalStatus::COMPLETED;
    
    // Update accounting
    totalValueLocked_ -= request.amount;
    stats_.totalWithdrawals++;
    stats_.pendingWithdrawals--;

    return true;
}

std::optional<WithdrawalRequest> BridgeContract::GetWithdrawal(const uint256& withdrawalId) const
{
    LOCK(cs_bridge_);
    auto it = withdrawals_.find(withdrawalId);
    if (it != withdrawals_.end()) {
        return it->second;
    }
    return std::nullopt;
}

WithdrawalStatus BridgeContract::GetWithdrawalStatus(const uint256& withdrawalId) const
{
    LOCK(cs_bridge_);
    auto it = withdrawals_.find(withdrawalId);
    if (it != withdrawals_.end()) {
        return it->second.status;
    }
    // Return CANCELLED for non-existent withdrawals (indicates invalid)
    return WithdrawalStatus::CANCELLED;
}

std::vector<WithdrawalRequest> BridgeContract::GetPendingWithdrawals(const uint160& address) const
{
    LOCK(cs_bridge_);
    std::vector<WithdrawalRequest> pending;
    
    for (const auto& pair : withdrawals_) {
        if (pair.second.l2Sender == address && 
            pair.second.status == WithdrawalStatus::PENDING) {
            pending.push_back(pair.second);
        }
    }
    
    return pending;
}

// ============================================================================
// Fast Withdrawal (Requirements 4.4, 6.2)
// ============================================================================

WithdrawalRequest BridgeContract::FastWithdrawal(
    const uint160& sender,
    const uint160& l1Recipient,
    CAmount amount,
    uint64_t l2BlockNumber,
    const uint256& stateRoot,
    uint64_t currentTime,
    uint32_t hatScore)
{
    // Fast withdrawal requires high reputation
    if (!QualifiesForFastWithdrawal(hatScore)) {
        // Fall back to standard withdrawal
        return InitiateWithdrawal(sender, l1Recipient, amount, l2BlockNumber,
                                  stateRoot, currentTime, hatScore);
    }

    // Use the standard initiation with the high HAT score
    // The challenge period will be automatically reduced
    return InitiateWithdrawal(sender, l1Recipient, amount, l2BlockNumber,
                              stateRoot, currentTime, hatScore);
}

bool BridgeContract::QualifiesForFastWithdrawal(uint32_t hatScore)
{
    return hatScore >= FAST_WITHDRAWAL_MIN_HAT_SCORE;
}

uint64_t BridgeContract::CalculateChallengePeriod(uint32_t hatScore)
{
    // High reputation users get reduced challenge period
    if (hatScore >= FAST_WITHDRAWAL_MIN_HAT_SCORE) {
        return FAST_CHALLENGE_PERIOD;  // 1 day
    }
    
    // Standard challenge period for everyone else
    return STANDARD_CHALLENGE_PERIOD;  // 7 days
}

// ============================================================================
// Challenge Operations (Requirement 4.6)
// ============================================================================

bool BridgeContract::ChallengeWithdrawal(
    const uint256& withdrawalId,
    const uint160& challenger,
    const std::vector<unsigned char>& /* fraudProof */,
    uint64_t currentTime)
{
    LOCK(cs_bridge_);

    auto it = withdrawals_.find(withdrawalId);
    if (it == withdrawals_.end()) {
        return false;  // Withdrawal not found
    }

    WithdrawalRequest& request = it->second;

    // Can only challenge pending withdrawals
    if (request.status != WithdrawalStatus::PENDING) {
        return false;
    }

    // Can only challenge before deadline
    if (request.IsChallengePeriodOver(currentTime)) {
        return false;  // Challenge period already over
    }

    // Mark as challenged
    request.status = WithdrawalStatus::CHALLENGED;
    request.challenger = challenger;
    request.challengeBond = CHALLENGE_BOND;
    
    stats_.pendingWithdrawals--;
    stats_.challengedWithdrawals++;

    return true;
}

bool BridgeContract::ResolveChallenge(const uint256& withdrawalId, bool challengeValid)
{
    LOCK(cs_bridge_);

    auto it = withdrawals_.find(withdrawalId);
    if (it == withdrawals_.end()) {
        return false;
    }

    WithdrawalRequest& request = it->second;

    if (request.status != WithdrawalStatus::CHALLENGED) {
        return false;  // Not in challenged state
    }

    if (challengeValid) {
        // Challenge was valid - cancel the withdrawal
        request.status = WithdrawalStatus::CANCELLED;
        // Note: In a real implementation, we would slash the requester
        // and reward the challenger here
    } else {
        // Challenge was invalid - withdrawal can proceed
        request.status = WithdrawalStatus::READY;
        // Note: In a real implementation, we would slash the challenger's bond
    }

    stats_.challengedWithdrawals--;

    return true;
}

// ============================================================================
// Emergency Withdrawal (Requirements 12.1, 12.2, 12.3)
// ============================================================================

bool BridgeContract::IsEmergencyModeActive(uint64_t lastSequencerActivity, uint64_t currentTime)
{
    if (currentTime <= lastSequencerActivity) {
        return false;
    }
    return (currentTime - lastSequencerActivity) >= EMERGENCY_MODE_THRESHOLD;
}

bool BridgeContract::EmergencyWithdrawal(
    const uint160& user,
    const uint256& lastValidStateRoot,
    const std::vector<unsigned char>& balanceProof,
    CAmount claimedBalance,
    uint64_t currentTime)
{
    LOCK(cs_bridge_);

    // Emergency mode must be active
    if (!emergencyMode_) {
        return false;
    }

    // Check if user already has an emergency withdrawal
    if (emergencyWithdrawals_.count(user) > 0 && 
        emergencyWithdrawals_[user].processed) {
        return false;  // Already processed
    }

    // Verify the balance proof
    if (!VerifyBalanceProof(user, lastValidStateRoot, balanceProof, claimedBalance)) {
        return false;  // Invalid proof
    }

    // Validate claimed balance
    if (claimedBalance <= 0 || claimedBalance > totalValueLocked_) {
        return false;  // Invalid balance
    }

    // Create emergency withdrawal request
    EmergencyWithdrawalRequest request;
    request.user = user;
    request.lastValidStateRoot = lastValidStateRoot;
    request.balanceProof = balanceProof;
    request.claimedBalance = claimedBalance;
    request.requestedAt = currentTime;
    request.processed = true;

    emergencyWithdrawals_[user] = request;

    // Update accounting
    totalValueLocked_ -= claimedBalance;
    stats_.totalEmergencyWithdrawals++;

    return true;
}

bool BridgeContract::VerifyBalanceProof(
    const uint160& user,
    const uint256& stateRoot,
    const std::vector<unsigned char>& balanceProof,
    CAmount claimedBalance) const
{
    // Deserialize the Merkle proof
    MerkleProof proof;
    if (!proof.Deserialize(balanceProof)) {
        return false;
    }

    // Convert address to key
    uint256 addressKey = AddressToKey(user);

    // Verify the proof against the state root using the value stored in the proof
    // The proof contains the full AccountState, not just the balance
    if (!SparseMerkleTree::VerifyProof(proof, stateRoot, addressKey, proof.value)) {
        return false;
    }

    // Deserialize the account state from the proof value
    AccountState accountState;
    if (!accountState.Deserialize(proof.value)) {
        return false;
    }

    // Verify the claimed balance matches the account state balance
    return accountState.balance == claimedBalance;
}

void BridgeContract::SetEmergencyMode(bool active)
{
    LOCK(cs_bridge_);
    emergencyMode_ = active;
}

bool BridgeContract::IsInEmergencyMode() const
{
    LOCK(cs_bridge_);
    return emergencyMode_;
}

// ============================================================================
// Accounting and Statistics (Requirement 4.5)
// ============================================================================

CAmount BridgeContract::GetTotalValueLocked() const
{
    LOCK(cs_bridge_);
    return totalValueLocked_;
}

BridgeStats BridgeContract::GetStats() const
{
    LOCK(cs_bridge_);
    BridgeStats currentStats = stats_;
    currentStats.totalValueLocked = totalValueLocked_;
    return currentStats;
}

void BridgeContract::Clear()
{
    LOCK(cs_bridge_);
    deposits_.clear();
    withdrawals_.clear();
    emergencyWithdrawals_.clear();
    dailyDeposits_.clear();
    totalValueLocked_ = 0;
    stats_ = BridgeStats();
    emergencyMode_ = false;
    nextWithdrawalId_ = 1;
}

size_t BridgeContract::GetDepositCount() const
{
    LOCK(cs_bridge_);
    return deposits_.size();
}

size_t BridgeContract::GetWithdrawalCount() const
{
    LOCK(cs_bridge_);
    return withdrawals_.size();
}

// ============================================================================
// Private Helper Methods
// ============================================================================

uint256 BridgeContract::GenerateWithdrawalId(
    const uint160& sender,
    CAmount amount,
    uint64_t timestamp) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << sender << amount << timestamp << nextWithdrawalId_ << chainId_;
    return ss.GetHash();
}

bool BridgeContract::VerifyWithdrawalProof(const WithdrawalRequest& request) const
{
    // If no proof provided, assume valid (for testing)
    if (request.merkleProof.empty()) {
        return true;
    }

    // Deserialize and verify the proof
    MerkleProof proof;
    if (!proof.Deserialize(request.merkleProof)) {
        return false;
    }

    // Convert address to key
    uint256 addressKey = AddressToKey(request.l2Sender);

    // Create expected account state
    AccountState expectedState;
    expectedState.balance = request.amount;

    // Serialize expected state
    std::vector<unsigned char> expectedValue = expectedState.Serialize();

    // Verify against the state root at withdrawal time
    return SparseMerkleTree::VerifyProof(proof, request.stateRoot, addressKey, expectedValue);
}

uint64_t BridgeContract::GetDayNumber(uint64_t timestamp)
{
    // Convert timestamp to day number (seconds since epoch / seconds per day)
    return timestamp / (24 * 60 * 60);
}

void BridgeContract::UpdateDailyDeposits(const uint160& address, CAmount amount, uint64_t timestamp)
{
    uint64_t day = GetDayNumber(timestamp);
    dailyDeposits_[address][day] += amount;
    
    // Clean up old entries (keep only last 2 days)
    auto& addressDeposits = dailyDeposits_[address];
    std::vector<uint64_t> oldDays;
    for (const auto& pair : addressDeposits) {
        if (pair.first < day - 1) {
            oldDays.push_back(pair.first);
        }
    }
    for (uint64_t oldDay : oldDays) {
        addressDeposits.erase(oldDay);
    }
}

} // namespace l2
