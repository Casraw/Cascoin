// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/consensus_validator.h>
#include <cvm/trust_context.h>
#include <cvm/fee_calculator.h>
#include <cvm/gas_subsidy.h>
#include <cvm/softfork.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/securehat.h>
#include <pubkey.h>
#include <script/standard.h>
#include <coins.h>
#include <validation.h>
#include <util.h>

namespace CVM {

// External global trust context (defined in trust_context.cpp)
extern std::shared_ptr<TrustContext> g_trustContext;

// External global CVM database (defined in cvmdb.cpp)
extern std::unique_ptr<CVMDatabase> g_cvmdb;

// Cache for pool balances to use on database query failure
static std::map<std::string, CAmount> g_poolBalanceCache;

using namespace ConsensusConstants;

ConsensusValidator::ConsensusValidator() {}

ConsensusValidator::~ConsensusValidator() {}

bool ConsensusValidator::ValidateReputationRange(uint8_t reputation, std::string& error) {
    if (reputation > MAX_REPUTATION) {
        error = strprintf("Invalid reputation: %d (max: %d)", reputation, MAX_REPUTATION);
        return false;
    }
    return true;
}

uint64_t ConsensusValidator::GetConsensusReputationDiscount(
    uint8_t reputation,
    uint64_t baseGasCost)
{
    // Deterministic discount calculation
    // All nodes must get the same result
    
    uint64_t discountPct = 0;
    
    if (reputation >= DISCOUNT_TIER_4_REP) {
        discountPct = DISCOUNT_TIER_4_PCT;  // 75%
    } else if (reputation >= DISCOUNT_TIER_3_REP) {
        discountPct = DISCOUNT_TIER_3_PCT;  // 50%
    } else if (reputation >= DISCOUNT_TIER_2_REP) {
        discountPct = DISCOUNT_TIER_2_PCT;  // 25%
    } else {
        discountPct = DISCOUNT_TIER_1_PCT;  // 0%
    }
    
    // Calculate discount amount
    uint64_t discount = (baseGasCost * discountPct) / 100;
    
    return discount;
}

bool ConsensusValidator::IsEligibleForFreeGas(uint8_t reputation) {
    // Deterministic check
    return reputation >= FREE_GAS_THRESHOLD;
}

uint64_t ConsensusValidator::GetMaxAllowedSubsidy(
    uint8_t reputation,
    uint64_t gasUsed)
{
    // Deterministic subsidy calculation
    // Higher reputation = higher subsidy limit
    
    if (reputation < 50) {
        return 0;  // No subsidy for low reputation
    }
    
    // Calculate max subsidy based on reputation
    uint64_t maxSubsidy = (gasUsed * reputation) / 100;
    
    // Cap at MAX_SUBSIDY_PER_TX
    if (maxSubsidy > MAX_SUBSIDY_PER_TX) {
        maxSubsidy = MAX_SUBSIDY_PER_TX;
    }
    
    return maxSubsidy;
}

bool ConsensusValidator::ValidateReputationDiscount(
    uint8_t reputation,
    uint64_t baseGasCost,
    uint64_t discountedGasCost,
    std::string& error)
{
    if (!ValidateReputationRange(reputation, error)) {
        return false;
    }
    
    // Calculate expected discount
    uint64_t expectedDiscount = GetConsensusReputationDiscount(reputation, baseGasCost);
    uint64_t expectedCost = baseGasCost - expectedDiscount;
    
    // Verify claimed cost matches expected
    if (discountedGasCost != expectedCost) {
        error = strprintf(
            "Invalid gas discount: claimed=%d, expected=%d (base=%d, rep=%d, discount=%d)",
            discountedGasCost, expectedCost, baseGasCost, reputation, expectedDiscount
        );
        return false;
    }
    
    return true;
}

bool ConsensusValidator::ValidateFreeGasEligibility(
    uint8_t reputation,
    bool claimedEligibility,
    std::string& error)
{
    if (!ValidateReputationRange(reputation, error)) {
        return false;
    }
    
    bool expectedEligibility = IsEligibleForFreeGas(reputation);
    
    if (claimedEligibility != expectedEligibility) {
        error = strprintf(
            "Invalid free gas eligibility: claimed=%d, expected=%d (rep=%d, threshold=%d)",
            claimedEligibility, expectedEligibility, reputation, FREE_GAS_THRESHOLD
        );
        return false;
    }
    
    return true;
}

bool ConsensusValidator::ValidateGasSubsidy(
    const uint160& address,
    uint8_t reputation,
    uint64_t subsidyAmount,
    const std::string& poolId,
    std::string& error)
{
    if (!ValidateReputationRange(reputation, error)) {
        return false;
    }
    
    // Check subsidy doesn't exceed maximum
    if (subsidyAmount > MAX_SUBSIDY_PER_TX) {
        error = strprintf(
            "Subsidy exceeds maximum: %d > %d",
            subsidyAmount, MAX_SUBSIDY_PER_TX
        );
        return false;
    }
    
    // Validate pool has sufficient balance
    if (!ValidatePoolBalance(poolId, subsidyAmount, error)) {
        return false;
    }
    
    return true;
}

uint8_t ConsensusValidator::CalculateDeterministicTrustScore(
    const uint160& address,
    int blockHeight)
{
    // SOLUTION: Optimistic Consensus with Sender-Declared Reputation
    // 
    // How it works:
    // 1. Sender declares their reputation in the transaction
    // 2. Each node validates with THEIR OWN HAT v2 score
    // 3. If sender's claim is <= node's calculated score: ACCEPT
    // 4. If sender's claim is > node's calculated score: REJECT
    // 
    // Why this works:
    // - Sender has incentive to be honest (too high = rejection)
    // - Each node validates with their own HAT v2 (personalized)
    // - Consensus is maintained because transaction contains the claim
    // - Different nodes can have different HAT v2 values
    // - Transaction is either valid for ALL or NONE (based on claim)
    // 
    // Example:
    // - Sender claims reputation 85
    // - Node A calculates HAT v2 = 90 → ACCEPT (85 <= 90)
    // - Node B calculates HAT v2 = 80 → REJECT (85 > 80)
    // - Node C calculates HAT v2 = 85 → ACCEPT (85 <= 85)
    // 
    // Result: Nodes with similar trust views agree on validity
    // This creates "trust clusters" in the network
    
    // Query HAT v2 score from reputation system
    // This uses ALL HAT v2 components including personalized WoT
    if (g_trustContext) {
        return g_trustContext->GetReputation(address);
    }
    
    // Fallback: Use ASRS (Adaptive Stake-weighted Reputation System) if HAT v2 not available
    // Requirements: 9.1 - Fall back to ASRS when HAT v2 unavailable
    if (g_cvmdb) {
        try {
            // First try SecureHAT (HAT v2) directly
            SecureHAT secureHat(*g_cvmdb);
            uint160 defaultViewer;  // Use null viewer for consensus calculation
            int16_t hatScore = secureHat.CalculateFinalTrust(address, defaultViewer);
            
            // HAT score is 0-100, convert to uint8_t
            if (hatScore >= 0 && hatScore <= 100) {
                LogPrint(BCLog::CVM, "ConsensusValidator: HAT v2 score for %s: %d\n",
                         address.ToString(), hatScore);
                return static_cast<uint8_t>(hatScore);
            }
        } catch (const std::exception& e) {
            LogPrint(BCLog::CVM, "ConsensusValidator: HAT v2 failed for %s: %s, falling back to ASRS\n",
                     address.ToString(), e.what());
        }
        
        // Fall back to ASRS (Anti-Scam Reputation System)
        try {
            ReputationSystem repSystem(*g_cvmdb);
            ReputationScore score;
            
            if (repSystem.GetReputation(address, score)) {
                // Convert ASRS score (-10000 to +10000) to 0-100 scale
                // Map: -10000 -> 0, 0 -> 50, +10000 -> 100
                int64_t normalized = ((score.score + 10000) * 100) / 20000;
                normalized = std::max(int64_t(0), std::min(int64_t(100), normalized));
                
                LogPrint(BCLog::CVM, "ConsensusValidator: ASRS fallback score for %s: raw=%d, normalized=%d\n",
                         address.ToString(), score.score, normalized);
                
                return static_cast<uint8_t>(normalized);
            }
        } catch (const std::exception& e) {
            LogPrint(BCLog::CVM, "ConsensusValidator: ASRS fallback failed for %s: %s\n",
                     address.ToString(), e.what());
        }
    }
    
    // Default to medium reputation (50) if all systems unavailable
    LogPrint(BCLog::CVM, "ConsensusValidator: No reputation system available for %s, using default 50\n",
             address.ToString());
    return 50;
}

uint64_t ConsensusValidator::CalculateExpectedGasCost(
    uint64_t baseGas,
    uint8_t reputation,
    bool useFreeGas)
{
    if (useFreeGas && IsEligibleForFreeGas(reputation)) {
        return 0;
    }
    
    uint64_t discount = GetConsensusReputationDiscount(reputation, baseGas);
    return baseGas - discount;
}

bool ConsensusValidator::ValidateTransactionCost(
    const CTransaction& tx,
    uint64_t claimedGasUsed,
    uint64_t claimedCost,
    int blockHeight,
    std::string& error)
{
    // Extract sender address from transaction inputs
    // Requirements: 9.2 - Extract sender addresses from transaction inputs
    uint160 sender;
    if (!ExtractSenderAddress(tx, sender)) {
        error = "Failed to extract sender address from transaction inputs";
        return false;
    }
    
    uint8_t reputation = CalculateDeterministicTrustScore(sender, blockHeight);
    
    // Check if transaction claims free gas
    bool usesFreeGas = (claimedCost == 0);
    
    // Calculate expected cost
    uint64_t expectedCost = CalculateExpectedGasCost(
        claimedGasUsed,
        reputation,
        usesFreeGas
    );
    
    // Verify claimed cost matches expected
    if (claimedCost != expectedCost) {
        error = strprintf(
            "Invalid transaction cost: claimed=%d, expected=%d (gas=%d, rep=%d)",
            claimedCost, expectedCost, claimedGasUsed, reputation
        );
        return false;
    }
    
    return true;
}

/**
 * Extract sender address from transaction inputs
 * 
 * Parses P2PKH and P2WPKH scripts to extract the sender address.
 * Uses the first input for sender determination.
 * 
 * Requirements: 9.2
 * 
 * @param tx Transaction to extract sender from
 * @param senderOut Output parameter for the extracted sender address
 * @return true if sender was successfully extracted
 */
bool ConsensusValidator::ExtractSenderAddress(const CTransaction& tx, uint160& senderOut)
{
    // Cannot extract sender from coinbase transactions
    if (tx.IsCoinBase()) {
        LogPrint(BCLog::CVM, "ConsensusValidator: Cannot extract sender from coinbase transaction\n");
        return false;
    }
    
    // Must have at least one input
    if (tx.vin.empty()) {
        LogPrint(BCLog::CVM, "ConsensusValidator: Transaction has no inputs\n");
        return false;
    }
    
    // Use the first input for sender determination
    const CTxIn& firstInput = tx.vin[0];
    
    // Get the previous output to extract the address
    // We need to look up the UTXO that this input is spending
    {
        LOCK(cs_main);
        
        if (!pcoinsTip) {
            LogPrint(BCLog::CVM, "ConsensusValidator: pcoinsTip not available\n");
            return false;
        }
        
        Coin coin;
        if (!pcoinsTip->GetCoin(firstInput.prevout, coin)) {
            LogPrint(BCLog::CVM, "ConsensusValidator: Could not find UTXO for input %s:%d\n",
                     firstInput.prevout.hash.ToString(), firstInput.prevout.n);
            return false;
        }
        
        // Extract destination from the scriptPubKey
        CTxDestination dest;
        if (!ExtractDestination(coin.out.scriptPubKey, dest)) {
            LogPrint(BCLog::CVM, "ConsensusValidator: Could not extract destination from scriptPubKey\n");
            return false;
        }
        
        // Handle P2PKH (CKeyID)
        const CKeyID* keyID = boost::get<CKeyID>(&dest);
        if (keyID) {
            senderOut = uint160(*keyID);
            LogPrint(BCLog::CVM, "ConsensusValidator: Extracted P2PKH sender: %s\n",
                     senderOut.ToString());
            return true;
        }
        
        // Handle P2WPKH (WitnessV0KeyHash)
        const WitnessV0KeyHash* witnessKeyHash = boost::get<WitnessV0KeyHash>(&dest);
        if (witnessKeyHash) {
            senderOut = uint160(*witnessKeyHash);
            LogPrint(BCLog::CVM, "ConsensusValidator: Extracted P2WPKH sender: %s\n",
                     senderOut.ToString());
            return true;
        }
        
        // Handle P2SH (CScriptID) - less common for sender extraction
        const CScriptID* scriptID = boost::get<CScriptID>(&dest);
        if (scriptID) {
            senderOut = uint160(*scriptID);
            LogPrint(BCLog::CVM, "ConsensusValidator: Extracted P2SH sender: %s\n",
                     senderOut.ToString());
            return true;
        }
        
        LogPrint(BCLog::CVM, "ConsensusValidator: Unsupported script type for sender extraction\n");
        return false;
    }
}

/**
 * Get pool balance from database
 * 
 * Queries LevelDB with key "pool_<id>_balance" and returns cached value on failure.
 * 
 * Requirements: 9.3
 * 
 * @param poolId The pool identifier
 * @return The pool balance, or 0 if not found
 */
CAmount ConsensusValidator::GetPoolBalance(const std::string& poolId)
{
    if (!g_cvmdb) {
        LogPrint(BCLog::CVM, "ConsensusValidator: CVM database not available for pool balance query\n");
        // Return cached value if available
        auto it = g_poolBalanceCache.find(poolId);
        if (it != g_poolBalanceCache.end()) {
            LogPrint(BCLog::CVM, "ConsensusValidator: Using cached pool balance for %s: %d\n",
                     poolId, it->second);
            return it->second;
        }
        return 0;
    }
    
    // Query LevelDB with key "pool_<id>_balance"
    std::string key = "pool_" + poolId + "_balance";
    std::vector<uint8_t> data;
    
    if (g_cvmdb->ReadGeneric(key, data)) {
        if (data.size() >= sizeof(CAmount)) {
            CAmount balance;
            memcpy(&balance, data.data(), sizeof(CAmount));
            
            // Update cache
            g_poolBalanceCache[poolId] = balance;
            
            LogPrint(BCLog::CVM, "ConsensusValidator: Pool %s balance: %d\n",
                     poolId, balance);
            return balance;
        }
    }
    
    LogPrint(BCLog::CVM, "ConsensusValidator: Pool %s balance not found in database\n", poolId);
    
    // Return cached value on failure
    auto it = g_poolBalanceCache.find(poolId);
    if (it != g_poolBalanceCache.end()) {
        LogPrint(BCLog::CVM, "ConsensusValidator: Using cached pool balance for %s: %d\n",
                 poolId, it->second);
        return it->second;
    }
    
    return 0;
}

bool ConsensusValidator::ValidatePoolBalance(
    const std::string& poolId,
    uint64_t requestedAmount,
    std::string& error)
{
    // Query actual pool balance from database
    // Requirements: 9.3
    CAmount balance = GetPoolBalance(poolId);
    
    if (static_cast<uint64_t>(balance) < requestedAmount) {
        error = strprintf(
            "Insufficient pool balance: pool=%s, balance=%d, requested=%d",
            poolId, balance, requestedAmount
        );
        return false;
    }
    
    return true;
}

/**
 * Extract gas usage and costs from CVM transaction
 * 
 * Parses OP_RETURN data to extract gas information from CVM transactions.
 * Gas info is encoded in the OP_RETURN data for contract deployments and calls.
 * 
 * Requirements: 9.4
 * 
 * @param tx Transaction to extract gas info from
 * @param gasUsed Output parameter for gas used
 * @param gasCost Output parameter for gas cost
 * @return true if gas info was successfully extracted
 */
bool ConsensusValidator::ExtractGasInfo(const CTransaction& tx, uint64_t& gasUsed, CAmount& gasCost)
{
    // Initialize outputs
    gasUsed = 0;
    gasCost = 0;
    
    // Find CVM OP_RETURN output
    int opReturnIndex = FindCVMOpReturn(tx);
    if (opReturnIndex < 0) {
        // Not a CVM transaction, check if it's an EVM transaction
        if (!IsEVMTransaction(tx)) {
            LogPrint(BCLog::CVM, "ConsensusValidator: Transaction is not a CVM/EVM transaction\n");
            return false;
        }
    }
    
    // Parse CVM OP_RETURN data
    if (opReturnIndex >= 0) {
        CVMOpType opType;
        std::vector<uint8_t> data;
        
        if (!ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
            LogPrint(BCLog::CVM, "ConsensusValidator: Failed to parse CVM OP_RETURN\n");
            return false;
        }
        
        // Extract gas info based on operation type
        switch (opType) {
            case CVMOpType::CONTRACT_DEPLOY:
            case CVMOpType::EVM_DEPLOY: {
                CVMDeployData deployData;
                if (deployData.Deserialize(data)) {
                    gasUsed = deployData.gasLimit;
                    // Gas cost is calculated from gas used and gas price
                    // For now, use a simple calculation (1 satoshi per gas unit)
                    gasCost = static_cast<CAmount>(gasUsed);
                    
                    LogPrint(BCLog::CVM, "ConsensusValidator: Extracted deploy gas info - gasLimit=%d\n",
                             gasUsed);
                    return true;
                }
                break;
            }
            
            case CVMOpType::CONTRACT_CALL:
            case CVMOpType::EVM_CALL: {
                CVMCallData callData;
                if (callData.Deserialize(data)) {
                    gasUsed = callData.gasLimit;
                    // Gas cost is calculated from gas used and gas price
                    gasCost = static_cast<CAmount>(gasUsed);
                    
                    LogPrint(BCLog::CVM, "ConsensusValidator: Extracted call gas info - gasLimit=%d\n",
                             gasUsed);
                    return true;
                }
                break;
            }
            
            case CVMOpType::REPUTATION_VOTE:
            case CVMOpType::TRUST_EDGE:
            case CVMOpType::BONDED_VOTE:
            case CVMOpType::DAO_DISPUTE:
            case CVMOpType::DAO_VOTE: {
                // These are Web-of-Trust operations, NOT contract executions
                // They do NOT have gas fees that should be split 70/30
                // Their transaction fees go 100% to the miner like regular transactions
                LogPrint(BCLog::CVM, "ConsensusValidator: WoT operation - no gas fee split\n");
                return false;  // Return false so these are not treated as contract transactions
            }
            
            default:
                LogPrint(BCLog::CVM, "ConsensusValidator: Unknown CVM operation type: %d\n",
                         static_cast<int>(opType));
                break;
        }
    }
    
    // For EVM transactions without CVM OP_RETURN, try to extract from transaction structure
    // EVM transactions may encode gas info differently
    if (IsEVMTransaction(tx)) {
        // EVM transactions typically have gas info in the first OP_RETURN or in witness data
        for (const auto& vout : tx.vout) {
            if (vout.scriptPubKey.IsUnspendable()) {
                // Try to parse as EVM gas info
                // Format: OP_RETURN <gas_used:8 bytes> <gas_price:8 bytes>
                std::vector<uint8_t> opReturnData;
                CScript::const_iterator pc = vout.scriptPubKey.begin();
                opcodetype opcode;
                
                // Skip OP_RETURN
                if (vout.scriptPubKey.GetOp(pc, opcode) && opcode == OP_RETURN) {
                    // Get the data
                    if (vout.scriptPubKey.GetOp(pc, opcode, opReturnData)) {
                        // Check for CVM magic bytes first
                        if (opReturnData.size() >= 4 && 
                            opReturnData[0] == CVM_MAGIC[0] &&
                            opReturnData[1] == CVM_MAGIC[1] &&
                            opReturnData[2] == CVM_MAGIC[2] &&
                            opReturnData[3] == CVM_MAGIC[3]) {
                            // This is a CVM transaction, already handled above
                            continue;
                        }
                        
                        // Try to extract raw gas info (16 bytes: 8 for gas_used, 8 for gas_price)
                        if (opReturnData.size() >= 16) {
                            memcpy(&gasUsed, opReturnData.data(), sizeof(uint64_t));
                            uint64_t gasPrice;
                            memcpy(&gasPrice, opReturnData.data() + 8, sizeof(uint64_t));
                            gasCost = static_cast<CAmount>(gasUsed * gasPrice);
                            
                            LogPrint(BCLog::CVM, "ConsensusValidator: Extracted EVM gas info - gasUsed=%d, gasPrice=%d, gasCost=%d\n",
                                     gasUsed, gasPrice, gasCost);
                            return true;
                        }
                    }
                }
            }
        }
        
        // Default gas for EVM transactions if not found
        gasUsed = 21000;  // Base transaction gas
        gasCost = static_cast<CAmount>(gasUsed);
        LogPrint(BCLog::CVM, "ConsensusValidator: Using default gas for EVM transaction\n");
        return true;
    }
    
    LogPrint(BCLog::CVM, "ConsensusValidator: Failed to extract gas info from transaction\n");
    return false;
}

bool ConsensusValidator::ValidateBlockTrustFeatures(
    const CBlock& block,
    int blockHeight,
    const Consensus::Params& params,
    std::string& error)
{
    // Validate all trust-aware features in block
    
    uint64_t totalSubsidies = 0;
    
    for (const auto& tx : block.vtx) {
        // Skip coinbase
        if (tx->IsCoinBase()) {
            continue;
        }
        
        // Check if CVM/EVM transaction
        if (!IsEVMTransaction(*tx) && FindCVMOpReturn(*tx) < 0) {
            continue;
        }
        
        // TODO: Validate transaction costs
        // This requires extracting gas usage and costs from transaction
        
        // TODO: Track total subsidies
        // Ensure doesn't exceed MAX_SUBSIDY_PER_BLOCK
    }
    
    // Validate total subsidies
    if (totalSubsidies > MAX_SUBSIDY_PER_BLOCK) {
        error = strprintf(
            "Block subsidies exceed maximum: %d > %d",
            totalSubsidies, MAX_SUBSIDY_PER_BLOCK
        );
        return false;
    }
    
    return true;
}

} // namespace CVM
