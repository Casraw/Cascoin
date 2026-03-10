// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_CONFIG_H
#define CASCOIN_L2_CONFIG_H

/**
 * @file l2_config.h
 * @brief L2 configuration and initialization
 * 
 * This file provides functions for configuring and initializing
 * the L2 subsystem based on command-line arguments and config file.
 */

#include <l2/l2_common.h>
#include <string>

namespace l2 {

// Default values for L2 configuration
static const bool DEFAULT_L2_ENABLED = true;
static const int DEFAULT_L2_MODE = static_cast<int>(L2NodeMode::FULL_NODE);
static const uint64_t DEFAULT_L2_CHAIN_ID_VALUE = 1;

/**
 * Get L2 help message for command-line options
 * @return Help message string
 */
std::string GetL2HelpMessage();

/**
 * Initialize L2 configuration from command-line arguments
 * Should be called during AppInitParameterInteraction
 * @return true if initialization successful
 */
bool InitL2Config();

/**
 * Start L2 subsystem
 * Should be called during AppInitMain after basic initialization
 * @return true if startup successful
 */
bool StartL2();

/**
 * Stop L2 subsystem
 * Should be called during Shutdown
 */
void StopL2();

/**
 * Interrupt L2 subsystem
 * Should be called during Interrupt
 */
void InterruptL2();

// Internal functions for setting L2 state (declared in l2_common.cpp)
void SetL2Enabled(bool enabled);
void SetL2NodeMode(L2NodeMode mode);
void SetL2ChainId(uint64_t chainId);

} // namespace l2

#endif // CASCOIN_L2_CONFIG_H
