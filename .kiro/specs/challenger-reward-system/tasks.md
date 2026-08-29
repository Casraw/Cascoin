# Implementation Plan: Challenger Reward System

## Overview

This implementation plan breaks down the Challenger Reward System into discrete coding tasks. The system extends the existing Web-of-Trust DAO dispute mechanism with reward distribution and commit-reveal voting. Implementation follows the existing Cascoin codebase patterns using C++17, LevelDB for storage, and Boost.Test for testing.

## Tasks

- [x] 1. Extend WoTConfig with reward parameters
  - [x] 1.1 Add reward percentage fields to WoTConfig struct in `src/cvm/trustgraph.h`
    - Add `challengerRewardPercent` (default 50)
    - Add `daoVoterRewardPercent` (default 30)
    - Add `burnPercent` (default 20)
    - Add `wronglyAccusedRewardPercent` (default 70)
    - Add `failedChallengeBurnPercent` (default 30)
    - Add `commitPhaseDuration` (default 720 blocks)
    - Add `revealPhaseDuration` (default 720 blocks)
    - Add `enableCommitReveal` (default true)
    - Add `ValidateRewardPercentages()` method
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6_
  
  - [x] 1.2 Write property test for percentage validation
    - **Property 4: Reward Percentage Validation**
    - **Validates: Requirements 4.6**

- [x] 2. Implement PendingReward and RewardDistribution data structures
  - [x] 2.1 Create `src/cvm/reward_types.h` with data structures
    - Define `PendingReward` struct with rewardId, disputeId, recipient, amount, type, timestamps, claimed flag
    - Define `RewardType` enum (CHALLENGER_BOND_RETURN, CHALLENGER_BOUNTY, DAO_VOTER_REWARD, WRONGLY_ACCUSED_COMPENSATION)
    - Define `RewardDistribution` struct with full distribution breakdown
    - Add serialization methods for LevelDB storage
    - _Requirements: 3.1, 3.2, 6.4_
  
  - [x] 2.2 Write unit tests for reward type serialization
    - Test round-trip serialization of PendingReward
    - Test round-trip serialization of RewardDistribution
    - _Requirements: 3.2_

- [x] 3. Implement VoteCommitment data structure and CommitRevealManager
  - [x] 3.1 Create `src/cvm/commit_reveal.h` with VoteCommitment struct
    - Define `VoteCommitment` struct with disputeId, voter, commitmentHash, stake, timestamps, revealed flag
    - Add serialization methods
    - _Requirements: 8.1, 8.7_
  
  - [x] 3.2 Create `src/cvm/commit_reveal.cpp` with CommitRevealManager class
    - Implement `SubmitCommitment()` - store commitment hash and stake
    - Implement `RevealVote()` - verify hash and record revealed vote
    - Implement `CalculateCommitmentHash()` - SHA256(vote || nonce)
    - Implement `IsCommitPhase()` and `IsRevealPhase()` based on block height
    - Implement `GetCommitments()` - retrieve all commitments for dispute
    - Implement `ForfeitUnrevealedStakes()` - mark unrevealed as forfeited
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7_
  
  - [x] 3.3 Write property test for commit-reveal hash integrity
    - **Property 7: Commit-Reveal Hash Integrity (Round-Trip)**
    - **Validates: Requirements 8.1, 8.4, 8.7**
  
  - [x] 3.4 Write property test for phase transitions
    - **Property 8: Phase Transition Correctness**
    - **Validates: Requirements 8.2, 8.3**

- [x] 4. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement RewardDistributor class
  - [x] 5.1 Create `src/cvm/reward_distributor.h` with class declaration
    - Declare `RewardDistributor` class with CVMDatabase and WoTConfig references
    - Declare public methods: `DistributeSlashRewards()`, `DistributeFailedChallengeRewards()`, `GetPendingRewards()`, `ClaimReward()`, `GetRewardDistribution()`
    - Declare private helpers: `CalculateVoterRewards()`, `StorePendingReward()`, `EmitRewardEvent()`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 2.1, 2.2, 2.3, 3.1, 3.3, 3.4, 3.5_
  
  - [x] 5.2 Implement `DistributeSlashRewards()` in `src/cvm/reward_distributor.cpp`
    - Calculate challenger bond return (100% of original bond)
    - Calculate challenger bounty (configurable % of slashed bond)
    - Calculate DAO voter rewards (configurable % of slashed bond, proportional to stakes)
    - Calculate burn amount (configurable % of slashed bond + rounding remainder)
    - Store pending rewards for challenger and winning voters
    - Store RewardDistribution record
    - Emit reward events
    - Handle edge case: no voters on winning side (give voter portion to challenger)
    - Handle edge case: all voters on losing side (burn voter portion)
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 5.1, 5.2, 5.4, 5.5, 6.1, 6.2, 6.4_
  
  - [x] 5.3 Implement `DistributeFailedChallengeRewards()` in `src/cvm/reward_distributor.cpp`
    - Forfeit challenger bond (no return)
    - Calculate wrongly accused compensation (configurable % of forfeited bond)
    - Calculate burn amount (remaining %)
    - Store pending reward for wrongly accused voter
    - Handle edge case: invalid voter address (burn entire bond)
    - Emit reward events
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 6.1, 6.2_
  
  - [x] 5.4 Implement `GetPendingRewards()` and `ClaimReward()` in `src/cvm/reward_distributor.cpp`
    - Query database for all unclaimed rewards by recipient
    - Verify claim authorization (recipient matches)
    - Mark reward as claimed with timestamp
    - Prevent double-claiming
    - Emit claim event
    - _Requirements: 3.3, 3.4, 3.5, 6.3_
  
  - [x] 5.5 Write property test for conservation of funds (slash)
    - **Property 1: Conservation of Funds (Slash Decision)**
    - **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 5.4, 5.5**
  
  - [x] 5.6 Write property test for conservation of funds (failed challenge)
    - **Property 2: Conservation of Funds (Failed Challenge)**
    - **Validates: Requirements 2.1, 2.2, 2.3**
  
  - [x] 5.7 Write property test for proportional voter rewards
    - **Property 3: Proportional Voter Reward Distribution**
    - **Validates: Requirements 1.3, 1.5**
  
  - [x] 5.8 Write property test for claim idempotence
    - **Property 5: Claim Idempotence**
    - **Validates: Requirements 3.3, 3.4**
  
  - [x] 5.9 Write property test for pending rewards completeness
    - **Property 6: Pending Rewards Query Completeness**
    - **Validates: Requirements 3.5**

- [x] 6. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Extend DAODispute struct and integrate with TrustGraph
  - [x] 7.1 Extend DAODispute struct in `src/cvm/trustgraph.h`
    - Add `commitPhaseStart` field
    - Add `revealPhaseStart` field
    - Add `useCommitReveal` flag
    - Add `rewardsDistributed` flag
    - Add `rewardDistributionId` field
    - Update serialization
    - _Requirements: 8.2, 8.3, 9.3_
  
  - [x] 7.2 Integrate RewardDistributor into TrustGraph in `src/cvm/trustgraph.cpp`
    - Add `rewardDistributor` member to TrustGraph
    - Add `commitRevealManager` member to TrustGraph
    - Modify `ResolveDispute()` to call `DistributeSlashRewards()` or `DistributeFailedChallengeRewards()`
    - Add `GetDisputedVote()` helper method
    - Handle backward compatibility for legacy disputes
    - _Requirements: 9.1, 9.3, 9.4, 9.5_
  
  - [x] 7.3 Write property test for non-reveal stake forfeiture
    - **Property 9: Non-Reveal Stake Forfeiture**
    - **Validates: Requirements 8.5, 8.6**
  
  - [x] 7.4 Write property test for only revealed votes count
    - **Property 10: Only Revealed Votes Count**
    - **Validates: Requirements 8.6**

- [x] 8. Implement RPC commands
  - [x] 8.1 Implement `getpendingrewards` RPC in `src/rpc/cvm.cpp`
    - Accept address parameter
    - Return array of pending rewards with id, amount, type, disputeId
    - _Requirements: 7.1_
  
  - [x] 8.2 Implement `claimreward` RPC in `src/rpc/cvm.cpp`
    - Accept reward_id and from_address parameters
    - Verify ownership and claim reward
    - User pays transaction fee for claim
    - Return claim transaction details
    - _Requirements: 7.2, 3.6_
  
  - [x] 8.3 Implement `claimallrewards` RPC in `src/rpc/cvm.cpp`
    - Accept from_address parameter
    - Batch-claim all pending rewards for address in single transaction
    - User pays single transaction fee for all claims
    - Return list of claimed rewards and transaction details
    - _Requirements: 7.2, 3.7_
  
  - [x] 8.4 Implement `getrewarddistribution` RPC in `src/rpc/cvm.cpp`
    - Accept dispute_id parameter
    - Return full breakdown: challenger amounts, voter rewards map, burn amount
    - _Requirements: 7.3_
  
  - [x] 8.5 Implement `commitdisputevote` RPC in `src/rpc/cvm.cpp`
    - Accept dispute_id, commitment_hash, stake, from_address parameters
    - Verify DAO membership and commit phase
    - Store commitment
    - _Requirements: 8.1, 8.7_
  
  - [x] 8.6 Implement `revealdisputevote` RPC in `src/rpc/cvm.cpp`
    - Accept dispute_id, vote, nonce, from_address parameters
    - Verify reveal phase and hash match
    - Record revealed vote
    - _Requirements: 8.4_
  
  - [x] 8.7 Extend `getdispute` RPC to include reward distribution
    - Add reward distribution fields to response if dispute is resolved
    - _Requirements: 7.4_
  
  - [x] 8.8 Write unit tests for RPC commands
    - Test `getpendingrewards` returns correct data
    - Test `claimreward` success and failure cases
    - Test `claimallrewards` batch claiming
    - Test `getrewarddistribution` returns complete breakdown
    - Test `commitdisputevote` and `revealdisputevote` flow
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

- [x] 9. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Update Makefile and integrate components
  - [x] 10.1 Update `src/Makefile.am` to include new source files
    - Add `cvm/reward_types.h`
    - Add `cvm/commit_reveal.h` and `cvm/commit_reveal.cpp`
    - Add `cvm/reward_distributor.h` and `cvm/reward_distributor.cpp`
    - _Requirements: 9.2_
  
  - [x] 10.2 Update `src/test/Makefile.am` to include test files
    - Add test files for reward system
    - _Requirements: 9.2_

- [x] 11. Add dashboard UI for reward claiming
  - [x] 11.1 Extend dispute detail view in `src/httpserver/l2_dashboard.cpp` (or create new dashboard component)
    - Show pending rewards section when viewing a resolved dispute
    - Display reward breakdown: challenger bounty, voter rewards, burned amount
    - Add "Claim Reward" button for each pending reward
    - Show claim status (pending/claimed) with timestamps
    - _Requirements: 7.1, 7.2, 7.3, 7.4_
  
  - [x] 11.2 Add rewards overview page to dashboard
    - List all pending rewards for the connected wallet
    - Show total claimable amount
    - Allow batch claiming of multiple rewards
    - Display claim history
    - _Requirements: 7.1, 7.2_

- [x] 12. Write edge case unit tests
  - [x] 12.1 Write unit tests for edge cases
    - Test no voters on winning side (voter portion goes to challenger)
    - Test all voters on losing side (voter portion burned)
    - Test zero slashed bond (challenger bond returned, no bounty)
    - Test invalid voter address (entire bond burned)
    - Test legacy dispute backward compatibility
    - Test pre-reward-system dispute returns empty data
    - _Requirements: 5.1, 5.2, 5.3, 2.4, 9.3, 9.4, 9.5_

- [x] 13. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- All tasks are required (comprehensive testing enabled)
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties (100+ iterations each)
- Unit tests validate specific examples and edge cases
- The implementation uses existing Cascoin patterns: LevelDB for storage, Boost.Test for testing, Bitcoin-style serialization
