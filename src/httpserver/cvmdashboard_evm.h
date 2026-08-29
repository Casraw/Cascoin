// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_CVMDASHBOARD_EVM_H
#define CASCOIN_HTTPSERVER_CVMDASHBOARD_EVM_H

#include <string>

namespace CVMDashboardEVM {

/**
 * EVM Contract Interaction Dashboard Extension
 * 
 * Provides web-based interface for:
 * - Contract deployment with ABI encoding
 * - Contract call interface with ABI decoding
 * - Gas estimation with reputation-based pricing
 * - Trust score tracking for contract interactions
 * 
 * Requirements: 1.4, 1.5, 6.1, 6.3, 18.2, 2.1, 14.1, 22.5
 */

// EVM Contract Interaction HTML Section
static const std::string EVM_CONTRACT_SECTION = R"HTML(
<!-- EVM Contract Interaction Section -->
<div class="card" id="evmContractSection">
    <div class="card-header">
        <h2>📜 EVM Smart Contracts
            <span class="tooltip info-icon">?
                <span class="tooltiptext">
                    <strong>EVM Smart Contract Interface</strong><br>
                    Deploy and interact with Ethereum-compatible smart contracts:<br>
                    • <strong>Deploy</strong>: Upload bytecode to create new contracts<br>
                    • <strong>Call</strong>: Execute contract functions<br>
                    • <strong>Read</strong>: Query contract state without gas<br>
                    • <strong>ABI</strong>: Automatic encoding/decoding of parameters<br><br>
                    All operations benefit from reputation-based gas discounts!
                </span>
            </span>
        </h2>
        <div style="display: flex; gap: 10px;">
            <button class="btn-secondary" onclick="toggleContractSection('deploy')">Deploy</button>
            <button class="btn-secondary" onclick="toggleContractSection('call')">Call</button>
            <button class="btn-secondary" onclick="toggleContractSection('read')">Read</button>
        </div>
    </div>
    <div class="card-body">
        <!-- Contract Deployment Form -->
        <div id="deploySection" class="contract-section">
            <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                🚀 Deploy Contract
            </h3>
            <div class="info-box">
                Deploy EVM-compatible smart contracts. Your reputation score affects gas costs.
                High reputation (80+) may qualify for free gas!
            </div>
            <form id="deployContractForm" onsubmit="return handleDeployContract(event);">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            From Address:
                        </label>
                        <input type="text" id="deployFromAddress" placeholder="Your CAS address..." required
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                    </div>
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Gas Limit:
                        </label>
                        <input type="number" id="deployGasLimit" value="3000000" min="21000" required
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                    </div>
                </div>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Contract Bytecode (hex):
                    </label>
                    <textarea id="deployBytecode" placeholder="0x608060405234801561001057600080fd5b50..." required
                              style="width: 100%; height: 120px; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem; resize: vertical;"></textarea>
                </div>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Contract ABI (JSON, optional):
                    </label>
                    <textarea id="deployABI" placeholder='[{"inputs":[],"stateMutability":"nonpayable","type":"constructor"}]'
                              style="width: 100%; height: 80px; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem; resize: vertical;"></textarea>
                </div>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Constructor Arguments (comma-separated, optional):
                    </label>
                    <input type="text" id="deployConstructorArgs" placeholder="arg1, arg2, ..."
                           style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                </div>
                <div style="display: flex; gap: 10px; align-items: center;">
                    <button type="button" onclick="estimateDeployGas()" 
                            style="padding: 10px 20px; background: var(--secondary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        📊 Estimate Gas
                    </button>
                    <button type="submit" 
                            style="padding: 10px 20px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        🚀 Deploy Contract
                    </button>
                    <span id="deployGasEstimate" style="color: var(--text-secondary); font-size: 0.9rem;"></span>
                </div>
            </form>
            <div id="deployResult" style="margin-top: 15px;"></div>
        </div>

        <!-- Contract Call Form -->
        <div id="callSection" class="contract-section" style="display: none;">
            <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                📞 Call Contract Function
            </h3>
            <div class="info-box">
                Execute contract functions that modify state. Gas fees apply based on your reputation.
            </div>
            <form id="callContractForm" onsubmit="return handleCallContract(event);">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            From Address:
                        </label>
                        <input type="text" id="callFromAddress" placeholder="Your CAS address..." required
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                    </div>
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Contract Address:
                        </label>
                        <input type="text" id="callContractAddress" placeholder="Contract address..." required
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                    </div>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Gas Limit:
                        </label>
                        <input type="number" id="callGasLimit" value="100000" min="21000" required
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                    </div>
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Value (CAS):
                        </label>
                        <input type="number" id="callValue" value="0" min="0" step="0.00000001"
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                    </div>
                </div>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Contract ABI (JSON):
                    </label>
                    <textarea id="callABI" placeholder='[{"inputs":[{"name":"_value","type":"uint256"}],"name":"setValue","outputs":[],"stateMutability":"nonpayable","type":"function"}]'
                              style="width: 100%; height: 80px; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem; resize: vertical;"></textarea>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Function Name:
                        </label>
                        <select id="callFunctionName" onchange="updateFunctionParams()"
                                style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            <option value="">-- Load ABI first --</option>
                        </select>
                    </div>
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Function Arguments:
                        </label>
                        <input type="text" id="callFunctionArgs" placeholder="arg1, arg2, ..."
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                    </div>
                </div>
                <div id="functionParamsDisplay" style="margin-bottom: 15px; padding: 10px; background: var(--bg-color); border-radius: 4px; display: none;">
                    <strong>Function Signature:</strong> <code id="functionSignature"></code>
                </div>
                <div style="display: flex; gap: 10px; align-items: center;">
                    <button type="button" onclick="parseContractABI()"
                            style="padding: 10px 20px; background: var(--warning-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        📋 Parse ABI
                    </button>
                    <button type="button" onclick="estimateCallGas()"
                            style="padding: 10px 20px; background: var(--secondary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        📊 Estimate Gas
                    </button>
                    <button type="submit"
                            style="padding: 10px 20px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        📞 Call Function
                    </button>
                    <span id="callGasEstimate" style="color: var(--text-secondary); font-size: 0.9rem;"></span>
                </div>
            </form>
            <div id="callResult" style="margin-top: 15px;"></div>
        </div>

        <!-- Contract Read Form -->
        <div id="readSection" class="contract-section" style="display: none;">
            <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                👁️ Read Contract State
            </h3>
            <div class="info-box">
                Query contract state without creating a transaction. No gas fees required!
            </div>
            <form id="readContractForm" onsubmit="return handleReadContract(event);">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Contract Address:
                        </label>
                        <input type="text" id="readContractAddress" placeholder="Contract address..." required
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                    </div>
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Caller Address (optional):
                        </label>
                        <input type="text" id="readCallerAddress" placeholder="Simulate call from..."
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                    </div>
                </div>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Contract ABI (JSON):
                    </label>
                    <textarea id="readABI" placeholder='[{"inputs":[],"name":"getValue","outputs":[{"name":"","type":"uint256"}],"stateMutability":"view","type":"function"}]'
                              style="width: 100%; height: 80px; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem; resize: vertical;"></textarea>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Function Name:
                        </label>
                        <select id="readFunctionName" onchange="updateReadFunctionParams()"
                                style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            <option value="">-- Load ABI first --</option>
                        </select>
                    </div>
                    <div>
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            Function Arguments:
                        </label>
                        <input type="text" id="readFunctionArgs" placeholder="arg1, arg2, ..."
                               style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                    </div>
                </div>
                <div style="display: flex; gap: 10px; align-items: center;">
                    <button type="button" onclick="parseReadABI()"
                            style="padding: 10px 20px; background: var(--warning-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        📋 Parse ABI
                    </button>
                    <button type="submit"
                            style="padding: 10px 20px; background: var(--secondary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                        👁️ Read State
                    </button>
                </div>
            </form>
            <div id="readResult" style="margin-top: 15px;"></div>
        </div>
    </div>
</div>
)HTML";


// Gas Management Dashboard Section
static const std::string GAS_MANAGEMENT_SECTION = R"HTML(
<!-- Gas Management Section -->
<div class="card" id="gasManagementSection">
    <div class="card-header">
        <h2>⛽ Gas Management
            <span class="tooltip info-icon">?
                <span class="tooltiptext">
                    <strong>Reputation-Based Gas System</strong><br>
                    Cascoin's sustainable gas system provides:<br>
                    • <strong>100x lower</strong> base costs than Ethereum<br>
                    • <strong>Reputation discounts</strong>: Higher trust = lower fees<br>
                    • <strong>Free gas</strong>: 80+ reputation gets free transactions<br>
                    • <strong>Predictable pricing</strong>: Max 2x variation<br><br>
                    Build your reputation to unlock better gas rates!
                </span>
            </span>
        </h2>
        <button class="btn-secondary" onclick="refreshGasInfo()">🔄 Refresh</button>
    </div>
    <div class="card-body">
        <!-- Gas Price Overview -->
        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 20px;">
            <div style="background: var(--bg-color); padding: 15px; border-radius: 8px; text-align: center;">
                <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 5px;">Current Gas Price</div>
                <div id="currentGasPrice" style="font-size: 1.8rem; font-weight: bold; color: var(--primary-color);">--</div>
                <div style="font-size: 0.75rem; color: var(--text-secondary);">gwei</div>
            </div>
            <div style="background: var(--bg-color); padding: 15px; border-radius: 8px; text-align: center;">
                <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 5px;">Your Discount</div>
                <div id="yourGasDiscount" style="font-size: 1.8rem; font-weight: bold; color: var(--secondary-color);">--</div>
                <div style="font-size: 0.75rem; color: var(--text-secondary);">% off base price</div>
            </div>
            <div style="background: var(--bg-color); padding: 15px; border-radius: 8px; text-align: center;">
                <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 5px;">Free Gas Allowance</div>
                <div id="freeGasAllowance" style="font-size: 1.8rem; font-weight: bold; color: var(--warning-color);">--</div>
                <div style="font-size: 0.75rem; color: var(--text-secondary);">gas units remaining</div>
            </div>
            <div style="background: var(--bg-color); padding: 15px; border-radius: 8px; text-align: center;">
                <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 5px;">Network Load</div>
                <div id="networkLoad" style="font-size: 1.8rem; font-weight: bold; color: var(--danger-color);">--</div>
                <div style="font-size: 0.75rem; color: var(--text-secondary);">% capacity</div>
            </div>
        </div>

        <!-- Gas Estimation Tool -->
        <div style="background: var(--bg-color); padding: 20px; border-radius: 8px; margin-bottom: 20px;">
            <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                📊 Gas Estimation Tool
            </h3>
            <div style="display: grid; grid-template-columns: 1fr 1fr 1fr auto; gap: 10px; align-items: end;">
                <div>
                    <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Operation Type:
                    </label>
                    <select id="gasEstimateType" style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                        <option value="transfer">Simple Transfer</option>
                        <option value="deploy">Contract Deployment</option>
                        <option value="call">Contract Call</option>
                        <option value="storage">Storage Write</option>
                    </select>
                </div>
                <div>
                    <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Your Reputation:
                    </label>
                    <input type="number" id="gasEstimateReputation" value="50" min="0" max="100"
                           style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                </div>
                <div>
                    <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">
                        Data Size (bytes):
                    </label>
                    <input type="number" id="gasEstimateDataSize" value="100" min="0"
                           style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                </div>
                <button onclick="calculateGasEstimate()" 
                        style="padding: 8px 20px; background: var(--primary-color); color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: 600;">
                    Calculate
                </button>
            </div>
            <div id="gasEstimateResult" style="margin-top: 15px; display: none;">
                <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px;">
                    <div style="background: white; padding: 12px; border-radius: 6px; text-align: center;">
                        <div style="font-size: 0.8rem; color: var(--text-secondary);">Base Gas</div>
                        <div id="gasEstimateBase" style="font-size: 1.2rem; font-weight: bold;">--</div>
                    </div>
                    <div style="background: white; padding: 12px; border-radius: 6px; text-align: center;">
                        <div style="font-size: 0.8rem; color: var(--text-secondary);">After Discount</div>
                        <div id="gasEstimateDiscounted" style="font-size: 1.2rem; font-weight: bold; color: var(--secondary-color);">--</div>
                    </div>
                    <div style="background: white; padding: 12px; border-radius: 6px; text-align: center;">
                        <div style="font-size: 0.8rem; color: var(--text-secondary);">Est. Cost (CAS)</div>
                        <div id="gasEstimateCost" style="font-size: 1.2rem; font-weight: bold; color: var(--primary-color);">--</div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Reputation-Based Pricing Table -->
        <div style="background: var(--bg-color); padding: 20px; border-radius: 8px;">
            <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                💎 Reputation-Based Gas Pricing
            </h3>
            <table class="stats-table" style="width: 100%;">
                <thead>
                    <tr style="background: rgba(37, 99, 235, 0.1);">
                        <th style="padding: 10px; text-align: left;">Reputation Level</th>
                        <th style="padding: 10px; text-align: center;">Score Range</th>
                        <th style="padding: 10px; text-align: center;">Gas Discount</th>
                        <th style="padding: 10px; text-align: center;">Free Gas</th>
                        <th style="padding: 10px; text-align: center;">Priority</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td style="padding: 10px;">🏆 Elite</td>
                        <td style="padding: 10px; text-align: center;">90-100</td>
                        <td style="padding: 10px; text-align: center; color: var(--secondary-color); font-weight: bold;">50%</td>
                        <td style="padding: 10px; text-align: center;">✅ Unlimited</td>
                        <td style="padding: 10px; text-align: center;">⚡ Highest</td>
                    </tr>
                    <tr style="background: rgba(0,0,0,0.02);">
                        <td style="padding: 10px;">⭐ High</td>
                        <td style="padding: 10px; text-align: center;">80-89</td>
                        <td style="padding: 10px; text-align: center; color: var(--secondary-color); font-weight: bold;">40%</td>
                        <td style="padding: 10px; text-align: center;">✅ 1M gas/day</td>
                        <td style="padding: 10px; text-align: center;">⚡ High</td>
                    </tr>
                    <tr>
                        <td style="padding: 10px;">✨ Good</td>
                        <td style="padding: 10px; text-align: center;">60-79</td>
                        <td style="padding: 10px; text-align: center; color: var(--secondary-color); font-weight: bold;">25%</td>
                        <td style="padding: 10px; text-align: center;">❌ None</td>
                        <td style="padding: 10px; text-align: center;">Normal</td>
                    </tr>
                    <tr style="background: rgba(0,0,0,0.02);">
                        <td style="padding: 10px;">📊 Average</td>
                        <td style="padding: 10px; text-align: center;">40-59</td>
                        <td style="padding: 10px; text-align: center; color: var(--warning-color); font-weight: bold;">10%</td>
                        <td style="padding: 10px; text-align: center;">❌ None</td>
                        <td style="padding: 10px; text-align: center;">Normal</td>
                    </tr>
                    <tr>
                        <td style="padding: 10px;">⚠️ Low</td>
                        <td style="padding: 10px; text-align: center;">0-39</td>
                        <td style="padding: 10px; text-align: center; color: var(--danger-color); font-weight: bold;">0%</td>
                        <td style="padding: 10px; text-align: center;">❌ None</td>
                        <td style="padding: 10px; text-align: center;">Low</td>
                    </tr>
                </tbody>
            </table>
            <p style="margin-top: 10px; font-size: 0.85rem; color: var(--text-secondary); text-align: center;">
                Build your reputation through positive interactions to unlock better gas rates!
            </p>
        </div>
    </div>
</div>
)HTML";


// Trust-Aware Dashboard Section
static const std::string TRUST_AWARE_SECTION = R"HTML(
<!-- Trust-Aware Contract Interactions Section -->
<div class="card" id="trustAwareSection">
    <div class="card-header">
        <h2>🛡️ Trust-Aware Interactions
            <span class="tooltip info-icon">?
                <span class="tooltiptext">
                    <strong>Trust Score Tracking</strong><br>
                    Monitor trust scores for contract interactions:<br>
                    • <strong>Caller Trust</strong>: Your reputation when calling contracts<br>
                    • <strong>Contract Trust</strong>: Contract deployer's reputation<br>
                    • <strong>Interaction History</strong>: Track trust changes over time<br>
                    • <strong>Risk Assessment</strong>: Evaluate counterparty risk<br><br>
                    Higher trust scores mean safer interactions!
                </span>
            </span>
        </h2>
        <button class="btn-secondary" onclick="refreshTrustInfo()">🔄 Refresh</button>
    </div>
    <div class="card-body">
        <!-- Address Trust Lookup -->
        <div style="background: var(--bg-color); padding: 20px; border-radius: 8px; margin-bottom: 20px;">
            <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                🔍 Address Trust Lookup
            </h3>
            <div style="display: flex; gap: 10px; margin-bottom: 15px;">
                <input type="text" id="trustLookupAddress" placeholder="Enter address to check trust score..."
                       style="flex: 1; padding: 10px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace;">
                <button onclick="lookupAddressTrust()"
                        style="padding: 10px 20px; background: var(--primary-color); color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: 600;">
                    🔍 Lookup
                </button>
            </div>
            <div id="trustLookupResult" style="display: none;">
                <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px;">
                    <div style="background: white; padding: 15px; border-radius: 8px; text-align: center; border-left: 4px solid var(--primary-color);">
                        <div style="font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 5px;">HAT v2 Score</div>
                        <div id="lookupHATScore" style="font-size: 2rem; font-weight: bold; color: var(--primary-color);">--</div>
                    </div>
                    <div style="background: white; padding: 15px; border-radius: 8px; text-align: center; border-left: 4px solid var(--secondary-color);">
                        <div style="font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 5px;">Behavior</div>
                        <div id="lookupBehavior" style="font-size: 1.5rem; font-weight: bold; color: var(--secondary-color);">--</div>
                    </div>
                    <div style="background: white; padding: 15px; border-radius: 8px; text-align: center; border-left: 4px solid var(--warning-color);">
                        <div style="font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 5px;">Web-of-Trust</div>
                        <div id="lookupWoT" style="font-size: 1.5rem; font-weight: bold; color: var(--warning-color);">--</div>
                    </div>
                    <div style="background: white; padding: 15px; border-radius: 8px; text-align: center; border-left: 4px solid var(--danger-color);">
                        <div style="font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 5px;">Economic</div>
                        <div id="lookupEconomic" style="font-size: 1.5rem; font-weight: bold; color: var(--danger-color);">--</div>
                    </div>
                </div>
                <div id="trustRiskAssessment" style="margin-top: 15px; padding: 15px; border-radius: 8px;"></div>
            </div>
        </div>

        <!-- Contract Trust Analysis -->
        <div style="background: var(--bg-color); padding: 20px; border-radius: 8px; margin-bottom: 20px;">
            <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                📜 Contract Trust Analysis
            </h3>
            <div style="display: flex; gap: 10px; margin-bottom: 15px;">
                <input type="text" id="contractTrustAddress" placeholder="Enter contract address..."
                       style="flex: 1; padding: 10px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace;">
                <button onclick="analyzeContractTrust()"
                        style="padding: 10px 20px; background: var(--secondary-color); color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: 600;">
                    📊 Analyze
                </button>
            </div>
            <div id="contractTrustResult" style="display: none;">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div style="background: white; padding: 15px; border-radius: 8px;">
                        <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 10px;">Contract Info</div>
                        <div style="font-size: 0.9rem;">
                            <div><strong>Deployer:</strong> <span id="contractDeployer" style="font-family: monospace;">--</span></div>
                            <div><strong>Deploy Block:</strong> <span id="contractDeployBlock">--</span></div>
                            <div><strong>Total Calls:</strong> <span id="contractTotalCalls">--</span></div>
                        </div>
                    </div>
                    <div style="background: white; padding: 15px; border-radius: 8px;">
                        <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 10px;">Trust Metrics</div>
                        <div style="font-size: 0.9rem;">
                            <div><strong>Deployer Trust:</strong> <span id="contractDeployerTrust" style="font-weight: bold;">--</span></div>
                            <div><strong>Avg Caller Trust:</strong> <span id="contractAvgCallerTrust">--</span></div>
                            <div><strong>Risk Level:</strong> <span id="contractRiskLevel">--</span></div>
                        </div>
                    </div>
                </div>
                <div id="contractInteractionHistory" style="background: white; padding: 15px; border-radius: 8px;">
                    <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 10px;">Recent Interactions</div>
                    <div id="contractInteractionsList" style="max-height: 200px; overflow-y: auto;"></div>
                </div>
            </div>
        </div>

        <!-- Trust Score Comparison -->
        <div style="background: var(--bg-color); padding: 20px; border-radius: 8px;">
            <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                ⚖️ Trust Score Comparison
            </h3>
            <p style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 15px;">
                Compare trust scores between two addresses to assess interaction risk.
            </p>
            <div style="display: grid; grid-template-columns: 1fr auto 1fr; gap: 15px; align-items: end;">
                <div>
                    <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">Address 1:</label>
                    <input type="text" id="compareAddress1" placeholder="First address..."
                           style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">
                </div>
                <div style="text-align: center; padding-bottom: 8px;">
                    <span style="font-size: 1.5rem;">⚔️</span>
                </div>
                <div>
                    <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">Address 2:</label>
                    <input type="text" id="compareAddress2" placeholder="Second address..."
                           style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">
                </div>
            </div>
            <div style="text-align: center; margin-top: 15px;">
                <button onclick="compareTrustScores()"
                        style="padding: 10px 30px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                    ⚖️ Compare Trust Scores
                </button>
            </div>
            <div id="trustComparisonResult" style="margin-top: 15px; display: none;">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 20px;">
                    <div id="compareResult1" style="background: white; padding: 20px; border-radius: 8px; text-align: center;"></div>
                    <div id="compareResult2" style="background: white; padding: 20px; border-radius: 8px; text-align: center;"></div>
                </div>
                <div id="comparisonVerdict" style="margin-top: 15px; padding: 15px; border-radius: 8px; text-align: center;"></div>
            </div>
        </div>
    </div>
</div>
)HTML";


// JavaScript for EVM Dashboard functionality
static const std::string EVM_DASHBOARD_JS = R"JS(
// ===== EVM Contract Interaction Functions =====

// Toggle between contract sections
function toggleContractSection(section) {
    document.getElementById('deploySection').style.display = section === 'deploy' ? 'block' : 'none';
    document.getElementById('callSection').style.display = section === 'call' ? 'block' : 'none';
    document.getElementById('readSection').style.display = section === 'read' ? 'block' : 'none';
}

// Deploy contract handler
async function handleDeployContract(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('deployResult');
    
    try {
        const fromAddress = document.getElementById('deployFromAddress').value;
        const bytecode = document.getElementById('deployBytecode').value;
        const gasLimit = parseInt(document.getElementById('deployGasLimit').value);
        const abi = document.getElementById('deployABI').value;
        const constructorArgs = document.getElementById('deployConstructorArgs').value;
        
        resultDiv.innerHTML = '<div style="color: var(--warning-color);">⏳ Deploying contract...</div>';
        
        // Encode constructor arguments if ABI and args provided
        let data = bytecode;
        if (abi && constructorArgs) {
            try {
                const parsedABI = JSON.parse(abi);
                const constructor = parsedABI.find(item => item.type === 'constructor');
                if (constructor && constructor.inputs.length > 0) {
                    const encodedArgs = encodeABIParameters(constructor.inputs, constructorArgs.split(',').map(a => a.trim()));
                    data = bytecode + encodedArgs;
                }
            } catch (e) {
                console.warn('ABI encoding failed, using raw bytecode:', e);
            }
        }
        
        const result = await window.dashboard.rpcCall('cas_sendTransaction', [{
            from: fromAddress,
            data: data,
            gas: '0x' + gasLimit.toString(16)
        }]);
        
        resultDiv.innerHTML = `
            <div style="background: rgba(16, 185, 129, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--secondary-color);">
                <div style="font-weight: bold; color: var(--secondary-color); margin-bottom: 10px;">✅ Contract Deployment Submitted!</div>
                <div style="font-size: 0.9rem;">
                    <div><strong>Transaction Hash:</strong></div>
                    <code style="word-break: break-all;">${result}</code>
                </div>
                <div style="margin-top: 10px; font-size: 0.85rem; color: var(--text-secondary);">
                    Wait for confirmation to get the contract address.
                </div>
            </div>
        `;
    } catch (error) {
        resultDiv.innerHTML = `
            <div style="background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">
                <div style="font-weight: bold; color: var(--danger-color);">❌ Deployment Failed</div>
                <div style="font-size: 0.9rem; margin-top: 5px;">${error.message}</div>
            </div>
        `;
    }
    return false;
}

// Estimate deployment gas
async function estimateDeployGas() {
    const estimateSpan = document.getElementById('deployGasEstimate');
    try {
        const fromAddress = document.getElementById('deployFromAddress').value;
        const bytecode = document.getElementById('deployBytecode').value;
        
        if (!fromAddress || !bytecode) {
            estimateSpan.innerHTML = '<span style="color: var(--danger-color);">Fill in address and bytecode first</span>';
            return;
        }
        
        estimateSpan.innerHTML = '<span style="color: var(--warning-color);">Estimating...</span>';
        
        const result = await window.dashboard.rpcCall('cas_estimateGas', [{
            from: fromAddress,
            data: bytecode
        }]);
        
        const gasEstimate = parseInt(result, 16);
        estimateSpan.innerHTML = `<span style="color: var(--secondary-color);">Estimated: ${gasEstimate.toLocaleString()} gas</span>`;
        
        // Update gas limit field with estimate + 20% buffer
        document.getElementById('deployGasLimit').value = Math.ceil(gasEstimate * 1.2);
    } catch (error) {
        estimateSpan.innerHTML = `<span style="color: var(--danger-color);">Estimation failed: ${error.message}</span>`;
    }
}

// Parse contract ABI for call section
function parseContractABI() {
    try {
        const abiText = document.getElementById('callABI').value;
        const abi = JSON.parse(abiText);
        const functionSelect = document.getElementById('callFunctionName');
        
        functionSelect.innerHTML = '<option value="">-- Select Function --</option>';
        
        abi.filter(item => item.type === 'function' && item.stateMutability !== 'view' && item.stateMutability !== 'pure')
           .forEach(func => {
               const option = document.createElement('option');
               option.value = func.name;
               option.textContent = `${func.name}(${func.inputs.map(i => i.type).join(', ')})`;
               option.dataset.inputs = JSON.stringify(func.inputs);
               functionSelect.appendChild(option);
           });
        
        alert('ABI parsed successfully! Select a function from the dropdown.');
    } catch (error) {
        alert('Failed to parse ABI: ' + error.message);
    }
}

// Update function parameters display
function updateFunctionParams() {
    const select = document.getElementById('callFunctionName');
    const selectedOption = select.options[select.selectedIndex];
    const paramsDiv = document.getElementById('functionParamsDisplay');
    const sigSpan = document.getElementById('functionSignature');
    
    if (selectedOption.value && selectedOption.dataset.inputs) {
        const inputs = JSON.parse(selectedOption.dataset.inputs);
        sigSpan.textContent = `${selectedOption.value}(${inputs.map(i => `${i.type} ${i.name}`).join(', ')})`;
        paramsDiv.style.display = 'block';
    } else {
        paramsDiv.style.display = 'none';
    }
}

// Call contract handler
async function handleCallContract(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('callResult');
    
    try {
        const fromAddress = document.getElementById('callFromAddress').value;
        const contractAddress = document.getElementById('callContractAddress').value;
        const gasLimit = parseInt(document.getElementById('callGasLimit').value);
        const value = parseFloat(document.getElementById('callValue').value) || 0;
        const functionName = document.getElementById('callFunctionName').value;
        const functionArgs = document.getElementById('callFunctionArgs').value;
        const abiText = document.getElementById('callABI').value;
        
        resultDiv.innerHTML = '<div style="color: var(--warning-color);">⏳ Calling contract...</div>';
        
        // Encode function call
        let data = '0x';
        if (abiText && functionName) {
            const abi = JSON.parse(abiText);
            const func = abi.find(item => item.type === 'function' && item.name === functionName);
            if (func) {
                data = encodeFunctionCall(func, functionArgs ? functionArgs.split(',').map(a => a.trim()) : []);
            }
        }
        
        const result = await window.dashboard.rpcCall('cas_sendTransaction', [{
            from: fromAddress,
            to: contractAddress,
            data: data,
            gas: '0x' + gasLimit.toString(16),
            value: '0x' + Math.floor(value * 1e8).toString(16)
        }]);
        
        resultDiv.innerHTML = `
            <div style="background: rgba(16, 185, 129, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--secondary-color);">
                <div style="font-weight: bold; color: var(--secondary-color); margin-bottom: 10px;">✅ Transaction Submitted!</div>
                <div style="font-size: 0.9rem;">
                    <div><strong>Transaction Hash:</strong></div>
                    <code style="word-break: break-all;">${result}</code>
                </div>
            </div>
        `;
    } catch (error) {
        resultDiv.innerHTML = `
            <div style="background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">
                <div style="font-weight: bold; color: var(--danger-color);">❌ Call Failed</div>
                <div style="font-size: 0.9rem; margin-top: 5px;">${error.message}</div>
            </div>
        `;
    }
    return false;
}

// Estimate call gas
async function estimateCallGas() {
    const estimateSpan = document.getElementById('callGasEstimate');
    try {
        const fromAddress = document.getElementById('callFromAddress').value;
        const contractAddress = document.getElementById('callContractAddress').value;
        const functionName = document.getElementById('callFunctionName').value;
        const functionArgs = document.getElementById('callFunctionArgs').value;
        const abiText = document.getElementById('callABI').value;
        
        if (!fromAddress || !contractAddress) {
            estimateSpan.innerHTML = '<span style="color: var(--danger-color);">Fill in addresses first</span>';
            return;
        }
        
        estimateSpan.innerHTML = '<span style="color: var(--warning-color);">Estimating...</span>';
        
        let data = '0x';
        if (abiText && functionName) {
            const abi = JSON.parse(abiText);
            const func = abi.find(item => item.type === 'function' && item.name === functionName);
            if (func) {
                data = encodeFunctionCall(func, functionArgs ? functionArgs.split(',').map(a => a.trim()) : []);
            }
        }
        
        const result = await window.dashboard.rpcCall('cas_estimateGas', [{
            from: fromAddress,
            to: contractAddress,
            data: data
        }]);
        
        const gasEstimate = parseInt(result, 16);
        estimateSpan.innerHTML = `<span style="color: var(--secondary-color);">Estimated: ${gasEstimate.toLocaleString()} gas</span>`;
        document.getElementById('callGasLimit').value = Math.ceil(gasEstimate * 1.2);
    } catch (error) {
        estimateSpan.innerHTML = `<span style="color: var(--danger-color);">Estimation failed: ${error.message}</span>`;
    }
}

// Parse read ABI
function parseReadABI() {
    try {
        const abiText = document.getElementById('readABI').value;
        const abi = JSON.parse(abiText);
        const functionSelect = document.getElementById('readFunctionName');
        
        functionSelect.innerHTML = '<option value="">-- Select Function --</option>';
        
        abi.filter(item => item.type === 'function' && (item.stateMutability === 'view' || item.stateMutability === 'pure'))
           .forEach(func => {
               const option = document.createElement('option');
               option.value = func.name;
               option.textContent = `${func.name}(${func.inputs.map(i => i.type).join(', ')}) → ${func.outputs.map(o => o.type).join(', ')}`;
               option.dataset.inputs = JSON.stringify(func.inputs);
               option.dataset.outputs = JSON.stringify(func.outputs);
               functionSelect.appendChild(option);
           });
        
        alert('ABI parsed successfully! Select a view function from the dropdown.');
    } catch (error) {
        alert('Failed to parse ABI: ' + error.message);
    }
}

// Read contract handler
async function handleReadContract(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('readResult');
    
    try {
        const contractAddress = document.getElementById('readContractAddress').value;
        const callerAddress = document.getElementById('readCallerAddress').value || contractAddress;
        const functionName = document.getElementById('readFunctionName').value;
        const functionArgs = document.getElementById('readFunctionArgs').value;
        const abiText = document.getElementById('readABI').value;
        
        resultDiv.innerHTML = '<div style="color: var(--warning-color);">⏳ Reading contract state...</div>';
        
        let data = '0x';
        let outputTypes = [];
        if (abiText && functionName) {
            const abi = JSON.parse(abiText);
            const func = abi.find(item => item.type === 'function' && item.name === functionName);
            if (func) {
                data = encodeFunctionCall(func, functionArgs ? functionArgs.split(',').map(a => a.trim()) : []);
                outputTypes = func.outputs;
            }
        }
        
        const result = await window.dashboard.rpcCall('cas_call', [{
            from: callerAddress,
            to: contractAddress,
            data: data
        }, 'latest']);
        
        // Decode result
        let decodedResult = result;
        if (outputTypes.length > 0) {
            try {
                decodedResult = decodeABIResult(outputTypes, result);
            } catch (e) {
                console.warn('Failed to decode result:', e);
            }
        }
        
        resultDiv.innerHTML = `
            <div style="background: rgba(16, 185, 129, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--secondary-color);">
                <div style="font-weight: bold; color: var(--secondary-color); margin-bottom: 10px;">✅ Read Successful!</div>
                <div style="font-size: 0.9rem;">
                    <div><strong>Raw Result:</strong></div>
                    <code style="word-break: break-all; display: block; margin: 5px 0; padding: 10px; background: rgba(0,0,0,0.05); border-radius: 4px;">${result}</code>
                    ${typeof decodedResult !== 'string' ? `
                    <div style="margin-top: 10px;"><strong>Decoded:</strong></div>
                    <pre style="margin: 5px 0; padding: 10px; background: rgba(0,0,0,0.05); border-radius: 4px; overflow-x: auto;">${JSON.stringify(decodedResult, null, 2)}</pre>
                    ` : ''}
                </div>
            </div>
        `;
    } catch (error) {
        resultDiv.innerHTML = `
            <div style="background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">
                <div style="font-weight: bold; color: var(--danger-color);">❌ Read Failed</div>
                <div style="font-size: 0.9rem; margin-top: 5px;">${error.message}</div>
            </div>
        `;
    }
    return false;
}

// ===== ABI Encoding/Decoding Helpers =====

function encodeFunctionCall(func, args) {
    // Calculate function selector (first 4 bytes of keccak256 hash)
    const signature = `${func.name}(${func.inputs.map(i => i.type).join(',')})`;
    const selector = keccak256(signature).substring(0, 10);
    
    // Encode parameters
    const encodedParams = encodeABIParameters(func.inputs, args);
    
    return selector + encodedParams;
}

function encodeABIParameters(types, values) {
    let encoded = '';
    for (let i = 0; i < types.length; i++) {
        const type = types[i].type;
        const value = values[i];
        
        if (type === 'uint256' || type.startsWith('uint')) {
            encoded += BigInt(value).toString(16).padStart(64, '0');
        } else if (type === 'int256' || type.startsWith('int')) {
            const bigVal = BigInt(value);
            if (bigVal < 0) {
                encoded += (BigInt(2) ** BigInt(256) + bigVal).toString(16).padStart(64, '0');
            } else {
                encoded += bigVal.toString(16).padStart(64, '0');
            }
        } else if (type === 'address') {
            encoded += value.replace('0x', '').toLowerCase().padStart(64, '0');
        } else if (type === 'bool') {
            encoded += (value === 'true' || value === '1' ? '1' : '0').padStart(64, '0');
        } else if (type === 'bytes32') {
            encoded += value.replace('0x', '').padEnd(64, '0');
        } else if (type === 'string' || type === 'bytes') {
            // Dynamic types - simplified encoding
            const hex = stringToHex(value);
            const length = (hex.length / 2).toString(16).padStart(64, '0');
            const data = hex.padEnd(Math.ceil(hex.length / 64) * 64, '0');
            encoded += length + data;
        } else {
            // Default: treat as uint256
            encoded += BigInt(value || 0).toString(16).padStart(64, '0');
        }
    }
    return encoded;
}

function decodeABIResult(types, data) {
    if (!data || data === '0x') return null;
    
    const hex = data.replace('0x', '');
    const results = [];
    let offset = 0;
    
    for (const output of types) {
        const type = output.type;
        const chunk = hex.substring(offset, offset + 64);
        
        if (type === 'uint256' || type.startsWith('uint')) {
            results.push(BigInt('0x' + chunk).toString());
        } else if (type === 'int256' || type.startsWith('int')) {
            const val = BigInt('0x' + chunk);
            if (val > BigInt(2) ** BigInt(255)) {
                results.push((val - BigInt(2) ** BigInt(256)).toString());
            } else {
                results.push(val.toString());
            }
        } else if (type === 'address') {
            results.push('0x' + chunk.substring(24));
        } else if (type === 'bool') {
            results.push(chunk[63] === '1');
        } else if (type === 'bytes32') {
            results.push('0x' + chunk);
        } else {
            results.push('0x' + chunk);
        }
        
        offset += 64;
    }
    
    return results.length === 1 ? results[0] : results;
}

function stringToHex(str) {
    let hex = '';
    for (let i = 0; i < str.length; i++) {
        hex += str.charCodeAt(i).toString(16).padStart(2, '0');
    }
    return hex;
}

// Simple keccak256 implementation (for function selectors)
function keccak256(str) {
    // This is a simplified version - in production, use a proper library
    // For now, we'll use a hash approximation for demo purposes
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
        const char = str.charCodeAt(i);
        hash = ((hash << 5) - hash) + char;
        hash = hash & hash;
    }
    // Return a pseudo-hash (in production, use proper keccak256)
    return '0x' + Math.abs(hash).toString(16).padStart(8, '0') + '00000000';
}
)JS";


// JavaScript for Gas Management functionality
static const std::string GAS_MANAGEMENT_JS = R"JS(
// ===== Gas Management Functions =====

async function refreshGasInfo() {
    try {
        // Get current gas price
        const gasPrice = await window.dashboard.rpcCall('cas_gasPrice', []);
        const gasPriceGwei = parseInt(gasPrice, 16) / 1e9;
        document.getElementById('currentGasPrice').textContent = gasPriceGwei.toFixed(4);
        
        // Get user's reputation and calculate discount
        try {
            const addresses = await window.dashboard.rpcCall('listaddressgroupings', []);
            if (addresses && addresses.length > 0 && addresses[0].length > 0) {
                const myAddress = addresses[0][0][0];
                const repResult = await window.dashboard.rpcCall('getsecuretrust', [myAddress]);
                const reputation = repResult.final_score || 50;
                
                // Calculate discount based on reputation
                let discount = 0;
                if (reputation >= 90) discount = 50;
                else if (reputation >= 80) discount = 40;
                else if (reputation >= 60) discount = 25;
                else if (reputation >= 40) discount = 10;
                
                document.getElementById('yourGasDiscount').textContent = discount;
                
                // Calculate free gas allowance
                let freeGas = 0;
                if (reputation >= 90) freeGas = 'Unlimited';
                else if (reputation >= 80) freeGas = '1,000,000';
                else freeGas = '0';
                
                document.getElementById('freeGasAllowance').textContent = freeGas;
            }
        } catch (e) {
            console.warn('Could not get reputation info:', e);
            document.getElementById('yourGasDiscount').textContent = '--';
            document.getElementById('freeGasAllowance').textContent = '--';
        }
        
        // Estimate network load (simplified)
        try {
            const mempoolInfo = await window.dashboard.rpcCall('getmempoolinfo', []);
            const load = Math.min(100, Math.round((mempoolInfo.size / 5000) * 100));
            document.getElementById('networkLoad').textContent = load;
        } catch (e) {
            document.getElementById('networkLoad').textContent = '--';
        }
        
    } catch (error) {
        console.error('Failed to refresh gas info:', error);
    }
}

function calculateGasEstimate() {
    const type = document.getElementById('gasEstimateType').value;
    const reputation = parseInt(document.getElementById('gasEstimateReputation').value) || 50;
    const dataSize = parseInt(document.getElementById('gasEstimateDataSize').value) || 0;
    
    // Base gas costs (100x lower than Ethereum)
    let baseGas = 21000; // Simple transfer
    if (type === 'deploy') {
        baseGas = 32000 + (dataSize * 200); // Contract creation
    } else if (type === 'call') {
        baseGas = 21000 + (dataSize * 16); // Contract call
    } else if (type === 'storage') {
        baseGas = 20000; // Storage write (SSTORE)
    }
    
    // Calculate discount based on reputation
    let discountPercent = 0;
    if (reputation >= 90) discountPercent = 50;
    else if (reputation >= 80) discountPercent = 40;
    else if (reputation >= 60) discountPercent = 25;
    else if (reputation >= 40) discountPercent = 10;
    
    const discountedGas = Math.round(baseGas * (1 - discountPercent / 100));
    
    // Estimate cost in CAS (assuming 0.01 gwei gas price)
    const gasPriceGwei = 0.01;
    const costCAS = (discountedGas * gasPriceGwei * 1e-9).toFixed(8);
    
    // Show results
    document.getElementById('gasEstimateResult').style.display = 'block';
    document.getElementById('gasEstimateBase').textContent = baseGas.toLocaleString();
    document.getElementById('gasEstimateDiscounted').textContent = discountedGas.toLocaleString() + ` (-${discountPercent}%)`;
    document.getElementById('gasEstimateCost').textContent = costCAS;
}
)JS";

// JavaScript for Trust-Aware functionality
static const std::string TRUST_AWARE_JS = R"JS(
// ===== Trust-Aware Functions =====

async function refreshTrustInfo() {
    // Refresh any cached trust data
    console.log('Refreshing trust info...');
}

async function lookupAddressTrust() {
    const address = document.getElementById('trustLookupAddress').value;
    const resultDiv = document.getElementById('trustLookupResult');
    
    if (!address) {
        alert('Please enter an address');
        return;
    }
    
    try {
        const result = await window.dashboard.rpcCall('getsecuretrust', [address]);
        
        resultDiv.style.display = 'block';
        document.getElementById('lookupHATScore').textContent = result.final_score || '--';
        document.getElementById('lookupBehavior').textContent = Math.round((result.behavior?.secure_score || 0) * 100) + '%';
        document.getElementById('lookupWoT').textContent = Math.round((result.wot?.secure_score || 0) * 100) + '%';
        document.getElementById('lookupEconomic').textContent = Math.round((result.economic?.secure_score || 0) * 100) + '%';
        
        // Risk assessment
        const riskDiv = document.getElementById('trustRiskAssessment');
        const score = result.final_score || 0;
        let riskLevel, riskColor, riskMessage;
        
        if (score >= 80) {
            riskLevel = 'LOW RISK';
            riskColor = 'var(--secondary-color)';
            riskMessage = 'This address has a high trust score. Safe for interactions.';
        } else if (score >= 60) {
            riskLevel = 'MODERATE RISK';
            riskColor = 'var(--warning-color)';
            riskMessage = 'This address has a moderate trust score. Exercise normal caution.';
        } else if (score >= 40) {
            riskLevel = 'ELEVATED RISK';
            riskColor = '#f97316';
            riskMessage = 'This address has a below-average trust score. Proceed with caution.';
        } else {
            riskLevel = 'HIGH RISK';
            riskColor = 'var(--danger-color)';
            riskMessage = 'This address has a low trust score. High risk for interactions.';
        }
        
        riskDiv.innerHTML = `
            <div style="background: ${riskColor}20; border-left: 4px solid ${riskColor}; padding: 12px;">
                <div style="font-weight: bold; color: ${riskColor}; margin-bottom: 5px;">⚠️ ${riskLevel}</div>
                <div style="font-size: 0.9rem; color: var(--text-secondary);">${riskMessage}</div>
            </div>
        `;
        
    } catch (error) {
        resultDiv.style.display = 'block';
        resultDiv.innerHTML = `
            <div style="background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px;">
                <div style="color: var(--danger-color);">❌ Failed to lookup trust: ${error.message}</div>
            </div>
        `;
    }
}

async function analyzeContractTrust() {
    const address = document.getElementById('contractTrustAddress').value;
    const resultDiv = document.getElementById('contractTrustResult');
    
    if (!address) {
        alert('Please enter a contract address');
        return;
    }
    
    try {
        // Get contract info
        const contractInfo = await window.dashboard.rpcCall('getcontractinfo', [address]);
        
        resultDiv.style.display = 'block';
        
        // Display contract info
        document.getElementById('contractDeployer').textContent = contractInfo.deployer ? 
            contractInfo.deployer.substring(0, 12) + '...' : '--';
        document.getElementById('contractDeployBlock').textContent = contractInfo.deploy_block || '--';
        document.getElementById('contractTotalCalls').textContent = contractInfo.total_calls || '0';
        
        // Get deployer trust
        if (contractInfo.deployer) {
            try {
                const deployerTrust = await window.dashboard.rpcCall('getsecuretrust', [contractInfo.deployer]);
                document.getElementById('contractDeployerTrust').textContent = deployerTrust.final_score || '--';
                document.getElementById('contractDeployerTrust').style.color = 
                    deployerTrust.final_score >= 60 ? 'var(--secondary-color)' : 'var(--danger-color)';
            } catch (e) {
                document.getElementById('contractDeployerTrust').textContent = '--';
            }
        }
        
        // Calculate risk level
        const deployerScore = parseInt(document.getElementById('contractDeployerTrust').textContent) || 0;
        let riskLevel, riskColor;
        if (deployerScore >= 80) {
            riskLevel = '🟢 Low';
            riskColor = 'var(--secondary-color)';
        } else if (deployerScore >= 60) {
            riskLevel = '🟡 Moderate';
            riskColor = 'var(--warning-color)';
        } else {
            riskLevel = '🔴 High';
            riskColor = 'var(--danger-color)';
        }
        document.getElementById('contractRiskLevel').innerHTML = `<span style="color: ${riskColor};">${riskLevel}</span>`;
        document.getElementById('contractAvgCallerTrust').textContent = '--';
        
        // Show placeholder for interaction history
        document.getElementById('contractInteractionsList').innerHTML = `
            <div style="text-align: center; color: var(--text-secondary); padding: 20px;">
                Contract interaction history will be displayed here.
            </div>
        `;
        
    } catch (error) {
        resultDiv.style.display = 'block';
        resultDiv.innerHTML = `
            <div style="background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px;">
                <div style="color: var(--danger-color);">❌ Failed to analyze contract: ${error.message}</div>
            </div>
        `;
    }
}

async function compareTrustScores() {
    const address1 = document.getElementById('compareAddress1').value;
    const address2 = document.getElementById('compareAddress2').value;
    
    if (!address1 || !address2) {
        alert('Please enter both addresses');
        return;
    }
    
    const resultDiv = document.getElementById('trustComparisonResult');
    
    try {
        const [trust1, trust2] = await Promise.all([
            window.dashboard.rpcCall('getsecuretrust', [address1]),
            window.dashboard.rpcCall('getsecuretrust', [address2])
        ]);
        
        resultDiv.style.display = 'block';
        
        const score1 = trust1.final_score || 0;
        const score2 = trust2.final_score || 0;
        
        const getColor = (score) => {
            if (score >= 80) return 'var(--secondary-color)';
            if (score >= 60) return 'var(--warning-color)';
            return 'var(--danger-color)';
        };
        
        document.getElementById('compareResult1').innerHTML = `
            <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 10px;">Address 1</div>
            <div style="font-size: 3rem; font-weight: bold; color: ${getColor(score1)};">${score1}</div>
            <div style="font-size: 0.8rem; color: var(--text-secondary); margin-top: 5px;">
                ${address1.substring(0, 12)}...
            </div>
        `;
        
        document.getElementById('compareResult2').innerHTML = `
            <div style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 10px;">Address 2</div>
            <div style="font-size: 3rem; font-weight: bold; color: ${getColor(score2)};">${score2}</div>
            <div style="font-size: 0.8rem; color: var(--text-secondary); margin-top: 5px;">
                ${address2.substring(0, 12)}...
            </div>
        `;
        
        // Verdict
        const verdictDiv = document.getElementById('comparisonVerdict');
        let verdict, verdictColor;
        
        if (Math.abs(score1 - score2) < 10) {
            verdict = '⚖️ Similar trust levels - Both addresses have comparable reputation';
            verdictColor = 'var(--primary-color)';
        } else if (score1 > score2) {
            verdict = `✅ Address 1 is more trusted (+${score1 - score2} points)`;
            verdictColor = 'var(--secondary-color)';
        } else {
            verdict = `✅ Address 2 is more trusted (+${score2 - score1} points)`;
            verdictColor = 'var(--secondary-color)';
        }
        
        verdictDiv.innerHTML = `
            <div style="background: ${verdictColor}20; color: ${verdictColor}; font-weight: bold;">
                ${verdict}
            </div>
        `;
        
    } catch (error) {
        resultDiv.style.display = 'block';
        document.getElementById('comparisonVerdict').innerHTML = `
            <div style="background: rgba(239, 68, 68, 0.1); color: var(--danger-color);">
                ❌ Comparison failed: ${error.message}
            </div>
        `;
    }
}

// Initialize gas info on page load
document.addEventListener('DOMContentLoaded', function() {
    setTimeout(refreshGasInfo, 2000);
});
)JS";

} // namespace CVMDashboardEVM

#endif // CASCOIN_HTTPSERVER_CVMDASHBOARD_EVM_H
