// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_CHAINPARAMS_H
#define CASCOIN_L2_CHAINPARAMS_H

/**
 * @file l2_chainparams.h
 * @brief L2-specific chain parameters for Cascoin Layer 2
 * 
 * This file defines the L2-specific parameters that extend the base
 * CChainParams. These parameters control sequencer requirements,
 * challenge periods, gas limits, and other L2-specific settings.
 */

#include <amount.h>
#include <uint256.h>

#include <cstdint>
#include <string>

namespace l2 {

/**
 * L2-specific chain parameters
 * These parameters are network-specific (mainnet/testnet/regtest)
 */
struct L2Params {
    // === Sequencer Parameters ===
    
    /** Minimum HAT v2 score required to be a sequencer */
    uint32_t nMinSequencerHATScore;
    
    /** Minimum stake required to be a sequencer (in satoshis) */
    CAmount nMinSequencerStake;
    
    /** Minimum number of peers required for sequencer eligibility */
    uint32_t nMinSequencerPeerCount;
    
    /** Number of blocks each leader produces before rotation */
    uint32_t nBlocksPerLeader;
    
    /** Leader timeout in seconds before failover */
    uint32_t nLeaderTimeoutSeconds;
    
    /** Minimum number of active sequencers for redundancy */
    uint32_t nMinActiveSequencers;
    
    // === Consensus Parameters ===
    
    /** Consensus threshold (percentage * 100, e.g., 67 = 67%) */
    uint32_t nConsensusThresholdPercent;
    
    /** Decryption threshold for encrypted mempool (percentage * 100) */
    uint32_t nDecryptionThresholdPercent;
    
    // === Block Parameters ===
    
    /** Target L2 block time in milliseconds */
    uint32_t nTargetBlockTimeMs;
    
    /** Maximum gas per L2 block */
    uint64_t nMaxBlockGas;
    
    /** Maximum gas per L2 transaction */
    uint64_t nMaxTxGas;
    
    /** L2 blocks between L1 state root submissions */
    uint32_t nL1AnchorInterval;
    
    // === Bridge Parameters ===
    
    /** Standard challenge period for withdrawals (seconds) */
    uint64_t nStandardChallengePeriod;
    
    /** Fast challenge period for high-reputation users (seconds) */
    uint64_t nFastChallengePeriod;
    
    /** HAT score threshold for fast withdrawals */
    uint32_t nFastWithdrawalHATThreshold;
    
    /** Maximum deposit per transaction (satoshis) */
    CAmount nMaxDepositPerTx;
    
    /** Maximum daily deposit per address (satoshis) */
    CAmount nMaxDailyDeposit;
    
    /** Maximum withdrawal per transaction (satoshis) */
    CAmount nMaxWithdrawalPerTx;
    
    /** Large withdrawal threshold requiring extra verification (satoshis) */
    CAmount nLargeWithdrawalThreshold;
    
    /** Challenge bond required for fraud proofs (satoshis) */
    CAmount nChallengeBond;
    
    // === Rate Limiting ===
    
    /** Maximum transactions per block for new addresses */
    uint32_t nMaxTxPerBlockNewAddress;
    
    /** Maximum transactions per block for high-reputation addresses */
    uint32_t nMaxTxPerBlockHighRep;
    
    /** HAT score threshold for increased rate limits */
    uint32_t nRateLimitHATThreshold;
    
    // === Reputation Parameters ===
    
    /** HAT score threshold for gas discount */
    uint32_t nGasDiscountHATThreshold;
    
    /** Gas discount percentage for high-reputation users */
    uint32_t nGasDiscountPercent;
    
    /** HAT score threshold for instant soft-finality */
    uint32_t nInstantFinalityHATThreshold;
    
    // === State Management ===
    
    /** State rent rate (satoshis per byte per year) */
    CAmount nStateRentRate;
    
    /** Inactivity threshold for state archiving (blocks) */
    uint64_t nArchiveThresholdBlocks;
    
    /** Maximum contract storage size (bytes) */
    uint64_t nMaxContractStorageSize;
    
    // === Data Availability ===
    
    /** Maximum batch size for L1 submission (bytes) */
    uint64_t nMaxBatchSize;
    
    /** Batch submission interval (L2 blocks) */
    uint32_t nBatchInterval;
    
    // === Timestamp Security ===
    
    /** Maximum timestamp drift from L1 (seconds) */
    uint32_t nMaxTimestampDrift;
    
    /** Maximum future timestamp allowed (seconds) */
    uint32_t nMaxFutureTimestamp;
    
    // === Emergency Parameters ===
    
    /** Hours of sequencer unavailability before emergency mode */
    uint32_t nEmergencyModeHours;
    
    /** Circuit breaker: max daily withdrawal as percentage of TVL */
    uint32_t nCircuitBreakerWithdrawalPercent;
    
    // === L1 Finality ===
    
    /** L1 confirmations required before L2 state is final */
    uint32_t nL1FinalityConfirmations;
    
    // === Fee Distribution ===
    
    /** Percentage of fees to active sequencer */
    uint32_t nFeeToActiveSequencerPercent;
    
    /** Percentage of fees to other sequencers */
    uint32_t nFeeToOtherSequencersPercent;
    
    /** Percentage of fees burned */
    uint32_t nFeeBurnPercent;
};

/**
 * Get L2 parameters for mainnet
 */
const L2Params& MainnetL2Params();

/**
 * Get L2 parameters for testnet
 */
const L2Params& TestnetL2Params();

/**
 * Get L2 parameters for regtest
 * Note: Regtest uses reduced values for faster testing
 */
const L2Params& RegtestL2Params();

/**
 * Get L2 parameters for the current network
 */
const L2Params& GetL2Params();

/**
 * Initialize L2 parameters based on network selection
 * Called during node initialization
 */
void SelectL2Params(const std::string& network);

} // namespace l2

#endif // CASCOIN_L2_CHAINPARAMS_H
