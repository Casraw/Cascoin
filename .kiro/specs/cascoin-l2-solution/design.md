# Cascoin L2 Solution Design Document

## Overview

Dieses Dokument beschreibt das technische Design einer nativen Layer-2 Skalierungslösung für Cascoin. Die Lösung basiert auf einem Optimistic Rollup-Ansatz, der durch das einzigartige HAT v2 Reputationssystem erweitert wird. Das Design ermöglicht permissionless Sequencer-Teilnahme, automatisches Failover, Trust-Weighted Finality und vollständige CVM-Kompatibilität.

### Design Goals

1. **Dezentralisierung**: Permissionless Sequencer-Netzwerk ohne zentrale Registrierung
2. **Sicherheit**: Fraud Proofs + Reputation-basierte Validierung
3. **Performance**: 1000+ TPS, Sub-Sekunden Blockzeiten
4. **Kompatibilität**: Volle CVM-Unterstützung, bestehende Tools funktionieren
5. **Fairness**: MEV-Protection, Censorship Resistance
6. **Nachhaltigkeit**: State Rent, effiziente Datenkompression

## Architecture

### High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              CASCOIN L2 SYSTEM                                  │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                        L2 EXECUTION LAYER                               │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │   │
│  │  │  Encrypted  │  │  Sequencer  │  │    CVM      │  │   State     │    │   │
│  │  │   Mempool   │──│  Consensus  │──│  Executor   │──│   Manager   │    │   │
│  │  │             │  │   (PBFT)    │  │             │  │             │    │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                        │                                        │
│                                        ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                      SEQUENCER NETWORK LAYER                            │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │   │
│  │  │  Sequencer  │  │   Leader    │  │  Failover   │  │  Reputation │    │   │
│  │  │  Discovery  │──│  Election   │──│   Manager   │──│   Tracker   │    │   │
│  │  │   (P2P)     │  │             │  │             │  │             │    │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                        │                                        │
│                                        ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                        DATA AVAILABILITY LAYER                          │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │   │
│  │  │    Batch    │  │    Data     │  │   Merkle    │  │  Compression│    │   │
│  │  │  Assembler  │──│  Publisher  │──│   Prover    │──│    Engine   │    │   │
│  │  │             │  │             │  │             │  │             │    │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                        │                                        │
└────────────────────────────────────────┼────────────────────────────────────────┘
                                         │
                                         ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              CASCOIN L1 (MAINCHAIN)                             │
├─────────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐            │
│  │   Bridge    │  │    L2       │  │   Fraud     │  │   State     │            │
│  │  Contract   │  │  Registry   │  │   Verifier  │  │   Anchor    │            │
│  │             │  │             │  │             │  │             │            │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘            │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Component Interaction Flow

```
User Transaction Flow:
┌──────┐    ┌───────────┐    ┌───────────┐    ┌──────────┐    ┌─────────┐
│ User │───▶│ Encrypted │───▶│ Sequencer │───▶│   CVM    │───▶│  State  │
│      │    │  Mempool  │    │  Leader   │    │ Executor │    │  Root   │
└──────┘    └───────────┘    └───────────┘    └──────────┘    └────┬────┘
                                                                    │
                                                                    ▼
┌──────┐    ┌───────────┐    ┌───────────┐    ┌──────────┐    ┌─────────┐
│ User │◀───│  Receipt  │◀───│  L2 Block │◀───│ Consensus│◀───│  Batch  │
│      │    │           │    │           │    │  (2/3)   │    │         │
└──────┘    └───────────┘    └───────────┘    └──────────┘    └────┬────┘
                                                                    │
                                                                    ▼
                                                              ┌─────────┐
                                                              │   L1    │
                                                              │ Anchor  │
                                                              └─────────┘
```

## Components and Interfaces

### 1. Sequencer Discovery and Management (`src/l2/sequencer_discovery.h/cpp`)

```cpp
// Sequencer announcement message for P2P network
struct SeqAnnounceMsg {
    uint160 sequencerAddress;      // Sequencer's address
    uint256 stakeAmount;           // Staked CAS amount
    uint32_t hatScore;             // Self-reported HAT v2 score
    uint64_t blockHeight;          // Current L1 block height
    std::vector<uint8_t> signature; // Cryptographic signature
    uint64_t timestamp;            // Announcement timestamp
    
    // Network info for connectivity
    std::string publicEndpoint;    // Optional public endpoint
    uint32_t peerCount;            // Number of connected peers
};

class SequencerDiscovery {
public:
    // Announce this node as sequencer candidate
    bool AnnounceAsSequencer(const CKey& signingKey, uint256 stakeAmount);
    
    // Process incoming sequencer announcements
    void ProcessSeqAnnounce(const SeqAnnounceMsg& msg, CNode* pfrom);
    
    // Get list of eligible sequencers
    std::vector<SequencerInfo> GetEligibleSequencers() const;
    
    // Check if address is eligible sequencer
    bool IsEligibleSequencer(const uint160& address) const;
    
    // Verify sequencer eligibility via distributed attestation
    bool VerifySequencerEligibility(const uint160& address);
    
private:
    // Local registry of known sequencers
    std::map<uint160, SequencerInfo> sequencerRegistry;
    
    // Attestation cache (like L1 validator attestations)
    std::map<uint160, std::vector<SequencerAttestation>> attestationCache;
    
    // Eligibility criteria
    static constexpr uint32_t MIN_HAT_SCORE = 70;
    static constexpr uint256 MIN_STAKE = 100 * COIN;  // 100 CAS
    static constexpr uint32_t MIN_PEER_COUNT = 1;
    
    // Verify stake on L1
    bool VerifyStakeOnL1(const uint160& address, uint256 minStake);
    
    // Request attestations from random nodes
    void RequestAttestations(const uint160& sequencerAddr);
};
```

### 2. Leader Election and Consensus (`src/l2/leader_election.h/cpp`)

```cpp
// Leader election result
struct LeaderElectionResult {
    uint160 leaderAddress;
    uint64_t slotNumber;           // Which slot this leader is for
    uint64_t validUntilBlock;      // Leader valid until this L2 block
    std::vector<uint160> backupSequencers;  // Ordered failover list
    uint256 electionSeed;          // Deterministic seed used
};

class LeaderElection {
public:
    // Deterministic leader election for given slot
    LeaderElectionResult ElectLeader(
        uint64_t slotNumber,
        const std::vector<SequencerInfo>& eligibleSequencers,
        const uint256& randomSeed
    );
    
    // Check if this node is current leader
    bool IsCurrentLeader() const;
    
    // Get current leader info
    SequencerInfo GetCurrentLeader() const;
    
    // Handle leader timeout (failover)
    void HandleLeaderTimeout(uint64_t slotNumber);
    
    // Claim leadership after timeout
    bool ClaimLeadership(const CKey& signingKey);
    
private:
    // Current election state
    LeaderElectionResult currentElection;
    
    // Timeout tracking
    std::chrono::steady_clock::time_point lastBlockTime;
    static constexpr auto LEADER_TIMEOUT = std::chrono::seconds(3);
    
    // Rotation parameters
    static constexpr uint32_t BLOCKS_PER_LEADER = 10;
    
    // Generate deterministic seed from L1 data
    uint256 GenerateElectionSeed(uint64_t slotNumber);
    
    // Weighted random selection based on reputation
    uint160 WeightedRandomSelect(
        const std::vector<SequencerInfo>& sequencers,
        const uint256& seed
    );
};
```

### 3. Sequencer Consensus Protocol (`src/l2/sequencer_consensus.h/cpp`)

```cpp
// Block proposal from leader
struct L2BlockProposal {
    uint64_t blockNumber;
    uint256 parentHash;
    uint256 stateRoot;
    uint256 transactionsRoot;
    std::vector<CTransaction> transactions;
    uint160 proposerAddress;
    uint64_t timestamp;
    std::vector<uint8_t> proposerSignature;
};

// Vote on block proposal
struct SequencerVote {
    uint256 blockHash;
    uint160 voterAddress;
    VoteType vote;  // ACCEPT, REJECT, ABSTAIN
    std::string rejectReason;  // If REJECT
    std::vector<uint8_t> signature;
};

enum class ConsensusState {
    WAITING_FOR_PROPOSAL,
    COLLECTING_VOTES,
    CONSENSUS_REACHED,
    CONSENSUS_FAILED,
    FAILOVER_IN_PROGRESS
};

class SequencerConsensus {
public:
    // Propose new L2 block (leader only)
    bool ProposeBlock(const L2BlockProposal& proposal);
    
    // Vote on received proposal
    SequencerVote VoteOnProposal(const L2BlockProposal& proposal);
    
    // Process incoming vote
    void ProcessVote(const SequencerVote& vote);
    
    // Check if consensus reached
    bool HasConsensus(const uint256& blockHash) const;
    
    // Get finalized block
    std::optional<L2Block> GetFinalizedBlock(const uint256& blockHash) const;
    
    // Handle consensus failure
    void HandleConsensusFailed(const uint256& blockHash);
    
private:
    // Current consensus state
    ConsensusState state;
    
    // Votes for current proposal
    std::map<uint160, SequencerVote> currentVotes;
    
    // Consensus threshold (2/3 of sequencers)
    static constexpr double CONSENSUS_THRESHOLD = 0.67;
    
    // Validate block proposal
    bool ValidateProposal(const L2BlockProposal& proposal);
    
    // Calculate weighted vote count
    double CalculateWeightedVotes(VoteType voteType) const;
    
    // Broadcast vote to other sequencers
    void BroadcastVote(const SequencerVote& vote);
};
```

### 4. Encrypted Mempool for MEV Protection (`src/l2/encrypted_mempool.h/cpp`)

```cpp
// Encrypted transaction wrapper
struct EncryptedTransaction {
    std::vector<uint8_t> encryptedPayload;  // Threshold-encrypted tx
    uint256 commitmentHash;                  // Hash of plaintext for ordering
    uint160 senderAddress;                   // Sender (for rate limiting)
    uint64_t nonce;                          // Sender nonce
    uint256 maxFee;                          // Max fee (visible for prioritization)
    uint64_t submissionTime;                 // When submitted
};

// Decryption share from sequencer
struct DecryptionShare {
    uint160 sequencerAddress;
    std::vector<uint8_t> share;
    std::vector<uint8_t> signature;
};

class EncryptedMempool {
public:
    // Submit encrypted transaction
    bool SubmitEncryptedTx(const EncryptedTransaction& encTx);
    
    // Get transactions for block (still encrypted)
    std::vector<EncryptedTransaction> GetTransactionsForBlock(
        uint64_t blockNumber,
        uint64_t gasLimit
    );
    
    // Contribute decryption share (sequencer)
    bool ContributeDecryptionShare(
        const uint256& txHash,
        const DecryptionShare& share
    );
    
    // Decrypt transaction when threshold reached
    std::optional<CTransaction> DecryptTransaction(const uint256& txHash);
    
    // Check if transaction can be decrypted
    bool CanDecrypt(const uint256& txHash) const;
    
private:
    // Encrypted transaction pool
    std::map<uint256, EncryptedTransaction> encryptedPool;
    
    // Decryption shares per transaction
    std::map<uint256, std::vector<DecryptionShare>> decryptionShares;
    
    // Threshold for decryption (2/3 of sequencers)
    static constexpr double DECRYPTION_THRESHOLD = 0.67;
    
    // Rate limiting per address
    std::map<uint160, RateLimitInfo> rateLimits;
    
    // Threshold decryption using Shamir's Secret Sharing
    std::vector<uint8_t> ThresholdDecrypt(
        const std::vector<DecryptionShare>& shares,
        const std::vector<uint8_t>& encryptedData
    );
    
    // Randomize transaction ordering within block
    void RandomizeOrdering(std::vector<EncryptedTransaction>& txs);
};
```

### 5. Bridge Contract Interface (`src/l2/bridge_contract.h/cpp`)

```cpp
// Deposit event from L1
struct DepositEvent {
    uint256 depositId;
    uint160 depositor;
    uint160 l2Recipient;
    uint256 amount;
    uint64_t l1BlockNumber;
    uint256 l1TxHash;
};

// Withdrawal request from L2
struct WithdrawalRequest {
    uint256 withdrawalId;
    uint160 l2Sender;
    uint160 l1Recipient;
    uint256 amount;
    uint64_t l2BlockNumber;
    uint256 stateRoot;           // State root at withdrawal time
    std::vector<uint8_t> merkleProof;  // Proof of withdrawal in state
    uint64_t challengeDeadline;  // When challenge period ends
    WithdrawalStatus status;
};

enum class WithdrawalStatus {
    PENDING,           // Waiting for challenge period
    CHALLENGED,        // Under dispute
    READY,             // Challenge period passed, ready to claim
    COMPLETED,         // Successfully withdrawn
    CANCELLED          // Cancelled due to valid challenge
};

class BridgeContract {
public:
    // Process deposit from L1 (mint on L2)
    bool ProcessDeposit(const DepositEvent& deposit);
    
    // Initiate withdrawal from L2
    WithdrawalRequest InitiateWithdrawal(
        const uint160& sender,
        const uint160& l1Recipient,
        uint256 amount
    );
    
    // Challenge a withdrawal
    bool ChallengeWithdrawal(
        const uint256& withdrawalId,
        const std::vector<uint8_t>& fraudProof
    );
    
    // Finalize withdrawal after challenge period
    bool FinalizeWithdrawal(const uint256& withdrawalId);
    
    // Fast withdrawal for high-reputation users
    bool FastWithdrawal(
        const uint160& sender,
        const uint160& l1Recipient,
        uint256 amount,
        uint32_t hatScore
    );
    
    // Get withdrawal status
    WithdrawalStatus GetWithdrawalStatus(const uint256& withdrawalId) const;
    
    // Emergency withdrawal (when L2 is down)
    bool EmergencyWithdrawal(
        const uint160& user,
        const uint256& lastValidStateRoot,
        const std::vector<uint8_t>& balanceProof
    );
    
private:
    // Pending withdrawals
    std::map<uint256, WithdrawalRequest> pendingWithdrawals;
    
    // Challenge period configuration
    static constexpr uint64_t STANDARD_CHALLENGE_PERIOD = 7 * 24 * 60 * 60;  // 7 days
    static constexpr uint64_t FAST_CHALLENGE_PERIOD = 1 * 24 * 60 * 60;      // 1 day (high rep)
    
    // Deposit/withdrawal limits
    static constexpr uint256 MAX_DEPOSIT_PER_TX = 10000 * COIN;
    static constexpr uint256 MAX_DAILY_DEPOSIT = 100000 * COIN;
    
    // Verify merkle proof for withdrawal
    bool VerifyWithdrawalProof(const WithdrawalRequest& request);
    
    // Calculate challenge period based on reputation
    uint64_t CalculateChallengePeriod(uint32_t hatScore) const;
};
```


### 6. Fraud Proof System (`src/l2/fraud_proof.h/cpp`)

```cpp
// Fraud proof types
enum class FraudProofType {
    INVALID_STATE_TRANSITION,    // State root doesn't match execution
    INVALID_TRANSACTION,         // Transaction violates rules
    INVALID_SIGNATURE,           // Sequencer signature invalid
    DATA_WITHHOLDING,           // Transaction data not available
    TIMESTAMP_MANIPULATION,      // Invalid timestamp
    DOUBLE_SPEND                 // Same funds spent twice
};

// Fraud proof submission
struct FraudProof {
    FraudProofType type;
    uint256 disputedStateRoot;
    uint64_t disputedBlockNumber;
    uint256 previousStateRoot;
    
    // Evidence
    std::vector<CTransaction> relevantTransactions;
    std::vector<uint8_t> stateProof;        // Merkle proof of disputed state
    std::vector<uint8_t> executionTrace;    // Step-by-step execution
    
    // Challenger info
    uint160 challengerAddress;
    uint256 challengeBond;
    std::vector<uint8_t> challengerSignature;
};

// Interactive fraud proof step
struct InteractiveFraudProofStep {
    uint64_t stepNumber;
    uint256 preStateRoot;
    uint256 postStateRoot;
    std::vector<uint8_t> instruction;       // Single CVM instruction
    std::vector<uint8_t> witness;           // Required state for execution
};

class FraudProofSystem {
public:
    // Submit fraud proof
    bool SubmitFraudProof(const FraudProof& proof);
    
    // Verify fraud proof on L1
    bool VerifyFraudProof(const FraudProof& proof);
    
    // Start interactive fraud proof
    uint256 StartInteractiveProof(
        const uint256& disputedStateRoot,
        const uint160& challenger,
        const uint160& sequencer
    );
    
    // Submit step in interactive proof
    bool SubmitInteractiveStep(
        const uint256& proofId,
        const InteractiveFraudProofStep& step
    );
    
    // Resolve interactive proof
    FraudProofResult ResolveInteractiveProof(const uint256& proofId);
    
    // Get challenge deadline
    uint64_t GetChallengeDeadline(const uint256& stateRoot) const;
    
    // Check if state root is finalized (past challenge period)
    bool IsStateRootFinalized(const uint256& stateRoot) const;
    
private:
    // Active fraud proofs
    std::map<uint256, FraudProof> activeProofs;
    
    // Interactive proof sessions
    std::map<uint256, InteractiveProofSession> interactiveSessions;
    
    // Challenge bond requirement
    static constexpr uint256 CHALLENGE_BOND = 10 * COIN;  // 10 CAS
    
    // Re-execute transaction on L1 to verify
    ExecutionResult ReExecuteOnL1(
        const CTransaction& tx,
        const uint256& preStateRoot
    );
    
    // Binary search for invalid step in interactive proof
    uint64_t BinarySearchInvalidStep(
        const std::vector<InteractiveFraudProofStep>& steps
    );
    
    // Slash sequencer for valid fraud proof
    void SlashSequencer(const uint160& sequencer, const FraudProof& proof);
    
    // Reward challenger
    void RewardChallenger(const uint160& challenger, uint256 reward);
};
```

### 7. State Management (`src/l2/state_manager.h/cpp`)

```cpp
// L2 State using Sparse Merkle Tree
class L2StateManager {
public:
    // Get current state root
    uint256 GetStateRoot() const;
    
    // Apply transaction and return new state root
    uint256 ApplyTransaction(const CTransaction& tx);
    
    // Apply batch of transactions
    uint256 ApplyBatch(const std::vector<CTransaction>& txs);
    
    // Get account state
    AccountState GetAccountState(const uint160& address) const;
    
    // Get contract storage
    uint256 GetContractStorage(const uint160& contract, const uint256& key) const;
    
    // Generate inclusion proof
    std::vector<uint8_t> GenerateInclusionProof(
        const uint160& address,
        const uint256& stateRoot
    ) const;
    
    // Generate exclusion proof
    std::vector<uint8_t> GenerateExclusionProof(
        const uint160& address,
        const uint256& stateRoot
    ) const;
    
    // Verify proof
    bool VerifyProof(
        const std::vector<uint8_t>& proof,
        const uint256& stateRoot,
        const uint160& address,
        const AccountState& expectedState
    ) const;
    
    // Revert to previous state root
    bool RevertToStateRoot(const uint256& stateRoot);
    
    // State rent management
    void ProcessStateRent(uint64_t currentBlock);
    
    // Archive inactive state
    void ArchiveInactiveState(uint64_t inactivityThreshold);
    
private:
    // Sparse Merkle Tree for state
    SparseMerkleTree stateTree;
    
    // State history for rollbacks
    std::map<uint256, StateSnapshot> stateHistory;
    
    // Last access time per account (for state rent)
    std::map<uint160, uint64_t> lastAccessTime;
    
    // State rent rate (CAS per byte per year)
    static constexpr uint256 STATE_RENT_RATE = 1;  // 1 satoshi per byte per year
    
    // Inactivity threshold for archiving (1 year in blocks)
    static constexpr uint64_t ARCHIVE_THRESHOLD = 365 * 24 * 60 * 60 / 2.5 * 60;
};

// Account state structure
struct AccountState {
    uint256 balance;
    uint64_t nonce;
    uint256 codeHash;           // Hash of contract code (if contract)
    uint256 storageRoot;        // Root of contract storage tree
    uint32_t hatScore;          // Cached HAT v2 score
    uint64_t lastActivity;      // Last transaction block
};
```

### 8. Data Availability Layer (`src/l2/data_availability.h/cpp`)

```cpp
// Batch data for L1 submission
struct BatchData {
    uint64_t startBlock;
    uint64_t endBlock;
    uint256 preStateRoot;
    uint256 postStateRoot;
    std::vector<uint8_t> compressedTransactions;
    uint256 transactionsRoot;   // Merkle root of transactions
    uint160 sequencerAddress;
    std::vector<uint8_t> sequencerSignature;
};

// Data availability commitment
struct DACommitment {
    uint256 dataHash;
    uint64_t dataSize;
    uint256 erasureCodingRoot;  // For data availability sampling
    std::vector<uint256> columnRoots;
    std::vector<uint256> rowRoots;
};

class DataAvailabilityLayer {
public:
    // Publish batch data to L1
    uint256 PublishBatch(const BatchData& batch);
    
    // Compress transactions for efficient storage
    std::vector<uint8_t> CompressTransactions(
        const std::vector<CTransaction>& txs
    );
    
    // Decompress transactions
    std::vector<CTransaction> DecompressTransactions(
        const std::vector<uint8_t>& compressed
    );
    
    // Generate DA commitment
    DACommitment GenerateDACommitment(const std::vector<uint8_t>& data);
    
    // Data availability sampling for light clients
    bool SampleDataAvailability(
        const DACommitment& commitment,
        uint32_t sampleCount
    );
    
    // Reconstruct data from L1
    std::vector<uint8_t> ReconstructFromL1(
        uint64_t startBlock,
        uint64_t endBlock
    );
    
    // Check if data is available
    bool IsDataAvailable(const uint256& dataHash) const;
    
private:
    // Compression using zstd
    std::vector<uint8_t> ZstdCompress(const std::vector<uint8_t>& data);
    std::vector<uint8_t> ZstdDecompress(const std::vector<uint8_t>& compressed);
    
    // Erasure coding for DA sampling
    std::vector<std::vector<uint8_t>> ErasureEncode(
        const std::vector<uint8_t>& data,
        uint32_t dataShards,
        uint32_t parityShards
    );
    
    // Batch submission interval
    static constexpr uint32_t BATCH_INTERVAL = 100;  // Every 100 L2 blocks
    
    // Maximum batch size
    static constexpr uint64_t MAX_BATCH_SIZE = 128 * 1024;  // 128 KB
};
```

### 9. Cross-Layer Messaging (`src/l2/cross_layer_messaging.h/cpp`)

```cpp
// Message from L1 to L2
struct L1ToL2Message {
    uint256 messageId;
    uint160 l1Sender;
    uint160 l2Target;
    std::vector<uint8_t> data;
    uint256 value;              // CAS to transfer
    uint64_t l1BlockNumber;
    uint256 l1TxHash;
    MessageStatus status;
};

// Message from L2 to L1
struct L2ToL1Message {
    uint256 messageId;
    uint160 l2Sender;
    uint160 l1Target;
    std::vector<uint8_t> data;
    uint256 value;
    uint64_t l2BlockNumber;
    uint256 stateRoot;
    std::vector<uint8_t> merkleProof;
    uint64_t challengeDeadline;
    MessageStatus status;
};

enum class MessageStatus {
    PENDING,
    EXECUTED,
    FAILED,
    CHALLENGED,
    FINALIZED
};

class CrossLayerMessaging {
public:
    // Send message from L1 to L2
    uint256 SendL1ToL2(
        const uint160& l1Sender,
        const uint160& l2Target,
        const std::vector<uint8_t>& data,
        uint256 value
    );
    
    // Process L1 to L2 message on L2
    bool ProcessL1ToL2Message(const L1ToL2Message& message);
    
    // Send message from L2 to L1
    uint256 SendL2ToL1(
        const uint160& l2Sender,
        const uint160& l1Target,
        const std::vector<uint8_t>& data,
        uint256 value
    );
    
    // Finalize L2 to L1 message on L1 (after challenge period)
    bool FinalizeL2ToL1Message(const L2ToL1Message& message);
    
    // Get pending L1 to L2 messages
    std::vector<L1ToL2Message> GetPendingL1ToL2Messages() const;
    
    // Get message status
    MessageStatus GetMessageStatus(const uint256& messageId) const;
    
    // Retry failed message
    bool RetryMessage(const uint256& messageId);
    
private:
    // Pending messages
    std::map<uint256, L1ToL2Message> pendingL1ToL2;
    std::map<uint256, L2ToL1Message> pendingL2ToL1;
    
    // Message execution with reentrancy protection
    bool ExecuteMessageSafe(
        const uint160& target,
        const std::vector<uint8_t>& data,
        uint256 value
    );
    
    // Reentrancy guard
    std::set<uint256> executingMessages;
    
    // Generate message proof
    std::vector<uint8_t> GenerateMessageProof(const uint256& messageId);
};
```

### 10. Reputation Integration (`src/l2/l2_reputation.h/cpp`)

```cpp
// L2-specific reputation data
struct L2ReputationData {
    uint32_t l1HatScore;        // Imported from L1
    uint32_t l2BehaviorScore;   // L2-specific behavior
    uint32_t l2EconomicScore;   // L2 economic activity
    uint32_t aggregatedScore;   // Combined score
    uint64_t lastL1Sync;        // Last sync with L1
    uint64_t l2TransactionCount;
    uint256 l2VolumeTraded;
};

class L2ReputationManager {
public:
    // Import reputation from L1
    bool ImportL1Reputation(const uint160& address);
    
    // Get aggregated reputation (L1 + L2)
    uint32_t GetAggregatedReputation(const uint160& address) const;
    
    // Update L2-specific reputation
    void UpdateL2Reputation(
        const uint160& address,
        const L2Activity& activity
    );
    
    // Sync reputation changes back to L1
    bool SyncToL1(const uint160& address);
    
    // Get reputation-based benefits
    ReputationBenefits GetBenefits(const uint160& address) const;
    
    // Check if address qualifies for fast withdrawal
    bool QualifiesForFastWithdrawal(const uint160& address) const;
    
    // Get gas discount based on reputation
    uint32_t GetGasDiscount(const uint160& address) const;
    
private:
    // L2 reputation cache
    std::map<uint160, L2ReputationData> reputationCache;
    
    // Sync interval (blocks)
    static constexpr uint64_t L1_SYNC_INTERVAL = 1000;
    
    // Reputation thresholds
    static constexpr uint32_t FAST_WITHDRAWAL_THRESHOLD = 80;
    static constexpr uint32_t GAS_DISCOUNT_THRESHOLD = 70;
    
    // Calculate aggregated score
    uint32_t CalculateAggregatedScore(
        uint32_t l1Score,
        uint32_t l2Behavior,
        uint32_t l2Economic
    ) const;
    
    // Prevent reputation gaming
    bool DetectReputationGaming(const uint160& address) const;
};

// Reputation-based benefits
struct ReputationBenefits {
    uint32_t gasDiscountPercent;     // 0-50%
    uint64_t challengePeriodSeconds; // Reduced for high rep
    uint32_t rateLimitMultiplier;    // Higher limits for high rep
    bool instantSoftFinality;        // Instant confirmation
    uint32_t priorityLevel;          // Transaction priority
};
```


## Data Models

### L2 Block Structure

```cpp
struct L2Block {
    // Header
    uint64_t blockNumber;
    uint256 parentHash;
    uint256 stateRoot;
    uint256 transactionsRoot;
    uint256 receiptsRoot;
    uint160 sequencer;
    uint64_t timestamp;
    uint64_t gasLimit;
    uint64_t gasUsed;
    
    // Consensus
    std::vector<SequencerSignature> signatures;  // 2/3+ sequencer signatures
    
    // Body
    std::vector<CTransaction> transactions;
    std::vector<L1ToL2Message> l1Messages;       // Processed L1 messages
    
    // L1 Anchoring
    uint64_t l1AnchorBlock;                      // Which L1 block this references
    uint256 l1AnchorHash;
    
    // Compute block hash
    uint256 GetHash() const;
    
    // Validate block structure
    bool ValidateStructure() const;
};

struct SequencerSignature {
    uint160 sequencerAddress;
    std::vector<uint8_t> signature;
    uint64_t timestamp;
};
```

### L2 Transaction Structure

```cpp
struct L2Transaction {
    // Standard fields (compatible with L1)
    uint160 from;
    uint160 to;
    uint256 value;
    uint64_t nonce;
    uint64_t gasLimit;
    uint256 gasPrice;
    std::vector<uint8_t> data;
    
    // L2-specific fields
    L2TxType type;
    uint256 l2ChainId;
    
    // Optional: Encrypted for MEV protection
    bool isEncrypted;
    std::vector<uint8_t> encryptedPayload;
    
    // Signature
    std::vector<uint8_t> signature;
    
    // Compute transaction hash
    uint256 GetHash() const;
    
    // Verify signature
    bool VerifySignature() const;
};

enum class L2TxType {
    STANDARD,           // Normal transaction
    CONTRACT_DEPLOY,    // Deploy contract
    CONTRACT_CALL,      // Call contract
    WITHDRAWAL,         // Initiate withdrawal to L1
    L2_TO_L1_MESSAGE,   // Send message to L1
    FORCED_INCLUSION    // Forced from L1
};
```

### L2 Registry on L1

```cpp
struct L2ChainInfo {
    uint256 chainId;
    std::string name;
    uint160 bridgeContract;
    uint160 deployer;
    uint64_t deploymentBlock;
    
    // Configuration
    uint64_t blockTime;              // Target block time (ms)
    uint64_t gasLimit;               // Per-block gas limit
    uint64_t challengePeriod;        // Fraud proof window (seconds)
    uint32_t minSequencerStake;      // Minimum stake for sequencers
    uint32_t minSequencerReputation; // Minimum HAT score
    
    // State
    uint256 latestStateRoot;
    uint64_t latestL2Block;
    uint64_t latestL1Anchor;
    L2Status status;
};

enum class L2Status {
    BOOTSTRAPPING,      // Initial setup phase
    ACTIVE,             // Normal operation
    PAUSED,             // Temporarily paused
    EMERGENCY,          // Emergency mode (withdrawals only)
    DEPRECATED          // Being phased out
};
```

### Sparse Merkle Tree for State

```cpp
// 256-bit sparse Merkle tree
class SparseMerkleTree {
public:
    // Tree depth (256 for full address space)
    static constexpr uint32_t TREE_DEPTH = 256;
    
    // Get value at key
    std::vector<uint8_t> Get(const uint256& key) const;
    
    // Set value at key
    void Set(const uint256& key, const std::vector<uint8_t>& value);
    
    // Delete key
    void Delete(const uint256& key);
    
    // Get root hash
    uint256 GetRoot() const;
    
    // Generate Merkle proof
    MerkleProof GenerateProof(const uint256& key) const;
    
    // Verify Merkle proof
    static bool VerifyProof(
        const MerkleProof& proof,
        const uint256& root,
        const uint256& key,
        const std::vector<uint8_t>& value
    );
    
private:
    // Node structure
    struct Node {
        uint256 hash;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
    };
    
    std::unique_ptr<Node> root;
    
    // Default hash for empty subtrees (cached)
    std::array<uint256, TREE_DEPTH + 1> defaultHashes;
    
    // Hash function
    uint256 HashNode(const uint256& left, const uint256& right) const;
};

struct MerkleProof {
    std::vector<uint256> siblings;  // Sibling hashes along path
    std::vector<bool> path;         // Left (false) or right (true) at each level
    uint256 leafHash;               // Hash of the leaf value
};
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: State Root Consistency
*For any* sequence of L2 transactions applied to a state, re-executing the same transactions from the same initial state SHALL produce the identical final state root.
**Validates: Requirements 3.1, 5.2, 19.2**

### Property 2: Sequencer Election Determinism
*For any* given slot number and set of eligible sequencers, the leader election algorithm SHALL always select the same leader when using the same random seed.
**Validates: Requirements 2a.1, 2a.2**

### Property 3: Failover Consistency
*For any* sequencer failure, the failover order SHALL be deterministic and all nodes SHALL agree on the next leader within the timeout period.
**Validates: Requirements 2b.2, 2b.3, 2b.5**

### Property 4: Deposit-Withdrawal Balance
*For any* L2 chain, the sum of all deposits minus the sum of all completed withdrawals SHALL equal the total L2 balance across all accounts.
**Validates: Requirements 4.1, 4.2, 4.5**

### Property 5: Fraud Proof Soundness
*For any* valid fraud proof submitted, re-executing the disputed transaction on L1 SHALL produce a different state root than the one submitted by the sequencer.
**Validates: Requirements 5.2, 5.3**

### Property 6: Fraud Proof Completeness
*For any* invalid state transition, there SHALL exist a fraud proof that can demonstrate the invalidity within the challenge period.
**Validates: Requirements 5.1, 5.6**

### Property 7: MEV Protection Round-Trip
*For any* encrypted transaction, decrypting with threshold shares and re-encrypting SHALL produce the original encrypted payload.
**Validates: Requirements 16.1, 16.2**

### Property 8: Merkle Proof Verification
*For any* state element and its inclusion proof, verifying the proof against the state root SHALL return true if and only if the element exists in the state.
**Validates: Requirements 37.2, 37.3, 37.4**

### Property 9: Cross-Layer Message Integrity
*For any* L1→L2 or L2→L1 message, the message content received SHALL be identical to the message content sent.
**Validates: Requirements 9.1, 9.2, 9.4**

### Property 10: Reputation Aggregation Consistency
*For any* address, the aggregated reputation score SHALL be a deterministic function of L1 and L2 reputation components.
**Validates: Requirements 10.3, 10.5**

### Property 11: Consensus Threshold Safety
*For any* L2 block, if 2/3+ of sequencers sign the block, no conflicting block for the same height SHALL achieve consensus.
**Validates: Requirements 2a.5, 22.1**

### Property 12: Emergency Exit Completeness
*For any* user with L2 balance, if emergency mode is activated, the user SHALL be able to withdraw their full balance using the last valid state root.
**Validates: Requirements 12.1, 12.2, 12.3**

### Property 13: Data Availability Reconstruction
*For any* batch published to L1, it SHALL be possible to reconstruct the complete L2 state from L1 data alone.
**Validates: Requirements 7.3, 11.6, 41.2**

### Property 14: Rate Limit Enforcement
*For any* address, the number of transactions per block SHALL not exceed the rate limit determined by their reputation score.
**Validates: Requirements 26.2, 26.3**

### Property 15: Timestamp Monotonicity
*For any* sequence of L2 blocks, timestamps SHALL be strictly monotonically increasing.
**Validates: Requirements 27.2, 27.3**

### Property 16: Challenge Bond Slashing
*For any* invalid challenge, the challenger's bond SHALL be slashed and distributed to the challenged party.
**Validates: Requirements 29.1, 29.2**

### Property 17: Sequencer Stake Slashing
*For any* valid fraud proof, the sequencer's stake SHALL be slashed by at least the minimum penalty amount.
**Validates: Requirements 5.4, 16.6**

### Property 18: Gas Fee Distribution
*For any* L2 block, the total gas fees collected SHALL be distributed according to the defined ratio (70/20/10).
**Validates: Requirements 18.2, 38.2**

### Property 19: Forced Inclusion Guarantee
*For any* transaction submitted via L1 forced inclusion, it SHALL be included in an L2 block within 24 hours or the sequencer SHALL be slashed.
**Validates: Requirements 17.2, 17.3**

### Property 20: L1 Reorg Recovery
*For any* L1 reorganization affecting anchored L2 state, the L2 state SHALL revert to the last valid anchor and re-process subsequent transactions.
**Validates: Requirements 19.2, 19.3**

## Error Handling

### Sequencer Errors

| Error | Cause | Handling |
|-------|-------|----------|
| `SEQ_NOT_ELIGIBLE` | Sequencer doesn't meet requirements | Reject announcement, log reason |
| `SEQ_TIMEOUT` | Leader didn't produce block in time | Trigger failover to next sequencer |
| `SEQ_INVALID_BLOCK` | Proposed block fails validation | Reject block, penalize reputation |
| `SEQ_DOUBLE_SIGN` | Sequencer signed conflicting blocks | Slash stake, ban sequencer |
| `SEQ_OFFLINE` | Sequencer not responding | Remove from active set after timeout |

### Bridge Errors

| Error | Cause | Handling |
|-------|-------|----------|
| `BRIDGE_DEPOSIT_LIMIT` | Deposit exceeds limit | Reject deposit, return funds |
| `BRIDGE_WITHDRAWAL_LIMIT` | Withdrawal exceeds limit | Reject or require additional verification |
| `BRIDGE_INSUFFICIENT_BALANCE` | L2 balance too low | Reject withdrawal |
| `BRIDGE_CHALLENGE_FAILED` | Invalid challenge submitted | Slash challenger bond |
| `BRIDGE_PROOF_INVALID` | Merkle proof doesn't verify | Reject withdrawal |

### Consensus Errors

| Error | Cause | Handling |
|-------|-------|----------|
| `CONSENSUS_NO_QUORUM` | Less than 2/3 votes | Extend voting period or failover |
| `CONSENSUS_CONFLICTING` | Conflicting votes detected | Investigate for collusion |
| `CONSENSUS_TIMEOUT` | Voting period expired | Trigger new election |
| `CONSENSUS_INVALID_VOTE` | Vote signature invalid | Ignore vote, log incident |

### State Errors

| Error | Cause | Handling |
|-------|-------|----------|
| `STATE_ROOT_MISMATCH` | Computed root doesn't match | Trigger fraud proof investigation |
| `STATE_PROOF_INVALID` | Merkle proof verification failed | Reject operation |
| `STATE_RENT_UNPAID` | Account hasn't paid state rent | Archive state, allow restoration |
| `STATE_REVERT_FAILED` | Cannot revert to requested state | Enter emergency mode |

## Testing Strategy

**WICHTIG: Alle Tests finden im Regtest-Modus statt!**

Regtest ermöglicht:
- Sofortige Block-Generierung ohne Mining
- Kontrollierte Testumgebung
- Deterministische Ergebnisse
- Schnelle Iteration

### Unit Tests (Regtest)
- Test each component in isolation
- Mock external dependencies where needed
- Focus on edge cases and error conditions
- Minimum 90% code coverage
- Run via `make check` im Regtest-Modus

### Property-Based Tests (Regtest)
- Use Boost.Test mit custom generators
- Minimum 100 iterations per property
- Test all 20 correctness properties
- Include shrinking for counterexample minimization
- Regtest erlaubt schnelle Block-Generierung für State-Tests

### Integration Tests (Regtest)
- Multi-Node Regtest Setup (3-5 Nodes)
- Simulate multi-sequencer scenarios
- Test failover and recovery
- Test cross-layer messaging end-to-end
- `test/functional/` Python Tests mit Regtest

### Regtest-Spezifische L2 Tests
```bash
# Start Regtest mit L2 aktiviert
cascoind -regtest -l2=1

# Generiere Blöcke für L1 Anchoring
cascoin-cli -regtest generate 100

# Deploy L2 Chain
cascoin-cli -regtest l2_deploy "TestL2" 500 30000000 604800

# Test Sequencer Announcement
cascoin-cli -regtest l2_announcesequencer

# Test Deposit/Withdrawal
cascoin-cli -regtest l2_deposit <l2chainid> <amount>
cascoin-cli -regtest l2_withdraw <l2chainid> <amount>
```

### Stress Tests (Regtest)
- 1000+ TPS load testing mit automatischer Block-Generierung
- Network partition simulation (disconnect Regtest nodes)
- Sequencer failure scenarios
- Large state tree operations
- Regtest erlaubt schnelle Reorg-Simulation

### Security Tests (Regtest)
- Fuzzing for all input handlers
- Formal verification for critical paths (bridge, fraud proofs)
- Fraud Proof Testing mit kontrollierten Invalid States
- Challenge/Response Testing

### Testnet Deployment (nach Regtest)
- Deploy on dedicated testnet erst nach erfolgreichen Regtest-Tests
- Run for minimum 30 days before mainnet
- Conduct disaster recovery drills
- Community testing and feedback

### Test-Konfiguration für Regtest

```cpp
// chainparams.cpp - Regtest L2 Parameters
class CRegTestParams : public CChainParams {
    // L2 Regtest-spezifische Parameter
    consensus.l2Enabled = true;
    consensus.l2MinSequencerStake = 10 * COIN;  // Niedriger für Tests
    consensus.l2MinSequencerReputation = 50;     // Niedriger für Tests
    consensus.l2ChallengePeriod = 10;            // 10 Blöcke statt 7 Tage
    consensus.l2BatchInterval = 5;               // Alle 5 Blöcke
    consensus.l2BlockTime = 100;                 // 100ms für schnelle Tests
};
```
