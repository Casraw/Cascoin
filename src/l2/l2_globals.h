// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_GLOBALS_H
#define CASCOIN_L2_GLOBALS_H

/**
 * @file l2_globals.h
 * @brief Shared, process-wide L2 runtime singletons and the block-driven
 *        burn-and-mint processor.
 *
 * The RPC layer and the block-connection code must operate on the SAME
 * L2StateManager / BurnRegistry / L2TokenMinter instances, otherwise a mint
 * performed while connecting a block would be invisible to l2_getbalance /
 * l2_gettotalsupply (which historically each created their own file-static
 * singletons). These accessors centralize that state.
 *
 * ProcessConnectedBlockForBurns() implements the burn-and-mint pipeline:
 * it detects OP_RETURN "L2BURN" outputs, waits for REQUIRED_CONFIRMATIONS L1
 * confirmations, and then deterministically mints 1:1 L2 tokens to the burn
 * recipient. Because a confirmed L1 burn is an objective on-chain fact, the
 * mint is fully deterministic and does not require multi-sequencer voting;
 * this makes single-node / regtest operation work correctly.
 */

class CBlock;

namespace l2 {

class L2StateManager;
class BurnRegistry;
class L2TokenMinter;

/** Get the process-wide L2 state manager (balances / nonces). */
L2StateManager& GetGlobalStateManager();

/** Get the process-wide burn registry (processed burns / total burned). */
BurnRegistry& GetGlobalBurnRegistry();

/** Get the process-wide token minter, backed by the two singletons above. */
L2TokenMinter& GetGlobalMinter();

/**
 * @brief Process a freshly connected L1 block for burn-and-mint.
 *
 * Detects burn transactions in @p block and mints L2 tokens for any tracked
 * burn that has reached REQUIRED_CONFIRMATIONS confirmations relative to
 * @p chainHeight. Safe to call on every connected block; minting is idempotent
 * (guarded by the burn registry). Never throws.
 *
 * @param block        The block that was just connected.
 * @param height       The height of @p block.
 * @param chainHeight  The current chain tip height (== height on connect).
 */
void ProcessConnectedBlockForBurns(const CBlock& block, int height, int chainHeight);

/**
 * @brief Drop tracked (not-yet-minted) burns at or above @p height on reorg.
 *
 * Already-minted burns are intentionally left in place; full unwinding of
 * minted balances on deep reorgs is out of scope for this processor.
 */
void HandleBurnReorg(int height);

} // namespace l2

#endif // CASCOIN_L2_GLOBALS_H
