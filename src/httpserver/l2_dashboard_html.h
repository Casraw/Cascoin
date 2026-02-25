// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_L2_DASHBOARD_HTML_H
#define CASCOIN_HTTPSERVER_L2_DASHBOARD_HTML_H

/**
 * @file l2_dashboard_html.h
 * @brief Embedded HTML/CSS/JS for L2 Web Dashboard
 * 
 * This file contains the complete embedded dashboard frontend including:
 * - L2 Chain Status (Blocks, TPS, Gas)
 * - Sequencer Status (Leader, Uptime, Reputation)
 * - Bridge Status (TVL, Pending Withdrawals)
 * - Recent Transactions and Blocks
 * 
 * Requirements: 33.8, 39.1
 */

#include <string>

namespace l2 {
namespace L2DashboardHTML {

/**
 * @brief Complete L2 Dashboard HTML page
 * 
 * Single-page application with embedded CSS and JavaScript.
 * Auto-refreshes data every 5 seconds via API calls.
 */
inline const std::string INDEX_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cascoin L2 Dashboard</title>
    <style>
        :root {
            --primary-color: #2563eb;
            --primary-dark: #1d4ed8;
            --success-color: #10b981;
            --warning-color: #f59e0b;
            --danger-color: #ef4444;
            --bg-color: #0f172a;
            --card-bg: #1e293b;
            --text-primary: #f1f5f9;
            --text-secondary: #94a3b8;
            --border-color: #334155;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-primary);
            line-height: 1.6;
        }
        
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 20px;
        }
        
        header {
            background: linear-gradient(135deg, var(--primary-color), var(--primary-dark));
            padding: 20px 0;
            margin-bottom: 30px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
        }
        
        header .container {
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .logo {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        
        .logo h1 {
            font-size: 1.8rem;
            font-weight: 700;
        }
        
        .logo span {
            background: rgba(255,255,255,0.2);
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.85rem;
        }
        
        .status-indicator {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 8px 16px;
            background: rgba(255,255,255,0.1);
            border-radius: 8px;
        }
        
        .status-dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: var(--success-color);
            animation: pulse 2s infinite;
        }
        
        .status-dot.warning { background: var(--warning-color); }
        .status-dot.danger { background: var(--danger-color); }
        
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .card {
            background: var(--card-bg);
            border-radius: 12px;
            padding: 24px;
            border: 1px solid var(--border-color);
            transition: transform 0.2s, box-shadow 0.2s;
        }
        
        .card:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 25px rgba(0, 0, 0, 0.3);
        }
        
        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
        }
        
        .card-title {
            font-size: 1.1rem;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        
        .card-icon {
            width: 40px;
            height: 40px;
            border-radius: 10px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.2rem;
        }
        
        .card-icon.blue { background: rgba(37, 99, 235, 0.2); color: var(--primary-color); }
        .card-icon.green { background: rgba(16, 185, 129, 0.2); color: var(--success-color); }
        .card-icon.yellow { background: rgba(245, 158, 11, 0.2); color: var(--warning-color); }
        .card-icon.red { background: rgba(239, 68, 68, 0.2); color: var(--danger-color); }
        
        .stat-value {
            font-size: 2.2rem;
            font-weight: 700;
            margin-bottom: 4px;
        }
        
        .stat-label {
            color: var(--text-secondary);
            font-size: 0.9rem;
        }
        
        .stat-change {
            display: inline-flex;
            align-items: center;
            gap: 4px;
            padding: 2px 8px;
            border-radius: 4px;
            font-size: 0.8rem;
            margin-left: 8px;
        }
        
        .stat-change.positive { background: rgba(16, 185, 129, 0.2); color: var(--success-color); }
        .stat-change.negative { background: rgba(239, 68, 68, 0.2); color: var(--danger-color); }
        
        .table-card {
            grid-column: span 2;
        }
        
        @media (max-width: 768px) {
            .table-card { grid-column: span 1; }
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
        }
        
        th, td {
            padding: 12px 16px;
            text-align: left;
            border-bottom: 1px solid var(--border-color);
        }
        
        th {
            color: var(--text-secondary);
            font-weight: 600;
            font-size: 0.85rem;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        
        tr:hover {
            background: rgba(255, 255, 255, 0.02);
        }
        
        .hash {
            font-family: 'Monaco', 'Menlo', monospace;
            font-size: 0.85rem;
            color: var(--primary-color);
        }
        
        .badge {
            display: inline-block;
            padding: 4px 10px;
            border-radius: 20px;
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
        }
        
        .badge.success { background: rgba(16, 185, 129, 0.2); color: var(--success-color); }
        .badge.warning { background: rgba(245, 158, 11, 0.2); color: var(--warning-color); }
        .badge.danger { background: rgba(239, 68, 68, 0.2); color: var(--danger-color); }
        .badge.info { background: rgba(37, 99, 235, 0.2); color: var(--primary-color); }
        
        .progress-bar {
            height: 8px;
            background: var(--border-color);
            border-radius: 4px;
            overflow: hidden;
            margin-top: 8px;
        }
        
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, var(--primary-color), var(--success-color));
            border-radius: 4px;
            transition: width 0.5s ease;
        }
        
        .sequencer-list {
            max-height: 400px;
            overflow-y: auto;
        }
        
        .sequencer-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 12px 0;
            border-bottom: 1px solid var(--border-color);
        }
        
        .sequencer-item:last-child {
            border-bottom: none;
        }
        
        .sequencer-info {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        
        .sequencer-avatar {
            width: 40px;
            height: 40px;
            border-radius: 50%;
            background: linear-gradient(135deg, var(--primary-color), var(--success-color));
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: 700;
        }
        
        .sequencer-details h4 {
            font-size: 0.95rem;
            margin-bottom: 2px;
        }
        
        .sequencer-details span {
            font-size: 0.8rem;
            color: var(--text-secondary);
        }
        
        .sequencer-stats {
            text-align: right;
        }
        
        .sequencer-stats .uptime {
            font-size: 1.1rem;
            font-weight: 600;
            color: var(--success-color);
        }
        
        .sequencer-stats .blocks {
            font-size: 0.8rem;
            color: var(--text-secondary);
        }
        
        .alert-banner {
            background: rgba(239, 68, 68, 0.1);
            border: 1px solid var(--danger-color);
            border-radius: 8px;
            padding: 16px 20px;
            margin-bottom: 20px;
            display: none;
        }
        
        .alert-banner.show {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        
        .alert-banner .icon {
            font-size: 1.5rem;
        }
        
        .refresh-indicator {
            font-size: 0.8rem;
            color: var(--text-secondary);
        }
        
        footer {
            text-align: center;
            padding: 30px 0;
            color: var(--text-secondary);
            font-size: 0.9rem;
            border-top: 1px solid var(--border-color);
            margin-top: 40px;
        }
        
        .loading {
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 40px;
        }
        
        .spinner {
            width: 40px;
            height: 40px;
            border: 3px solid var(--border-color);
            border-top-color: var(--primary-color);
            border-radius: 50%;
            animation: spin 1s linear infinite;
        }
        
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <header>
        <div class="container">
            <div class="logo">
                <h1>⚡ Cascoin L2</h1>
                <span>Dashboard</span>
            </div>
            <div class="status-indicator">
                <div class="status-dot" id="healthDot"></div>
                <span id="healthStatus">Connecting...</span>
            </div>
        </div>
    </header>
    
    <div class="container">
        <div class="alert-banner" id="alertBanner">
            <span class="icon">⚠️</span>
            <div>
                <strong>Security Alert</strong>
                <p id="alertMessage"></p>
            </div>
        </div>
        
        <!-- Stats Grid -->
        <div class="grid">
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Block Height</span>
                    <div class="card-icon blue">📦</div>
                </div>
                <div class="stat-value" id="blockHeight">-</div>
                <div class="stat-label">Latest L2 Block</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Transactions</span>
                    <div class="card-icon green">📊</div>
                </div>
                <div class="stat-value" id="tps">0.00</div>
                <div class="stat-label">TPS (Transactions per Second)</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Gas Utilization</span>
                    <div class="card-icon yellow">⛽</div>
                </div>
                <div class="stat-value" id="gasUtil">0%</div>
                <div class="progress-bar">
                    <div class="progress-fill" id="gasProgress" style="width: 0%"></div>
                </div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Total Value Locked</span>
                    <div class="card-icon blue">🔒</div>
                </div>
                <div class="stat-value" id="tvl">0 CAS</div>
                <div class="stat-label">Bridge TVL</div>
            </div>
        </div>
        
        <!-- Sequencers and Blocks -->
        <div class="grid">
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Active Sequencers</span>
                    <span class="badge info" id="sequencerCount">0</span>
                </div>
                <div class="sequencer-list" id="sequencerList">
                    <div class="loading"><div class="spinner"></div></div>
                </div>
            </div>
            
            <div class="card table-card">
                <div class="card-header">
                    <span class="card-title">Recent Blocks</span>
                    <span class="refresh-indicator">Auto-refresh: 5s</span>
                </div>
                <table>
                    <thead>
                        <tr>
                            <th>Block</th>
                            <th>Hash</th>
                            <th>Txns</th>
                            <th>Gas Used</th>
                            <th>Sequencer</th>
                            <th>Time</th>
                        </tr>
                    </thead>
                    <tbody id="blocksTable">
                        <tr><td colspan="6" class="loading"><div class="spinner"></div></td></tr>
                    </tbody>
                </table>
            </div>
        </div>
        
        <!-- Bridge Status -->
        <div class="grid">
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Pending Withdrawals</span>
                    <div class="card-icon yellow">⏳</div>
                </div>
                <div class="stat-value" id="pendingWithdrawals">0</div>
                <div class="stat-label">Awaiting Challenge Period</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Total Deposits</span>
                    <div class="card-icon green">📥</div>
                </div>
                <div class="stat-value" id="totalDeposits">0</div>
                <div class="stat-label">All-time Deposits</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Total Withdrawals</span>
                    <div class="card-icon blue">📤</div>
                </div>
                <div class="stat-value" id="totalWithdrawals">0</div>
                <div class="stat-label">Completed Withdrawals</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Security Alerts</span>
                    <div class="card-icon red">🛡️</div>
                </div>
                <div class="stat-value" id="alertCount">0</div>
                <div class="stat-label">Active Alerts</div>
            </div>
        </div>
    </div>
    
    <footer>
        <p>Cascoin L2 Dashboard • Protocol Version: <span id="protocolVersion">-</span> • Chain ID: <span id="chainId">-</span></p>
        <p style="margin-top: 8px;">Last updated: <span id="lastUpdate">-</span></p>
    </footer>
    
    <script>
        // API endpoints
        const API = {
            status: '/l2/status',
            sequencers: '/l2/sequencers',
            blocks: '/l2/blocks',
            stats: '/l2/api/stats',
            alerts: '/l2/api/alerts',
            withdrawals: '/l2/api/withdrawals'
        };
        
        // Fetch with error handling
        async function fetchJSON(url) {
            try {
                const response = await fetch(url);
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                return await response.json();
            } catch (error) {
                console.error(`Error fetching ${url}:`, error);
                return null;
            }
        }
        
        // Format hash for display
        function formatHash(hash, len = 8) {
            if (!hash || hash.length < len * 2) return hash || '-';
            return hash.substring(0, len) + '...' + hash.substring(hash.length - len);
        }
        
        // Format timestamp
        function formatTime(timestamp) {
            if (!timestamp) return '-';
            const date = new Date(timestamp * 1000);
            return date.toLocaleTimeString();
        }
        
        // Format large numbers
        function formatNumber(num) {
            if (num >= 1000000) return (num / 1000000).toFixed(2) + 'M';
            if (num >= 1000) return (num / 1000).toFixed(2) + 'K';
            return num.toString();
        }
        
        // Update status
        async function updateStatus() {
            const data = await fetchJSON(API.status);
            if (!data) {
                document.getElementById('healthDot').className = 'status-dot danger';
                document.getElementById('healthStatus').textContent = 'Disconnected';
                return;
            }
            
            document.getElementById('healthDot').className = 
                data.isHealthy ? 'status-dot' : 'status-dot warning';
            document.getElementById('healthStatus').textContent = 
                data.isHealthy ? 'Healthy' : data.healthStatus;
            
            document.getElementById('blockHeight').textContent = formatNumber(data.blockHeight);
            document.getElementById('tps').textContent = data.tps.toFixed(2);
            document.getElementById('gasUtil').textContent = data.gasUtilization.toFixed(1) + '%';
            document.getElementById('gasProgress').style.width = data.gasUtilization + '%';
            document.getElementById('chainId').textContent = data.chainId;
            document.getElementById('protocolVersion').textContent = data.version;
        }
        
        // Update sequencers
        async function updateSequencers() {
            const data = await fetchJSON(API.sequencers);
            if (!data) return;
            
            document.getElementById('sequencerCount').textContent = data.eligibleCount;
            
            const list = document.getElementById('sequencerList');
            if (data.sequencers.length === 0) {
                list.innerHTML = '<p style="text-align:center;color:var(--text-secondary);padding:20px;">No sequencers registered</p>';
                return;
            }
            
            list.innerHTML = data.sequencers.map((seq, i) => `
                <div class="sequencer-item">
                    <div class="sequencer-info">
                        <div class="sequencer-avatar">${i + 1}</div>
                        <div class="sequencer-details">
                            <h4>${formatHash(seq.address, 6)}</h4>
                            <span>HAT: ${seq.hatScore} • Stake: ${seq.stakeFormatted}</span>
                        </div>
                    </div>
                    <div class="sequencer-stats">
                        <div class="uptime">${seq.uptime.toFixed(1)}%</div>
                        <div class="blocks">${seq.blocksProduced} blocks</div>
                    </div>
                </div>
            `).join('');
        }
        
        // Update blocks
        async function updateBlocks() {
            const data = await fetchJSON(API.blocks + '?limit=10');
            if (!data) return;
            
            const tbody = document.getElementById('blocksTable');
            if (data.blocks.length === 0) {
                tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:var(--text-secondary);">No blocks yet</td></tr>';
                return;
            }
            
            tbody.innerHTML = data.blocks.map(block => `
                <tr>
                    <td><strong>#${block.number}</strong></td>
                    <td><span class="hash">${formatHash(block.hash)}</span></td>
                    <td>${block.transactionCount}</td>
                    <td>${formatNumber(block.gasUsed)}</td>
                    <td><span class="hash">${formatHash(block.sequencer, 4)}</span></td>
                    <td>${formatTime(block.timestamp)}</td>
                </tr>
            `).join('');
        }
        
        // Update stats
        async function updateStats() {
            const data = await fetchJSON(API.stats);
            if (!data) return;
            
            document.getElementById('tvl').textContent = data.tvlFormatted || '0 CAS';
            document.getElementById('totalDeposits').textContent = formatNumber(data.totalDeposits);
            document.getElementById('totalWithdrawals').textContent = formatNumber(data.totalWithdrawals);
        }
        
        // Update withdrawals
        async function updateWithdrawals() {
            const data = await fetchJSON(API.withdrawals);
            if (!data) return;
            
            document.getElementById('pendingWithdrawals').textContent = data.pendingCount;
        }
        
        // Update alerts
        async function updateAlerts() {
            const data = await fetchJSON(API.alerts);
            if (!data) return;
            
            document.getElementById('alertCount').textContent = data.activeCount;
            
            const banner = document.getElementById('alertBanner');
            if (data.criticalCount > 0 && data.alerts.length > 0) {
                document.getElementById('alertMessage').textContent = data.alerts[0].message;
                banner.classList.add('show');
            } else {
                banner.classList.remove('show');
            }
        }
        
        // Update last refresh time
        function updateLastRefresh() {
            document.getElementById('lastUpdate').textContent = new Date().toLocaleString();
        }
        
        // Main update function
        async function updateAll() {
            await Promise.all([
                updateStatus(),
                updateSequencers(),
                updateBlocks(),
                updateStats(),
                updateWithdrawals(),
                updateAlerts()
            ]);
            updateLastRefresh();
        }
        
        // SSE/Polling for real-time updates
        let clientId = null;
        let eventSource = null;
        
        function initEventStream() {
            // Try SSE first
            if (typeof EventSource !== 'undefined') {
                try {
                    eventSource = new EventSource('/l2/stream');
                    
                    eventSource.onopen = function() {
                        console.log('SSE connected');
                    };
                    
                    eventSource.addEventListener('connected', function(e) {
                        const data = JSON.parse(e.data);
                        clientId = data.clientId;
                        console.log('SSE client ID:', clientId);
                    });
                    
                    eventSource.addEventListener('message', function(e) {
                        handleEvent(JSON.parse(e.data));
                    });
                    
                    eventSource.onerror = function() {
                        console.log('SSE error, falling back to polling');
                        eventSource.close();
                        startPolling();
                    };
                    
                    return;
                } catch (err) {
                    console.log('SSE not available:', err);
                }
            }
            
            // Fall back to polling
            startPolling();
        }
        
        function startPolling() {
            setInterval(async function() {
                try {
                    const url = clientId ? `/l2/api/events?clientId=${clientId}` : '/l2/api/events';
                    const response = await fetch(url);
                    const data = await response.json();
                    
                    if (!clientId && data.clientId) {
                        clientId = data.clientId;
                    }
                    
                    if (data.events) {
                        data.events.forEach(handleEvent);
                    }
                } catch (err) {
                    console.error('Polling error:', err);
                }
            }, 2000);
        }
        
        function handleEvent(event) {
            console.log('Event received:', event);
            
            switch (event.type) {
                case 1: // NEW_BLOCK
                    updateBlocks();
                    updateStatus();
                    break;
                case 2: // SEQUENCER_UPDATE
                    updateSequencers();
                    break;
                case 3: // SECURITY_ALERT
                    updateAlerts();
                    showNotification(event.data.message, event.data.alertType);
                    break;
                case 4: // STATS_UPDATE
                    if (event.data.tps !== undefined) {
                        document.getElementById('tps').textContent = event.data.tps.toFixed(2);
                    }
                    if (event.data.gasUtilization !== undefined) {
                        document.getElementById('gasUtil').textContent = event.data.gasUtilization.toFixed(1) + '%';
                        document.getElementById('gasProgress').style.width = event.data.gasUtilization + '%';
                    }
                    break;
                case 5: // WITHDRAWAL_UPDATE
                    updateWithdrawals();
                    break;
                case 6: // LEADER_CHANGE
                    updateSequencers();
                    break;
            }
        }
        
        function showNotification(message, type) {
            const banner = document.getElementById('alertBanner');
            document.getElementById('alertMessage').textContent = message;
            banner.classList.add('show');
            
            // Auto-hide after 10 seconds for non-critical alerts
            if (type !== 'critical' && type !== 'emergency') {
                setTimeout(() => banner.classList.remove('show'), 10000);
            }
        }
        
        // Initial load and auto-refresh
        updateAll();
        setInterval(updateAll, 5000);
        
        // Initialize event stream for real-time updates
        initEventStream();
    </script>
</body>
</html>
)HTML";

} // namespace L2DashboardHTML
} // namespace l2

#endif // CASCOIN_HTTPSERVER_L2_DASHBOARD_HTML_H
