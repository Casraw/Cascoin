// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reward_types.h>
#include <hash.h>
#include <version.h>

namespace CVM {

uint256 PendingReward::GenerateRewardId(const uint256& disputeId, 
                                        const uint160& recipient, 
                                        RewardType type) {
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << disputeId;
    ss << recipient;
    ss << static_cast<uint8_t>(type);
    return ss.GetHash();
}

} // namespace CVM
