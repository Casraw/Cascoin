// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/fee_calculator.h>
#include <cvm/cvm.h>
#include <cvm/trust_context.h>
#include <cvm/reputation.h>
#include <policy/fees.h>
#include <validation.h>
#include <util.h>
#include <utilstrencodings.h>

namespace CVM {

FeeCalculator::FeeCalculator()
    : m_db(nullptr)
{
    m_trustContext = std::make_shared<TrustContext>();
    m_gasSystem = std::make_unique<cvm::SustainableGasSystem>();
    m_gasAllowanceTracker = std::make_unique<GasAllowanceTracker>();
    m_gasSubsidyTracker = std::make_unique<GasSubsidyTracker>();
}

FeeCalculator::~FeeCalculator() = default;

void FeeCalculator::Initialize(CVMDatabase* db)
{
    m_db = db;
    if (m_gasAllowanceTracker && db) {
        m_gasAllowanceTracker->LoadFromDatabase(*db);
    }
    if (m_gasSubsidyTracker && db) {
        m_gasSubsidyTracker->LoadFromDatabase(*db);
    }
}

FeeCalculationResult FeeCalculator::CalculateFee(
    const CTransaction& tx,
    int currentHeight)
{
    FeeCalculationResult result;
    
    // Check if this is a CVM/EVM transaction
    if (!IsEVMTransaction(tx) && FindCVMOpReturn(tx) < 0) {
        result.error = "Not a CVM/EVM transaction";
        return result;
    }
    
    // Extract gas limit
    result.gasLimit = ExtractGasLimit(tx);
    if (result.gasLimit == 0) {
        result.error = "Invalid gas limit";
        return result;
    }
    
    // Get sender address and reputation
    uint160 senderAddr = GetSenderAddress(tx);
    result.reputation = GetReputation(senderAddr);
    
    // Check for free gas eligibility (80+ reputation)
    if (IsEligibleForFreeGas(tx, senderAddr, result.gasLimit, currentHeight)) {
        result.isFreeGas = true;
        result.effectiveFee = 0;
        result.gasPrice = 0;
        
        LogPrint(BCLog::CVM, "FeeCalculator: Transaction %s eligible for free gas (reputation=%d)\n",
                 tx.GetHash().ToString(), result.reputation);
        return result;
    }
    
    // Get network load for dynamic pricing
    uint64_t networkLoad = GetNetworkLoad();
    
    // Check for price guarantee
    uint64_t guaranteedPrice = 0;
    result.hasPriceGuarantee = HasPriceGuarantee(senderAddr, guaranteedPrice, currentHeight);
    
    // Calculate gas price
    if (result.hasPriceGuarantee) {
        result.gasPrice = guaranteedPrice;
    } else {
        result.gasPrice = EstimateGasPrice(result.reputation, networkLoad);
    }
    
    // Calculate base fee (gas limit * gas price)
    result.baseFee = GasToSatoshis(result.gasLimit, result.gasPrice);
    
    // Apply reputation discount
    result.reputationDiscount = CalculateReputationDiscount(result.baseFee, result.reputation);
    
    // Calculate gas subsidy
    result.gasSubsidy = CalculateGasSubsidy(tx, senderAddr, result.gasLimit, result.reputation);
    result.hasSubsidy = (result.gasSubsidy > 0);
    
    // Calculate effective fee
    result.effectiveFee = result.baseFee - result.reputationDiscount - result.gasSubsidy;
    
    // Ensure fee is non-negative
    if (result.effectiveFee < 0) {
        result.effectiveFee = 0;
    }
    
    LogPrint(BCLog::CVM, "FeeCalculator: tx=%s baseFee=%d discount=%d subsidy=%d effective=%d reputation=%d\n",
             tx.GetHash().ToString(), result.baseFee, result.reputationDiscount, 
             result.gasSubsidy, result.effectiveFee, result.reputation);
    
    return result;
}

CAmount FeeCalculator::GetMinimumFee(
    const CTransaction& tx,
    uint8_t reputation,
    int currentHeight)
{
    // For free gas eligible transactions, minimum fee is 0
    uint160 senderAddr = GetSenderAddress(tx);
    uint64_t gasLimit = ExtractGasLimit(tx);
    
    if (IsEligibleForFreeGas(tx, senderAddr, gasLimit, currentHeight)) {
        return 0;
    }
    
    // Calculate minimum fee based on reputation
    uint64_t networkLoad = GetNetworkLoad();
    uint64_t gasPrice = EstimateGasPrice(reputation, networkLoad);
    
    // Minimum fee is 10% of base fee (to prevent spam)
    CAmount baseFee = GasToSatoshis(gasLimit, gasPrice);
    CAmount minFee = baseFee / 10;
    
    // Apply reputation discount
    CAmount discount = CalculateReputationDiscount(minFee, reputation);
    CAmount effectiveFee = minFee - discount;
    
    // Ensure minimum of 1 satoshi for non-free transactions
    return std::max<CAmount>(effectiveFee, 1);
}

CAmount FeeCalculator::GetRequiredFee(
    const CTransaction& tx,
    unsigned int nTxSize,
    uint8_t reputation,
    int currentHeight)
{
    // Check if CVM/EVM transaction
    if (!IsEVMTransaction(tx) && FindCVMOpReturn(tx) < 0) {
        // Not a CVM/EVM transaction, use standard Bitcoin fee calculation
        return ::minRelayTxFee.GetFee(nTxSize);
    }
    
    // For CVM/EVM transactions, use our fee calculator
    FeeCalculationResult result = CalculateFee(tx, currentHeight);
    
    if (!result.IsValid()) {
        // Fallback to standard fee if calculation fails
        LogPrint(BCLog::CVM, "FeeCalculator: Fee calculation failed: %s, using standard fee\n", 
                 result.error);
        return ::minRelayTxFee.GetFee(nTxSize);
    }
    
    return result.effectiveFee;
}

uint64_t FeeCalculator::EstimateGasPrice(
    uint8_t reputation,
    uint64_t networkLoad)
{
    if (!m_gasSystem) {
        return 10000000; // Default 0.01 gwei
    }
    
    return m_gasSystem->GetPredictableGasPrice(reputation, networkLoad);
}

uint64_t FeeCalculator::EstimateGasPriceForTransaction(const CTransaction& tx)
{
    uint160 senderAddr = GetSenderAddress(tx);
    uint8_t reputation = GetReputation(senderAddr);
    uint64_t networkLoad = GetNetworkLoad();
    
    return EstimateGasPrice(reputation, networkLoad);
}

bool FeeCalculator::IsEligibleForFreeGas(
    const CTransaction& tx,
    const uint160& senderAddr,
    uint64_t gasLimit,
    int currentHeight)
{
    // Check reputation threshold (80+)
    uint8_t reputation = GetReputation(senderAddr);
    if (!m_gasSystem || !m_gasSystem->IsEligibleForFreeGas(reputation)) {
        return false;
    }
    
    // Check remaining free gas allowance
    if (!m_gasAllowanceTracker || !m_trustContext) {
        return false;
    }
    
    return m_gasAllowanceTracker->HasSufficientAllowance(senderAddr, gasLimit, *m_trustContext, currentHeight);
}

uint64_t FeeCalculator::GetRemainingFreeGas(
    const uint160& address,
    int currentHeight)
{
    if (!m_gasAllowanceTracker || !m_trustContext) {
        return 0;
    }
    
    auto state = m_gasAllowanceTracker->GetAllowanceState(address, *m_trustContext, currentHeight);
    return (state.dailyAllowance > state.usedToday) ? (state.dailyAllowance - state.usedToday) : 0;
}

CAmount FeeCalculator::CalculateGasSubsidy(
    const CTransaction& tx,
    const uint160& senderAddr,
    uint64_t gasLimit,
    uint8_t reputation)
{
    if (!m_gasSubsidyTracker || !m_trustContext) {
        return 0;
    }
    
    // Check if operation is beneficial
    bool isBeneficial = (reputation >= 80); // Simplified check
    
    // Calculate subsidy based on gas used and trust context
    uint64_t subsidyGas = m_gasSubsidyTracker->CalculateSubsidy(gasLimit, *m_trustContext, isBeneficial);
    
    // Convert gas subsidy to satoshis
    uint64_t networkLoad = GetNetworkLoad();
    uint64_t gasPrice = EstimateGasPrice(reputation, networkLoad);
    return GasToSatoshis(subsidyGas, gasPrice);
}

bool FeeCalculator::ValidateGasSubsidy(
    const CTransaction& tx,
    const GasSubsidyTracker::SubsidyRecord& subsidy)
{
    if (!m_gasSubsidyTracker) {
        return false;
    }
    
    // Validate subsidy record has valid data
    if (subsidy.gasUsed == 0 || subsidy.subsidyAmount == 0) {
        return false;
    }
    
    // Check if subsidy amount is reasonable (not more than gas used)
    return (subsidy.subsidyAmount <= subsidy.gasUsed);
}

bool FeeCalculator::HasPriceGuarantee(
    const uint160& address,
    uint64_t& guaranteedPrice,
    int currentHeight)
{
    if (!m_gasSystem) {
        return false;
    }
    
    return m_gasSystem->HasPriceGuarantee(address, guaranteedPrice, currentHeight);
}

CAmount FeeCalculator::ApplyPriceGuarantee(
    CAmount baseFee,
    uint64_t guaranteedPrice,
    uint64_t gasLimit)
{
    // Calculate fee with guaranteed price
    CAmount guaranteedFee = GasToSatoshis(gasLimit, guaranteedPrice);
    
    // Return lower of base fee or guaranteed fee
    return std::min(baseFee, guaranteedFee);
}

CAmount FeeCalculator::CalculateReputationDiscount(
    CAmount baseFee,
    uint8_t reputation)
{
    double multiplier = GetReputationMultiplier(reputation);
    
    // Discount is (1 - multiplier) * baseFee
    double discountPercent = 1.0 - multiplier;
    CAmount discount = static_cast<CAmount>(baseFee * discountPercent);
    
    return discount;
}

double FeeCalculator::GetReputationMultiplier(uint8_t reputation)
{
    // Reputation-based multiplier (lower is better)
    // 90-100: 0.5x (50% discount)
    // 80-89:  0.6x (40% discount)
    // 70-79:  0.7x (30% discount)
    // 60-69:  0.8x (20% discount)
    // 50-59:  0.9x (10% discount)
    // <50:    1.0x (no discount)
    
    if (reputation >= 90) return 0.5;
    if (reputation >= 80) return 0.6;
    if (reputation >= 70) return 0.7;
    if (reputation >= 60) return 0.8;
    if (reputation >= 50) return 0.9;
    return 1.0;
}

CAmount FeeCalculator::GasToSatoshis(uint64_t gasAmount, uint64_t gasPrice)
{
    // Gas price is in wei (smallest unit)
    // We need to convert to satoshis
    // 1 CAS = 100,000,000 satoshis
    // Gas price is typically in gwei (1e9 wei)
    
    // Total cost in wei = gasAmount * gasPrice
    uint64_t totalWei = gasAmount * gasPrice;
    
    // Convert wei to satoshis
    // Assuming 1 CAS = 1 ETH for simplicity
    // 1 ETH = 1e18 wei
    // 1 CAS = 1e8 satoshis
    // Therefore: satoshis = (wei * 1e8) / 1e18 = wei / 1e10
    
    uint64_t satoshis = totalWei / 10000000000ULL; // Divide by 1e10
    
    // Ensure minimum of 1 satoshi for non-zero gas
    if (totalWei > 0 && satoshis == 0) {
        satoshis = 1;
    }
    
    return static_cast<CAmount>(satoshis);
}

uint64_t FeeCalculator::SatoshisToGas(CAmount satoshis, uint64_t gasPrice)
{
    if (gasPrice == 0) {
        return 0;
    }
    
    // Reverse of GasToSatoshis
    // wei = satoshis * 1e10
    uint64_t totalWei = static_cast<uint64_t>(satoshis) * 10000000000ULL;
    
    // gasAmount = totalWei / gasPrice
    return totalWei / gasPrice;
}

uint64_t FeeCalculator::ExtractGasLimit(const CTransaction& tx)
{
    // Find CVM OP_RETURN output
    int opReturnIndex = FindCVMOpReturn(tx);
    if (opReturnIndex < 0) {
        return 0;
    }
    
    // Parse OP_RETURN data
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
        return 0;
    }
    
    // Extract gas limit based on operation type
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

uint160 FeeCalculator::GetSenderAddress(const CTransaction& tx)
{
    // Get sender from first input
    if (tx.vin.empty()) {
        return uint160();
    }
    
    // Look up the input's previous output from UTXO set
    // In Bitcoin-based systems, we need access to CCoinsViewCache to look up previous outputs
    // Since we don't have direct access here, we'll use a simplified approach
    
    // For CVM/EVM transactions, the sender is typically encoded in the transaction
    // or we need to look it up from the blockchain state
    
    // Try to extract from CVM OP_RETURN data first
    int opReturnIndex = FindCVMOpReturn(tx);
    if (opReturnIndex >= 0) {
        CVMOpType opType;
        std::vector<uint8_t> data;
        if (ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
            // For deploy operations
            if (opType == CVMOpType::CONTRACT_DEPLOY || opType == CVMOpType::EVM_DEPLOY) {
                CVMDeployData deployData;
                if (deployData.Deserialize(data)) {
                    // Deployer address is typically the first input's address
                    // We'll need to look this up from the UTXO set in a full implementation
                    // For now, return empty - this will be filled in by validation.cpp
                    // which has access to CCoinsViewCache
                    return uint160();
                }
            }
            // For call operations
            else if (opType == CVMOpType::CONTRACT_CALL || opType == CVMOpType::EVM_CALL) {
                CVMCallData callData;
                if (callData.Deserialize(data)) {
                    // Caller address needs to be looked up from UTXO set
                    return uint160();
                }
            }
        }
    }
    
    // Fallback: return empty address
    // The caller (validation.cpp) should provide the address when it has access to UTXO set
    return uint160();
}

uint8_t FeeCalculator::GetReputation(const uint160& address)
{
    if (!m_db) {
        LogPrint(BCLog::CVM, "FeeCalculator::GetReputation: No database available\n");
        return 50; // Default medium reputation
    }
    
    // Use TrustContext to get reputation score
    if (m_trustContext) {
        uint32_t reputation = m_trustContext->GetReputation(address);
        
        // TrustContext returns 0-100 scale, which matches our uint8_t
        // Clamp to 0-100 range
        if (reputation > 100) {
            reputation = 100;
        }
        
        LogPrint(BCLog::CVM, "FeeCalculator::GetReputation: address=%s reputation=%d\n",
                 address.ToString(), reputation);
        
        return static_cast<uint8_t>(reputation);
    }
    
    // Fallback: try to query from reputation system directly
    try {
        ReputationSystem repSystem(*m_db);
        ReputationScore score;
        
        if (repSystem.GetReputation(address, score)) {
            // ReputationScore uses -10000 to +10000 scale
            // Convert to 0-100 scale
            // Map: -10000 -> 0, 0 -> 50, +10000 -> 100
            int64_t normalized = (score.score + 10000) * 100 / 20000;
            
            // Clamp to 0-100
            if (normalized < 0) normalized = 0;
            if (normalized > 100) normalized = 100;
            
            LogPrint(BCLog::CVM, "FeeCalculator::GetReputation: address=%s score=%d normalized=%d\n",
                     address.ToString(), score.score, normalized);
            
            return static_cast<uint8_t>(normalized);
        }
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "FeeCalculator::GetReputation: Error querying reputation: %s\n", e.what());
    }
    
    // Default to medium reputation if not found
    LogPrint(BCLog::CVM, "FeeCalculator::GetReputation: No reputation found for %s, using default\n",
             address.ToString());
    return 50;
}

uint64_t FeeCalculator::GetNetworkLoad()
{
    // Calculate current network load based on mempool size and block fullness
    // Returns value from 0 (empty) to 100 (full)
    
    // Access global mempool if available
    // Note: In Bitcoin Core, mempool is typically accessed via a global pointer
    // We'll use a simplified calculation based on available metrics
    
    uint64_t load = 50; // Default moderate load
    
    try {
        // Try to access mempool statistics
        // In a full implementation, we would:
        // 1. Get mempool size (number of transactions)
        // 2. Get mempool memory usage
        // 3. Get recent block fullness
        // 4. Calculate load as percentage of capacity
        
        // For now, we'll use a heuristic based on sustainable gas system
        if (m_gasSystem) {
            // Check if we have congestion data
            // The sustainable gas system tracks network congestion
            // We can infer load from gas price trends
            
            // Get current gas price for medium reputation
            uint64_t currentPrice = m_gasSystem->GetPredictableGasPrice(50, 50);
            uint64_t basePrice = m_gasSystem->GetGasParameters().baseGasPrice;
            
            // Calculate load based on price ratio
            // If current price is 2x base price, network is at 100% load
            if (basePrice > 0) {
                uint64_t priceRatio = (currentPrice * 100) / basePrice;
                
                // Map price ratio to load percentage
                // 100% ratio = 0% load (base price)
                // 200% ratio = 100% load (2x base price)
                if (priceRatio >= 100) {
                    load = (priceRatio - 100);
                    if (load > 100) load = 100;
                } else {
                    load = 0; // Below base price means no load
                }
                
                LogPrint(BCLog::CVM, "FeeCalculator::GetNetworkLoad: currentPrice=%d basePrice=%d ratio=%d load=%d\n",
                         currentPrice, basePrice, priceRatio, load);
                
                return load;
            }
        }
        
        // Fallback: check if we can access mempool size
        // This would require access to the global mempool object
        // For now, return moderate load
        LogPrint(BCLog::CVM, "FeeCalculator::GetNetworkLoad: Using default load=%d\n", load);
        
    } catch (const std::exception& e) {
        LogPrint(BCLog::CVM, "FeeCalculator::GetNetworkLoad: Error calculating load: %s\n", e.what());
    }
    
    return load;
}

uint64_t FeeCalculator::GetGasToSatoshiRate()
{
    // This would be dynamically adjusted based on CAS/USD price
    // For now, return default rate
    return DEFAULT_GAS_TO_SATOSHI_RATE;
}

} // namespace CVM
