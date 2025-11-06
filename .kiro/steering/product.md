# Cascoin Core Product Overview

Cascoin is a cryptocurrency fork of Litecoin Cash that extends Bitcoin/Litecoin architecture with two major innovations:

## Core Features

**Cascoin Virtual Machine (CVM)**: A register-based virtual machine for smart contract execution, integrated via soft fork for backward compatibility. Features 40+ opcodes across 8 categories (stack, arithmetic, logical, comparison, control flow, storage, cryptographic, context), gas metering (max 1M gas/tx), and persistent storage via LevelDB. Contracts are deployed via OP_RETURN transactions, making them invisible to old nodes.

**Web-of-Trust Reputation System**: A personalized reputation framework where trust is established through graph relationships, economically secured through bonding, and governed by decentralized arbitration (DAO).

## Mining & Consensus

- **Multi-Algorithm Mining**: SHA256d (Bitcoin-compatible), MinotaurX (YesPower-based), and Hive Mining (Labyrinth proof system)
- **Hive Mining**: Unique "bee" system where miners create virtual bees that can mine blocks without continuous hashing (Note: Future versions may transition to "mice and cheese" terminology)
- **2.5 minute block time** with dynamic difficulty adjustment (LWMA-3 algorithm)
- **Backward compatibility** with Bitcoin mining infrastructure for SHA256d
- **Hybrid consensus** combining traditional PoW with innovative Hive system

## Key Differentiators

- **Soft Fork Integration**: CVM/WoT features activate without chain splits using OP_RETURN transactions
- **Economic Security**: Bond-and-slash mechanism prevents spam and malicious behavior
- **Personalized Trust**: Reputation scores are context-aware based on individual trust networks
- **Backward Compatibility**: Old nodes continue to work without upgrades

## Target Use Cases

- Decentralized marketplaces with reputation-based seller scoring
- DeFi credit scoring based on on-chain reputation
- DAO governance with Sybil-resistant voting
- Content curation platforms
- Decentralized identity verification