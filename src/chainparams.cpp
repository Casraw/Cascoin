// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>
#include <base58.h> // Cascoin: Needed for DecodeDestination()

#include <assert.h>

#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Cascoin Official Launch: 13/Apr/2024 - The Future of Hybrid Consensus";
    const CScript genesisOutputScript = CScript() << ParseHex("047a7a73093d1606ee3b5c364975d8fd0798186d21332c1529688b5102abc931f31114fc3106515b417a864f81fc2d020cdccf72f3d77b92ca3093328cbebcade0") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 840000;
        consensus.BIP16Height = 0; // 87afb798a3ad9378fcd56123c81fb31cfd9a8df4719b9774d71730c16315a092 - October 1, 2012
        consensus.BIP34Height = 1; // Activate BIP34 after the genesis block
        consensus.BIP34Hash = uint256S("0xeedd29e9e095e28308e5e138952d7f70cc6f06387bebfa0b3b4fd5a615f5dca9");
        consensus.BIP65Height = 0; // bab3041e8977e0dc3eeff63fe707b92bde1dd449d8efafb248c27c8264cc311a
        consensus.BIP66Height = 0; // 7aceee012833fa8952f8835d8b1b3ae233cd6ab08fdb27a771d2bd7bdc491894
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60; // 3.5 days
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 6048; // 75% of 8064
        consensus.nMinerConfirmationWindow = 8064; // nPowTargetTimespan / nPowTargetSpacing * 4
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE ; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // January 28, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // January 31st, 2018

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // January 28, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // January 31st, 2018

        // Cascoin: Hive: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // Dec 26, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // Dec 26, 2019

        // Cascoin: Hive 1.1: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE_1_1].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE_1_1].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;  // Sept 20, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE_1_1].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;    // Sept 20, 2020

        // Cascoin: MinotaurX+Hive1.2: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_MINOTAURX].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_MINOTAURX].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;  // 1200 UTC Sept 16, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_MINOTAURX].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;  // Start + 1 year

        // Cascoin: Rialto: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_RIALTO].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_RIALTO].nStartTime = 2000000000;               // Far future (2033)
        consensus.vDeployments[Consensus::DEPLOYMENT_RIALTO].nTimeout = 2000000000 + 31536000;      // Start + 1 year

        // Cascoin fields
        consensus.powForkTime = 0;                 // Time of PoW hash method change
        consensus.lastScryptBlock = -1;                // Height of last scrypt block
        consensus.powLimitSHA = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");   // Initial hash target at fork
        consensus.slowStartBlocks = 0;                   // Scale post-fork block reward up over this many blocks
        consensus.premineAmount = 0;                   // Premine amount (less than 1% of issued currency at fork time)
        std::vector<unsigned char> vch = ParseHex("76a9145d613503928485276a83a4dc6abf22b8ce7fb16488ac");
        consensus.premineOutputScript = CScript(vch.begin(), vch.end());	// Output script for premine block (CashierDaZEsyBQkuvv4c2uPZFx6m2XTgT)
        consensus.totalMoneySupplyHeight = 6215968;         // Height at which TMS is reached, do not issue rewards past this point

        // Cascoin: Hive: Consensus Fields
        consensus.minBeeCost = 10000;                       // Minimum cost of a bee, used when no more block rewards
        consensus.beeCostFactor = 2500;                     // Bee cost is block_reward/beeCostFactor
        consensus.beeCreationAddress = "HiveCascoinBurnAddressXXXXXXXXXXkTBcG";        // Properly formatted unspendable address for bee creation
        consensus.hiveCommunityAddress = "HF2sgFXX5NgDojHhTPCTFknc6bqh3xCd8u";      // Community fund address
        consensus.communityContribFactor = 10;              // Optionally, donate bct_value/maxCommunityContribFactor to community fund
        consensus.beeGestationBlocks = 48*24;               // The number of blocks for a new bee to mature
        consensus.beeLifespanBlocks = 48*24*14;             // The number of blocks a bee lives for after maturation
        consensus.powLimitHive = uint256S("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");  // Target for ~2^20 work, good for CPU bootstrap
        consensus.minHiveCheckBlock = 0;              // Allow checking for Hive blocks from the start (was 1537566)
        consensus.hiveTargetAdjustAggression = 30;          // Snap speed for bee hash target adjustment EMA
        consensus.hiveBlockSpacingTarget = 2;               // Target Hive block frequency (1 out of this many blocks should be Hivemined)
        consensus.hiveBlockSpacingTargetTypical = 3;        // Observed Hive block frequency (1 out of this many blocks are observed to be Hive)
        consensus.hiveBlockSpacingTargetTypical_1_1 = 2;    // Observed Hive block frequency in Hive 1.1 (1 out of this many blocks are observed to be Hive)
        consensus.hiveNonceMarker = 192;                    // Nonce marker for hivemined blocks

        // Cascoin: Hive 1.1-related consensus fields
        consensus.minK = 2;                                 // Minimum chainwork scale for Hive blocks (see Hive whitepaper section 5)
        consensus.maxK = 16;                                // Maximum chainwork scale for Hive blocks (see Hive whitepaper section 5)
        consensus.maxHiveDiff = 0.006;                      // Hive difficulty at which max chainwork bonus is awarded
        consensus.maxKPow = 5;                              // Maximum chainwork scale for PoW blocks
        consensus.powSplit1 = 0.005;                        // Below this Hive difficulty threshold, PoW block chainwork bonus is halved
        consensus.powSplit2 = 0.0025;                       // Below this Hive difficulty threshold, PoW block chainwork bonus is halved again
        consensus.maxConsecutiveHiveBlocks = 2;             // Maximum hive blocks that can occur consecutively before a PoW block is required
        consensus.hiveDifficultyWindow = 36;                // How many blocks the SMA averages over in hive difficulty adjust

        // Cascoin: MinotaurX+Hive1.2-related consensus fields
        consensus.lwmaAveragingWindow = 90;                 // Averaging window size for LWMA diff adjust
        consensus.powTypeLimits.emplace_back(uint256S("0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));   // sha256d limit
        consensus.powTypeLimits.emplace_back(uint256S("0x000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));   // MinotaurX limit

        // Cascoin: Rialto-related consensus fields
        // Note: These are *NOT* finalised.
        consensus.nickCreationAddress       = "HF2sgFXX5NgDojHhTPCTFknc6bqh3xCd8u";     // Nick creation address
        consensus.nickCreationCost3Char     = 1000000000000;                            // Minimum costs to register a nick
        consensus.nickCreationCost4Char     = 100000000000;
        consensus.nickCreationCostStandard  = 1000000000;
        consensus.nickCreationAntiDust      = 10000;                                    // Portion of creation cost burnt in 2nd output

        // Cascoin: CVM (Cascoin Virtual Machine) related consensus fields
        consensus.cvmActivationHeight       = 220000;                                   // Activate CVM at block 220000 (2+ months for network upgrade)
        consensus.cvmMaxGasPerBlock         = 10000000;                                 // 10M gas per block
        consensus.cvmMaxGasPerTx            = 1000000;                                  // 1M gas per transaction
        consensus.cvmMaxCodeSize            = 24576;                                    // 24KB max contract size
        
        // Cascoin: Anti-Scam Reputation System (ASRS) related consensus fields
        consensus.asrsActivationHeight      = 220000;                                   // Activate ASRS at block 220000 (2+ months for network upgrade)
        consensus.asrsMinVotingPower        = 1;                                        // Minimum voting power to participate
        consensus.asrsMaxScoreChange        = 1000;                                     // Max score change per vote

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");  // Cascoin: 1695238

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00"); // Cascoin: 1695238

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xca;
        pchMessageStart[1] = 0x5c;
        pchMessageStart[2] = 0x01;
        pchMessageStart[3] = 0xcf;
        nDefaultPort = 22222;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1744615453, 517471, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000928be1f2ccc448590307e4f6e165702244b5be0f79c08e48d1fc7128c82"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a31ddc148dbb95ec904c376f0d84e9c23ebcf12a91f6100d3a43df055bb5460"));

        // Note that of those with the service bits flag, most only support a subset of possible options
        vSeeds.emplace_back("seed.cascoin.net");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 40);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 8);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 50);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 188);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "cas";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
                {0, uint256S("0x00000928be1f2ccc448590307e4f6e165702244b5be0f79c08e48d1fc7128c82")}
            }
        };

        chainTxData = ChainTxData{
            // Data as of block 0000000000000012e28998c604bbfc58bc0ae30523bca8a2f41320f4db2655d1 (height 2511842).
            1744615453, // * UNIX timestamp of last known number of transactions
            0,   // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.0      // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 840000;
        consensus.BIP16Height = 0; // always enforce BIP16
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x00000928be1f2ccc448590307e4f6e165702244b5be0f79c08e48d1fc7128c82"); // Block hash at block 48
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60; // 3.5 days
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 15; // Require 75% of last 20 blocks to activate rulechanges
        consensus.nMinerConfirmationWindow = 20;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;                  // Feb 13, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;         // Start + 1 year

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;               // Feb 13, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;      // Start + 1 year

        // Cascoin: Hive: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;                 // Feb 13, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;        // Start + 1 year

        // Cascoin: Hive 1.1: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE_1_1].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE_1_1].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;             // Feb 13, 2024 (but later than the above 3)
        consensus.vDeployments[Consensus::DEPLOYMENT_HIVE_1_1].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;    // Start + 1 year

        // Cascoin: MinotaurX+Hive1.2: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_MINOTAURX].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_MINOTAURX].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;            // Feb 13, 2024 (but later than the above 4!)
        consensus.vDeployments[Consensus::DEPLOYMENT_MINOTAURX].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;   // Start + 1 year

        // Cascoin: Rialto: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_RIALTO].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_RIALTO].nStartTime = 2000000000;               // Feb 14, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_RIALTO].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;      // Start + 1 year

        // Cascoin fields
        consensus.powForkTime = 0;                 // Time of PoW hash method change (block 50)
        consensus.lastScryptBlock = 0;                     // Height of last scrypt block
        consensus.powLimitSHA = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");   // Initial hash target at fork
        consensus.slowStartBlocks = 0;                   // Scale post-fork block reward up over this many blocks
        consensus.premineAmount = 0;                   // Premine amount (less than 1% of issued currency at fork time)
//        std::vector<unsigned char> vch = ParseHex("HF2sgFXX5NgDojHhTPCTFknc6bqh3xCd8u");
//        consensus.premineOutputScript = CScript(vch.begin(), vch.end());	// Output script for premine block (CashierDaZEsyBQkuvv4c2uPZFx6m2XTgT)
        consensus.premineOutputScript = CScript();
        consensus.totalMoneySupplyHeight = 6215968;         // Height at which TMS is reached, do not issue rewards past this point (Note, not accurate value for testnet)

        // Cascoin: Hive: Consensus Fields
        consensus.minBeeCost = 10000;                       // Minimum cost of a bee, used when no more block rewards
        consensus.beeCostFactor = 2500;                     // Bee cost is block_reward/beeCostFactor
        consensus.beeCreationAddress = "tEstNetCreateCASWorkerBeeXXXYq6T3r";        // Unspendable address for bee creation
        consensus.hiveCommunityAddress = "tCY5JWV4LYe64ivrAE2rD6P3bYxYtcoTsz";      // Community fund address
        consensus.communityContribFactor = 10;              // Optionally, donate bct_value/maxCommunityContribFactor to community fund
        consensus.beeGestationBlocks = 40;                  // The number of blocks for a new bee to mature
        consensus.beeLifespanBlocks = 48*24*14;             // The number of blocks a bee lives for after maturation
        consensus.powLimitHive = uint256S("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");  // Target for ~2^20 work, good for CPU bootstrap
        consensus.minHiveCheckBlock = 50;                   // Don't bother checking below this height for Hive blocks (not used for consensus/validation checks, just efficiency when looking for potential BCTs)
        consensus.hiveTargetAdjustAggression = 30;          // Snap speed for bee hash target adjustment EMA
        consensus.hiveBlockSpacingTarget = 2;               // Target Hive block frequency (1 out of this many blocks should be Hivemined)
        consensus.hiveBlockSpacingTargetTypical = 3;        // Observed Hive block frequency (1 out of this many blocks are observed to be Hive)
        consensus.hiveBlockSpacingTargetTypical_1_1 = 2;    // Observed Hive block frequency in Hive 1.1 (1 out of this many blocks are observed to be Hive)
        consensus.hiveNonceMarker = 192;                    // Nonce marker for hivemined blocks

        // Cascoin: Hive 1.1-related consensus fields
        consensus.minK = 2;                                 // Minimum chainwork scale for Hive blocks (see Hive whitepaper section 5)
        consensus.maxK = 10;                                // Maximum chainwork scale for Hive blocks (see Hive whitepaper section 5)
        consensus.maxHiveDiff = 0.002;                      // Hive difficulty at which max chainwork bonus is awarded
        consensus.maxKPow = 5;                              // Maximum chainwork scale for PoW blocks
        consensus.powSplit1 = 0.001;                        // Below this Hive difficulty threshold, PoW block chainwork bonus is halved
        consensus.powSplit2 = 0.0005;                       // Below this Hive difficulty threshold, PoW block chainwork bonus is halved again
        consensus.maxConsecutiveHiveBlocks = 2;             // Maximum hive blocks that can occur consecutively before a PoW block is required
        consensus.hiveDifficultyWindow = 36;                // How many blocks the SMA averages over in hive difficulty adjust

        // Cascoin: MinotaurX+Hive1.2-related consensus fields
        consensus.lwmaAveragingWindow = 90;                 // Averaging window size for LWMA diff adjust
        consensus.powTypeLimits.emplace_back(uint256S("0x000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));   // sha256d limit
        consensus.powTypeLimits.emplace_back(uint256S("0x000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));   // MinotaurX limit

        // Cascoin: Rialto-related consensus fields
        consensus.nickCreationAddress = "tKJjaPcSS3nXYBN4QmmYnSanr9oUhSXAZB";        // Nick creation address
        consensus.nickCreationCost3Char     = 100000000000; // Minimum costs to register a nick
        consensus.nickCreationCost4Char     = 5000000000;
        consensus.nickCreationCostStandard  = 100000000;
        consensus.nickCreationAntiDust      = 10000;        // Portion of creation cost burnt in 2nd output
        //consensus.firstRialtoBlock = uint256S("0xb602f3f5093626bea32c5b9cf499de562e7a364dbe55ee3d34211e7f15502a7e");   // Block 500: First block to consider for Rialto registrations (only required if launching without a UASF)

        // Cascoin: CVM (Cascoin Virtual Machine) related consensus fields
        consensus.cvmActivationHeight       = 500;                                      // Activate CVM at block 500 (earlier for testing)
        consensus.cvmMaxGasPerBlock         = 10000000;                                 // 10M gas per block
        consensus.cvmMaxGasPerTx            = 1000000;                                  // 1M gas per transaction
        consensus.cvmMaxCodeSize            = 24576;                                    // 24KB max contract size

        // Cascoin: Anti-Scam Reputation System (ASRS) related consensus fields
        consensus.asrsActivationHeight      = 500;                                      // Activate ASRS at block 500 (earlier for testing)
        consensus.asrsMinVotingPower        = 1;                                        // Minimum voting power to participate
        consensus.asrsMaxScoreChange        = 1000;                                     // Max score change per vote

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");  // Block 412

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00"); // Block 412

        // Testnet uses different magic bytes to prevent connecting to mainnet
        pchMessageStart[0] = 0xfc;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0xb7;
        pchMessageStart[3] = 0xdc;
        nDefaultPort = 22223;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1744615453, 517471, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000928be1f2ccc448590307e4f6e165702244b5be0f79c08e48d1fc7128c82"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a31ddc148dbb95ec904c376f0d84e9c23ebcf12a91f6100d3a43df055bb5460"));

        vFixedSeeds.clear();
        vSeeds.emplace_back("testseed.cascoin.net");

        // Testnet uses different address prefixes (111 = 't' prefix like Bitcoin testnet)
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 58);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tcas";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        checkpointData = (CCheckpointData) {
            {
                {0, uint256S("0x00000d1ccd9bc4897ac8a60f4ff9c03b4683c3d3055f9a95cc8857075e34adc4")}
            }
        };

        chainTxData = ChainTxData{  // As at block 412
            1707835909,
            415,
            0.001
        };
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.BIP34Height = 0; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 0; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Cascoin fields
        consensus.powForkTime = 1543765622;                 // Time of PoW hash method change (block 100)
        consensus.lastScryptBlock = 200;                    // Height of last scrypt block
        consensus.powLimitSHA = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");   // Initial hash target at fork
        consensus.slowStartBlocks = 0;                   // Scale post-fork block reward up over this many blocks
        consensus.premineAmount = 0;                   // Premine amount (less than 1% of issued currency at fork time)
//        std::vector<unsigned char> vch = ParseHex("HF2sgFXX5NgDojHhTPCTFknc6bqh3xCd8u");
//        consensus.premineOutputScript = CScript(vch.begin(), vch.end());	// Output script for premine block (CashierDaZEsyBQkuvv4c2uPZFx6m2XTgT)
        consensus.premineOutputScript = CScript();
        consensus.totalMoneySupplyHeight = 6215968;         // Height at which TMS is reached, do not issue rewards past this point (Note, not accurate value for testnet)
        consensus.hiveNonceMarker = 192;                    // Nonce marker for hivemined blocks
        // Cascoin: Add powLimitHive for regtest, matching the other networks for consistency during bootstrap
        consensus.powLimitHive = uint256S("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // Target for ~2^20 work, good for CPU bootstrap

        // Cascoin: MinotaurX+Hive1.2-related consensus fields
        consensus.lwmaAveragingWindow = 90;                 // Averaging window size for LWMA diff adjust
        consensus.powTypeLimits.emplace_back(uint256S("0x000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));   // sha256d limit
        consensus.powTypeLimits.emplace_back(uint256S("0x000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));   // MinotaurX limit

        // Cascoin: Hive: Consensus Fields (regtest specific defaults)
        consensus.minBeeCost = 10000;
        consensus.beeCostFactor = 2500;
        consensus.beeGestationBlocks = 48*24;               // reasonable default
        consensus.beeLifespanBlocks = 48*24*14;             // reasonable default
        consensus.hiveTargetAdjustAggression = 30;
        consensus.hiveBlockSpacingTarget = 2;
        consensus.hiveBlockSpacingTargetTypical = 3;
        consensus.hiveBlockSpacingTargetTypical_1_1 = 2;

        // Cascoin: Rialto-related consensus fields
        consensus.nickCreationAddress = "tKJjaPcSS3nXYBN4QmmYnSanr9oUhSXAZB";        // Nick creation address (regtest uses same as testnet)
        consensus.nickCreationCost3Char     = 100000000000; // Minimum costs to register a nick
        consensus.nickCreationCost4Char     = 5000000000;
        consensus.nickCreationCostStandard  = 100000000;
        consensus.nickCreationAntiDust      = 10000;        // Portion of creation cost burnt in 2nd output

        // Cascoin: CVM (Cascoin Virtual Machine) related consensus fields
        consensus.cvmActivationHeight       = 0;                                        // Activate CVM immediately for regtest
        consensus.cvmMaxGasPerBlock         = 10000000;                                 // 10M gas per block
        consensus.cvmMaxGasPerTx            = 1000000;                                  // 1M gas per transaction
        consensus.cvmMaxCodeSize            = 24576;                                    // 24KB max contract size

        // Cascoin: Anti-Scam Reputation System (ASRS) related consensus fields
        consensus.asrsActivationHeight      = 0;                                        // Activate ASRS immediately for regtest
        consensus.asrsMinVotingPower        = 1;                                        // Minimum voting power to participate
        consensus.asrsMaxScoreChange        = 1000;                                     // Max score change per vote

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // Regtest uses different magic bytes to prevent connecting to mainnet/testnet
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 22224;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1744615453, 517471, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000928be1f2ccc448590307e4f6e165702244b5be0f79c08e48d1fc7128c82"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a31ddc148dbb95ec904c376f0d84e9c23ebcf12a91f6100d3a43df055bb5460"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = {
            {
                {0, uint256S("0x6df96ca93ab1065decd9a64ecaae1a9e55e927cae15170fe72dc801b037ca0a3")}
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Regtest uses different address prefixes (111 = 't' prefix)
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 58);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "rcas";
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    // Be robust against early thread startup: wait until SelectParams() ran
    // instead of throwing across threads which can terminate the process.
    while (!globalChainParams) {
        MilliSleep(1); // avoid busy spin; utiltime.h provides MilliSleep
    }
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
