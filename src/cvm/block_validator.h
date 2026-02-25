// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_BLOCK_VALIDATOR_H
#define CASCOIN_CVM_BLOCK_VALIDATOR_H

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <coins.h>
#include <amount.h>
#include <uint256.h>
#include <cvm/softfork.h>
#include <cvm/enhanced_vm.h>
#include <cvm/fee_calculator.h>
#include <cvm/gas_subsidy.h>
#include <cvm/receipt.h>

class CValidationState;
class CBlockIndex;

namespace Consensus {
    struct Params;
}

namespace CVM {

// Forward declarations
class CVMDatabase;
class EnhancedVM;
class TrustContext;

/**
 * Block validation result for CVM/EVM transactions
 */
struct BlockValidationResult {
    bool success;
    uint64_t totalGasUsed;
    CAmount totalFees;
    uint64_t contractsExecuted;
    uint64_t contractsDeployed;
    std::string error;
    
    BlockValidationResult()
        : success(true), totalGasUsed(0), totalFees(0),
          contractsExecuted(0), contractsDeployed(0) {}
};

/**
 * CVM/EVM Block Validator
 * 
 * Validates and executes CVM/EVM transactions during block connection.
 * 
 * Features:
 * - Contract execution during block validation
 * - Gas limit enforcement (10M gas per block)
 * - Reputation-based gas cost verification
 * - Atomic rollback for failed executions
 * - UTXO set updates based on execution results
 * - Contract state storage in database
 * 
 * Requirements: 1.1, 10.1, 10.2
 */
class BlockValidator {
public:
    BlockValidator();
    ~BlockValidator();
    
    /**
     * Initialize block validator
     * 
     * @param db CVM database
     */
    void Initialize(CVMDatabase* db);
    
    // ===== Block Validation =====
    
    /**
     * Validate and execute CVM/EVM transactions in block
     * 
     * This is called from ConnectBlock() after standard transaction validation.
     * 
     * Process:
     * 1. Identify CVM/EVM transactions
     * 2. Execute contracts in order
     * 3. Enforce gas limits (10M per block)
     * 4. Verify reputation-based gas costs
     * 5. Update UTXO set based on results
     * 6. Store contract state changes
     * 7. Rollback on failure
     * 
     * @param block Block to validate
     * @param state Validation state
     * @param pindex Block index
     * @param view Coins view for UTXO access
     * @param chainparams Chain parameters
     * @param fJustCheck If true, don't modify state (for testing)
     * @return Validation result
     */
    BlockValidationResult ValidateBlock(
        const CBlock& block,
        CValidationState& state,
        CBlockIndex* pindex,
        CCoinsViewCache& view,
        const Consensus::Params& chainparams,
        bool fJustCheck = false
    );
    
    /**
     * Execute single CVM/EVM transaction
     * 
     * @param tx Transaction to execute
     * @param txIndex Transaction index in block
     * @param blockHeight Block height
     * @param view Coins view
     * @param gasUsed Output: gas used by transaction
     * @param error Output: error message if execution fails
     * @return true if execution succeeded
     */
    bool ExecuteTransaction(
        const CTransaction& tx,
        unsigned int txIndex,
        int blockHeight,
        CCoinsViewCache& view,
        uint64_t& gasUsed,
        std::string& error
    );
    
    // ===== Gas Limit Enforcement =====
    
    /**
     * Check if transaction would exceed block gas limit
     * 
     * @param currentGasUsed Gas already used in block
     * @param txGasLimit Gas limit for transaction
     * @return true if within limit
     */
    bool CheckBlockGasLimit(uint64_t currentGasUsed, uint64_t txGasLimit) const;
    
    /**
     * Get maximum gas per block
     * 
     * @return Maximum gas (10M)
     */
    uint64_t GetMaxBlockGas() const { return MAX_BLOCK_GAS; }
    
    // ===== Reputation-Based Gas Verification =====
    
    /**
     * Verify reputation-based gas costs for transaction
     * 
     * Ensures:
     * - Free gas eligibility is valid (80+ reputation)
     * - Gas discounts match reputation
     * - Gas subsidies are properly applied
     * 
     * @param tx Transaction
     * @param blockHeight Block height
     * @return true if gas costs are valid
     */
    bool VerifyReputationGasCosts(
        const CTransaction& tx,
        int blockHeight
    );
    
    // ===== Contract Deployment =====
    
    /**
     * Deploy contract from transaction
     * 
     * @param tx Transaction containing deployment
     * @param blockHeight Block height
     * @param view Coins view
     * @param gasUsed Output: gas used
     * @param contractAddr Output: deployed contract address
     * @param error Output: error message if deployment fails
     * @return true if deployment succeeded
     */
    bool DeployContract(
        const CTransaction& tx,
        int blockHeight,
        CCoinsViewCache& view,
        uint64_t& gasUsed,
        uint160& contractAddr,
        std::string& error
    );
    
    // ===== Contract Execution =====
    
    /**
     * Execute contract call from transaction
     * 
     * @param tx Transaction containing call
     * @param blockHeight Block height
     * @param view Coins view
     * @param gasUsed Output: gas used
     * @param error Output: error message if call fails
     * @return true if call succeeded
     */
    bool ExecuteContractCall(
        const CTransaction& tx,
        int blockHeight,
        CCoinsViewCache& view,
        uint64_t& gasUsed,
        std::string& error
    );
    
    // ===== State Management =====
    
    /**
     * Save contract state changes to database
     * 
     * @param fJustCheck If true, don't actually save (for testing)
     * @return true if save succeeded
     */
    bool SaveContractState(bool fJustCheck);
    
    /**
     * Rollback contract state changes
     * 
     * Called when block validation fails.
     */
    void RollbackContractState();
    
    // ===== Gas Subsidy Distribution =====
    
    /**
     * Distribute gas subsidies for block
     * 
     * Called after all transactions are executed.
     * 
     * @param block Block
     * @param blockHeight Block height
     * @return true if distribution succeeded
     */
    bool DistributeGasSubsidies(
        const CBlock& block,
        int blockHeight
    );
    
    /**
     * Process gas rebates (10 block confirmation)
     * 
     * @param blockHeight Current block height
     * @return true if processing succeeded
     */
    bool ProcessGasRebates(int blockHeight);
    
    // ===== Statistics =====
    
    /**
     * Get validation statistics
     * 
     * @return Validation result with statistics
     */
    const BlockValidationResult& GetLastResult() const { return m_lastResult; }
    
    // ===== HAT v2 Consensus Integration =====
    
    /**
     * Set HAT consensus validator
     * 
     * @param validator HAT consensus validator instance
     */
    void SetHATConsensusValidator(class HATConsensusValidator* validator);
    
    /**
     * Validate block transactions have completed HAT consensus
     * 
     * Checks that all CVM/EVM transactions in block have:
     * - Completed HAT v2 validation (not PENDING_VALIDATION)
     * - Been approved (VALIDATED state, not REJECTED or DISPUTED)
     * 
     * @param block Block to validate
     * @param error Output error message
     * @return true if all transactions validated
     */
    bool ValidateBlockHATConsensus(const CBlock& block, std::string& error);
    
    /**
     * Record fraud attempts in block
     * 
     * Adds fraud records as special transactions in block.
     * Called during block creation (mining).
     * 
     * @param block Block to add fraud records to
     * @param fraudRecords Fraud records to include
     * @return true if records added successfully
     */
    bool RecordFraudInBlock(CBlock& block, const std::vector<struct FraudRecord>& fraudRecords);
    
    /**
     * Extract fraud records from block
     * 
     * Parses fraud records from block transactions.
     * Called during block validation.
     * 
     * @param block Block to extract from
     * @return Vector of fraud records
     */
    std::vector<struct FraudRecord> ExtractFraudRecords(const CBlock& block);
    
private:
    CVMDatabase* m_db;
    std::shared_ptr<TrustContext> m_trustContext;
    std::unique_ptr<EnhancedVM> m_vm;
    std::unique_ptr<FeeCalculator> m_feeCalculator;
    std::unique_ptr<GasSubsidyTracker> m_gasSubsidyTracker;
    class HATConsensusValidator* m_hatValidator;  // HAT consensus validator
    
    BlockValidationResult m_lastResult;
    
    // Gas limit constants
    static constexpr uint64_t MAX_BLOCK_GAS = 10000000; // 10M gas per block
    static constexpr uint64_t MAX_TX_GAS = 1000000;     // 1M gas per transaction
    
    /**
     * Check if CVM soft fork is active
     * 
     * @param blockHeight Block height
     * @param chainparams Chain parameters
     * @return true if active
     */
    bool IsCVMActive(int blockHeight, const Consensus::Params& chainparams);
    
    /**
     * Extract gas limit from transaction
     * 
     * @param tx Transaction
     * @return Gas limit, or 0 if not CVM/EVM transaction
     */
    uint64_t ExtractGasLimit(const CTransaction& tx);
    
    /**
     * Get sender address from transaction
     * 
     * @param tx Transaction
     * @param view Coins view for input lookup
     * @return Sender address
     */
    uint160 GetSenderAddress(const CTransaction& tx, CCoinsViewCache& view);
};

} // namespace CVM

#endif // CASCOIN_CVM_BLOCK_VALIDATOR_H
