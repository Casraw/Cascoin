// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/block_validator.h>
#include <cvm/cvm.h>
#include <cvm/cvmtx.h>
#include <cvm/softfork.h>
#include <cvm/trust_context.h>
#include <cvm/hat_consensus.h>
#include <cvm/access_control_audit.h>
#include <cvm/securehat.h>
#include <consensus/validation.h>
#include <validation.h>
#include <chainparams.h>
#include <util.h>
#include <utilstrencodings.h>
#include <script/script.h>
#include <script/standard.h>
#include <pubkey.h>
#include <hash.h>
#include <net_processing.h>
#include <quantum_registry_fwd.h>
#include <cstdlib>

namespace CVM {

// Extract the value carried by a CVM transaction that is made available to the
// executing contract as CALLVALUE (bugfix 2.60), on the ACTIVE block-validation
// path. This is kept in sync with blockprocessor.cpp's ExtractTransactionValue
// so the two block-processing paths agree (reconciliation 3.12).
//
// The wallet conveys contract value by BURNING it to a provably-unspendable
// OP_RETURN output that encodes the contract address (see
// CWallet::CreateContractCallTransaction: `OP_RETURN << contractAddress`), NOT
// by a spendable output. A transaction's spendable CHANGE output therefore is
// NOT value sent to the contract and must be excluded. We sum only the value of
// unspendable (OP_RETURN) outputs other than the CVM marker output.
//
// Consequences:
//  - A contract DEPLOY (deploycontract carries no value; the tx has only the
//    CVM marker OP_RETURN plus spendable change) yields CALLVALUE == 0
//    (preservation 3.22).
//  - A zero-value CALL likewise yields CALLVALUE == 0 (preservation 3.22).
//  - A value-bearing CALL yields the burned value as CALLVALUE (2.60).
static uint64_t ExtractTransactionValue(const CTransaction& tx, int cvmOutputIndex)
{
    CAmount total = 0;
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        if (static_cast<int>(i) == cvmOutputIndex) {
            continue;
        }
        // Only value burned to an unspendable (OP_RETURN) output counts as
        // contract value; spendable change is excluded.
        if (tx.vout[i].nValue > 0 && tx.vout[i].scriptPubKey.IsUnspendable()) {
            total += tx.vout[i].nValue;
        }
    }
    return static_cast<uint64_t>(total);
}

// Helper: Update behavior + temporal metrics for an address after CVM activity
static void UpdateActivityMetrics(CVMDatabase& db, const uint160& actor, const uint256& txid, const uint160& partner) {
    try {
        SecureHAT hat(db);
        
        // Behavior metrics
        BehaviorMetrics metrics = hat.GetBehaviorMetrics(actor);
        // A default-constructed TrustNodeId (type 0) means "no stored identity".
        // `actor`/`partner` are legacy P2PKH-shaped uint160 identities here, so
        // wrap them into TrustNodeId without narrowing.
        if (metrics.address.type == 0) {
            metrics.address = TrustNodeId::FromLegacyUint160(actor);
            metrics.account_creation = GetTime();
        }
        TradeRecord trade;
        trade.txid = txid;
        trade.partner = TrustNodeId::FromLegacyUint160(partner);
        trade.volume = 0;
        trade.timestamp = GetTime();
        trade.success = true;
        trade.disputed = false;
        metrics.AddTrade(trade);
        metrics.AddActivity(GetTime());
        metrics.UpdateScores();
        hat.StoreBehaviorMetrics(metrics);
        
        // Temporal metrics
        TemporalMetrics temporal = hat.GetTemporalMetrics(actor);
        if (temporal.account_creation == 0 || temporal.account_creation >= GetTime()) {
            temporal.account_creation = GetTime();
        }
        temporal.last_activity = GetTime();
        temporal.activity_timestamps.push_back(GetTime());
        // Keep last 1000 timestamps to avoid unbounded growth
        if (temporal.activity_timestamps.size() > 1000) {
            temporal.activity_timestamps.erase(temporal.activity_timestamps.begin());
        }
        hat.StoreTemporalMetrics(actor, temporal);
        
        LogPrint(BCLog::CVM, "BlockValidator: Updated HAT metrics for %s (trades=%d)\n",
                 actor.ToString(), metrics.total_trades);
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Failed to update HAT metrics: %s\n", e.what());
    }
}

BlockValidator::BlockValidator()
    : m_db(nullptr)
    , m_hatValidator(nullptr)
    , m_stateCommitted(false)
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
    
    // Initialize trust context WITH database so reputation lookups work
    if (db) {
        m_trustContext = std::make_shared<TrustContext>(db);
    } else {
        m_trustContext = std::make_shared<TrustContext>();
    }
    
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
    
    // Capture the baseline contract-state snapshot so RollbackContractState can
    // perform a real revert of any writes made against this database (bugfix
    // 2.21). This also handles the case where in-block writes are applied
    // directly to the database between Initialize() and RollbackContractState().
    SnapshotContractState();
}

void BlockValidator::SnapshotContractState()
{
    m_contractSnapshot.clear();
    m_contractSnapshotValues.clear();
    m_stateCommitted = false;
    
    if (!m_db) {
        return;
    }
    
    // Record every contract address present at block begin, along with its
    // current value, so both newly-written and modified contracts can be
    // reverted on failure.
    std::vector<uint160> existing = m_db->ListContracts();
    for (const uint160& addr : existing) {
        m_contractSnapshot.insert(addr);
        Contract c;
        if (m_db->ReadContract(addr, c)) {
            m_contractSnapshotValues[addr] = c;
        }
    }
    
    LogPrint(BCLog::CVM, "BlockValidator: Captured contract-state snapshot (%d contracts)\n",
             (int)m_contractSnapshot.size());
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
    m_txGasUsed.clear();
    
    // Snapshot contract-state at block begin so a rejected block can be reverted
    // atomically (bugfix 2.21). Marks this block's state as not-yet-committed.
    SnapshotContractState();
    
    // Check if CVM is active
    if (!IsCVMActive(pindex->nHeight, chainparams)) {
        m_lastResult.success = true;
        return m_lastResult;
    }
    
    LogPrint(BCLog::CVM, "BlockValidator: Validating block %s at height %d\n",
             block.GetHash().ToString(), pindex->nHeight);
    
    // The real block hash is threaded into contract execution so the Enhanced
    // VM sees the correct BLOCKHASH/block context during block processing on the
    // ACTIVE validation path (bugfix 2.60), instead of an empty uint256(). This
    // reconciles this path with blockprocessor.cpp (3.12).
    const uint256 blockHash = block.GetHash();
    
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
        if (!VerifyReputationGasCosts(tx, pindex->nHeight, view)) {
            m_lastResult.success = false;
            m_lastResult.error = "Invalid reputation-based gas costs";
            return m_lastResult;
        }
        
        // Execute transaction
        uint64_t gasUsed = 0;
        std::string error;
        if (!ExecuteTransaction(tx, i, pindex->nHeight, blockHash, view, gasUsed, error)) {
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
        
        // Record the actual gas used for this transaction so subsidy accounting
        // can use the real gas used rather than the gas limit.
        m_txGasUsed[tx.GetHash()] = gasUsed;
        
        LogPrint(BCLog::CVM, "BlockValidator: Executed tx %s, gas used: %d\n",
                 tx.GetHash().ToString(), gasUsed);
    }
    
    // Enforce the per-block subsidy maximum (bugfix 2.1). Accumulate the actual
    // per-transaction subsidies and reject the block when the total exceeds the
    // cvmMaxGasPerBlock-derived subsidy maximum. This is done before saving
    // state so a block that over-subsidizes is never persisted.
    {
        uint64_t accumulatedSubsidy = 0;
        if (!AccumulateBlockSubsidy(block, accumulatedSubsidy)) {
            const uint64_t maxSubsidyPerBlock = GetMaxSubsidyPerBlock(chainparams);
            m_lastResult.success = false;
            m_lastResult.error = strprintf(
                "Block exceeds per-block subsidy maximum: %d > %d",
                accumulatedSubsidy, maxSubsidyPerBlock);
            LogPrint(BCLog::CVM, "BlockValidator: %s\n", m_lastResult.error);
            if (!fJustCheck) {
                RollbackContractState();
            }
            return m_lastResult;
        }
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
    const uint256& blockHash,
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
        return DeployContract(tx, blockHeight, blockHash, view, gasUsed, contractAddr, error);
    } else if (opType == CVMOpType::CONTRACT_CALL || opType == CVMOpType::EVM_CALL) {
        m_lastResult.contractsExecuted++;
        return ExecuteContractCall(tx, blockHeight, blockHash, view, gasUsed, error);
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
    int blockHeight,
    const CCoinsViewCache& view)
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
    
    // Extract gas info from transaction
    uint64_t gasLimit = m_feeCalculator->ExtractGasLimit(tx);
    if (gasLimit == 0) {
        // Not a gas-based transaction, skip verification
        return true;
    }
    
    // Expected fee based on gas usage and gas price (from the fee calculator).
    CAmount expectedFee = feeResult.effectiveFee;
    
    // Verify the fee calculation is internally consistent:
    // effectiveFee should equal baseFee - discount - subsidy (but not negative).
    CAmount baseFee = feeResult.baseFee;
    CAmount discount = feeResult.reputationDiscount;
    CAmount subsidy = feeResult.gasSubsidy;
    
    CAmount calculatedEffective = baseFee - discount - subsidy;
    if (calculatedEffective < 0) {
        calculatedEffective = 0;
    }
    
    // Allow a small tolerance (1%) for rounding differences.
    CAmount tolerance = expectedFee / 100;
    if (tolerance < 1000) {
        tolerance = 1000; // Minimum tolerance of 1000 satoshis
    }
    
    if (std::abs(calculatedEffective - expectedFee) > tolerance) {
        LogPrint(BCLog::CVM, "BlockValidator: Fee calculation inconsistency - calculated: %d, expected: %d\n",
                 calculatedEffective, expectedFee);
        return false;
    }
    
    // Verify the actual fee the transaction pays against its INPUT values
    // (bugfix 2.20). The actual fee is the sum of the input values (resolved
    // via the coins view) minus the sum of the output values. It must cover the
    // expected gas-based effective fee, otherwise the contract transaction has
    // not paid for the gas it consumes.
    CAmount totalInputValue = 0;
    bool haveAllInputs = true;
    for (const auto& in : tx.vin) {
        const Coin& coin = view.AccessCoin(in.prevout);
        if (coin.IsSpent()) {
            // The input's UTXO is not available in the view (e.g. it was already
            // consumed earlier in this block's connection, or the view does not
            // carry it in this validation context). We cannot compute the actual
            // fee from inputs in that case, so fall back to the gas/price
            // consistency check performed above rather than rejecting.
            haveAllInputs = false;
            break;
        }
        totalInputValue += coin.out.nValue;
    }
    
    if (haveAllInputs) {
        CAmount totalOutputValue = 0;
        for (const auto& out : tx.vout) {
            totalOutputValue += out.nValue;
        }
        
        CAmount actualFee = totalInputValue - totalOutputValue;
        
        LogPrint(BCLog::CVM, "BlockValidator: Fee verification vs inputs - inputs: %d, outputs: %d, actualFee: %d, expectedFee: %d, gas: %d\n",
                 totalInputValue, totalOutputValue, actualFee, expectedFee, gasLimit);
        
        // A negative fee (outputs exceed inputs) is always invalid.
        if (actualFee < 0) {
            LogPrint(BCLog::CVM, "BlockValidator: Transaction outputs exceed inputs (actualFee=%d)\n",
                     actualFee);
            return false;
        }
        
        // The paid fee must cover the expected gas-based effective fee (within
        // the rounding tolerance).
        if (actualFee + tolerance < expectedFee) {
            LogPrint(BCLog::CVM, "BlockValidator: Insufficient fee for gas cost - actualFee: %d, expectedFee: %d\n",
                     actualFee, expectedFee);
            return false;
        }
    } else {
        LogPrint(BCLog::CVM, "BlockValidator: Input values unavailable in view; verified gas/price consistency only for tx %s\n",
                 tx.GetHash().ToString());
    }
    
    return true;
}

bool BlockValidator::DeployContract(
    const CTransaction& tx,
    int blockHeight,
    const uint256& blockHash,
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
    
    // Log contract deployment for auditing purposes (informational only).
    // Access control is NOT enforced during block validation - the block was
    // already accepted by the network. Enforcement happens at TX submission time.
    if (g_accessControlAuditor && m_trustContext) {
        int16_t deployerReputation = static_cast<int16_t>(m_trustContext->GetReputation(deployer));
        LogPrint(BCLog::CVM, "BlockValidator: Deployer %s reputation: %d (audit only)\n",
                 deployer.ToString(), deployerReputation);
    }
    
    // The OP_RETURN now contains the full bytecode (serialized in CVMDeployData).
    // For backward compatibility with old transactions that used witness-based
    // bytecode delivery, also check witness data as a fallback.
    if (deployData.bytecode.empty()) {
        for (const auto& txin : tx.vin) {
            const auto& wit = txin.scriptWitness;
            for (size_t i = 0; i < wit.stack.size(); ++i) {
                const auto& candidate = wit.stack[i];
                if (!candidate.empty()) {
                    uint256 candidateHash = Hash(candidate.begin(), candidate.end());
                    if (candidateHash == deployData.codeHash) {
                        deployData.bytecode = candidate;
                        LogPrint(BCLog::CVM, "BlockValidator: Extracted bytecode (%d bytes) from witness stack[%d] (legacy fallback)\n",
                                 candidate.size(), (int)i);
                        break;
                    }
                }
            }
            if (!deployData.bytecode.empty()) break;
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
            contract.deployer = deployer;
            // code stays empty — bytecode not available in OP_RETURN
            contract.deploymentTx = tx.GetHash();
            contract.deploymentHeight = blockHeight;
            m_db->WriteContract(contractAddr, contract);
            m_db->WriteNonce(deployer, nonce + 1);
        }
        
        gasUsed = 0; // No execution, no gas consumed
        LogPrint(BCLog::CVM, "BlockValidator: Metadata-only contract registered at %s (deployer=%s, nonce=%d)\n",
                 contractAddr.ToString(), deployer.ToString(), nonce);
        
        // Update deployer's behavior metrics for HAT v2 scoring
        if (m_db) {
            UpdateActivityMetrics(*m_db, deployer, tx.GetHash(), contractAddr);
        }
        
        return true;
    }
    
    // Check if VM is available
    if (!m_vm) {
        error = "EnhancedVM not initialized";
        return false;
    }
    
    // Let EnhancedVM handle nonce management and contract address generation.
    // Do NOT call GetNextNonce() here — EnhancedVM::DeployContract() does it
    // internally, and calling it twice would cause a nonce mismatch where
    // BlockValidator logs one address but EnhancedVM writes to a different one.

    // Pass the ACTUAL transaction value and the REAL block hash to the Enhanced
    // VM so CALLVALUE and BLOCKHASH/block context are correct during block
    // processing on the active validation path (bugfix 2.60), instead of a
    // hardcoded 0 / uint256(). A zero-value deploy still surfaces CALLVALUE == 0
    // (preservation 3.22).
    uint64_t deployValue = ExtractTransactionValue(tx, opReturnIndex);

    // Execute contract deployment using EnhancedVM. The Enhanced VM flushes
    // pending contract-state writes durably via CommitExecutionState (bugfix
    // 2.61) on success, so any SSTORE performed by the constructor is persisted.
    try {
        EnhancedExecutionResult result = m_vm->DeployContract(
            deployData.bytecode,
            deployData.constructorData,
            deployData.gasLimit,
            deployer,
            deployValue, // deploy_value (actual transaction value)
            blockHeight,
            blockHash, // real block hash
            0  // timestamp
        );
        
        // Get the contract address from the result (set by EnhancedVM)
        contractAddr = result.contract_address;
        
        if (!result.success) {
            // Execution / validation failure of the contract should NOT invalidate the block.
            // Treat this like an EVM-style failed CREATE: consume gas, do not register code,
            // but still advance nonce so future deployments remain deterministic.
            LogPrint(BCLog::CVM, "BlockValidator: Contract deployment failed (tx=%s, deployer=%s, addr=%s): %s\n",
                     tx.GetHash().ToString(), deployer.ToString(), contractAddr.ToString(), result.error);

            gasUsed = deployData.gasLimit;

            error.clear();
            return true;
        }
        
        gasUsed = result.gas_used;
        
        // Update contract metadata with deployment info (TX hash, block height, deployer)
        // EnhancedVM stores the bytecode but doesn't have access to this context.
        if (m_db && !contractAddr.IsNull()) {
            Contract contract;
            if (m_db->ReadContract(contractAddr, contract)) {
                contract.deploymentTx = tx.GetHash();
                contract.deploymentHeight = blockHeight;
                contract.deployer = deployer;
                m_db->WriteContract(contractAddr, contract);
            }
        }
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract deployed at %s, gas used: %d, format: %d\n",
                 contractAddr.ToString(), gasUsed, static_cast<int>(deployData.format));
        
        // Update deployer's behavior metrics for HAT v2 scoring
        if (m_db) {
            UpdateActivityMetrics(*m_db, deployer, tx.GetHash(), contractAddr);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        // Same semantics: a deployment throwing should not invalidate the whole block.
        LogPrint(BCLog::CVM, "BlockValidator: Contract deployment exception (tx=%s, deployer=%s): %s\n",
                 tx.GetHash().ToString(), deployer.ToString(), e.what());

        gasUsed = deployData.gasLimit;

        error.clear();
        return true;
    }
}

bool BlockValidator::ExecuteContractCall(
    const CTransaction& tx,
    int blockHeight,
    const uint256& blockHash,
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
    
    // Pass the ACTUAL transaction value and the REAL block hash to the Enhanced
    // VM so CALLVALUE and BLOCKHASH/block context are correct during block
    // processing on the active validation path (bugfix 2.60), instead of a
    // hardcoded 0 / uint256(). A zero-value call still surfaces CALLVALUE == 0
    // (preservation 3.22).
    uint64_t callValue = ExtractTransactionValue(tx, opReturnIndex);

    // Execute contract call using EnhancedVM. The Enhanced VM flushes pending
    // contract-state writes durably via CommitExecutionState (bugfix 2.61) on
    // success.
    try {
        EnhancedExecutionResult result = m_vm->CallContract(
            callData.contractAddress,
            callData.callData,
            callData.gasLimit,
            caller,
            callValue, // call_value (actual transaction value)
            blockHeight,
            blockHash, // real block hash
            0  // timestamp
        );
        
        if (!result.success) {
            // Contract call failure should not invalidate the block.
            LogPrint(BCLog::CVM, "BlockValidator: Contract call failed (tx=%s, caller=%s, contract=%s): %s\n",
                     tx.GetHash().ToString(), caller.ToString(), callData.contractAddress.ToString(), result.error);

            // Be conservative for gas accounting: if VM couldn't report gas, consume full limit.
            gasUsed = result.gas_used > 0 ? result.gas_used : callData.gasLimit;
            if (gasUsed > callData.gasLimit) gasUsed = callData.gasLimit;

            error.clear();
            return true;
        }
        
        gasUsed = result.gas_used;
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract call to %s, gas used: %d, format: %d\n",
                 callData.contractAddress.ToString(), gasUsed, static_cast<int>(callData.format));
        
        // Update caller's behavior metrics for HAT v2 scoring
        if (m_db) {
            UpdateActivityMetrics(*m_db, caller, tx.GetHash(), callData.contractAddress);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Contract call exception (tx=%s, caller=%s, contract=%s): %s\n",
                 tx.GetHash().ToString(), caller.ToString(), callData.contractAddress.ToString(), e.what());

        gasUsed = callData.gasLimit;
        error.clear();
        return true;
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
        // The EnhancedVM saves state changes to the database during execution
        // through the EnhancedStorage layer. Flush any pending writes so the
        // block's state is durably persisted.
        if (!m_db->Flush()) {
            LogPrint(BCLog::CVM, "BlockValidator: Database flush failed during state save\n");
            return false;
        }
        
        // Mark this block's state as committed. Once committed, the writes made
        // during the block are part of the canonical chain state and must NEVER
        // be rolled back (preservation 3.17 / 3.23). Re-baseline the snapshot to
        // the now-committed state so any subsequent RollbackContractState is a
        // no-op with respect to these writes.
        m_stateCommitted = true;
        m_contractSnapshot.clear();
        m_contractSnapshotValues.clear();
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract state saved and committed successfully\n");
        return true;
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Failed to save contract state: %s\n", e.what());
        return false;
    }
}

void BlockValidator::RollbackContractState()
{
    LogPrint(BCLog::CVM, "BlockValidator: Rolling back contract state\n");
    
    if (!m_db) {
        LogPrint(BCLog::CVM, "BlockValidator: No database available for rollback\n");
        return;
    }
    
    // An already-committed (accepted) block's state is canonical chain state and
    // must never be reverted (preservation 3.17 / 3.23).
    if (m_stateCommitted) {
        LogPrint(BCLog::CVM, "BlockValidator: State already committed; nothing to roll back\n");
        return;
    }
    
    try {
        // Real revert against the snapshot captured at block begin:
        //  - any contract present now but NOT in the snapshot was written during
        //    the failed block and is erased,
        //  - any contract that existed in the snapshot but was modified is
        //    restored to its snapshot value.
        std::vector<uint160> current = m_db->ListContracts();
        size_t erased = 0;
        size_t restored = 0;
        for (const uint160& addr : current) {
            if (m_contractSnapshot.find(addr) == m_contractSnapshot.end()) {
                // Newly written during the failed block -> remove the leaked write.
                m_db->DeleteContract(addr);
                ++erased;
            } else {
                // Pre-existing contract -> restore its prior value if we have it.
                auto it = m_contractSnapshotValues.find(addr);
                if (it != m_contractSnapshotValues.end()) {
                    m_db->WriteContract(addr, it->second);
                    ++restored;
                }
            }
        }
        
        // Rewrite the contract index to exactly the snapshot set so erased
        // (leaked) addresses no longer appear in ListContracts().
        std::vector<uint160> snapshotList(m_contractSnapshot.begin(), m_contractSnapshot.end());
        m_db->GetDB().Write(std::string(1, DB_CONTRACT_LIST), snapshotList);
        
        // Persist the reverted state.
        m_db->Flush();
        
        LogPrint(BCLog::CVM, "BlockValidator: Contract state rollback complete (erased %d, restored %d)\n",
                 (int)erased, (int)restored);
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "BlockValidator: Rollback exception: %s\n", e.what());
    }
}

bool BlockValidator::AccumulateBlockSubsidy(const CBlock& block, uint64_t& accumulatedSubsidy)
{
    accumulatedSubsidy = 0;
    
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Only contract (gas-bearing) CVM/EVM transactions can claim a subsidy.
        if (!IsEVMTransaction(*tx) && FindCVMOpReturn(*tx) < 0) {
            continue;
        }
        
        uint64_t gasLimit = ExtractGasLimit(*tx);
        if (gasLimit == 0) {
            continue;
        }
        
        // The subsidizable gas for a contract transaction is its gas limit.
        // Accumulate it toward the block's subsidy budget.
        accumulatedSubsidy += gasLimit;
    }
    
    // Enforce the cvmMaxGasPerBlock-derived per-block subsidy maximum. Use the
    // consensus params so this stays reconciled with ExecuteCVMBlock.
    const Consensus::Params& params = Params().GetConsensus();
    const uint64_t maxSubsidyPerBlock = GetMaxSubsidyPerBlock(params);
    
    return accumulatedSubsidy <= maxSubsidyPerBlock;
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
            
            // Determine whether the operation is actually beneficial to the
            // network from the trust context, rather than assuming it always is.
            bool isBeneficial = m_gasSubsidyTracker->IsBeneficialOperation(*m_trustContext);
            
            // Use the actual gas used during execution (recorded in ValidateBlock)
            // rather than the gas limit. Fall back to the gas limit only if no
            // execution record exists for this transaction.
            uint64_t actualGasUsed = gasLimit;
            auto itGas = m_txGasUsed.find(tx->GetHash());
            if (itGas != m_txGasUsed.end()) {
                actualGasUsed = itGas->second;
            }
            
            // Calculate and record subsidy
            uint64_t subsidyAmount = m_gasSubsidyTracker->CalculateSubsidy(
                actualGasUsed,
                *m_trustContext,
                isBeneficial
            );
            
            if (subsidyAmount > 0) {
                // Record subsidy for this transaction
                GasSubsidyTracker::SubsidyRecord record;
                record.txid = tx->GetHash();
                record.blockHeight = blockHeight;
                record.gasUsed = actualGasUsed;
                record.subsidyAmount = subsidyAmount;
                
                m_gasSubsidyTracker->ApplySubsidy(
                    tx->GetHash(),
                    uint160(), // address - should extract from tx
                    actualGasUsed,
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

    // Strategy 1a: Quantum registry format - single stack element [marker + pubkey + sig]
    // The quantum signing code (CreateQuantumSig) packs marker(1) + pubkey(897) + signature
    // into a single witness stack element, so stack.size() == 1.
    if (scriptWitness.stack.size() >= 1) {
        QuantumWitnessData qwd = ParseQuantumWitness(scriptWitness.stack);
        if (qwd.isValid && !qwd.pubkey.empty() &&
            qwd.pubkey.size() == CPubKey::QUANTUM_PUBLIC_KEY_SIZE) {
            uint160 address = Hash160(qwd.pubkey.begin(), qwd.pubkey.end());
            LogPrint(BCLog::CVM, "BlockValidator: Extracted address from quantum registry witness: %s\n",
                     address.ToString());
            return address;
        }
    }

    // Strategy 1b: Standard SegWit witness - two stack elements [signature, pubkey]
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
        // Quantum FALCON-512 pubkey (897 bytes) in two-element format
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
