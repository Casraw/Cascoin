# Implementation Plan: CVM TODO Completion

## Overview

This implementation plan addresses the ~49 TODO items across 16 CVM source files. The tasks are organized into four categories: Core Validator Infrastructure, Blockchain Integration, System Components, and Security & Maintenance. Each task builds incrementally on previous work and references specific requirements from the requirements document.

## Tasks

- [x] 1. Implement Validator Cryptographic Operations
  - [x] 1.1 Implement signature generation in ValidationResponse::Sign()
    - Use CKey::Sign() with secp256k1 ECDSA
    - Hash message using CHashWriter with SER_GETHASH
    - Store 64-byte signature in response.signature
    - _Requirements: 1.1_
  - [x] 1.2 Implement signature verification in ValidationResponse::VerifySignature()
    - Use CPubKey::Verify() to validate signature
    - Reconstruct message hash from response fields
    - Return false on verification failure with logging
    - _Requirements: 1.2, 1.3_
  - [x] 1.3 Implement DeriveValidatorAddress() helper function
    - Use CPubKey::GetID() which returns CKeyID (uint160)
    - Ensure consistent with standard Bitcoin P2PKH derivation
    - _Requirements: 1.4_
  - [ ]* 1.4 Write property test for signature round-trip
    - **Property 1: Signature Round-Trip**
    - **Validates: Requirements 1.1, 1.2**

- [x] 2. Implement Wallet Integration for Validators
  - [x] 2.1 Implement GetMyValidatorAddress() in validator_attestation.cpp
    - Access wallet via pwalletMain global
    - Return primary receiving address as validator address
    - Handle wallet unavailable case
    - _Requirements: 2.2, 2.3_
  - [x] 2.2 Implement GetValidatorKey() for signing operations
    - Use CWallet::GetKey() to retrieve private key
    - Check CWallet::IsLocked() before access
    - Return error if wallet locked or key not found
    - _Requirements: 2.1, 2.3_
  - [ ]* 2.3 Write property test for wallet key retrieval
    - **Property 4: Wallet Key Retrieval Consistency**
    - **Validates: Requirements 2.1, 2.2**

- [x] 3. Implement P2P Network Communication for Validators
  - [x] 3.1 Implement BroadcastValidationTask() using connman->ForEachNode
    - Create messages using CNetMsgMaker
    - Use MSG_VALIDATION_TASK message type
    - Serialize task data using CDataStream
    - _Requirements: 3.1_
  - [x] 3.2 Implement BroadcastValidationResponse() using connman->ForEachNode
    - Use MSG_VALIDATION_RESPONSE message type
    - Include signature in broadcast
    - _Requirements: 3.2_
  - [x] 3.3 Implement SendToPeer() for attestation responses
    - Send response back to requesting peer in HandleAttestationRequest()
    - Send batch response in HandleBatchAttestationRequest()
    - _Requirements: 3.3, 3.4_

- [x] 4. Checkpoint - Ensure validator infrastructure compiles
  - Verify all validator cryptographic, wallet, and P2P code compiles
  - Run existing unit tests to ensure no regressions
  - Ask the user if questions arise

- [x] 5. Implement Blockchain Data Access for Validator Eligibility
  - [x] 5.1 Implement VerifyStakeRequirements() with UTXO scanning
    - Use pcoinsTip to scan UTXO set for validator address
    - Calculate total stake amount from matching outputs
    - Identify oldest UTXO for stake age determination
    - _Requirements: 4.1, 4.2_
  - [x] 5.2 Implement VerifyHistoryRequirements() with blockchain scanning
    - Scan blockchain for first transaction involving address
    - Count total transactions and unique address interactions
    - Use address index if available, fall back to block scanning
    - _Requirements: 4.3, 4.4_
  - [x] 5.3 Implement VerifyAntiSybilRequirements() for funding source diversity
    - Verify stake originates from 3+ diverse funding sources
    - Track input sources for stake UTXOs
    - _Requirements: 4.5_
  - [ ]* 5.4 Write property tests for stake and history verification
    - **Property 5: Stake Verification Accuracy**
    - **Property 6: History Verification Accuracy**
    - **Property 7: Stake Diversity Enforcement**
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**

- [x] 6. Implement Database Persistence for Validator Records
  - [x] 6.1 Implement WriteValidatorRecord() in cvmdb.cpp
    - Use key format: "validator_" + address.ToString()
    - Serialize using CDataStream with SER_DISK
    - _Requirements: 5.1_
  - [x] 6.2 Implement IterateValidatorRecords() for pool loading
    - Use LevelDB iterator with "validator_" prefix
    - Rebuild validator pool on startup
    - _Requirements: 5.2_
  - [x] 6.3 Complete ListKeysWithPrefix() implementation in walletcluster.cpp
    - Implement proper database iteration for cache rebuild
    - _Requirements: 5.3_
  - [ ]* 6.4 Write property test for database persistence round-trip
    - **Property 8: Database Persistence Round-Trip**
    - **Validates: Requirements 5.1, 5.2, 5.3**

- [x] 7. Checkpoint - Ensure blockchain integration compiles and tests pass
  - Verify UTXO scanning and database persistence code compiles
  - Run unit tests for database operations
  - Ask the user if questions arise

- [x] 8. Implement HAT Consensus DAO Integration
  - [x] 8.1 Implement NotifyDAOMembers() via P2P network
    - Use MSG_DAO_DISPUTE message type
    - Broadcast to DAO member addresses
    - _Requirements: 6.1_
  - [x] 8.2 Implement PackageDisputeEvidence() serialization
    - Serialize validator responses using CDataStream
    - Include trust graph snapshot at dispute time
    - _Requirements: 6.2_
  - [x] 8.3 Implement LoadTransactionEvidence() for fraud recording
    - Load transaction from pcoinsTip or block index
    - Include in fraud attempt record
    - _Requirements: 6.3_
  - [ ]* 8.4 Write property test for evidence serialization round-trip
    - **Property 9: DAO Evidence Serialization Round-Trip**
    - **Validates: Requirements 6.2, 6.3**

- [x] 9. Implement Mempool Manager Block Height Access
  - [x] 9.1 Fix GetCurrentBlockHeight() in mempool_manager.cpp
    - Replace placeholder with chainActive.Height()
    - Use proper locking (LOCK(cs_main))
    - _Requirements: 7.1_
  - [x] 9.2 Implement mempool iteration for priority distribution
    - Access mempool.mapTx with proper locking (LOCK(mempool.cs))
    - Count transactions by priority level
    - _Requirements: 7.2_
  - [ ]* 9.3 Write property test for block height consistency
    - **Property 10: Block Height Consistency**
    - **Validates: Requirements 7.1, 15.1**

- [x] 10. Implement Gas System Integration
  - [x] 10.1 Implement GetCurrentPriceMultiplier() in sustainable_gas.cpp
    - Track gas usage over last 100 blocks
    - Calculate multiplier based on congestion vs target
    - Clamp to [0.5, 2.0] range
    - _Requirements: 8.1_
  - [x] 10.2 Implement CalculateTrustDensity() with reputation integration
    - Query reputation system for network-wide statistics
    - Calculate trust density metric
    - _Requirements: 8.2_
  - [x] 10.3 Implement ValidateGasSubsidy() in mempool_manager.cpp
    - Verify subsidy eligibility
    - Enforce amount limits
    - _Requirements: 8.3_
  - [ ]* 10.4 Write property test for gas price multiplier bounds
    - **Property 11: Gas Price Multiplier Bounds**
    - **Validates: Requirements 8.1**

- [x] 11. Checkpoint - Ensure system components compile
  - Verify DAO, mempool, and gas system code compiles
  - Run unit tests for gas calculations
  - Ask the user if questions arise

- [x] 12. Implement Consensus Validator Integration
  - [x] 12.1 Implement ASRS fallback in consensus_validator.cpp
    - Check HAT v2 availability
    - Fall back to ASRS when unavailable
    - _Requirements: 9.1_
  - [x] 12.2 Implement ExtractSenderAddress() from transaction inputs
    - Parse P2PKH/P2WPKH scripts to extract sender
    - Use first input for sender determination
    - _Requirements: 9.2_
  - [x] 12.3 Implement GetPoolBalance() database query
    - Query LevelDB with key "pool_<id>_balance"
    - Return cached value on failure
    - _Requirements: 9.3_
  - [x] 12.4 Implement ExtractGasInfo() from CVM transactions
    - Parse OP_RETURN data for gas usage and costs
    - _Requirements: 9.4_
  - [ ]* 12.5 Write property tests for reputation fallback and sender extraction
    - **Property 12: Reputation Fallback Correctness**
    - **Property 13: Sender Address Extraction**
    - **Validates: Requirements 9.1, 9.2**

- [x] 13. Implement Block Validator CVM Activation
  - [x] 13.1 Implement IsCVMActive() check in block_validator.cpp
    - Check Params().GetConsensus().cvmActivationHeight
    - Replace placeholder "always active" with proper check
    - _Requirements: 10.1_
  - [x] 13.2 Implement HAT v2 score expiration validation
    - Check calculatedAtBlock and expiresAtBlock
    - Reject expired scores
    - _Requirements: 10.2_
  - [x] 13.3 Implement transaction fee verification
    - Compare actual fees with expected from gas calculation
    - _Requirements: 10.3_
  - [ ]* 13.4 Write property tests for soft fork activation and HAT expiration
    - **Property 14: Soft Fork Activation Consistency**
    - **Property 15: HAT Score Expiration**
    - **Validates: Requirements 10.1, 10.2**

- [x] 14. Implement Soft Fork Contract Validation
  - [x] 14.1 Implement deployer address extraction in softfork.cpp
    - Extract deployer from transaction inputs
    - Use same logic as sender extraction
    - _Requirements: 11.1_
  - [x] 14.2 Implement deployer reputation check
    - Verify reputation >= 50 (minimum threshold)
    - Reject deployment if threshold not met
    - _Requirements: 11.2_
  - [x] 14.3 Implement contract format verification
    - Check format byte at offset 0 of contract data
    - Verify matches expected format
    - _Requirements: 11.3_
  - [ ]* 14.4 Write property tests for reputation threshold and format verification
    - **Property 16: Reputation Threshold Enforcement**
    - **Property 17: Contract Format Verification**
    - **Validates: Requirements 11.2, 11.3**

- [x] 15. Checkpoint - Ensure consensus validation compiles
  - Verify all consensus and soft fork validation code compiles
  - Run unit tests for validation logic
  - Ask the user if questions arise

- [x] 16. Implement Transaction Builder Contract Support
  - [x] 16.1 Implement ResolveContractP2SH() in txbuilder.cpp
    - Generate P2SH script: OP_HASH160 <Hash160(contractScript)> OP_EQUAL
    - Enable value transfers to contracts
    - _Requirements: 12.1_
  - [x] 16.2 Implement BuildDAOSlashTransaction()
    - Construct transaction spending bond output to slash recipient
    - Require DAO multisig authorization
    - _Requirements: 12.2_
  - [ ]* 16.3 Write property test for contract P2SH resolution
    - **Property 18: Contract P2SH Resolution**
    - **Validates: Requirements 12.1**

- [x] 17. Implement Cross-Chain Trust Verification
  - [x] 17.1 Implement VerifyCrossChainTrust() in trust_context.cpp
    - Verify LayerZero oracle + relayer signatures
    - Verify CCIP Chainlink DON attestations
    - _Requirements: 13.1_
  - [x] 17.2 Implement VerifyReputationMerkleProof() in reputation_signature.cpp
    - Standard binary merkle tree verification
    - Leaf = Hash256(address || reputation || timestamp)
    - _Requirements: 13.2_
  - [ ]* 17.3 Write property test for merkle proof verification
    - **Property 19: Merkle Proof Verification**
    - **Validates: Requirements 13.2**

- [-] 18. Implement Cleanup Manager Resource Reclamation
  - [ ] 18.1 Implement DeleteContractStorage() in cleanup_manager.cpp
    - Iterate keys with prefix "contract_<addr>_storage_"
    - Delete each storage entry via LevelDB batch
    - _Requirements: 14.1_
  - [ ] 18.2 Implement UpdateContractMetadata() for cleanup status
    - Set contract metadata isCleanedUp = true
    - _Requirements: 14.2_
  - [ ] 18.3 Implement ReclaimStorage() database compaction
    - Trigger LevelDB compaction for space reclamation
    - _Requirements: 14.3_
  - [ ]* 18.4 Write property test for cleanup completeness
    - **Property 20: Contract Cleanup Completeness**
    - **Validates: Requirements 14.1, 14.2**

- [ ] 19. Implement Vote Manipulation Detection
  - [ ] 19.1 Fix GetCurrentBlockHeight() in vote_manipulation_detector.cpp
    - Replace placeholder with chainActive.Height()
    - Use proper locking
    - _Requirements: 15.1_
  - [ ] 19.2 Implement PruneReputationHistory() with accurate height
    - Use actual block height for time-based calculations
    - Prune history older than 10,000 blocks
    - _Requirements: 15.2_

- [ ] 20. Implement Eclipse and Sybil Protection
  - [ ] 20.1 Add validation timestamps to ValidatorNetworkInfo
    - Store timestamps in validationTimestamps vector
    - Maintain sliding window of last 100 validations
    - _Requirements: 16.1_
  - [ ] 20.2 Implement DetectCoordinatedAttack() timing analysis
    - Flag patterns where 5+ validators respond within 1 second
    - Report suspicious patterns for DAO review
    - _Requirements: 16.2_
  - [ ]* 20.3 Write property test for timing pattern detection
    - **Property 21: Timing Pattern Detection**
    - **Validates: Requirements 16.1, 16.2**

- [ ] 21. Implement Gas Tracking Integration
  - [ ] 21.1 Fix gas tracking in validator_compensation.cpp
    - Integrate with actual gas tracking system
    - Extract actual gas usage from transactions
    - _Requirements: 8.1, 9.4_

- [ ] 22. Final Checkpoint - Full compilation and test suite
  - Ensure all CVM code compiles without errors
  - Run full unit test suite
  - Run property-based tests (minimum 100 iterations each)
  - Ask the user if questions arise

- [ ] 23. Write Integration Tests
  - [ ] 23.1 Write P2P validator communication integration test
    - Test message flow between nodes
    - _Requirements: 3.1, 3.2, 3.3, 3.4_
  - [ ] 23.2 Write end-to-end validation cycle test
    - Test complete validation from task creation to response
    - _Requirements: 1.1, 1.2, 3.1, 3.2_
  - [ ] 23.3 Write soft fork activation integration test
    - Test CVM activation at correct height
    - _Requirements: 10.1_
  - [ ] 23.4 Write DAO dispute flow integration test
    - Test dispute escalation and notification
    - _Requirements: 6.1, 6.2, 6.3_

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties
- Unit tests validate specific examples and edge cases
- All property tests should use Boost.Test framework with minimum 100 iterations
- Test file location: `src/test/cvm_todo_tests.cpp`

## Build Instructions

To build the project on Ubuntu/Debian, follow the instructions in `doc/build-linux.md`:

```bash
# Install prerequisites (see doc/build-linux.md for full list)
# Then build:
./autogen.sh

# Configure with full options for Ubuntu
./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
  --enable-evmc \
  --with-evmc=/usr/local \
  --with-evmone=/usr/local \
  --with-incompatible-bdb \
  MOC=/usr/lib/qt6/libexec/moc \
  UIC=/usr/lib/qt6/libexec/uic \
  RCC=/usr/lib/qt6/libexec/rcc \
  LRELEASE=$(command -v lrelease || command -v lrelease-qt6 || echo /usr/lib/qt6/libexec/lrelease) \
  LUPDATE=$(command -v lupdate || command -v lupdate-qt6 || echo /usr/lib/qt6/libexec/lupdate)

make -j$(nproc)
```

For minimal builds without GUI/EVMC (faster compilation):
```bash
./configure --with-gui=no --disable-tests --disable-bench --disable-evmc --with-incompatible-bdb
make -j$(nproc)
```
