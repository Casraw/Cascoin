// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/activation.h>
#include <chain.h>
#include <consensus/params.h>
#include <versionbits.h>

// Global cache for version bits state
extern VersionBitsCache versionbitscache;

bool IsCVMEVMEnabled(const CBlockIndex* pindex, const Consensus::Params& params)
{
    // CVM-EVM is enabled if:
    // 1. We're past the CVM activation height (base CVM must be active)
    // 2. The CVM-EVM soft-fork deployment is active via BIP9
    
    if (!pindex) {
        return false;
    }
    
    // Check if we're past CVM activation height
    if (pindex->nHeight < params.cvmActivationHeight) {
        return false;
    }
    
    // Check if CVM-EVM deployment is active via version bits
    ThresholdState state = VersionBitsState(pindex->pprev, params, Consensus::DEPLOYMENT_CVM_EVM, versionbitscache);
    return state == THRESHOLD_ACTIVE;
}

bool IsCVMEVMEnabled(int nHeight, const Consensus::Params& params)
{
    // For height-based checks without a block index, we check:
    // 1. Height is past CVM activation
    // 2. Deployment start time indicates it should be active
    
    if (nHeight < params.cvmActivationHeight) {
        return false;
    }
    
    // If deployment is set to ALWAYS_ACTIVE, it's enabled
    const Consensus::BIP9Deployment& deployment = params.vDeployments[Consensus::DEPLOYMENT_CVM_EVM];
    if (deployment.nStartTime == Consensus::BIP9Deployment::ALWAYS_ACTIVE) {
        return true;
    }
    
    // Otherwise, we can't determine without a block index
    // This is a conservative approach - return false if uncertain
    return false;
}
