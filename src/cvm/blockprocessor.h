// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_BLOCKPROCESSOR_H
#define CASCOIN_CVM_BLOCKPROCESSOR_H

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>

namespace CVM {

class CVMDatabase;
class TrustGraph;
struct CVMReputationData;
struct CVMDeployData;
struct CVMCallData;
struct CVMTrustEdgeData;
struct CVMBondedVoteData;
struct CVMDAODisputeData;
struct CVMDAOVoteData;

/**
 * CVM Block Processor
 * 
 * Processes CVM transactions found in blocks.
 * This runs during block validation ONLY on new nodes (soft fork).
 * Old nodes skip this entirely and just see OP_RETURN outputs.
 */
class CVMBlockProcessor {
public:
    /**
     * Process all CVM transactions in a block
     * 
     * Called during ConnectBlock() to update CVM state.
     * Only called on nodes with CVM active.
     * 
     * @param block Block to process
     * @param height Block height
     * @param db CVM database to update
     */
    static void ProcessBlock(
        const CBlock& block,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process single transaction for CVM operations
     * 
     * Looks for OP_RETURN outputs with CVM data and processes them.
     * 
     * @param tx Transaction to process
     * @param height Block height
     * @param db CVM database to update
     */
    static void ProcessTransaction(
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );

private:
    /**
     * Process reputation vote transaction
     * 
     * @param voteData Vote data from OP_RETURN
     * @param tx Transaction containing vote
     * @param height Block height
     * @param db CVM database to update
     */
    static void ProcessVote(
        const CVMReputationData& voteData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process contract deployment transaction
     * 
     * @param deployData Deployment data from OP_RETURN
     * @param tx Transaction containing deployment
     * @param height Block height
     * @param db CVM database to update
     */
    static void ProcessDeploy(
        const CVMDeployData& deployData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process contract call transaction
     * 
     * @param callData Call data from OP_RETURN
     * @param tx Transaction containing call
     * @param height Block height
     * @param db CVM database to update
     */
    static void ProcessCall(
        const CVMCallData& callData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process trust edge transaction (Web-of-Trust)
     * 
     * @param trustData Trust edge data from OP_RETURN
     * @param tx Transaction containing trust edge
     * @param height Block height
     * @param db CVM database to update
     * @return true if processed successfully
     */
    static bool ProcessTrustEdge(
        const CVMTrustEdgeData& trustData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process bonded vote transaction (Web-of-Trust)
     * 
     * @param voteData Bonded vote data from OP_RETURN
     * @param tx Transaction containing bonded vote
     * @param height Block height
     * @param db CVM database to update
     * @return true if processed successfully
     */
    static bool ProcessBondedVote(
        const CVMBondedVoteData& voteData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process DAO dispute transaction (Web-of-Trust)
     * 
     * @param disputeData Dispute data from OP_RETURN
     * @param tx Transaction containing dispute
     * @param height Block height
     * @param db CVM database to update
     * @return true if processed successfully
     */
    static bool ProcessDAODispute(
        const CVMDAODisputeData& disputeData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Process DAO vote transaction (Web-of-Trust)
     * 
     * @param voteData DAO vote data from OP_RETURN
     * @param tx Transaction containing DAO vote
     * @param height Block height
     * @param db CVM database to update
     * @return true if processed successfully
     */
    static bool ProcessDAOVote(
        const CVMDAOVoteData& voteData,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    /**
     * Validate bond output in transaction
     * 
     * Checks that the transaction has a bond output with the correct amount.
     * 
     * @param tx Transaction to validate
     * @param expectedBond Expected bond amount
     * @return true if bond output found and valid
     */
    static bool ValidateBond(
        const CTransaction& tx,
        CAmount expectedBond
    );
};

} // namespace CVM

#endif // CASCOIN_CVM_BLOCKPROCESSOR_H

