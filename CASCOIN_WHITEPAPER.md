# Cascoin: A Quantum-Resistant Blockchain with Smart Contracts, Web-of-Trust Reputation, and Native Layer 2 Scaling

**Version 2.0 | January 2026**

---

## Abstract

Cascoin represents a significant advancement in blockchain technology by combining proven Bitcoin/Litecoin architecture with an innovative Cascoin Virtual Machine (CVM), a Web-of-Trust based Anti-Scam Reputation System (ASRS), post-quantum cryptography using FALCON-512, and a native Layer 2 scaling solution. This whitepaper introduces a multi-layer approach to blockchain functionality: maintaining compatibility with established mining algorithms (SHA256d, MinotaurX, Labyrinth/Hive) while adding smart contract capabilities, a novel reputation framework, quantum-resistant signatures, and high-throughput L2 scaling.

Unlike traditional reputation systems that rely on centralized authorities or simplistic global scores, Cascoin implements a personalized Web-of-Trust model where reputation is context-dependent and calculated through trust graph traversal. The quantum-resistant upgrade provides protection against future quantum computing threats while maintaining full backward compatibility. The native L2 solution leverages the HAT v2 reputation system for sequencer selection and provides 1000+ TPS with MEV protection.

**Key Innovations:**
- Soft-fork compatible smart contract VM (CVM) with EVM compatibility
- Web-of-Trust reputation with personalized scoring (HAT v2)
- Post-quantum cryptography using FALCON-512 signatures
- Public Key Registry for optimized quantum transactions
- Native Layer 2 with burn-and-mint token model
- MEV protection via encrypted mempool
- Economic security through bond staking and slashing
- DAO-based dispute resolution

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Background and Motivation](#2-background-and-motivation)
3. [Architecture Overview](#3-architecture-overview)
4. [Cascoin Virtual Machine (CVM)](#4-cascoin-virtual-machine-cvm)
5. [Web-of-Trust Reputation System](#5-web-of-trust-reputation-system)
6. [Post-Quantum Cryptography](#6-post-quantum-cryptography)
7. [Layer 2 Scaling Solution](#7-layer-2-scaling-solution)
8. [Economic Model](#8-economic-model)
9. [Consensus and Security](#9-consensus-and-security)
10. [Implementation Details](#10-implementation-details)
11. [Use Cases](#11-use-cases)
12. [Roadmap](#12-roadmap)
13. [Conclusion](#13-conclusion)

---


---

## 1. Introduction

### 1.1 The Problem

Modern blockchain ecosystems face five critical challenges:

1. **Limited Functionality**: Bitcoin-derived chains excel at value transfer but lack programmability
2. **Trust Deficit**: Decentralized systems struggle with identity verification and reputation
3. **Scam Proliferation**: Without reputation mechanisms, bad actors operate with impunity
4. **Quantum Vulnerability**: Current ECDSA signatures will be broken by quantum computers
5. **Scalability Limitations**: Layer-1 throughput cannot meet growing demand

Traditional solutions involve either:
- Hard forks (risking chain splits)
- Centralized reputation systems (defeating decentralization)
- Simple voting mechanisms (vulnerable to Sybil attacks)
- Separate quantum-resistant chains (fragmenting ecosystems)
- External L2 solutions (introducing trust assumptions)

### 1.2 The Solution

Cascoin introduces four complementary systems:

**Cascoin Virtual Machine (CVM)**: A lightweight, gas-metered virtual machine for smart contract execution, integrated via soft fork for backward compatibility. Features 40+ opcodes with quantum signature verification support.

**Web-of-Trust Reputation (WoT)**: A personalized reputation framework where trust is established through graph relationships, propagated across wallet clusters, economically secured through bonding, and governed by decentralized arbitration.

**Post-Quantum Cryptography**: Native FALCON-512 signature support with a public key registry for transaction size optimization, providing quantum resistance while maintaining ECDSA compatibility.

**Native Layer-2 Scaling**: A burn-and-mint token model with reputation-weighted sequencer consensus, fraud proofs, and cross-layer messaging for high-throughput transactions.

### 1.3 Design Principles

1. **Backward Compatibility**: No hard forks, gradual adoption
2. **Economic Security**: Skin-in-the-game through bonding
3. **Decentralization**: No central authorities
4. **Personalization**: Context-aware reputation
5. **Quantum Resistance**: Future-proof cryptography
6. **Scalability**: Native L2 without external dependencies
7. **Transparency**: All operations on-chain

---

## 2. Background and Motivation

### 2.1 Evolution of Blockchain Programmability

**Bitcoin (2009)**: Limited scripting for basic transactions
**Ethereum (2015)**: Turing-complete smart contracts via hard fork
**Layer 2 Solutions (2017+)**: Off-chain computation, on-chain settlement
**Post-Quantum Research (2020+)**: NIST standardization of quantum-resistant algorithms

**Cascoin's Approach**: Soft-fork compatible VM with on-chain state, combining Bitcoin's security model with Ethereum's programmability, quantum resistance, and native L2 scaling.

### 2.2 The Reputation Problem

In decentralized systems, reputation is critical but challenging:

**Traditional Web**: Centralized platforms (Google, Amazon) control reputation
**Early Blockchain**: No reputation layer, pseudonymity enables scams
**Simple Voting**: Sybil attacks create fake identities to manipulate scores

**Academic Foundation**: Cascoin's WoT draws from:
- PGP Web-of-Trust (Zimmermann, 1992)
- Advogato Trust Metric (Levien, 2009)
- PageRank algorithm (Page & Brin, 1998)
- Stake-weighted voting (Proof-of-Stake literature)

### 2.3 The Quantum Threat

**Current State**: ECDSA signatures secure ~$2 trillion in cryptocurrency
**Threat Timeline**: Cryptographically relevant quantum computers expected 2030-2040
**Risk**: "Harvest now, decrypt later" attacks already occurring
**Solution**: Proactive migration to post-quantum algorithms

### 2.4 Why Web-of-Trust?

**Global Reputation**:
```
Everyone sees: Alice = Score 50
Problem: One bad actor with 1000 fake accounts can manipulate
```

**Web-of-Trust**:
```
Bob's Trust Network:
  Bob → Alice (80%) → Charlie (90%)
  
Bob's view of Charlie:
  Trust Path Weight: 0.8 × 0.9 = 0.72
  Reputation: Weighted by path strength
  
Result: Personalized, Sybil-resistant
```

## 1. Introduction

### 1.1 The Problem

Modern blockchain ecosystems face five critical challenges:

1. **Limited Functionality**: Bitcoin-derived chains excel at value transfer but lack programmability
2. **Trust Deficit**: Decentralized systems struggle with identity verification and reputation
3. **Scam Proliferation**: Without reputation mechanisms, bad actors operate with impunity
4. **Quantum Vulnerability**: Current ECDSA signatures will be broken by quantum computers
5. **Scalability Limitations**: Layer 1 throughput cannot meet mainstream adoption demands

Traditional solutions involve either:
- Hard forks (risking chain splits)
- Centralized reputation systems (defeating decentralization)
- Simple voting mechanisms (vulnerable to Sybil attacks)
- Ignoring quantum threats (risking future fund theft)
- External L2 bridges (introducing security vulnerabilities)

### 1.2 The Solution

Cascoin introduces four complementary systems:

**Cascoin Virtual Machine (CVM)**: A lightweight, gas-metered virtual machine for smart contract execution with EVM compatibility, integrated via soft fork for backward compatibility.

**Web-of-Trust Reputation (WoT)**: A personalized reputation framework where trust is established through graph relationships, economically secured through bonding, and governed by decentralized arbitration. The HAT v2 (Hybrid Adaptive Trust) system combines behavior analysis, graph metrics, economic factors, and temporal decay.

**Post-Quantum Cryptography**: FALCON-512 lattice-based signatures providing NIST Level 1 quantum security, with a Public Key Registry for transaction size optimization.

**Native Layer 2**: A high-throughput scaling solution using burn-and-mint token model, sequencer consensus, and MEV protection through encrypted mempool.

### 1.3 Design Principles

1. **Backward Compatibility**: No hard forks, gradual adoption
2. **Economic Security**: Skin-in-the-game through bonding
3. **Decentralization**: No central authorities
4. **Personalization**: Context-aware reputation
5. **Quantum Resistance**: Future-proof cryptography
6. **Scalability**: Native L2 for high throughput
7. **Transparency**: All operations on-chain

---

## 2. Background and Motivation

### 2.1 Evolution of Blockchain Programmability

**Bitcoin (2009)**: Limited scripting for basic transactions
**Ethereum (2015)**: Turing-complete smart contracts via hard fork
**Layer 2 Solutions (2017+)**: Off-chain computation, on-chain settlement
**Post-Quantum Era (2024+)**: NIST standardization of quantum-resistant algorithms

**Cascoin's Approach**: Soft-fork compatible VM with on-chain state, combining Bitcoin's security model with Ethereum's programmability, quantum-resistant signatures, and native L2 scaling.

### 2.2 The Reputation Problem

In decentralized systems, reputation is critical but challenging:

**Traditional Web**: Centralized platforms (Google, Amazon) control reputation
**Early Blockchain**: No reputation layer, pseudonymity enables scams
**Simple Voting**: Sybil attacks create fake identities to manipulate scores

**Academic Foundation**: Cascoin's WoT draws from:
- PGP Web-of-Trust (Zimmermann, 1992)
- Advogato Trust Metric (Levien, 2009)
- PageRank algorithm (Page & Brin, 1998)
- Stake-weighted voting (Proof-of-Stake literature)

### 2.3 The Quantum Threat

Quantum computers pose an existential threat to current blockchain cryptography:

**ECDSA Vulnerability**: Shor's algorithm can break elliptic curve cryptography
**Timeline**: Estimates suggest cryptographically relevant quantum computers by 2030-2035
**Risk**: All funds in ECDSA-secured addresses could be stolen

**Cascoin's Response**: Proactive migration to FALCON-512, a NIST-selected post-quantum signature scheme.

### 2.4 The Scalability Challenge

Layer 1 blockchains face inherent throughput limitations:

**Bitcoin**: ~7 TPS
**Ethereum**: ~15-30 TPS
**Cascoin L1**: ~40 TPS (2.5 min blocks, 4MB)

**Cascoin L2**: 1000+ TPS with 500ms block times, enabling mainstream adoption.

---


---

## 3. Architecture Overview

### 3.1 System Layers

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│        (Wallets, Explorers, dApps, L2 Applications)         │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                    RPC Interface Layer                       │
│              (JSON-RPC Commands: 40+ endpoints)             │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                      Layer-2 System                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │  Sequencer   │  │    Fraud     │  │   Cross-Layer    │  │
│  │  Consensus   │  │    Proofs    │  │    Messaging     │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│              CVM + Web-of-Trust + Quantum Layer             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │     CVM      │  │ Trust Graph  │  │  Quantum Pubkey  │  │
│  │  (Contracts) │  │ (Reputation) │  │    Registry      │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                   State Storage Layer                        │
│    (LevelDB: Contracts, Trust, Reputation, Quantum Keys)    │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                Consensus Layer (Blockchain)                  │
│           (SHA256d, MinotaurX, Labyrinth/Hive)              │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 Soft Fork Integration

**OP_RETURN Strategy**:
- Old nodes: See `OP_RETURN` → valid (unspendable output)
- New nodes: Parse `OP_RETURN` → validate CVM/WoT/L2 rules

**Benefits**:
- No chain split
- Gradual network upgrade
- Backward compatible

**Format**:
```
OP_RETURN <MAGIC> <OpType> <Data>
  │         │        │       │
  │         │        │       └─ Serialized operation data
  │         │        └───────── Operation type (0x01-0x10)
  │         └────────────────── Magic bytes (CVM1, L2BN, etc.)
  └──────────────────────────── Bitcoin OP_RETURN opcode
```

### 3.3 Address Types

Cascoin supports multiple address formats:

| Type | Prefix | Version | Use Case |
|------|--------|---------|----------|
| P2PKH | C | 28 | Legacy addresses |
| P2SH | H | 50 | Script hash |
| Bech32 | cas1 | 0 | SegWit v0 |
| Bech32m | cas1p | 1 | Taproot |
| Quantum | casq1 | 2 | FALCON-512 |

---

## 4. Cascoin Virtual Machine (CVM)

### 4.1 Design Goals

1. **Simplicity**: Easier to audit than complex VMs
2. **Gas Metering**: Prevent infinite loops and resource abuse
3. **Determinism**: Same bytecode = same result
4. **Security**: Isolated execution environment
5. **Quantum Support**: Native quantum signature verification

### 4.2 Instruction Set

**40+ Opcodes** across categories:

**Arithmetic** (10 opcodes):
- `ADD`, `SUB`, `MUL`, `DIV`, `MOD`
- `EXP`, `ADDMOD`, `MULMOD`
- `SIGNEXTEND`, `NOT`

**Logical** (5 opcodes):
- `AND`, `OR`, `XOR`, `NOT`, `BYTE`

**Comparison** (4 opcodes):
- `LT`, `GT`, `EQ`, `ISZERO`

**Stack Operations** (6 opcodes):
- `PUSH`, `POP`, `DUP`, `SWAP`, `PEEK`, `JUMP`

**Storage** (2 opcodes):
- `STORAGE_READ`, `STORAGE_WRITE`

**Context** (8 opcodes):
- `ADDRESS`, `CALLER`, `VALUE`, `BLOCK_NUMBER`
- `TIMESTAMP`, `BLOCKHASH`, `BLOCKHEIGHT`, `GAS`

**Cryptography** (4 opcodes):
- `SHA256`, `VERIFY_SIG` (ECDSA + FALCON-512)
- `PUBKEY`, `HASH160`

### 4.3 Quantum Signature Support

The CVM automatically detects and verifies both signature types:

```cpp
// Signature type detection
bool IsQuantumSignature(const std::vector<uint8_t>& signature) {
    // ECDSA signatures: 64-72 bytes
    // FALCON-512 signatures: ~700 bytes
    return signature.size() > 100;
}

// Unified verification
bool VerifySignature(message, signature, pubkey) {
    if (IsQuantumSignature(signature)) {
        return VerifySignatureQuantum(message, signature, pubkey);
    } else {
        return VerifySignatureECDSA(message, signature, pubkey);
    }
}
```

## 3. Architecture Overview

### 3.1 System Layers

```
┌─────────────────────────────────────────────────────────────┐
│                   Application Layer                          │
│            (Wallets, Explorers, dApps)                       │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                    Layer 2 (L2)                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Burn-Mint   │  │  Sequencer  │  │  Encrypted Mempool  │  │
│  │   Bridge    │  │  Consensus  │  │   (MEV Protection)  │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
│  1000+ TPS | 500ms blocks | HAT-weighted sequencer selection │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                   RPC Interface Layer                        │
│  (JSON-RPC: CVM, WoT, Quantum, L2 Commands)                 │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│           CVM + Web-of-Trust + Quantum Layer                 │
│  ┌──────────────┐  ┌────────────────┐  ┌─────────────────┐  │
│  │     CVM      │  │  Trust Graph   │  │ FALCON-512 Sigs │  │
│  │  (Contracts) │  │  (Reputation)  │  │ (Quantum-Safe)  │  │
│  └──────────────┘  └────────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                  State Storage Layer                         │
│  (LevelDB: Contracts, Trust, Reputation, Quantum Registry)  │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│              Consensus Layer (Blockchain)                    │
│  (SHA256d, MinotaurX, Labyrinth/Hive | 2.5 min blocks)      │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 Soft Fork Integration

**OP_RETURN Strategy**:
- Old nodes: See `OP_RETURN` → valid (unspendable output)
- New nodes: Parse `OP_RETURN` → validate CVM/WoT/L2 rules

**Benefits**:
- No chain split
- Gradual network upgrade
- Backward compatible

**Format**:
```
OP_RETURN <MAGIC> <OpType> <Data>
  │         │        │       │
  │         │        │       └─ Serialized operation data
  │         │        └───────── Operation type
  │         └────────────────── Magic bytes (CVM1, REP, L2BURN, etc.)
  └──────────────────────────── Bitcoin OP_RETURN opcode
```

### 3.3 Quantum Address Format

**Legacy Addresses (ECDSA)**:
- Base58 addresses starting with `L` or `C`
- Bech32 addresses starting with `cas1` (witness v0/v1)

**Quantum Addresses (FALCON-512)**:
- Bech32m with witness version 2
- Mainnet: `casq1...`
- Testnet: `tcasq1...`
- Regtest: `rcasq1...`

---

## 4. Cascoin Virtual Machine (CVM)

### 4.1 Design Goals

1. **Simplicity**: Easier to audit than complex VMs
2. **Gas Metering**: Prevent infinite loops and resource abuse
3. **Determinism**: Same bytecode = same result
4. **Security**: Isolated execution environment
5. **EVM Compatibility**: Support for Solidity contracts via evmone integration

### 4.2 Instruction Set

**40+ Opcodes** across categories:

**Stack Operations** (6 opcodes):
- `PUSH`, `POP`, `DUP`, `SWAP`, `PEEK`, `JUMP`

**Arithmetic** (10 opcodes):
- `ADD`, `SUB`, `MUL`, `DIV`, `MOD`
- `EXP`, `ADDMOD`, `MULMOD`, `SIGNEXTEND`, `NOT`

**Logical** (5 opcodes):
- `AND`, `OR`, `XOR`, `NOT`, `BYTE`

**Comparison** (4 opcodes):
- `LT`, `GT`, `EQ`, `ISZERO`

**Storage** (2 opcodes):
- `SLOAD` (200 gas), `SSTORE` (5,000 gas)

**Context** (8 opcodes):
- `ADDRESS`, `CALLER`, `CALLVALUE`, `TIMESTAMP`
- `BLOCKHASH`, `BLOCKHEIGHT`, `GAS`, `BALANCE`

**Cryptography** (4 opcodes):
- `SHA256` (60 gas)
- `VERIFY_SIG` (auto-detect: 60 gas ECDSA, 3,000 gas FALCON-512)
- `VERIFY_SIG_ECDSA` (0x61) - Explicit ECDSA verification
- `VERIFY_SIG_QUANTUM` (0x60) - Explicit FALCON-512 verification

### 4.3 Gas Model

```
Gas Cost = Base Cost + Data Cost + Storage Cost

Base Costs:
- Stack operations: 3 gas
- Arithmetic: 5 gas
- Jump operations: 8 gas
- Conditional jumps: 10 gas
- Storage Read (SLOAD): 200 gas
- Storage Write (SSTORE): 5,000 gas
- SHA256: 60 gas
- ECDSA Signature Verification: 60 gas
- FALCON-512 Signature Verification: 3,000 gas
- Contract Call: 700 gas
```

**Limits**:
- Max gas per transaction: 1,000,000
- Max gas per block: 10,000,000
- Max contract code size: 24 KB
- Max stack size: 1,024 elements

### 4.4 EVM Compatibility

CVM includes evmone integration for Solidity contract support:

```cpp
// EVM Engine provides:
- Full EVM opcode support via evmone
- Solidity contract deployment
- Cross-VM contract calls
- Gas translation between CVM and EVM
```

### 4.5 Contract Lifecycle

1. **Deployment**:
   ```
   OP_RETURN "CVM" 0x01 0x01 <contract_data>
   - Validates bytecode
   - Assigns contract address: hash(deployer + nonce)
   - Stores in LevelDB
   ```

2. **Execution**:
   ```
   OP_RETURN "CVM" 0x01 0x02 <call_data>
   - Loads contract from database
   - Executes bytecode
   - Updates state
   - Deducts gas
   ```

3. **State Management**:
   ```
   Key-Value Store:
     contract_<addr>_storage_<key> → value
   
   Persistent across calls
   Survives block reorgs (with snapshots)
   ```

---


### 4.4 Gas Model

```
Gas Cost = Base Cost + Data Cost + Storage Cost

Base Costs:
- Arithmetic: 3-5 gas
- Storage Write: 5,000 gas
- Storage Read: 200 gas
- SHA256: 60 gas
- ECDSA Signature Verification: 3,000 gas
- FALCON-512 Signature Verification: 5,000 gas
```

**Limits**:
- Max gas per transaction: 1,000,000
- Max gas per block: 10,000,000
- Max contract code size: 24 KB
- Max stack size: 1,024 elements
- Max call depth: 256

### 4.5 Contract Lifecycle

1. **Deployment**:
   ```
   User creates TX with OP_RETURN:
     - OpType: 0x01 (CONTRACT_DEPLOY)
     - Data: codeHash + gasLimit + metadata
   
   CVM:
     - Validates bytecode
     - Assigns contract address (hash of TX)
     - Stores in LevelDB
   ```

2. **Execution**:
   ```
   User creates TX with OP_RETURN:
     - OpType: 0x02 (CONTRACT_CALL)
     - Data: contractAddr + callData + gasLimit
   
   CVM:
     - Loads contract from database
     - Executes bytecode
     - Updates state
     - Deducts gas
   ```

3. **State Management**:
   ```
   Key-Value Store:
     contract_<addr>_storage_<key> → value
   
   Persistent across calls
   Survives block reorgs (with snapshots)
   ```

---

## 5. Web-of-Trust Reputation System

### 5.1 Conceptual Model

**Traditional Reputation**:
```
Global Score: Alice = 75
  ↓
Everyone sees same number
  ↓
Vulnerable to Sybil attacks
```

**Web-of-Trust**:
```
Bob's Trust Network:
  Bob → Alice (80%) → Charlie (90%)
  
Bob's view of Charlie:
  Trust Path Weight: 0.8 × 0.9 = 0.72
  Reputation: Weighted by path strength
  
Result: Personalized, context-aware
```

### 5.2 Trust Graph Structure

**Node**: Blockchain address
**Edge**: Trust relationship with weight

```cpp
struct TrustEdge {
    uint160 from;           // Who trusts
    uint160 to;             // Who is trusted
    int16_t weight;         // -100 to +100
    CAmount bond;           // CAS staked
    uint32_t timestamp;     // Creation time
    uint256 txHash;         // Transaction reference
};
```

**Properties**:
- Directed (A→B ≠ B→A)
- Weighted (quantified trust)
- Bonded (economic security)
- Timestamped (temporal tracking)

### 5.3 Wallet Clustering and Trust Propagation

A key innovation in Cascoin's WoT is automatic trust propagation across wallet clusters. When addresses are identified as belonging to the same wallet (through common-input-ownership heuristics), trust relationships are automatically propagated:

```cpp
struct PropagatedTrustEdge {
    uint160 fromAddress;        // Original truster
    uint160 toAddress;          // Propagated target (cluster member)
    uint160 originalTarget;     // Original target address
    uint256 sourceEdgeTx;       // Reference to original trust edge
    int16_t trustWeight;        // Inherited trust weight
    uint32_t propagatedAt;      // Propagation timestamp
    CAmount bondAmount;         // Inherited bond amount
};
```

**Benefits**:
- Prevents reputation gaming through new addresses
- Negative trust follows bad actors across all their addresses
- Cluster-wide reputation scoring

**Cluster Trust Summary**:
```cpp
struct ClusterTrustSummary {
    uint160 clusterId;              // Canonical cluster address
    std::set<uint160> members;      // All addresses in cluster
    int64_t totalIncomingTrust;     // Sum of positive trust
    int64_t totalNegativeTrust;     // Sum of negative trust
    double effectiveScore;          // Minimum score across members
    uint32_t edgeCount;             // Total trust edges
};
```

### 5.4 HAT v2 (Hybrid Adaptive Trust)

The HAT v2 scoring system combines multiple factors for comprehensive trust assessment:

```
HAT Score = w1×BehaviorScore + w2×WoTScore + w3×EconomicScore + w4×TemporalScore

Where:
- BehaviorScore: On-chain activity analysis
- WoTScore: Web-of-Trust path-weighted reputation
- EconomicScore: Bond amounts and stake history
- TemporalScore: Account age and consistency
```

**RPC Commands**:
- `getbehaviormetrics` - Get behavior analysis for address
- `getgraphmetrics` - Get graph analysis metrics
- `getsecuretrust` - Get secure HAT v2 trust score
- `gettrustbreakdown` - Get detailed trust calculation breakdown

## 5. Web-of-Trust Reputation System

### 5.1 HAT v2 (Hybrid Adaptive Trust)

The HAT v2 system combines multiple trust signals for robust Sybil resistance:

**Four Trust Components**:
1. **Behavior Metrics**: On-chain activity patterns
2. **Graph Analysis**: Web-of-Trust path strength
3. **Economic Factors**: Stake and bond amounts
4. **Temporal Decay**: Time-weighted trust degradation

**HAT v2 Score Calculation**:
```
HAT_Score = w1 × BehaviorScore + w2 × GraphScore + 
            w3 × EconomicScore + w4 × TemporalScore

Where: w1 + w2 + w3 + w4 = 1.0
Default weights: 0.25, 0.35, 0.25, 0.15
```

### 5.2 Trust Graph Structure

**Node**: Blockchain address
**Edge**: Trust relationship with weight

```
Trust Edge = {
    from: uint160,          // Who trusts
    to: uint160,            // Who is trusted
    weight: int16,          // -100 to +100
    bond: CAmount,          // CAS staked
    timestamp: uint32
}
```

**Properties**:
- Directed (A→B ≠ B→A)
- Weighted (quantified trust)
- Bonded (economic security)
- Timestamped (temporal tracking)

### 5.3 Personalized Reputation

**Web-of-Trust vs Global Reputation**:
```
Global Score: Alice = 75 (same for everyone, Sybil-vulnerable)

Web-of-Trust:
  Bob sees: Alice = 80 (through trusted friends)
  Carol sees: Alice = 20 (through different network)
  Result: Personalized, Sybil-resistant
```

**Path Finding Algorithm**:
```python
def get_weighted_reputation(viewer, target, max_depth=3):
    paths = find_trust_paths(viewer, target, max_depth)
    if not paths:
        return global_reputation(target)
    
    weighted_sum = 0
    total_weight = 0
    
    for vote in get_votes(target):
        if vote.slashed:
            continue
        for path in paths:
            path_weight = path.total_weight
            weighted_sum += vote.value * path_weight
            total_weight += path_weight
    
    return weighted_sum / total_weight if total_weight > 0 else 0
```

### 5.4 Wallet Clustering for Sybil Resistance

The system detects related addresses to prevent Sybil attacks:

**Clustering Signals**:
- Common input ownership (CoinJoin detection)
- Change address patterns
- Timing correlations
- Amount patterns

**Trust Propagation**:
- Trust relationships propagate within clusters
- Negative reputation affects entire cluster
- Prevents sock-puppet manipulation

### 5.5 Bonding Mechanism

**Purpose**: Prevent spam and malicious votes

**Bond Calculation**:
```
bond_required = MIN_BOND + (|vote_value| × BOND_PER_POINT)

Example:
  Vote +50 → 1.00 + (50 × 0.01) = 1.50 CAS
  Vote +100 → 1.00 + (100 × 0.01) = 2.00 CAS
```

### 5.6 DAO Governance

**Dispute Resolution Process**:
1. **Challenge**: Anyone can challenge a vote with bond
2. **DAO Review**: DAO members stake CAS and vote
3. **Weighted Voting**: Vote power = stake amount
4. **Resolution**: Slash if majority supports, keep otherwise
5. **Execution**: Smart contract enforces decision

**Parameters**:
- Min DAO votes: 5
- Quorum: 51% of total DAO stake
- Timeout: 1440 blocks (~1 day)
- Challenge bond: 1 CAS minimum

---


### 5.5 Bonding Mechanism

**Purpose**: Prevent spam and malicious votes

**Bond Calculation**:
```
bond_required = MIN_BOND + (|vote_value| × BOND_PER_POINT)

Example:
  Vote +50 → 1.00 + (50 × 0.01) = 1.50 CAS
  Vote +100 → 1.00 + (100 × 0.01) = 2.00 CAS
  Vote -80 → 1.00 + (80 × 0.01) = 1.80 CAS
```

**Bond Lifecycle**:
1. User locks CAS when creating trust/vote
2. Bond stored on-chain in transaction
3. If vote challenged → DAO arbitration
4. If slash decision → bond burned/redistributed
5. If kept → bond returned after timeout

### 5.6 DAO Governance and Challenger Rewards

**Purpose**: Decentralized dispute resolution with economic incentives

**Process**:
1. **Challenge**: Anyone can challenge a vote with bond
2. **Commit-Reveal Voting**: DAO members commit vote hashes, then reveal
3. **Weighted Voting**: Vote power = stake amount
4. **Resolution**: Slash if majority supports, keep otherwise
5. **Reward Distribution**: Successful challengers receive portion of slashed bonds

**Challenger Reward System**:
```cpp
struct ChallengerReward {
    uint160 challenger;
    uint256 disputeId;
    CAmount rewardAmount;      // 50% of slashed bond
    CAmount originalBond;      // Challenger's bond returned
    uint64_t distributedAt;
};
```

**Parameters**:
- Min DAO votes: 5
- Quorum: 51% of total DAO stake
- Timeout: 1440 blocks (~1 day)
- Challenge bond: 1 CAS minimum
- Challenger reward: 50% of slashed stake

---

## 6. Post-Quantum Cryptography

### 6.1 The Quantum Threat

Current ECDSA signatures used in Bitcoin and most cryptocurrencies will be vulnerable to Shor's algorithm running on sufficiently powerful quantum computers. While such computers don't exist today, the "harvest now, decrypt later" threat means transactions broadcast today could be compromised in the future.

### 6.2 FALCON-512 Integration

Cascoin implements FALCON-512, a lattice-based signature scheme selected by NIST for post-quantum standardization:

**Key Characteristics**:
- Public key size: 897 bytes
- Signature size: ~666 bytes (variable, max 700 bytes)
- Security level: NIST Level 1 (128-bit classical security)
- Performance: Fast signing and verification

### 6.3 Quantum Address Format

Quantum addresses use Bech32m encoding with witness version 2:

```
Format: casq1<32-byte-program>
  - HRP: "casq" (mainnet), "tcasq" (testnet), "rcasq" (regtest)
  - Witness version: 2
  - Program: SHA256(pubkey) (32 bytes)

Example: casq1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx
```

**Address Derivation**:
```cpp
uint256 GetQuantumWitnessProgram(const std::vector<unsigned char>& pubkey) {
    // Program = SHA256(897-byte FALCON-512 public key)
    CSHA256 hasher;
    hasher.Write(pubkey.data(), pubkey.size());
    uint256 result;
    hasher.Finalize(result.begin());
    return result;
}
```

### 6.4 Public Key Registry

To optimize transaction sizes, Cascoin implements a public key registry that stores FALCON-512 public keys once and references them by hash in subsequent transactions:

**Size Optimization**:
- First transaction (registration): ~1,563 bytes
- Subsequent transactions (reference): ~698 bytes
- Savings: ~55% reduction after initial registration

**Registry Structure**:
```cpp
class QuantumPubKeyRegistry {
    // LevelDB storage: hash -> pubkey
    // LRU cache for performance (1000 entries)
    
    bool RegisterPubKey(const std::vector<unsigned char>& pubkey);
    bool LookupPubKey(const uint256& hash, std::vector<unsigned char>& pubkey);
    bool IsRegistered(const uint256& hash);
};
```

**Witness Format**:
```
Registration (0x51):
  [0x51] [pubkey_hash:32] [full_pubkey:897] [signature:~666]

Reference (0x52):
  [0x52] [pubkey_hash:32] [signature:~666]
```

### 6.5 Transaction Verification Flow

```
1. Parse witness stack
2. Check marker byte (0x51 or 0x52)
3. If 0x51 (registration):
   - Verify pubkey hash matches SHA256(pubkey)
   - Register pubkey in registry
   - Verify FALCON-512 signature
4. If 0x52 (reference):
   - Lookup pubkey from registry by hash
   - Verify FALCON-512 signature
5. Return verification result
```

### 6.6 Hybrid Security Model

Cascoin supports both ECDSA and FALCON-512 signatures, allowing gradual migration:

- **Legacy addresses**: Continue using ECDSA (P2PKH, P2WPKH)
- **Quantum addresses**: Use FALCON-512 (witness version 2)
- **CVM contracts**: Automatically detect and verify both signature types
- **Transition period**: Users can migrate funds at their own pace

## 6. Post-Quantum Cryptography

### 6.1 FALCON-512 Overview

Cascoin implements FALCON-512, a lattice-based digital signature algorithm selected by NIST for post-quantum standardization.

**Security Properties**:
- NIST Level 1 security (128-bit quantum security)
- Based on NTRU lattice problems
- Constant-time operations prevent timing attacks
- Canonical signature validation prevents malleability

**Key and Signature Sizes**:
| Type | ECDSA | FALCON-512 |
|------|-------|------------|
| Private Key | 32 bytes | 1,281 bytes |
| Public Key | 33/65 bytes | 897 bytes |
| Signature | 64-72 bytes | ~666 bytes (max 700) |

**Performance**:
- Signature generation: <10ms
- Signature verification: <2ms

### 6.2 Public Key Registry

The Public Key Registry provides storage optimization for quantum transactions by storing 897-byte FALCON-512 public keys once on-chain and referencing them by 32-byte SHA256 hash in subsequent transactions.

**Transaction Types**:
| Type | Marker | Witness Format | Size |
|------|--------|----------------|------|
| Registration | 0x51 | `[0x51][897-byte PubKey][Signature]` | ~1,598 bytes |
| Reference | 0x52 | `[0x52][32-byte Hash][Signature]` | ~733 bytes |

**Storage Savings**: ~55% reduction after initial registration

**How It Works**:
1. **First Transaction**: Full 897-byte public key included (0x51 marker). Node automatically registers key in registry.
2. **Subsequent Transactions**: Only 32-byte hash reference (0x52 marker). Validators look up full public key from registry.
3. **Automatic Process**: Wallets handle registration and reference logic transparently.

**Registry Architecture**:
```cpp
class QuantumPubKeyRegistry {
    // LevelDB-backed storage
    // LRU cache (1000 entries) for performance
    // O(1) lookup by SHA256 hash
    
    bool RegisterPubKey(const vector<uint8>& pubkey);
    bool LookupPubKey(const uint256& hash, vector<uint8>& pubkey);
    bool IsRegistered(const uint256& hash);
};
```

### 6.3 Migration Path

**Activation Timeline**:
| Network | Activation Height | Status |
|---------|-------------------|--------|
| Mainnet | 350,000 | Pending |
| Testnet | 50,000 | Active |
| Regtest | 1 | Immediate |

**Migration Process**:
1. Update node to quantum-enabled version
2. Generate quantum address: `cascoin-cli getnewaddress "" "quantum"`
3. Migrate funds: `cascoin-cli migrate_to_quantum`

**Backward Compatibility**:
- Legacy ECDSA addresses remain fully functional
- Both signature types supported indefinitely
- Gradual migration encouraged, not forced

### 6.4 CVM Quantum Support

Smart contracts can verify both signature types:

```
VERIFY_SIG (auto-detect by size):
  ≤72 bytes → ECDSA (60 gas)
  >100 bytes → FALCON-512 (3,000 gas)

VERIFY_SIG_ECDSA (0x61): Explicit ECDSA (60 gas)
VERIFY_SIG_QUANTUM (0x60): Explicit FALCON-512 (3,000 gas)
```

---


---

## 7. Layer-2 Scaling Solution

### 7.1 Design Overview

Cascoin's native L2 solution provides high-throughput transaction processing while inheriting L1 security. Unlike external L2 solutions, Cascoin's L2 is built directly into the node software.

**Key Features**:
- Burn-and-mint token model (no bridge contracts)
- Reputation-weighted sequencer consensus
- Fraud proofs with interactive verification
- Cross-layer messaging
- Encrypted mempool for MEV protection

### 7.2 Burn-and-Mint Token Model

Instead of traditional lock-and-mint bridges, Cascoin uses a burn-and-mint model:

**L1 → L2 Transfer**:
```
1. User creates L1 transaction burning CAS to OP_RETURN
2. OP_RETURN format: "L2BURN" <chain_id:4> <recipient:33> <amount:8>
3. Sequencers detect burn and submit MintConfirmations
4. 2/3 sequencer consensus triggers L2 token minting
5. Recipient receives L2 CAS tokens
```

**Burn Transaction Format**:
```cpp
struct BurnData {
    uint32_t chainId;           // L2 chain identifier
    CPubKey recipientPubKey;    // L2 recipient (33 bytes compressed)
    CAmount amount;             // Amount in satoshis
};

// OP_RETURN: "L2BURN" + chainId + pubkey + amount = 51 bytes
```

**Benefits**:
- No bridge contract vulnerabilities
- Atomic burns (cannot be reversed)
- Simple verification (just check OP_RETURN)
- No locked funds to attack

### 7.3 Sequencer Consensus

L2 blocks are produced by sequencers using reputation-weighted consensus:

**Sequencer Requirements**:
- Minimum stake: 1000 CAS
- Minimum HAT v2 reputation score: 50
- Active participation in consensus

**Block Production**:
```cpp
struct L2BlockProposal {
    uint64_t blockNumber;
    uint256 parentHash;
    uint256 stateRoot;
    uint256 transactionsRoot;
    std::vector<uint256> transactionHashes;
    uint160 proposerAddress;
    uint64_t timestamp;
    std::vector<unsigned char> proposerSignature;
    uint64_t gasLimit;      // 30M gas default
    uint64_t gasUsed;
};
```

**Consensus Process**:
1. Leader proposes block
2. Other sequencers validate and vote (ACCEPT/REJECT/ABSTAIN)
3. 2/3+ weighted votes required for consensus
4. Block finalized and state updated

### 7.4 Fraud Proof System

Any node can challenge invalid L2 state transitions:

**Fraud Proof Types**:
- `INVALID_STATE_TRANSITION`: Incorrect state root
- `INVALID_TRANSACTION`: Malformed transaction
- `INVALID_SIGNATURE`: Bad signature
- `DATA_WITHHOLDING`: Missing data availability
- `TIMESTAMP_MANIPULATION`: Invalid timestamps
- `DOUBLE_SPEND`: Duplicate transaction execution

**Challenge Process**:
```cpp
struct FraudProof {
    FraudProofType type;
    uint256 disputedStateRoot;
    uint64_t disputedBlockNumber;
    uint256 previousStateRoot;
    std::vector<CMutableTransaction> relevantTransactions;
    std::vector<unsigned char> stateProof;
    std::vector<unsigned char> executionTrace;
    uint160 challengerAddress;
    CAmount challengeBond;      // 10 CAS minimum
};
```

**Interactive Fraud Proofs**:
For complex disputes, binary search narrows down the invalid step:
1. Challenger claims state transition is invalid
2. Sequencer provides execution trace
3. Binary search identifies specific invalid instruction
4. On-chain verification of single step
5. Winner determined, loser slashed

**Slashing**:
- Valid fraud proof: Sequencer loses 50-100% of stake
- Challenger reward: 50% of slashed amount
- Invalid challenge: Challenger loses bond

### 7.5 Cross-Layer Messaging

Secure communication between L1 and L2:

```cpp
struct CrossLayerMessage {
    uint256 messageId;
    uint160 sender;
    uint160 recipient;
    std::vector<unsigned char> data;
    CAmount value;
    uint64_t gasLimit;
    MessageDirection direction;  // L1_TO_L2 or L2_TO_L1
    MessageStatus status;
};
```

**Message Flow**:
1. Sender creates message on source layer
2. Message included in block
3. Relayers submit proof to destination layer
4. Message executed on destination
5. Status updated to FINALIZED

### 7.6 Security Features

**Rate Limiting**:
- Per-address transaction limits
- Global throughput caps
- Automatic throttling under attack

**Collusion Detection**:
- Monitor sequencer voting patterns
- Detect coordinated behavior
- Automatic reputation penalties

**Reorg Monitoring**:
- Track L1 reorganizations
- Pause L2 operations during deep reorgs
- Automatic recovery procedures

**Forced Inclusion**:
- Users can force transaction inclusion via L1
- Prevents sequencer censorship
- 24-hour maximum delay guarantee

## 7. Layer 2 Scaling Solution

### 7.1 Overview

Cascoin L2 is a native Layer 2 scaling solution that provides:

- **High throughput**: 1000+ TPS with 500ms block times
- **Low fees**: 90%+ reduction compared to L1
- **CVM compatibility**: Full smart contract support
- **Reputation integration**: HAT v2 trust scores for sequencer selection
- **MEV protection**: Encrypted mempool with threshold decryption

### 7.2 Burn-and-Mint Token Model

Unlike traditional bridge contracts, Cascoin L2 uses a cryptographically secure burn-and-mint model:

**Flow**:
```
1. User burns CAS on L1 via OP_RETURN
   └─► CAS permanently destroyed (provably unspendable)

2. Sequencers detect burn transaction
   └─► Wait for 6 L1 confirmations

3. 2/3 sequencer consensus reached
   └─► Sequencers sign confirmation

4. L2 tokens minted
   └─► 1:1 ratio with burned CAS

5. Supply invariant maintained
   └─► L2_Supply == Total_Burned_L1 == Sum(Balances)
```

**OP_RETURN Burn Format**:
```
Byte 0:      OP_RETURN (0x6a)
Byte 1:      OP_PUSHDATA (length)
Bytes 2-7:   "L2BURN" (6 bytes, ASCII marker)
Bytes 8-11:  chain_id (4 bytes, little-endian)
Bytes 12-44: recipient_pubkey (33 bytes, compressed)
Bytes 45-52: amount (8 bytes, satoshis)
```

**Why OP_RETURN is Secure**:
- Bitcoin Script semantics: OP_RETURN immediately terminates execution
- No private key attack: Unlike burn addresses, no "key" to find
- Consensus-enforced: All nodes agree outputs are unspendable
- Quantum-safe: Even quantum computers can't spend OP_RETURN outputs

### 7.3 Sequencer Consensus

**Leader Election**:
- HAT v2 weighted selection
- Minimum requirements: HAT score ≥70, stake ≥100 CAS
- Rotation every 10 blocks
- Automatic failover on timeout (3 seconds)

**Block Production**:
```
1. Leader proposes block
   └─► Includes transactions, state root, signatures

2. Sequencers validate and vote
   └─► ACCEPT, REJECT, or ABSTAIN

3. 2/3+ weighted votes required
   └─► Weight = stake × HAT_score

4. Block finalized
   └─► State committed, rewards distributed
```

**Consensus Parameters**:
| Parameter | Value |
|-----------|-------|
| Target block time | 500ms |
| Max gas per block | 30,000,000 |
| Consensus threshold | 67% (2/3) |
| Leader timeout | 3 seconds |
| Blocks per leader | 10 |

### 7.4 MEV Protection

L2 implements MEV protection through an encrypted mempool:

**Process**:
1. **Submission**: Transactions encrypted before submission
2. **Ordering**: Ordered by commitment hash (not content)
3. **Decryption**: Threshold decryption requires 2/3+ sequencers
4. **Execution**: Decrypted transactions executed in random order

**Benefits**:
- No front-running: Sequencers can't see transaction content
- No sandwich attacks: Transaction ordering is randomized
- Fair execution: All users treated equally

### 7.5 Fraud Proofs

**Types**:
- Invalid state transition
- Invalid transaction
- Invalid signature
- Data withholding
- Timestamp manipulation
- Double spend

**Interactive Fraud Proofs**:
- Binary search to identify invalid step
- Maximum 256 steps before timeout
- 1-hour timeout per step
- Slashing for fraudulent sequencers

**Slashing Parameters**:
| Parameter | Value |
|-----------|-------|
| Challenge bond | 10 CAS |
| Min slashing | 50 CAS |
| Max slashing | 100% of stake |
| Challenger reward | 50% of slashed |

### 7.6 Cross-Layer Messaging

**L1 → L2 Messages**:
- Guaranteed delivery within next L2 block
- Maximum 100 messages per block
- Reentrancy protection

**L2 → L1 Messages**:
- Challenge period required
- Standard: 7 days
- Fast (HAT ≥80): 1 day
- Merkle proof for verification

---


---

## 8. Economic Model

### 8.1 Token Economics

**Cascoin (CAS)** serves multiple purposes:

1. **Transaction Fees**: Pay for L1 blockchain transactions
2. **Gas**: Pay for CVM smart contract execution
3. **Bonds**: Stake for trust relationships and votes
4. **DAO Participation**: Stake for governance rights
5. **Sequencer Stake**: Required for L2 block production
6. **Challenge Bonds**: Required for fraud proof submission
7. **Security**: Incentivize honest behavior across all layers

### 8.2 Fee Structure

```
L1 Transaction Fees:
  - Simple transfer: ~0.0001 CAS
  - Contract deployment: 0.001 - 0.01 CAS (based on size)
  - Contract call: 0.0001 - 0.001 CAS (based on gas)
  - Quantum transaction: ~0.0002 CAS (larger witness)
  
Trust/Vote Bonds:
  - Trust edge: 1.00 - 2.00 CAS
  - Bonded vote: 1.00 - 2.00 CAS
  - DAO dispute: 1.00 CAS minimum
  
L2 Fees:
  - L2 transfer: ~0.00001 CAS
  - L2 contract call: ~0.0001 CAS
  - Cross-layer message: ~0.001 CAS
  
Sequencer Requirements:
  - Minimum stake: 1000 CAS
  - Fraud proof challenge bond: 10 CAS
```

### 8.3 Incentive Alignment

**Miners**:
- Block rewards (existing Cascoin schedule)
- Transaction fees (increased with CVM/WoT/L2 usage)

**Sequencers**:
- L2 transaction fees
- Block production rewards
- Risk: Stake slashing for misbehavior

**Challengers**:
- 50% of slashed sequencer stake
- Bond returned on successful challenge
- Risk: Bond loss on invalid challenge

**Users**:
- Benefit: Access to contracts, reputation, and L2 scaling
- Cost: Gas + bonds + L2 fees
- Risk: Bond slashing if malicious

**DAO Members**:
- Benefit: Governance fees + slashed bonds
- Cost: Stake required
- Risk: Lost stake if wrong decisions

---

## 9. Consensus and Security

### 9.1 L1 Consensus Layer

**Mining Algorithms**:
- SHA256d: Bitcoin-compatible ASIC mining
- MinotaurX: CPU-friendly YesPower-based
- Labyrinth/Hive: Unique "bee" proof system

**Block Parameters**:
- Block time: 2.5 minutes
- Block size: 4MB
- Difficulty adjustment: LWMA-3 algorithm

### 9.2 L2 Consensus

**Sequencer Selection**:
- Reputation-weighted leader election
- Round-robin with reputation multiplier
- Automatic failover on timeout

**Block Finality**:
- Soft finality: 2/3 sequencer consensus (~seconds)
- Hard finality: L1 confirmation (~2.5 minutes)
- Challenge period: 24 hours for fraud proofs

### 9.3 Security Guarantees

**CVM Security**:
- Gas limits prevent DoS
- Isolated execution (no system calls)
- Deterministic replay for validation
- Storage quotas prevent state bloat
- Quantum signature verification

**WoT Security**:
- Sybil resistance via trust paths and wallet clustering
- Economic security via bonding
- DAO oversight prevents collusion
- Slashing deters malicious behavior
- Trust propagation prevents reputation gaming

**Quantum Security**:
- FALCON-512 resistant to known quantum attacks
- Public key registry prevents key reuse attacks
- Hybrid model allows gradual migration
- Registry rebuild capability for recovery

**L2 Security**:
- Fraud proofs enable trustless verification
- Sequencer stake at risk for misbehavior
- Forced inclusion prevents censorship
- Cross-layer messaging with proofs
- Collusion detection and penalties

### 9.4 Attack Vectors and Mitigations

| Attack | Mitigation |
|--------|------------|
| Sybil Attack | Trust paths + wallet clustering |
| Eclipse Attack | Multiple trust paths, diversity |
| Collusion | DAO arbitration, bond slashing |
| Gas Griefing | Gas limits, fee market |
| Storage Spam | Storage costs, pruning |
| Quantum Attack | FALCON-512 signatures |
| L2 Censorship | Forced inclusion via L1 |
| Sequencer Fraud | Fraud proofs, slashing |
| Bridge Attack | Burn-and-mint (no bridge) |

## 8. Economic Model

### 8.1 Token Economics

**Cascoin (CAS)** serves multiple purposes:

1. **Transaction Fees**: Pay for L1 blockchain transactions
2. **Gas**: Pay for smart contract execution
3. **Bonds**: Stake for trust relationships and votes
4. **DAO Participation**: Stake for governance rights
5. **Sequencer Stake**: Required for L2 block production
6. **Security**: Incentivize honest behavior

### 8.2 Fee Structure

**L1 Fees**:
| Operation | Approximate Cost |
|-----------|------------------|
| Simple transfer | ~0.0001 CAS |
| Contract deployment | 0.001 - 0.01 CAS |
| Contract call | 0.0001 - 0.001 CAS |
| Quantum transaction (first) | ~0.002 CAS |
| Quantum transaction (subsequent) | ~0.001 CAS |

**L2 Fees** (90%+ reduction):
| Operation | Approximate Cost |
|-----------|------------------|
| Transfer | ~0.00001 CAS |
| Contract deployment | ~0.0001 CAS |
| Storage write | ~0.000025 CAS |

**Trust/Vote Bonds**:
| Operation | Bond Required |
|-----------|---------------|
| Trust edge | 1.00 - 2.00 CAS |
| Bonded vote | 1.00 - 2.00 CAS |
| DAO dispute | 1.00 CAS minimum |
| Sequencer stake | 100 CAS minimum |

### 8.3 Incentive Alignment

**Miners**:
- Block rewards (existing Cascoin schedule)
- Transaction fees (increased with CVM/WoT/L2 usage)

**Sequencers**:
- L2 block rewards
- Transaction fee share
- Slashing risk for misbehavior

**Users**:
- Benefit: Access to contracts, reputation, L2 scaling
- Cost: Gas + bonds
- Risk: Bond slashing if malicious

**DAO Members**:
- Benefit: Governance fees + slashed bonds
- Cost: Stake required
- Risk: Lost stake if wrong decisions

### 8.4 HAT Score Benefits

| HAT Score | Benefits |
|-----------|----------|
| ≥ 70 | Sequencer eligibility |
| ≥ 70 | Increased rate limits (500 tx/block vs 100) |
| ≥ 70 | Gas discount (up to 50%) |
| ≥ 80 | Fast withdrawal (1 day vs 7 days) |
| ≥ 80 | Instant soft-finality |

---

## 9. Consensus and Security

### 9.1 L1 Consensus Layer

**Mining Algorithms**:
- **SHA256d**: Bitcoin-compatible, ASIC-friendly
- **MinotaurX**: YesPower-based, CPU-friendly
- **Labyrinth/Hive**: Unique "bee" system for passive mining

**Difficulty Adjustment**:
- LWMA-3 algorithm for responsive adjustment
- Separate targets per algorithm
- 2.5 minute target block time

### 9.2 L2 Consensus

**Sequencer Selection**:
- HAT v2 weighted random selection
- Minimum stake and reputation requirements
- Automatic failover on timeout

**Block Finality**:
- Soft finality: 2/3 sequencer consensus (~1 second)
- Hard finality: L1 anchor confirmation (~2.5 minutes)

### 9.3 Security Guarantees

**CVM Security**:
- Gas limits prevent DoS
- Isolated execution (no system calls)
- Deterministic replay for validation
- Storage quotas prevent state bloat

**WoT Security**:
- Sybil resistance via trust paths
- Economic security via bonding
- DAO oversight prevents collusion
- Wallet clustering detects sock puppets

**Quantum Security**:
- FALCON-512 provides 128-bit quantum security
- Constant-time operations prevent timing attacks
- Public Key Registry prevents key reuse attacks

**L2 Security**:
- Fraud proofs enable trustless verification
- Encrypted mempool prevents MEV
- Challenge periods protect withdrawals
- Emergency exit mechanism if sequencers fail

### 9.4 Attack Mitigations

| Attack | Mitigation |
|--------|------------|
| Sybil | Trust paths, wallet clustering |
| Eclipse | Multiple trust paths, diversity |
| Collusion | DAO arbitration, bond slashing |
| Gas Griefing | Gas limits, fee market |
| Storage Spam | Storage costs, pruning |
| Quantum | FALCON-512 signatures |
| MEV | Encrypted mempool |
| Sequencer Fraud | Fraud proofs, slashing |

---

## 10. Implementation Details

### 10.1 Database Architecture

**LevelDB** for persistent storage:

```
L1 Keys:
  contract_<addr>           → Contract metadata
  contract_<addr>_storage_<key> → Storage value
  trust_<from>_<to>         → Trust edge
  trust_in_<to>_<from>      → Reverse index
  vote_<txhash>             → Bonded vote
  votes_<target>_<txhash>   → Vote by target
  dispute_<id>              → DAO dispute
  Q<pubkey_hash>            → Quantum public key

L2 Keys:
  l2_account_<addr>         → Account state
  l2_block_<number>         → Block data
  l2_burn_<txhash>          → Burn record
  l2_mint_<txhash>          → Mint record
```

### 10.2 RPC API Surface

**CVM Commands** (4):
- `deploycontract` - Deploy smart contract
- `callcontract` - Call contract method
- `getcontractinfo` - Query contract metadata
- `sendcvmcontract` - Broadcast deployment

**Reputation Commands** (4):
- `getreputation` - Get reputation score
- `votereputation` - Cast reputation vote
- `sendcvmvote` - Broadcast vote transaction
- `listreputations` - List addresses with scores

**Web-of-Trust Commands** (6):
- `addtrust` - Add trust relationship
- `getweightedreputation` - Get personalized reputation
- `gettrustgraphstats` - Query graph statistics
- `listtrustrelations` - List trust relationships
- `sendtrustrelation` - Broadcast trust transaction
- `sendbondedvote` - Broadcast bonded vote

**HAT v2 Commands** (4):
- `getbehaviormetrics` - Get behavior analysis
- `getgraphmetrics` - Get graph analysis
- `getsecuretrust` - Get HAT v2 score
- `gettrustbreakdown` - Get detailed breakdown

**Quantum Commands** (4):
- `getnewaddress "" "quantum"` - Generate quantum address
- `getlegacyaddress` - Generate legacy address
- `migrate_to_quantum` - Migrate funds to quantum
- `getquantumregistrystats` - Registry statistics

**L2 Commands** (15+):
- `l2_getchaininfo` - Get L2 chain info
- `l2_getbalance` - Get L2 balance
- `l2_createburntx` - Create burn transaction
- `l2_sendburntx` - Broadcast burn transaction
- `l2_getburnstatus` - Check burn/mint status
- `l2_gettotalsupply` - Get L2 token supply
- `l2_verifysupply` - Verify supply invariant
- `l2_announcesequencer` - Announce as sequencer
- `l2_getsequencers` - List sequencers
- `l2_getleader` - Get current leader
- `l2_getblockbynumber` - Get L2 block
- `l2_deploy` - Deploy L2 chain
- `l2_registerchain` - Register L2 chain
- `l2_listchains` - List L2 chains
- `l2_getpendingburns` - Get pending burns

### 10.3 Activation Parameters

**Mainnet**:
| Feature | Activation Height |
|---------|-------------------|
| CVM/ASRS | Block 220,000 |
| Quantum | Block 350,000 |
| L2 | Block 400,000 |

**Testnet**:
| Feature | Activation Height |
|---------|-------------------|
| CVM/ASRS | Block 500 |
| Quantum | Block 50,000 |
| L2 | Block 60,000 |

**Regtest**:
- All features active from block 0/1

---


---

## 10. Implementation Details

### 10.1 Database Architecture

**LevelDB** for persistent storage across all subsystems:

```
CVM Keys:
  contract_<addr> → Contract metadata
  contract_<addr>_storage_<key> → Storage value

WoT Keys:
  trust_<from>_<to> → Trust edge
  trust_in_<to>_<from> → Reverse index
  trust_prop_<from>_<to> → Propagated trust edge
  cluster_trust_<clusterId> → Cluster summary
  vote_<txhash> → Bonded vote
  votes_<target>_<txhash> → Vote by target
  dispute_<id> → DAO dispute

Quantum Keys:
  Q<pubkey_hash> → FALCON-512 public key (897 bytes)

L2 Keys:
  l2_block_<number> → L2 block data
  l2_state_<key> → L2 state values
  l2_burn_<txhash> → Burn transaction record
  l2_mint_<txhash> → Mint consensus state
```

### 10.2 RPC Interface

**40+ RPC Commands** across categories:

**CVM (4 commands)**:
- `deploycontract` - Prepare contract deployment
- `callcontract` - Prepare contract call
- `getcontractinfo` - Query contract metadata
- `sendcvmcontract` - Broadcast contract deployment

**Reputation (4 commands)**:
- `getreputation` - Get reputation score for address
- `votereputation` - Cast reputation vote (off-chain)
- `sendcvmvote` - Broadcast reputation vote transaction
- `listreputations` - List addresses with reputation scores

**Web-of-Trust (6 commands)**:
- `addtrust` - Add trust relationship (off-chain)
- `getweightedreputation` - Get personalized reputation via trust paths
- `gettrustgraphstats` - Get WoT graph statistics
- `listtrustrelations` - List all trust relationships
- `sendtrustrelation` - Broadcast trust relationship transaction
- `sendbondedvote` - Broadcast bonded reputation vote

**HAT v2 (4 commands)**:
- `getbehaviormetrics` - Get behavior analysis for address
- `getgraphmetrics` - Get graph analysis metrics
- `getsecuretrust` - Get secure HAT v2 trust score
- `gettrustbreakdown` - Get detailed trust calculation breakdown

**Wallet Clustering (3 commands)**:
- `buildwalletclusters` - Analyze blockchain for wallet clusters
- `getwalletcluster` - Get addresses in same wallet cluster
- `geteffectivetrust` - Get trust score considering clustering

**DAO (4 commands)**:
- `createdispute` - Challenge a bonded vote
- `listdisputes` - List DAO disputes
- `getdispute` - Get dispute details
- `votedispute` - Vote on dispute as DAO member

**Quantum (4 commands)**:
- `getquantumaddress` - Generate quantum address from pubkey
- `registerquantumpubkey` - Register FALCON-512 public key
- `lookupquantumpubkey` - Lookup registered public key
- `getquantumregistrystats` - Get registry statistics

**L2 (10+ commands)**:
- `l2_getinfo` - Get L2 chain information
- `l2_getblock` - Get L2 block by number
- `l2_getbalance` - Get L2 balance for address
- `l2_sendtransaction` - Send L2 transaction
- `l2_getsequencers` - List active sequencers
- `l2_submitfraudproof` - Submit fraud proof
- `l2_getpendingburns` - List pending burn confirmations
- `l2_forceinclusion` - Force transaction inclusion

### 10.3 Activation Parameters

**Mainnet**:
- CVM activation: Block 220,000
- WoT activation: Block 220,000
- Quantum activation: Block 250,000
- L2 activation: Block 280,000

**Testnet**:
- All features: Block 500

**Regtest**:
- All features: Block 0 (immediate)

---

## 11. Use Cases

### 11.1 Decentralized Marketplace

**Problem**: eBay/Amazon have monopolistic control over seller reputation

**Solution**:
```
Marketplace dApp on Cascoin:
  - Sellers have Web-of-Trust reputation
  - Buyers see personalized trust scores
  - Economic bonding for seller commitments
  - DAO arbitration for disputes
  - L2 for high-frequency transactions
  - Quantum-secure payments for high-value items
```

### 11.2 DeFi Credit Scoring

**Problem**: Traditional credit systems centralized and opaque

**Solution**:
```
Credit protocol:
  - Borrowers build on-chain reputation
  - Lenders see reputation from their perspective
  - Loan defaults impact trust graph (propagated to all addresses)
  - Interest rates adjust based on HAT v2 score
  - L2 for fast loan processing
```

### 11.3 Quantum-Secure Treasury

**Problem**: Long-term holdings vulnerable to future quantum attacks

**Solution**:
```
Treasury management:
  - Store funds in quantum addresses (casq1...)
  - FALCON-512 signatures protect against quantum threats
  - Multi-sig with quantum keys
  - Gradual migration from legacy addresses
```

### 11.4 High-Throughput Gaming

**Problem**: L1 too slow for real-time game transactions

**Solution**:
```
Gaming platform on L2:
  - Fast L2 transactions for in-game actions
  - Reputation system for player trust
  - L1 settlement for high-value items
  - Fraud proofs ensure fair play
```

### 11.5 Supply Chain Verification

**Problem**: Counterfeit goods and unverifiable provenance

**Solution**:
```
Supply chain tracking:
  - Manufacturers build reputation through WoT
  - Products tracked via CVM contracts
  - Quantum signatures for long-term authenticity
  - L2 for high-volume tracking updates
```

## 11. Use Cases

### 11.1 Decentralized Marketplace

**Problem**: eBay/Amazon have monopolistic control over seller reputation

**Solution**:
```
Marketplace dApp on Cascoin:
  - Sellers have Web-of-Trust reputation
  - Buyers see personalized trust scores
  - Economic bonding for seller commitments
  - DAO arbitration for disputes
  - L2 for high-frequency transactions
  
Buyer checks seller:
  → Sees reputation from their trust network
  → More accurate than global score
  → Resistant to fake reviews
```

### 11.2 DeFi Credit Scoring

**Problem**: Traditional credit systems centralized and opaque

**Solution**:
```
Credit protocol on L2:
  - Borrowers build on-chain reputation
  - Lenders see reputation from their perspective
  - Loan defaults impact trust graph
  - Interest rates adjust based on HAT score
  
Smart contract:
  if (hat_score >= 80):
      interest_rate = 5%
      withdrawal_period = 1_day
  elif (hat_score >= 70):
      interest_rate = 10%
      withdrawal_period = 7_days
  else:
      loan_denied
```

### 11.3 Quantum-Safe Treasury

**Problem**: Corporate/DAO treasuries vulnerable to future quantum attacks

**Solution**:
```
Quantum-safe multisig:
  - Treasury uses FALCON-512 addresses
  - Protected against quantum computers
  - Gradual migration from ECDSA
  - No urgency, proactive security
```

### 11.4 High-Throughput Gaming

**Problem**: L1 too slow for real-time gaming transactions

**Solution**:
```
Gaming on L2:
  - 1000+ TPS for in-game transactions
  - 500ms finality for responsive gameplay
  - MEV protection prevents front-running
  - Low fees enable microtransactions
  - CVM contracts for game logic
```

### 11.5 Identity Verification

**Problem**: KYC is centralized and privacy-invasive

**Solution**:
```
Decentralized identity:
  - Trusted verifiers attest to identity
  - Attestations propagate via trust graph
  - Users control what information to reveal
  - Quantum-safe signatures for long-term validity
  
Example:
  Alice verified by Bob (trusted notary)
  → Carol trusts Bob
  → Carol can trust Alice's identity claim
```

---

## 12. Roadmap

### Phase 1: Foundation (Q4 2024 - Q1 2025) ✅ COMPLETE

- [x] CVM scaffolding (40+ opcodes)
- [x] LevelDB integration
- [x] RPC interface
- [x] Simple reputation system
- [x] Gas metering
- [x] Compilation & testing

### Phase 2: Web-of-Trust (Q1 2025) ✅ COMPLETE

- [x] Trust graph data structures
- [x] Path finding algorithms
- [x] Weighted reputation calculation
- [x] Bonding mechanism
- [x] DAO governance framework
- [x] HAT v2 implementation
- [x] Wallet clustering

### Phase 3: Quantum Resistance (Q2-Q3 2025) ✅ COMPLETE

- [x] FALCON-512 integration
- [x] Quantum address format (casq1...)
- [x] Public Key Registry
- [x] Wallet migration tools
- [x] CVM quantum signature opcodes
- [x] Testnet activation

### Phase 4: Layer 2 (Q3-Q4 2025) ✅ COMPLETE

- [x] Burn-and-mint token model
- [x] Sequencer consensus
- [x] Leader election
- [x] Fraud proof system
- [x] Encrypted mempool (MEV protection)
- [x] Cross-layer messaging
- [x] L2 RPC commands

### Phase 5: Integration & Polish (Q1 2026) 🔄 IN PROGRESS

- [ ] Qt wallet L2 integration
- [ ] Quantum address display in UI
- [ ] Trust graph visualization
- [ ] L2 dashboard
- [ ] Mobile wallet support
- [ ] Developer SDK (JavaScript, Python, Rust)

### Phase 6: Mainnet Launch (Q2 2026)

- [ ] Testnet stress testing
- [ ] Security audit
- [ ] Quantum activation at block 350,000
- [ ] L2 activation at block 400,000
- [ ] Exchange integrations
- [ ] Marketing & adoption

---

## 13. Technical Specifications

### 13.1 Performance Metrics

**CVM**:
- Execution speed: ~100,000 opcodes/second
- Contract deployment: <1 second
- State read: <10ms
- State write: <50ms

**Web-of-Trust**:
- Trust edge query: <5ms
- Path finding (depth 3): <50ms for 1000 edges
- HAT v2 calculation: <100ms
- Graph traversal: O(b^d) complexity

**Quantum**:
- Signature generation: <10ms
- Signature verification: <2ms
- Registry lookup (cached): <1ms
- Registry lookup (database): <10ms

**Layer 2**:
- Block time: 500ms
- Throughput: 1000+ TPS
- Finality (soft): ~1 second
- Finality (hard): ~2.5 minutes

**Blockchain (L1)**:
- Block time: 2.5 minutes
- Block size: 4MB
- Throughput: ~40 TPS

### 13.2 Size Specifications

| Component | Size |
|-----------|------|
| Max contract code | 24 KB |
| Max stack size | 1,024 elements |
| FALCON-512 public key | 897 bytes |
| FALCON-512 signature | ~666 bytes (max 700) |
| Quantum address program | 32 bytes |
| L2 block max gas | 30,000,000 |

### 13.3 Compatibility

**Bitcoin Core**: Based on Bitcoin Core codebase
**Litecoin**: Inherits Litecoin improvements
**EVM**: Compatible via evmone integration
**NIST PQC**: FALCON-512 (NIST Level 1)

---

## 14. Conclusion

Cascoin represents a comprehensive advancement in blockchain technology, addressing the fundamental challenges of programmability, trust, quantum vulnerability, and scalability through four integrated systems:

### Key Achievements

1. **Smart Contracts**: 40+ opcode CVM with EVM compatibility
2. **Reputation**: HAT v2 personalized trust with Sybil resistance
3. **Quantum Safety**: FALCON-512 signatures with optimized registry
4. **Scalability**: Native L2 with 1000+ TPS and MEV protection
5. **Backward Compatibility**: Soft fork integration, no chain splits
6. **Economic Security**: Bond-and-slash with DAO governance

### Vision

We envision Cascoin as the foundation for a new generation of decentralized applications where:
- Trust is earned through relationships, not purchased
- Reputation is personal and context-aware
- Cryptography is future-proof against quantum threats
- Transactions are fast, cheap, and MEV-resistant
- Economic incentives align with honest behavior
- Governance is truly decentralized

### Call to Action

**Developers**: Build dApps on Cascoin, leverage CVM, WoT, and L2
**Miners**: Continue securing the network
**Users**: Migrate to quantum addresses, build your reputation
**Sequencers**: Stake and participate in L2 consensus
**DAO Members**: Help govern the protocol
**Community**: Spread the word, grow the ecosystem

---


---

## 12. Roadmap

### Phase 1: Foundation (Q4 2024 - Q1 2025) ✅ COMPLETE

- [x] CVM scaffolding (40+ opcodes)
- [x] LevelDB integration
- [x] RPC interface
- [x] Simple reputation system
- [x] Gas metering
- [x] Compilation & testing

### Phase 2: Web-of-Trust (Q1 2025) ✅ COMPLETE

- [x] Trust graph data structures
- [x] Path finding algorithms
- [x] Weighted reputation calculation
- [x] Bonding mechanism design
- [x] DAO governance framework
- [x] Database persistence
- [x] RPC commands

### Phase 3: Advanced WoT Features (Q2 2025) ✅ COMPLETE

- [x] Wallet clustering algorithms
- [x] Trust propagation across clusters
- [x] HAT v2 scoring system
- [x] Behavior metrics analysis
- [x] Challenger reward system
- [x] Commit-reveal voting

### Phase 4: Post-Quantum Cryptography (Q3 2025) ✅ COMPLETE

- [x] FALCON-512 integration via liboqs
- [x] Quantum address format (casq1...)
- [x] Public key registry with LRU cache
- [x] Witness format (registration/reference)
- [x] CVM quantum signature verification
- [x] Registry rebuild capability

### Phase 5: Layer-2 Foundation (Q3-Q4 2025) ✅ COMPLETE

- [x] L2 block and transaction structures
- [x] Sparse Merkle tree for state
- [x] Sequencer discovery and registration
- [x] Leader election algorithm
- [x] Sequencer consensus protocol
- [x] Burn-and-mint token model
- [x] Mint consensus (2/3 sequencer agreement)

### Phase 6: L2 Security (Q4 2025) ✅ COMPLETE

- [x] Fraud proof system
- [x] Interactive fraud proofs
- [x] Cross-layer messaging
- [x] Data availability proofs
- [x] Rate limiting
- [x] Collusion detection
- [x] Reorg monitoring
- [x] Forced inclusion mechanism

### Phase 7: Integration & Testing (Q1 2026) 🔄 IN PROGRESS

- [ ] Full L1-L2 integration testing
- [ ] Stress testing under load
- [ ] Security audit
- [ ] Bug bounty program
- [ ] Documentation completion
- [ ] SDK development

### Phase 8: Wallet & UX (Q1-Q2 2026)

- [ ] Qt wallet quantum address support
- [ ] L2 wallet integration
- [ ] Reputation display in UI
- [ ] Contract deployment wizard
- [ ] Trust graph visualization
- [ ] Mobile wallet support

### Phase 9: Mainnet Launch (Q2-Q3 2026)

- [ ] Testnet stress testing
- [ ] Community testing period
- [ ] Final security audit
- [ ] Staged activation (CVM → WoT → Quantum → L2)
- [ ] Exchange integrations
- [ ] Marketing & adoption

---

## 13. Technical Specifications

### 13.1 Performance Metrics

**CVM**:
- Execution speed: ~100,000 opcodes/second
- Contract deployment: <1 second
- State read: <10ms
- State write: <50ms
- ECDSA verification: ~3ms
- FALCON-512 verification: ~5ms

**Web-of-Trust**:
- Trust edge query: <5ms
- Path finding (depth 3): <50ms for 1000 edges
- Reputation calculation: <100ms
- Cluster lookup: <10ms
- Trust propagation: <100ms per cluster

**Quantum Registry**:
- Public key registration: <20ms
- Public key lookup: <5ms (cached), <15ms (database)
- Cache hit rate: >90% for active addresses

**Layer-2**:
- L2 block time: 1-2 seconds
- L2 throughput: 1000+ TPS
- Cross-layer message: <5 minutes
- Fraud proof verification: <1 second

### 13.2 Size Specifications

| Component | Size |
|-----------|------|
| FALCON-512 public key | 897 bytes |
| FALCON-512 signature | ~666 bytes (max 700) |
| Quantum witness (registration) | ~1,563 bytes |
| Quantum witness (reference) | ~698 bytes |
| Trust edge | ~100 bytes |
| L2 transaction | ~200 bytes |
| L2 block header | ~500 bytes |
| Fraud proof | <1 MB |

### 13.3 Limits

| Parameter | Value |
|-----------|-------|
| Max contract size | 24 KB |
| Max stack size | 1,024 elements |
| Max call depth | 256 |
| Max gas per tx | 1,000,000 |
| Max gas per block | 10,000,000 |
| Max trust path depth | 10 |
| Max cluster size | 10,000 addresses |
| Registry cache size | 1,000 entries |
| L2 block gas limit | 30,000,000 |
| Challenge period | 24 hours |

---

## 14. Conclusion

Cascoin represents a comprehensive advancement in blockchain technology, addressing the fundamental challenges of programmability, trust, quantum vulnerability, and scalability through four integrated systems:

### Key Achievements

1. **Smart Contracts**: 40+ opcode CVM with quantum signature support
2. **Reputation**: Personalized Web-of-Trust with wallet clustering and HAT v2 scoring
3. **Quantum Security**: Native FALCON-512 support with optimized public key registry
4. **Scalability**: Native L2 with burn-and-mint model and fraud proofs
5. **Economic Security**: Bond-and-slash mechanism with challenger rewards
6. **Backward Compatibility**: Soft fork integration, no chain split

### Vision

We envision Cascoin as the foundation for a new generation of decentralized applications where:
- Trust is earned through relationships and propagated across identities
- Reputation is personal, context-aware, and Sybil-resistant
- Transactions are quantum-secure for long-term protection
- Scalability is native, not bolted on
- Economic incentives align with honest behavior
- Governance is truly decentralized

### Call to Action

**Developers**: Build dApps leveraging CVM, WoT, quantum security, and L2 scaling
**Miners**: Continue securing the network, benefit from increased usage
**Sequencers**: Participate in L2 consensus, earn transaction fees
**Users**: Build your reputation, migrate to quantum addresses, use L2 for fast transactions
**DAO Members**: Help govern the protocol, earn rewards for honest arbitration
**Community**: Spread the word, grow the ecosystem

---

## References

### Academic Papers

1. Zimmermann, P. R. (1992). "PGP User's Guide"
2. Levien, R. (2009). "Attack-Resistant Trust Metrics"
3. Page, L., Brin, S. (1998). "The PageRank Citation Ranking"
4. Nakamoto, S. (2008). "Bitcoin: A Peer-to-Peer Electronic Cash System"
5. Buterin, V. (2014). "Ethereum: A Next-Generation Smart Contract"
6. NIST (2022). "Post-Quantum Cryptography Standardization"
7. Fouque, P.A. et al. (2018). "Falcon: Fast-Fourier Lattice-based Compact Signatures"

### Technical Documentation

- Bitcoin Core: https://github.com/bitcoin/bitcoin
- Litecoin: https://github.com/litecoin-project/litecoin
- LevelDB: https://github.com/google/leveldb
- liboqs: https://github.com/open-quantum-safe/liboqs
- FALCON: https://falcon-sign.info/

---

## Appendix A: Code Statistics

```
Total Lines of Code: ~15,000+
  - CVM Implementation: ~2,500 lines
  - Web-of-Trust: ~3,000 lines
  - Quantum System: ~2,000 lines
  - Layer-2 System: ~5,000 lines
  - RPC Interface: ~1,500 lines
  - Tests: ~3,000 lines

Key Files:
  - src/cvm/ - CVM implementation
  - src/cvm/trustgraph.h - WoT algorithms
  - src/cvm/trustpropagator.h - Cluster propagation
  - src/quantum_registry.h - Quantum key storage
  - src/address_quantum.h - Quantum addresses
  - src/l2/ - Layer-2 implementation

Compilation Status: ✅ Success
Test Coverage: Unit + functional tests
Production Readiness: 80%
```

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| CVM | Cascoin Virtual Machine |
| WoT | Web-of-Trust |
| HAT | Hybrid Adaptive Trust |
| ASRS | Anti-Scam Reputation System |
| DAO | Decentralized Autonomous Organization |
| FALCON-512 | Post-quantum signature algorithm |
| L2 | Layer-2 scaling solution |
| Sybil Attack | Creating fake identities to manipulate system |
| Soft Fork | Backward-compatible protocol upgrade |
| Gas | Computational resource unit |
| Bond | CAS locked as economic security |
| Slash | Penalty for malicious behavior |
| Trust Edge | Directed relationship with weight |
| Trust Path | Sequence of trust edges |
| Wallet Cluster | Group of addresses owned by same entity |
| Sequencer | L2 block producer |
| Fraud Proof | Evidence of invalid state transition |

---

**Document Version**: 2.0
**Last Updated**: January 31, 2026
**Status**: Official Release
**License**: Creative Commons CC-BY-4.0

---

© 2024-2026 Cascoin Project. All rights reserved.
Distributed under MIT License for code, CC-BY-4.0 for documentation.

## 15. References

### Academic Papers

1. Zimmermann, P. R. (1992). "PGP User's Guide"
2. Levien, R. (2009). "Attack-Resistant Trust Metrics"
3. Page, L., Brin, S. (1998). "The PageRank Citation Ranking"
4. Nakamoto, S. (2008). "Bitcoin: A Peer-to-Peer Electronic Cash System"
5. Buterin, V. (2014). "Ethereum: A Next-Generation Smart Contract"
6. Fouque, P.-A., et al. (2018). "Falcon: Fast-Fourier Lattice-based Compact Signatures over NTRU"
7. NIST (2024). "Post-Quantum Cryptography Standardization"

### Technical Documentation

- Bitcoin Core: https://github.com/bitcoin/bitcoin
- Litecoin: https://github.com/litecoin-project/litecoin
- LevelDB: https://github.com/google/leveldb
- FALCON: https://falcon-sign.info/
- evmone: https://github.com/ethereum/evmone
- liboqs: https://github.com/open-quantum-safe/liboqs

### Cascoin Resources

- Source Code: https://github.com/AquaCash/cascoin
- Documentation: doc/
- Quantum Guide: doc/quantum/QUANTUM_MIGRATION_GUIDE.md
- L2 Guide: doc/l2/L2_DEVELOPER_GUIDE.md
- Web-of-Trust: WEB_OF_TRUST.md

---

## Appendix A: Code Statistics

```
Total Lines of Code: ~50,000+
  - CVM Implementation: ~5,000 lines
  - Web-of-Trust/HAT v2: ~8,000 lines
  - Quantum Cryptography: ~3,000 lines
  - Layer 2 System: ~15,000 lines
  - RPC Interface: ~5,000 lines
  - Tests: ~10,000 lines

Key Directories:
  - src/cvm/ - 70+ files
  - src/l2/ - 50+ files
  - src/crypto/ - Cryptographic primitives
  - src/qt/ - GUI implementation

Compilation Status: ✅ Success
Test Coverage: Unit + Functional tests
Production Readiness: Testnet active, Mainnet pending
```

---

## Appendix B: Glossary

**CVM**: Cascoin Virtual Machine
**WoT**: Web-of-Trust
**ASRS**: Anti-Scam Reputation System
**HAT**: Hybrid Adaptive Trust
**DAO**: Decentralized Autonomous Organization
**FALCON-512**: Fast-Fourier Lattice-based Compact Signatures over NTRU
**PQC**: Post-Quantum Cryptography
**L2**: Layer 2 scaling solution
**MEV**: Miner/Maximal Extractable Value
**Sybil Attack**: Creating fake identities to manipulate system
**Soft Fork**: Backward-compatible protocol upgrade
**Gas**: Computational resource unit
**Bond**: CAS locked as economic security
**Slash**: Penalty for malicious behavior
**Trust Edge**: Directed relationship with weight
**Trust Path**: Sequence of trust edges
**Sequencer**: L2 block producer
**Burn-and-Mint**: Token model where L1 tokens are burned to mint L2 tokens

---

## Appendix C: Contact

**Project**: Cascoin Core Team
**Development**: Open source community
**Website**: cascoin.org
**GitHub**: github.com/AquaCash/cascoin
**Documentation**: docs.cascoin.org

---

**Document Version**: 2.0
**Last Updated**: January 31, 2026
**Status**: Official Release
**License**: Creative Commons CC-BY-4.0

---

© 2024-2026 Cascoin Project. All rights reserved.
Distributed under MIT License for code, CC-BY-4.0 for documentation.
