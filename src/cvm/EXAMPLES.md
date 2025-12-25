# CVM Example Contracts and Usage

This document provides practical examples of using the Cascoin Virtual Machine.

## Opcode Reference

| Opcode | Hex | Description | Stack Effect | Gas |
|--------|-----|-------------|--------------|-----|
| PUSH | 0x01 | Push value onto stack | -> value | 3 |
| POP | 0x02 | Pop value from stack | value -> | 3 |
| DUP | 0x03 | Duplicate top value | value -> value value | 3 |
| SWAP | 0x04 | Swap top two values | a b -> b a | 3 |
| ADD | 0x10 | Addition | a b -> (a+b) | 5 |
| SUB | 0x11 | Subtraction | a b -> (a-b) | 5 |
| MUL | 0x12 | Multiplication | a b -> (a*b) | 5 |
| DIV | 0x13 | Division | a b -> (a/b) | 5 |
| MOD | 0x14 | Modulo | a b -> (a%b) | 5 |
| EQ | 0x30 | Equality | a b -> (a==b) | 3 |
| LT | 0x32 | Less than | a b -> (a<b) | 3 |
| GT | 0x33 | Greater than | a b -> (a>b) | 3 |
| JUMP | 0x40 | Unconditional jump | target -> | 8 |
| JUMPI | 0x41 | Conditional jump | target cond -> | 10 |
| SLOAD | 0x50 | Load from storage | key -> value | 200 |
| SSTORE | 0x51 | Store to storage | key value -> | 5000 |
| SHA256 | 0x60 | SHA256 hash | data -> hash | 60 |
| ADDRESS | 0x70 | Get contract address | -> address | 1 |
| CALLER | 0x72 | Get caller address | -> caller | 1 |
| TIMESTAMP | 0x74 | Get timestamp | -> time | 1 |
| BLOCKHEIGHT | 0x76 | Get block height | -> height | 1 |
| GAS | 0x80 | Get remaining gas | -> gas | 1 |
| STOP | 0x44 | Halt execution | - | 1 |
| RETURN | 0x43 | Return from execution | - | 1 |

## Example 1: Simple Counter

This contract maintains a counter that can be incremented.

### Bytecode (Human-Readable)
```
// Get current counter value
PUSH 1 0x00         // Push storage key 0
SLOAD               // Load current counter value

// Increment by 1
PUSH 1 0x01         // Push increment value
ADD                 // Add to counter

// Save new value
PUSH 1 0x00         // Push storage key 0
SSTORE              // Store new value

STOP                // End execution
```

### Bytecode (Hex)
```
01 01 00            # PUSH 1 byte: 0x00
50                  # SLOAD
01 01 01            # PUSH 1 byte: 0x01
10                  # ADD
01 01 00            # PUSH 1 byte: 0x00
51                  # SSTORE
44                  # STOP
```

**Full Hex String**: `0101005001010110010100514`

**Gas Cost**: ~5213 gas
- PUSH: 3
- SLOAD: 200
- PUSH: 3
- ADD: 5
- PUSH: 3
- SSTORE: 5000
- STOP: 1

### Deployment
```bash
cascoin-cli deploycontract "0101005001010110010100514" 10000
```

## Example 2: Value Storage

Store and retrieve arbitrary values.

### Store Value (5000)
```
// Store 5000 at key 0
PUSH 4 0x00001388   // Push value 5000 (0x1388)
PUSH 1 0x00         // Push key 0
SSTORE              // Store
STOP
```

**Hex**: `01040000138801010051444`

### Retrieve Value
```
// Load value from key 0
PUSH 1 0x00         // Push key 0
SLOAD               // Load value
STOP                // Value remains on stack
```

**Hex**: `01010050444`

## Example 3: Conditional Logic

Implement simple conditional behavior.

### Check if Value > 100
```
// Load stored value
PUSH 1 0x00         // Storage key
SLOAD               // Load value

// Compare with 100
PUSH 1 0x64         // Push 100
GT                  // Greater than?

// Conditional jump
PUSH 1 0x0A         // Jump target (position 10)
JUMPI               // Jump if true

// If false: Store 0
PUSH 1 0x00
PUSH 1 0x01
SSTORE
STOP

// If true: Store 1 (position 10)
PUSH 1 0x01
PUSH 1 0x01
SSTORE
STOP
```

## Example 4: Hash-Based Verification

Use SHA256 for data verification.

```
// Hash input data
PUSH 4 0x12345678   // Data to hash
SHA256              // Compute hash

// Compare with expected hash
PUSH 32 0x...       // Expected hash (32 bytes)
EQ                  // Equal?

// Store result
PUSH 1 0x00         // Result key
SSTORE
STOP
```

## Example 5: Access Control

Simple owner-only contract.

```
// Get caller address
CALLER              // Push caller

// Load owner address from storage
PUSH 1 0x00         // Owner key
SLOAD               // Load owner

// Check if caller == owner
EQ                  // Equal?

// Jump if authorized
PUSH 1 0x0C         // Jump to authorized code
JUMPI

// Unauthorized: revert
PUSH 1 0x00
STOP

// Authorized: proceed (position 12)
// ... contract logic here ...
STOP
```

## Deploying Contracts

### Step 1: Prepare Bytecode
Create your bytecode hex string following the opcode format.

### Step 2: Deploy
```bash
cascoin-cli deploycontract "BYTECODE_HEX" GAS_LIMIT
```

Example:
```bash
cascoin-cli deploycontract "0101005001010110010100514" 10000
```

### Step 3: Get Contract Address
The deployment transaction will create a contract at a deterministic address.

## Calling Contracts

### Basic Call
```bash
cascoin-cli callcontract "CONTRACT_ADDRESS" "" 100000
```

### Call with Data
```bash
cascoin-cli callcontract "CONTRACT_ADDRESS" "CALL_DATA_HEX" 100000
```

## Reputation System Examples

### Vote on Address Reputation

#### Positive Vote (Trusted Exchange)
```bash
cascoin-cli votereputation "HXExchangeAddress..." 80 "Verified exchange, good track record"
```

#### Negative Vote (Suspected Scam)
```bash
cascoin-cli votereputation "HSuspiciousAddr..." -90 "Multiple complaints, exit scam suspected"
```

#### Neutral Update
```bash
cascoin-cli votereputation "HUnknownAddr..." 5 "New address, small positive interaction"
```

### Check Reputation
```bash
cascoin-cli getreputation "HXAddress..."
```

Example response:
```json
{
  "address": "HXAddress...",
  "score": -6500,
  "level": "Very Poor",
  "votecount": 23,
  "category": "scam",
  "transactions": 156,
  "volume": 250000000,
  "suspicious": 45,
  "lastupdated": 1234567890,
  "shouldwarn": true
}
```

### List Low-Reputation Addresses
```bash
cascoin-cli listreputations -5000 100
```

## Testing in Regtest

### Start Regtest Node
```bash
cascoind -regtest -daemon
```

### Generate Blocks
```bash
cascoin-cli -regtest generate 101
```

### Deploy Test Contract
```bash
cascoin-cli -regtest deploycontract "0101005001010110010100514" 10000
```

### Call Contract
```bash
cascoin-cli -regtest callcontract "CONTRACT_ADDRESS" "" 10000
```

### Mine Block to Confirm
```bash
cascoin-cli -regtest generate 1
```

## Advanced Patterns

### Pattern 1: Event Logging
Contracts can emit events using OP_LOG:
```
PUSH 1 0x01         // Topic count
PUSH 4 0x12345678   // Topic
PUSH 2 0x1234       // Data
LOG
```

### Pattern 2: Inter-Contract Communication
```
PUSH 20 0x...       // Target contract address (20 bytes)
PUSH 4 0x...        // Call data
PUSH 2 0x2710       // Gas limit (10000)
CALL
```

### Pattern 3: State Machine
```
// Load state
PUSH 1 0x00
SLOAD

// Check state == 1
PUSH 1 0x01
EQ
PUSH 1 0x10
JUMPI

// State != 1 logic...
STOP

// State == 1 logic (position 16)
PUSH 1 0x02
PUSH 1 0x00
SSTORE              // Transition to state 2
STOP
```

## Gas Optimization Tips

1. **Minimize Storage Writes**: SSTORE is expensive (5000 gas)
2. **Use Smaller Values**: Smaller PUSH operations are cheaper
3. **Avoid Redundant Loads**: Load from storage once, reuse value
4. **Efficient Branching**: Structure code to minimize jumps
5. **Batch Operations**: Group multiple operations when possible

## Security Best Practices

1. **Validate Inputs**: Check all input data
2. **Access Control**: Implement owner/permission checks
3. **Reentrancy Protection**: Use state guards for external calls
4. **Gas Limits**: Set reasonable limits for all operations
5. **Integer Overflow**: Be aware of 256-bit arithmetic limits

## Debugging

### Enable Debug Logging
```bash
cascoind -debug=cvm -debug=asrs
```

### Check Gas Usage
Monitor gas consumption in logs:
```
CVM: Executing contract, gas limit: 10000
CVM: OpCode PUSH gas: 3, remaining: 9997
CVM: OpCode SLOAD gas: 200, remaining: 9797
...
CVM: Execution complete, gas used: 203
```

### Validate Bytecode
```bash
# Use a test deployment to validate syntax
cascoin-cli -regtest deploycontract "YOUR_BYTECODE" 1000000
```

## Common Errors

### "bad-contract-code"
- Bytecode contains invalid opcodes
- PUSH size exceeds code length
- Solution: Validate bytecode format

### "excessive-gas-limit"
- Gas limit exceeds per-tx maximum (1M)
- Solution: Reduce gas limit or optimize code

### "contract-too-large"
- Bytecode exceeds 24KB limit
- Solution: Split into multiple contracts

### "cvm-not-active"
- CVM not activated at current height
- Solution: Wait for activation block

## Next Steps

1. Study the opcode reference
2. Write simple contracts in hex
3. Test in regtest environment
4. Deploy to testnet
5. Audit and optimize
6. Deploy to mainnet after activation

## Resources

- `/src/cvm/opcodes.h` - Full opcode definitions
- `/src/cvm/README.md` - Architecture overview
- `/src/cvm/vmstate.h` - VM state structure
- Test contracts in `/test/functional/cvm_*.py` (future)

