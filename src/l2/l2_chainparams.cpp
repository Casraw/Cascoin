// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <l2/l2_chainparams.h>
#include <chainparamsbase.h>

namespace l2 {

// Mainnet L2 Parameters
static L2Params mainnetL2Params = {
    // === Sequencer Parameters ===
    .nMinSequencerHATScore = 70,                    // Minimum HAT score of 70
    .nMinSequencerStake = 100 * COIN,               // 100 CAS minimum stake
    .nMinSequencerPeerCount = 3,                    // At least 3 peers
    .nBlocksPerLeader = 10,                         // Rotate every 10 blocks
    .nLeaderTimeoutSeconds = 3,                     // 3 second timeout
    .nMinActiveSequencers = 3,                      // Minimum 3 sequencers
    
    // === Consensus Parameters ===
    .nConsensusThresholdPercent = 67,               // 2/3 majority
    .nDecryptionThresholdPercent = 67,              // 2/3 for decryption
    
    // === Block Parameters ===
    .nTargetBlockTimeMs = 500,                      // 500ms block time
    .nMaxBlockGas = 30000000,                       // 30M gas per block
    .nMaxTxGas = 1000000,                           // 1M gas per tx
    .nL1AnchorInterval = 100,                       // Anchor every 100 L2 blocks
    
    // === Bridge Parameters ===
    .nStandardChallengePeriod = 7 * 24 * 60 * 60,   // 7 days
    .nFastChallengePeriod = 1 * 24 * 60 * 60,       // 1 day for high rep
    .nFastWithdrawalHATThreshold = 80,              // HAT score > 80
    .nMaxDepositPerTx = 10000 * COIN,               // 10,000 CAS
    .nMaxDailyDeposit = 100000 * COIN,              // 100,000 CAS
    .nMaxWithdrawalPerTx = 10000 * COIN,            // 10,000 CAS
    .nLargeWithdrawalThreshold = 50000 * COIN,      // 50,000 CAS
    .nChallengeBond = 10 * COIN,                    // 10 CAS
    
    // === Rate Limiting ===
    .nMaxTxPerBlockNewAddress = 100,                // 100 tx/block for new
    .nMaxTxPerBlockHighRep = 500,                   // 500 tx/block for high rep
    .nRateLimitHATThreshold = 70,                   // HAT score > 70
    
    // === Reputation Parameters ===
    .nGasDiscountHATThreshold = 80,                 // HAT score > 80
    .nGasDiscountPercent = 50,                      // 50% discount
    .nInstantFinalityHATThreshold = 80,             // HAT score > 80
    
    // === State Management ===
    .nStateRentRate = 1,                            // 1 satoshi/byte/year
    .nArchiveThresholdBlocks = 365 * 24 * 60 * 60 / 150 * 60, // ~1 year
    .nMaxContractStorageSize = 1024 * 1024,         // 1 MB
    
    // === Data Availability ===
    .nMaxBatchSize = 128 * 1024,                    // 128 KB
    .nBatchInterval = 100,                          // Every 100 L2 blocks
    
    // === Timestamp Security ===
    .nMaxTimestampDrift = 15 * 60,                  // 15 minutes
    .nMaxFutureTimestamp = 30,                      // 30 seconds
    
    // === Emergency Parameters ===
    .nEmergencyModeHours = 24,                      // 24 hours
    .nCircuitBreakerWithdrawalPercent = 10,         // 10% of TVL
    
    // === L1 Finality ===
    .nL1FinalityConfirmations = 6,                  // 6 confirmations
    
    // === Fee Distribution ===
    .nFeeToActiveSequencerPercent = 70,             // 70% to active
    .nFeeToOtherSequencersPercent = 20,             // 20% to others
    .nFeeBurnPercent = 10                           // 10% burned
};

// Testnet L2 Parameters (slightly relaxed)
static L2Params testnetL2Params = {
    // === Sequencer Parameters ===
    .nMinSequencerHATScore = 50,                    // Lower threshold
    .nMinSequencerStake = 10 * COIN,                // 10 CAS minimum
    .nMinSequencerPeerCount = 1,                    // At least 1 peer
    .nBlocksPerLeader = 10,
    .nLeaderTimeoutSeconds = 5,                     // 5 second timeout
    .nMinActiveSequencers = 2,                      // Minimum 2 sequencers
    
    // === Consensus Parameters ===
    .nConsensusThresholdPercent = 67,
    .nDecryptionThresholdPercent = 67,
    
    // === Block Parameters ===
    .nTargetBlockTimeMs = 1000,                     // 1 second block time
    .nMaxBlockGas = 30000000,
    .nMaxTxGas = 1000000,
    .nL1AnchorInterval = 50,                        // More frequent anchoring
    
    // === Bridge Parameters ===
    .nStandardChallengePeriod = 1 * 24 * 60 * 60,   // 1 day
    .nFastChallengePeriod = 1 * 60 * 60,            // 1 hour
    .nFastWithdrawalHATThreshold = 60,
    .nMaxDepositPerTx = 100000 * COIN,
    .nMaxDailyDeposit = 1000000 * COIN,
    .nMaxWithdrawalPerTx = 100000 * COIN,
    .nLargeWithdrawalThreshold = 500000 * COIN,
    .nChallengeBond = 1 * COIN,
    
    // === Rate Limiting ===
    .nMaxTxPerBlockNewAddress = 200,
    .nMaxTxPerBlockHighRep = 1000,
    .nRateLimitHATThreshold = 50,
    
    // === Reputation Parameters ===
    .nGasDiscountHATThreshold = 60,
    .nGasDiscountPercent = 50,
    .nInstantFinalityHATThreshold = 60,
    
    // === State Management ===
    .nStateRentRate = 1,
    .nArchiveThresholdBlocks = 30 * 24 * 60 * 60 / 150 * 60, // ~30 days
    .nMaxContractStorageSize = 1024 * 1024,
    
    // === Data Availability ===
    .nMaxBatchSize = 128 * 1024,
    .nBatchInterval = 50,
    
    // === Timestamp Security ===
    .nMaxTimestampDrift = 30 * 60,                  // 30 minutes
    .nMaxFutureTimestamp = 60,
    
    // === Emergency Parameters ===
    .nEmergencyModeHours = 6,                       // 6 hours
    .nCircuitBreakerWithdrawalPercent = 20,
    
    // === L1 Finality ===
    .nL1FinalityConfirmations = 3,
    
    // === Fee Distribution ===
    .nFeeToActiveSequencerPercent = 70,
    .nFeeToOtherSequencersPercent = 20,
    .nFeeBurnPercent = 10
};

// Regtest L2 Parameters (very relaxed for testing)
static L2Params regtestL2Params = {
    // === Sequencer Parameters ===
    .nMinSequencerHATScore = 0,                     // No minimum for testing
    .nMinSequencerStake = 1 * COIN,                 // 1 CAS minimum
    .nMinSequencerPeerCount = 0,                    // No peer requirement
    .nBlocksPerLeader = 5,                          // Faster rotation
    .nLeaderTimeoutSeconds = 10,                    // 10 second timeout
    .nMinActiveSequencers = 1,                      // Single sequencer OK
    
    // === Consensus Parameters ===
    .nConsensusThresholdPercent = 51,               // Simple majority
    .nDecryptionThresholdPercent = 51,
    
    // === Block Parameters ===
    .nTargetBlockTimeMs = 100,                      // 100ms for fast testing
    .nMaxBlockGas = 30000000,
    .nMaxTxGas = 1000000,
    .nL1AnchorInterval = 10,                        // Very frequent anchoring
    
    // === Bridge Parameters ===
    .nStandardChallengePeriod = 60,                 // 1 minute
    .nFastChallengePeriod = 10,                     // 10 seconds
    .nFastWithdrawalHATThreshold = 0,               // No threshold
    .nMaxDepositPerTx = 1000000 * COIN,             // Very high limits
    .nMaxDailyDeposit = 10000000 * COIN,
    .nMaxWithdrawalPerTx = 1000000 * COIN,
    .nLargeWithdrawalThreshold = 5000000 * COIN,
    .nChallengeBond = CENT,                         // 0.01 CAS
    
    // === Rate Limiting ===
    .nMaxTxPerBlockNewAddress = 10000,              // Effectively unlimited
    .nMaxTxPerBlockHighRep = 10000,
    .nRateLimitHATThreshold = 0,
    
    // === Reputation Parameters ===
    .nGasDiscountHATThreshold = 0,
    .nGasDiscountPercent = 50,
    .nInstantFinalityHATThreshold = 0,
    
    // === State Management ===
    .nStateRentRate = 0,                            // No rent in regtest
    .nArchiveThresholdBlocks = 1000,                // Short archive threshold
    .nMaxContractStorageSize = 10 * 1024 * 1024,    // 10 MB
    
    // === Data Availability ===
    .nMaxBatchSize = 1024 * 1024,                   // 1 MB
    .nBatchInterval = 5,                            // Every 5 blocks
    
    // === Timestamp Security ===
    .nMaxTimestampDrift = 60 * 60,                  // 1 hour
    .nMaxFutureTimestamp = 300,                     // 5 minutes
    
    // === Emergency Parameters ===
    .nEmergencyModeHours = 1,                       // 1 hour
    .nCircuitBreakerWithdrawalPercent = 100,        // Disabled
    
    // === L1 Finality ===
    .nL1FinalityConfirmations = 1,                  // Single confirmation
    
    // === Fee Distribution ===
    .nFeeToActiveSequencerPercent = 70,
    .nFeeToOtherSequencersPercent = 20,
    .nFeeBurnPercent = 10
};

// Currently selected L2 params
static const L2Params* pCurrentL2Params = &mainnetL2Params;

const L2Params& MainnetL2Params()
{
    return mainnetL2Params;
}

const L2Params& TestnetL2Params()
{
    return testnetL2Params;
}

const L2Params& RegtestL2Params()
{
    return regtestL2Params;
}

const L2Params& GetL2Params()
{
    return *pCurrentL2Params;
}

void SelectL2Params(const std::string& network)
{
    if (network == CBaseChainParams::MAIN) {
        pCurrentL2Params = &mainnetL2Params;
    } else if (network == CBaseChainParams::TESTNET) {
        pCurrentL2Params = &testnetL2Params;
    } else if (network == CBaseChainParams::REGTEST) {
        pCurrentL2Params = &regtestL2Params;
    } else {
        // Default to mainnet
        pCurrentL2Params = &mainnetL2Params;
    }
}

} // namespace l2
