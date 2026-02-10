// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <l2/l2_config.h>
#include <l2/l2_chainparams.h>
#include <util.h>
#include <chainparamsbase.h>
#include <utilmoneystr.h>

namespace l2 {

std::string GetL2HelpMessage()
{
    std::string strUsage;
    
    strUsage += HelpMessageGroup(_("Layer 2 options:"));
    strUsage += HelpMessageOpt("-l2", strprintf(_("Enable L2 functionality (default: %u)"), DEFAULT_L2_ENABLED));
    strUsage += HelpMessageOpt("-nol2", _("Disable L2 functionality (equivalent to -l2=0)"));
    strUsage += HelpMessageOpt("-l2mode=<mode>", strprintf(_("L2 node mode: 0=disabled, 1=light, 2=full (default: %d)"), DEFAULT_L2_MODE));
    strUsage += HelpMessageOpt("-l2chainid=<n>", strprintf(_("L2 chain ID to connect to (default: %u)"), DEFAULT_L2_CHAIN_ID_VALUE));
    
    return strUsage;
}

bool InitL2Config()
{
    // Check for -nol2 flag first (takes precedence)
    if (gArgs.GetBoolArg("-nol2", false)) {
        SetL2Enabled(false);
        SetL2NodeMode(L2NodeMode::DISABLED);
        LogPrintf("L2: Disabled via -nol2 flag\n");
        return true;
    }
    
    // Check -l2 flag
    bool l2Enabled = gArgs.GetBoolArg("-l2", DEFAULT_L2_ENABLED);
    SetL2Enabled(l2Enabled);
    
    if (!l2Enabled) {
        SetL2NodeMode(L2NodeMode::DISABLED);
        LogPrintf("L2: Disabled via -l2=0\n");
        return true;
    }
    
    // Parse L2 mode
    int l2ModeInt = gArgs.GetArg("-l2mode", DEFAULT_L2_MODE);
    L2NodeMode l2Mode;
    
    switch (l2ModeInt) {
        case 0:
            l2Mode = L2NodeMode::DISABLED;
            SetL2Enabled(false);
            break;
        case 1:
            l2Mode = L2NodeMode::LIGHT_CLIENT;
            break;
        case 2:
            l2Mode = L2NodeMode::FULL_NODE;
            break;
        default:
            LogPrintf("L2: Invalid -l2mode value %d, using default (full node)\n", l2ModeInt);
            l2Mode = L2NodeMode::FULL_NODE;
            break;
    }
    SetL2NodeMode(l2Mode);
    
    // Parse L2 chain ID
    uint64_t l2ChainId = gArgs.GetArg("-l2chainid", DEFAULT_L2_CHAIN_ID_VALUE);
    if (l2ChainId == 0) {
        LogPrintf("L2: Invalid -l2chainid value 0, using default\n");
        l2ChainId = DEFAULT_L2_CHAIN_ID_VALUE;
    }
    SetL2ChainId(l2ChainId);
    
    // Select L2 params based on network
    SelectL2Params(ChainNameFromCommandLine());
    
    // Log configuration
    const char* modeStr = "unknown";
    switch (l2Mode) {
        case L2NodeMode::DISABLED:    modeStr = "disabled"; break;
        case L2NodeMode::LIGHT_CLIENT: modeStr = "light client"; break;
        case L2NodeMode::FULL_NODE:   modeStr = "full node"; break;
    }
    
    LogPrintf("L2: Initialized - enabled=%d, mode=%s, chainid=%lu\n", 
              l2Enabled, modeStr, l2ChainId);
    
    return true;
}

bool StartL2()
{
    if (!IsL2Enabled()) {
        LogPrintf("L2: Not starting (disabled)\n");
        return true;
    }
    
    LogPrintf("L2: Starting subsystem...\n");
    
    // TODO: Initialize L2 components based on mode
    // - State manager
    // - Sequencer discovery
    // - Bridge contract interface
    // - etc.
    
    const L2Params& params = GetL2Params();
    LogPrintf("L2: Using parameters - minSequencerStake=%s, challengePeriod=%lu seconds\n",
              FormatMoney(params.nMinSequencerStake), params.nStandardChallengePeriod);
    
    LogPrintf("L2: Subsystem started successfully\n");
    return true;
}

void StopL2()
{
    if (!IsL2Enabled()) {
        return;
    }
    
    LogPrintf("L2: Stopping subsystem...\n");
    
    // TODO: Cleanup L2 components
    // - Flush state
    // - Close connections
    // - etc.
    
    LogPrintf("L2: Subsystem stopped\n");
}

void InterruptL2()
{
    if (!IsL2Enabled()) {
        return;
    }
    
    LogPrintf("L2: Interrupting subsystem...\n");
    
    // TODO: Signal L2 threads to stop
}

} // namespace l2
