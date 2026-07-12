// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cvmtx.h>
#include <cvm/cvm.h>
#include <cvm/cvmdb.h>
#include <cvm/contract.h>
#include <cvm/reputation.h>
#include <cvm/enhanced_vm.h>
#include <cvm/trust_context.h>
#include <cvm/softfork.h>
#include <validation.h>
#include <primitives/block.h>
#include <chain.h>
#include <hash.h>
#include <util.h>
#include <consensus/validation.h>

namespace CVM {

namespace {

/**
 * Resolve the deployer/caller address for a contract transaction.
 *
 * Prefers the real address extracted from the spent UTXO (ExtractDeployerAddress),
 * which is the authoritative source when the UTXO set is available. When the
 * spending output cannot be resolved (e.g. during isolated block processing or
 * unit tests where the UTXO set is not populated) it falls back to the same
 * deterministic derivation used by the primary CVMBlockProcessor path in
 * blockprocessor.cpp — Hash(SER_GETHASH, first-input prevout)[0:20] — so the two
 * execution paths agree on the deployer (and therefore on the derived contract
 * address). The fallback never yields the null address for a transaction that
 * has inputs.
 */
uint160 ResolveDeployerAddress(const CTransaction& tx)
{
    uint160 addr;
    if (ExtractDeployerAddress(tx, addr) && !addr.IsNull()) {
        return addr;
    }

    addr.SetNull();
    if (!tx.vin.empty()) {
        CHashWriter hw(SER_GETHASH, 0);
        hw << tx.vin[0].prevout;
        uint256 h = hw.GetHash();
        memcpy(addr.begin(), h.begin(), 20);
    }
    return addr;
}

} // anonymous namespace

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

uint64_t GetMaxSubsidyPerBlock(const Consensus::Params& params) {
    // At most 10% of the block gas capacity may be subsidized. This keeps the
    // subsidy budget reconciled with the block gas cap while still leaving a
    // meaningful allowance for genuinely beneficial operations.
    return params.cvmMaxGasPerBlock / 10;
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

    // Per-block subsidy accounting (bugfix 2.1 / Property 1).
    //
    // Every subsidy-eligible contract transaction contributes toward a per-block
    // subsidy budget. The budget is derived directly from the block gas cap so
    // the two limits stay reconciled: at most a fixed fraction of the block's
    // gas capacity may be subsidized. The block is rejected if the accumulated
    // subsidy exceeds this maximum. Accumulating the actual per-transaction
    // subsidy replaces the previous behaviour where no subsidy accounting
    // existed at all (the total was implicitly zero).
    const uint64_t maxSubsidyPerBlock = GetMaxSubsidyPerBlock(params);
    uint64_t totalSubsidy = 0;
    
    // Process each transaction
    for (const auto& tx : block.vtx) {
        if (IsContractTransaction(*tx)) {
            ContractTxType txType = GetContractTxType(*tx);
            
            if (txType == ContractTxType::DEPLOY) {
                ContractDeployTx deployTx;
                if (ParseContractDeployTx(*tx, deployTx)) {
                    // Derive the contract address from the deployer and nonce
                    // using the canonical GenerateContractAddress(deployer, nonce)
                    // scheme (bugfix 2.16), instead of the first 20 bytes of the
                    // transaction hash. This makes the block-processing address
                    // agree with the address every other CVM path derives for the
                    // same deployer and nonce, so a deployed contract can be found
                    // by later calls.
                    uint160 deployer = ResolveDeployerAddress(*tx);
                    uint64_t nonce = g_cvmdb->GetNextNonce(deployer);
                    uint160 contractAddr = GenerateContractAddress(deployer, nonce);

                    // Execute the constructor via the Enhanced VM and account for
                    // the ACTUAL gas used (bugfix 2.17), rather than merely
                    // accumulating the gas limit. The constructor runs against the
                    // canonical contract address so any initial state writes land
                    // in that contract's storage.
                    auto trustCtx = std::make_shared<TrustContext>(g_cvmdb.get());
                    EnhancedVM vm(g_cvmdb.get(), trustCtx);
                    EnhancedExecutionResult result = vm.Execute(
                        deployTx.code, deployTx.gasLimit, contractAddr, deployer,
                        /*call_value=*/0, deployTx.initData, pindex->nHeight,
                        block.GetHash(), pindex->nTime);
                    totalGasUsed += result.gas_used;

                    // The per-block subsidy budget stays keyed on the eligible
                    // (gas-limit) amount so the per-block subsidy maximum remains
                    // enforced (bugfix 2.1, task 5.1).
                    totalSubsidy += deployTx.gasLimit;

                    // Store the contract at the canonical address with the
                    // resolved deployer recorded.
                    Contract contract;
                    contract.address = contractAddr;
                    contract.deployer = deployer;
                    contract.code = deployTx.code;
                    contract.deploymentHeight = pindex->nHeight;
                    contract.deploymentTx = tx->GetHash();

                    g_cvmdb->WriteContract(contractAddr, contract);

                    LogPrint(BCLog::ALL, "CVM: Deployed contract at %s (deployer=%s, gasUsed=%llu)\n",
                             contractAddr.ToString(), deployer.ToString(),
                             (unsigned long long)result.gas_used);
                }
            } else if (txType == ContractTxType::CALL) {
                ContractCallTx callTx;
                if (ParseContractCallTx(*tx, callTx)) {
                    // Execute the target contract's code via the Enhanced VM and
                    // account for the actual gas used (bugfix 2.17) instead of only
                    // accumulating the gas limit.
                    uint160 caller = ResolveDeployerAddress(*tx);
                    auto trustCtx = std::make_shared<TrustContext>(g_cvmdb.get());
                    EnhancedVM vm(g_cvmdb.get(), trustCtx);
                    EnhancedExecutionResult result = vm.CallContract(
                        callTx.contractAddress, callTx.data, callTx.gasLimit, caller,
                        callTx.value, pindex->nHeight, block.GetHash(), pindex->nTime);
                    totalGasUsed += result.gas_used;

                    // Accumulate the subsidy this call is eligible for.
                    totalSubsidy += callTx.gasLimit;

                    LogPrint(BCLog::ALL, "CVM: Called contract at %s (caller=%s, gasUsed=%llu)\n",
                             callTx.contractAddress.ToString(), caller.ToString(),
                             (unsigned long long)result.gas_used);
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

    // Enforce the per-block subsidy maximum (bugfix 2.1). Reject any block whose
    // accumulated subsidy exceeds the budget, even when the raw gas total is
    // under the block gas cap.
    if (totalSubsidy > maxSubsidyPerBlock) {
        LogPrintf("ERROR: Block exceeds per-block subsidy maximum: %llu > %llu\n",
                 totalSubsidy, maxSubsidyPerBlock);
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
                // Bugfix 2.18: resolve the REAL voter address from the tx inputs
                // (spent UTXO via ExtractDeployerAddress, with the deterministic
                // first-input fallback used by the block-processing paths) before
                // applying the vote. ApplyVote itself rejects a null/zero voter,
                // so a vote whose voter cannot be resolved is never attributed to
                // the zero address.
                uint160 voterAddr = ResolveDeployerAddress(*tx);

                if (voterAddr.IsNull()) {
                    LogPrint(BCLog::ALL, "ASRS: skipping vote for %s — voter could not be resolved\n",
                            voteTx.targetAddress.ToString());
                } else if (repSystem.ApplyVote(voterAddr, voteTx, pindex->nTime)) {
                    // Update the voter's per-participant behavior score to reflect
                    // their participation in the reputation vote (bugfix 2.18).
                    repSystem.UpdateBehaviorScore(voterAddr, *tx, pindex->nHeight);

                    LogPrint(BCLog::ALL, "ASRS: Applied reputation vote from %s for %s\n",
                            voterAddr.ToString(), voteTx.targetAddress.ToString());
                }
            }
        }
    }
}

bool IsCVMOrReputationTx(const CTransaction& tx) {
    return IsContractTransaction(tx) || IsReputationVoteTransaction(tx);
}

} // namespace CVM

