// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_SECURITY_RPC_H
#define CASCOIN_CVM_SECURITY_RPC_H

#include <rpc/server.h>

/**
 * Register security monitoring RPC commands
 */
void RegisterSecurityRPCCommands(CRPCTable &t);

#endif // CASCOIN_CVM_SECURITY_RPC_H
