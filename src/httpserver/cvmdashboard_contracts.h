// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_CVMDASHBOARD_CONTRACTS_H
#define CASCOIN_HTTPSERVER_CVMDASHBOARD_CONTRACTS_H

#include <string>

namespace CVMDashboardContracts {

/**
 * Contract Management Dashboard Extension
 *
 * Provides web-based interface for:
 * - Listing wallet-owned contracts (listmycontracts)
 * - Contract detail view with metadata
 * - Tab navigation: Interact, Storage, Transactions
 * - Auto-refresh every 30 seconds
 * - Error handling with retry button
 *
 * Requirements: 1.1, 1.2, 1.4, 3.1, 3.2, 7.2
 */

// Contract Management HTML Section
static const std::string CONTRACT_MANAGEMENT_SECTION = R"HTML(
<!-- Contract Management Section -->
<div class="card" id="contractManagementSection">
    <div class="card-header">
        <h2>📋 Meine Contracts
            <span class="tooltip info-icon">?
                <span class="tooltiptext">
                    <strong>Contract Management</strong><br>
                    Übersicht aller Smart Contracts Ihrer Wallet:<br>
                    • <strong>Liste</strong>: Alle von Ihnen deployten Contracts<br>
                    • <strong>Details</strong>: Metadaten und Status<br>
                    • <strong>Interagieren</strong>: Funktionen aufrufen<br>
                    • <strong>Storage</strong>: Persistenten Zustand einsehen<br>
                    • <strong>Transaktionen</strong>: Aufruf-Historie<br><br>
                    Auto-Refresh alle 30 Sekunden.
                </span>
            </span>
        </h2>
        <button class="btn-secondary" onclick="loadMyContracts()">🔄 Refresh</button>
    </div>
    <div class="card-body">
        <!-- Wallet Status Banner (Req 7.1, 7.3) -->
        <div id="walletStatusBanner" style="display: none; padding: 12px 16px; border-radius: 8px; margin-bottom: 15px; font-size: 0.9rem;">
        </div>
        <!-- Contract List -->
        <div id="contractListContainer">
            <div id="contractListLoading" style="text-align: center; padding: 20px; color: var(--text-secondary);">
                ⏳ Lade Contracts...
            </div>
            <div id="contractListError" style="display: none; background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">
                <div style="font-weight: bold; color: var(--danger-color); margin-bottom: 10px;">❌ Fehler beim Laden der Contracts</div>
                <div id="contractListErrorMsg" style="font-size: 0.9rem; color: var(--text-secondary); margin-bottom: 10px;"></div>
                <button onclick="loadMyContracts()" style="padding: 8px 16px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                    🔄 Erneut versuchen
                </button>
            </div>
            <div id="contractListEmpty" style="display: none; text-align: center; padding: 30px; color: var(--text-secondary);">
                <div style="font-size: 2rem; margin-bottom: 10px;">📭</div>
                <div style="font-size: 1rem; margin-bottom: 5px;">Keine Contracts gefunden.</div>
                <div style="font-size: 0.85rem;">Deployen Sie einen Contract über die EVM-Sektion ↑</div>
            </div>
            <table id="contractListTable" class="stats-table" style="display: none; width: 100%;">
                <thead>
                    <tr style="background: rgba(37, 99, 235, 0.1);">
                        <th style="padding: 10px; text-align: left;">Contract-Adresse</th>
                        <th style="padding: 10px; text-align: center;">Format</th>
                        <th style="padding: 10px; text-align: center;">Block</th>
                    </tr>
                </thead>
                <tbody id="contractListBody">
                </tbody>
            </table>
        </div>

        <!-- Contract Detail View -->
        <div id="contractDetailView" style="display: none; margin-top: 20px;">
            <div style="background: var(--bg-color); padding: 20px; border-radius: 8px; margin-bottom: 20px;">
                <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                    📄 Contract Details: <span id="contractDetailTitle" style="font-family: monospace;"></span>
                </h3>
                <div id="contractDetailLoading" style="text-align: center; padding: 15px; color: var(--text-secondary);">
                    ⏳ Lade Details...
                </div>
                <table id="contractDetailTable" class="stats-table" style="display: none;">
                    <tbody>
                        <tr><td>Adresse</td><td id="detailAddress" style="font-family: monospace; word-break: break-all;"></td></tr>
                        <tr><td>Deployer</td><td id="detailDeployer" style="font-family: monospace; word-break: break-all;"></td></tr>
                        <tr><td>Block</td><td id="detailBlock"></td></tr>
                        <tr><td>TX</td><td id="detailTx" style="font-family: monospace; word-break: break-all;"></td></tr>
                        <tr><td>Größe</td><td id="detailSize"></td></tr>
                        <tr><td>Format</td><td id="detailFormat"></td></tr>
                        <tr><td>Status</td><td id="detailStatus"></td></tr>
                    </tbody>
                </table>
            </div>

            <!-- Tab Navigation -->
            <div style="display: flex; gap: 10px; margin-bottom: 15px;">
                <button class="btn-secondary" id="tabInteract" onclick="switchContractTab('interact')" style="font-weight: bold; border-color: var(--primary-color); color: var(--primary-color);">
                    ⚡ Interagieren
                </button>
                <button class="btn-secondary" id="tabStorage" onclick="switchContractTab('storage')">
                    💾 Storage
                </button>
                <button class="btn-secondary" id="tabTransactions" onclick="switchContractTab('transactions')">
                    📜 Transaktionen
                </button>
            </div>

            <!-- Tab Content: Interact -->
            <div id="tabContentInteract" class="contract-tab-content">
                <div style="background: var(--bg-color); padding: 20px; border-radius: 8px;">
                    <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                        ⚡ Contract Interaktion
                    </h3>
                    <div class="info-box">
                        Geben Sie die ABI des Contracts ein, um Funktionen aufzurufen. Ohne ABI können Sie Raw-Hex-Daten senden.
                    </div>

                    <!-- ABI Input -->
                    <div style="margin-bottom: 20px;">
                        <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">
                            ABI (JSON):
                        </label>
                        <textarea id="contractAbiInput" placeholder='[{"inputs":[],"name":"getValue","outputs":[{"name":"","type":"uint256"}],"stateMutability":"view","type":"function"}]'
                                  style="width: 100%; height: 80px; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem; resize: vertical;"></textarea>
                        <div id="contractAbiError" style="display: none; color: var(--danger-color); font-size: 0.85rem; margin-top: 5px;"></div>
                        <button onclick="parseInteractABI()" style="margin-top: 8px; padding: 8px 16px; background: var(--warning-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                            📋 Parse ABI
                        </button>
                    </div>

                    <!-- Read Functions -->
                    <div id="readFunctionsSection" style="display: none; margin-bottom: 20px;">
                        <h4 style="font-size: 0.95rem; margin-bottom: 10px; color: var(--secondary-color);">
                            📖 Read Functions
                        </h4>
                        <div id="readFunctionsList" style="border: 1px solid var(--border-color); border-radius: 8px; overflow: hidden;">
                        </div>
                    </div>

                    <!-- Write Functions -->
                    <div id="writeFunctionsSection" style="display: none; margin-bottom: 20px;">
                        <h4 style="font-size: 0.95rem; margin-bottom: 10px; color: var(--primary-color);">
                            ✏️ Write Functions
                        </h4>
                        <div id="writeFunctionsList" style="border: 1px solid var(--border-color); border-radius: 8px; overflow: hidden;">
                        </div>
                    </div>

                    <!-- Raw Call Fallback -->
                    <div style="margin-top: 20px; padding: 15px; border: 1px solid var(--border-color); border-radius: 8px; background: rgba(0,0,0,0.02);">
                        <h4 style="font-size: 0.95rem; margin-bottom: 10px; color: var(--text-secondary);">
                            🔧 Raw Call
                        </h4>
                        <div style="margin-bottom: 10px;">
                            <label style="display: block; font-size: 0.85rem; margin-bottom: 4px; color: var(--text-secondary);">Data (hex):</label>
                            <input type="text" id="rawCallData" placeholder="0x..."
                                   style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                        </div>
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 10px;">
                            <div>
                                <label style="display: block; font-size: 0.85rem; margin-bottom: 4px; color: var(--text-secondary);">Gas Limit:</label>
                                <input type="number" id="rawCallGasLimit" value="100000" min="1"
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <div>
                                <label style="display: block; font-size: 0.85rem; margin-bottom: 4px; color: var(--text-secondary);">Value:</label>
                                <input type="number" id="rawCallValue" value="0" min="0" step="0.00000001"
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                        </div>
                        <div style="display: flex; gap: 10px;">
                            <button onclick="executeRawRead()" style="padding: 8px 16px; background: var(--secondary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                                👁️ Read
                            </button>
                            <button onclick="executeRawSend()" style="padding: 8px 16px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                                📤 Send TX
                            </button>
                        </div>
                        <div id="rawCallResult" style="margin-top: 10px;"></div>
                    </div>

                    <!-- Global Interaction Result -->
                    <div id="interactionResult" style="margin-top: 15px;"></div>
                </div>
            </div>

            <!-- Tab Content: Storage -->
            <div id="tabContentStorage" class="contract-tab-content" style="display: none;">
                <div style="background: var(--bg-color); padding: 20px; border-radius: 8px;">
                    <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                        💾 Contract Storage
                    </h3>
                    <div id="storageLoading" style="text-align: center; padding: 20px; color: var(--text-secondary);">
                        ⏳ Lade Storage...
                    </div>
                    <div id="storageError" style="display: none; background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">
                        <div style="font-weight: bold; color: var(--danger-color); margin-bottom: 10px;">❌ Fehler beim Laden des Storage</div>
                        <div id="storageErrorMsg" style="font-size: 0.9rem; color: var(--text-secondary); margin-bottom: 10px;"></div>
                        <button onclick="loadContractStorage()" style="padding: 8px 16px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                            🔄 Erneut versuchen
                        </button>
                    </div>
                    <div id="storageEmpty" style="display: none; text-align: center; padding: 30px; color: var(--text-secondary);">
                        <div style="font-size: 2rem; margin-bottom: 10px;">📭</div>
                        <div style="font-size: 1rem;">Dieser Contract hat keine Storage-Einträge.</div>
                    </div>
                    <table id="storageTable" class="stats-table" style="display: none; width: 100%;">
                        <thead>
                            <tr style="background: rgba(37, 99, 235, 0.1);">
                                <th style="padding: 10px; text-align: left;">Slot (Key)</th>
                                <th style="padding: 10px; text-align: left;">Value</th>
                            </tr>
                        </thead>
                        <tbody id="storageTableBody">
                        </tbody>
                    </table>
                </div>
            </div>

            <!-- Tab Content: Transactions -->
            <div id="tabContentTransactions" class="contract-tab-content" style="display: none;">
                <div style="background: var(--bg-color); padding: 20px; border-radius: 8px;">
                    <h3 style="font-size: 1rem; margin-bottom: 15px; color: var(--primary-color);">
                        📜 Transaktionshistorie
                    </h3>
                    <div id="receiptsLoading" style="text-align: center; padding: 20px; color: var(--text-secondary);">
                        ⏳ Lade Transaktionshistorie...
                    </div>
                    <div id="receiptsError" style="display: none; background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">
                        <div style="font-weight: bold; color: var(--danger-color); margin-bottom: 10px;">❌ Fehler beim Laden der Transaktionshistorie</div>
                        <div id="receiptsErrorMsg" style="font-size: 0.9rem; color: var(--text-secondary); margin-bottom: 10px;"></div>
                        <button onclick="loadContractReceipts()" style="padding: 8px 16px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                            🔄 Erneut versuchen
                        </button>
                    </div>
                    <div id="receiptsEmpty" style="display: none; text-align: center; padding: 30px; color: var(--text-secondary);">
                        <div style="font-size: 2rem; margin-bottom: 10px;">📭</div>
                        <div style="font-size: 1rem;">Keine Transaktionen für diesen Contract gefunden.</div>
                    </div>
                    <table id="receiptsTable" class="stats-table" style="display: none; width: 100%;">
                        <thead>
                            <tr style="background: rgba(37, 99, 235, 0.1);">
                                <th style="padding: 10px; text-align: center;">Block</th>
                                <th style="padding: 10px; text-align: left;">TX Hash</th>
                                <th style="padding: 10px; text-align: left;">Von</th>
                                <th style="padding: 10px; text-align: center;">Gas</th>
                                <th style="padding: 10px; text-align: center;">Status</th>
                            </tr>
                        </thead>
                        <tbody id="receiptsTableBody">
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    </div>
</div>
)HTML";


// JavaScript for Contract Management functionality
static const std::string CONTRACT_MANAGEMENT_JS = R"JS(
// ===== Contract Management Functions =====

// Currently selected contract address
var selectedContractAddress = null;

// Auto-refresh timer ID
var contractRefreshTimer = null;

// Load all contracts belonging to the current wallet
async function loadMyContracts() {
    var loadingDiv = document.getElementById('contractListLoading');
    var errorDiv = document.getElementById('contractListError');
    var emptyDiv = document.getElementById('contractListEmpty');
    var tableEl = document.getElementById('contractListTable');
    var bodyEl = document.getElementById('contractListBody');

    // Show loading, hide others
    loadingDiv.style.display = 'block';
    errorDiv.style.display = 'none';
    emptyDiv.style.display = 'none';
    tableEl.style.display = 'none';

    try {
        var contracts = await window.dashboard.rpcCall('listmycontracts', []);

        loadingDiv.style.display = 'none';

        if (!contracts || contracts.length === 0) {
            emptyDiv.style.display = 'block';
            return;
        }

        // Populate table
        bodyEl.innerHTML = '';
        for (var i = 0; i < contracts.length; i++) {
            var c = contracts[i];
            var row = document.createElement('tr');
            row.style.cursor = 'pointer';
            row.style.transition = 'background 0.2s';
            row.onmouseover = function() { this.style.background = 'rgba(37, 99, 235, 0.05)'; };
            row.onmouseout = function() { this.style.background = ''; };

            var addrShort = c.address.length > 16 ? c.address.substring(0, 8) + '...' + c.address.substring(c.address.length - 6) : c.address;

            row.innerHTML =
                '<td style="padding: 10px; font-family: monospace; font-size: 0.85rem;" title="' + c.address + '">' + addrShort + '</td>' +
                '<td style="padding: 10px; text-align: center;"><span style="background: ' + (c.format === 'EVM' ? 'rgba(37, 99, 235, 0.1); color: var(--primary-color)' : 'rgba(16, 185, 129, 0.1); color: var(--secondary-color)') + '; padding: 2px 8px; border-radius: 4px; font-size: 0.8rem; font-weight: 600;">' + c.format + '</span></td>' +
                '<td style="padding: 10px; text-align: center;">' + c.deploymentHeight + '</td>';

            // Closure to capture address
            (function(addr) {
                row.onclick = function() { selectContract(addr); };
            })(c.address);

            bodyEl.appendChild(row);
        }

        tableEl.style.display = 'table';

    } catch (error) {
        loadingDiv.style.display = 'none';
        errorDiv.style.display = 'block';
        document.getElementById('contractListErrorMsg').textContent = error.message || 'Unbekannter Fehler';
    }
}

// Select a contract and load its details
async function selectContract(address) {
    selectedContractAddress = address;

    var detailView = document.getElementById('contractDetailView');
    var detailLoading = document.getElementById('contractDetailLoading');
    var detailTable = document.getElementById('contractDetailTable');
    var detailTitle = document.getElementById('contractDetailTitle');

    // Show detail view
    detailView.style.display = 'block';
    detailLoading.style.display = 'block';
    detailTable.style.display = 'none';

    var addrShort = address.length > 16 ? address.substring(0, 8) + '...' + address.substring(address.length - 6) : address;
    detailTitle.textContent = addrShort;

    // Reset to first tab
    switchContractTab('interact');

    try {
        var info = await window.dashboard.rpcCall('getcontractinfo', [address]);

        detailLoading.style.display = 'none';
        detailTable.style.display = 'table';

        document.getElementById('detailAddress').textContent = info.address || address;
        document.getElementById('detailDeployer').textContent = info.deployer || '--';
        document.getElementById('detailBlock').textContent = info.deploymentHeight || info.deploy_block || '--';
        document.getElementById('detailTx').textContent = info.deploymentTx || info.deploy_tx || '--';
        document.getElementById('detailSize').textContent = (info.codeSize || info.code_size || 0) + ' Bytes';
        document.getElementById('detailFormat').textContent = info.format || info.bytecode_format || '--';

        // Status with visual indicator (Req 3.2)
        var isCleanedUp = info.isCleanedUp || info.is_cleaned_up || false;
        if (isCleanedUp) {
            document.getElementById('detailStatus').innerHTML = '<span style="color: var(--warning-color);">⚠️ Bereinigt</span>';
        } else {
            document.getElementById('detailStatus').innerHTML = '<span style="color: var(--secondary-color);">✅ Aktiv</span>';
        }

        // Scroll to detail view
        detailView.scrollIntoView({ behavior: 'smooth', block: 'start' });

    } catch (error) {
        detailLoading.style.display = 'none';
        detailTable.style.display = 'none';
        detailView.innerHTML =
            '<div style="background: rgba(239, 68, 68, 0.1); padding: 15px; border-radius: 8px; border-left: 4px solid var(--danger-color);">' +
            '<div style="font-weight: bold; color: var(--danger-color);">❌ Fehler beim Laden der Contract-Details</div>' +
            '<div style="font-size: 0.9rem; margin-top: 5px;">' + (error.message || 'Unbekannter Fehler') + '</div>' +
            '</div>';
    }
}

// Switch between contract detail tabs
function switchContractTab(tabName) {
    // Hide all tab contents
    var tabs = document.querySelectorAll('.contract-tab-content');
    for (var i = 0; i < tabs.length; i++) {
        tabs[i].style.display = 'none';
    }

    // Reset all tab button styles
    var tabBtns = ['tabInteract', 'tabStorage', 'tabTransactions'];
    for (var i = 0; i < tabBtns.length; i++) {
        var btn = document.getElementById(tabBtns[i]);
        if (btn) {
            btn.style.fontWeight = 'normal';
            btn.style.borderColor = 'var(--border-color)';
            btn.style.color = 'inherit';
        }
    }

    // Show selected tab content and highlight button
    var contentId, btnId;
    if (tabName === 'interact') {
        contentId = 'tabContentInteract';
        btnId = 'tabInteract';
    } else if (tabName === 'storage') {
        contentId = 'tabContentStorage';
        btnId = 'tabStorage';
    } else if (tabName === 'transactions') {
        contentId = 'tabContentTransactions';
        btnId = 'tabTransactions';
    }

    if (contentId) {
        var contentEl = document.getElementById(contentId);
        if (contentEl) contentEl.style.display = 'block';
    }
    if (btnId) {
        var btnEl = document.getElementById(btnId);
        if (btnEl) {
            btnEl.style.fontWeight = 'bold';
            btnEl.style.borderColor = 'var(--primary-color)';
            btnEl.style.color = 'var(--primary-color)';
        }
    }

    // Load data for the selected tab
    if (tabName === 'storage') {
        loadContractStorage();
    } else if (tabName === 'transactions') {
        loadContractReceipts();
    }
}

// Start auto-refresh timer for contract list (every 30 seconds, Req 1.2)
function startContractAutoRefresh() {
    if (contractRefreshTimer) {
        clearInterval(contractRefreshTimer);
    }
    contractRefreshTimer = setInterval(function() {
        loadMyContracts();
    }, 30000);
}

// Stop auto-refresh timer
function stopContractAutoRefresh() {
    if (contractRefreshTimer) {
        clearInterval(contractRefreshTimer);
        contractRefreshTimer = null;
    }
}

// ===== Storage Browser Functions (Task 7.1) =====

// Load and display all storage entries for the selected contract
async function loadContractStorage() {
    var loadingDiv = document.getElementById('storageLoading');
    var errorDiv = document.getElementById('storageError');
    var emptyDiv = document.getElementById('storageEmpty');
    var tableEl = document.getElementById('storageTable');
    var bodyEl = document.getElementById('storageTableBody');

    // Show loading, hide others
    loadingDiv.style.display = 'block';
    errorDiv.style.display = 'none';
    emptyDiv.style.display = 'none';
    tableEl.style.display = 'none';

    try {
        if (!selectedContractAddress) {
            throw new Error('Kein Contract ausgewählt.');
        }

        var result = await window.dashboard.rpcCall('getcontractstorage', [selectedContractAddress]);

        loadingDiv.style.display = 'none';

        var entries = result && result.entries ? result.entries : [];

        if (entries.length === 0) {
            emptyDiv.style.display = 'block';
            return;
        }

        // Populate table with key-value pairs
        bodyEl.innerHTML = '';
        for (var i = 0; i < entries.length; i++) {
            var entry = entries[i];
            var row = document.createElement('tr');

            var keyDisplay = entry.key || '';
            var valueDisplay = entry.value || '';

            row.innerHTML =
                '<td style="padding: 10px; font-family: monospace; font-size: 0.85rem; word-break: break-all;" title="' + escapeHtml(keyDisplay) + '">Slot ' + escapeHtml(keyDisplay) + '</td>' +
                '<td style="padding: 10px; font-family: monospace; font-size: 0.85rem; word-break: break-all;" title="' + escapeHtml(valueDisplay) + '">' + escapeHtml(valueDisplay) + '</td>';

            bodyEl.appendChild(row);
        }

        tableEl.style.display = 'table';

    } catch (error) {
        loadingDiv.style.display = 'none';
        errorDiv.style.display = 'block';
        document.getElementById('storageErrorMsg').textContent = error.message || 'Unbekannter Fehler';
    }
}

// ===== Transaction History Functions (Task 7.2) =====

// Load and display all transaction receipts for the selected contract
async function loadContractReceipts() {
    var loadingDiv = document.getElementById('receiptsLoading');
    var errorDiv = document.getElementById('receiptsError');
    var emptyDiv = document.getElementById('receiptsEmpty');
    var tableEl = document.getElementById('receiptsTable');
    var bodyEl = document.getElementById('receiptsTableBody');

    // Show loading, hide others
    loadingDiv.style.display = 'block';
    errorDiv.style.display = 'none';
    emptyDiv.style.display = 'none';
    tableEl.style.display = 'none';

    try {
        if (!selectedContractAddress) {
            throw new Error('Kein Contract ausgewählt.');
        }

        var receipts = await window.dashboard.rpcCall('getcontractreceipts', [selectedContractAddress]);

        loadingDiv.style.display = 'none';

        if (!receipts || receipts.length === 0) {
            emptyDiv.style.display = 'block';
            return;
        }

        // Populate table with receipt entries
        bodyEl.innerHTML = '';
        for (var i = 0; i < receipts.length; i++) {
            var r = receipts[i];
            var row = document.createElement('tr');

            var txHashShort = r.txHash ? (r.txHash.length > 16 ? r.txHash.substring(0, 8) + '...' + r.txHash.substring(r.txHash.length - 6) : r.txHash) : '--';
            var fromShort = r.from ? (r.from.length > 16 ? r.from.substring(0, 8) + '...' + r.from.substring(r.from.length - 6) : r.from) : '--';
            var gasUsed = r.gasUsed !== undefined ? r.gasUsed : '--';
            var blockNumber = r.blockNumber !== undefined ? r.blockNumber : '--';

            // Status: 1 = success, 0 = failure
            var statusHtml;
            if (r.status === 1) {
                statusHtml = '<span style="color: var(--secondary-color); font-weight: 600;">✅ OK</span>';
            } else {
                statusHtml = '<span style="color: var(--danger-color); font-weight: 600;">❌ Fail</span>';
            }

            var rowHtml =
                '<td style="padding: 10px; text-align: center;">' + escapeHtml(String(blockNumber)) + '</td>' +
                '<td style="padding: 10px; font-family: monospace; font-size: 0.85rem;" title="' + escapeHtml(r.txHash || '') + '">' + escapeHtml(txHashShort) + '</td>' +
                '<td style="padding: 10px; font-family: monospace; font-size: 0.85rem;" title="' + escapeHtml(r.from || '') + '">' + escapeHtml(fromShort) + '</td>' +
                '<td style="padding: 10px; text-align: center;">' + escapeHtml(String(gasUsed)) + '</td>' +
                '<td style="padding: 10px; text-align: center;">' + statusHtml + '</td>';

            row.innerHTML = rowHtml;
            bodyEl.appendChild(row);

            // Show revert reason for failed receipts
            if (r.status === 0 && r.revertReason) {
                var revertRow = document.createElement('tr');
                revertRow.innerHTML =
                    '<td colspan="5" style="padding: 6px 10px 12px 10px; background: rgba(239, 68, 68, 0.05); border-left: 3px solid var(--danger-color);">' +
                    '<span style="font-size: 0.8rem; color: var(--danger-color);">⚠️ Revert: ' + escapeHtml(r.revertReason) + '</span>' +
                    '</td>';
                bodyEl.appendChild(revertRow);
            }
        }

        tableEl.style.display = 'table';

    } catch (error) {
        loadingDiv.style.display = 'none';
        errorDiv.style.display = 'block';
        document.getElementById('receiptsErrorMsg').textContent = error.message || 'Unbekannter Fehler';
    }
}

// ===== Contract Interaction Functions (Task 6.1) =====

// Parsed ABI data for the currently selected contract
var parsedContractABI = null;

// Parse the ABI JSON from the input field and categorize functions
function parseInteractABI() {
    var abiInput = document.getElementById('contractAbiInput');
    var errorDiv = document.getElementById('contractAbiError');
    var readSection = document.getElementById('readFunctionsSection');
    var writeSection = document.getElementById('writeFunctionsSection');
    var readList = document.getElementById('readFunctionsList');
    var writeList = document.getElementById('writeFunctionsList');

    errorDiv.style.display = 'none';
    readSection.style.display = 'none';
    writeSection.style.display = 'none';
    readList.innerHTML = '';
    writeList.innerHTML = '';
    parsedContractABI = null;

    try {
        var abi = JSON.parse(abiInput.value);
        if (!Array.isArray(abi)) {
            throw new Error('ABI muss ein JSON-Array sein.');
        }
        parsedContractABI = abi;

        var readFunctions = [];
        var writeFunctions = [];

        for (var i = 0; i < abi.length; i++) {
            var entry = abi[i];
            if (entry.type !== 'function') continue;
            var mutability = entry.stateMutability || '';
            if (mutability === 'view' || mutability === 'pure') {
                readFunctions.push(entry);
            } else {
                writeFunctions.push(entry);
            }
        }

        // Render read functions
        if (readFunctions.length > 0) {
            readSection.style.display = 'block';
            for (var i = 0; i < readFunctions.length; i++) {
                readList.appendChild(buildReadFunctionRow(readFunctions[i], i));
            }
        }

        // Render write functions
        if (writeFunctions.length > 0) {
            writeSection.style.display = 'block';
            for (var i = 0; i < writeFunctions.length; i++) {
                writeList.appendChild(buildWriteFunctionRow(writeFunctions[i], i));
            }
        }

        if (readFunctions.length === 0 && writeFunctions.length === 0) {
            errorDiv.textContent = 'Keine Funktionen in der ABI gefunden.';
            errorDiv.style.display = 'block';
        }

    } catch (e) {
        errorDiv.textContent = 'ABI-Parse-Fehler: ' + e.message;
        errorDiv.style.display = 'block';
    }
}

// Build the function signature string for display
function buildFunctionSignature(func) {
    var params = (func.inputs || []).map(function(inp) { return inp.type; }).join(', ');
    var outputs = (func.outputs || []).map(function(out) { return out.type; }).join(', ');
    var sig = func.name + '(' + params + ')';
    if (outputs) {
        sig += ' → ' + outputs;
    }
    return sig;
}

// Build a UI row for a read (view/pure) function
function buildReadFunctionRow(func, index) {
    var row = document.createElement('div');
    row.style.cssText = 'padding: 12px 15px; border-bottom: 1px solid var(--border-color);';

    var sig = buildFunctionSignature(func);
    var hasInputs = func.inputs && func.inputs.length > 0;

    var html = '<div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: ' + (hasInputs ? '10' : '0') + 'px;">';
    html += '<code style="font-size: 0.85rem; color: var(--secondary-color);">' + escapeHtml(sig) + '</code>';
    html += '<button onclick="executeReadFunction(' + index + ')" style="padding: 6px 14px; background: var(--secondary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.8rem; white-space: nowrap;">Ausführen</button>';
    html += '</div>';

    // Parameter input fields
    if (hasInputs) {
        for (var j = 0; j < func.inputs.length; j++) {
            var inp = func.inputs[j];
            var label = inp.name ? inp.name + ' (' + inp.type + ')' : inp.type;
            html += '<div style="margin-bottom: 6px;">';
            html += '<label style="display: block; font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 2px;">' + escapeHtml(label) + '</label>';
            html += '<input type="text" id="readFunc_' + index + '_param_' + j + '" placeholder="' + escapeHtml(inp.type) + '"';
            html += ' style="width: 100%; padding: 6px 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">';
            html += '</div>';
        }
    }

    // Result display area
    html += '<div id="readFunc_' + index + '_result" style="margin-top: 8px;"></div>';

    row.innerHTML = html;
    return row;
}

// Build a UI row for a write (nonpayable/payable) function
function buildWriteFunctionRow(func, index) {
    var row = document.createElement('div');
    row.style.cssText = 'padding: 12px 15px; border-bottom: 1px solid var(--border-color);';

    var sig = buildFunctionSignature(func);
    var hasInputs = func.inputs && func.inputs.length > 0;

    var html = '<div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">';
    html += '<code style="font-size: 0.85rem; color: var(--primary-color);">' + escapeHtml(sig) + '</code>';
    html += '<button onclick="executeWriteFunction(' + index + ')" style="padding: 6px 14px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.8rem; white-space: nowrap;">Senden</button>';
    html += '</div>';

    // Parameter input fields
    if (hasInputs) {
        for (var j = 0; j < func.inputs.length; j++) {
            var inp = func.inputs[j];
            var label = inp.name ? inp.name + ' (' + inp.type + ')' : inp.type;
            html += '<div style="margin-bottom: 6px;">';
            html += '<label style="display: block; font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 2px;">' + escapeHtml(label) + '</label>';
            html += '<input type="text" id="writeFunc_' + index + '_param_' + j + '" placeholder="' + escapeHtml(inp.type) + '"';
            html += ' style="width: 100%; padding: 6px 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">';
            html += '</div>';
        }
    }

    // Gas limit field
    html += '<div style="margin-top: 6px;">';
    html += '<label style="display: block; font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 2px;">Gas Limit</label>';
    html += '<input type="number" id="writeFunc_' + index + '_gas" value="100000" min="1"';
    html += ' style="width: 150px; padding: 6px 8px; border: 1px solid var(--border-color); border-radius: 4px; font-size: 0.8rem;">';
    html += '</div>';

    // Result display area
    html += '<div id="writeFunc_' + index + '_result" style="margin-top: 8px;"></div>';

    row.innerHTML = html;
    return row;
}

// Collect parameter values from input fields for a function
function collectFunctionParams(prefix, func) {
    var values = [];
    for (var j = 0; j < (func.inputs || []).length; j++) {
        var el = document.getElementById(prefix + '_param_' + j);
        values.push(el ? el.value : '');
    }
    return values;
}

// Get the list of read functions from the parsed ABI
function getReadFunctions() {
    if (!parsedContractABI) return [];
    var result = [];
    for (var i = 0; i < parsedContractABI.length; i++) {
        var entry = parsedContractABI[i];
        if (entry.type !== 'function') continue;
        var m = entry.stateMutability || '';
        if (m === 'view' || m === 'pure') {
            result.push(entry);
        }
    }
    return result;
}

// Get the list of write functions from the parsed ABI
function getWriteFunctions() {
    if (!parsedContractABI) return [];
    var result = [];
    for (var i = 0; i < parsedContractABI.length; i++) {
        var entry = parsedContractABI[i];
        if (entry.type !== 'function') continue;
        var m = entry.stateMutability || '';
        if (m !== 'view' && m !== 'pure') {
            result.push(entry);
        }
    }
    return result;
}

// Display a result message in a target div
function showFunctionResult(targetId, success, message) {
    var el = document.getElementById(targetId);
    if (!el) return;
    if (success) {
        el.innerHTML = '<div style="background: rgba(16, 185, 129, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--secondary-color); font-size: 0.85rem; word-break: break-all;">' +
            '<span style="color: var(--secondary-color); font-weight: bold;">✅ Ergebnis:</span> ' + escapeHtml(message) +
            '</div>';
    } else {
        el.innerHTML = '<div style="background: rgba(239, 68, 68, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--danger-color); font-size: 0.85rem; word-break: break-all;">' +
            '<span style="color: var(--danger-color); font-weight: bold;">❌ Fehler:</span> ' + escapeHtml(message) +
            '</div>';
    }
}

// Execute a read (view/pure) function
async function executeReadFunction(index) {
    var readFuncs = getReadFunctions();
    if (index >= readFuncs.length) return;
    var func = readFuncs[index];
    var resultId = 'readFunc_' + index + '_result';
    var resultEl = document.getElementById(resultId);
    if (resultEl) {
        resultEl.innerHTML = '<span style="color: var(--text-secondary); font-size: 0.85rem;">⏳ Wird ausgeführt...</span>';
    }

    try {
        if (!selectedContractAddress) {
            throw new Error('Kein Contract ausgewählt.');
        }

        var paramValues = collectFunctionParams('readFunc_' + index, func);

        // Encode the function call using the existing helper
        var callData = encodeFunctionCall(func, paramValues);

        // Execute read-only call via callcontract RPC
        var result = await window.dashboard.rpcCall('callcontract', [selectedContractAddress, callData, 100000]);

        // Extract the return hex data
        var returnData = '';
        if (typeof result === 'string') {
            returnData = result;
        } else if (result && result.executionResult && result.executionResult.output) {
            returnData = result.executionResult.output;
        } else if (result && result.output) {
            returnData = result.output;
        } else if (result && result.return) {
            returnData = result.return;
        }

        // Decode the result using the existing helper
        var decoded = null;
        if (returnData && returnData !== '0x' && func.outputs && func.outputs.length > 0) {
            decoded = decodeABIResult(func.outputs, returnData);
        }

        var displayMsg = returnData || '(leer)';
        if (decoded !== null && decoded !== undefined) {
            displayMsg = returnData + ' (decoded: ' + decoded + ')';
        }

        showFunctionResult(resultId, true, displayMsg);

    } catch (e) {
        var errMsg = extractRevertReason(e) || e.message || 'Unbekannter Fehler';
        showFunctionResult(resultId, false, errMsg);
    }
}

// Execute a write (state-changing) function
async function executeWriteFunction(index) {
    var writeFuncs = getWriteFunctions();
    if (index >= writeFuncs.length) return;
    var func = writeFuncs[index];
    var resultId = 'writeFunc_' + index + '_result';
    var resultEl = document.getElementById(resultId);

    // Check wallet status before attempting write (Req 7.1)
    if (currentWalletStatus === 'locked') {
        showFunctionResult(resultId, false, 'Wallet ist gesperrt. Bitte entsperren Sie die Wallet zuerst.');
        return;
    }
    if (currentWalletStatus === 'missing') {
        showFunctionResult(resultId, false, 'Wallet nicht verfügbar. Eine Wallet wird benötigt.');
        return;
    }

    if (resultEl) {
        resultEl.innerHTML = '<span style="color: var(--text-secondary); font-size: 0.85rem;">⏳ Transaktion wird gesendet...</span>';
    }

    try {
        if (!selectedContractAddress) {
            throw new Error('Kein Contract ausgewählt.');
        }

        var paramValues = collectFunctionParams('writeFunc_' + index, func);

        // Encode the function call
        var callData = encodeFunctionCall(func, paramValues);

        // Get gas limit from the function-specific field
        var gasEl = document.getElementById('writeFunc_' + index + '_gas');
        var gasLimit = gasEl ? parseInt(gasEl.value) || 100000 : 100000;

        // Send transaction via callcontract RPC (with value=0 for non-payable)
        var value = 0;
        var result = await window.dashboard.rpcCall('callcontract', [selectedContractAddress, callData, gasLimit, value]);

        // Extract transaction hash and gas info
        var txHash = '';
        var gasUsed = '';
        if (typeof result === 'string') {
            txHash = result;
        } else if (result && result.txid) {
            txHash = result.txid;
            gasUsed = result.gasUsed || '';
        } else if (result && result.transactionHash) {
            txHash = result.transactionHash;
            gasUsed = result.gasUsed || '';
        } else if (result && result.executionResult) {
            txHash = result.executionResult.txid || result.executionResult.transactionHash || '';
            gasUsed = result.executionResult.gasUsed || '';
        }

        var displayMsg = '';
        if (txHash) {
            displayMsg = 'TX: ' + txHash;
        } else {
            displayMsg = JSON.stringify(result);
        }
        if (gasUsed) {
            displayMsg += ' | Gas: ' + gasUsed;
        }

        showFunctionResult(resultId, true, displayMsg);

    } catch (e) {
        var errMsg = extractRevertReason(e) || e.message || 'Unbekannter Fehler';
        showFunctionResult(resultId, false, errMsg);
    }
}

// Execute a raw read call (no ABI)
async function executeRawRead() {
    var resultDiv = document.getElementById('rawCallResult');
    resultDiv.innerHTML = '<span style="color: var(--text-secondary); font-size: 0.85rem;">⏳ Wird ausgeführt...</span>';

    try {
        if (!selectedContractAddress) {
            throw new Error('Kein Contract ausgewählt.');
        }

        var data = document.getElementById('rawCallData').value || '';
        var gasLimit = parseInt(document.getElementById('rawCallGasLimit').value) || 100000;

        var result = await window.dashboard.rpcCall('callcontract', [selectedContractAddress, data, gasLimit]);

        var returnData = '';
        if (typeof result === 'string') {
            returnData = result;
        } else if (result && result.executionResult && result.executionResult.output) {
            returnData = result.executionResult.output;
        } else if (result && result.output) {
            returnData = result.output;
        } else if (result && result.return) {
            returnData = result.return;
        } else {
            returnData = JSON.stringify(result);
        }

        resultDiv.innerHTML = '<div style="background: rgba(16, 185, 129, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--secondary-color); font-size: 0.85rem; word-break: break-all;">' +
            '<span style="color: var(--secondary-color); font-weight: bold;">✅ Ergebnis:</span> ' + escapeHtml(returnData || '(leer)') +
            '</div>';

    } catch (e) {
        var errMsg = extractRevertReason(e) || e.message || 'Unbekannter Fehler';
        resultDiv.innerHTML = '<div style="background: rgba(239, 68, 68, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--danger-color); font-size: 0.85rem; word-break: break-all;">' +
            '<span style="color: var(--danger-color); font-weight: bold;">❌ Fehler:</span> ' + escapeHtml(errMsg) +
            '</div>';
    }
}

// Execute a raw send transaction (no ABI)
async function executeRawSend() {
    var resultDiv = document.getElementById('rawCallResult');

    // Check wallet status before attempting send (Req 7.1)
    if (currentWalletStatus === 'locked') {
        resultDiv.innerHTML = '<div style="background: rgba(245, 158, 11, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--warning-color); font-size: 0.85rem;">' +
            '<span style="color: var(--warning-color); font-weight: bold;">🔒 Wallet gesperrt:</span> Bitte entsperren Sie die Wallet zuerst.' +
            '</div>';
        return;
    }
    if (currentWalletStatus === 'missing') {
        resultDiv.innerHTML = '<div style="background: rgba(239, 68, 68, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--danger-color); font-size: 0.85rem;">' +
            '<span style="color: var(--danger-color); font-weight: bold;">⚠️ Wallet nicht verfügbar:</span> Eine Wallet wird benötigt.' +
            '</div>';
        return;
    }

    resultDiv.innerHTML = '<span style="color: var(--text-secondary); font-size: 0.85rem;">⏳ Transaktion wird gesendet...</span>';

    try {
        if (!selectedContractAddress) {
            throw new Error('Kein Contract ausgewählt.');
        }

        var data = document.getElementById('rawCallData').value || '';
        var gasLimit = parseInt(document.getElementById('rawCallGasLimit').value) || 100000;
        var value = parseFloat(document.getElementById('rawCallValue').value) || 0;

        var result = await window.dashboard.rpcCall('callcontract', [selectedContractAddress, data, gasLimit, value]);

        var txHash = '';
        if (typeof result === 'string') {
            txHash = result;
        } else if (result && result.txid) {
            txHash = result.txid;
        } else if (result && result.transactionHash) {
            txHash = result.transactionHash;
        } else {
            txHash = JSON.stringify(result);
        }

        resultDiv.innerHTML = '<div style="background: rgba(16, 185, 129, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--secondary-color); font-size: 0.85rem; word-break: break-all;">' +
            '<span style="color: var(--secondary-color); font-weight: bold;">✅ TX gesendet:</span> ' + escapeHtml(txHash) +
            '</div>';

    } catch (e) {
        var errMsg = extractRevertReason(e) || e.message || 'Unbekannter Fehler';
        resultDiv.innerHTML = '<div style="background: rgba(239, 68, 68, 0.1); padding: 10px; border-radius: 6px; border-left: 4px solid var(--danger-color); font-size: 0.85rem; word-break: break-all;">' +
            '<span style="color: var(--danger-color); font-weight: bold;">❌ Fehler:</span> ' + escapeHtml(errMsg) +
            '</div>';
    }
}

// Extract revert reason from an error object
function extractRevertReason(error) {
    if (!error) return null;
    // Check for revert reason in various formats
    if (error.data && error.data.message) return error.data.message;
    if (error.revertReason) return 'Revert: ' + error.revertReason;
    if (error.reason) return 'Revert: ' + error.reason;
    var msg = error.message || '';
    // Try to extract revert reason from error message string
    var revertMatch = msg.match(/revert(?:ed)?[:\s]+(.+)/i);
    if (revertMatch) return 'Revert: ' + revertMatch[1];
    return null;
}

// HTML-escape helper to prevent XSS in displayed values
function escapeHtml(str) {
    if (typeof str !== 'string') str = String(str);
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#039;');
}

// ===== Wallet Status Check Functions (Task 8.1, Req 7.1, 7.2, 7.3) =====

// Current wallet status: 'ok', 'locked', 'missing', 'rpc_error'
var currentWalletStatus = 'ok';

// Wallet status check timer
var walletStatusTimer = null;

// Check wallet status by calling getwalletinfo RPC directly
// Uses raw fetch to preserve error codes (rpcCall loses them)
async function checkWalletStatus() {
    var banner = document.getElementById('walletStatusBanner');
    if (!banner) return;

    try {
        var rpcUrl = window.dashboard ? window.dashboard.rpcUrl : 'http://127.0.0.1:19332/';
        var response = await fetch(rpcUrl, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                jsonrpc: '2.0',
                id: 'wallet-status-' + Date.now(),
                method: 'getwalletinfo',
                params: []
            })
        });

        if (!response.ok) {
            // HTTP error - RPC not reachable (Req 7.2)
            // Note: The existing loadMyContracts() error handling already covers
            // RPC unreachable with a retry button in the contract list.
            // We only update the wallet status tracking here.
            setWalletStatus('rpc_error', 'RPC-Schnittstelle nicht erreichbar.');
            return;
        }

        var data = await response.json();

        if (data.error) {
            var errorCode = data.error.code || 0;
            var errorMsg = data.error.message || 'Unbekannter Fehler';

            if (errorCode === -13) {
                // Wallet locked (RPC_WALLET_UNLOCK_NEEDED) - Req 7.1
                setWalletStatus('locked', 'Wallet ist gesperrt. Bitte entsperren Sie die Wallet, um Contract-Interaktionen durchzuführen.');
            } else if (errorCode === -4 || errorCode === -18 || errorCode === -19) {
                // Wallet not available / not found / not specified (Req 7.3)
                // -4 = RPC_WALLET_ERROR, -18 = RPC_WALLET_NOT_FOUND, -19 = RPC_WALLET_NOT_SPECIFIED
                setWalletStatus('missing', 'Wallet nicht verfügbar. Eine Wallet wird benötigt, um Ihre Contracts anzuzeigen.');
            } else {
                // Other RPC error - treat as OK for wallet status purposes
                // (e.g., method not found on older nodes)
                setWalletStatus('ok', '');
            }
        } else {
            // Success - wallet is available and unlocked
            setWalletStatus('ok', '');
        }

    } catch (e) {
        // Network/fetch error - RPC not reachable
        setWalletStatus('rpc_error', 'RPC-Schnittstelle nicht erreichbar.');
    }
}

// Update the wallet status banner and button states
function setWalletStatus(status, message) {
    var banner = document.getElementById('walletStatusBanner');
    if (!banner) return;

    var previousStatus = currentWalletStatus;
    currentWalletStatus = status;

    if (status === 'ok') {
        banner.style.display = 'none';
        banner.innerHTML = '';
        updateWalletDependentButtons(true);
    } else if (status === 'locked') {
        banner.style.display = 'block';
        banner.style.background = 'rgba(245, 158, 11, 0.1)';
        banner.style.borderLeft = '4px solid var(--warning-color)';
        banner.innerHTML =
            '<div style="display: flex; align-items: center; gap: 10px;">' +
            '<span style="font-size: 1.2rem;">🔒</span>' +
            '<div>' +
            '<div style="font-weight: bold; color: var(--warning-color); margin-bottom: 4px;">Wallet gesperrt</div>' +
            '<div style="color: var(--text-secondary); font-size: 0.85rem;">' + escapeHtml(message) + '</div>' +
            '</div>' +
            '</div>';
        updateWalletDependentButtons(false);
    } else if (status === 'missing') {
        banner.style.display = 'block';
        banner.style.background = 'rgba(239, 68, 68, 0.1)';
        banner.style.borderLeft = '4px solid var(--danger-color)';
        banner.innerHTML =
            '<div style="display: flex; align-items: center; gap: 10px;">' +
            '<span style="font-size: 1.2rem;">⚠️</span>' +
            '<div>' +
            '<div style="font-weight: bold; color: var(--danger-color); margin-bottom: 4px;">Wallet nicht verfügbar</div>' +
            '<div style="color: var(--text-secondary); font-size: 0.85rem;">' + escapeHtml(message) + '</div>' +
            '</div>' +
            '</div>';
        updateWalletDependentButtons(false);
    } else if (status === 'rpc_error') {
        banner.style.display = 'block';
        banner.style.background = 'rgba(239, 68, 68, 0.1)';
        banner.style.borderLeft = '4px solid var(--danger-color)';
        banner.innerHTML =
            '<div style="display: flex; align-items: center; gap: 10px;">' +
            '<span style="font-size: 1.2rem;">🔌</span>' +
            '<div style="flex: 1;">' +
            '<div style="font-weight: bold; color: var(--danger-color); margin-bottom: 4px;">Verbindungsfehler</div>' +
            '<div style="color: var(--text-secondary); font-size: 0.85rem;">' + escapeHtml(message) + '</div>' +
            '</div>' +
            '<button onclick="checkWalletStatus(); loadMyContracts();" style="padding: 6px 14px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.8rem; white-space: nowrap;">🔄 Retry</button>' +
            '</div>';
        updateWalletDependentButtons(false);
    }
}

// Enable or disable wallet-dependent interaction buttons (Req 7.1)
// When wallet is locked/missing: disable write function buttons and raw send button
function updateWalletDependentButtons(enabled) {
    // Disable/enable all write function "Senden" buttons
    var writeBtns = document.querySelectorAll('#writeFunctionsList button');
    for (var i = 0; i < writeBtns.length; i++) {
        writeBtns[i].disabled = !enabled;
        writeBtns[i].style.opacity = enabled ? '1' : '0.5';
        writeBtns[i].style.cursor = enabled ? 'pointer' : 'not-allowed';
    }

    // Disable/enable the raw "Send TX" button
    var rawSendBtns = document.querySelectorAll('button[onclick="executeRawSend()"]');
    for (var i = 0; i < rawSendBtns.length; i++) {
        rawSendBtns[i].disabled = !enabled;
        rawSendBtns[i].style.opacity = enabled ? '1' : '0.5';
        rawSendBtns[i].style.cursor = enabled ? 'pointer' : 'not-allowed';
    }
}

// Start periodic wallet status check (every 30 seconds, aligned with contract refresh)
function startWalletStatusCheck() {
    if (walletStatusTimer) {
        clearInterval(walletStatusTimer);
    }
    walletStatusTimer = setInterval(function() {
        checkWalletStatus();
    }, 30000);
}

// Stop periodic wallet status check
function stopWalletStatusCheck() {
    if (walletStatusTimer) {
        clearInterval(walletStatusTimer);
        walletStatusTimer = null;
    }
}

// Initialize contract management on page load
document.addEventListener('DOMContentLoaded', function() {
    // Delay initial load slightly to let dashboard initialize
    setTimeout(function() {
        checkWalletStatus();
        loadMyContracts();
        startContractAutoRefresh();
        startWalletStatusCheck();
    }, 1500);
});
)JS";

} // namespace CVMDashboardContracts

#endif // CASCOIN_HTTPSERVER_CVMDASHBOARD_CONTRACTS_H
