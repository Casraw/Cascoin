# Cascoin Virtual Machine (CVM)

The Cascoin Virtual Machine (CVM) is a lightweight, register-based virtual machine integrated into the Cascoin blockchain. It enables smart contract functionality while maintaining compatibility with existing Bitcoin/Litecoin infrastructure.

## Features

- **Register-Based Architecture**: Efficient bytecode execution with stack-based operations
- **Gas Metering**: Resource usage control linked to block size limits
- **Storage Operations**: Persistent key-value storage for contract state
- **Cryptographic Operations**: Built-in SHA256 and signature verification
- **Modular Design**: Clean separation between VM, storage, and validation

## Architecture

### Core Components

1. **VM Engine (`cvm.cpp/h`)**: Main execution engine
   - Bytecode interpretation
   - Opcode handlers
   - Execution state management

2. **VM State (`vmstate.cpp/h`)**: Execution context
   - Stack operations
   - Gas tracking
   - Program counter management
   - Snapshot/revert support

3. **Opcodes (`opcodes.cpp/h`)**: Instruction set definition
   - Stack manipulation (PUSH, POP, DUP, SWAP)
   - Arithmetic (ADD, SUB, MUL, DIV, MOD)
   - Logical operations (AND, OR, XOR, NOT)
   - Comparisons (EQ, NE, LT, GT, LE, GE)
   - Control flow (JUMP, JUMPI, CALL, RETURN, STOP)
   - Storage (SLOAD, SSTORE)
   - Cryptography (SHA256, VERIFY_SIG)
   - Context operations (ADDRESS, CALLER, TIMESTAMP, etc.)

4. **Contract Management (`contract.cpp/h`)**: Contract lifecycle
   - Deployment transactions
   - Call transactions
   - Address generation
   - Bytecode validation

5. **Storage (`cvmdb.cpp/h`)**: LevelDB-backed persistence
   - Contract code storage
   - State storage (key-value pairs)
   - Account nonces
   - Contract balances

6. **Integration (`cvmtx.cpp/h`)**: Blockchain integration
   - Mempool validation
   - Block execution
   - Consensus rule enforcement

## Anti-Scam Reputation System (ASRS)

The ASRS provides on-chain reputation scoring for addresses without creating blacklists or censoring transactions.

### Features

- **Decentralized Voting**: Community-driven reputation scores
- **On-Chain Behavior Analysis**: Automatic pattern detection
- **Non-Blocking**: Never prevents transactions, only provides warnings
- **Time Decay**: Scores decay over time to account for address rehabilitation
- **Voting Power**: Based on voter's own reputation and stake

### Reputation Scoring

Scores range from -10000 (dangerous) to +10000 (excellent):
- **7500+**: Excellent
- **5000-7499**: Very Good
- **2500-4999**: Good
- **0-2499**: Neutral
- **-2499 to -1**: Questionable
- **-5000 to -2500**: Poor
- **-7500 to -5001**: Very Poor
- **-10000 to -7501**: Dangerous

### Pattern Detection

The system automatically detects:
- **Mixer-like behavior**: Many inputs/outputs with similar amounts
- **Dusting attacks**: Sending tiny amounts to many addresses
- **Rapid-fire transactions**: Potential spam patterns
- **Exchange patterns**: High-volume address identification

## Activation

CVM and ASRS activate at configured block heights (see `chainparams.cpp`):
- **Mainnet**: Block 150,000
- **Testnet**: Block 500
- **Regtest**: Block 0 (immediate)

## Gas Model

Gas prevents DoS attacks while maintaining reasonable costs:

### Gas Limits
- **Per Transaction**: 1,000,000 gas
- **Per Block**: 10,000,000 gas

### Gas Costs
- **Very Low** (3 gas): Stack operations, basic logic
- **Low** (5 gas): Arithmetic operations
- **Mid** (8 gas): Jump operations
- **High** (10 gas): Conditional jumps
- **Storage Load** (200 gas): SLOAD operation
- **Storage Store** (5000 gas): SSTORE operation
- **SHA256** (60 gas): Hash computation
- **Signature Verification** (3000 gas): VERIFY_SIG operation
- **Contract Call** (700 gas): Inter-contract calls

## Example Contract: Simple Storage

```asm
; Store a value
PUSH 1 0x20                 ; Push size (32 bytes)
PUSH 32 0x0000...0005       ; Push value (5)
PUSH 1 0x00                 ; Push key (0)
SSTORE                      ; Store value at key
STOP

; Load a value
PUSH 1 0x00                 ; Push key (0)
SLOAD                       ; Load value from key
; Value is now on stack
STOP
```

## Example Contract: Counter

```asm
; Increment counter
PUSH 1 0x00                 ; Push counter key
SLOAD                       ; Load current value
PUSH 1 0x01                 ; Push increment (1)
ADD                         ; Add 1 to counter
PUSH 1 0x00                 ; Push counter key
SSTORE                      ; Store new value
STOP
```

## RPC Commands

### Contract Operations

#### deploycontract
Deploy a new smart contract:
```bash
cascoin-cli deploycontract "bytecode_hex" [gas_limit] ["init_data"]
```

#### callcontract
Call a deployed contract:
```bash
cascoin-cli callcontract "contract_address" ["data"] [gas_limit] [value]
```

#### getcontractinfo
Get information about a deployed contract:
```bash
cascoin-cli getcontractinfo "contract_address"
```

### Reputation Operations

#### getreputation
Get reputation score for an address:
```bash
cascoin-cli getreputation "address"
```

Example output:
```json
{
  "address": "HXXxxx...",
  "score": 5000,
  "level": "Very Good",
  "votecount": 42,
  "category": "normal",
  "transactions": 1234,
  "volume": 50000000,
  "suspicious": 0,
  "lastupdated": 1234567890,
  "shouldwarn": false
}
```

#### votereputation
Vote on an address's reputation:
```bash
cascoin-cli votereputation "address" vote_value "reason" ["proof"]
```

- `vote_value`: -100 to +100
- `reason`: Why you're voting (required)
- `proof`: Optional evidence in hex

#### listreputations
List addresses with reputation scores:
```bash
cascoin-cli listreputations [threshold] [count]
```

## Transaction Format

### Contract Deployment
Transactions contain an OP_RETURN output with:
```
OP_RETURN "CVM" 0x01 0x01 <contract_data>
```
- Marker: "CVM"
- Version: 0x01
- Type: 0x01 (deploy)
- Data: Serialized ContractDeployTx

### Contract Call
```
OP_RETURN "CVM" 0x01 0x02 <call_data>
```
- Type: 0x02 (call)
- Data: Serialized ContractCallTx

### Reputation Vote
```
OP_RETURN "REP" 0x01 <vote_data>
```
- Marker: "REP"
- Version: 0x01
- Data: Serialized ReputationVoteTx

## Security Considerations

1. **Gas Limits**: Prevent infinite loops and resource exhaustion
2. **Code Size Limits**: Max 24KB per contract
3. **Stack Size Limits**: Max 1024 items
4. **Bytecode Validation**: All opcodes verified before execution
5. **Isolated Execution**: Contracts cannot affect consensus directly

## Future Enhancements

- Inter-contract calls (partial implementation)
- Event logs and filtering
- Contract upgradability patterns
- Advanced pattern detection for ASRS
- Reputation-based transaction prioritization
- Off-chain reputation verification
- Contract formal verification tools

## Development Notes

### Testing
Use regtest mode for development:
```bash
cascoind -regtest
```

CVM activates immediately in regtest, allowing instant testing.

### Debugging
Enable CVM logging:
```bash
cascoind -debug=cvm -debug=asrs
```

### Integration Points

1. **init.cpp**: CVM initialization during node startup
2. **validation.cpp**: Transaction and block validation hooks
3. **miner.cpp**: Gas accounting in block templates
4. **rpc/**: RPC command handlers

## References

- Ethereum Yellow Paper (for gas model inspiration)
- CKB-VM architecture
- Bitcoin Script limitations and learnings

