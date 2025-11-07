# Task 13.1 Completion: EVM Transaction Format Support

## Overview

Implemented EVM transaction format support with full soft-fork compatibility, allowing EVM contracts to be deployed and called via OP_RETURN transactions that are backward-compatible with old nodes.

## Implementation Details

### 1. Extended Soft-Fork Transaction Types

Added new transaction types to `src/cvm/softfork.h`:

**New CVMOpType Values:**
- `EVM_DEPLOY = 0x08`: Explicit EVM contract deployment
- `EVM_CALL = 0x09`: Explicit EVM contract call

**New BytecodeFormat Enum:**
```cpp
enum class BytecodeFormat : uint8_t {
    CVM = 0x01,   // Register-based CVM bytecode
    EVM = 0x02,   // Stack-based EVM bytecode
    AUTO = 0x00   // Auto-detect format
};
```

### 2. Enhanced Transaction Data Structures

**CVMDeployData:**
- Added `BytecodeFormat format` field
- Maintains backward compatibility with existing deployments
- Serialization includes format byte
- Deserialization handles old transactions without format byte (defaults to AUTO)

**CVMCallData:**
- Added `BytecodeFormat format` field
- Allows explicit format specification or auto-detection
- Backward compatible serialization/deserialization

### 3. EVM Transaction Validation

Implemented comprehensive validation functions in `src/cvm/softfork.cpp`:

**`ValidateEVMDeployment()`:**
- Validates bytecode format (rejects CVM format for EVM deployments)
- Checks gas limit (0 < gasLimit <= MAX_GAS_PER_TX)
- Validates code hash is not null
- Verifies bytecode size (≤ MAX_CONTRACT_SIZE = 24KB)
- Confirms code hash matches provided bytecode
- Supports auto-detection for AUTO format
- Checks deployer reputation (minimum 50 for deployment)

**`ValidateEVMCall()`:**
- Validates contract address is not null
- Checks gas limit bounds
- Verifies contract exists in database
- Validates format matches contract format (if specified)

**`IsEVMTransaction()`:**
- Detects if transaction is EVM-related
- Checks for explicit EVM_DEPLOY/EVM_CALL operations
- Checks for generic operations with EVM format specified

**`GetTransactionBytecodeFormat()`:**
- Extracts bytecode format from transaction
- Returns CVM, EVM, or AUTO based on transaction data

### 4. Soft-Fork Compatibility

**Key Design Principles:**
- **Old Nodes**: See OP_RETURN → Accept as valid (unspendable output)
- **New Nodes**: Parse OP_RETURN → Validate EVM rules → Execute contracts

**Transaction Structure:**
```
Input 0: Funding from deployer
Output 0: OP_RETURN <CVM_MAGIC> <OpType> <Data>
  - CVM_MAGIC: 0x43564d31 ("CVM1")
  - OpType: EVM_DEPLOY (0x08) or EVM_CALL (0x09)
  - Data: Serialized CVMDeployData or CVMCallData
Output 1: Contract address (P2SH) or payment
Output 2: Change back to deployer
```

**Backward Compatibility:**
- Existing CVM transactions continue to work
- Format byte is optional (defaults to AUTO for old transactions)
- Auto-detection uses BytecodeDetector for format identification
- No chain split - old nodes accept blocks with EVM transactions

### 5. Format Detection Strategy

**Three-Level Format Specification:**
1. **Explicit EVM**: Use EVM_DEPLOY/EVM_CALL operation types
2. **Format Field**: Set format = BytecodeFormat::EVM in data
3. **Auto-Detection**: Set format = BytecodeFormat::AUTO, let BytecodeDetector identify

**Auto-Detection Process:**
- BytecodeDetector analyzes bytecode patterns
- EVM: Detects PUSH1-PUSH32, standard EVM opcodes
- CVM: Detects register-based patterns
- Hybrid: Detects mixed patterns

## Soft-Fork Activation

**Activation Height:**
- Configured in `chainparams.cpp` via `cvmActivationHeight`
- Mainnet: Block 220,000 (already activated for CVM)
- EVM features use same activation height
- Gradual rollout allows network upgrade without fork

**Validation Rules:**
- Before activation: EVM transactions rejected
- After activation: EVM transactions validated and executed
- Old nodes: Always accept (see as OP_RETURN)
- New nodes: Enforce EVM rules after activation

## Transaction Examples

### EVM Contract Deployment

```cpp
// Create deployment data
CVMDeployData deployData;
deployData.codeHash = Hash(bytecode.begin(), bytecode.end());
deployData.gasLimit = 1000000;
deployData.format = BytecodeFormat::EVM;  // Explicit EVM
deployData.bytecode = evmBytecode;
deployData.constructorData = constructorArgs;

// Build OP_RETURN
CScript opReturn = BuildCVMOpReturn(
    CVMOpType::EVM_DEPLOY,
    deployData.Serialize()
);

// Add to transaction
CMutableTransaction mtx;
mtx.vout.push_back(CTxOut(0, opReturn));  // OP_RETURN output
mtx.vout.push_back(CTxOut(amount, contractScript));  // Contract address
```

### EVM Contract Call

```cpp
// Create call data
CVMCallData callData;
callData.contractAddress = contractAddr;
callData.gasLimit = 500000;
callData.format = BytecodeFormat::EVM;
callData.callData = abiEncodedCall;

// Build OP_RETURN
CScript opReturn = BuildCVMOpReturn(
    CVMOpType::EVM_CALL,
    callData.Serialize()
);

// Add to transaction
CMutableTransaction mtx;
mtx.vout.push_back(CTxOut(0, opReturn));  // OP_RETURN output
mtx.vout.push_back(CTxOut(value, contractScript));  // Payment to contract
```

## Requirements Satisfied

✅ **Requirement 1.1**: EVM bytecode compatibility
- Transaction format supports EVM bytecode
- Soft-fork compatible deployment mechanism
- Backward compatible with existing CVM transactions

✅ **Requirement 1.4**: Solidity compiler output support
- EVM bytecode can be deployed directly
- ABI encoding supported in call data
- Constructor parameters supported

✅ **Requirement 10.5**: Backward compatibility
- Old nodes accept EVM transactions (see as OP_RETURN)
- New nodes validate and execute EVM contracts
- No chain split
- Gradual network upgrade

## Integration Points

### Existing Components
- **CVM Soft-Fork**: Extended with EVM transaction types
- **BytecodeDetector**: Used for auto-format detection
- **CVMDatabase**: Stores contract metadata including format

### Future Integration (Remaining Phase 6 Tasks)
- **Task 13.2**: Mempool integration for EVM transactions
- **Task 13.3**: Fee calculation with reputation adjustments
- **Task 13.4**: Priority queue for transaction ordering
- **Task 14.1**: Block validation with EVM execution
- **Task 15.1**: RPC methods for EVM contract interaction

## Testing Recommendations

1. **Unit Tests**:
   - Test CVMDeployData serialization/deserialization with format
   - Test CVMCallData serialization/deserialization with format
   - Test backward compatibility (old format without format byte)
   - Test ValidateEVMDeployment with various inputs
   - Test ValidateEVMCall with various inputs
   - Test IsEVMTransaction detection
   - Test GetTransactionBytecodeFormat extraction

2. **Integration Tests**:
   - Test EVM contract deployment via OP_RETURN
   - Test EVM contract call via OP_RETURN
   - Test auto-detection with EVM bytecode
   - Test explicit format specification
   - Test backward compatibility with old CVM transactions
   - Test soft-fork activation at specified height

3. **Soft-Fork Tests**:
   - Test old node accepts EVM transactions
   - Test new node validates EVM transactions
   - Test no chain split with mixed nodes
   - Test activation height enforcement

## Notes

- EVM transactions use same OP_RETURN mechanism as CVM
- Maximum OP_RETURN size: 80 bytes (Bitcoin compatible)
- Full bytecode stored separately (witness data or database)
- Code hash in OP_RETURN for verification
- Format byte is optional for backward compatibility
- Auto-detection fallback for unspecified format

## Next Steps

Task 13.2 will integrate EVM transactions with the mempool, implementing reputation-based prioritization and validation before relay.
