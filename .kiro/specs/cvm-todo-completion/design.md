# Design Document: CVM TODO Completion

## Overview

This design document specifies the implementation approach for completing the remaining TODO items in the Cascoin Virtual Machine (CVM) integration. The implementation focuses on sixteen major areas organized into four categories:

1. **Core Validator Infrastructure**: Cryptographic operations, wallet integration, P2P networking
2. **Blockchain Integration**: Data access, database persistence, block validation, soft fork activation
3. **System Components**: Gas system, mempool management, consensus validation, transaction building
4. **Security & Maintenance**: DAO integration, cross-chain verification, cleanup management, attack detection

The design prioritizes backward compatibility, security, and minimal changes to existing interfaces while completing the missing functionality.

## Architecture

The CVM TODO completion follows the existing CVM architecture with additions to integrate with core Cascoin systems:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              CVM Layer                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │  Validator  │  │    HAT      │  │   Mempool   │  │   Cleanup   │        │
│  │ Attestation │  │  Consensus  │  │   Manager   │  │   Manager   │        │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
│         │                │                │                │                │
│  ┌──────┴──────┐  ┌──────┴──────┐  ┌──────┴──────┐  ┌──────┴──────┐        │
│  │  Soft Fork  │  │    DAO      │  │  Consensus  │  │   Eclipse   │        │
│  │  Validator  │  │ Integration │  │  Validator  │  │  Protection │        │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
├─────────┼────────────────┼────────────────┼────────────────┼────────────────┤
│         ▼                ▼                ▼                ▼                │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │                        Integration Layer                                 ││
│  │  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐││
│  │  │ Wallet │  │  P2P   │  │ Chain  │  │Database│  │  Gas   │  │  Tx    │││
│  │  │Adapter │  │Adapter │  │Adapter │  │Adapter │  │ System │  │Builder │││
│  │  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘││
│  └──────┼───────────┼───────────┼───────────┼───────────┼───────────┼──────┘│
└─────────┼───────────┼───────────┼───────────┼───────────┼───────────┼───────┘
          ▼           ▼           ▼           ▼           ▼           ▼
   ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
   │  Wallet  │ │ CConnman │ │chainActive│ │ LevelDB  │ │ Mempool  │ │ Script   │
   │  (keys)  │ │  (P2P)   │ │  (chain) │ │  (store) │ │  (txs)   │ │ (P2SH)   │
   └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘
```

## Components and Interfaces

### 1. Cryptographic Operations Module

**Location:** `src/cvm/validator_attestation.cpp`

**Purpose:** Implement secp256k1 signature generation and verification for validator responses.

```cpp
// Interface for cryptographic operations
class ValidatorCrypto {
public:
    // Sign a validation response using the validator's private key
    static bool SignResponse(ValidationResponse& response, const CKey& privateKey);
    
    // Verify a validation response signature
    static bool VerifyResponseSignature(const ValidationResponse& response);
    
    // Derive validator address from public key
    static uint160 DeriveValidatorAddress(const CPubKey& pubkey);
};
```

**Implementation Details:**
- Use existing `CKey::Sign()` for ECDSA signing
- Use existing `CPubKey::Verify()` for signature verification
- Hash message using `CHashWriter` with `SER_GETHASH`
- Address derivation uses `CPubKey::GetID()` which returns `CKeyID` (uint160)

### 2. Wallet Integration Module

**Location:** `src/cvm/validator_wallet.cpp` (new file)

**Purpose:** Bridge between validator system and wallet for key management.

```cpp
// Interface for wallet integration
class ValidatorWalletAdapter {
public:
    // Get the local validator's address from wallet
    static uint160 GetMyValidatorAddress();
    
    // Get the private key for signing
    static bool GetValidatorKey(CKey& keyOut);
    
    // Check if wallet is available and unlocked
    static bool IsWalletAvailable();
};
```

**Implementation Details:**
- Access wallet via `pwalletMain` global
- Use `CWallet::GetKey()` to retrieve private keys
- Check `CWallet::IsLocked()` for wallet availability
- Return primary receiving address as validator address

### 3. P2P Network Adapter

**Location:** `src/cvm/validator_p2p.cpp` (new file)

**Purpose:** Handle P2P message broadcasting for validator communication.

```cpp
// Interface for P2P operations
class ValidatorP2PAdapter {
public:
    // Broadcast validation task to selected validators
    static void BroadcastValidationTask(
        const uint256& taskHash,
        int64_t blockHeight,
        const std::vector<uint160>& selectedValidators,
        CConnman* connman);
    
    // Broadcast validation response
    static void BroadcastValidationResponse(
        const ValidationResponse& response,
        CConnman* connman);
    
    // Send response to specific peer
    static void SendToPeer(
        CNode* peer,
        const std::string& msgType,
        const std::vector<uint8_t>& data);
};
```

**Implementation Details:**
- Use `connman->ForEachNode()` for broadcasting
- Create messages using `CNetMsgMaker`
- Use existing message types: `MSG_VALIDATION_TASK`, `MSG_VALIDATION_RESPONSE`
- Serialize data using `CDataStream`

### 4. Blockchain Data Adapter

**Location:** `src/cvm/chain_adapter.cpp` (new file)

**Purpose:** Access blockchain data for validator eligibility verification.

```cpp
// Interface for blockchain data access
class ChainDataAdapter {
public:
    // Get current block height
    static int GetCurrentBlockHeight();
    
    // Scan UTXOs for an address
    static bool ScanUTXOs(
        const uint160& address,
        std::vector<COutPoint>& utxos,
        CAmount& totalAmount,
        int& oldestAge);
    
    // Get transaction history for address
    static bool GetAddressHistory(
        const uint160& address,
        int& firstSeenBlock,
        int& transactionCount,
        int& uniqueInteractions);
    
    // Check if CVM soft fork is active
    static bool IsCVMActive(int blockHeight);
};
```

**Implementation Details:**
- Use `chainActive.Height()` for current height
- Use `pcoinsTip` (UTXO set) for stake verification
- Use address index if available, otherwise scan blocks
- Check `chainparams.GetConsensus().CVMActivationHeight`

### 5. Database Persistence Layer

**Location:** `src/cvm/cvmdb.cpp` (extend existing)

**Purpose:** Persist validator records and support iteration.

```cpp
// Extended CVMDatabase interface
class CVMDatabase {
public:
    // Write validator eligibility record
    bool WriteValidatorRecord(const ValidatorEligibilityRecord& record);
    
    // Read validator eligibility record
    bool ReadValidatorRecord(const uint160& address, ValidatorEligibilityRecord& record);
    
    // Iterate all validator records
    bool IterateValidatorRecords(
        std::function<bool(const ValidatorEligibilityRecord&)> callback);
    
    // List keys with prefix (already exists, ensure implementation)
    std::vector<std::string> ListKeysWithPrefix(const std::string& prefix);
};
```

**Implementation Details:**
- Use LevelDB iterator for `IterateValidatorRecords`
- Key format: `"validator_" + address.ToString()`
- Serialize using `CDataStream` with `SER_DISK`

### 6. Gas System Integration

**Location:** `src/cvm/sustainable_gas.cpp` (extend existing)

**Purpose:** Implement gas price multiplier and trust density calculations.

```cpp
// Extended SustainableGasSystem interface
class SustainableGasSystem {
public:
    // Get current price multiplier based on congestion
    double GetCurrentPriceMultiplier();
    
    // Calculate network trust density
    double CalculateTrustDensity();
    
    // Validate gas subsidy claims
    bool ValidateGasSubsidy(const CTransaction& tx, CAmount subsidyAmount);
    
private:
    // Track recent block gas usage
    std::deque<uint64_t> m_recentBlockGas;
    
    // Target gas per block
    static const uint64_t TARGET_GAS_PER_BLOCK = 5000000;
};
```

**Implementation Details:**
- Track gas usage over last 100 blocks
- Multiplier = 1.0 + (avgGas - targetGas) / targetGas * 0.5
- Clamp multiplier to [0.5, 2.0] range
- Trust density from reputation system statistics
- Subsidy validation checks eligibility and amount limits

### 7. HAT Consensus DAO Integration

**Location:** `src/cvm/hat_consensus.cpp` (extend existing)

**Purpose:** Integrate DAO notification and dispute evidence handling.

```cpp
// Interface for DAO integration
class DAOIntegration {
public:
    // Notify DAO members of escalated dispute
    static void NotifyDAOMembers(
        const uint256& disputeId,
        const std::vector<uint160>& daoMembers,
        CConnman* connman);
    
    // Package dispute evidence for DAO review
    static std::vector<uint8_t> PackageDisputeEvidence(
        const std::vector<ValidationResponse>& responses,
        const TrustGraphSnapshot& trustData);
    
    // Load transaction for fraud evidence
    static bool LoadTransactionEvidence(
        const uint256& txHash,
        CTransaction& txOut);
};
```

**Implementation Details:**
- Use P2P broadcast to notify DAO members via `MSG_DAO_DISPUTE`
- Serialize validator responses using `CDataStream`
- Include trust graph snapshot at dispute time
- Load transactions from `pcoinsTip` or block index

### 8. Mempool Manager Integration

**Location:** `src/cvm/mempool_manager.cpp` (extend existing)

**Purpose:** Provide accurate block height and mempool access for gas calculations.

```cpp
// Interface for mempool management
class MempoolManagerAdapter {
public:
    // Get current block height from chainActive
    static int GetCurrentBlockHeight();
    
    // Iterate mempool transactions for priority distribution
    static void IterateMempool(
        std::function<bool(const CTxMemPoolEntry&)> callback);
    
    // Calculate gas allowance based on current height
    static uint64_t CalculateGasAllowance(int blockHeight);
};
```

**Implementation Details:**
- Access `chainActive.Height()` for current height
- Use `mempool.mapTx` for iteration with proper locking (`LOCK(mempool.cs)`)
- Gas allowance scales with block height for network growth

### 9. Consensus Validator Integration

**Location:** `src/cvm/consensus_validator.cpp` (extend existing)

**Purpose:** Integrate consensus validation with reputation systems and transaction data.

```cpp
// Interface for consensus validation
class ConsensusValidatorAdapter {
public:
    // Fall back to ASRS when HAT v2 unavailable
    static double GetReputationWithFallback(
        const uint160& address,
        bool& usedFallback);
    
    // Extract sender address from transaction inputs
    static bool ExtractSenderAddress(
        const CTransaction& tx,
        uint160& senderOut);
    
    // Query pool balance from database
    static CAmount GetPoolBalance(const std::string& poolId);
    
    // Extract gas usage and costs from transaction
    static bool ExtractGasInfo(
        const CTransaction& tx,
        uint64_t& gasUsed,
        CAmount& gasCost);
};
```

**Implementation Details:**
- Check HAT v2 availability, fall back to ASRS if unavailable
- Extract sender from first P2PKH/P2WPKH input using script parsing
- Pool balance stored in LevelDB with key `pool_<id>_balance`
- Gas info encoded in OP_RETURN data for CVM transactions

### 10. Block Validator CVM Activation

**Location:** `src/cvm/block_validator.cpp` (extend existing)

**Purpose:** Check CVM soft fork activation and validate HAT scores.

```cpp
// Interface for block validation
class BlockValidatorAdapter {
public:
    // Check if CVM soft fork is active at height
    static bool IsCVMActive(int blockHeight);
    
    // Verify HAT v2 score is not expired
    static bool IsHATScoreValid(
        const HATScore& score,
        int currentHeight);
    
    // Verify transaction fees match expected
    static bool VerifyTransactionFees(
        const CTransaction& tx,
        CAmount expectedFee);
};
```

**Implementation Details:**
- Check `Params().GetConsensus().CVMActivationHeight`
- HAT scores expire after configurable block count (default: 1000 blocks)
- Fee verification compares actual vs expected from gas calculation

### 11. Soft Fork Contract Validation

**Location:** `src/cvm/soft_fork.cpp` (extend existing)

**Purpose:** Validate contract deployments and calls with reputation checks.

```cpp
// Interface for soft fork validation
class SoftForkValidator {
public:
    // Extract deployer address from deployment transaction
    static bool ExtractDeployerAddress(
        const CTransaction& tx,
        uint160& deployerOut);
    
    // Check deployer reputation meets minimum threshold
    static bool CheckDeployerReputation(
        const uint160& deployer,
        double minReputation = 50.0);
    
    // Verify contract format matches expected
    static bool VerifyContractFormat(
        const std::vector<uint8_t>& contractData,
        uint8_t expectedFormat);
};
```

**Implementation Details:**
- Deployer extraction uses same logic as sender extraction
- Minimum reputation threshold: 50 (configurable)
- Contract format byte at offset 0 of contract data

### 12. Transaction Builder Contract Support

**Location:** `src/cvm/txbuilder.cpp` (extend existing)

**Purpose:** Support value transfers to contracts and DAO slash paths.

```cpp
// Interface for transaction building
class TransactionBuilderAdapter {
public:
    // Resolve contract's P2SH address for value transfer
    static bool ResolveContractP2SH(
        const uint160& contractAddress,
        CScript& scriptPubKeyOut);
    
    // Implement DAO slash path for bonded transactions
    static bool BuildDAOSlashTransaction(
        const uint256& bondTxHash,
        const uint160& slashRecipient,
        CAmount slashAmount,
        CMutableTransaction& txOut);
};
```

**Implementation Details:**
- Contract P2SH: `OP_HASH160 <Hash160(contractScript)> OP_EQUAL`
- DAO slash spends bond output to slash recipient
- Requires DAO multisig authorization

### 13. Cross-Chain Trust Verification

**Location:** `src/cvm/trust_context.cpp` (extend existing)

**Purpose:** Verify cross-chain trust relationships with cryptographic proofs.

```cpp
// Interface for cross-chain verification
class CrossChainTrustVerifier {
public:
    // Verify cross-chain trust with LayerZero/CCIP
    static bool VerifyCrossChainTrust(
        const CrossChainTrustClaim& claim,
        const std::vector<uint8_t>& proof);
    
    // Verify merkle proof for reputation data
    static bool VerifyReputationMerkleProof(
        const uint256& root,
        const uint256& leaf,
        const std::vector<uint256>& proof,
        uint32_t index);
};
```

**Implementation Details:**
- LayerZero: Verify oracle + relayer signatures
- CCIP: Verify Chainlink DON attestations
- Merkle proof: Standard binary tree verification
- Leaf = Hash256(address || reputation || timestamp)

### 14. Cleanup Manager Resource Reclamation

**Location:** `src/cvm/cleanup_manager.cpp` (extend existing)

**Purpose:** Clean up expired contracts and reclaim storage.

```cpp
// Interface for cleanup management
class CleanupManager {
public:
    // Delete all storage entries for expired contract
    static bool DeleteContractStorage(const uint160& contractAddress);
    
    // Update contract metadata to reflect cleanup
    static bool UpdateContractMetadata(
        const uint160& contractAddress,
        bool isCleanedUp);
    
    // Reclaim storage space (compact database)
    static bool ReclaimStorage();
};
```

**Implementation Details:**
- Iterate keys with prefix `contract_<addr>_storage_`
- Delete each storage entry via LevelDB batch
- Set contract metadata `isCleanedUp = true`
- Trigger LevelDB compaction for space reclamation

### 15. Vote Manipulation Detection

**Location:** `src/cvm/vote_manipulation.cpp` (extend existing)

**Purpose:** Detect vote manipulation with accurate timing data.

```cpp
// Interface for vote manipulation detection
class VoteManipulationDetector {
public:
    // Get current block height for time-based calculations
    static int GetCurrentBlockHeight();
    
    // Prune reputation history older than threshold
    static void PruneReputationHistory(int maxAgeBlocks);
    
    // Detect suspicious voting patterns
    static bool DetectManipulation(
        const uint160& targetAddress,
        const std::vector<ReputationVote>& recentVotes);
};
```

**Implementation Details:**
- Use `chainActive.Height()` for current height
- Prune history older than 10,000 blocks by default
- Detect patterns: rapid voting, coordinated timing, circular voting

### 16. Eclipse and Sybil Protection

**Location:** `src/cvm/eclipse_sybil.cpp` (extend existing)

**Purpose:** Detect coordinated validator attacks through timing analysis.

```cpp
// Extended ValidatorNetworkInfo structure
struct ValidatorNetworkInfo {
    uint160 validatorAddress;
    std::vector<ValidationTimestamp> validationTimestamps;
    int64_t lastSeenTime;
    uint32_t responseCount;
    
    ADD_SERIALIZE_METHODS;
    // ... serialization
};

// Interface for attack detection
class EclipseSybilProtection {
public:
    // Store validation timestamp
    static void RecordValidationTimestamp(
        const uint160& validator,
        const uint256& taskHash,
        int64_t timestamp);
    
    // Detect coordinated timing attacks
    static bool DetectCoordinatedAttack(
        const uint256& taskHash,
        const std::vector<ValidationTimestamp>& timestamps);
};
```

**Implementation Details:**
- Store timestamps in `ValidatorNetworkInfo` per validator
- Coordinated attack: 5+ validators respond within 1 second
- Flag suspicious patterns for DAO review
- Maintain sliding window of last 100 validations per validator

## Data Models

### ValidatorEligibilityRecord (existing, ensure serialization)

```cpp
struct ValidatorEligibilityRecord {
    uint160 validatorAddress;
    CAmount stakeAmount;
    int stakeAge;
    int blocksSinceFirstSeen;
    int transactionCount;
    int uniqueInteractions;
    int64_t lastUpdateBlock;
    int64_t lastUpdateTime;
    bool isEligible;
    bool meetsStakeRequirement;
    bool meetsHistoryRequirement;
    bool meetsInteractionRequirement;
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(validatorAddress);
        READWRITE(stakeAmount);
        READWRITE(stakeAge);
        READWRITE(blocksSinceFirstSeen);
        READWRITE(transactionCount);
        READWRITE(uniqueInteractions);
        READWRITE(lastUpdateBlock);
        READWRITE(lastUpdateTime);
        READWRITE(isEligible);
        READWRITE(meetsStakeRequirement);
        READWRITE(meetsHistoryRequirement);
        READWRITE(meetsInteractionRequirement);
    }
};
```

### ValidationTimestamp (for eclipse/sybil protection)

```cpp
struct ValidationTimestamp {
    uint256 taskHash;
    int64_t timestamp;
    uint160 validatorAddress;
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(taskHash);
        READWRITE(timestamp);
        READWRITE(validatorAddress);
    }
};
```

### CrossChainTrustClaim (for cross-chain verification)

```cpp
struct CrossChainTrustClaim {
    uint32_t sourceChainId;
    uint160 sourceAddress;
    uint160 targetAddress;
    double trustScore;
    int64_t timestamp;
    uint256 proofRoot;
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sourceChainId);
        READWRITE(sourceAddress);
        READWRITE(targetAddress);
        READWRITE(trustScore);
        READWRITE(timestamp);
        READWRITE(proofRoot);
    }
};
```

### HATScore (for HAT v2 validation)

```cpp
struct HATScore {
    uint160 address;
    double score;
    int64_t calculatedAtBlock;
    int64_t expiresAtBlock;
    uint256 calculationHash;  // For verification
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(score);
        READWRITE(calculatedAtBlock);
        READWRITE(expiresAtBlock);
        READWRITE(calculationHash);
    }
};
```

### DisputeEvidence (for DAO integration)

```cpp
struct DisputeEvidence {
    uint256 disputeId;
    std::vector<ValidationResponse> validatorResponses;
    std::vector<uint8_t> trustGraphSnapshot;
    uint256 transactionHash;
    int64_t timestamp;
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(disputeId);
        READWRITE(validatorResponses);
        READWRITE(trustGraphSnapshot);
        READWRITE(transactionHash);
        READWRITE(timestamp);
    }
};
```

### ReputationVote (for vote manipulation detection)

```cpp
struct ReputationVote {
    uint160 voterAddress;
    uint160 targetAddress;
    int8_t voteValue;  // -1, 0, or +1
    int64_t blockHeight;
    int64_t timestamp;
    CAmount bondAmount;
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(voterAddress);
        READWRITE(targetAddress);
        READWRITE(voteValue);
        READWRITE(blockHeight);
        READWRITE(timestamp);
        READWRITE(bondAmount);
    }
};
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Signature Round-Trip

*For any* valid validation response and valid private key, signing the response and then verifying the signature with the corresponding public key SHALL succeed.

**Validates: Requirements 1.1, 1.2**

### Property 2: Invalid Signature Rejection

*For any* validation response with a corrupted or mismatched signature, signature verification SHALL fail.

**Validates: Requirements 1.3**

### Property 3: Address Derivation Consistency

*For any* valid secp256k1 public key, deriving the validator address SHALL produce the same result as standard Bitcoin P2PKH address derivation (Hash160 of public key).

**Validates: Requirements 1.4**

### Property 4: Wallet Key Retrieval Consistency

*For any* unlocked wallet with a valid primary address, retrieving the validator key SHALL return a key that can sign messages verifiable with the corresponding public key.

**Validates: Requirements 2.1, 2.2**

### Property 5: Stake Verification Accuracy

*For any* address with known UTXOs, scanning the UTXO set SHALL return the correct total stake amount and identify the oldest UTXO correctly.

**Validates: Requirements 4.1, 4.2**

### Property 6: History Verification Accuracy

*For any* address with known transaction history, scanning the blockchain SHALL return the correct first-seen block, transaction count, and unique interaction count.

**Validates: Requirements 4.3, 4.4**

### Property 7: Stake Diversity Enforcement

*For any* set of UTXOs, the eligibility check SHALL correctly enforce the requirement of 3+ diverse funding sources.

**Validates: Requirements 4.5**

### Property 8: Database Persistence Round-Trip

*For any* valid ValidatorEligibilityRecord, writing to the database and then reading back SHALL produce an equivalent record.

**Validates: Requirements 5.1, 5.2, 5.3**

### Property 9: DAO Evidence Serialization Round-Trip

*For any* valid DisputeEvidence, serializing and deserializing SHALL produce an equivalent record with all validator responses and trust data intact.

**Validates: Requirements 6.2, 6.3**

### Property 10: Block Height Consistency

*For any* call to GetCurrentBlockHeight across mempool manager, vote manipulation detector, and other components, the returned value SHALL match chainActive.Height().

**Validates: Requirements 7.1, 15.1**

### Property 11: Gas Price Multiplier Bounds

*For any* network congestion level, the gas price multiplier SHALL be within the range [0.5, 2.0].

**Validates: Requirements 8.1**

### Property 12: Reputation Fallback Correctness

*For any* address, when HAT v2 is unavailable, the fallback to ASRS SHALL return a valid reputation score.

**Validates: Requirements 9.1**

### Property 13: Sender Address Extraction

*For any* valid CVM transaction with P2PKH or P2WPKH inputs, extracting the sender address SHALL return the correct address that signed the first input.

**Validates: Requirements 9.2, 11.1**

### Property 14: Soft Fork Activation Consistency

*For any* block height, the CVM activation check SHALL return true if and only if the height is >= the configured activation height in chainparams.

**Validates: Requirements 10.1**

### Property 15: HAT Score Expiration

*For any* HAT score with calculatedAtBlock and expiresAtBlock, the score SHALL be considered valid if and only if currentHeight < expiresAtBlock.

**Validates: Requirements 10.2**

### Property 16: Reputation Threshold Enforcement

*For any* deployer reputation value, the deployment check SHALL accept if and only if reputation >= minimum threshold (50).

**Validates: Requirements 11.2**

### Property 17: Contract Format Verification

*For any* contract data with a format byte, the format verification SHALL succeed if and only if the format byte matches the expected format.

**Validates: Requirements 11.3**

### Property 18: Contract P2SH Resolution

*For any* valid contract address, resolving the P2SH script SHALL produce a valid P2SH scriptPubKey that can receive funds.

**Validates: Requirements 12.1**

### Property 19: Merkle Proof Verification

*For any* valid merkle proof and leaf, verification SHALL succeed. *For any* invalid proof (wrong path, wrong root, or tampered leaf), verification SHALL fail.

**Validates: Requirements 13.2**

### Property 20: Contract Cleanup Completeness

*For any* expired contract, after cleanup, querying storage for that contract SHALL return no entries.

**Validates: Requirements 14.1, 14.2**

### Property 21: Timing Pattern Detection

*For any* set of validation timestamps from multiple validators, if timestamps are suspiciously clustered (within 1 second of each other for 5+ validators), the system SHALL flag this as a potential coordinated attack.

**Validates: Requirements 16.1, 16.2**

## Error Handling

### Wallet Errors

| Error Condition | Handling |
|----------------|----------|
| Wallet locked | Return error, log warning, skip validation participation |
| Key not found | Return error, log error, skip validation participation |
| Wallet unavailable | Return error, log warning, disable validator features |

### P2P Errors

| Error Condition | Handling |
|----------------|----------|
| No connman | Log warning, skip broadcast (non-fatal) |
| Peer disconnected | Skip peer, continue with others |
| Message too large | Split into batches or reject |
| DAO notification failure | Retry up to 3 times, then log error |

### Database Errors

| Error Condition | Handling |
|----------------|----------|
| Write failure | Log error, return false, retry once |
| Read failure | Return default/empty, log warning |
| Corruption detected | Log critical, trigger recovery |
| Iteration failure | Return partial results, log warning |

### Chain Access Errors

| Error Condition | Handling |
|----------------|----------|
| Chain not synced | Return stale data with warning flag |
| UTXO scan timeout | Return partial results, log warning |
| Index unavailable | Fall back to block scanning |
| Block height unavailable | Use cached value, log warning |

### Consensus Validation Errors

| Error Condition | Handling |
|----------------|----------|
| HAT v2 unavailable | Fall back to ASRS, log info |
| Sender extraction failure | Reject transaction, log error |
| Pool balance query failure | Use cached value or reject |
| Gas extraction failure | Reject transaction, log error |

### Cross-Chain Errors

| Error Condition | Handling |
|----------------|----------|
| Invalid proof | Reject claim, log warning |
| Unsupported chain | Return error, log info |
| Proof verification timeout | Reject claim, log warning |

### Cleanup Errors

| Error Condition | Handling |
|----------------|----------|
| Storage deletion failure | Log error, mark for retry |
| Metadata update failure | Log error, continue cleanup |
| Compaction failure | Log warning, schedule retry |

### Security Detection Errors

| Error Condition | Handling |
|----------------|----------|
| Timestamp storage failure | Log warning, continue detection |
| Pattern analysis timeout | Use partial data, log warning |
| False positive detected | Log info, allow manual review |

## Testing Strategy

### Unit Tests

Unit tests will verify specific examples and edge cases:

1. **Cryptographic Operations**
   - Test signing with known test vectors
   - Test verification with valid/invalid signatures
   - Test address derivation with known keys

2. **Wallet Integration**
   - Test with mock wallet (locked/unlocked states)
   - Test key retrieval success/failure

3. **Database Operations**
   - Test write/read cycle
   - Test iteration over multiple records
   - Test handling of missing records

4. **Consensus Validation**
   - Test HAT v2 to ASRS fallback
   - Test sender address extraction from various input types
   - Test gas info extraction from CVM transactions

5. **Soft Fork Validation**
   - Test deployer reputation threshold enforcement
   - Test contract format verification
   - Test CVM activation height checks

6. **Cross-Chain Verification**
   - Test merkle proof verification with known proofs
   - Test invalid proof rejection

7. **Cleanup Operations**
   - Test storage deletion completeness
   - Test metadata update correctness

8. **Security Detection**
   - Test timing pattern detection with known attack patterns
   - Test false positive handling

### Property-Based Tests

Property-based tests will use the **Boost.Test** framework with custom generators to verify universal properties across many inputs.

**Configuration:**
- Minimum 100 iterations per property test
- Use `BOOST_DATA_TEST_CASE` for data-driven tests
- Custom generators for CVM data types

**Test File Location:** `src/test/cvm_todo_tests.cpp`

**Property Test Tags:**
Each property test will be annotated with:
```cpp
// Feature: cvm-todo-completion, Property N: [Property Title]
// Validates: Requirements X.Y
```

**Property Test Coverage:**
- Properties 1-3: Cryptographic operations
- Property 4: Wallet key retrieval
- Properties 5-7: Blockchain data access
- Properties 8-9: Database and serialization
- Properties 10-11: Block height and gas pricing
- Properties 12-13: Reputation and sender extraction
- Properties 14-17: Soft fork and validation
- Properties 18-19: Contract and merkle operations
- Properties 20-21: Cleanup and security

### Integration Tests

Integration tests in `test/functional/`:

1. **P2P Validator Communication** - Test message flow between nodes
2. **End-to-End Validation** - Test complete validation cycle
3. **Soft Fork Activation** - Test CVM activation at correct height
4. **DAO Dispute Flow** - Test dispute escalation and notification
5. **Cross-Chain Trust** - Test cross-chain claim verification
6. **Contract Lifecycle** - Test deployment, execution, and cleanup
