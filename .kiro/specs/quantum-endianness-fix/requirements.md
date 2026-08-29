# Requirements Document

## Introduction

This specification addresses an endianness inconsistency in how the quantum witness program (SHA256 hash of the FALCON-512 public key) is handled across the Cascoin Core codebase. The SHA256 hash is stored internally as a `uint256` (little-endian), but is reversed to big-endian during Bech32m address encoding. The decoding path does not reverse the bytes back, causing a mismatch that propagates through script storage, verification, signing, and mining. After commit c75c089b, transactions are accepted into the mempool but blocks containing quantum transactions cannot be mined.

## Glossary

- **Witness_Program**: The 32-byte SHA256 hash of a FALCON-512 public key, stored in a transaction output script as the quantum address identifier
- **uint256**: A 256-bit unsigned integer type inherited from Bitcoin Core that stores bytes in little-endian order internally
- **Bech32m**: The address encoding format (BIP-350) used for quantum addresses with HRP prefixes casq/tcasq/rcasq
- **GetQuantumID**: The `CPubKey` method that computes single SHA256 of the quantum public key, returning a `uint256` (little-endian)
- **Hash_Function**: `Hash()` computes double SHA256 (SHA256d); `CSHA256` computes single SHA256. These produce different outputs for the same input
- **Encoding_Path**: The data flow from `GetQuantumID()` through `EncodeQuantumAddress()` to Bech32m string, which reverses bytes from LE to BE
- **Decoding_Path**: The data flow from Bech32m string through `DecodeDestination()` to `WitnessV2Quantum`, which stores bytes as-is (BE) without reversing back to LE
- **Script_Path**: The data flow from `WitnessV2Quantum` through `GetScriptForDestination()` to the witness script, which stores the program bytes directly
- **Verification_Path**: The data flow in `VerifyWitnessV2Quantum()` that extracts program bytes from the script and compares them against `SHA256(pubkey)`
- **Signing_Path**: The data flow in `ProduceSignature()` and `IsSolvable()` that extracts program bytes from the script and matches them against `GetQuantumID()`
- **Mining_Path**: The block assembly path in `CreateNewBlock` that validates transactions containing quantum inputs/outputs
- **LE**: Little-endian byte order (least significant byte first), the internal storage order of `uint256`
- **BE**: Big-endian byte order (most significant byte first), the byte order used in Bech32m encoding and currently stored in scripts

## Requirements

### Requirement 1: Canonical Byte Order for Witness Program Storage

**User Story:** As a Cascoin Core developer, I want a single canonical byte order for the quantum witness program throughout the codebase, so that all code paths produce consistent comparisons without ad-hoc byte reversals.

#### Acceptance Criteria

1. THE Decoding_Path SHALL store the witness program in the same byte order as `GetQuantumID()` output (LE) when constructing a `WitnessV2Quantum` from a decoded Bech32m address
2. THE Script_Path SHALL store the witness program bytes in the canonical order (matching `GetQuantumID()` output) within the witness script
3. WHEN a `WitnessV2Quantum` destination is created from a decoded quantum address, THE Decoding_Path SHALL reverse the Bech32m-decoded bytes (BE) back to LE before storing them in the `WitnessV2Quantum`
4. WHEN a `WitnessV2Quantum` destination is created directly from `GetQuantumID()`, THE destination SHALL store the bytes in LE without modification

### Requirement 2: Address Encoding Round-Trip Consistency

**User Story:** As a Cascoin user, I want quantum address encoding and decoding to be perfectly reversible, so that sending coins to a quantum address always produces the correct script.

#### Acceptance Criteria

1. FOR ALL valid FALCON-512 public keys, encoding a quantum address via `EncodeQuantumAddress()` then decoding via `DecodeDestination()` SHALL produce a `WitnessV2Quantum` whose bytes match the original `GetQuantumID()` output exactly
2. FOR ALL valid quantum address strings, decoding via `DecodeDestination()` then re-encoding via the `WitnessV2Quantum` encoder SHALL produce the original address string
3. WHEN `DecodeDestination()` processes a quantum Bech32m address, THE Decoding_Path SHALL reverse the 32 program bytes from BE to LE so that the resulting `WitnessV2Quantum` matches the `GetQuantumID()` output for the same public key

### Requirement 3: Verification Without Ad-Hoc Byte Reversal

**User Story:** As a Cascoin Core developer, I want the verification path to compare the witness program against `SHA256(pubkey)` without needing to reverse bytes at comparison time, so that the code is simpler and less error-prone.

#### Acceptance Criteria

1. WHEN `VerifyWitnessV2Quantum()` extracts the program bytes from the script, THE Verification_Path SHALL compare them directly against the `CSHA256` output without any byte reversal at comparison time
2. WHEN the canonical byte order fix is applied to the Decoding_Path and Script_Path, THE Verification_Path SHALL remove the existing `programLE` reversal loop in `VerifyWitnessV2Quantum()`
3. THE Verification_Path SHALL use single SHA256 (`CSHA256`) for computing the pubkey hash, consistent with `GetQuantumID()`

### Requirement 4: Signing Without Ad-Hoc Byte Reversal

**User Story:** As a Cascoin Core developer, I want the signing path to match witness program bytes against `GetQuantumID()` without needing to reverse bytes, so that signing logic is straightforward.

#### Acceptance Criteria

1. WHEN `ProduceSignature()` extracts the witness program from `vSolutions[0]` for a `TX_WITNESS_V2_QUANTUM` output, THE Signing_Path SHALL use the bytes directly to construct the `pubkeyHash` for key lookup without any byte reversal
2. WHEN `IsSolvable()` extracts the witness program from `vSolutions[0]`, THE Signing_Path SHALL use the bytes directly for comparison against `GetQuantumID()` without any byte reversal
3. WHEN `IsMine()` extracts the witness program from `vSolutions`, THE Signing_Path SHALL use the bytes directly for comparison against `GetQuantumID()` without any byte reversal

### Requirement 5: Hash Function Consistency

**User Story:** As a Cascoin Core developer, I want all quantum pubkey hash computations to use the same hash function, so that hash comparisons never fail due to single-SHA256 vs double-SHA256 mismatch.

#### Acceptance Criteria

1. THE `GetQuantumID()` method SHALL use single SHA256 (`CSHA256`) to compute the quantum public key hash
2. THE `VerifyWitnessV2Quantum()` function SHALL use single SHA256 (`CSHA256`) to compute the pubkey hash for comparison
3. THE `ParseQuantumWitness()` function SHALL use single SHA256 (`CSHA256`) to compute the pubkey hash for registration witnesses
4. THE `VerifyQuantumTransaction()` function SHALL use single SHA256 (`CSHA256`) to compute the pubkey hash, replacing the current `Hash()` (double SHA256) call
5. THE `GetQuantumWitnessProgram()` functions in `address_quantum.cpp` SHALL use single SHA256 (`CSHA256`) to compute the witness program

### Requirement 6: Block Mining Compatibility

**User Story:** As a Cascoin miner, I want blocks containing quantum transactions to be mineable and valid, so that quantum address users can transact normally.

#### Acceptance Criteria

1. WHEN `CreateNewBlock` assembles a block containing transactions with quantum inputs, THE Mining_Path SHALL produce a valid block that passes `CheckBlock` and `ConnectBlock` validation
2. WHEN a block contains both ECDSA and quantum transactions, THE Mining_Path SHALL validate all transactions consistently regardless of signature type
3. WHEN a quantum transaction is in the mempool and a miner calls `getblocktemplate` or mines via MinotaurX, THE Mining_Path SHALL include the transaction in the candidate block without error

### Requirement 7: Backward Compatibility for Existing Quantum UTXOs

**User Story:** As a Cascoin user with coins on a quantum address, I want the endianness fix to handle my existing UTXOs correctly, so that I do not lose access to my funds.

#### Acceptance Criteria

1. IF existing quantum UTXOs were created with the old (BE) byte order in scripts, THEN THE system SHALL provide a migration strategy or dual-recognition mechanism so that those UTXOs remain spendable
2. WHEN the fix is deployed on testnet, THE system SHALL handle both pre-fix and post-fix quantum UTXOs during the transition period
3. IF the testnet is reset (redeployed from genesis), THEN THE system SHALL only need to support the corrected (canonical LE) byte order

### Requirement 8: Existing ECDSA Transaction Non-Interference

**User Story:** As a Cascoin user with coins on a standard ECDSA address, I want the quantum endianness fix to have zero impact on my transactions, so that my funds remain safe and spendable.

#### Acceptance Criteria

1. WHEN a transaction spends from a standard ECDSA (P2PKH, P2SH, P2WPKH, P2WSH) output, THE system SHALL process the transaction identically to the behavior before the fix
2. WHEN a transaction sends to a standard ECDSA address, THE system SHALL create the output script identically to the behavior before the fix
3. THE fix SHALL modify only code paths that handle `TX_WITNESS_V2_QUANTUM` or `WitnessV2Quantum` types

### Requirement 9: Unit Test Coverage for Endianness Consistency

**User Story:** As a Cascoin Core developer, I want comprehensive unit tests for the quantum endianness fix, so that regressions are caught automatically.

#### Acceptance Criteria

1. THE test suite SHALL include a round-trip test: for a generated FALCON-512 keypair, `EncodeQuantumAddress()` then `DecodeDestination()` SHALL produce a `WitnessV2Quantum` matching `GetQuantumID()`
2. THE test suite SHALL include a verification test: for a generated FALCON-512 keypair, the witness program stored in the script SHALL match `CSHA256(pubkey)` output directly without byte reversal
3. THE test suite SHALL include a signing test: `ProduceSignature()` SHALL successfully sign a transaction spending from a quantum output
4. THE test suite SHALL include a hash consistency test: `GetQuantumID()`, `GetQuantumWitnessProgram()`, `ParseQuantumWitness()` pubkey hash, and `VerifyQuantumTransaction()` pubkey hash SHALL all produce identical results for the same public key
5. THE test suite SHALL include a non-interference test: ECDSA transaction signing and verification SHALL produce identical results before and after the fix
