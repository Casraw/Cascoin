# Design Document: L2 Burn-and-Mint Token Model

## Overview

Dieses Design-Dokument spezifiziert das **Burn-and-Mint Token-Modell** für Cascoin L2. Das System verwendet OP_RETURN Transaktionen auf L1 um CAS kryptographisch sicher zu verbrennen und entsprechende L2-Tokens zu minten.

### Design Rationale

**Warum OP_RETURN statt Burn-Adresse?**
- Eine Burn-Adresse in chainparams könnte von einem böswilligen Actor modifiziert werden
- OP_RETURN Outputs sind **kryptographisch garantiert unspendable** - kein Private Key existiert
- Jeder Node kann unabhängig verifizieren dass ein OP_RETURN Output nicht ausgegeben werden kann
- Keine Vertrauensannahmen über Adressen notwendig

**Warum Sequencer-Konsens?**
- Ein einzelner Sequencer könnte seinen Code modifizieren um ungültige Burns zu akzeptieren
- Mit 2/3 Konsens müsste ein Angreifer die Mehrheit der Sequencer kontrollieren
- Dezentralisierung schützt vor einzelnen böswilligen Akteuren

## Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         L1 (Cascoin Mainchain)                       │
│                                                                      │
│  User Wallet                    L1 Blockchain                        │
│  ┌─────────────┐               ┌─────────────────────────────────┐  │
│  │   100 CAS   │──── Burn ────►│  TX: OP_RETURN "L2BURN" ...     │  │
│  │             │    TX         │  (100 CAS permanently destroyed) │  │
│  └─────────────┘               └─────────────────────────────────┘  │
│                                          │                           │
└──────────────────────────────────────────┼───────────────────────────┘
                                           │ L1 TX beobachten
                                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         L2 Sequencer Network                         │
│                                                                      │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │
│  │ Sequencer 1 │  │ Sequencer 2 │  │ Sequencer 3 │  ...             │
│  │  Validiert  │  │  Validiert  │  │  Validiert  │                  │
│  │  OP_RETURN  │  │  OP_RETURN  │  │  OP_RETURN  │                  │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                  │
│         │                │                │                          │
│         └────────────────┼────────────────┘                          │
│                          │ 2/3 Konsens                               │
│                          ▼                                           │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    MINT 100 L2-Tokens                        │    │
│  │                    an recipient_pubkey                       │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```


### Burn Transaction Flow

```
┌──────────────────────────────────────────────────────────────────┐
│                    BURN-AND-MINT FLOW                            │
│                                                                  │
│  1. User erstellt Burn-TX auf L1                                 │
│     ┌─────────────────────────────────────────────────────────┐  │
│     │  Input: 100.001 CAS (100 zum Burnen + 0.001 TX Fee)     │  │
│     │  Output 0: OP_RETURN "L2BURN" <chain_id> <pubkey> <amt> │  │
│     │  Output 1: 0.0 CAS (Change, falls vorhanden)            │  │
│     │                                                          │  │
│     │  Effekt: 100 CAS sind PERMANENT ZERSTÖRT                │  │
│     └─────────────────────────────────────────────────────────┘  │
│                                                                  │
│  2. L1 TX wird in Block aufgenommen                              │
│     └─► Warten auf 6 Bestätigungen                               │
│                                                                  │
│  3. Sequencer erkennen die Burn-TX                               │
│     ├─► Sequencer 1: "Ich sehe gültigen Burn, signiere"          │
│     ├─► Sequencer 2: "Ich sehe gültigen Burn, signiere"          │
│     └─► Sequencer 3: "Ich sehe gültigen Burn, signiere"          │
│                                                                  │
│  4. 2/3 Konsens erreicht                                         │
│     └─► L2 State Manager mintet 100 L2-Tokens                    │
│                                                                  │
│  5. User hat 100 L2-Tokens                                       │
│     └─► Kann auf L2 transferieren, Smart Contracts nutzen, etc.  │
└──────────────────────────────────────────────────────────────────┘
```

## Components and Interfaces

### 1. BurnTransaction Parser (`src/l2/burn_parser.h/cpp`)

```cpp
// OP_RETURN Burn Format
// OP_RETURN "L2BURN" <chain_id:4> <recipient:33> <amount:8>
// Total: 6 + 4 + 33 + 8 = 51 bytes

struct BurnData {
    uint32_t chainId;           // L2 Chain ID
    CPubKey recipientPubKey;    // 33-byte compressed pubkey
    CAmount amount;             // Amount burned (in satoshis)
    
    // Derived
    uint160 recipientAddress;   // Derived from pubkey
    
    bool IsValid() const;
    static std::optional<BurnData> Parse(const CScript& script);
};

class BurnTransactionParser {
public:
    // Parse OP_RETURN output from L1 transaction
    static std::optional<BurnData> ParseBurnTransaction(const CTransaction& tx);
    
    // Validate burn transaction format
    static bool ValidateBurnFormat(const CScript& script);
    
    // Calculate burned amount from transaction
    static CAmount CalculateBurnedAmount(const CTransaction& tx);
    
    // Create burn transaction script
    static CScript CreateBurnScript(
        uint32_t chainId,
        const CPubKey& recipient,
        CAmount amount
    );
    
private:
    static constexpr const char* BURN_MARKER = "L2BURN";
    static constexpr size_t BURN_DATA_SIZE = 51;  // 6 + 4 + 33 + 8
};
```

### 2. Burn Validator (`src/l2/burn_validator.h/cpp`)

```cpp
// Validation result
struct BurnValidationResult {
    bool isValid;
    std::string errorMessage;
    BurnData burnData;
    int confirmations;
    uint256 blockHash;
};

class BurnValidator {
public:
    // Validate a burn transaction
    BurnValidationResult ValidateBurn(const uint256& l1TxHash);
    
    // Check if burn has enough confirmations
    bool HasSufficientConfirmations(const uint256& l1TxHash) const;
    
    // Check if burn matches our chain ID
    bool MatchesChainId(const BurnData& data) const;
    
    // Check if burn was already processed
    bool IsAlreadyProcessed(const uint256& l1TxHash) const;
    
private:
    uint32_t chainId_;
    static constexpr int REQUIRED_CONFIRMATIONS = 6;
    
    // Fetch transaction from L1
    std::optional<CTransaction> FetchL1Transaction(const uint256& txHash);
    
    // Get confirmation count
    int GetConfirmationCount(const uint256& txHash) const;
};
```


### 3. Mint Consensus Manager (`src/l2/mint_consensus.h/cpp`)

```cpp
// Mint confirmation from a sequencer
struct MintConfirmation {
    uint256 l1TxHash;           // The burn transaction hash
    uint160 l2Recipient;        // Recipient on L2
    CAmount amount;             // Amount to mint
    uint160 sequencerAddress;   // Confirming sequencer
    std::vector<uint8_t> signature;  // Sequencer's signature
    uint64_t timestamp;
    
    uint256 GetHash() const;
    bool VerifySignature(const CPubKey& sequencerPubKey) const;
};

// Consensus state for a burn
struct MintConsensusState {
    uint256 l1TxHash;
    BurnData burnData;
    std::map<uint160, MintConfirmation> confirmations;
    uint64_t firstSeenTime;
    ConsensusStatus status;
    
    enum class ConsensusStatus {
        PENDING,        // Waiting for confirmations
        REACHED,        // 2/3 consensus reached
        MINTED,         // Tokens minted
        FAILED,         // Consensus failed (timeout or invalid)
        REJECTED        // Explicitly rejected
    };
    
    double GetConfirmationRatio(size_t totalSequencers) const;
    bool HasReachedConsensus(size_t totalSequencers) const;
};

class MintConsensusManager {
public:
    // Submit a mint confirmation (called by local sequencer)
    bool SubmitConfirmation(const MintConfirmation& confirmation);
    
    // Process incoming confirmation from network
    void ProcessConfirmation(const MintConfirmation& confirmation, CNode* pfrom);
    
    // Check if consensus reached for a burn
    bool HasConsensus(const uint256& l1TxHash) const;
    
    // Get consensus state
    std::optional<MintConsensusState> GetConsensusState(const uint256& l1TxHash) const;
    
    // Get all pending burns
    std::vector<MintConsensusState> GetPendingBurns() const;
    
    // Process consensus timeout
    void ProcessTimeouts();
    
private:
    std::map<uint256, MintConsensusState> consensusStates_;
    mutable CCriticalSection cs_consensus_;
    
    static constexpr double CONSENSUS_THRESHOLD = 0.67;  // 2/3
    static constexpr uint64_t CONSENSUS_TIMEOUT = 600;   // 10 minutes
    
    // Broadcast confirmation to other sequencers
    void BroadcastConfirmation(const MintConfirmation& confirmation);
    
    // Trigger minting when consensus reached
    void TriggerMinting(const MintConsensusState& state);
};
```

### 4. Burn Registry (`src/l2/burn_registry.h/cpp`)

```cpp
// Record of a processed burn
struct BurnRecord {
    uint256 l1TxHash;           // L1 burn transaction
    uint64_t l1BlockNumber;     // L1 block containing burn
    uint256 l1BlockHash;        // L1 block hash
    uint160 l2Recipient;        // Recipient on L2
    CAmount amount;             // Amount burned/minted
    uint64_t l2MintBlock;       // L2 block where minted
    uint256 l2MintTxHash;       // L2 mint transaction
    uint64_t timestamp;         // When processed
    
    ADD_SERIALIZE_METHODS;
};

class BurnRegistry {
public:
    // Check if burn was already processed
    bool IsProcessed(const uint256& l1TxHash) const;
    
    // Record a processed burn
    bool RecordBurn(const BurnRecord& record);
    
    // Get burn record
    std::optional<BurnRecord> GetBurnRecord(const uint256& l1TxHash) const;
    
    // Get all burns for an address
    std::vector<BurnRecord> GetBurnsForAddress(const uint160& address) const;
    
    // Get total burned amount
    CAmount GetTotalBurned() const;
    
    // Get burn history (paginated)
    std::vector<BurnRecord> GetBurnHistory(uint64_t fromBlock, uint64_t toBlock) const;
    
    // Handle L2 reorg - unmark burns as processed
    void HandleReorg(uint64_t reorgFromBlock);
    
private:
    // LevelDB storage
    // Key: "burn_<l1TxHash>" -> BurnRecord
    // Key: "burn_addr_<address>_<l1TxHash>" -> (index for address lookup)
    // Key: "burn_total" -> CAmount
};
```


### 5. L2 Token Minter (`src/l2/l2_minter.h/cpp`)

```cpp
// Mint result
struct MintResult {
    bool success;
    std::string errorMessage;
    uint256 l2TxHash;
    uint64_t l2BlockNumber;
    CAmount amountMinted;
};

class L2TokenMinter {
public:
    // Mint tokens after consensus (called by consensus manager)
    MintResult MintTokens(
        const uint256& l1TxHash,
        const uint160& recipient,
        CAmount amount
    );
    
    // Get current L2 token supply
    CAmount GetTotalSupply() const;
    
    // Verify supply invariant
    bool VerifySupplyInvariant() const;
    
    // Get balance for address
    CAmount GetBalance(const uint160& address) const;
    
private:
    L2StateManager& stateManager_;
    BurnRegistry& burnRegistry_;
    
    // Update state atomically
    bool UpdateState(const uint160& recipient, CAmount amount);
    
    // Emit mint event
    void EmitMintEvent(const uint256& l1TxHash, const uint160& recipient, CAmount amount);
};
```

### 6. Sequencer Fee Distributor (`src/l2/fee_distributor.h/cpp`)

```cpp
// Fee distribution for a block
struct BlockFeeDistribution {
    uint64_t blockNumber;
    uint160 sequencerAddress;
    CAmount totalFees;
    uint32_t transactionCount;
    uint64_t timestamp;
};

class FeeDistributor {
public:
    // Distribute fees to block producer
    bool DistributeBlockFees(
        uint64_t blockNumber,
        const uint160& sequencer,
        const std::vector<CTransaction>& transactions
    );
    
    // Calculate total fees in block
    CAmount CalculateBlockFees(const std::vector<CTransaction>& transactions) const;
    
    // Get fee history for sequencer
    std::vector<BlockFeeDistribution> GetFeeHistory(
        const uint160& sequencer,
        uint64_t fromBlock,
        uint64_t toBlock
    ) const;
    
    // Get total fees earned by sequencer
    CAmount GetTotalFeesEarned(const uint160& sequencer) const;
    
private:
    static constexpr CAmount MIN_TRANSACTION_FEE = 1000;  // 0.00001 L2-Token
    
    // Credit fees to sequencer
    void CreditFees(const uint160& sequencer, CAmount amount);
};
```

### 7. RPC Interface (`src/rpc/l2_burn.cpp`)

```cpp
// Create burn transaction
// l2_createburntx <amount> <l2_recipient_address>
UniValue l2_createburntx(const JSONRPCRequest& request);

// Broadcast burn transaction
// l2_sendburntx <hex_tx>
UniValue l2_sendburntx(const JSONRPCRequest& request);

// Get burn status
// l2_getburnstatus <l1_tx_hash>
UniValue l2_getburnstatus(const JSONRPCRequest& request);

// Get pending burns (waiting for consensus)
// l2_getpendingburns
UniValue l2_getpendingburns(const JSONRPCRequest& request);

// Get mint history
// l2_getminthistory [from_block] [to_block]
UniValue l2_getminthistory(const JSONRPCRequest& request);

// Get total supply
// l2_gettotalsupply
UniValue l2_gettotalsupply(const JSONRPCRequest& request);

// Verify supply invariant
// l2_verifysupply
UniValue l2_verifysupply(const JSONRPCRequest& request);

// Get burns for address
// l2_getburnsforaddress <address>
UniValue l2_getburnsforaddress(const JSONRPCRequest& request);
```


## Data Models

### OP_RETURN Burn Script Format

```
┌─────────────────────────────────────────────────────────────────┐
│                    OP_RETURN BURN FORMAT                        │
│                                                                 │
│  Byte 0:      OP_RETURN (0x6a)                                  │
│  Byte 1:      OP_PUSHDATA (length of data)                      │
│  Bytes 2-7:   "L2BURN" (6 bytes, ASCII marker)                  │
│  Bytes 8-11:  chain_id (4 bytes, little-endian uint32)          │
│  Bytes 12-44: recipient_pubkey (33 bytes, compressed)           │
│  Bytes 45-52: amount (8 bytes, little-endian int64, satoshis)   │
│                                                                 │
│  Total: 53 bytes (1 + 1 + 6 + 4 + 33 + 8)                       │
│                                                                 │
│  Example:                                                       │
│  6a 33 4c324255524e 01000000 02abc...def 00e1f50500000000       │
│  │  │  │            │        │           │                      │
│  │  │  │            │        │           └─ 100 CAS (1e8 sat)   │
│  │  │  │            │        └─ recipient pubkey                │
│  │  │  │            └─ chain_id = 1                             │
│  │  │  └─ "L2BURN"                                              │
│  │  └─ push 51 bytes                                            │
│  └─ OP_RETURN                                                   │
└─────────────────────────────────────────────────────────────────┘
```

### Database Schema

```
LevelDB Keys for Burn-and-Mint:

# Burn Registry
burn_record_<l1TxHash>           → BurnRecord (serialized)
burn_by_addr_<address>_<l1TxHash> → uint8_t (1, just for index)
burn_total_amount                → CAmount (total burned)
burn_count                       → uint64_t (number of burns)

# Consensus State (temporary, cleared after minting)
consensus_<l1TxHash>             → MintConsensusState (serialized)
consensus_pending                → set<uint256> (pending l1TxHashes)

# Supply Tracking
l2_total_supply                  → CAmount
l2_total_minted                  → CAmount (should equal total_supply)
l2_total_burned_l1               → CAmount (CAS burned on L1)

# Fee Distribution
fee_dist_<blockNumber>           → BlockFeeDistribution (serialized)
fee_total_<sequencerAddr>        → CAmount (total fees earned)
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do.*

### Property 1: OP_RETURN Format Validation
*For any* byte sequence, the system SHALL accept it as a valid burn script if and only if it starts with OP_RETURN, contains the "L2BURN" marker, and has exactly 51 bytes of payload with valid chain_id, pubkey, and amount.
**Validates: Requirements 1.2, 2.1**

### Property 2: Burn Amount Consistency
*For any* valid burn transaction, the amount encoded in the OP_RETURN payload SHALL equal the sum of inputs minus the sum of spendable outputs minus the transaction fee.
**Validates: Requirements 1.4, 4.2**

### Property 3: Double-Mint Prevention
*For any* L1 transaction hash, the system SHALL mint L2 tokens at most once. If a mint request references an already-processed L1 transaction, the system SHALL reject it.
**Validates: Requirements 2.4, 5.3, 5.4**

### Property 4: Consensus Threshold
*For any* burn transaction, the system SHALL mint tokens if and only if at least 2/3 of active sequencers have submitted valid confirmations for that burn.
**Validates: Requirements 3.1, 3.4, 10.3**

### Property 5: 1:1 Mint Ratio
*For any* successful mint operation, the amount of L2 tokens minted SHALL exactly equal the amount of CAS burned on L1 (as encoded in the OP_RETURN).
**Validates: Requirements 4.2**

### Property 6: Supply Invariant
*For any* L2 chain state, the total L2 token supply SHALL equal the sum of all CAS amounts burned on L1 (as recorded in the burn registry). Additionally, the sum of all L2 balances SHALL equal the total supply.
**Validates: Requirements 8.1, 8.3**

### Property 7: Confirmation Uniqueness
*For any* sequencer and burn transaction, the system SHALL accept at most one confirmation. Duplicate confirmations from the same sequencer SHALL be rejected.
**Validates: Requirements 3.6**

### Property 8: Chain ID Validation
*For any* burn transaction, the system SHALL only process it if the chain_id in the OP_RETURN matches the current L2 chain's ID.
**Validates: Requirements 2.3**

### Property 9: Confirmation Count Requirement
*For any* burn transaction, the system SHALL only begin consensus if the L1 transaction has at least 6 confirmations.
**Validates: Requirements 2.2**

### Property 10: Fee-Only Sequencer Rewards
*For any* L2 block, the sequencer reward SHALL equal exactly the sum of transaction fees in that block. No new tokens SHALL be minted as block rewards.
**Validates: Requirements 6.1, 6.2, 6.3**


## Error Handling

### Burn Transaction Errors

| Error Code | Condition | Message |
|------------|-----------|---------|
| `L2_BURN_INVALID_FORMAT` | OP_RETURN format wrong | "Invalid burn transaction format" |
| `L2_BURN_WRONG_MARKER` | Missing "L2BURN" marker | "Missing L2BURN marker in OP_RETURN" |
| `L2_BURN_WRONG_CHAIN` | chain_id mismatch | "Burn transaction is for different L2 chain" |
| `L2_BURN_INVALID_PUBKEY` | Invalid recipient pubkey | "Invalid recipient public key" |
| `L2_BURN_ZERO_AMOUNT` | Amount is zero | "Burn amount must be greater than zero" |
| `L2_BURN_AMOUNT_MISMATCH` | Encoded != actual burned | "Burn amount does not match transaction" |

### Consensus Errors

| Error Code | Condition | Message |
|------------|-----------|---------|
| `L2_CONSENSUS_NOT_SEQUENCER` | Signer not a sequencer | "Confirmation from non-sequencer" |
| `L2_CONSENSUS_INVALID_SIG` | Bad signature | "Invalid confirmation signature" |
| `L2_CONSENSUS_DUPLICATE` | Already confirmed | "Sequencer already confirmed this burn" |
| `L2_CONSENSUS_TIMEOUT` | 10 min without 2/3 | "Consensus timeout for burn" |
| `L2_CONSENSUS_INSUFFICIENT` | < 3 sequencers | "Insufficient sequencers for consensus" |

### Minting Errors

| Error Code | Condition | Message |
|------------|-----------|---------|
| `L2_MINT_ALREADY_PROCESSED` | Burn already minted | "Burn transaction already processed" |
| `L2_MINT_NO_CONSENSUS` | Consensus not reached | "Consensus not reached for this burn" |
| `L2_MINT_INSUFFICIENT_CONF` | < 6 L1 confirmations | "Burn transaction needs more confirmations" |
| `L2_MINT_STATE_ERROR` | State update failed | "Failed to update L2 state" |

### Supply Errors

| Error Code | Condition | Message |
|------------|-----------|---------|
| `L2_SUPPLY_INVARIANT_VIOLATED` | Supply != sum(balances) | "Supply invariant violated - critical error" |
| `L2_SUPPLY_MISMATCH` | Minted != burned | "Minted amount does not match burned amount" |

## Security Considerations

### Why OP_RETURN is Secure

```
┌─────────────────────────────────────────────────────────────────┐
│                    OP_RETURN SECURITY                           │
│                                                                 │
│  OP_RETURN outputs are PROVABLY UNSPENDABLE because:            │
│                                                                 │
│  1. Bitcoin Script Semantics:                                   │
│     - OP_RETURN immediately terminates script execution         │
│     - Any script starting with OP_RETURN always fails           │
│     - No valid scriptSig can satisfy an OP_RETURN scriptPubKey  │
│                                                                 │
│  2. No Private Key Attack:                                      │
│     - Unlike burn addresses, there's no "key" to find           │
│     - Even quantum computers can't spend OP_RETURN outputs      │
│     - The unspendability is mathematical, not probabilistic     │
│                                                                 │
│  3. Node Consensus:                                             │
│     - All nodes agree OP_RETURN outputs are unspendable         │
│     - This is part of the consensus rules, not configurable     │
│     - A modified node would fork from the network               │
│                                                                 │
│  Comparison to Burn Address:                                    │
│  ┌─────────────────┬─────────────────────────────────────────┐  │
│  │ Burn Address    │ OP_RETURN                               │  │
│  ├─────────────────┼─────────────────────────────────────────┤  │
│  │ Configurable    │ Hardcoded in consensus                  │  │
│  │ Could be hacked │ Mathematically impossible               │  │
│  │ Trust required  │ Trustless verification                  │  │
│  │ Key might exist │ No key concept                          │  │
│  └─────────────────┴─────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Consensus Attack Resistance

```
┌─────────────────────────────────────────────────────────────────┐
│                    ATTACK SCENARIOS                             │
│                                                                 │
│  Attack 1: Single Malicious Sequencer                           │
│  ─────────────────────────────────────                          │
│  Scenario: Sequencer modifies code to accept fake burns         │
│  Defense: Needs 2/3 consensus - one sequencer can't mint alone  │
│  Result: Attack fails, malicious sequencer is identified        │
│                                                                 │
│  Attack 2: Fake Burn Transaction                                │
│  ─────────────────────────────────                              │
│  Scenario: Attacker creates TX that looks like burn but isn't   │
│  Defense: All sequencers verify OP_RETURN format independently  │
│  Result: Invalid format rejected by all honest sequencers       │
│                                                                 │
│  Attack 3: Double-Mint                                          │
│  ─────────────────────────────                                  │
│  Scenario: Submit same burn TX twice to mint twice              │
│  Defense: Burn Registry tracks all processed L1 TX hashes       │
│  Result: Second attempt rejected as "already processed"         │
│                                                                 │
│  Attack 4: Majority Sequencer Collusion                         │
│  ─────────────────────────────────────                          │
│  Scenario: 2/3+ sequencers collude to mint without real burn    │
│  Defense: L1 TX must exist and be verifiable by anyone          │
│  Result: Fraud is publicly provable, sequencers lose reputation │
│                                                                 │
│  Attack 5: L1 Reorg After Mint                                  │
│  ───────────────────────────────                                │
│  Scenario: L1 reorg removes burn TX after L2 already minted     │
│  Defense: Require 6 confirmations before consensus starts       │
│  Result: 6-block reorg extremely unlikely, risk acceptable      │
└─────────────────────────────────────────────────────────────────┘
```

## Testing Strategy

### Unit Tests

1. **OP_RETURN Parsing**
   - Valid burn script parsing
   - Invalid marker rejection
   - Wrong length rejection
   - Invalid pubkey rejection

2. **Burn Validation**
   - Confirmation count checking
   - Chain ID matching
   - Amount calculation

3. **Consensus**
   - Confirmation collection
   - Threshold calculation
   - Duplicate rejection
   - Timeout handling

4. **Minting**
   - Successful mint
   - Double-mint prevention
   - State update atomicity

### Property-Based Tests

- **Property 1**: Generate random scripts, verify only valid formats accepted
- **Property 3**: Generate sequences of mint requests, verify no double-mints
- **Property 6**: After any operation sequence, verify supply invariant

### Integration Tests

1. **Full Burn-Mint Flow**: Create burn TX → Wait confirmations → Consensus → Mint
2. **Multi-Sequencer Consensus**: 5 sequencers, verify 2/3 threshold
3. **Reorg Handling**: Simulate L2 reorg, verify burn registry updates

