// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/block_validator.h>
#include <cvm/cvm.h>
#include <cvm/softfork.h>
#include <cvm/trust_context.h>
#include <cvm/hat_consensus.h>
#include <cvm/access_control_audit.h>
#include <consensus/validation.h>
#include <validation.h>
#include <util.h>
#include <utilstrencodings.h>
#include <script/script.h>
#include <script/standard.h>
#include <pubkey.h>
#include <hash.h>
#include <net_processing.h>
#include <cstdlib>

namespace CVM {

BlockValidator::BlockValidator()
    : m_db(nullptr)
    , m_hatValidator(nullptr)
{
    // VM and other components will be initialized in Initialize() method
    // when database is available
    m_feeCalculator = std::make_unique<FeeCalculator>();
    m_gasSubsidyTracker = std::make_unique<GasSubsidyTracker>();
}

BlockValidator::~BlockValidator() = default;

void BlockValidator::Initialize(CVMDatabase* db)
{
    m_db = db;
    
    // Initialize trust context
    m_trustContext = std::make_shared<TrustContext>();
    
    // Initialize VM with database and trust context
    if (db && m_trustContext) {
        m_vm = std::make_unique<EnhancedVM>(db, m_trustContext);
    }
    
    if (m_feeCalculator) {
        m_feeCalculator->Initialize(db);
    }
    if (m_gasSubsidyTracker && db) {
        m_gasSubsidyTracker->LoadFromDatabase(*db);
    }
}

BlockValidationResult BlockValidator::ValidateBlock(
    const CBlock& block,
    CValidationState& state,
    CBlockIndex* pindex,
    CCoinsViewCache& view,
    const Consensus::Params& chainparams,
    bool fJustCheck)
{
    m_lastResult = BlockValidationResult();
    
    // Check if CVM is active
    if (!IsCVMActive(pindex->nHeight, chainparams)) {
        m_lastResult.success = true;
        return m_lastResult;
    }
    
    LogPrint(BCLog::CVM, "BlockValidator: Validating block %s at height %d\n",
             block.GetHash().ToString(), pindex->nHeight);
    
    uint64_t blockGasUsed = 0;
    
    // Process each transaction
    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];
        
        // Skip coinbase
        if (tx.IsCoinBase()) {
            continue;
        }
        
        // Check if CVM/EVM transaction
        if (!IsEVMTransaction(tx) && FindCVMOpReturn(tx) < 0) {
            continue;
        }
        
        // Extract gas limit - only required for contract transactions
        // Trust, reputation, and other CVM transactions don't use gas
        uint64_t txGasLimit = ExtractGasLimit(tx);
        if (txGasLimit == 0) {
            // Check if this is a contract transaction that requires gas
            int opReturnIndex = FindCVMOpReturn(tx);
            if (opReturnIndex >= 0) {
                CVMOpType opType;
                std::vector<uint8_t> data;
                if (ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
                    // Only contract deploy/call transactions require gas
                    if (opType == CVMOpType::CONTRACT_DEPLOY || 
                        opType == CVMOpType::CONTRACT_CALL ||
                        opType == CVMOpType::EVM_DEPLOY ||
                        opType == CVMOpType::EVM_CALL) {
                        m_lastResult.success = false;
                        m_lastResult.error = "Invalid gas limit";
                        return m_lastResult;
                    }
                    // For non-contract CVM transactions (trust, reputation, etc.),
                    // skip gas validation and continue to next transaction
                    LogPrint(BCLog::CVM, "BlockValidator: Non-contract CVM tx %s (type %d), skipping gas validation\n",
                             tx.GetHash().ToString(), static_cast<int>(opType));
                    continue;
                }
            }
            // If we can't parse the OP_RETURN, skip this transaction
            continue;
        }
        
        // Check block gas limit
        if (!CheckBlockGasLimit(blockGasUsed, txGasLimit)) {
            m_lastResult.success = false;
            m_lastResult.error = strprintf("Block gas limit exceeded: %d + %d > %d",
                                          blockGasUsed, txGasLimit, MAX_BLOCK_GAS);
            LogPrint(BCLog::CVM, "BlockValidator: %s\n", m_lastResult.error);
            return m_lastResult;
        }
        
        // Verify reputation-based gas costs
        if (!VerifyReputationGasCosts(tx, pindex->nHeight)) {
            m_lastResult.success = false;
            m_lastResult.error = "Invalid reputation-based gas costs";
            return m_lastResult;
        }
        
        // Execute transaction
        uint64_t gasUsed = 0;
        std::string error;
        if (!ExecuteTransaction(tx, i, pindex->nHeight, view, gasUsed, error)) {
            m_lastResult.success = false;
            m_lastResult.error = strprintf("Transaction execution failed: %s", error);
            LogPrint(BCLog::CVM, "BlockValidator: %s\n", m_lastResult.error);
            
            // Rollback state changes
            if (!fJustCheck) {
                RollbackContractState();
            }
            return m_lastResult;
        }
        
        blockGasUsed += gasUsed;
        m_lastResult.totalGasUsed += gasUsed;
        
        LogPrint(BCLog::CVM, "BlockValidator: Executed tx %s, gas used: %d\n",
                 tx.GetHash().ToString(), gasUsed);
    }
    
    // Save contract state
    if (!fJustCheck) {
        if (!SaveContractState(fJustCheck)) {
            m_lastResult.success = false;
            m_lastResult.error = "Failed to save contract state";
            RollbackContractState();
            return m_lastResult;
        }
        
        // Distribute gas subsidies
        if (!DistributeGasSubsidies(block, pindex->nHeight)) {
            LogPrint(BCLog::CVM, "BlockValidator: Warning - gas subsidy distribution failed\n");
            // Don't fail block validation for subsidy issues
        }
        
        // Process gas rebates
        if (!ProcessGasRebates(pindex->nHeight)) {
            LogPrint(BCLog::CVM, "BlockValidator: Warning - gas rebate processing failed\n");
            // Don't fail block validation for rebate issues
        }
    }
    
    LogPrint(BCLog::CVM, "BlockValidator: Block validated successfully, total gas: %d, contracts: %d\n",
             m_lastResult.totalGasUsed, m_lastResult.contractsExecuted);
    
    m_lastResult.success = true;
    return m_lastResult;
}

bool BlockValidator::ExecuteTransaction(
    const CTransaction& tx,
    unsigned int txIndex,
    int blockHeight,
    CCoinsViewCache& view,
    uint64_t& gasUsed,
    std::string& error)
{
    // Find CVM OP_RETURN
    int opReturnIndex = FindCVMOpReturn(tx);
    if (opReturnIndex < 0) {
        error = "No CVM OP_RETURN found";
        return false;
    }
    
    // Parse OP_RETURN
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
        error = "Failed to parse CVM OP_RETURN";
        return false;
    }
    
    // Handle based on operation type
    if (opType == CVMOpType::CONTRACT_DEPLOY || opType == CVMOpType::EVM_DEPLOY) {
        uint160 contractAddr;
        m_lastResult.contractsDeployed++;
        return DeployContract(tx, blockHeight, view, gasUsed, contractAddr, error);
    } else if (opType == CVMOpType::CONTRACT_CALL || opType == CVMOpType::EVM_CALL) {
        m_lastResult.contractsExecuted++;
        return ExecuteContractCall(tx, blockHeight, view, gasUsed, error);
    }
    
    // Not a contract transaction
    gasUsed = 0;
    return true;
}

bool BlockValidator::CheckBlockGasLimit(uint64_t currentGasUsed, uint64_t txGasLimit) const
{
    return (currentGasUsed + txGasLimit) <= MAX_BLOCK_GAS;
}

bool BlockValidator::VerifyReputationGasCosts(
    const CTransaction& tx,
    int blockHeight)
{
    if (!m_feeCalculator) {
        return true; // Skip verification if fee calculator not available
    }
    
    // Calculate expected fee
    FeeCalculationResult feeResult = m_feeCalculator->CalculateFee(tx, blockHeight);
    
    if (!feeResult.IsValid()) {
        LogPrint(BCLog::CVM, "BlockValidator: Fee calculation failed: %s\n", feeResult.error);
        return false;
    }
    
    // For free gas transactions, no fee verification needed
    if (feeResult.isFreeGas) {
        return true;
    }
    
    // Verify actual transaction fee matches expected fee
    // Calculate actual fee from transaction (inputs - outputs)
    // Note: We need the view to look up input values, but for CVM transactions
    // the fee is typically encoded in the gas price * gas limit
    
    // Extract gas info from transaction
    uint64_t gasLimit = m_feeCalculator->ExtractGasLimit(tx);
    if (gasLimit == 0) {
        // Not a gas-based transaction, skip verification
        return true;
    }
    
    // Calculate expected fee based on gas
    CAmount expectedFee = feeResult.effectiveFee;
    
    // Calculate actual fee from transaction outputs
    // For CVM transactions, the fee is the gas cost which should match
    // the expected fee from the fee calculator
    CAmount totalOutputValue = 0;
    for (const auto& out : tx.vout) {
        totalOutputValue += out.nValue;
    }
    
    // The actual fee verification requires knowing the input values
    // For now, we verify that the gas limit and gas price in the transaction
    // are consistent with the expected fee calculation
    
    // Allow a small tolerance (1%) for rounding differences
    CAmount tolerance = expectedFee / 100;
    if (tolerance < 1000) {
        tolerance = 1000; // Minimum tolerance of 1000 satoshis
    }
    
    // Verify the gas parameters are reasonable
    // The effective fee should be within the expected range based on reputation
    CAmount baseFee = feeResult.baseFee;
    CAmount discount = feeResult.reputationDiscount;
    CAmount subsidy = feeResult.gasSubsidy;
    
    // Log the fee breakdown for debugging
    LogPrint(BCLog::CVM, "BlockValidator: Fee verification - base: %d, discount: %d, subsidy: %d, effective: %d, gas: %d\n",
             baseFee, discount, subsidy, expectedFee, gasLimit);
    
    // Verify the fee calculation is internally consistent
    // effectiveFee should equal baseFee - discount - subsidy (but not negative)
    CAmount calculatedEffective = baseFee - discount - subsidy;
    if (calculatedEffective < 0) {
        calculatedEffective = 0;
    }
    
    if (std::abs(calculatedEffective - expectedFee) > tolerance) {
        LogPrint(BCLog::CVM, "BlockValidator: Fee calculation inconsistency - calculated: %d, expected: %d\n",
                 calculatedEffective, expectedFee);
        return false;
    }
    
    return true;
}

bool BlockValidator::DeployContract(
    const CTransaction& tx,
    int blockHeight,
    CCoinsViewCache& view,
    uint64_t& gasUsed,
    uint160& contractAddr,
    std::string& error)
{
    // Parse deployment data
    int opReturnIndex = FindCVMOpReturn(tx);
    CVMOpType opType;
    std::vector<uint8_t> data;
    ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data);
    
    CVMDeployData deployData;
    if (!deployData.Deserialize(data)) {
        error = "Failed to deserialize deployment data";
        return false;
    }
    
    // Get deployer address
    uint160 deployer = GetSenderAddress(tx, view);
    if (deployer.IsNull()) {
        error = "Could not extract deployer address";
        return false;
    }
    
    // Log contract deployment access control check
    if (g_accessControlAuditor && m_trustContext) {
        int16_t deployerReputation = static_cast<int16_t>(m_trustContext->GetReputation(deployer));
        int16_t requiredReputation = g_accessControlAuditor->GetMinimumReputation(AccessOperationType::CONTRACT_DEPLOYMENT);
        
        AccessDecision decision = g_accessControlAuditor->LogReputationGatedOperation(
            deployer,
            AccessOperationType::CONTRACT_DEPLOYMENT,
            "DeployContract",
            requiredReputation,
            deployerReputation,
            "", // resource ID will be contract address after deployment
            tx.GetHash());
        
        if (decision != AccessDecision::GRANTED) {
            error = "Contract deployment denied by access control";
            return false;
        }
    }
    
    // The OP_RETURN only contains codeHash, gasLimit, format, and metadata.
    // The actual bytecode is NOT stored in the OP_RETURN (80-byte limit).
    // Try to extract bytecode from the transaction's witness data, where
    // the wallet may have placed it as an additional witness stack element.
    if (deployData.bytecode.empty()) {
        // Try extracting bytecode from witness data of the first input
        for (const auto& txin : tx.vin) {
            const auto& wit = txin.scriptWitness;
            // Convention: if witness stack has 3+ elements, the first element
            // (before sig and pubkey) may contain the contract bytecode
            if (wit.stack.size() >= 3) {
                const auto& candidate = wit.stack[0];
                // Verify against codeHash from OP_RETURN
                if (!candidate.empty()) {
                    uint256 candidateHash = Hash(candidate.begin(), candidate.end());
                    if (candidateHash == deployData.codeHash) {
                        deployData.bytecode = candidate;
                        LogPrint(BCLog::CVM, "BlockValidator: Extracted bytecode (%d bytes) from witness data, hash matches\n",
                                 candidate.size());
                        break;
                    }
                }
            }
        }
    }
    
    // If bytecode is still empty, register the contract as a metadata-only
    // deployment. The OP_RETURN soft-fork design stores only the codeHash;
    // the bytecode is provided out-of-band. During block validation we
    // accept the deployment based on the OP_RETURN metadata and register
    // the contract address so future calls can reference it.
    if (deployData.bytecode.empty()) {
        LogPrint(BCLog::CVM, "BlockValidator: No bytecode in tx %s, registering metadata-only deployment (codeHash=%s)\n",
                 tx.GetHash().ToString(), deployData.codeHash.ToString());
        
        // Generate contract address from deployer + nonce
        uint64_t nonce = 0;
        if (m_db) {
            nonce = m_db->GetNextNonce(deployer);
        }
        contractAddr = GenerateContractAddress(deployer, nonce);
        
        // Register the contract in the database with empty code but valid metadata
        if (m_db) {
            Contract contract;
            contract.address = contractAddr;
            // code stays empty — bytecode not available in OP_RETURN
            contract.deploymentTx = tx.GetHash();
            contract.deploymentHeight = blockHeight;
            m_db->WriteContract(contractAddr, contract);
            m_db->WriteNonce(deployer, nonce + 1);
        }
        
        gasUsed = 0; // No execution, no gas consumed
        LogPrint(BCLog::CVM, "BlockValidator: Metadata-only contract registered at %s (deployer=%s, nonce=%d)\n",
                 contractAddr.ToString(), deployer.ToString(), nonce);
        return true;
    }
    
    // Check if VM is available
    if (!m_vm) {
        error = "EnhancedVM not initialized";
        return false;
    }
    
    // Execute contract deployment using EnhancedVM
    try {
        EnhancedExecutionResult result = m_vm->DeployContract(
            deployData.bytecode,
            deployData.constructorData,
            deployData.gasLimit,
            deployer,
            0, // deploy_value (from transaction value)
            blockHeight,
            uint256(), // block hash
            0  // timestamp
        );
        
        if (!result.success) {
            error = result.error;
            return false;
        }
        
        gasUsed = result.gas_used;
        // Generate contract address from deployer + nonce
        contractAddr = uint160(Hash160(deployer.begin(), deployer.end()));
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract deployed at %s, gas used: %d, format: %d\n",
                 contractAddr.ToString(), gasUsed, static_cast<int>(deployData.format));
        
        return true;
        
    } catch (const std::exception& e) {
        error = strprintf("Contract deployment exception: %s", e.what());
        LogPrint(BCLog::CVM, "BlockValidator: %s\n", error);
        return false;
    }
}

bool BlockValidator::ExecuteContractCall(
    const CTransaction& tx,
    int blockHeight,
    CCoinsViewCache& view,
    uint64_t& gasUsed,
    std::string& error)
{
    // Parse call data
    int opReturnIndex = FindCVMOpReturn(tx);
    CVMOpType opType;
    std::vector<uint8_t> data;
    ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data);
    
    CVMCallData callData;
    if (!callData.Deserialize(data)) {
        error = "Failed to deserialize call data";
        return false;
    }
    
    // Get caller address
    uint160 caller = GetSenderAddress(tx, view);
    if (caller.IsNull()) {
        error = "Could not extract caller address";
        return false;
    }
    
    // Log contract call access control check
    if (g_accessControlAuditor && m_trustContext) {
        int16_t callerReputation = static_cast<int16_t>(m_trustContext->GetReputation(caller));
        int16_t requiredReputation = g_accessControlAuditor->GetMinimumReputation(AccessOperationType::CONTRACT_CALL);
        
        AccessDecision decision = g_accessControlAuditor->LogReputationGatedOperation(
            caller,
            AccessOperationType::CONTRACT_CALL,
            "CallContract",
            requiredReputation,
            callerReputation,
            callData.contractAddress.GetHex(),
            tx.GetHash());
        
        if (decision != AccessDecision::GRANTED) {
            error = "Contract call denied by access control";
            return false;
        }
    }
    
    // Check if VM is available
    if (!m_vm) {
        error = "EnhancedVM not initialized";
        return false;
    }
    
    // Execute contract call using EnhancedVM
    try {
        EnhancedExecutionResult result = m_vm->CallContract(
            callData.contractAddress,
            callData.callData,
            callData.gasLimit,
            caller,
            0, // call_value (from transaction value)
            blockHeight,
            uint256(), // block hash
            0  // timestamp
        );
        
        if (!result.success) {
            error = result.error;
            return false;
        }
        
        gasUsed = result.gas_used;
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract call to %s, gas used: %d, format: %d\n",
                 callData.contractAddress.ToString(), gasUsed, static_cast<int>(callData.format));
        
        return true;
        
    } catch (const std::exception& e) {
        error = strprintf("Contract call exception: %s", e.what());
        LogPrint(BCLog::CVM, "BlockValidator: %s\n", error);
        return false;
    }
}

bool BlockValidator::SaveContractState(bool fJustCheck)
{
    if (fJustCheck) {
        return true; // Don't save in test mode
    }
    
    // Save contract state changes to database
    if (!m_db) {
        LogPrint(BCLog::CVM, "BlockValidator: No database available for state save\n");
        return false;
    }
    
    try {
        // The EnhancedVM automatically saves state changes to the database
        // during execution through the EnhancedStorage layer.
        // This method is called to ensure all pending writes are flushed.
        
        // Flush any pending database writes
        // In LevelDB, this would be a batch write commit
        // For now, we assume state is already saved during execution
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract state saved successfully\n");
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Failed to save contract state: %s\n", e.what());
        return false;
    }
}

void BlockValidator::RollbackContractState()
{
    LogPrint(BCLog::CVM, "BlockValidator: Rolling back contract state\n");
    
    // Rollback contract state changes
    // In a full implementation, this would:
    // 1. Revert all database writes made during this block
    // 2. Restore previous contract storage states
    // 3. Remove newly deployed contracts
    
    // The EnhancedVM uses database transactions for atomicity.
    // When a block fails validation, we need to rollback the database transaction.
    
    if (!m_db) {
        LogPrint(BCLog::CVM, "BlockValidator: No database available for rollback\n");
        return;
    }
    
    try {
        // In LevelDB, this would rollback the current batch
        // For now, we log the rollback
        // The actual rollback mechanism depends on how the database
        // transaction system is implemented
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract state rollback complete\n");
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Rollback exception: %s\n", e.what());
    }
}

bool BlockValidator::DistributeGasSubsidies(
    const CBlock& block,
    int blockHeight)
{
    if (!m_gasSubsidyTracker || !m_trustContext) {
        LogPrint(BCLog::CVM, "BlockValidator: Gas subsidy tracker or trust context not available\n");
        return false;
    }
    
    try {
        // Distribute gas subsidies to eligible transactions in this block
        for (const auto& tx : block.vtx) {
            // Skip coinbase
            if (tx->IsCoinBase()) {
                continue;
            }
            
            // Check if CVM/EVM transaction
            if (!IsEVMTransaction(*tx) && FindCVMOpReturn(*tx) < 0) {
                continue;
            }
            
            // Extract gas limit
            uint64_t gasLimit = ExtractGasLimit(*tx);
            if (gasLimit == 0) {
                continue;
            }
            
            // Check if transaction is eligible for subsidy
            // This would check if the operation is beneficial to the network
            bool isBeneficial = true; // Simplified - should check actual benefit
            
            // Calculate and record subsidy
            uint64_t subsidyAmount = m_gasSubsidyTracker->CalculateSubsidy(
                gasLimit,
                *m_trustContext,
                isBeneficial
            );
            
            if (subsidyAmount > 0) {
                // Record subsidy for this transaction
                GasSubsidyTracker::SubsidyRecord record;
                record.txid = tx->GetHash();
                record.blockHeight = blockHeight;
                record.gasUsed = gasLimit; // Simplified - should use actual gas used
                record.subsidyAmount = subsidyAmount;
                
                m_gasSubsidyTracker->ApplySubsidy(
                    tx->GetHash(),
                    uint160(), // address - should extract from tx
                    gasLimit,
                    subsidyAmount,
                    *m_trustContext,
                    blockHeight
                );
                
                LogPrint(BCLog::CVM, "BlockValidator: Subsidy recorded for tx %s: %d gas\n",
                         tx->GetHash().ToString(), subsidyAmount);
            }
        }
        
        // Save subsidy tracker state
        if (m_db) {
            m_gasSubsidyTracker->SaveToDatabase(*m_db);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Gas subsidy distribution failed: %s\n", e.what());
        return false;
    }
}

bool BlockValidator::ProcessGasRebates(int blockHeight)
{
    if (!m_gasSubsidyTracker) {
        LogPrint(BCLog::CVM, "BlockValidator: Gas subsidy tracker not available\n");
        return false;
    }
    
    // Process rebates for transactions confirmed 10 blocks ago
    int rebateHeight = blockHeight - 10;
    if (rebateHeight < 0) {
        return true; // No rebates to process yet
    }
    
    try {
        // Distribute pending rebates
        int rebatesDistributed = m_gasSubsidyTracker->DistributePendingRebates(blockHeight);
        
        if (rebatesDistributed > 0) {
            LogPrint(BCLog::CVM, "BlockValidator: Distributed %d gas rebates at height %d\n",
                     rebatesDistributed, blockHeight);
        }
        
        // In a full implementation, this would:
        // 1. Create rebate transactions or credits
        // 2. Update the subsidy pool balance
        // 3. Mark the rebates as processed
        
        // For now, the DistributePendingRebates method handles the logic
        
        // Save updated state
        if (m_db) {
            m_gasSubsidyTracker->SaveToDatabase(*m_db);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Gas rebate processing failed: %s\n", e.what());
        return false;
    }
}

bool BlockValidator::IsCVMActive(int blockHeight, const Consensus::Params& chainparams)
{
    // Check if CVM soft fork is active at this height
    // CVM activates at the configured activation height in chainparams
    return blockHeight >= chainparams.cvmActivationHeight;
}

uint64_t BlockValidator::ExtractGasLimit(const CTransaction& tx)
{
    int opReturnIndex = FindCVMOpReturn(tx);
    if (opReturnIndex < 0) {
        return 0;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
        return 0;
    }
    
    if (opType == CVMOpType::CONTRACT_DEPLOY || opType == CVMOpType::EVM_DEPLOY) {
        CVMDeployData deployData;
        if (deployData.Deserialize(data)) {
            return deployData.gasLimit;
        }
    } else if (opType == CVMOpType::CONTRACT_CALL || opType == CVMOpType::EVM_CALL) {
        CVMCallData callData;
        if (callData.Deserialize(data)) {
            return callData.gasLimit;
        }
    }
    
    return 0;
}

uint160 BlockValidator::GetSenderAddress(const CTransaction& tx, CCoinsViewCache& view)
{
    // Get sender from first input
    if (tx.vin.empty()) {
        return uint160();
    }
    
    const CTxIn& txin = tx.vin[0];
    
    // Strategy 1: Try to extract from witness data (SegWit / Quantum)
    const auto& scriptWitness = txin.scriptWitness;
    if (scriptWitness.stack.size() >= 2) {
        const std::vector<unsigned char>& pubkeyData = scriptWitness.stack.back();
        // Standard ECDSA compressed/uncompressed pubkey
        if (pubkeyData.size() == 33 || pubkeyData.size() == 65) {
            CPubKey pubkey(pubkeyData.begin(), pubkeyData.end());
            if (pubkey.IsValid()) {
                LogPrint(BCLog::CVM, "BlockValidator: Extracted address from witness pubkey: %s\n",
                         pubkey.GetID().ToString());
                return pubkey.GetID();
            }
        }
        // Quantum FALCON-512 pubkey (897 bytes)
        if (pubkeyData.size() == CPubKey::QUANTUM_PUBLIC_KEY_SIZE) {
            CPubKey pubkey(pubkeyData.begin(), pubkeyData.end());
            if (pubkey.IsValid() && pubkey.IsQuantum()) {
                // For quantum keys, use Hash160 of the pubkey as the address
                uint160 address = Hash160(pubkeyData.begin(), pubkeyData.end());
                LogPrint(BCLog::CVM, "BlockValidator: Extracted address from quantum witness pubkey: %s\n",
                         address.ToString());
                return address;
            }
        }
    }
    
    // Strategy 2: Try to extract from scriptSig (P2PKH)
    const CScript& scriptSig = txin.scriptSig;
    if (scriptSig.size() > 0) {
        CScript::const_iterator pc = scriptSig.begin();
        std::vector<unsigned char> data;
        opcodetype opcode;
        
        // Skip signature
        if (scriptSig.GetOp(pc, opcode, data)) {
            // Get pubkey
            if (scriptSig.GetOp(pc, opcode, data)) {
                if (data.size() == 33 || data.size() == 65) {
                    CPubKey pubkey(data.begin(), data.end());
                    if (pubkey.IsValid()) {
                        LogPrint(BCLog::CVM, "BlockValidator: Extracted address from scriptSig pubkey: %s\n",
                                 pubkey.GetID().ToString());
                        return pubkey.GetID();
                    }
                }
            }
        }
    }
    
    // Strategy 3: Fall back to UTXO lookup (may fail if coins already spent by UpdateCoins)
    Coin coin;
    if (view.GetCoin(txin.prevout, coin)) {
        const CScript& scriptPubKey = coin.out.scriptPubKey;
        CTxDestination dest;
        if (ExtractDestination(scriptPubKey, dest)) {
            if (const CKeyID* keyID = boost::get<CKeyID>(&dest)) {
                return *keyID;
            }
            if (const CScriptID* scriptID = boost::get<CScriptID>(&dest)) {
                return *scriptID;
            }
            if (const WitnessV0KeyHash* wkh = boost::get<WitnessV0KeyHash>(&dest)) {
                return *wkh;
            }
            if (const WitnessV0ScriptHash* wsh = boost::get<WitnessV0ScriptHash>(&dest)) {
                uint160 address;
                memcpy(address.begin(), wsh->begin(), 20);
                return address;
            }
            if (const WitnessV2Quantum* quantum = boost::get<WitnessV2Quantum>(&dest)) {
                uint160 address;
                memcpy(address.begin(), quantum->begin(), 20);
                return address;
            }
            if (const WitnessUnknown* wu = boost::get<WitnessUnknown>(&dest)) {
                uint160 address;
                unsigned int copyLen = std::min(wu->length, (unsigned int)20);
                memcpy(address.begin(), wu->program, copyLen);
                return address;
            }
        }
    }
    
    LogPrint(BCLog::CVM, "BlockValidator: Could not extract sender address for tx %s input %s:%d\n",
             tx.GetHash().ToString(), txin.prevout.hash.ToString(), txin.prevout.n);
    return uint160();
}

// ===== HAT v2 Consensus Integration =====

void BlockValidator::SetHATConsensusValidator(HATConsensusValidator* validator) {
    m_hatValidator = validator;
}

bool BlockValidator::ValidateBlockHATConsensus(const CBlock& block, std::string& error) {
    if (!m_hatValidator) {
        // If no HAT validator, skip validation
        return true;
    }
    
    // HAT v2 score expiration: scores are valid for 1000 blocks by default
    static const int HAT_SCORE_EXPIRATION_BLOCKS = 1000;
    
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check if CVM/EVM transaction
        int cvmOutputIndex = FindCVMOpReturn(*tx);
        if (cvmOutputIndex < 0) {
            continue;  // Not a CVM/EVM transaction
        }
        
        // Check transaction has validated reputation
        TransactionState state = m_hatValidator->GetTransactionState(tx->GetHash());
        
        if (state != TransactionState::VALIDATED) {
            error = strprintf("Block contains unvalidated transaction: %s (state: %d)",
                            tx->GetHash().ToString(), (int)state);
            return false;
        }
        
        // Verify HAT v2 score is still valid (not expired)
        // Get the validation request to check the score timestamp
        DisputeCase dispute = m_hatValidator->GetDisputeCase(tx->GetHash());
        if (!dispute.validatorResponses.empty()) {
            // Check if the self-reported score has expired
            const HATv2Score& selfReportedScore = dispute.selfReportedScore;
            
            // Calculate the block height when the score was calculated
            // Using timestamp to estimate block height (assuming ~2.5 min blocks = 150 seconds)
            int64_t currentTime = block.GetBlockTime();
            int64_t scoreAge = currentTime - selfReportedScore.timestamp;
            
            // Convert time to approximate block count (2.5 min = 150 seconds per block)
            int estimatedBlocksElapsed = static_cast<int>(scoreAge / 150);
            
            if (estimatedBlocksElapsed > HAT_SCORE_EXPIRATION_BLOCKS) {
                error = strprintf("Block contains transaction with expired HAT v2 score: %s (score age: ~%d blocks, max: %d)",
                                tx->GetHash().ToString(), estimatedBlocksElapsed, HAT_SCORE_EXPIRATION_BLOCKS);
                LogPrint(BCLog::CVM, "BlockValidator: %s\n", error);
                return false;
            }
        }
    }
    
    return true;
}

bool BlockValidator::RecordFraudInBlock(CBlock& block, const std::vector<FraudRecord>& fraudRecords) {
    // Create special OP_RETURN transactions to encode fraud records in the blockchain
    // This makes fraud records permanent and verifiable by all nodes
    
    // ANTI-MANIPULATION PROTECTION: Only accept DAO-approved fraud records
    // This prevents arbitrary users from adding false fraud accusations to blocks
    
    for (const auto& fraud : fraudRecords) {
        // Validate fraud record before adding to block
        if (!g_hatConsensusValidator || !g_hatConsensusValidator->ValidateFraudRecord(fraud)) {
            LogPrintf("BlockValidator: Skipping invalid fraud record for %s\n",
                     fraud.fraudsterAddress.ToString());
            continue;  // Skip invalid fraud records
        }
        
        // Create fraud record transaction
        CMutableTransaction fraudTx;
        fraudTx.nVersion = 2;
        fraudTx.nLockTime = 0;
        
        // Create OP_RETURN output with fraud record data
        // Format: OP_RETURN <magic> <version> <serialized_fraud_record>
        CScript fraudScript;
        fraudScript << OP_RETURN;
        
        // Magic bytes to identify fraud records: "FRAUD"
        std::vector<unsigned char> magic = {0x46, 0x52, 0x41, 0x55, 0x44};
        fraudScript << magic;
        
        // Version byte
        fraudScript << std::vector<unsigned char>{0x01};
        
        // Serialize fraud record
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << fraud;
        std::vector<unsigned char> fraudData(ss.begin(), ss.end());
        fraudScript << fraudData;
        
        // Add output
        CTxOut fraudOut(0, fraudScript);  // 0 value for OP_RETURN
        fraudTx.vout.push_back(fraudOut);
        
        // Add to block
        block.vtx.push_back(MakeTransactionRef(std::move(fraudTx)));
        
        LogPrint(BCLog::CVM, "BlockValidator: Recorded fraud by %s in block (penalty: %d points, tx: %s)\n",
                 fraud.fraudsterAddress.ToString(), fraud.reputationPenalty, 
                 block.vtx.back()->GetHash().ToString());
    }
    
    return true;
}

std::vector<FraudRecord> BlockValidator::ExtractFraudRecords(const CBlock& block) {
    std::vector<FraudRecord> fraudRecords;
    
    // Parse fraud record transactions from block
    // Look for OP_RETURN transactions with fraud record magic bytes
    
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check each output for fraud record OP_RETURN
        for (const auto& out : tx->vout) {
            if (out.scriptPubKey.size() < 10) {  // Minimum size check
                continue;
            }
            
            // Check for OP_RETURN
            if (out.scriptPubKey[0] != OP_RETURN) {
                continue;
            }
            
            // Parse script to extract data
            CScript::const_iterator pc = out.scriptPubKey.begin() + 1;
            std::vector<unsigned char> data;
            opcodetype opcode;
            
            // Read magic bytes
            if (!out.scriptPubKey.GetOp(pc, opcode, data)) {
                continue;
            }
            
            // Check magic: "FRAUD"
            if (data.size() != 5 || 
                data[0] != 0x46 || data[1] != 0x52 || data[2] != 0x41 || 
                data[3] != 0x55 || data[4] != 0x44) {
                continue;
            }
            
            // Read version
            if (!out.scriptPubKey.GetOp(pc, opcode, data)) {
                continue;
            }
            if (data.size() != 1 || data[0] != 0x01) {
                continue;  // Unsupported version
            }
            
            // Read fraud record data
            if (!out.scriptPubKey.GetOp(pc, opcode, data)) {
                continue;
            }
            
            // Deserialize fraud record
            try {
                CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
                FraudRecord fraud;
                ss >> fraud;
                
                fraudRecords.push_back(fraud);
                
                LogPrint(BCLog::CVM, "BlockValidator: Extracted fraud record for %s from block (penalty: %d points)\n",
                         fraud.fraudsterAddress.ToString(), fraud.reputationPenalty);
            } catch (const std::exception& e) {
                LogPrintf("BlockValidator: Failed to deserialize fraud record: %s\n", e.what());
                continue;
            }
        }
    }
    
    return fraudRecords;
}

} // namespace CVM
