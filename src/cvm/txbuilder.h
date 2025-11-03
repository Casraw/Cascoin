// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TXBUILDER_H
#define CASCOIN_CVM_TXBUILDER_H

#include <primitives/transaction.h>
#include <amount.h>
#include <uint256.h>
#include <vector>
#include <string>

class CWallet;
class COutput;
class CCoinControl;

namespace CVM {

/**
 * CVM Transaction Builder
 * 
 * Builds real Bitcoin transactions with CVM data in OP_RETURN outputs.
 * This makes CVM soft-fork compatible - old nodes see standard OP_RETURN,
 * new nodes parse and validate CVM data.
 */
class CVMTransactionBuilder {
public:
    /**
     * Build reputation vote transaction
     * 
     * Creates a transaction with:
     * - Input(s): Funding from wallet
     * - Output 0: OP_RETURN with vote data
     * - Output 1: Change back to wallet
     * 
     * @param wallet Wallet to fund transaction
     * @param targetAddress Address being voted on
     * @param voteValue Vote value (-100 to +100)
     * @param reason Human-readable reason
     * @param fee Returns calculated fee
     * @param error Returns error message if fails
     * @return Transaction (empty if failed)
     */
    static CMutableTransaction BuildVoteTransaction(
        CWallet* wallet,
        const uint160& targetAddress,
        int16_t voteValue,
        const std::string& reason,
        CAmount& fee,
        std::string& error
    );
    
    /**
     * Build contract deployment transaction
     * 
     * Creates a transaction with:
     * - Input(s): Funding from wallet
     * - Output 0: OP_RETURN with deployment metadata
     * - Output 1: Contract address (P2SH)
     * - Output 2: Change back to wallet
     * 
     * @param wallet Wallet to fund transaction
     * @param bytecode Contract bytecode
     * @param gasLimit Gas limit for deployment
     * @param fee Returns calculated fee
     * @param error Returns error message if fails
     * @return Transaction (empty if failed)
     */
    static CMutableTransaction BuildDeployTransaction(
        CWallet* wallet,
        const std::vector<uint8_t>& bytecode,
        uint64_t gasLimit,
        CAmount& fee,
        std::string& error
    );
    
    /**
     * Build contract call transaction
     * 
     * Creates a transaction with:
     * - Input(s): Funding from wallet
     * - Output 0: OP_RETURN with call data
     * - Output 1: Value to contract (if any)
     * - Output 2: Change back to wallet
     * 
     * @param wallet Wallet to fund transaction
     * @param contractAddress Target contract
     * @param callData Function call data
     * @param gasLimit Gas limit for call
     * @param value Amount to send to contract
     * @param fee Returns calculated fee
     * @param error Returns error message if fails
     * @return Transaction (empty if failed)
     */
    static CMutableTransaction BuildCallTransaction(
        CWallet* wallet,
        const uint160& contractAddress,
        const std::vector<uint8_t>& callData,
        uint64_t gasLimit,
        CAmount value,
        CAmount& fee,
        std::string& error
    );
    
    /**
     * Build trust edge transaction (Web-of-Trust)
     * 
     * Creates a transaction with:
     * - Input(s): Funding from wallet (fee + bond)
     * - Output 0: OP_RETURN with trust edge data
     * - Output 1: Bond output (locked P2SH with timelock)
     * - Output 2: Change back to wallet
     * 
     * @param wallet Wallet to fund transaction
     * @param toAddress Address being trusted
     * @param weight Trust weight (-100 to +100)
     * @param bondAmount CAS to lock as bond
     * @param reason Human-readable reason
     * @param fee Returns calculated fee
     * @param error Returns error message if fails
     * @return Transaction (empty if failed)
     */
    static CMutableTransaction BuildTrustTransaction(
        CWallet* wallet,
        const uint160& toAddress,
        int16_t weight,
        CAmount bondAmount,
        const std::string& reason,
        CAmount& fee,
        std::string& error
    );
    
    /**
     * Build bonded reputation vote transaction (Web-of-Trust)
     * 
     * Creates a transaction with:
     * - Input(s): Funding from wallet (fee + bond)
     * - Output 0: OP_RETURN with bonded vote data
     * - Output 1: Bond output (locked P2SH with timelock)
     * - Output 2: Change back to wallet
     * 
     * @param wallet Wallet to fund transaction
     * @param targetAddress Address being voted on
     * @param voteValue Vote value (-100 to +100)
     * @param bondAmount CAS to lock as bond
     * @param reason Human-readable reason
     * @param fee Returns calculated fee
     * @param error Returns error message if fails
     * @return Transaction (empty if failed)
     */
    static CMutableTransaction BuildBondedVoteTransaction(
        CWallet* wallet,
        const uint160& targetAddress,
        int16_t voteValue,
        CAmount bondAmount,
        const std::string& reason,
        CAmount& fee,
        std::string& error
    );
    
    /**
     * Sign a CVM transaction
     * 
     * @param wallet Wallet with keys
     * @param tx Transaction to sign
     * @param error Returns error message if fails
     * @return true if signed successfully
     */
    static bool SignTransaction(
        CWallet* wallet,
        CMutableTransaction& tx,
        std::string& error
    );
    
    /**
     * Broadcast transaction to network
     * 
     * @param tx Transaction to broadcast
     * @param txid Returns transaction ID
     * @param error Returns error message if fails
     * @return true if broadcast successfully
     */
    static bool BroadcastTransaction(
        const CTransaction& tx,
        uint256& txid,
        std::string& error
    );

private:
    /**
     * Select UTXOs from wallet to fund transaction
     * 
     * @param wallet Wallet to select from
     * @param amount Amount needed (plus estimated fee)
     * @param coinControl Optional coin control
     * @param selected Returns selected outputs
     * @param total Returns total selected amount
     * @return true if sufficient coins selected
     */
    static bool SelectCoins(
        CWallet* wallet,
        CAmount amount,
        const CCoinControl* coinControl,
        std::vector<COutput>& selected,
        CAmount& total
    );
    
    /**
     * Add change output to transaction
     * 
     * @param tx Transaction to add change to
     * @param wallet Wallet for change address
     * @param change Change amount
     * @return true if change added (false if dust)
     */
    static bool AddChangeOutput(
        CMutableTransaction& tx,
        CWallet* wallet,
        CAmount change
    );
    
    /**
     * Calculate transaction fee
     * Uses standard fee estimation based on transaction size
     * 
     * @param tx Transaction to calculate fee for
     * @return Estimated fee
     */
    static CAmount CalculateFee(const CMutableTransaction& tx);
    
    /**
     * Get dust threshold for output
     * 
     * @param scriptPubKey Output script
     * @return Minimum amount to avoid dust
     */
    static CAmount GetDustThreshold(const CScript& scriptPubKey);
    
    /**
     * Create bond locking script (P2SH with timelock)
     * 
     * Script allows:
     * - User to reclaim after locktime (1440 blocks = ~1 day)
     * - DAO to slash before locktime (if malicious)
     * 
     * @param userPubKeyHash User's address (can reclaim after timeout)
     * @param lockBlocks Number of blocks to lock (default 1440)
     * @return P2SH script for bond locking
     */
    static CScript CreateBondScript(
        const uint160& userPubKeyHash,
        int lockBlocks = 1440
    );
    
    /**
     * Add bond output to transaction
     * 
     * @param tx Transaction to add bond to
     * @param bondScript P2SH bond locking script
     * @param bondAmount Amount to lock
     * @return true if bond added successfully
     */
    static bool AddBondOutput(
        CMutableTransaction& tx,
        const CScript& bondScript,
        CAmount bondAmount
    );
};

} // namespace CVM

#endif // CASCOIN_CVM_TXBUILDER_H

