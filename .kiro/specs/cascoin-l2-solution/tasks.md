# Implementation Plan: Cascoin L2 Solution

## Overview

Dieser Implementation Plan beschreibt die schrittweise Entwicklung der Cascoin L2 Lösung. Die Implementierung erfolgt in Phasen, beginnend mit der Kerninfrastruktur und aufbauend zu komplexeren Features. Alle Tests werden im Regtest-Modus durchgeführt.

## Build Instructions

Für Build-Anweisungen siehe [doc/build-linux.md](../../../doc/build-linux.md).

**Empfohlene Build-Methode (System Libraries):**
```bash
./autogen.sh
./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
  --with-incompatible-bdb \
  MOC=/usr/lib/qt6/libexec/moc \
  UIC=/usr/lib/qt6/libexec/uic \
  RCC=/usr/lib/qt6/libexec/rcc \
  LRELEASE=$(command -v lrelease || command -v lrelease-qt6 || echo /usr/lib/qt6/libexec/lrelease) \
  LUPDATE=$(command -v lupdate || command -v lupdate-qt6 || echo /usr/lib/qt6/libexec/lupdate)
make -j$(nproc)
```

**Tests ausführen:**
```bash
make check -j$(nproc)
```

**L2 Tests parallel ausführen (schneller, mit Fortschrittsanzeige):**
```bash
# Benötigt GNU parallel: sudo apt install parallel
src/test/run_l2_tests_parallel.sh

# Mit benutzerdefinierter Anzahl paralleler Jobs:
src/test/run_l2_tests_parallel.sh 8
```

Das Skript zeigt:
- Fortschrittsbalken mit ETA
- Einzelne Testzeiten pro Test-Suite
- Zusammenfassung mit Pass/Fail-Status
- Top 5 langsamste Tests

## Tasks

- [x] 1. Project Setup und Core Infrastructure
  - [x] 1.1 Erstelle L2 Verzeichnisstruktur in `src/l2/`
    - Erstelle Verzeichnis `src/l2/` mit Unterordnern für components
    - Erstelle `src/l2/Makefile.am` für Build-Integration
    - Aktualisiere `src/Makefile.am` um L2 einzubinden
    - _Requirements: 1.1, 11.1_

  - [x] 1.2 Implementiere L2 Chainparams
    - Erweitere `CChainParams` um L2-spezifische Parameter
    - Füge Regtest L2 Parameter hinzu (niedrige Schwellwerte)
    - Füge Mainnet/Testnet L2 Parameter hinzu
    - _Requirements: 1.2, 11.2_

  - [x] 1.3 Implementiere L2 Konfigurationsoptionen
    - Füge `-l2`, `-nol2`, `-l2chainid` CLI Optionen hinzu
    - Implementiere L2 Konfiguration in `init.cpp`
    - Füge L2 Optionen zu `cascoin.conf` hinzu
    - _Requirements: 11.2, 11.3_

- [x] 2. Sparse Merkle Tree Implementation
  - [x] 2.1 Implementiere SparseMerkleTree Klasse (`src/l2/sparse_merkle_tree.h/cpp`)
    - Implementiere 256-bit Sparse Merkle Tree
    - Implementiere Get/Set/Delete Operationen
    - Implementiere GetRoot() Funktion
    - _Requirements: 37.1, 3.1_

  - [x] 2.2 Implementiere Merkle Proof Generation
    - Implementiere GenerateInclusionProof()
    - Implementiere GenerateExclusionProof()
    - Implementiere statische VerifyProof() Funktion
    - _Requirements: 37.2, 37.3, 37.5_

  - [x] 2.3 Write property test for Merkle Proof Round-Trip
    - **Property 8: Merkle Proof Verification**
    - **Validates: Requirements 37.2, 37.3, 37.4**

- [x] 3. State Management
  - [x] 3.1 Implementiere AccountState Struktur (`src/l2/account_state.h`)
    - Definiere AccountState mit balance, nonce, codeHash, storageRoot
    - Füge hatScore und lastActivity Felder hinzu
    - Implementiere Serialisierung/Deserialisierung
    - _Requirements: 10.1, 20.1_

  - [x] 3.2 Implementiere L2StateManager Klasse (`src/l2/state_manager.h/cpp`)
    - Implementiere GetStateRoot(), GetAccountState()
    - Implementiere ApplyTransaction(), ApplyBatch()
    - Implementiere RevertToStateRoot()
    - _Requirements: 3.1, 19.2_

  - [x] 3.3 Write property test for State Root Consistency
    - **Property 1: State Root Consistency**
    - **Validates: Requirements 3.1, 5.2, 19.2**

  - [x] 3.4 Implementiere State Rent und Archivierung
    - Implementiere ProcessStateRent()
    - Implementiere ArchiveInactiveState()
    - Füge State Restoration hinzu
    - _Requirements: 20.1, 20.2, 20.3_

- [x] 4. Checkpoint - State Management Tests
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Sequencer Discovery und Management
  - [x] 5.1 Implementiere SeqAnnounceMsg Struktur (`src/l2/sequencer_discovery.h`)
    - Definiere Nachrichtenformat für Sequencer-Ankündigung
    - Implementiere Serialisierung für P2P
    - Füge Signatur-Validierung hinzu
    - _Requirements: 2.5, 2.1_

  - [x] 5.2 Implementiere SequencerDiscovery Klasse (`src/l2/sequencer_discovery.cpp`)
    - Implementiere AnnounceAsSequencer()
    - Implementiere ProcessSeqAnnounce()
    - Implementiere GetEligibleSequencers()
    - _Requirements: 2.1, 2.2, 2.3_

  - [x] 5.3 Implementiere Sequencer Eligibility Verification
    - Implementiere VerifySequencerEligibility() mit Attestation
    - Implementiere VerifyStakeOnL1()
    - Integriere HAT v2 Score Prüfung
    - _Requirements: 2.3, 2.4_

  - [x] 5.4 Write property test for Sequencer Eligibility
    - **Property: Sequencer Eligibility Determinism**
    - **Validates: Requirements 2.3, 2.4**

- [x] 6. Leader Election
  - [x] 6.1 Implementiere LeaderElection Klasse (`src/l2/leader_election.h/cpp`)
    - Implementiere ElectLeader() mit deterministischem Algorithmus
    - Implementiere GenerateElectionSeed()
    - Implementiere WeightedRandomSelect()
    - _Requirements: 2a.1, 2a.2_

  - [x] 6.2 Write property test for Leader Election Determinism
    - **Property 2: Sequencer Election Determinism**
    - **Validates: Requirements 2a.1, 2a.2**

  - [x] 6.3 Implementiere Failover Management
    - Implementiere HandleLeaderTimeout()
    - Implementiere ClaimLeadership()
    - Implementiere Split-Brain Prevention
    - _Requirements: 2b.1, 2b.2, 2b.5_

  - [x] 6.4 Write property test for Failover Consistency
    - **Property 3: Failover Consistency**
    - **Validates: Requirements 2b.2, 2b.3, 2b.5**

- [x] 7. Sequencer Consensus Protocol
  - [x] 7.1 Implementiere L2BlockProposal und SequencerVote Strukturen
    - Definiere Block Proposal Format
    - Definiere Vote Format mit ACCEPT/REJECT/ABSTAIN
    - Implementiere Signatur-Handling
    - _Requirements: 2a.4, 2a.5_

  - [x] 7.2 Implementiere SequencerConsensus Klasse (`src/l2/sequencer_consensus.h/cpp`)
    - Implementiere ProposeBlock() (Leader only)
    - Implementiere VoteOnProposal()
    - Implementiere ProcessVote()
    - _Requirements: 2a.3, 2a.4, 2a.5_

  - [x] 7.3 Implementiere Consensus Determination
    - Implementiere HasConsensus() mit 2/3 Threshold
    - Implementiere CalculateWeightedVotes()
    - Implementiere HandleConsensusFailed()
    - _Requirements: 2a.5, 2a.6_

  - [x] 7.4 Write property test for Consensus Threshold Safety
    - **Property 11: Consensus Threshold Safety**
    - **Validates: Requirements 2a.5, 22.1**

- [x] 8. Checkpoint - Sequencer Network Tests
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. L2 Block und Transaction Strukturen
  - [x] 9.1 Implementiere L2Block Struktur (`src/l2/l2_block.h`)
    - Definiere Block Header mit stateRoot, transactionsRoot
    - Füge Sequencer Signatures hinzu
    - Implementiere GetHash() und ValidateStructure()
    - _Requirements: 3.1, 3.5_

  - [x] 9.2 Implementiere L2Transaction Struktur (`src/l2/l2_transaction.h`)
    - Erweitere CTransaction für L2-spezifische Felder
    - Füge L2TxType enum hinzu
    - Implementiere Encrypted Transaction Support
    - _Requirements: 8.1, 16.1_

  - [x] 9.3 Implementiere L2 Block Validation
    - Implementiere Block Header Validation
    - Implementiere Transaction Validation
    - Implementiere Signature Verification
    - _Requirements: 3.1, 2a.5_

- [x] 10. Encrypted Mempool (MEV Protection)
  - [x] 10.1 Implementiere EncryptedTransaction Struktur
    - Definiere verschlüsseltes Transaktionsformat
    - Implementiere Commitment Hash Berechnung
    - Füge Rate Limiting Felder hinzu
    - _Requirements: 16.1, 26.2_

  - [x] 10.2 Implementiere EncryptedMempool Klasse (`src/l2/encrypted_mempool.h/cpp`)
    - Implementiere SubmitEncryptedTx()
    - Implementiere GetTransactionsForBlock()
    - Implementiere RandomizeOrdering()
    - _Requirements: 16.1, 16.3_

  - [x] 10.3 Implementiere Threshold Decryption
    - Implementiere ContributeDecryptionShare()
    - Implementiere ThresholdDecrypt() mit Shamir's Secret Sharing
    - Implementiere CanDecrypt() Check
    - _Requirements: 16.2_

  - [x] 10.4 Write property test for MEV Protection Round-Trip
    - **Property 7: MEV Protection Round-Trip**
    - **Validates: Requirements 16.1, 16.2**

- [x] 11. Bridge Contract
  - [x] 11.1 Implementiere Deposit/Withdrawal Strukturen (`src/l2/bridge_contract.h`)
    - Definiere DepositEvent Struktur
    - Definiere WithdrawalRequest Struktur
    - Definiere WithdrawalStatus enum
    - _Requirements: 4.1, 4.2_

  - [x] 11.2 Implementiere BridgeContract Klasse (`src/l2/bridge_contract.cpp`)
    - Implementiere ProcessDeposit()
    - Implementiere InitiateWithdrawal()
    - Implementiere FinalizeWithdrawal()
    - _Requirements: 4.1, 4.2, 4.3_

  - [x] 11.3 Implementiere Fast Withdrawal für High-Reputation Users
    - Implementiere FastWithdrawal()
    - Implementiere CalculateChallengePeriod() basierend auf Reputation
    - Integriere HAT v2 Score Prüfung
    - _Requirements: 4.4, 6.2_

  - [x] 11.4 Write property test for Deposit-Withdrawal Balance
    - **Property 4: Deposit-Withdrawal Balance**
    - **Validates: Requirements 4.1, 4.2, 4.5**

  - [x] 11.5 Implementiere Emergency Withdrawal
    - Implementiere EmergencyWithdrawal()
    - Implementiere Balance Proof Verification
    - Füge Emergency Mode Detection hinzu
    - _Requirements: 12.1, 12.2, 12.3_

  - [x] 11.6 Write property test for Emergency Exit Completeness
    - **Property 12: Emergency Exit Completeness**
    - **Validates: Requirements 12.1, 12.2, 12.3**

- [x] 12. Checkpoint - Bridge Contract Tests
  - Ensure all tests pass, ask the user if questions arise.

- [x] 13. Fraud Proof System
  - [x] 13.1 Implementiere FraudProof Strukturen (`src/l2/fraud_proof.h`)
    - Definiere FraudProofType enum
    - Definiere FraudProof Struktur mit Evidence
    - Definiere InteractiveFraudProofStep
    - _Requirements: 5.1, 5.6_

  - [x] 13.2 Implementiere FraudProofSystem Klasse (`src/l2/fraud_proof.cpp`)
    - Implementiere SubmitFraudProof()
    - Implementiere VerifyFraudProof()
    - Implementiere ReExecuteOnL1()
    - _Requirements: 5.1, 5.2_

  - [x] 13.3 Write property test for Fraud Proof Soundness
    - **Property 5: Fraud Proof Soundness**
    - **Validates: Requirements 5.2, 5.3**

  - [x] 13.4 Implementiere Interactive Fraud Proofs
    - Implementiere StartInteractiveProof()
    - Implementiere SubmitInteractiveStep()
    - Implementiere BinarySearchInvalidStep()
    - _Requirements: 5.6_

  - [x] 13.5 Implementiere Slashing und Rewards
    - Implementiere SlashSequencer()
    - Implementiere RewardChallenger()
    - Integriere mit Reputation System
    - _Requirements: 5.4, 5.5_

  - [x] 13.6 Write property test for Sequencer Stake Slashing
    - **Property 17: Sequencer Stake Slashing**
    - **Validates: Requirements 5.4, 16.6**

- [x] 14. Data Availability Layer
  - [x] 14.1 Implementiere BatchData Struktur (`src/l2/data_availability.h`)
    - Definiere Batch Format für L1 Submission
    - Definiere DACommitment für Sampling
    - Implementiere Serialisierung
    - _Requirements: 3.2, 3.4, 7.1_

  - [x] 14.2 Implementiere DataAvailabilityLayer Klasse (`src/l2/data_availability.cpp`)
    - Implementiere PublishBatch()
    - Implementiere CompressTransactions() mit zstd
    - Implementiere DecompressTransactions()
    - _Requirements: 7.1, 7.5_

  - [x] 14.3 Implementiere DA Sampling
    - Implementiere GenerateDACommitment()
    - Implementiere ErasureEncode()
    - Implementiere SampleDataAvailability()
    - _Requirements: 7.2, 24.4_

  - [x] 14.4 Write property test for Data Availability Reconstruction
    - **Property 13: Data Availability Reconstruction**
    - **Validates: Requirements 7.3, 11.6, 41.2**
    - **Status: PASSED** - All DA tests pass including compression round-trip, erasure coding, reconstruction with missing shard, DA commitment verification, batch round-trip, and DA sampling confidence

- [x] 15. Cross-Layer Messaging
  - [x] 15.1 Implementiere Message Strukturen (`src/l2/cross_layer_messaging.h`)
    - Definiere L1ToL2Message Struktur
    - Definiere L2ToL1Message Struktur
    - Definiere MessageStatus enum
    - _Requirements: 9.1, 9.2_

  - [x] 15.2 Implementiere CrossLayerMessaging Klasse (`src/l2/cross_layer_messaging.cpp`)
    - Implementiere SendL1ToL2()
    - Implementiere ProcessL1ToL2Message()
    - Implementiere SendL2ToL1()
    - _Requirements: 9.1, 9.2, 9.3_

  - [x] 15.3 Implementiere Reentrancy Protection
    - Implementiere ExecuteMessageSafe() mit Mutex
    - Implementiere Message Queuing für nächsten Block
    - Füge Reentrancy Guards hinzu
    - _Requirements: 28.1, 28.2, 28.4_

  - [x] 15.4 Write property test for Cross-Layer Message Integrity
    - **Property 9: Cross-Layer Message Integrity**
    - **Validates: Requirements 9.1, 9.2, 9.4**

- [x] 16. Checkpoint - Core L2 Functionality Tests
  - All L2 test suites pass (13/13 test suites, all tests passing)


- [x] 17. Reputation Integration
  - [x] 17.1 Implementiere L2ReputationData Struktur (`src/l2/l2_reputation.h`)
    - Definiere L2-spezifische Reputation Felder
    - Definiere ReputationBenefits Struktur
    - Implementiere Serialisierung
    - _Requirements: 10.1, 10.2_

  - [x] 17.2 Implementiere L2ReputationManager Klasse (`src/l2/l2_reputation.cpp`)
    - Implementiere ImportL1Reputation()
    - Implementiere GetAggregatedReputation()
    - Implementiere UpdateL2Reputation()
    - _Requirements: 10.1, 10.3, 10.4_

  - [x] 17.3 Write property test for Reputation Aggregation Consistency
    - **Property 10: Reputation Aggregation Consistency**
    - **Validates: Requirements 10.3, 10.5**

  - [x] 17.4 Implementiere Reputation-Based Benefits
    - Implementiere GetBenefits()
    - Implementiere QualifiesForFastWithdrawal()
    - Implementiere GetGasDiscount()
    - _Requirements: 6.1, 6.2, 18.5_

- [x] 18. L2 RPC Interface
  - [x] 18.1 Implementiere L2 RPC Commands (`src/rpc/l2.cpp`)
    - Implementiere `l2_getBalance`
    - Implementiere `l2_getTransactionCount`
    - Implementiere `l2_getBlockByNumber`
    - _Requirements: 11.7, 40.1_

  - [x] 18.2 Implementiere L2 Deployment RPC
    - Implementiere `l2_deploy`
    - Implementiere `l2_getChainInfo`
    - Implementiere `l2_listChains`
    - _Requirements: 1.1, 1.5_

  - [x] 18.3 Implementiere Sequencer RPC
    - Implementiere `l2_announceSequencer`
    - Implementiere `l2_getSequencers`
    - Implementiere `l2_getLeader`
    - _Requirements: 2.5, 2.6_

  - [x] 18.4 Implementiere Bridge RPC
    - Implementiere `l2_deposit`
    - Implementiere `l2_withdraw`
    - Implementiere `l2_getWithdrawalStatus`
    - _Requirements: 4.1, 4.2_

- [x] 19. L2 P2P Network Integration
  - [x] 19.1 Erweitere P2P Message Types (`src/protocol.h`)
    - Füge `SEQANNOUNCE` Message Type hinzu
    - Füge `L2BLOCK` Message Type hinzu
    - Füge `L2VOTE` Message Type hinzu
    - _Requirements: 2.5, 2a.3, 11.8_

  - [x] 19.2 Implementiere L2 Message Handler (`src/net_processing.cpp`)
    - Implementiere ProcessSeqAnnounce Handler
    - Implementiere ProcessL2Block Handler
    - Implementiere ProcessL2Vote Handler
    - _Requirements: 2.2, 2a.4_

  - [x] 19.3 Implementiere L2 Peer Management
    - Erweitere Peer Info um L2 Capabilities
    - Implementiere L2-spezifisches Peer Scoring
    - Füge L2 Sync Logic hinzu
    - _Requirements: 11.5, 11.6_

- [x] 20. Checkpoint - RPC und P2P Tests
  - All L2 test suites pass (13/13)
  - DA sampling issue fixed (simplified Merkle proof verification)
  - RPC commands implemented in `src/rpc/l2.cpp`
  - P2P peer management implemented in `src/l2/l2_peer_manager.cpp`

- [x] 21. L2 Chain Registry auf L1
  - [x] 21.1 Implementiere L2Registry CVM Contract
    - Erstelle Contract für L2 Chain Registration
    - Implementiere registerL2Chain() Funktion
    - Implementiere getL2ChainInfo() Funktion
    - _Requirements: 1.1, 1.5_

  - [x] 21.2 Implementiere L2 Deployment Validation
    - Validiere Deployment Parameter
    - Prüfe Deployer Stake
    - Generiere unique Chain ID
    - _Requirements: 1.2, 1.3, 1.4_

- [x] 22. Rate Limiting und Spam Protection
  - [x] 22.1 Implementiere Rate Limiter (`src/l2/rate_limiter.h/cpp`)
    - Implementiere per-Address Rate Limiting
    - Implementiere Reputation-basierte Limits
    - Implementiere Adaptive Gas Pricing
    - _Requirements: 26.2, 26.3, 26.5_

  - [x] 22.2 Write property test for Rate Limit Enforcement
    - **Property 14: Rate Limit Enforcement**
    - **Validates: Requirements 26.2, 26.3**

- [x] 23. Timestamp Validation
  - [x] 23.1 Implementiere Timestamp Validator (`src/l2/timestamp_validator.h/cpp`)
    - Implementiere L1 Timestamp Binding
    - Implementiere Monotonicity Check
    - Implementiere Future Timestamp Rejection
    - _Requirements: 27.1, 27.2, 27.3_

  - [x] 23.2 Write property test for Timestamp Monotonicity
    - **Property 15: Timestamp Monotonicity**
    - **Validates: Requirements 27.2, 27.3**

- [x] 24. Challenge System
  - [x] 24.1 Implementiere Challenge Handler (`src/l2/challenge_handler.h/cpp`)
    - Implementiere ChallengeWithdrawal()
    - Implementiere ValidateChallenge()
    - Implementiere ProcessChallengeResult()
    - _Requirements: 4.6, 29.1, 29.2_

  - [x] 24.2 Write property test for Challenge Bond Handling
    - **Property 16: Challenge Bond Slashing**
    - **Validates: Requirements 29.1, 29.2**

- [x] 25. Forced Transaction Inclusion
  - [x] 25.1 Implementiere Forced Inclusion System (`src/l2/forced_inclusion.h/cpp`)
    - Implementiere L1 Transaction Submission
    - Implementiere Inclusion Tracking
    - Implementiere Sequencer Slashing bei Ignorieren
    - _Requirements: 17.1, 17.2, 17.3_

  - [x] 25.2 Write property test for Forced Inclusion Guarantee
    - **Property 19: Forced Inclusion Guarantee**
    - **Validates: Requirements 17.2, 17.3**

- [x] 26. Gas Fee Distribution
  - [x] 26.1 Implementiere Fee Distributor (`src/l2/fee_distributor.h/cpp`)
    - Implementiere 70/20/10 Fee Split
    - Implementiere Sequencer Reward Tracking
    - Implementiere Fee Burning
    - _Requirements: 18.2, 38.2_

  - [x] 26.2 Write property test for Gas Fee Distribution
    - **Property 18: Gas Fee Distribution**
    - **Validates: Requirements 18.2, 38.2**

- [x] 27. Checkpoint - Advanced Features Tests
  - Ensure all tests pass, ask the user if questions arise.

- [x] 28. L1 Reorg Handling
  - [x] 28.1 Implementiere Reorg Monitor (`src/l2/reorg_monitor.h/cpp`)
    - Implementiere L1 Reorg Detection
    - Implementiere State Revert Logic
    - Implementiere Transaction Replay
    - _Requirements: 19.1, 19.2, 19.3_

  - [x] 28.2 Write property test for L1 Reorg Recovery
    - **Property 20: L1 Reorg Recovery**
    - **Validates: Requirements 19.2, 19.3**

- [x] 29. Anti-Collusion Detection
  - [x] 29.1 Implementiere Collusion Detector (`src/l2/collusion_detector.h/cpp`)
    - Implementiere Timing Correlation Detection
    - Implementiere Voting Pattern Analysis
    - Implementiere Wallet Cluster Integration
    - _Requirements: 22.1, 22.2, 22.4_

- [x] 30. Security Monitoring
  - [x] 30.1 Implementiere Security Monitor (`src/l2/security_monitor.h/cpp`)
    - Implementiere Anomaly Detection
    - Implementiere Alert System
    - Implementiere Audit Logging
    - _Requirements: 33.1, 33.2, 33.6_

  - [x] 30.2 Implementiere Circuit Breaker
    - Implementiere TVL Monitoring
    - Implementiere Automatic Pause bei Anomalien
    - Implementiere Recovery Procedures
    - _Requirements: 36.6, 33.5_

- [x] 31. GUI und Web Dashboard Integration
  - [x] 31.1 Erweitere cascoin-qt für L2 (`src/qt/`)
    - Füge L2 Balance Anzeige hinzu
    - Füge L2 Transaction History hinzu
    - Füge Deposit/Withdraw UI hinzu
    - _Requirements: 40.3_

  - [x] 31.2 Implementiere L2 Web Dashboard (`src/httpserver/l2_dashboard.h/cpp`)
    - Erstelle HTTP Endpoints für Dashboard Daten
    - Implementiere `/l2/status` - Chain Status und Health
    - Implementiere `/l2/sequencers` - Sequencer Liste und Performance
    - Implementiere `/l2/blocks` - Block Explorer Daten
    - _Requirements: 25.4, 39.1_

  - [x] 31.3 Erstelle Web Dashboard Frontend (`src/httpserver/l2_dashboard_html.h`)
    - Erstelle eingebettetes HTML/CSS/JS Dashboard
    - Zeige L2 Chain Status (Blocks, TPS, Gas)
    - Zeige Sequencer Status (Leader, Uptime, Reputation)
    - Zeige Bridge Status (TVL, Pending Withdrawals)
    - Zeige Recent Transactions und Blocks
    - _Requirements: 33.8, 39.1_

  - [x] 31.4 Implementiere Dashboard API Endpoints
    - Implementiere `/l2/api/stats` - Statistiken JSON
    - Implementiere `/l2/api/sequencers` - Sequencer JSON
    - Implementiere `/l2/api/transactions` - Recent Transactions
    - Implementiere `/l2/api/withdrawals` - Pending Withdrawals
    - _Requirements: 39.2, 39.3_

  - [x] 31.5 Implementiere Dashboard WebSocket für Live Updates
    - Implementiere WebSocket Server für Real-Time Updates
    - Pushe neue Blocks automatisch
    - Pushe Sequencer Status Changes
    - Pushe Security Alerts
    - _Requirements: 33.1, 25.4_

- [x] 32. Documentation
  - [x] 32.1 Erstelle L2 Operator Guide (`doc/L2_OPERATOR_GUIDE.md`)
    - Dokumentiere Node Setup
    - Dokumentiere Sequencer Operation
    - Dokumentiere Troubleshooting
    - _Requirements: 11.1, 30.1_

  - [x] 32.2 Erstelle L2 Developer Guide (`doc/L2_DEVELOPER_GUIDE.md`)
    - Dokumentiere RPC API
    - Dokumentiere Contract Deployment
    - Dokumentiere Cross-Layer Messaging
    - _Requirements: 40.1, 9.3_

- [ ] 33. Final Checkpoint - Full Integration Tests
  - Ensure all tests pass, ask the user if questions arise.
  - Run full Regtest integration test suite
  - Verify all 20 correctness properties pass
  - Conduct manual testing of all RPC commands

## Notes

- All tasks including property-based tests are required for comprehensive coverage
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- All tests run in Regtest mode with reduced parameters
- Property tests validate universal correctness properties (20 total)
- Unit tests validate specific examples and edge cases
- **Build mit allen CPU-Kernen:** Verwende `make -j$(nproc)` für maximale Kompiliergeschwindigkeit
- **System Libraries:** Verwende Host-Bibliotheken (siehe [doc/build-linux.md](../../../doc/build-linux.md)) statt des depends-Systems für schnellere Entwicklung
