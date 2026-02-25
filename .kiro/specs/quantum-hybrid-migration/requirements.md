# Requirements Document

## Introduction

This document specifies the requirements for implementing Post-Quantum Cryptography (PQC) using FALCON-512 in the Cascoin blockchain. The implementation provides a hybrid cryptographic approach that maintains full backward compatibility with existing ECDSA-based transactions while enabling quantum-resistant signatures for new transactions. This upgrade is critical for long-term security against quantum computing threats while preserving the existing network's functionality.

## Glossary

- **PQC_System**: The Post-Quantum Cryptography subsystem responsible for FALCON-512 key generation, signing, and verification
- **Key_Manager**: The component managing dual-stack cryptographic keys (ECDSA and FALCON-512)
- **Witness_Verifier**: The script interpreter component that validates signatures based on witness version
- **Address_Encoder**: The component responsible for encoding and decoding quantum-safe addresses
- **Migration_Service**: The wallet service that facilitates migration from legacy to quantum addresses
- **Hive_Agent_Manager**: The component managing Hive mining agents with support for both key types
- **CVM_Signature_Verifier**: The CVM opcode handler for signature verification supporting both algorithms
- **Network_Protocol_Handler**: The P2P networking component handling quantum-aware message formats
- **FALCON-512**: A lattice-based digital signature algorithm selected by NIST for post-quantum standardization
- **Witness_Version_2**: The new segregated witness version designated for quantum transactions
- **liboqs**: Open Quantum Safe library providing FALCON-512 implementation
- **HRP**: Human Readable Part of Bech32/Bech32m encoded addresses

## Requirements

### Requirement 1: Dual-Stack Key Management

**User Story:** As a wallet developer, I want the key management system to support both ECDSA and FALCON-512 keys polymorphically, so that the wallet can handle both legacy and quantum-resistant transactions seamlessly.

#### Acceptance Criteria

1. THE Key_Manager SHALL support storage of ECDSA private keys (32 bytes) and FALCON-512 private keys (1281 bytes) using a unified interface
2. THE Key_Manager SHALL support storage of ECDSA public keys (33/65 bytes compressed/uncompressed) and FALCON-512 public keys (897 bytes) using a unified interface
3. WHEN a key is created, THE Key_Manager SHALL store a key type flag (KEY_TYPE_ECDSA or KEY_TYPE_QUANTUM) to distinguish between key types
4. THE Key_Manager SHALL provide a GetKeyType() method that returns the cryptographic algorithm type for any stored key
5. WHEN Sign() is called on a quantum key, THE PQC_System SHALL generate a FALCON-512 signature (approximately 666 bytes)
6. WHEN Sign() is called on an ECDSA key, THE Key_Manager SHALL generate a standard secp256k1 signature (64-72 bytes)
7. THE Key_Manager SHALL use secure memory allocation (secure_allocator) for all private key storage regardless of key type
8. WHEN a quantum key is serialized, THE Key_Manager SHALL include the key type flag in the serialization format
9. WHEN a quantum key is deserialized, THE Key_Manager SHALL correctly restore the key type and validate key data integrity

### Requirement 2: Witness Version 2 Consensus Rules

**User Story:** As a node operator, I want the consensus rules to support Witness Version 2 for quantum signatures, so that quantum-signed transactions are validated correctly while legacy transactions continue to work.

#### Acceptance Criteria

1. WHEN VerifyScript encounters a witness program with version 0 or 1, THE Witness_Verifier SHALL use existing secp256k1 ECDSA verification
2. WHEN VerifyScript encounters a witness program with version 2, THE Witness_Verifier SHALL use FALCON-512 verification via liboqs
3. THE Witness_Verifier SHALL accept FALCON-512 signatures up to 700 bytes in witness version 2 programs
4. WHEN a witness version 2 signature verification fails, THE Witness_Verifier SHALL return SCRIPT_ERR_SIG_QUANTUM_VERIFY
5. THE Witness_Verifier SHALL reject witness version 2 transactions before the soft fork activation height
6. WHEN witness version 2 is active, THE Witness_Verifier SHALL validate that the public key in the witness is exactly 897 bytes
7. IF a witness version 2 program contains an invalid public key size, THEN THE Witness_Verifier SHALL return SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH
8. THE Witness_Verifier SHALL compute signature hashes for witness version 2 using the same BIP143-style algorithm as witness version 0

### Requirement 3: Quantum Address Format

**User Story:** As a user, I want distinct address formats for quantum-safe addresses, so that I can easily identify and use quantum-resistant addresses.

#### Acceptance Criteria

1. THE Address_Encoder SHALL encode quantum addresses using Bech32m with HRP "casq" for mainnet
2. THE Address_Encoder SHALL encode quantum addresses using Bech32m with HRP "tcasq" for testnet
3. THE Address_Encoder SHALL encode quantum addresses using Bech32m with HRP "rcasq" for regtest
4. WHEN encoding a quantum address, THE Address_Encoder SHALL use witness version 2 in the Bech32m encoding
5. WHEN decoding an address with HRP "casq", "tcasq", or "rcasq", THE Address_Encoder SHALL recognize it as a quantum address
6. THE Address_Encoder SHALL derive quantum address data from the SHA256 hash of the 897-byte FALCON-512 public key
7. WHEN a legacy address (Base58 or Bech32 v0/v1) is decoded, THE Address_Encoder SHALL route to ECDSA verification logic
8. WHEN a quantum address is decoded, THE Address_Encoder SHALL route to FALCON-512 verification logic
9. THE Address_Encoder SHALL reject addresses with witness version 2 but incorrect HRP

### Requirement 4: Hive Mining Quantum Support

**User Story:** As a Hive miner, I want to create and use mining agents with quantum-safe keys, so that my mining operations are protected against future quantum attacks.

#### Acceptance Criteria

1. THE Hive_Agent_Manager SHALL accept both ECDSA and FALCON-512 signatures for agent creation transactions
2. WHEN creating a new Hive agent after soft fork activation, THE Hive_Agent_Manager SHALL default to FALCON-512 keys
3. THE Hive_Agent_Manager SHALL continue to validate existing ECDSA-based agents without modification
4. WHEN a Hive block is mined with a quantum agent, THE Hive_Agent_Manager SHALL include the FALCON-512 signature in the block
5. THE Hive_Agent_Manager SHALL verify agent ownership using the appropriate signature algorithm based on the agent's key type
6. WHEN validating a Hive coinbase, THE Hive_Agent_Manager SHALL check the signature type matches the agent's registered key type
7. THE Hive_Agent_Manager SHALL allow migration of existing agents to quantum keys via a special migration transaction

### Requirement 5: Wallet Migration Service

**User Story:** As a wallet user, I want to migrate my funds from legacy addresses to quantum-safe addresses, so that I can protect my holdings against quantum threats.

#### Acceptance Criteria

1. WHEN GetNewAddress() is called after soft fork activation, THE Migration_Service SHALL generate a quantum (FALCON-512) address by default
2. THE Migration_Service SHALL provide GetLegacyAddress() function for backward compatibility when legacy addresses are needed
3. THE Migration_Service SHALL provide migrate_to_quantum RPC command that sweeps funds from legacy UTXOs to a new quantum address
4. WHEN migrate_to_quantum is called, THE Migration_Service SHALL create a transaction spending all legacy UTXOs to a single quantum address
5. THE Migration_Service SHALL estimate and display the fee for migration transactions before execution
6. WHEN migrate_to_quantum is called with insufficient funds for fees, THE Migration_Service SHALL return an error with the required amount
7. THE Migration_Service SHALL support partial migration by allowing users to specify which UTXOs to migrate
8. THE Migration_Service SHALL log all migration transactions for audit purposes

### Requirement 6: Soft Fork Activation

**User Story:** As a network participant, I want the quantum features to activate via soft fork at a predetermined height, so that the network upgrade is coordinated and backward compatible.

#### Acceptance Criteria

1. THE PQC_System SHALL activate at block height 350000 on mainnet (current height: ~276606)
2. THE PQC_System SHALL activate at block height 50000 on testnet (current height: ~5377)
3. THE PQC_System SHALL activate at block height 1 on regtest for testing purposes
4. WHILE the current block height is below the activation height, THE Witness_Verifier SHALL treat witness version 2 as anyone-can-spend (future soft fork compatibility)
5. WHEN the activation height is reached, THE PQC_System SHALL begin enforcing FALCON-512 signature validation for witness version 2
6. THE PQC_System SHALL add a new deployment entry DEPLOYMENT_QUANTUM in consensus params
7. IF a witness version 2 transaction is included before activation, THEN THE Witness_Verifier SHALL reject the block as invalid after activation
8. THE PQC_System SHALL maintain backward compatibility such that all pre-activation transactions remain valid

### Requirement 7: CVM Smart Contract Integration

**User Story:** As a smart contract developer, I want CVM contracts to support both ECDSA and FALCON-512 signatures, so that contracts can verify quantum-safe signatures.

#### Acceptance Criteria

1. THE CVM_Signature_Verifier SHALL extend the VERIFY_SIG opcode to accept both ECDSA and FALCON-512 signatures
2. WHEN VERIFY_SIG receives a signature longer than 100 bytes, THE CVM_Signature_Verifier SHALL attempt FALCON-512 verification
3. WHEN VERIFY_SIG receives a signature of 72 bytes or less, THE CVM_Signature_Verifier SHALL use ECDSA verification
4. THE CVM_Signature_Verifier SHALL charge 3000 gas for FALCON-512 signature verification (vs 60 gas for ECDSA)
5. THE CVM_Signature_Verifier SHALL provide a new opcode VERIFY_SIG_QUANTUM that explicitly requires FALCON-512
6. THE CVM_Signature_Verifier SHALL provide a new opcode VERIFY_SIG_ECDSA that explicitly requires ECDSA
7. WHEN a contract address is derived, THE CVM_Signature_Verifier SHALL use the same derivation regardless of deployer key type
8. IF VERIFY_SIG_QUANTUM receives an ECDSA signature, THEN THE CVM_Signature_Verifier SHALL return verification failure

### Requirement 8: Network Protocol Extensions

**User Story:** As a node developer, I want the P2P protocol to handle larger quantum signatures efficiently, so that network performance is maintained.

#### Acceptance Criteria

1. THE Network_Protocol_Handler SHALL extend transaction messages to support signatures up to 700 bytes
2. THE Network_Protocol_Handler SHALL implement version negotiation to identify quantum-aware peers
3. WHEN connecting to a peer, THE Network_Protocol_Handler SHALL exchange quantum capability flags in the version message
4. THE Network_Protocol_Handler SHALL maintain backward compatible message formats for non-quantum peers
5. WHEN relaying a quantum transaction to a non-quantum peer, THE Network_Protocol_Handler SHALL skip the relay
6. THE Network_Protocol_Handler SHALL implement inventory type MSG_QUANTUM_TX for quantum transactions
7. THE Network_Protocol_Handler SHALL prioritize quantum transaction relay to quantum-aware peers
8. WHEN a block contains quantum transactions, THE Network_Protocol_Handler SHALL relay it to all peers (quantum and non-quantum)

### Requirement 9: Security and Performance

**User Story:** As a security auditor, I want the quantum implementation to meet security standards and performance targets, so that the network remains secure and responsive.

#### Acceptance Criteria

1. THE PQC_System SHALL use FALCON-512 providing NIST Level 1 security (128-bit quantum security)
2. THE PQC_System SHALL generate FALCON-512 signatures in less than 10 milliseconds on reference hardware
3. THE PQC_System SHALL verify FALCON-512 signatures in less than 2 milliseconds on reference hardware
4. THE PQC_System SHALL use at least 256 bits of entropy from the system CSPRNG for key generation
5. THE PQC_System SHALL implement constant-time operations for signature generation to prevent timing attacks
6. WHEN a transaction includes a quantum signature, THE PQC_System SHALL increase the transaction virtual size by the signature size
7. THE PQC_System SHALL limit the maximum additional size per signature to 1024 bytes
8. THE PQC_System SHALL validate that FALCON-512 signatures are in canonical form to prevent malleability
9. IF signature malleability is detected, THEN THE PQC_System SHALL reject the signature as invalid

### Requirement 10: Serialization and Storage

**User Story:** As a wallet developer, I want quantum keys and signatures to be properly serialized and stored, so that wallet data remains consistent and recoverable.

#### Acceptance Criteria

1. THE Key_Manager SHALL serialize quantum private keys with a version byte prefix (0x02 for quantum)
2. THE Key_Manager SHALL serialize quantum public keys with a version byte prefix (0x05 for quantum)
3. WHEN storing quantum keys in the wallet database, THE Key_Manager SHALL use the existing key storage schema with extended fields
4. THE Key_Manager SHALL support BIP32-style hierarchical deterministic derivation for quantum keys where mathematically possible
5. IF BIP32 derivation is not possible for quantum keys, THEN THE Key_Manager SHALL use an alternative deterministic derivation scheme
6. THE Key_Manager SHALL maintain separate key pools for ECDSA and quantum keys
7. WHEN backing up a wallet, THE Key_Manager SHALL include both ECDSA and quantum keys in the backup
8. THE Key_Manager SHALL support importing quantum private keys via WIF-like encoding with distinct version bytes
