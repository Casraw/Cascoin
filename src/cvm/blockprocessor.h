// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_BLOCKPROCESSOR_H
#define CASCOIN_CVM_BLOCKPROCESSOR_H

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <memory>

namespace CVM {

class CVMDatabase;
class TrustGraph;
class WalletClusterer;
class TrustPropagator;
class ClusterUpdateHandler;
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
    
    /**
     * Process cluster updates after CVM transactions
     * 
     * Detects new cluster members and triggers trust inheritance.
     * Called after ProcessBlock() to handle wallet trust propagation.
     * 
     * @param block Block that was processed
     * @param height Block height
     * @param db CVM database
     * @return Number of cluster updates processed
     * 
     * Requirements: 2.3, 2.4
     */
    static uint32_t ProcessClusterUpdates(
        const CBlock& block,
        int height,
        CVMDatabase& db
    );
};

/**
 * Global trust propagation components
 * 
 * These are initialized when the CVM database is initialized and
 * used during block processing for wallet trust propagation.
 * 
 * Requirements: 2.4, 16.1
 */
extern std::unique_ptr<TrustGraph> g_trustGraph;
extern std::unique_ptr<WalletClusterer> g_walletClusterer;
extern std::unique_ptr<TrustPropagator> g_trustPropagator;
extern std::unique_ptr<ClusterUpdateHandler> g_clusterUpdateHandler;

/**
 * Initialize trust propagation components
 * 
 * Called after CVM database is initialized to set up the
 * wallet trust propagation system.
 * 
 * @param db Reference to CVM database
 * @return true if initialization successful
 */
bool InitTrustPropagation(CVMDatabase& db);

/**
 * Shutdown trust propagation components
 * 
 * Called during shutdown to clean up resources.
 */
void ShutdownTrustPropagation();

} // namespace CVM

#endif // CASCOIN_CVM_BLOCKPROCESSOR_H

