# Implementation Plan: L2 Burn-and-Mint Token Model

## Overview

Diese Implementierung ersetzt das alte Token-Modell durch ein sicheres Burn-and-Mint System mit OP_RETURN und Sequencer-Konsens. CAS wird auf L1 via OP_RETURN verbrannt und L2-Tokens werden nach 2/3 Sequencer-Konsens gemintet.

## Tasks

- [x] 1. Implement OP_RETURN Burn Parser
  - [x] 1.1 Create BurnData struct in `src/l2/burn_parser.h`
    - Define chainId, recipientPubKey, amount fields
    - Implement IsValid() method
    - Implement Parse() static method for CScript
    - Add serialization support
    - _Requirements: 1.2, 2.1_

  - [x] 1.2 Implement BurnTransactionParser class
    - Implement ParseBurnTransaction() for full TX parsing
    - Implement ValidateBurnFormat() for script validation
    - Implement CalculateBurnedAmount() to verify amount
    - Implement CreateBurnScript() to create burn outputs
    - _Requirements: 1.2, 1.3, 1.4_

  - [x] 1.3 Write property test for OP_RETURN format validation
    - **Property 1: OP_RETURN Format Validation**
    - **Validates: Requirements 1.2, 2.1**

  - [x] 1.4 Write property test for burn amount consistency
    - **Property 2: Burn Amount Consistency**
    - **Validates: Requirements 1.4, 4.2**

- [x] 2. Implement Burn Validator
  - [x] 2.1 Create BurnValidator class in `src/l2/burn_validator.h/cpp`
    - Implement ValidateBurn() method
    - Implement HasSufficientConfirmations() (min 6)
    - Implement MatchesChainId() check
    - Implement IsAlreadyProcessed() check
    - _Requirements: 2.1, 2.2, 2.3, 2.4_

  - [x] 2.2 Write property test for confirmation count requirement
    - **Property 9: Confirmation Count Requirement**
    - **Validates: Requirements 2.2**

  - [x] 2.3 Write property test for chain ID validation
    - **Property 8: Chain ID Validation**
    - **Validates: Requirements 2.3**

- [x] 3. Checkpoint - Burn Parsing and Validation
  - Ensure all tests pass, ask the user if questions arise.


- [x] 4. Implement Burn Registry
  - [x] 4.1 Create BurnRecord struct in `src/l2/burn_registry.h`
    - Define l1TxHash, l1BlockNumber, l1BlockHash fields
    - Define l2Recipient, amount, l2MintBlock, l2MintTxHash fields
    - Add timestamp and serialization
    - _Requirements: 5.1, 5.2_

  - [x] 4.2 Implement BurnRegistry class in `src/l2/burn_registry.cpp`
    - Implement IsProcessed() check
    - Implement RecordBurn() to store processed burns
    - Implement GetBurnRecord() query
    - Implement GetBurnsForAddress() query
    - Implement GetTotalBurned() query
    - Implement HandleReorg() for L2 reorgs
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_

  - [x] 4.3 Write property test for double-mint prevention
    - **Property 3: Double-Mint Prevention**
    - **Validates: Requirements 2.4, 5.3, 5.4**

- [x] 5. Implement Mint Consensus Manager
  - [x] 5.1 Create MintConfirmation struct in `src/l2/mint_consensus.h`
    - Define l1TxHash, l2Recipient, amount, sequencerAddress fields
    - Define signature and timestamp fields
    - Implement GetHash() and VerifySignature() methods
    - _Requirements: 3.2_

  - [x] 5.2 Create MintConsensusState struct
    - Define l1TxHash, burnData, confirmations map
    - Define firstSeenTime and status enum
    - Implement GetConfirmationRatio() and HasReachedConsensus()
    - _Requirements: 3.3, 3.4_

  - [x] 5.3 Implement MintConsensusManager class in `src/l2/mint_consensus.cpp`
    - Implement SubmitConfirmation() for local sequencer
    - Implement ProcessConfirmation() for network messages
    - Implement HasConsensus() check (2/3 threshold)
    - Implement GetConsensusState() query
    - Implement GetPendingBurns() query
    - Implement ProcessTimeouts() for 10-minute timeout
    - _Requirements: 3.1, 3.3, 3.4, 3.5, 3.6_

  - [x] 5.4 Write property test for consensus threshold
    - **Property 4: Consensus Threshold**
    - **Validates: Requirements 3.1, 3.4, 10.3**

  - [x] 5.5 Write property test for confirmation uniqueness
    - **Property 7: Confirmation Uniqueness**
    - **Validates: Requirements 3.6**

- [x] 6. Checkpoint - Consensus System
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement L2 Token Minter
  - [x] 7.1 Create L2TokenMinter class in `src/l2/l2_minter.h/cpp`
    - Implement MintTokens() method (called after consensus)
    - Implement GetTotalSupply() query
    - Implement VerifySupplyInvariant() check
    - Implement GetBalance() query
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.3_

  - [x] 7.2 Write property test for 1:1 mint ratio
    - **Property 5: 1:1 Mint Ratio**
    - **Validates: Requirements 4.2**

  - [x] 7.3 Write property test for supply invariant
    - **Property 6: Supply Invariant**
    - **Validates: Requirements 8.1, 8.3**

- [x] 8. Implement Fee Distributor
  - [x] 8.1 Create FeeDistributor class in `src/l2/fee_distributor.h/cpp`
    - Implement DistributeBlockFees() method
    - Implement CalculateBlockFees() method
    - Implement GetFeeHistory() query
    - Implement GetTotalFeesEarned() query
    - Define MIN_TRANSACTION_FEE constant
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

  - [x] 8.2 Write property test for fee-only sequencer rewards
    - **Property 10: Fee-Only Sequencer Rewards**
    - **Validates: Requirements 6.1, 6.2, 6.3**

- [x] 9. Checkpoint - Minting and Fees
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Implement RPC Interface
  - [x] 10.1 Create `l2_createburntx` RPC in `src/rpc/l2_burn.cpp`
    - Accept amount and l2_recipient_address parameters
    - Create burn transaction with OP_RETURN output
    - Return unsigned transaction hex
    - _Requirements: 1.5, 9.1_

  - [x] 10.2 Create `l2_sendburntx` RPC
    - Accept signed transaction hex
    - Broadcast to L1 network
    - Return L1 transaction hash
    - _Requirements: 1.6, 9.1_

  - [x] 10.3 Create `l2_getburnstatus` RPC
    - Accept L1 transaction hash
    - Return: confirmations, consensus status, mint status
    - _Requirements: 5.5, 9.2_

  - [x] 10.4 Create `l2_getpendingburns` RPC
    - Return list of burns waiting for consensus
    - Include confirmation count per burn
    - _Requirements: 9.6_

  - [x] 10.5 Create `l2_getminthistory` RPC
    - Accept optional from_block and to_block parameters
    - Return paginated list of mints
    - _Requirements: 9.3_

  - [x] 10.6 Create `l2_gettotalsupply` RPC
    - Return current L2 token total supply
    - _Requirements: 9.4_

  - [x] 10.7 Create `l2_verifysupply` RPC
    - Verify supply invariant (total_supply == sum(balances))
    - Return verification result with details
    - _Requirements: 8.2, 9.5_

  - [x] 10.8 Create `l2_getburnsforaddress` RPC
    - Accept address parameter
    - Return all burns for that address
    - _Requirements: 9.1_

  - [x] 10.9 Register all new RPCs in RPC table
    - Add to `src/rpc/register.h`
    - Update Makefile.am if needed
    - _Requirements: 9.1-9.6_

- [x] 11. Checkpoint - RPC Interface
  - Test all RPCs manually
  - Ensure all tests pass, ask the user if questions arise.

- [x] 12. Remove Legacy Bridge Code
  - [x] 12.1 Remove old `l2_deposit` RPC completely
    - Remove function implementation from `src/rpc/l2.cpp`
    - Remove from RPC command table
    - Remove from `src/rpc/client.cpp` parameter conversion
    - _Requirements: 11.1, 11.4_

  - [x] 12.2 Remove old `l2_withdraw` RPC completely
    - Remove function implementation from `src/rpc/l2.cpp`
    - Remove from RPC command table
    - Remove from `src/rpc/client.cpp` parameter conversion
    - _Requirements: 11.1, 11.4_

  - [x] 12.3 Remove old bridge contract code
    - Remove or disable `src/l2/bridge_contract.cpp/h`
    - Remove bridge-related tests
    - _Requirements: 11.4_

  - [x] 12.4 Update L2TokenManager
    - Remove ProcessBlockReward() minting logic
    - Sequencer rewards now come from FeeDistributor only
    - _Requirements: 6.1, 6.2_

  - [x] 12.5 Clean up old deposit/withdrawal code paths
    - Remove deposit tracking from state manager
    - Remove withdrawal processing
    - _Requirements: 11.4_

- [x] 13. Checkpoint - Legacy Removal
  - Verify old RPCs return proper deprecation errors
  - Ensure no minting outside of burn-and-mint flow
  - Ask the user if questions arise.

- [x] 14. Integration with Existing L2 Components
  - [x] 14.1 Integrate BurnValidator with L1 chain monitoring
    - Subscribe to new L1 blocks
    - Scan for OP_RETURN burn transactions
    - Trigger validation when burn detected
    - _Requirements: 2.1, 2.2_

  - [x] 14.2 Integrate MintConsensusManager with sequencer network
    - Broadcast confirmations via P2P
    - Process incoming confirmations from peers
    - _Requirements: 3.1, 3.3_

  - [x] 14.3 Integrate L2TokenMinter with StateManager
    - Update balances atomically
    - Emit events for mint operations
    - _Requirements: 4.4, 4.5_

  - [x] 14.4 Integrate FeeDistributor with block production
    - Hook into block finalization
    - Distribute fees to block producer
    - _Requirements: 6.3, 6.4_

  - [x] 14.5 Update L2 block validation
    - Verify no unauthorized minting in blocks
    - Verify fee distribution is correct
    - _Requirements: 10.4_

- [x] 15. Checkpoint - Integration
  - Run full integration tests
  - Test multi-sequencer consensus
  - Ask the user if questions arise.

- [x] 16. Write Integration Tests
  - [x] 16.1 Test full burn-mint flow
    - Create burn TX on L1
    - Wait for confirmations
    - Verify consensus reached
    - Verify tokens minted
    - _Requirements: 1.1-1.6, 4.1-4.6_

  - [x] 16.2 Test multi-sequencer consensus
    - Setup 5 sequencers
    - Verify 2/3 threshold (4 needed)
    - Test with 3 confirmations (should fail)
    - Test with 4 confirmations (should succeed)
    - _Requirements: 3.1, 3.4, 10.3_

  - [x] 16.3 Test double-mint prevention
    - Submit same burn TX twice
    - Verify second attempt rejected
    - _Requirements: 5.3, 5.4_

  - [x] 16.4 Test fee distribution
    - Create block with transactions
    - Verify sequencer receives fees
    - Verify no new tokens minted
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 16.5 Test supply invariant
    - After multiple burns and transfers
    - Verify total_supply == sum(balances)
    - Verify total_supply == total_burned_l1
    - _Requirements: 8.1, 8.3, 8.4_

  - [x] 16.6 Test L2 reorg handling
    - Simulate L2 reorg
    - Verify burn registry updated correctly
    - _Requirements: 5.6_

- [x] 17. Update Documentation
  - [x] 17.1 Update L2_DEVELOPER_GUIDE.md
    - Document burn-and-mint flow
    - Document new RPC commands
    - Remove old bridge documentation
    - _Requirements: 11.5_

  - [x] 17.2 Update L2_OPERATOR_GUIDE.md
    - Document sequencer consensus requirements
    - Document fee distribution
    - _Requirements: 11.5_

  - [x] 17.4 Update Demo Scripts in `contrib/demos/l2-chain/`
    - Update `setup_l2_demo.sh` to use burn-and-mint flow
    - Remove old deposit/withdraw demo commands
    - Add burn transaction demo (create OP_RETURN TX)
    - Add consensus demo (show sequencer confirmations)
    - Update `config/regtest.conf` if needed
    - _Requirements: 11.5_

  - [x] 17.5 Update Demo README
    - Document new burn-and-mint demo workflow
    - Add example RPC calls for `l2_createburntx`, `l2_getburnstatus`
    - Explain how to observe consensus in demo
    - _Requirements: 11.5_

- [x] 18. Final Checkpoint
  - All unit tests pass
  - All property tests pass
  - All integration tests pass
  - Documentation complete
  - Ready for code review
