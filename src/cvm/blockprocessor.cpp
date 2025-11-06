// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/blockprocessor.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/contract.h>
#include <cvm/trustgraph.h>
#include <util.h>
#include <utilmoneystr.h>
#include <hash.h>

namespace CVM {

void CVMBlockProcessor::ProcessBlock(
    const CBlock& block,
    int height,
    CVMDatabase& db
) {
    int cvmTxCount = 0;
    
    // Process all transactions in block
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check if transaction contains CVM data
        int cvmOutputIndex = FindCVMOpReturn(*tx);
        
        if (cvmOutputIndex >= 0) {
            ProcessTransaction(*tx, height, db);
            cvmTxCount++;
        }
    }
    
    if (cvmTxCount > 0) {
        LogPrintf("CVM: Processed %d CVM transactions in block %d\n", 
                  cvmTxCount, height);
    }
}

void CVMBlockProcessor::ProcessTransaction(
    const CTransaction& tx,
    int height,
    CVMDatabase& db
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
                ProcessDeploy(deployData, tx, height, db);
            } else {
                LogPrintf("CVM Warning: Invalid deploy data in tx %s\n", 
                          tx.GetHash().ToString());
            }
            break;
        }
        
        case CVMOpType::CONTRACT_CALL: {
            CVMCallData callData;
            if (callData.Deserialize(data)) {
                ProcessCall(callData, tx, height, db);
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
    CVMDatabase& db
) {
    LogPrint(BCLog::ALL, "CVM: Processing contract deployment: hash=%s\n", 
             deployData.codeHash.ToString());
    
    // Generate contract address from tx hash + output index
    // (Similar to Ethereum contract address generation)
    uint160 contractAddress;
    {
        CHashWriter hw(SER_GETHASH, 0);
        hw << tx.GetHash();
        hw << uint32_t(0); // Output index
        uint256 hash = hw.GetHash();
        memcpy(contractAddress.begin(), hash.begin(), 20);
    }
    
    // Create contract record
    Contract contract;
    contract.address = contractAddress;
    contract.deploymentHeight = height;
    contract.deploymentTx = tx.GetHash();
    // Note: Actual bytecode would be stored separately or in witness data
    // For now, we just store the hash
    
    // Store contract
    db.WriteContract(contractAddress, contract);
    
    LogPrintf("CVM: Contract deployed - Address: %s, CodeHash: %s, Height: %d\n",
              contractAddress.ToString(), deployData.codeHash.ToString(), height);
}

void CVMBlockProcessor::ProcessCall(
    const CVMCallData& callData,
    const CTransaction& tx,
    int height,
    CVMDatabase& db
) {
    LogPrint(BCLog::ALL, "CVM: Processing contract call to %s\n", 
             callData.contractAddress.ToString());
    
    // Check if contract exists
    if (!db.Exists(callData.contractAddress)) {
        LogPrintf("CVM Warning: Call to non-existent contract %s in tx %s\n",
                  callData.contractAddress.ToString(), tx.GetHash().ToString());
        return;
    }
    
    // TODO: Execute contract call
    // For now, just log it
    LogPrintf("CVM: Contract call - Contract: %s, GasLimit: %d, Tx: %s\n",
              callData.contractAddress.ToString(), callData.gasLimit, 
              tx.GetHash().ToString());
    
    // In full implementation:
    // 1. Load contract bytecode
    // 2. Execute with CVM
    // 3. Update storage
    // 4. Consume gas
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

} // namespace CVM

