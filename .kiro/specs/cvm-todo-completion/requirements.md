# Requirements Document

## Introduction

This document specifies the requirements for completing the remaining TODO items in the Cascoin Virtual Machine (CVM) integration. The CVM codebase contains approximately 49 TODO comments across 16 files, representing incomplete implementations that need to be addressed for production readiness. These TODOs fall into several categories: validator attestation and P2P networking, blockchain data access, cryptographic operations, database persistence, and system integration.

## Glossary

- **CVM**: Cascoin Virtual Machine - the smart contract execution engine
- **HAT_Consensus**: Hybrid Adaptive Trust consensus system for validating reputation scores
- **Validator**: A network participant selected to verify transaction reputation claims
- **WoT**: Web-of-Trust - the trust graph system for personalized reputation
- **UTXO**: Unspent Transaction Output - the fundamental unit of Bitcoin-style accounting
- **P2P**: Peer-to-peer network communication between nodes
- **DAO**: Decentralized Autonomous Organization for dispute resolution
- **Mempool**: Memory pool of unconfirmed transactions awaiting block inclusion

## Requirements

### Requirement 1: Validator Cryptographic Operations

**User Story:** As a validator, I want to cryptographically sign and verify validation responses, so that the network can trust the authenticity of my attestations.

#### Acceptance Criteria

1. WHEN a validator generates a validation response, THE Validator_Attestation_System SHALL sign the response using secp256k1 ECDSA signatures
2. WHEN a validation response is received, THE Validator_Attestation_System SHALL verify the signature against the validator's public key
3. IF signature verification fails, THEN THE Validator_Attestation_System SHALL reject the response and log the failure
4. THE Validator_Attestation_System SHALL derive validator addresses from public keys using standard Bitcoin address derivation (Hash160)

### Requirement 2: Validator Wallet Integration

**User Story:** As a node operator, I want the validator system to integrate with my wallet, so that I can participate in validation using my existing keys.

#### Acceptance Criteria

1. WHEN the validator system needs to sign a response, THE Wallet_Integration SHALL retrieve the appropriate private key from the wallet
2. WHEN determining the local validator address, THE Wallet_Integration SHALL query the wallet for the primary address
3. IF the wallet is locked or unavailable, THEN THE Wallet_Integration SHALL return an error and prevent validation participation

### Requirement 3: Validator P2P Network Communication

**User Story:** As a network participant, I want validators to communicate validation tasks and responses over the P2P network, so that distributed consensus can be achieved.

#### Acceptance Criteria

1. WHEN a validation task is created, THE P2P_Network_Layer SHALL broadcast the task to selected validators using connman->ForEachNode
2. WHEN a validator generates a response, THE P2P_Network_Layer SHALL broadcast the response to the network
3. WHEN an attestation request is received, THE P2P_Network_Layer SHALL send the response back to the requesting peer
4. WHEN a batch attestation response is generated, THE P2P_Network_Layer SHALL send it to the requesting peer

### Requirement 4: Blockchain Data Access for Validator Eligibility

**User Story:** As a validator selection system, I want to verify validator eligibility using on-chain data, so that only qualified participants can validate transactions.

#### Acceptance Criteria

1. WHEN verifying stake requirements, THE Eligibility_Verifier SHALL scan the UTXO set for outputs belonging to the validator address
2. WHEN verifying stake requirements, THE Eligibility_Verifier SHALL calculate total stake amount and identify the oldest UTXO for stake age determination
3. WHEN verifying history requirements, THE Eligibility_Verifier SHALL scan the blockchain for the first transaction involving the address
4. WHEN verifying history requirements, THE Eligibility_Verifier SHALL count total transactions and unique address interactions
5. WHEN verifying anti-Sybil requirements, THE Eligibility_Verifier SHALL verify stake originates from 3 or more diverse funding sources

### Requirement 5: Database Persistence for Validator Records

**User Story:** As a system administrator, I want validator eligibility records to be persisted to the database, so that eligibility checks are efficient and survive restarts.

#### Acceptance Criteria

1. WHEN a validator eligibility record is computed, THE Database_Layer SHALL write the record to LevelDB
2. WHEN loading validator records on startup, THE Database_Layer SHALL iterate through stored records to rebuild the validator pool
3. THE Database_Layer SHALL use proper serialization for all validator data structures

### Requirement 6: HAT Consensus DAO Integration

**User Story:** As a DAO member, I want to be notified of disputes requiring resolution, so that I can participate in governance decisions.

#### Acceptance Criteria

1. WHEN a dispute is escalated to the DAO, THE DAO_Integration SHALL notify DAO members via the P2P network
2. WHEN packaging dispute evidence, THE DAO_Integration SHALL serialize validator responses and trust graph data
3. WHEN recording fraud attempts, THE DAO_Integration SHALL load the actual transaction for evidence

### Requirement 7: Mempool Manager Block Height Access

**User Story:** As a mempool manager, I want access to the current block height, so that I can properly calculate gas allowances and rate limits.

#### Acceptance Criteria

1. WHEN calculating gas allowances, THE Mempool_Manager SHALL retrieve the actual current block height from chainActive
2. WHEN iterating mempool for priority distribution, THE Mempool_Manager SHALL access the actual mempool data structure

### Requirement 8: Gas System Integration

**User Story:** As a transaction submitter, I want accurate gas pricing based on network conditions, so that I can pay appropriate fees.

#### Acceptance Criteria

1. WHEN calculating gas prices, THE Sustainable_Gas_System SHALL compute a price multiplier based on network congestion levels
2. WHEN calculating trust density, THE Sustainable_Gas_System SHALL integrate with the reputation system to obtain network-wide statistics
3. WHEN validating gas subsidies, THE Gas_Subsidy_Tracker SHALL verify subsidy eligibility and enforce amount limits

### Requirement 9: Consensus Validator Integration

**User Story:** As a block validator, I want proper integration between consensus validation and reputation systems, so that transactions are validated correctly.

#### Acceptance Criteria

1. IF HAT v2 is unavailable, THEN THE Consensus_Validator SHALL fall back to ASRS (Adaptive Stake-weighted Reputation System)
2. WHEN validating transactions, THE Consensus_Validator SHALL extract sender addresses from transaction inputs using script parsing
3. WHEN validating pool balances, THE Consensus_Validator SHALL query the actual pool balance from the database
4. WHEN validating transaction costs, THE Consensus_Validator SHALL extract gas usage and costs from CVM transaction data

### Requirement 10: Block Validator CVM Activation

**User Story:** As a node operator, I want the CVM soft fork activation to be properly checked, so that CVM transactions are only processed after activation.

#### Acceptance Criteria

1. WHEN processing blocks, THE Block_Validator SHALL check if the CVM soft fork is active at the current height using chainparams consensus rules
2. WHEN validating HAT v2 scores, THE Block_Validator SHALL verify scores have not expired based on block height
3. WHEN validating transaction fees, THE Block_Validator SHALL verify actual fees match expected fees from gas calculation

### Requirement 11: Soft Fork Contract Validation

**User Story:** As a contract deployer, I want proper reputation checks during deployment, so that only trusted users can deploy contracts.

#### Acceptance Criteria

1. WHEN a contract deployment is submitted, THE Soft_Fork_Validator SHALL extract the deployer address from transaction inputs
2. WHEN a contract deployment is submitted, THE Soft_Fork_Validator SHALL verify the deployer's reputation meets the minimum threshold (50)
3. WHEN a contract call specifies a format, THE Soft_Fork_Validator SHALL verify the contract format byte matches the expected format

### Requirement 12: Transaction Builder Contract Support

**User Story:** As a smart contract user, I want to send value to contracts, so that I can interact with payable contract functions.

#### Acceptance Criteria

1. WHEN building a transaction with contract value transfer, THE Transaction_Builder SHALL resolve the contract's P2SH address from the contract address
2. WHEN the DAO slash mechanism is invoked, THE Transaction_Builder SHALL construct a valid slash transaction spending the bond output to the slash recipient

### Requirement 13: Cross-Chain Trust Verification

**User Story:** As a cross-chain user, I want my trust relationships from other chains to be verified, so that I can maintain reputation across networks.

#### Acceptance Criteria

1. WHEN verifying cross-chain trust claims, THE Trust_Context SHALL perform full cryptographic verification using LayerZero or CCIP protocol attestations
2. WHEN verifying merkle proofs for reputation data, THE Reputation_Signature SHALL implement complete binary merkle tree proof verification

### Requirement 14: Cleanup Manager Resource Reclamation

**User Story:** As a node operator, I want expired contracts to be properly cleaned up, so that storage is reclaimed efficiently.

#### Acceptance Criteria

1. WHEN a contract expires, THE Cleanup_Manager SHALL delete all storage entries associated with the contract address
2. WHEN a contract expires, THE Cleanup_Manager SHALL update contract metadata to mark the contract as cleaned up
3. WHEN storage cleanup completes, THE Cleanup_Manager SHALL trigger database compaction to reclaim disk space

### Requirement 15: Vote Manipulation Detection

**User Story:** As a security system, I want to detect vote manipulation attempts, so that the reputation system remains trustworthy.

#### Acceptance Criteria

1. WHEN pruning reputation history, THE Vote_Manipulation_Detector SHALL retrieve the current block height from chainActive
2. WHEN performing time-based calculations, THE Vote_Manipulation_Detector SHALL use accurate block height references from the active chain

### Requirement 16: Eclipse and Sybil Protection

**User Story:** As a network security system, I want to detect coordinated validator attacks, so that consensus cannot be manipulated.

#### Acceptance Criteria

1. WHEN analyzing validator timing patterns, THE Eclipse_Sybil_Protection SHALL store validation timestamps in the ValidatorNetworkInfo structure
2. WHEN detecting coordinated attacks, THE Eclipse_Sybil_Protection SHALL flag patterns where 5 or more validators respond within 1 second of each other
