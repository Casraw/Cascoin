# Requirements Document

## Introduction

This document specifies the requirements for implementing a Challenger Reward System for the Cascoin Web-of-Trust DAO dispute mechanism. The current implementation has a critical gap: when a DAO dispute is resolved and a malicious vote is slashed, the challenger who initiated the dispute receives no reward. The slashed bond is effectively "burned" with no distribution, creating broken incentive structures that rely on pure altruism rather than economic incentives.

The Challenger Reward System will implement a reward distribution mechanism similar to Kleros/Augur, where successful challengers receive bounties, DAO voters on the winning side receive proportional rewards, and a portion is burned for deflation.

## Glossary

- **Challenger**: The address that initiates a DAO dispute by staking a challenge bond
- **Challenge_Bond**: The amount of CAS staked by the challenger to initiate a dispute
- **DAO_Member**: An address meeting DAO membership requirements (reputation ≥70, stake ≥100 CAS, recent activity)
- **DAO_Voter**: A DAO member who votes on a dispute
- **Slashed_Bond**: The bond amount confiscated from a malicious voter when a dispute resolves in favor of slashing
- **Winning_Side**: The side (slash or keep) that wins the dispute based on stake-weighted voting
- **Losing_Side**: The side that loses the dispute
- **Pending_Reward**: A reward that has been calculated but not yet claimed by the recipient
- **Reward_Pool**: The total amount available for distribution after a successful slash decision
- **TrustGraph**: The Web-of-Trust system component managing trust relationships and disputes
- **WoTConfig**: Configuration structure containing Web-of-Trust system parameters
- **Commit_Phase**: The period during which DAO voters submit encrypted vote commitments
- **Reveal_Phase**: The period during which DAO voters reveal their actual votes and verify against commitments
- **Commitment_Hash**: A hash of (vote_direction + secret_nonce) that hides the vote until reveal
- **Bandwagoning**: The tendency for voters to follow the majority without independent evaluation

## Requirements

### Requirement 1: Successful Challenge Reward Distribution

**User Story:** As a challenger, I want to receive a reward when my dispute succeeds, so that I have financial incentive to report malicious votes.

#### Acceptance Criteria

1. WHEN a dispute resolves with a slash decision, THE Reward_System SHALL return the challenger's original Challenge_Bond to the challenger
2. WHEN a dispute resolves with a slash decision, THE Reward_System SHALL allocate a configurable percentage (default 50%) of the Slashed_Bond to the challenger as a bounty
3. WHEN a dispute resolves with a slash decision, THE Reward_System SHALL allocate a configurable percentage (default 30%) of the Slashed_Bond to DAO_Voters on the Winning_Side proportional to their stakes
4. WHEN a dispute resolves with a slash decision, THE Reward_System SHALL burn a configurable percentage (default 20%) of the Slashed_Bond for deflation
5. WHEN calculating DAO voter rewards, THE Reward_System SHALL distribute rewards proportionally based on each voter's stake relative to total winning-side stake

### Requirement 2: Failed Challenge Penalty

**User Story:** As a system operator, I want challengers who make frivolous disputes to lose their bond, so that the system discourages spam challenges.

#### Acceptance Criteria

1. WHEN a dispute resolves without a slash decision (keep vote), THE Reward_System SHALL forfeit the challenger's Challenge_Bond
2. WHEN a challenger's bond is forfeited, THE Reward_System SHALL allocate a configurable percentage (default 70%) to the original voter who was wrongly accused
3. WHEN a challenger's bond is forfeited, THE Reward_System SHALL burn the remaining percentage (default 30%) for deflation
4. IF the original voter cannot receive the reward (invalid address), THEN THE Reward_System SHALL burn the entire forfeited Challenge_Bond

### Requirement 3: Claimable Rewards System

**User Story:** As a reward recipient, I want to claim my rewards when convenient, so that I control when funds are transferred to my wallet.

#### Acceptance Criteria

1. THE Reward_System SHALL store Pending_Rewards in the database rather than auto-sending funds
2. WHEN a reward is calculated, THE Reward_System SHALL create a Pending_Reward record with recipient address, amount, dispute ID, and reward type
3. WHEN a user claims a reward, THE Reward_System SHALL verify the claim is valid and mark the reward as claimed
4. THE Reward_System SHALL prevent double-claiming of the same reward
5. WHEN querying pending rewards, THE Reward_System SHALL return all unclaimed rewards for a given address
6. WHEN a user claims a reward, THE User SHALL pay the transaction fee (gas) for the claim transaction
7. THE Reward_System SHALL allow users to batch-claim multiple rewards in a single transaction to reduce total gas costs

### Requirement 4: Configuration Parameters

**User Story:** As a system administrator, I want to configure reward percentages, so that the economic incentives can be tuned without code changes.

#### Acceptance Criteria

1. THE WoTConfig SHALL include a challengerRewardPercent parameter with default value 50
2. THE WoTConfig SHALL include a daoVoterRewardPercent parameter with default value 30
3. THE WoTConfig SHALL include a burnPercent parameter with default value 20
4. THE WoTConfig SHALL include a failedChallengerPenaltyPercent parameter with default value 100 (full bond loss)
5. THE WoTConfig SHALL include a wronglyAccusedRewardPercent parameter with default value 70
6. WHEN reward percentages are configured, THE Reward_System SHALL validate that challengerRewardPercent + daoVoterRewardPercent + burnPercent equals 100

### Requirement 5: Edge Case Handling

**User Story:** As a system operator, I want the reward system to handle edge cases gracefully, so that funds are never lost or stuck.

#### Acceptance Criteria

1. IF no DAO_Voters voted on the Winning_Side, THEN THE Reward_System SHALL allocate the DAO voter reward portion to the challenger instead
2. IF all DAO_Voters voted on the Losing_Side, THEN THE Reward_System SHALL burn the DAO voter reward portion
3. IF the Slashed_Bond is zero or insufficient, THEN THE Reward_System SHALL still return the challenger's Challenge_Bond but skip bounty distribution
4. WHEN calculating rewards, THE Reward_System SHALL use integer arithmetic to avoid rounding errors with CAmount values
5. IF rounding leaves remainder satoshis, THEN THE Reward_System SHALL add the remainder to the burn amount

### Requirement 6: Event Logging and Transparency

**User Story:** As a user, I want to see reward distribution details, so that I can verify the system is operating fairly.

#### Acceptance Criteria

1. WHEN rewards are distributed, THE Reward_System SHALL emit a RewardDistributed event with dispute ID, recipient, amount, and reward type
2. WHEN a bond is burned, THE Reward_System SHALL emit a BondBurned event with dispute ID and amount
3. WHEN a reward is claimed, THE Reward_System SHALL emit a RewardClaimed event with recipient, amount, and dispute ID
4. THE Reward_System SHALL store a complete reward distribution record for each resolved dispute

### Requirement 7: RPC Interface

**User Story:** As a developer, I want RPC commands to interact with the reward system, so that I can build applications on top of it.

#### Acceptance Criteria

1. THE RPC_Interface SHALL provide a getpendingrewards command that returns all unclaimed rewards for an address
2. THE RPC_Interface SHALL provide a claimreward command that allows claiming a specific pending reward
3. THE RPC_Interface SHALL provide a getrewarddistribution command that returns the reward breakdown for a resolved dispute
4. WHEN a dispute is queried via getdispute, THE RPC_Interface SHALL include reward distribution details if the dispute is resolved

### Requirement 8: Commit-Reveal Voting Scheme

**User Story:** As a system designer, I want DAO voters to commit their votes before revealing them, so that voters cannot simply follow the majority (bandwagoning) and must make independent decisions.

#### Acceptance Criteria

1. WHEN a DAO_Voter votes on a dispute, THE Reward_System SHALL require a commitment hash (hash of vote + secret nonce) during the commit phase
2. THE Reward_System SHALL enforce a commit phase duration (configurable, default 720 blocks ~12 hours) during which votes are hidden
3. WHEN the commit phase ends, THE Reward_System SHALL transition the dispute to a reveal phase
4. DURING the reveal phase, THE Reward_System SHALL require voters to reveal their vote and nonce, verifying against the commitment hash
5. IF a voter fails to reveal within the reveal phase (configurable, default 720 blocks), THEN THE Reward_System SHALL forfeit that voter's stake
6. THE Reward_System SHALL only count revealed votes when calculating the dispute outcome
7. WHEN a commitment is submitted, THE Reward_System SHALL store the commitment hash and stake amount without revealing the vote direction

### Requirement 9: Integration with Existing Dispute Flow

**User Story:** As a developer, I want the reward system to integrate seamlessly with the existing dispute resolution flow, so that no code paths are broken.

#### Acceptance Criteria

1. WHEN ResolveDispute is called, THE TrustGraph SHALL call DistributeSlashRewards after determining the slash decision
2. THE Reward_System SHALL not modify the existing dispute resolution logic, only extend it
3. THE Reward_System SHALL be backward compatible with disputes created before the reward system was implemented
4. IF a dispute was resolved before the reward system existed, THEN THE Reward_System SHALL return empty reward data rather than error
5. THE Reward_System SHALL support both legacy (non-commit-reveal) and new (commit-reveal) dispute modes for backward compatibility

### Requirement 10: Dashboard User Interface

**User Story:** As a user, I want to see and claim my rewards directly in the web dashboard, so that I don't need to use command-line tools.

#### Acceptance Criteria

1. WHEN viewing a resolved dispute in the dashboard, THE Dashboard SHALL display the reward distribution breakdown
2. WHEN a user has pending rewards, THE Dashboard SHALL show a "Claim Reward" button for each claimable reward
3. THE Dashboard SHALL provide a rewards overview page listing all pending rewards for the connected wallet
4. WHEN a reward is claimed via the dashboard, THE Dashboard SHALL update the status to "claimed" and show the claim transaction
5. THE Dashboard SHALL display the total claimable amount across all pending rewards
