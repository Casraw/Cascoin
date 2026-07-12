// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/blockprocessor.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/contract.h>
#include <cvm/trustgraph.h>
#include <cvm/walletcluster.h>
#include <cvm/trustpropagator.h>
#include <cvm/clusterupdatehandler.h>
#include <cvm/enhanced_vm.h>
#include <cvm/trust_context.h>
#include <cvm/gas_allowance.h>
#include <cvm/sustainable_gas.h>
#include <cvm/gas_subsidy.h>
#include <util.h>
#include <utilmoneystr.h>
#include <hash.h>
#include <timedata.h>

namespace CVM {

// Global gas allowance tracker
static GasAllowanceTracker g_gasAllowanceTracker;

// Global gas subsidy tracker
static GasSubsidyTracker g_gasSubsidyTracker;

// Global trust propagation components (Requirements: 2.4, 16.1)
std::unique_ptr<TrustGraph> g_trustGraph;
std::unique_ptr<WalletClusterer> g_walletClusterer;
std::unique_ptr<TrustPropagator> g_trustPropagator;
std::unique_ptr<ClusterUpdateHandler> g_clusterUpdateHandler;

void CVMBlockProcessor::ProcessBlock(
    const CBlock& block,
    int height,
    CVMDatabase& db
) {
    int cvmTxCount = 0;

    // The real block hash is threaded into contract execution so the Enhanced
    // VM sees the correct BLOCKHASH/block context during block processing
    // (bugfix 2.60), instead of an empty uint256().
    const uint256 blockHash = block.GetHash();
    
    // Process all transactions in block
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check if transaction contains CVM data
        int cvmOutputIndex = FindCVMOpReturn(*tx);
        
        if (cvmOutputIndex >= 0) {
            ProcessTransaction(*tx, height, db, blockHash);
            cvmTxCount++;
        }
    }
    
    // Distribute pending rebates
    int rebatesDistributed = g_gasSubsidyTracker.DistributePendingRebates(height);
    
    if (cvmTxCount > 0 || rebatesDistributed > 0) {
        LogPrintf("CVM: Processed %d CVM transactions in block %d, Distributed %d rebates\n", 
                  cvmTxCount, height, rebatesDistributed);
    }
}

// Extract the value carried by a CVM transaction that is made available to the
// executing contract as CALLVALUE (bugfix 2.60). This is the total spendable
// value locked in the transaction's outputs other than the CVM OP_RETURN marker
// output (which carries no spendable value). A zero-value transaction (e.g. a
// pure OP_RETURN deploy) therefore yields CALLVALUE == 0, unchanged
// (preservation 3.22).
static uint64_t ExtractTransactionValue(const CTransaction& tx, int cvmOutputIndex)
{
    CAmount total = 0;
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        if (static_cast<int>(i) == cvmOutputIndex) {
            continue;
        }
        if (tx.vout[i].nValue > 0) {
            total += tx.vout[i].nValue;
        }
    }
    return static_cast<uint64_t>(total);
}

void CVMBlockProcessor::ProcessTransaction(
    const CTransaction& tx,
    int height,
    CVMDatabase& db,
    const uint256& blockHash
) {
    // Find CVM OP_RETURN output
    int cvmOutputIndex = FindCVMOpReturn(tx);
    if (cvmOutputIndex < 0) {
        return; // Not a CVM transaction
    }
    
    // Parse CVM data
    CVMOpType opType;
    std::vector<uint8_t> data;
    
    if (!ParseCVMOpReturn(tx.vout[cvmOutputIndex], opType, data)) {
        LogPrintf("CVM Warning: Failed to parse CVM OP_RETURN in tx %s\n", 
                  tx.GetHash().ToString());
        return;
    }
    
    // Process based on operation type
    switch (opType) {
        case CVMOpType::REPUTATION_VOTE: {
            CVMReputationData voteData;
            if (voteData.Deserialize(data)) {
                ProcessVote(voteData, tx, height, db);
            } else {
                LogPrintf("CVM Warning: Invalid vote data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::CONTRACT_DEPLOY: {
            CVMDeployData deployData;
            if (deployData.Deserialize(data)) {
                ProcessDeploy(deployData, tx, height, db, blockHash);
            } else {
                LogPrintf("CVM Warning: Invalid deploy data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::CONTRACT_CALL: {
            CVMCallData callData;
            if (callData.Deserialize(data)) {
                ProcessCall(callData, tx, height, db, blockHash);
            } else {
                LogPrintf("CVM Warning: Invalid call data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::TRUST_EDGE: {
            CVMTrustEdgeData trustData;
            if (trustData.Deserialize(data)) {
                ProcessTrustEdge(trustData, tx, height, db);
            } else {
                LogPrintf("CVM Warning: Invalid trust edge data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::BONDED_VOTE: {
            CVMBondedVoteData voteData;
            if (voteData.Deserialize(data)) {
                ProcessBondedVote(voteData, tx, height, db);
            } else {
                LogPrintf("CVM Warning: Invalid bonded vote data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::DAO_DISPUTE: {
            CVMDAODisputeData disputeData;
            if (disputeData.Deserialize(data)) {
                ProcessDAODispute(disputeData, tx, height, db);
            } else {
                LogPrintf("CVM Warning: Invalid DAO dispute data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::DAO_VOTE: {
            CVMDAOVoteData voteData;
            if (voteData.Deserialize(data)) {
                ProcessDAOVote(voteData, tx, height, db);
            } else {
                LogPrintf("CVM Warning: Invalid DAO vote data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        default:
            LogPrintf("CVM Warning: Unknown CVM operation type %d in tx %s\n", 
                      static_cast<int>(opType), tx.GetHash().ToString());
            break;
    }
}

void CVMBlockProcessor::ProcessVote(
    const CVMReputationData& voteData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db
) {
    LogPrint(BCLog::ALL, "CVM: Processing vote for %s: %+d\n", 
             voteData.targetAddress.ToString(), voteData.voteValue);
    
    // Get current reputation
    ReputationSystem repSystem(db);
    ReputationScore score;
    repSystem.GetReputation(voteData.targetAddress, score);
    
    // Update score
    score.score += voteData.voteValue;
    score.voteCount++;
    score.lastUpdated = voteData.timestamp;
    
    // Store updated reputation
    repSystem.UpdateReputation(voteData.targetAddress, score);
    
    LogPrintf("CVM: Vote processed - Address: %s, Vote: %+d, New Score: %d, VoteCount: %d\n",
              voteData.targetAddress.ToString(), voteData.voteValue, 
              score.score, score.voteCount);
}

void CVMBlockProcessor::ProcessDeploy(
    const CVMDeployData& deployData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db,
    const uint256& blockHash
) {
    LogPrint(BCLog::CVM, "CVM: Processing contract deployment: hash=%s\n", 
             deployData.codeHash.ToString());
    
    // Extract deployer address from transaction inputs
    uint160 deployer;
    if (!tx.vin.empty()) {
        // Get deployer from first input (simplified - would need proper address extraction)
        CHashWriter hw(SER_GETHASH, 0);
        hw << tx.vin[0].prevout;
        uint256 hash = hw.GetHash();
        memcpy(deployer.begin(), hash.begin(), 20);
    }
    
    // Get bytecode from deployData (now included in OP_RETURN serialization).
    // Fallback: check witness data for legacy transactions.
    std::vector<uint8_t> bytecode = deployData.bytecode;
    if (bytecode.empty()) {
        for (const auto& txin : tx.vin) {
            const auto& wit = txin.scriptWitness;
            for (size_t i = 0; i < wit.stack.size(); ++i) {
                const auto& candidate = wit.stack[i];
                if (!candidate.empty()) {
                    uint256 candidateHash = Hash(candidate.begin(), candidate.end());
                    if (candidateHash == deployData.codeHash) {
                        bytecode = candidate;
                        LogPrint(BCLog::CVM, "CVM BlockProcessor: Extracted bytecode (%d bytes) from witness stack[%d] (legacy fallback)\n",
                                 candidate.size(), (int)i);
                        break;
                    }
                }
            }
            if (!bytecode.empty()) break;
        }
    }
    if (bytecode.empty()) {
        LogPrintf("CVM Warning: Empty bytecode in deployment tx %s (no witness element matches codeHash)\n", tx.GetHash().ToString());
        return;
    }
    
    try {
        // Initialize trust context
        auto trust_ctx = std::make_shared<TrustContext>(&db);
        
        // Check if deployer is eligible for free gas
        cvm::SustainableGasSystem gasSystem;
        bool useFreeGas = gasSystem.IsEligibleForFreeGas(
            static_cast<uint8_t>(trust_ctx->GetCallerReputation())
        );
        
        // If eligible for free gas, check and deduct from allowance
        if (useFreeGas) {
            if (g_gasAllowanceTracker.HasSufficientAllowance(
                deployer, deployData.gasLimit, *trust_ctx, height)) {
                LogPrint(BCLog::CVM, "CVM: Using free gas for deployment (address has sufficient allowance)\n");
            } else {
                useFreeGas = false;
                LogPrint(BCLog::CVM, "CVM: Free gas allowance exhausted, will charge for deployment\n");
            }
        }
        
        // Derive the contract address from the deployer and nonce using the
        // canonical GenerateContractAddress(deployer, nonce) scheme, consistent
        // with every other CVM path (reconciliation / bugfix 2.16). The current
        // (pre-increment) nonce is used so the first deploy for a fresh deployer
        // lands at nonce 0; the nonce is advanced after a successful deploy.
        uint64_t nonce = 0;
        db.ReadNonce(deployer, nonce);
        uint160 contractAddr = GenerateContractAddress(deployer, nonce);

        // Pass the ACTUAL transaction value and the REAL block hash to the
        // Enhanced VM so CALLVALUE and BLOCKHASH/block context are correct during
        // block processing (bugfix 2.60), instead of a hardcoded 0 / uint256().
        uint64_t deployValue = ExtractTransactionValue(tx, FindCVMOpReturn(tx));

        // Initialize Enhanced VM with blockchain state
        // Note: In full integration, would pass CCoinsViewCache and CBlockIndex
        auto enhanced_vm = std::make_unique<EnhancedVM>(&db, trust_ctx);
        
        // Execute the constructor at the canonical contract address. The Enhanced
        // VM flushes pending contract-state writes durably via
        // CommitExecutionState (bugfix 2.61) on success, so any SSTORE performed
        // by the constructor is persisted.
        auto result = enhanced_vm->Execute(
            bytecode,
            deployData.gasLimit,
            contractAddr,
            deployer,
            deployValue,
            deployData.constructorData,
            height,
            blockHash,
            GetTime()
        );
        
        if (result.success) {
            // Persist the deployed contract at the canonical address with the
            // resolved deployer recorded, and advance the deployer's nonce.
            Contract contract;
            contract.address = contractAddr;
            contract.deployer = deployer;
            contract.code = bytecode;
            contract.deploymentHeight = height;
            contract.deploymentTx = tx.GetHash();
            db.WriteContract(contractAddr, contract);
            db.WriteNonce(deployer, nonce + 1);

            // Deduct gas from allowance if using free gas
            if (useFreeGas) {
                g_gasAllowanceTracker.DeductGas(deployer, result.gas_used, height);
            }
            
            // Check if operation is beneficial and apply subsidy
            bool isBeneficial = gasSystem.IsNetworkBeneficialOperation(0, *trust_ctx);
            if (isBeneficial) {
                uint64_t subsidy = g_gasSubsidyTracker.CalculateSubsidy(
                    result.gas_used, *trust_ctx, true
                );
                
                if (subsidy > 0) {
                    g_gasSubsidyTracker.ApplySubsidy(
                        tx.GetHash(), deployer, result.gas_used, subsidy, *trust_ctx, height
                    );
                    
                    // Queue rebate for distribution
                    g_gasSubsidyTracker.QueueRebate(
                        deployer, subsidy, height, "beneficial_deployment"
                    );
                }
            }
            
            LogPrintf("CVM: Contract deployed successfully - Address: %s, GasUsed: %d, FreeGas: %s, Subsidy: %s, Height: %d\n",
                      contractAddr.ToString(), result.gas_used, 
                      useFreeGas ? "yes" : "no", isBeneficial ? "yes" : "no", height);
        } else {
            LogPrintf("CVM Warning: Contract deployment failed - Error: %s, Tx: %s\n",
                      result.error, tx.GetHash().ToString());
        }
        
    } catch (const std::exception& e) {
        LogPrintf("CVM Error: Exception during contract deployment: %s, Tx: %s\n",
                  e.what(), tx.GetHash().ToString());
    }
}

void CVMBlockProcessor::ProcessCall(
    const CVMCallData& callData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db,
    const uint256& blockHash
) {
    LogPrint(BCLog::CVM, "CVM: Processing contract call to %s\n", 
             callData.contractAddress.ToString());
    
    // Check if contract exists
    if (!db.Exists(callData.contractAddress)) {
        LogPrintf("CVM Warning: Call to non-existent contract %s in tx %s\n",
                  callData.contractAddress.ToString(), tx.GetHash().ToString());
        return;
    }
    
    // Extract caller address from transaction inputs
    uint160 caller;
    if (!tx.vin.empty()) {
        // Get caller from first input (simplified - would need proper address extraction)
        CHashWriter hw(SER_GETHASH, 0);
        hw << tx.vin[0].prevout;
        uint256 hash = hw.GetHash();
        memcpy(caller.begin(), hash.begin(), 20);
    }
    
    try {
        // Initialize trust context
        auto trust_ctx = std::make_shared<TrustContext>(&db);
        
        // Check if caller is eligible for free gas
        cvm::SustainableGasSystem gasSystem;
        bool useFreeGas = gasSystem.IsEligibleForFreeGas(
            static_cast<uint8_t>(trust_ctx->GetCallerReputation())
        );
        
        // If eligible for free gas, check and deduct from allowance
        if (useFreeGas) {
            if (g_gasAllowanceTracker.HasSufficientAllowance(
                caller, callData.gasLimit, *trust_ctx, height)) {
                LogPrint(BCLog::CVM, "CVM: Using free gas for contract call (address has sufficient allowance)\n");
            } else {
                useFreeGas = false;
                LogPrint(BCLog::CVM, "CVM: Free gas allowance exhausted, will charge for call\n");
            }
        }
        
        // Initialize Enhanced VM with blockchain state
        // Note: In full integration, would pass CCoinsViewCache and CBlockIndex
        auto enhanced_vm = std::make_unique<EnhancedVM>(&db, trust_ctx);
        
        // Pass the ACTUAL transaction value and the REAL block hash to the
        // Enhanced VM so CALLVALUE and BLOCKHASH/block context are correct during
        // block processing (bugfix 2.60), instead of a hardcoded 0 / uint256().
        uint64_t callValue = ExtractTransactionValue(tx, FindCVMOpReturn(tx));

        // Execute contract call using Enhanced VM. The Enhanced VM flushes
        // pending contract-state writes durably via CommitExecutionState
        // (bugfix 2.61) on success.
        auto result = enhanced_vm->CallContract(
            callData.contractAddress,
            callData.callData,
            callData.gasLimit,
            caller,
            callValue,
            height,
            blockHash,
            GetTime()
        );
        
        if (result.success) {
            // Deduct gas from allowance if using free gas
            if (useFreeGas) {
                g_gasAllowanceTracker.DeductGas(caller, result.gas_used, height);
            }
            
            // Check if operation is beneficial and apply subsidy
            bool isBeneficial = gasSystem.IsNetworkBeneficialOperation(0, *trust_ctx);
            if (isBeneficial) {
                uint64_t subsidy = g_gasSubsidyTracker.CalculateSubsidy(
                    result.gas_used, *trust_ctx, true
                );
                
                if (subsidy > 0) {
                    g_gasSubsidyTracker.ApplySubsidy(
                        tx.GetHash(), caller, result.gas_used, subsidy, *trust_ctx, height
                    );
                    
                    // Queue rebate for distribution
                    g_gasSubsidyTracker.QueueRebate(
                        caller, subsidy, height, "beneficial_call"
                    );
                }
            }
            
            LogPrintf("CVM: Contract call successful - Contract: %s, GasUsed: %d, FreeGas: %s, Subsidy: %s, Tx: %s\n",
                      callData.contractAddress.ToString(), result.gas_used,
                      useFreeGas ? "yes" : "no", isBeneficial ? "yes" : "no", tx.GetHash().ToString());
            
            // Log any events emitted
            if (!result.logs.empty()) {
                LogPrint(BCLog::CVM, "CVM: Contract emitted %d log entries\n", result.logs.size());
            }
            
            // Storage updates are already committed by Enhanced VM
        } else {
            LogPrintf("CVM Warning: Contract call failed - Error: %s, Contract: %s, Tx: %s\n",
                      result.error, callData.contractAddress.ToString(), 
                      tx.GetHash().ToString());
        }
        
    } catch (const std::exception& e) {
        LogPrintf("CVM Error: Exception during contract call: %s, Contract: %s, Tx: %s\n",
                  e.what(), callData.contractAddress.ToString(), tx.GetHash().ToString());
    }
}

bool CVMBlockProcessor::ProcessTrustEdge(
    const CVMTrustEdgeData& trustData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db
) {
    LogPrintf("CVM: Processing trust edge: %s → %s, weight=%d\n", 
             HexStr(trustData.fromAddress), HexStr(trustData.toAddress), 
             trustData.weight);
    
    // Validate bond output
    if (!ValidateBond(tx, trustData.bondAmount)) {
        LogPrintf("CVM Warning: Invalid bond amount in trust edge tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }
    
    // Create trust edge
    TrustGraph trustGraph(db);
    
    // Store trust edge using the TrustGraph interface
    bool success = trustGraph.AddTrustEdge(
        trustData.fromAddress,
        trustData.toAddress,
        trustData.weight,
        trustData.bondAmount,
        tx.GetHash(),
        "" // reason - could be extracted from metadata
    );
    
    if (success) {
        LogPrintf("CVM: Trust edge stored - From: %s, To: %s, Weight: %d, Bond: %s\n",
                  HexStr(trustData.fromAddress), HexStr(trustData.toAddress),
                  trustData.weight, FormatMoney(trustData.bondAmount));
    } else {
        LogPrintf("CVM: Warning: Failed to store trust edge for tx %s\n",
                  tx.GetHash().ToString());
    }
    
    return success;
}

bool CVMBlockProcessor::ProcessBondedVote(
    const CVMBondedVoteData& voteData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db
) {
    LogPrintf("CVM: Processing bonded vote: %s votes %+d on %s\n", 
             HexStr(voteData.voter), voteData.voteValue, 
             HexStr(voteData.target));
    
    // Validate bond output
    if (!ValidateBond(tx, voteData.bondAmount)) {
        LogPrintf("CVM Warning: Invalid bond amount in bonded vote tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }
    
    // Create bonded vote record
    TrustGraph trustGraph(db);
    BondedVote vote;
    vote.voter = voteData.voter;
    vote.target = voteData.target;
    vote.voteValue = voteData.voteValue;
    vote.bondAmount = voteData.bondAmount;
    vote.bondTxHash = tx.GetHash();
    vote.timestamp = voteData.timestamp;
    vote.slashed = false;
    vote.reason = ""; // Could be extracted from metadata
    
    // Store vote
    bool success = trustGraph.RecordBondedVote(vote);
    
    if (success) {
        LogPrintf("CVM: Bonded vote stored - Voter: %s, Target: %s, Value: %+d, Bond: %s\n",
                  HexStr(voteData.voter), HexStr(voteData.target),
                  voteData.voteValue, FormatMoney(voteData.bondAmount));
    } else {
        LogPrintf("CVM: Warning: Failed to store bonded vote for tx %s\n",
                  tx.GetHash().ToString());
    }
    
    return success;
}

bool CVMBlockProcessor::ProcessDAODispute(
    const CVMDAODisputeData& disputeData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db
) {
    LogPrintf("CVM: Processing DAO dispute for vote %s\n", 
             disputeData.originalVoteTxHash.ToString());
    
    // Validate challenge bond
    if (!ValidateBond(tx, disputeData.challengeBond)) {
        LogPrintf("CVM Warning: Invalid challenge bond in dispute tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }
    
    // Create dispute record
    TrustGraph trustGraph(db);
    DAODispute dispute;
    dispute.disputeId = tx.GetHash();  // Use tx hash as dispute ID
    dispute.originalVoteTx = disputeData.originalVoteTxHash;
    dispute.challenger = disputeData.challenger;
    dispute.challengeBond = disputeData.challengeBond;
    dispute.createdTime = disputeData.timestamp;
    dispute.resolved = false;
    dispute.slashDecision = false;
    dispute.resolvedTime = 0;
    dispute.challengeReason = ""; // Could be extracted from metadata
    
    // Store dispute
    bool success = trustGraph.CreateDispute(dispute);
    
    if (success) {
        LogPrintf("CVM: DAO dispute created - ID: %s, Vote: %s, Challenger: %s\n",
                  tx.GetHash().ToString(), disputeData.originalVoteTxHash.ToString(),
                  disputeData.challenger.ToString());
    } else {
        LogPrintf("CVM Warning: Failed to create DAO dispute for tx %s\n",
                  tx.GetHash().ToString());
    }
    
    return success;
}

bool CVMBlockProcessor::ProcessDAOVote(
    const CVMDAOVoteData& voteData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db
) {
    LogPrintf("CVM: Processing DAO vote on dispute %s\n", 
             voteData.disputeId.ToString());
    
    // Store DAO vote
    TrustGraph trustGraph(db);
    bool success = trustGraph.VoteOnDispute(
        voteData.disputeId,
        voteData.daoMember,
        voteData.supportSlash,
        voteData.stake
    );
    
    if (success) {
        LogPrintf("CVM: DAO vote recorded - Dispute: %s, Member: %s, Slash: %s, Stake: %s\n",
                  voteData.disputeId.ToString(), voteData.daoMember.ToString(),
                  voteData.supportSlash ? "YES" : "NO", FormatMoney(voteData.stake));
        
        // Check if dispute can be resolved
        DAODispute dispute;
        if (trustGraph.GetDispute(voteData.disputeId, dispute)) {
            // Calculate total stake
            CAmount totalStake = 0;
            for (const auto& stake : dispute.daoStakes) {
                totalStake += stake.second;
            }
            
            // Resolve if enough DAO members have voted (minimum 5 votes or 5 CAS)
            if (dispute.daoVotes.size() >= 5 || totalStake >= 5 * COIN) {
                trustGraph.ResolveDispute(voteData.disputeId);
                LogPrintf("CVM: DAO dispute %s resolved\n", voteData.disputeId.ToString());
            }
        }
    } else {
        LogPrintf("CVM Warning: Failed to record DAO vote for tx %s\n",
                  tx.GetHash().ToString());
    }
    
    return success;
}

bool CVMBlockProcessor::ValidateBond(
    const CTransaction& tx,
    CAmount expectedBond
) {
    // Look for bond output
    // Bond output is typically output #1 (after OP_RETURN at #0)
    if (tx.vout.size() < 2) {
        return false; // No bond output
    }
    
    // Check output #1 (bond output)
    const CTxOut& bondOutput = tx.vout[1];
    
    // Verify amount
    if (bondOutput.nValue < expectedBond) {
        LogPrintf("CVM: Bond validation failed - Expected: %s, Found: %s\n",
                  FormatMoney(expectedBond), FormatMoney(bondOutput.nValue));
        return false;
    }
    
    // Verify it's a P2SH (bond script)
    // Bond scripts should be P2SH
    if (bondOutput.scriptPubKey.size() != 23 ||  // P2SH size
        bondOutput.scriptPubKey[0] != OP_HASH160 ||
        bondOutput.scriptPubKey[22] != OP_EQUAL) {
        LogPrintf("CVM: Bond validation failed - Not a P2SH script\n");
        return false;
    }
    
    return true;
}

uint32_t CVMBlockProcessor::ProcessClusterUpdates(
    const CBlock& block,
    int height,
    CVMDatabase& db
) {
    // Requirements: 2.3, 2.4
    // 2.3: When a new address inherits trust, emit an event for audit purposes
    // 2.4: While processing new blocks, check for new addresses joining existing clusters
    
    // Check if cluster update handler is initialized
    if (!g_clusterUpdateHandler) {
        LogPrint(BCLog::CVM, "CVM: ClusterUpdateHandler not initialized, skipping cluster updates\n");
        return 0;
    }
    
    // Convert block transactions to vector of CTransaction references
    std::vector<CTransaction> transactions;
    transactions.reserve(block.vtx.size());
    
    for (const auto& tx : block.vtx) {
        if (!tx->IsCoinBase()) {
            transactions.push_back(*tx);
        }
    }
    
    // Process cluster updates
    uint32_t updateCount = g_clusterUpdateHandler->ProcessBlock(height, transactions);
    
    if (updateCount > 0) {
        LogPrintf("CVM: Processed %u cluster updates at height %d\n", updateCount, height);
    }
    
    return updateCount;
}

bool InitTrustPropagation(CVMDatabase& db)
{
    LogPrintf("CVM: Initializing trust propagation components...\n");
    
    try {
        // Initialize TrustGraph
        g_trustGraph.reset(new TrustGraph(db));
        LogPrint(BCLog::CVM, "CVM: TrustGraph initialized\n");
        
        // Initialize WalletClusterer
        g_walletClusterer.reset(new WalletClusterer(db));
        LogPrint(BCLog::CVM, "CVM: WalletClusterer initialized\n");
        
        // Initialize TrustPropagator
        g_trustPropagator.reset(new TrustPropagator(db, *g_walletClusterer, *g_trustGraph));
        LogPrint(BCLog::CVM, "CVM: TrustPropagator initialized\n");
        
        // Initialize ClusterUpdateHandler
        g_clusterUpdateHandler.reset(new ClusterUpdateHandler(db, *g_walletClusterer, *g_trustPropagator));
        LogPrint(BCLog::CVM, "CVM: ClusterUpdateHandler initialized\n");
        
        LogPrintf("CVM: Trust propagation components initialized successfully\n");
        return true;
        
    } catch (const std::exception& e) {
        LogPrintf("CVM: ERROR - Failed to initialize trust propagation: %s\n", e.what());
        
        // Clean up any partially initialized components
        g_clusterUpdateHandler.reset();
        g_trustPropagator.reset();
        g_walletClusterer.reset();
        g_trustGraph.reset();
        
        return false;
    }
}

void ShutdownTrustPropagation()
{
    LogPrintf("CVM: Shutting down trust propagation components...\n");
    
    // Shutdown in reverse order of initialization
    if (g_clusterUpdateHandler) {
        // Save any pending state
        g_clusterUpdateHandler->SaveKnownMemberships();
        g_clusterUpdateHandler.reset();
        LogPrint(BCLog::CVM, "CVM: ClusterUpdateHandler shutdown\n");
    }
    
    if (g_trustPropagator) {
        g_trustPropagator.reset();
        LogPrint(BCLog::CVM, "CVM: TrustPropagator shutdown\n");
    }
    
    if (g_walletClusterer) {
        g_walletClusterer->SaveClusters();
        g_walletClusterer.reset();
        LogPrint(BCLog::CVM, "CVM: WalletClusterer shutdown\n");
    }
    
    if (g_trustGraph) {
        g_trustGraph.reset();
        LogPrint(BCLog::CVM, "CVM: TrustGraph shutdown\n");
    }
    
    LogPrintf("CVM: Trust propagation components shutdown complete\n");
}

} // namespace CVM

