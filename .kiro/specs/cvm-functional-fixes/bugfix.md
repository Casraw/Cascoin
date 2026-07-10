# Bugfix Requirements Document

## Introduction

An audit of the Cascoin CVM subsystem (`src/cvm/`) found several areas where the
code compiles and runs but does not perform the work it claims to. These are
non-functional placeholders, stubbed-out validation paths, and hardcoded values
left in place of real logic. The most serious cases are consensus-relevant:
block-level subsidy limits are never enforced, gas cost is computed with a fixed
1:1 rate, validator attestation blindly votes "valid", and reputation state
proofs are signed with a fabricated signature. The remaining cases weaken
Sybil-resistance, fraud detection, and EVM compatibility.

A subsequent complete, file-by-file sweep of the entire `src/cvm/` directory
(100+ source and header files) extended this inventory considerably. The sweep
confirmed additional consensus-critical gaps (a block-processing path that
derives contract addresses from the raw transaction hash, never runs contract
constructors or call code, and applies reputation votes to an empty address),
a core-VM signature-verification bypass (the `OP_VERIFY_SIG` family always
returns true), and many more stubbed or hardcoded paths across EVM
compatibility, cross-chain bridging, fee/gas/subsidy accounting,
Sybil/fraud detection, and storage/state synchronization. All of these
additional findings were verified by reading the actual source, and each is
recorded below with its file and function.

This bugfix effort captures the defective behaviors, the correct behavior each
should exhibit, and the surrounding behavior that must continue to work
unchanged. The goal is to make the flagged code paths actually functional
without regressing the parts of the CVM that already work (deterministic
validator selection, ECDSA signing/verification of validator responses,
persistent contract storage, and standard transactions).

The bug condition for this effort is: an input that exercises any of the flagged
CVM code paths documented in the Bug Analysis below (clauses 1.1–1.62). These
paths span, by category: consensus-critical accounting, validation, and block
processing (per-block subsidy limits, gas cost extraction, validator attestation,
cvmtx block processing, block_validator save/rollback, and validator
compensation); core-VM opcode handlers (signature verification, balance, call,
and log); reputation signatures and merkle proofs; HAT v2 distributed consensus;
fee/gas/subsidy accounting; EVM compatibility (transient storage, base fee,
CREATE address derivation, logsBloom, sender extraction, storage proofs,
primary-path value and block-hash handling, and state commit); cross-chain
bridging and oracle trust; distributed-consensus signatures and state sync;
Sybil-resistance and fraud detection; storage/state synchronization; the
security-monitoring RPC; and the graceful-degradation resource and health checks.
Non-buggy inputs are those that do not reach these paths and must behave
identically before and after the fix.

## Bug Analysis

### Current Behavior (Defect)

Consensus-critical accounting and validation:

1.1 WHEN a block contains CVM/EVM transactions that carry gas subsidies THEN the system counts total subsidies as zero (the accumulation loop body is a TODO) and never enforces the per-block subsidy maximum.

1.2 WHEN gas cost is extracted from a contract deploy or call transaction THEN the system sets gas cost equal to gas limit (a fixed 1 satoshi-per-gas rate) and ignores the actual gas price.

1.3 WHEN a validator node is selected for a validation task THEN the system generates a response that unconditionally reports `isValid = true` with a fixed 80% confidence, without ever validating the task.

1.4 WHEN a validator generates a validation response THEN the system sets the reported trust score to a hardcoded value of 50 instead of computing it from the trust graph.

Reputation signatures and proofs:

1.5 WHEN a reputation state proof is created THEN the system fills the signature field with the first 32 bytes of the proof hash (a placeholder) rather than a validator-produced signature, and computes the state root from a fixed string plus the current time rather than from the reputation state tree.

1.6 WHEN a reputation signature is verified THEN the system only checks that the signature is non-empty and at least 64 bytes, and does not perform any ECDSA verification against a public key, so a forged signature of sufficient length passes.

1.7 WHEN a reputation merkle proof is built for an address THEN the system fabricates a deterministic sibling hash instead of querying the real reputation state tree, so the proof does not attest to any committed state.

HAT v2 distributed consensus:

1.8 WHEN the HAT consensus validator sends a validation challenge to a selected validator THEN the system returns success without transmitting any P2P message, so no distributed validation actually occurs.

1.9 WHEN a HAT consensus dispute is created THEN the system sets the self-reported score equal to the independently calculated score, so the fraud check comparing self-reported and calculated scores can never detect a discrepancy.

Fee/subsidy handling and EVM compatibility:

1.10 WHEN a mempool transaction is not eligible for free gas THEN the system skips applicable gas subsidies entirely (the subsidy check is a TODO) and only applies a reputation-discounted normal fee.

1.11 WHEN contract bytecode executes the EVM transient storage opcodes (TLOAD/TSTORE) THEN the system has no transient storage handlers registered (they are null), so transient storage operations are non-functional.

1.12 WHEN contract bytecode reads the EIP-1559 base fee (BASEFEE opcode) THEN the system returns zero because the base fee is never populated.

Sybil-resistance, fraud detection, and health checks:

1.13 WHEN wallet cluster detection runs (transaction lookup by address and common-input-ownership heuristic) THEN the system returns empty results / false, so clustering-based Sybil resistance is non-functional.

1.14 WHEN rapid-fire abuse detection is invoked for an address THEN the system always returns false regardless of transaction history.

1.15 WHEN the graceful-degradation layer performs a reputation query or health check THEN the system returns a simulated placeholder success instead of querying the real reputation subsystem.

Additional consensus-critical block processing (`cvmtx.cpp`, `block_validator.cpp`, `validator_compensation.cpp`):

1.16 WHEN a contract deploy transaction is processed in `cvmtx.cpp` `ProcessCVMBlock` THEN the system derives the contract address from the first 20 bytes of the transaction hash (`memcpy(contractAddr.begin(), txHash.begin(), 20)`) instead of from the deployer address and nonce, producing addresses that do not match the documented deployer+nonce scheme.

1.17 WHEN a contract deploy or call transaction is processed in `cvmtx.cpp` `ProcessCVMBlock` THEN the system only accumulates `gasLimit` into `totalGasUsed` and never executes the constructor (deploy) or the contract code (call), so contract execution during block processing is a no-op.

1.18 WHEN reputation votes are applied in `cvmtx.cpp` `UpdateReputationScores` THEN the system applies each vote using a default-constructed (empty) `voterAddr` and never updates per-participant behavior scores, so votes are attributed to the zero address.

1.19 WHEN `block_validator.cpp` records a gas subsidy during block validation THEN the system hardcodes `isBeneficial = true` and records `gasUsed = gasLimit` instead of the actual benefit determination and actual gas used.

1.20 WHEN `block_validator.cpp` verifies contract-transaction fees THEN the system does not verify the fee against the transaction's input values and only checks internal gas-limit/gas-price consistency.

1.21 WHEN `block_validator.cpp` saves or rolls back contract state for a block THEN the system assumes state was already persisted during execution and performs the rollback as a log-only no-op, so failed blocks are not actually reverted.

1.22 WHEN `validator_compensation.cpp` `CheckCoinbaseValidatorPayments` validates the coinbase THEN the system does not enforce the 70/30 validator payment split (the strict validation is a TODO) and returns success.

Core VM opcode handlers (`cvm.cpp`):

1.23 WHEN the CVM executes `OP_VERIFY_SIG`, `OP_VERIFY_SIG_ECDSA`, or `OP_VERIFY_SIG_QUANTUM` THEN the system sets `verifyResult = true` unconditionally without performing any secp256k1 or FALCON verification, so any signature is accepted.

1.24 WHEN the CVM executes `OP_BALANCE` THEN the system pushes 0 regardless of the account's actual balance.

1.25 WHEN the CVM executes `OP_CALL` (`HandleCall`) or `CallContract` THEN the system sets an error "CALL not fully implemented" and returns false, so contract-to-contract calls are non-functional.

1.26 WHEN the CVM executes `OP_LOG` THEN the system returns true without consuming the topic count, topics, or data, so event logging from CVM bytecode is a no-op.

EVM compatibility (`nonce_manager.cpp`, `receipt.cpp`, `evm_rpc.cpp`, `evm_engine.cpp`, `enhanced_vm.cpp`, `enhanced_storage.cpp`):

1.27 WHEN `nonce_manager.cpp` `GenerateContractAddress` computes a CREATE address THEN the system uses `Hash160(sender ++ nonce)` instead of `keccak256(rlp([sender, nonce]))[12:]`, so the address is not Ethereum-compatible.

1.28 WHEN `receipt.cpp` `TransactionReceipt::ToJSON` emits `logsBloom` THEN the system always returns 512 zero hex characters instead of a bloom filter computed from the receipt's logs.

1.29 WHEN `evm_rpc.cpp` extracts the sender for an EVM transaction THEN the system returns an empty `uint160`, and gas estimation adds fixed constants rather than estimating actual execution gas.

1.30 WHEN `evm_engine.cpp` executes an EVM message THEN the system does not inject caller reputation into the message structure and performs only basic reputation-based trust-tagged memory access validation.

1.31 WHEN `enhanced_vm.cpp` saves execution state for a nested frame THEN the system pushes an unpopulated placeholder `ExecutionFrame`, so saved execution state does not capture the real context.

1.32 WHEN `enhanced_storage.cpp` generates or verifies a storage proof THEN the system builds a basic hash structure instead of a Merkle Patricia Trie proof, so the proof does not attest to committed storage state.

Cross-chain bridging and oracle trust (`cross_chain_bridge.cpp`, `trust_context.cpp`):

1.33 WHEN `cross_chain_bridge.cpp` `ReputationProof::Verify` validates an inbound proof THEN the system only checks that the proof bytes are non-empty and does not verify the proof against the source chain.

1.34 WHEN `cross_chain_bridge.cpp` sends an attestation via LayerZero or a reputation proof via CCIP THEN the system only logs the intent and stores locally, without transmitting any cross-chain message.

1.35 WHEN `cross_chain_bridge.cpp` generates a merkle proof or reads trust attestations THEN the system produces a simplified hash-based proof and returns only cached attestations rather than iterating committed state.

1.36 WHEN `trust_context.cpp` `IsKnownLayerZeroOracle` checks an oracle public key THEN the system accepts any valid public key instead of checking a registry of trusted oracles.

Distributed-consensus signatures and state sync (`consensus_safety.cpp`, `trust_graph_sync.cpp`):

1.37 WHEN `consensus_safety.cpp` verifies a cross-chain trust attestation signature THEN the system only checks the signature length is between 64 and 128 bytes and does not verify it against the attestor's public key.

1.38 WHEN `consensus_safety.cpp` computes a trust-graph delta or requests a delta from a peer THEN the system returns an empty delta and does not query the database or request data from the peer.

1.39 WHEN `trust_graph_sync.cpp` verifies trust-graph state or applies a delta without a configured consensus validator THEN the system returns false, so trust-graph synchronization silently fails.

Fee, gas, and subsidy accounting (`fee_calculator.cpp`, `sustainable_gas.cpp`, `gas_subsidy.cpp`, `gas_allowance.cpp`, `mempool_priority.cpp`, `mempool_manager.cpp`):

1.40 WHEN `fee_calculator.cpp` decides whether an operation is subsidy-eligible THEN the system uses a simplified `reputation >= 80` check rather than assessing actual network benefit.

1.41 WHEN `fee_calculator.cpp` `ExtractSenderAddress` extracts the transaction sender THEN the system returns an empty `uint160`, so downstream reputation/fee logic has no real sender.

1.42 WHEN `fee_calculator.cpp` computes network load or the gas-to-satoshi rate THEN the system returns a hardcoded moderate load (50) via a heuristic and a fixed default conversion rate rather than live values.

1.43 WHEN `sustainable_gas.cpp` `IsBeneficialOperation` classifies an operation THEN the system uses a simplified `callerReputation >= 70` check rather than a real benefit assessment.

1.44 WHEN `gas_subsidy.cpp` distributes pending rebates or persists subsidy state THEN the system increments counters without transferring funds or crediting accounts and serializes records incompletely.

1.45 WHEN `gas_allowance.cpp` `LoadAllowanceStates` loads allowance state at startup THEN the system does not iterate the database and relies on on-demand loading, so bulk allowance state is not restored.

1.46 WHEN `mempool_priority.cpp` initializes THEN the system does not initialize the CVM database (marked TODO), so priority decisions run without database context.

1.47 WHEN `mempool_manager.cpp` `ProcessValidatorResponse` receives a validator response THEN the system only logs it and does not accumulate responses toward a consensus decision.

Sybil-resistance and fraud detection (`reputation.cpp`, `walletcluster.cpp`):

1.48 WHEN `reputation.cpp` `PatternDetector::DetectExchangePattern` is invoked THEN the system always returns false regardless of transaction volume.

1.49 WHEN `reputation.cpp` `GetAddressesWithReputation` is invoked THEN the system returns an empty vector because no reputation index is maintained.

1.50 WHEN `walletcluster.cpp` `GetTransactionsForAddress` looks up an address's transactions THEN the system returns an empty list (placeholder for a transaction index), so common-input clustering has no data to operate on.

Storage, state synchronization, and miscellaneous (`cvmdb.cpp`, `access_control_audit.cpp`, `contract_state_sync.cpp`, `metrics.cpp`, `backward_compat.cpp`, `tx_priority.cpp`, `blockprocessor.cpp`, `commit_reveal.cpp`, `clusterupdatehandler.cpp`):

1.51 WHEN `cvmdb.cpp` `PruneReceipts` is called THEN the system only logs the request and does not delete any receipts.

1.52 WHEN `access_control_audit.cpp` `LoadBlacklist` loads blacklist entries THEN the system does not iterate database keys, so persisted blacklist entries are not restored.

1.53 WHEN `contract_state_sync.cpp` builds a state-proof request or contract metadata THEN the system uses simplified key encoding and reports `storageSize = 0` and `chunkCount = 1` rather than actual storage sizing.

1.54 WHEN `metrics.cpp` `RecordOpcodeExecution` records an opcode THEN the system skips per-opcode tracking (pending thread-safe map access).

1.55 WHEN `backward_compat.cpp` validates reputation-score or trust-graph preservation across activation THEN the system returns true without querying the trust-graph database or comparing scores against tolerance.

1.56 WHEN `tx_priority.cpp` or `blockprocessor.cpp` extracts a sender, deployer, or caller address THEN the system hashes the first input's prevout to produce a pseudo-address instead of extracting the real address.

1.57 WHEN `commit_reveal.cpp` determines the commit-phase start for a dispute THEN the system uses the dispute's `createdTime` as a stand-in rather than an explicit commit-phase-start field.

1.58 WHEN `clusterupdatehandler.cpp` processes cluster merges THEN the system uses the first cluster's canonical address as the linking address as a placeholder rather than the actual linking address.

Security monitoring RPC (`security_rpc.cpp`):

1.59 WHEN the `getvalidatorstats_security` RPC (`security_rpc.cpp`) is invoked THEN the system returns placeholder data — a single static `message` field ("Validator stats available through HAT consensus system") — instead of the per-validator statistics (total/accurate/inaccurate validations, abstentions, accuracy rate, reputation, last activity) that its own help text documents.

Primary block-processing execution path (`blockprocessor.cpp`, `enhanced_vm.cpp`, `graceful_degradation.cpp`):

1.60 WHEN a contract deploy or call is processed through the primary execution path in `blockprocessor.cpp` THEN the system passes a hardcoded `0` for the deploy/call value and an empty `uint256()` for the block hash to the Enhanced VM, so contract execution sees `CALLVALUE = 0` and an empty `BLOCKHASH`/block context regardless of the actual transaction value or block.

1.61 WHEN `enhanced_vm.cpp` `CommitExecutionState` is called THEN the system only logs "Committed execution state to database" and does not flush any pending contract-state writes, so it is a no-op that leaves committed execution results unpersisted.

1.62 WHEN `graceful_degradation.cpp` runs its resource-usage checks (`CheckMemoryUsage`, `CheckCPUUsage`, `CheckStorageUsage`) or exercises the `TRUST_CONTEXT` and `HAT_VALIDATION` fallback paths THEN the system executes empty no-op check bodies and reports success (via `RecordSubsystemSuccess`) without invoking the real trust-context or HAT-validation subsystems.

### Expected Behavior (Correct)

Consensus-critical accounting and validation:

2.1 WHEN a block contains CVM/EVM transactions that carry gas subsidies THEN the system SHALL accumulate the actual per-transaction subsidies and reject the block when the total exceeds the per-block subsidy maximum.

2.2 WHEN gas cost is extracted from a contract deploy or call transaction THEN the system SHALL compute gas cost from the actual gas used and the transaction's gas price.

2.3 WHEN a validator node is selected for a validation task THEN the system SHALL perform the actual validation of the task and report `isValid` and a confidence value derived from that validation result.

2.4 WHEN a validator generates a validation response THEN the system SHALL compute the reported trust score from the trust graph for the relevant address.

Reputation signatures and proofs:

2.5 WHEN a reputation state proof is created THEN the system SHALL produce a real validator signature over the proof data and SHALL derive the state root from the committed reputation state.

2.6 WHEN a reputation signature is verified THEN the system SHALL perform ECDSA verification against the signer's public key and reject signatures that do not verify, even if they are of valid length.

2.7 WHEN a reputation merkle proof is built for an address THEN the system SHALL derive the proof from the actual reputation state tree so that verification against the committed state root succeeds only for genuinely committed entries.

HAT v2 distributed consensus:

2.8 WHEN the HAT consensus validator sends a validation challenge to a selected validator THEN the system SHALL transmit the challenge as a P2P message to the target validator and report success only when the message is dispatched.

2.9 WHEN a HAT consensus dispute is created THEN the system SHALL use the validator's actual self-reported score so that a discrepancy between self-reported and calculated scores can be detected.

Fee/subsidy handling and EVM compatibility:

2.10 WHEN a mempool transaction is not eligible for free gas THEN the system SHALL apply any applicable gas subsidy before computing the effective fee.

2.11 WHEN contract bytecode executes the EVM transient storage opcodes (TLOAD/TSTORE) THEN the system SHALL store and load transient values correctly for the duration of the transaction and clear them afterward.

2.12 WHEN contract bytecode reads the EIP-1559 base fee (BASEFEE opcode) THEN the system SHALL return the correct base fee for the current block.

Sybil-resistance, fraud detection, and health checks:

2.13 WHEN wallet cluster detection runs THEN the system SHALL return the transactions associated with an address and apply the common-input-ownership heuristic to group addresses into clusters.

2.14 WHEN rapid-fire abuse detection is invoked for an address THEN the system SHALL return true when the address's transaction history matches the rapid-fire pattern and false otherwise.

2.15 WHEN the graceful-degradation layer performs a reputation query or health check THEN the system SHALL query the real reputation subsystem and report success or failure based on the actual result.

Additional consensus-critical block processing:

2.16 WHEN a contract deploy transaction is processed in `cvmtx.cpp` `ProcessCVMBlock` THEN the system SHALL derive the contract address from the deployer address and nonce using the same scheme used elsewhere in the CVM, so addresses are consistent across code paths.

2.17 WHEN a contract deploy or call transaction is processed in `cvmtx.cpp` `ProcessCVMBlock` THEN the system SHALL execute the constructor (deploy) or contract code (call) and account for the actual gas used.

2.18 WHEN reputation votes are applied in `cvmtx.cpp` `UpdateReputationScores` THEN the system SHALL resolve the real voter address before applying the vote and SHALL update per-participant behavior scores.

2.19 WHEN `block_validator.cpp` records a gas subsidy during block validation THEN the system SHALL determine benefit from the actual operation and record the actual gas used.

2.20 WHEN `block_validator.cpp` verifies contract-transaction fees THEN the system SHALL verify the fee against the transaction's input values.

2.21 WHEN `block_validator.cpp` saves or rolls back contract state for a block THEN the system SHALL persist state changes atomically and SHALL revert all state changes when a block is rejected.

2.22 WHEN `validator_compensation.cpp` `CheckCoinbaseValidatorPayments` validates the coinbase THEN the system SHALL enforce the 70/30 validator payment split once validator participation data is available.

Core VM opcode handlers:

2.23 WHEN the CVM executes `OP_VERIFY_SIG`, `OP_VERIFY_SIG_ECDSA`, or `OP_VERIFY_SIG_QUANTUM` THEN the system SHALL verify the signature against the provided message and public key using the appropriate algorithm and push 1 only on a valid signature.

2.24 WHEN the CVM executes `OP_BALANCE` THEN the system SHALL push the account's actual balance.

2.25 WHEN the CVM executes `OP_CALL` or `CallContract` THEN the system SHALL load and execute the target contract with proper gas and state handling, or fail deterministically with a defined error when execution is not possible.

2.26 WHEN the CVM executes `OP_LOG` THEN the system SHALL consume the topic count, topics, and data and emit a corresponding log entry.

EVM compatibility:

2.27 WHEN `nonce_manager.cpp` `GenerateContractAddress` computes a CREATE address THEN the system SHALL compute `keccak256(rlp([sender, nonce]))[12:]` so the address is Ethereum-compatible.

2.28 WHEN `receipt.cpp` `TransactionReceipt::ToJSON` emits `logsBloom` THEN the system SHALL compute the bloom filter from the receipt's log addresses and topics.

2.29 WHEN `evm_rpc.cpp` extracts the sender for an EVM transaction THEN the system SHALL resolve the actual sender address and estimate gas from actual execution.

2.30 WHEN `evm_engine.cpp` executes an EVM message THEN the system SHALL make caller reputation available to execution and enforce trust-tagged memory access according to the defined policy.

2.31 WHEN `enhanced_vm.cpp` saves execution state for a nested frame THEN the system SHALL capture the real execution context so it can be restored on return.

2.32 WHEN `enhanced_storage.cpp` generates or verifies a storage proof THEN the system SHALL produce and verify a proof that attests to the committed storage state.

Cross-chain bridging and oracle trust:

2.33 WHEN `cross_chain_bridge.cpp` `ReputationProof::Verify` validates an inbound proof THEN the system SHALL verify the proof against the source chain's committed state.

2.34 WHEN `cross_chain_bridge.cpp` sends an attestation via LayerZero or a reputation proof via CCIP THEN the system SHALL transmit the message via the corresponding endpoint and report success only when the message is dispatched.

2.35 WHEN `cross_chain_bridge.cpp` generates a merkle proof or reads trust attestations THEN the system SHALL derive the proof from the actual state trie and return all committed attestations, not only cached ones.

2.36 WHEN `trust_context.cpp` `IsKnownLayerZeroOracle` checks an oracle public key THEN the system SHALL verify the key against a registry of trusted oracles for the given chain.

Distributed-consensus signatures and state sync:

2.37 WHEN `consensus_safety.cpp` verifies a cross-chain trust attestation signature THEN the system SHALL verify the signature against the attestor's public key and reject signatures that do not verify.

2.38 WHEN `consensus_safety.cpp` computes a trust-graph delta or requests a delta from a peer THEN the system SHALL query the database for actual changes and request the delta from the peer.

2.39 WHEN `trust_graph_sync.cpp` verifies trust-graph state or applies a delta THEN the system SHALL perform the verification/application against real state rather than failing when a validator is not configured.

Fee, gas, and subsidy accounting:

2.40 WHEN `fee_calculator.cpp` decides whether an operation is subsidy-eligible THEN the system SHALL assess actual network benefit rather than reputation alone.

2.41 WHEN `fee_calculator.cpp` `ExtractSenderAddress` extracts the transaction sender THEN the system SHALL resolve the real sender address (e.g., via the UTXO set provided by validation) rather than returning empty.

2.42 WHEN `fee_calculator.cpp` computes network load or the gas-to-satoshi rate THEN the system SHALL derive load from actual mempool state and the conversion rate from the configured pricing source.

2.43 WHEN `sustainable_gas.cpp` `IsBeneficialOperation` classifies an operation THEN the system SHALL assess actual network benefit rather than reputation alone.

2.44 WHEN `gas_subsidy.cpp` distributes pending rebates or persists subsidy state THEN the system SHALL transfer or credit the rebate amounts and SHALL serialize all subsidy records completely.

2.45 WHEN `gas_allowance.cpp` `LoadAllowanceStates` loads allowance state at startup THEN the system SHALL restore persisted allowance state from the database.

2.46 WHEN `mempool_priority.cpp` initializes THEN the system SHALL initialize with the CVM database so priority decisions have database context.

2.47 WHEN `mempool_manager.cpp` `ProcessValidatorResponse` receives a validator response THEN the system SHALL accumulate responses in the validation session and evaluate consensus.

Sybil-resistance and fraud detection:

2.48 WHEN `reputation.cpp` `PatternDetector::DetectExchangePattern` is invoked THEN the system SHALL return true when the address's transaction volume matches the exchange pattern and false otherwise.

2.49 WHEN `reputation.cpp` `GetAddressesWithReputation` is invoked THEN the system SHALL return the addresses that have reputation records, backed by a maintained index.

2.50 WHEN `walletcluster.cpp` `GetTransactionsForAddress` looks up an address's transactions THEN the system SHALL return the address's transactions from a transaction index so clustering can operate on real data.

Storage, state synchronization, and miscellaneous:

2.51 WHEN `cvmdb.cpp` `PruneReceipts` is called THEN the system SHALL delete receipts for blocks before the given height.

2.52 WHEN `access_control_audit.cpp` `LoadBlacklist` loads blacklist entries THEN the system SHALL iterate the database and restore all persisted blacklist entries.

2.53 WHEN `contract_state_sync.cpp` builds a state-proof request or contract metadata THEN the system SHALL use proper key encoding and report the actual storage size and chunk count.

2.54 WHEN `metrics.cpp` `RecordOpcodeExecution` records an opcode THEN the system SHALL track per-opcode counts using thread-safe access.

2.55 WHEN `backward_compat.cpp` validates reputation-score or trust-graph preservation across activation THEN the system SHALL query the trust-graph database and compare scores against the defined tolerance.

2.56 WHEN `tx_priority.cpp` or `blockprocessor.cpp` extracts a sender, deployer, or caller address THEN the system SHALL extract the real address from the transaction inputs (via the UTXO set) rather than a pseudo-address.

2.57 WHEN `commit_reveal.cpp` determines the commit-phase start for a dispute THEN the system SHALL use an explicit commit-phase-start field.

2.58 WHEN `clusterupdatehandler.cpp` processes cluster merges THEN the system SHALL use the actual linking address that connects the merged clusters.

Security monitoring RPC:

2.59 WHEN the `getvalidatorstats_security` RPC (`security_rpc.cpp`) is invoked THEN the system SHALL return the actual per-validator statistics from the HAT consensus system for the requested validator address, populating the fields documented in the RPC's help text.

Primary block-processing execution path:

2.60 WHEN a contract deploy or call is processed through the primary execution path in `blockprocessor.cpp` THEN the system SHALL pass the actual transaction value and the real block hash to the Enhanced VM so that `CALLVALUE` and block context are correct during block processing.

2.61 WHEN `enhanced_vm.cpp` `CommitExecutionState` is called THEN the system SHALL flush pending contract-state writes to the database so that committed execution results are durably persisted.

2.62 WHEN `graceful_degradation.cpp` runs its resource-usage checks or exercises the `TRUST_CONTEXT` and `HAT_VALIDATION` fallback paths THEN the system SHALL measure actual memory/CPU/storage usage and invoke the real trust-context and HAT-validation subsystems, reporting success or failure based on the actual result.

### Unchanged Behavior (Regression Prevention)

3.1 WHEN a contract deploy or call transaction carries a gas price that produces a cost equal to the previous fixed rate THEN the system SHALL CONTINUE TO accept the same fee/subsidy split for that transaction.

3.2 WHEN a block contains no CVM/EVM transactions and no gas subsidies THEN the system SHALL CONTINUE TO validate the block exactly as before.

3.3 WHEN a transaction is a Web-of-Trust operation (reputation vote, trust edge, bonded vote, DAO dispute, DAO vote) THEN the system SHALL CONTINUE TO treat it as a non-contract transaction with 100% of fees to the miner.

3.4 WHEN a validation response is signed and later verified THEN the system SHALL CONTINUE TO produce and accept valid secp256k1 ECDSA signatures using the existing signing and verification path.

3.5 WHEN validators are selected for a task with a given seed and block height THEN the system SHALL CONTINUE TO select the same deterministic set via the existing Fisher-Yates selection.

3.6 WHEN a reputation merkle proof for a genuinely committed leaf is verified against the correct root THEN the system SHALL CONTINUE TO verify successfully using the existing merkle verification math.

3.7 WHEN contract bytecode uses persistent storage (SLOAD/SSTORE) THEN the system SHALL CONTINUE TO read and write contract state correctly.

3.8 WHEN a mempool transaction is eligible for free gas and has sufficient remaining allowance THEN the system SHALL CONTINUE TO charge a zero effective fee.

3.9 WHEN contract bytecode uses non-transient EVM opcodes and context values already supported THEN the system SHALL CONTINUE TO execute them with unchanged results.

3.10 WHEN a standard (non-CVM, non-EVM) transaction is validated or added to the mempool THEN the system SHALL CONTINUE TO process it exactly as before.

3.11 WHEN a valid signature is presented to the `OP_VERIFY_SIG` family THEN the system SHALL CONTINUE TO push 1 (accept), so contracts relying on genuinely valid signatures keep working after verification is enforced.

3.12 WHEN a contract deploy or call is processed through the primary execution path in `blockprocessor.cpp` THEN the system SHALL CONTINUE TO deploy and execute contracts as before, unaffected by fixes to the parallel `cvmtx.cpp` path, except that the value/block-hash context passed to the Enhanced VM and the state-commit behavior are corrected per 1.60/2.60 and 1.61/2.61.

3.13 WHEN a receipt is serialized to JSON THEN the system SHALL CONTINUE TO emit all existing Ethereum-compatible and Cascoin-specific fields with unchanged values, adding only a correctly computed `logsBloom`.

3.14 WHEN a contract address is generated via the existing deployer+nonce path used by the rest of the CVM THEN the system SHALL CONTINUE TO produce the same addresses.

3.15 WHEN a cross-chain proof or attestation with a genuinely valid signature and committed source state is verified THEN the system SHALL CONTINUE TO accept it once real verification is in place.

3.16 WHEN a transaction is subsidy-eligible under both the old simplified reputation check and a real benefit assessment THEN the system SHALL CONTINUE TO grant the subsidy for that transaction.

3.17 WHEN a block with no gas subsidies and no failed contract executions is validated THEN the system SHALL CONTINUE TO validate, save state, and finalize it exactly as before.

3.18 WHEN a validator response is signed and verified through the existing secp256k1 ECDSA path THEN the system SHALL CONTINUE TO produce and accept valid signatures unchanged.

3.19 WHEN persisted blacklist, allowance, reputation, or trust-graph state is already loaded correctly by an existing path THEN the system SHALL CONTINUE TO expose that state unchanged after the load routines are completed.

3.20 WHEN bytecode format detection, trust-graph manipulation detection, and behavior-metric scoring run THEN the system SHALL CONTINUE TO produce their current results, as these paths were verified functional and are out of scope for this fix.

3.21 WHEN the other security-monitoring RPCs (anomaly-alert resolution, security configuration) are invoked THEN the system SHALL CONTINUE TO behave exactly as before, unaffected by populating real validator statistics in `getvalidatorstats_security`.

3.22 WHEN a contract deploy or call carrying zero value is processed through the primary execution path in `blockprocessor.cpp` THEN the system SHALL CONTINUE TO expose `CALLVALUE = 0` to contract execution, since the corrected path passes the actual (zero) value.

3.23 WHEN a block's contract execution results were already durably persisted by an existing path THEN the system SHALL CONTINUE TO expose that state unchanged after `CommitExecutionState` flushes pending writes.

3.24 WHEN a subsystem monitored by `graceful_degradation.cpp` is genuinely healthy and operating normally THEN the system SHALL CONTINUE TO report success and proceed without degradation once the checks invoke the real subsystems.
