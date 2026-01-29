# Cascoin CVM Blockchain Integration Guide

This guide documents the transaction format for CVM smart contracts and mempool integration details for developers building on Cascoin.

---

## Table of Contents

1. [Transaction Format](#transaction-format)
2. [Soft Fork Compatibility](#soft-fork-compatibility)
3. [Mempool Integration](#mempool-integration)
4. [Block Validation](#block-validation)
5. [Gas System Integration](#gas-system-integration)
6. [Receipt Storage](#receipt-storage)
7. [State Synchronization](#state-synchronization)

---

## Transaction Format

### Overview

Cascoin EVM transactions use a soft-fork compatible format based on OP_RETURN outputs. This ensures:

- **Backward Compatibility**: Old nodes see valid transactions (just unspendable outputs)
- **No Chain Splits**: Gradual upgrade without consensus failures
- **Standard Bitcoin Format**: Compatible with existing wallet infrastructure

### OP_RETURN Structure

```
OP_RETURN <CVM_MAGIC> <OpType> <Data>

CVM_MAGIC: 0x43564d31 ("CVM1")
OpType:    1 byte operation type
Data:      Variable length payload
```

### Operation Types

| OpType | Value | Description |
|--------|-------|-------------|
| CONTRACT_DEPLOY | 0x01 | Deploy CVM contract |
| CONTRACT_CALL | 0x02 | Call CVM contract |
| REPUTATION_VOTE | 0x03 | Simple reputation vote |
| TRUST_EDGE | 0x04 | Web-of-Trust relationship |
| BONDED_VOTE | 0x05 | Bonded reputation vote |
| DAO_DISPUTE | 0x06 | Create DAO dispute |
| DAO_VOTE | 0x07 | Vote on DAO dispute |
| EVM_DEPLOY | 0x08 | Deploy EVM contract (explicit) |
| EVM_CALL | 0x09 | Call EVM contract (explicit) |

### Contract Deployment Transaction

```
Transaction Structure:
├── Inputs
│   └── Input 0: Funding from deployer (covers gas + contract value)
├── Outputs
│   ├── Output 0: OP_RETURN with deployment metadata
│   │   └── CVM_MAGIC + OpType(0x08) + DeployData
│   ├── Output 1: Contract address (P2SH)
│   │   └── Value: Initial contract balance
│   └── Output 2: Change back to deployer
└── Witness (if SegWit)
    └── Full contract bytecode + constructor data
```

### Deployment Data Structure

```cpp
struct CVMDeployData {
    uint256 codeHash;              // SHA256 of contract bytecode
    uint64_t gasLimit;             // Gas limit for deployment
    BytecodeFormat format;         // 0=CVM, 1=EVM, 2=AUTO
    std::vector<uint8_t> metadata; // Additional metadata (max 32 bytes)
};
```

Serialized format (in OP_RETURN):
```
[32 bytes: codeHash]
[8 bytes: gasLimit (little-endian)]
[1 byte: format]
[0-32 bytes: metadata]
```

### Contract Call Transaction

```
Transaction Structure:
├── Inputs
│   └── Input 0: Funding from caller (covers gas)
├── Outputs
│   ├── Output 0: OP_RETURN with call metadata
│   │   └── CVM_MAGIC + OpType(0x09) + CallData
│   ├── Output 1: Contract address (P2SH)
│   │   └── Value: CAS to send to contract
│   └── Output 2: Change back to caller
└── Witness (if SegWit)
    └── Extended call data (if > 32 bytes)
```

### Call Data Structure

```cpp
struct CVMCallData {
    uint160 contractAddress;       // Target contract (20 bytes)
    uint64_t gasLimit;             // Gas limit
    BytecodeFormat format;         // Expected format (0=CVM, 1=EVM, 2=AUTO)
    std::vector<uint8_t> callData; // Function selector + args (max 32 bytes in OP_RETURN)
};
```

Serialized format:
```
[20 bytes: contractAddress]
[8 bytes: gasLimit (little-endian)]
[1 byte: format]
[0-32 bytes: callData]
```

### Extended Call Data

For call data exceeding 32 bytes, use witness data:

```
OP_RETURN: Contains first 32 bytes of call data
Witness:   Contains full call data (up to 24KB)
```

### Bytecode Format Detection

The system automatically detects bytecode format:

```cpp
enum class BytecodeFormat : uint8_t {
    UNKNOWN = 0,
    CVM = 1,      // Register-based Cascoin VM
    EVM = 2,      // Stack-based Ethereum VM
    AUTO = 3      // Auto-detect from bytecode patterns
};
```

Detection algorithm:
1. Check for EVM patterns (PUSH1-PUSH32 sequences)
2. Check for CVM patterns (register opcodes)
3. Default to EVM if ambiguous

---

## Soft Fork Compatibility

### Activation

EVM features activate via soft fork at a specific block height:

| Network | Activation Height | Status |
|---------|------------------|--------|
| Mainnet | 500,000 | Pending |
| Testnet | 100,000 | Active |
| Regtest | 0 | Always Active |

### Old Node Behavior

Old nodes (pre-EVM) see CVM transactions as:
- Valid Bitcoin transactions
- OP_RETURN outputs (unspendable, ignored)
- Normal value transfers

This ensures:
- No consensus failures
- No chain splits
- Gradual network upgrade

### New Node Behavior

New nodes (EVM-enabled):
1. Parse OP_RETURN for CVM magic bytes
2. Validate CVM/EVM transaction rules
3. Execute contracts during block validation
4. Store contract state in database

### Version Bits

EVM activation uses BIP9 version bits:

```cpp
// In consensus/params.h
static constexpr int DEPLOYMENT_CVM_EVM = 28;

// Activation parameters
nStartTime = 1704067200;  // Jan 1, 2024
nTimeout = 1735689600;    // Jan 1, 2025
nThreshold = 1916;        // 95% of 2016 blocks
```

---

## Mempool Integration

### Transaction Validation

CVM/EVM transactions undergo additional validation:

```cpp
ValidationResult ValidateTransaction(const CTransaction& tx, int currentHeight) {
    // 1. Standard Bitcoin validation
    // 2. CVM format validation
    // 3. Gas limit checks
    // 4. Free gas eligibility
    // 5. Reputation requirements
    // 6. Rate limiting
}
```

### Validation Checks

| Check | Description | Failure Action |
|-------|-------------|----------------|
| Format | Valid OP_RETURN structure | Reject |
| Gas Limit | Within bounds (1M max) | Reject |
| Gas Price | Meets minimum | Reject |
| Reputation | Meets operation threshold | Reject |
| Rate Limit | Not exceeding limits | Delay |
| Free Gas | Allowance available | Charge fee |

### Priority Queue

Transactions are prioritized by:

1. **Reputation Score** (primary)
   - 90+ reputation: CRITICAL priority
   - 70-89: HIGH priority
   - 50-69: MEDIUM priority
   - 0-49: LOW priority

2. **Gas Price** (secondary)
   - Higher gas price = higher priority within same reputation tier

3. **Timestamp** (tertiary)
   - Earlier submission = higher priority

### Guaranteed Inclusion

Addresses with 90+ reputation get guaranteed inclusion:
- Transactions always included in next block
- Cannot be displaced by higher-fee transactions
- Subject to block gas limit only

### Rate Limiting

To prevent spam, rate limits apply:

| Reputation | Max TX/Block | Max TX/Hour |
|------------|--------------|-------------|
| 90+        | Unlimited    | Unlimited   |
| 70-89      | 100          | 1000        |
| 50-69      | 50           | 500         |
| 30-49      | 20           | 200         |
| 0-29       | 5            | 50          |

### HAT v2 Consensus Validation

Transactions with self-reported reputation undergo distributed validation:

```
1. Transaction enters mempool with claimed HAT v2 score
2. System selects 10+ random validators
3. Validators independently calculate sender's score
4. Consensus reached if 70%+ validators agree
5. Disputed transactions escalate to DAO
```

Transaction states:
- `PENDING_VALIDATION`: Awaiting validator responses
- `VALIDATED`: Consensus reached, eligible for mining
- `DISPUTED`: Escalated to DAO
- `REJECTED`: Consensus rejected, removed from mempool

---

## Block Validation

### Execution Order

CVM/EVM transactions execute in block order:

```
For each transaction in block:
    1. Identify CVM/EVM transaction
    2. Load contract state
    3. Execute contract code
    4. Enforce gas limits
    5. Update UTXO set
    6. Store state changes
    7. Generate receipt
```

### Gas Limits

| Limit | Value | Description |
|-------|-------|-------------|
| Max TX Gas | 1,000,000 | Per transaction |
| Max Block Gas | 10,000,000 | Per block |
| Min Gas Price | 1 micro-CAS | Minimum fee |

### Block Gas Accounting

```cpp
uint64_t blockGasUsed = 0;

for (const auto& tx : block.vtx) {
    if (IsCVMTransaction(tx)) {
        uint64_t txGasLimit = ExtractGasLimit(tx);
        
        // Check block limit
        if (blockGasUsed + txGasLimit > MAX_BLOCK_GAS) {
            // Transaction doesn't fit in block
            continue;
        }
        
        // Execute and track gas
        uint64_t gasUsed = ExecuteTransaction(tx);
        blockGasUsed += gasUsed;
    }
}
```

### Atomic Rollback

If any transaction fails:
1. Rollback all state changes for that transaction
2. Continue with next transaction
3. Failed transaction still pays gas (up to failure point)

### Consensus Rules

All nodes must agree on:
- Transaction execution order
- Gas costs (deterministic)
- State changes (deterministic)
- Receipt generation

---

## Gas System Integration

### Fee Calculation

```cpp
CAmount CalculateEffectiveFee(
    const CTransaction& tx,
    uint64_t gasLimit,
    uint8_t reputation
) {
    // Base gas price (100x lower than Ethereum)
    CAmount basePrice = GetGasPrice();
    
    // Reputation discount
    double discount = CalculateReputationDiscount(reputation);
    
    // Free gas check
    if (reputation >= 80) {
        uint64_t freeAllowance = GetFreeGasAllowance(sender);
        if (gasLimit <= freeAllowance) {
            return 0;  // Free transaction
        }
        gasLimit -= freeAllowance;  // Partial free gas
    }
    
    // Calculate fee
    return (gasLimit * basePrice * (1.0 - discount));
}
```

### Gas Price Oracle

Dynamic gas price based on network conditions:

```cpp
CAmount GetGasPrice() {
    // Base price: 1000 micro-CAS per gas
    CAmount basePrice = 1000;
    
    // Adjust for mempool congestion
    double congestionFactor = GetMempoolCongestion();
    
    // Max 2x variation
    double multiplier = std::min(2.0, 1.0 + congestionFactor);
    
    return basePrice * multiplier;
}
```

### Subsidy Distribution

Gas subsidies distributed during block processing:

```cpp
bool DistributeGasSubsidies(const CBlock& block, int blockHeight) {
    // 70% to miner
    // 30% to validators (split equally)
    
    CAmount totalFees = CalculateBlockFees(block);
    CAmount minerShare = totalFees * 70 / 100;
    CAmount validatorShare = totalFees * 30 / 100;
    
    // Distribute to validators who participated
    std::vector<uint160> validators = GetBlockValidators(block);
    CAmount perValidator = validatorShare / validators.size();
    
    for (const auto& validator : validators) {
        CreditValidator(validator, perValidator);
    }
}
```

---

## Receipt Storage

### Receipt Structure

```cpp
struct TransactionReceipt {
    uint256 transactionHash;
    uint256 blockHash;
    uint32_t blockNumber;
    uint32_t transactionIndex;
    
    uint160 from;
    uint160 to;                    // Empty for deployments
    uint160 contractAddress;       // Set for deployments
    
    uint64_t gasUsed;
    uint64_t cumulativeGasUsed;
    
    uint8_t status;                // 1 = success, 0 = failure
    std::vector<LogEntry> logs;
    
    // Cascoin extensions
    uint8_t senderReputation;
    bool usedFreeGas;
    CAmount effectiveFee;
};
```

### Log Entry Structure

```cpp
struct LogEntry {
    uint160 address;               // Contract that emitted log
    std::vector<uint256> topics;   // Indexed parameters
    std::vector<uint8_t> data;     // Non-indexed parameters
};
```

### Database Schema

Receipts stored in LevelDB:

```
Key: "receipt_" + txHash
Value: Serialized TransactionReceipt

Index: "receipt_block_" + blockHash + "_" + txIndex
Value: txHash
```

### Querying Receipts

```bash
# Get receipt by transaction hash
./src/cascoin-cli -testnet cas_getTransactionReceipt "0x..."

# Response
{
    "transactionHash": "0x...",
    "blockHash": "0x...",
    "blockNumber": "0x...",
    "transactionIndex": "0x0",
    "from": "0x...",
    "to": "0x...",
    "contractAddress": null,
    "gasUsed": "0x5208",
    "cumulativeGasUsed": "0x5208",
    "status": "0x1",
    "logs": [],
    "senderReputation": 75,
    "usedFreeGas": false,
    "effectiveFee": "0x..."
}
```

---

## State Synchronization

### Contract State Storage

Contract state stored in LevelDB:

```
Key: "contract_" + contractAddress
Value: Contract metadata (code hash, creator, block deployed)

Key: "contract_" + contractAddress + "_storage_" + storageKey
Value: 32-byte storage value

Key: "contract_" + contractAddress + "_code"
Value: Contract bytecode
```

### Initial Block Download (IBD)

During IBD, contract state is reconstructed:

1. Download blocks normally
2. Execute CVM/EVM transactions in order
3. Build contract state from execution results
4. Verify state root matches block header

### State Pruning

Old contract state can be pruned:

```bash
# Enable state pruning (keeps last 1000 blocks)
cascoind -testnet -cvmprune=1000
```

### Light Client Support

Light clients can verify contract state using:
- Storage proofs (Merkle proofs)
- Receipt proofs
- State root verification

---

## Implementation Files

Key source files for blockchain integration:

| File | Description |
|------|-------------|
| `src/cvm/softfork.h/cpp` | Soft fork transaction format |
| `src/cvm/mempool_manager.h/cpp` | Mempool integration |
| `src/cvm/block_validator.h/cpp` | Block validation |
| `src/cvm/receipt.h/cpp` | Receipt storage |
| `src/cvm/sustainable_gas.h/cpp` | Gas system |
| `src/cvm/fee_calculator.h/cpp` | Fee calculation |
| `src/cvm/cvmdb.h/cpp` | Database storage |

---

## Next Steps

- [Developer Guide](CVM_DEVELOPER_GUIDE.md) - Contract development
- [Operator Guide](CVM_OPERATOR_GUIDE.md) - Node configuration
- [Security Guide](CVM_SECURITY_GUIDE.md) - HAT v2 consensus

---

**Last Updated:** December 2025  
**Version:** 1.0  
**Status:** Production Ready ✅
