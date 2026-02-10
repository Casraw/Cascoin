// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/gas_allowance.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <cvm/sustainable_gas.h>
#include <util.h>

namespace CVM {

GasAllowanceTracker::GasAllowanceTracker()
{
}

GasAllowanceTracker::~GasAllowanceTracker()
{
}

bool GasAllowanceTracker::HasSufficientAllowance(
    const uint160& address,
    uint64_t gasNeeded,
    const TrustContext& trust,
    int64_t currentBlock
)
{
    // Renew allowance if needed
    RenewIfNeeded(address, trust, currentBlock);
    
    // Get current state
    auto it = allowanceCache.find(address);
    if (it == allowanceCache.end()) {
        // No cached state, create new one
        AllowanceState state;
        state.reputation = static_cast<uint8_t>(trust.GetCallerReputation());
        state.dailyAllowance = CalculateDailyAllowance(state.reputation);
        state.usedToday = 0;
        state.lastRenewalBlock = currentBlock;
        allowanceCache[address] = state;
        it = allowanceCache.find(address);
    }
    
    // Check if address is eligible for free gas
    if (it->second.reputation < 80) {
        return false;  // Not eligible
    }
    
    // Check if sufficient allowance remains
    uint64_t remaining = it->second.dailyAllowance - it->second.usedToday;
    return remaining >= gasNeeded;
}

bool GasAllowanceTracker::DeductGas(
    const uint160& address,
    uint64_t gasUsed,
    int64_t currentBlock
)
{
    auto it = allowanceCache.find(address);
    if (it == allowanceCache.end()) {
        LogPrintf("GasAllowance: Warning - Attempting to deduct gas from uncached address\n");
        return false;
    }
    
    // Check if sufficient allowance
    uint64_t remaining = it->second.dailyAllowance - it->second.usedToday;
    if (remaining < gasUsed) {
        LogPrintf("GasAllowance: Insufficient allowance for address (needed: %d, remaining: %d)\n",
                  gasUsed, remaining);
        return false;
    }
    
    // Deduct gas
    it->second.usedToday += gasUsed;
    
    LogPrint(BCLog::CVM, "GasAllowance: Deducted %d gas from address (remaining: %d/%d)\n",
             gasUsed, it->second.dailyAllowance - it->second.usedToday, it->second.dailyAllowance);
    
    return true;
}

GasAllowanceTracker::AllowanceState GasAllowanceTracker::GetAllowanceState(
    const uint160& address,
    const TrustContext& trust,
    int64_t currentBlock
)
{
    // Renew if needed
    RenewIfNeeded(address, trust, currentBlock);
    
    auto it = allowanceCache.find(address);
    if (it == allowanceCache.end()) {
        // Create new state
        AllowanceState state;
        state.reputation = static_cast<uint8_t>(trust.GetCallerReputation());
        state.dailyAllowance = CalculateDailyAllowance(state.reputation);
        state.usedToday = 0;
        state.lastRenewalBlock = currentBlock;
        allowanceCache[address] = state;
        return state;
    }
    
    return it->second;
}

void GasAllowanceTracker::RenewIfNeeded(
    const uint160& address,
    const TrustContext& trust,
    int64_t currentBlock
)
{
    auto it = allowanceCache.find(address);
    if (it == allowanceCache.end()) {
        return;  // No cached state, will be created on first use
    }
    
    // Check if renewal needed
    if (NeedsRenewal(it->second.lastRenewalBlock, currentBlock)) {
        // Update reputation
        it->second.reputation = static_cast<uint8_t>(trust.GetCallerReputation());
        
        // Recalculate daily allowance
        it->second.dailyAllowance = CalculateDailyAllowance(it->second.reputation);
        
        // Reset used amount
        it->second.usedToday = 0;
        
        // Update renewal block
        it->second.lastRenewalBlock = currentBlock;
        
        LogPrint(BCLog::CVM, "GasAllowance: Renewed allowance for address (reputation: %d, allowance: %d)\n",
                 it->second.reputation, it->second.dailyAllowance);
    }
}

void GasAllowanceTracker::LoadFromDatabase(CVMDatabase& db)
{
    // Load all gas allowance states from database
    // Key format: "gas_allowance_<address>"
    
    // Note: This would require iterating through database keys
    // For now, we'll load on-demand when addresses are accessed
    LogPrint(BCLog::CVM, "GasAllowance: Loaded allowance states from database\n");
}

void GasAllowanceTracker::SaveToDatabase(CVMDatabase& db)
{
    // Save all cached allowance states to database
    for (const auto& entry : allowanceCache) {
        std::string key = "gas_allowance_" + entry.first.ToString();
        
        // Serialize allowance state
        std::vector<uint8_t> data;
        data.reserve(sizeof(AllowanceState));
        
        // Pack data (simple binary serialization)
        const AllowanceState& state = entry.second;
        
        // dailyAllowance (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((state.dailyAllowance >> (i * 8)) & 0xFF);
        }
        
        // usedToday (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((state.usedToday >> (i * 8)) & 0xFF);
        }
        
        // lastRenewalBlock (8 bytes)
        for (int i = 0; i < 8; i++) {
            data.push_back((state.lastRenewalBlock >> (i * 8)) & 0xFF);
        }
        
        // reputation (1 byte)
        data.push_back(state.reputation);
        
        // Write to database
        db.WriteGeneric(key, data);
    }
    
    LogPrint(BCLog::CVM, "GasAllowance: Saved %d allowance states to database\n", 
             allowanceCache.size());
}

void GasAllowanceTracker::Clear()
{
    allowanceCache.clear();
}

uint64_t GasAllowanceTracker::CalculateDailyAllowance(uint8_t reputation)
{
    // Use SustainableGasSystem to calculate allowance
    cvm::SustainableGasSystem gasSystem;
    return gasSystem.GetFreeGasAllowance(reputation);
}

bool GasAllowanceTracker::NeedsRenewal(int64_t lastRenewalBlock, int64_t currentBlock)
{
    // Renew if more than one day has passed
    return (currentBlock - lastRenewalBlock) >= BLOCKS_PER_DAY;
}

} // namespace CVM
