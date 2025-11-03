// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cvmtx.h>
#include <cvm/cvm.h>
#include <cvm/cvmdb.h>
#include <cvm/contract.h>
#include <cvm/reputation.h>
#include <validation.h>
#include <primitives/block.h>
#include <chain.h>
#include <util.h>

namespace CVM {

bool InitCVM(const std::string& datadir) {
    LogPrintf("Initializing CVM...\n");
    
    fs::path cvmPath = fs::path(datadir) / "cvm";
    
    if (!InitCVMDatabase(cvmPath)) {
        LogPrintf("ERROR: Failed to initialize CVM database\n");
        return false;
    }
    
    LogPrintf("CVM initialized successfully\n");
    return true;
}

void ShutdownCVM() {
    LogPrintf("Shutting down CVM...\n");
    ShutdownCVMDatabase();
    LogPrintf("CVM shutdown complete\n");
}

bool IsCVMActive(int height, const Consensus::Params& params) {
    return height >= params.cvmActivationHeight;
}

bool IsASRSActive(int height, const Consensus::Params& params) {
    return height >= params.asrsActivationHeight;
}

bool CheckCVMTransaction(const CTransaction& tx, CValidationState& state,
                         int height, const Consensus::Params& params) {
    // If CVM not active yet, skip checks
    if (!IsCVMActive(height, params) && !IsASRSActive(height, params)) {
        return true;
    }
    
    // Check if this is a CVM transaction
    if (IsContractTransaction(tx)) {
        if (!IsCVMActive(height, params)) {
            return state.DoS(10, false, REJECT_INVALID, "cvm-not-active");
        }
        
        ContractTxType txType = GetContractTxType(tx);
        
        if (txType == ContractTxType::DEPLOY) {
            ContractDeployTx deployTx;
            if (!ParseContractDeployTx(tx, deployTx)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-cvm-deploy");
            }
            
            // Validate bytecode
            std::string error;
            if (!ValidateContractCode(deployTx.code, error)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-contract-code", false, error);
            }
            
            // Check gas limit
            if (deployTx.gasLimit > params.cvmMaxGasPerTx) {
                return state.DoS(10, false, REJECT_INVALID, "excessive-gas-limit");
            }
            
            // Check code size
            if (deployTx.code.size() > params.cvmMaxCodeSize) {
                return state.DoS(100, false, REJECT_INVALID, "contract-too-large");
            }
        } else if (txType == ContractTxType::CALL) {
            ContractCallTx callTx;
            if (!ParseContractCallTx(tx, callTx)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-cvm-call");
            }
            
            // Check gas limit
            if (callTx.gasLimit > params.cvmMaxGasPerTx) {
                return state.DoS(10, false, REJECT_INVALID, "excessive-gas-limit");
            }
        }
    }
    
    // Check if this is a reputation vote transaction
    if (IsReputationVoteTransaction(tx)) {
        if (!IsASRSActive(height, params)) {
            return state.DoS(10, false, REJECT_INVALID, "asrs-not-active");
        }
        
        ReputationVoteTx voteTx;
        if (!ParseReputationVoteTx(tx, voteTx)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-reputation-vote");
        }
        
        // Validate vote
        std::string error;
        if (!voteTx.IsValid(error)) {
            return state.DoS(10, false, REJECT_INVALID, "invalid-reputation-vote", false, error);
        }
    }
    
    return true;
}

bool ExecuteCVMBlock(const CBlock& block, const CBlockIndex* pindex,
                     CCoinsViewCache& view, const Consensus::Params& params) {
    if (!g_cvmdb) {
        LogPrintf("WARNING: CVM database not initialized\n");
        return true; // Don't fail validation if CVM not initialized
    }
    
    if (!IsCVMActive(pindex->nHeight, params)) {
        return true; // CVM not active yet
    }
    
    // Track total gas used in block
    uint64_t totalGasUsed = 0;
    
    // Process each transaction
    for (const auto& tx : block.vtx) {
        if (IsContractTransaction(*tx)) {
            ContractTxType txType = GetContractTxType(*tx);
            
            if (txType == ContractTxType::DEPLOY) {
                ContractDeployTx deployTx;
                if (ParseContractDeployTx(*tx, deployTx)) {
                    // Execute deployment
                    // Note: Full implementation would execute constructor
                    totalGasUsed += deployTx.gasLimit;
                    
                    // Generate contract address
                    // Simplified: use tx hash as address
                    uint160 contractAddr(tx->GetHash());
                    
                    // Store contract
                    Contract contract;
                    contract.address = contractAddr;
                    contract.code = deployTx.code;
                    contract.deploymentHeight = pindex->nHeight;
                    contract.deploymentTx = tx->GetHash();
                    
                    g_cvmdb->WriteContract(contractAddr, contract);
                    
                    LogPrint(BCLog::ALL, "CVM: Deployed contract at %s\n", contractAddr.ToString());
                }
            } else if (txType == ContractTxType::CALL) {
                ContractCallTx callTx;
                if (ParseContractCallTx(*tx, callTx)) {
                    // Execute contract call
                    // Note: Full implementation would execute contract code
                    totalGasUsed += callTx.gasLimit;
                    
                    LogPrint(BCLog::ALL, "CVM: Called contract at %s\n", callTx.contractAddress.ToString());
                }
            }
        }
    }
    
    // Check total gas doesn't exceed block limit
    if (totalGasUsed > params.cvmMaxGasPerBlock) {
        LogPrintf("ERROR: Block exceeds gas limit: %llu > %llu\n", 
                 totalGasUsed, params.cvmMaxGasPerBlock);
        return false;
    }
    
    return true;
}

void UpdateReputationScores(const CBlock& block, const CBlockIndex* pindex,
                            const Consensus::Params& params) {
    if (!g_cvmdb) {
        return;
    }
    
    if (!IsASRSActive(pindex->nHeight, params)) {
        return;
    }
    
    ReputationSystem repSystem(*g_cvmdb);
    
    // Process reputation votes
    for (const auto& tx : block.vtx) {
        if (IsReputationVoteTransaction(*tx)) {
            ReputationVoteTx voteTx;
            if (ParseReputationVoteTx(*tx, voteTx)) {
                // Get voter address (from first input)
                uint160 voterAddr; // Simplified: would extract from tx inputs
                
                // Apply vote
                repSystem.ApplyVote(voterAddr, voteTx, pindex->nTime);
                
                LogPrint(BCLog::ALL, "ASRS: Applied reputation vote for %s\n", 
                        voteTx.targetAddress.ToString());
            }
        }
        
        // Update behavior scores for all transaction participants
        // Simplified: would extract addresses from inputs/outputs
    }
}

bool IsCVMOrReputationTx(const CTransaction& tx) {
    return IsContractTransaction(tx) || IsReputationVoteTransaction(tx);
}

} // namespace CVM

