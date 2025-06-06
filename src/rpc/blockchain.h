// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_BLOCKCHAIN_H
#define BITCOIN_RPC_BLOCKCHAIN_H

class CBlock;
class CBlockIndex;
class UniValue;

/**
 * Get the difficulty of the net wrt to the given block index, or the chain tip if
 * not provided.
 *
 * @return A floating point number that is a multiple of the main net minimum
 * difficulty (4295032833 hashes).
 */
// Cascoin: Hive: If optional argument getHiveDifficulty is true, will return Hive difficulty as close to blockindex or tip as possible.
// If getHiveDifficulty is false, will return PoW difficulty as close to blockindex or tip as possible.
// Cascoin: MinotaurX+Hive1.2: Add additional POW_TYPE arg
double GetDifficulty(const CBlockIndex* blockindex = nullptr, bool getHiveDifficulty = false, POW_TYPE powType = POW_TYPE_SHA256);

/** Callback for when block tip changed. */
void RPCNotifyBlockChange(bool ibd, const CBlockIndex *);

/** Block description to JSON */
UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);

/** Mempool information to JSON */
UniValue mempoolInfoToJSON();

/** Mempool to JSON */
UniValue mempoolToJSON(bool fVerbose = false);

/** Block header to JSON */
UniValue blockheaderToJSON(const CBlockIndex* blockindex);

#endif

