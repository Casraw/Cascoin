// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_REWARD_DASHBOARD_HTML_H
#define CASCOIN_HTTPSERVER_REWARD_DASHBOARD_HTML_H

/**
 * @file reward_dashboard_html.h
 * @brief Embedded HTML/CSS/JS for Challenger Reward System Web Dashboard
 * 
 * This file contains the complete embedded dashboard frontend including:
 * - Pending Rewards Overview
 * - Dispute Detail View with Reward Breakdown
 * - Claim Reward Interface
 * - Claim History
 * 
 * Requirements: 7.1, 7.2, 7.3, 7.4, 10.1, 10.2, 10.3, 10.4, 10.5
 */

#include <string>

namespace reward {
namespace RewardDashboardHTML {

/**
 * @brief Complete Reward Dashboard HTML page
 * 
 * Single-page application with embedded CSS and JavaScript.
 * Auto-refreshes data every 10 seconds via API calls.
 */
inline const std::string INDEX_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cascoin Challenger Reward Dashboard</title>
    <style>
        :root {
            --primary-color: #6366f1;
            --primary-dark: #4f46e5;
            --success-color: #10b981;
            --warning-color: #f59e0b;
            --danger-color: #ef4444;
            --bg-color: #0f172a;
            --card-bg: #1e293b;
            --text-primary: #f1f5f9;
            --text-secondary: #94a3b8;
            --border-color: #334155;
        }
        
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-primary);
            line-height: 1.6;
        }
        
        .container { max-width: 1400px; margin: 0 auto; padding: 20px; }
        
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
            flex-wrap: wrap;
            gap: 15px;
        }
        
        .logo { display: flex; align-items: center; gap: 12px; }
        .logo h1 { font-size: 1.8rem; font-weight: 700; }
        .logo span {
            background: rgba(255,255,255,0.2);
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.85rem;
        }
        
        .address-input {
            display: flex;
            gap: 10px;
            align-items: center;
        }
        
        .address-input input {
            padding: 10px 15px;
            border: 1px solid var(--border-color);
            border-radius: 8px;
            background: rgba(255,255,255,0.1);
            color: var(--text-primary);
            font-size: 0.9rem;
            width: 350px;
        }
        
        .address-input input::placeholder { color: var(--text-secondary); }
        
        .btn {
            padding: 10px 20px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-weight: 600;
            transition: all 0.2s;
        }
        
        .btn-primary {
            background: var(--success-color);
            color: white;
        }
        
        .btn-primary:hover { background: #059669; }
        
        .btn-secondary {
            background: var(--primary-color);
            color: white;
        }
        
        .btn-secondary:hover { background: var(--primary-dark); }
        
        .btn-danger {
            background: var(--danger-color);
            color: white;
        }
        
        .btn-danger:hover { background: #dc2626; }
        
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
        
        .card-icon.purple { background: rgba(99, 102, 241, 0.2); color: var(--primary-color); }
        .card-icon.green { background: rgba(16, 185, 129, 0.2); color: var(--success-color); }
        .card-icon.yellow { background: rgba(245, 158, 11, 0.2); color: var(--warning-color); }
        .card-icon.red { background: rgba(239, 68, 68, 0.2); color: var(--danger-color); }
        
        .stat-value { font-size: 2.2rem; font-weight: 700; margin-bottom: 4px; }
        .stat-label { color: var(--text-secondary); font-size: 0.9rem; }
        
        .table-card { grid-column: span 2; }
        @media (max-width: 768px) { .table-card { grid-column: span 1; } }
        
        table { width: 100%; border-collapse: collapse; }
        
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
        }
        
        tr:hover { background: rgba(255, 255, 255, 0.02); }
        
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
        .badge.info { background: rgba(99, 102, 241, 0.2); color: var(--primary-color); }
        
        .reward-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px;
            border: 1px solid var(--border-color);
            border-radius: 8px;
            margin-bottom: 10px;
            background: rgba(255,255,255,0.02);
        }
        
        .reward-info h4 { font-size: 1rem; margin-bottom: 4px; }
        .reward-info span { font-size: 0.85rem; color: var(--text-secondary); }
        
        .reward-amount {
            text-align: right;
        }
        
        .reward-amount .amount {
            font-size: 1.2rem;
            font-weight: 700;
            color: var(--success-color);
        }
        
        .reward-amount .type {
            font-size: 0.8rem;
            color: var(--text-secondary);
        }
        
        .tabs {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 10px;
        }
        
        .tab {
            padding: 10px 20px;
            border: none;
            background: transparent;
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 1rem;
            border-radius: 8px 8px 0 0;
            transition: all 0.2s;
        }
        
        .tab.active {
            background: var(--primary-color);
            color: white;
        }
        
        .tab:hover:not(.active) { background: rgba(255,255,255,0.05); }
        
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        
        .distribution-breakdown {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 20px;
        }
        
        .breakdown-item {
            padding: 15px;
            background: rgba(255,255,255,0.02);
            border-radius: 8px;
            border: 1px solid var(--border-color);
        }
        
        .breakdown-item .label {
            font-size: 0.85rem;
            color: var(--text-secondary);
            margin-bottom: 5px;
        }
        
        .breakdown-item .value {
            font-size: 1.1rem;
            font-weight: 600;
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
        
        @keyframes spin { to { transform: rotate(360deg); } }
        
        .empty-state {
            text-align: center;
            padding: 40px;
            color: var(--text-secondary);
        }
        
        .empty-state .icon { font-size: 3rem; margin-bottom: 15px; }
        
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.7);
            z-index: 1000;
            align-items: center;
            justify-content: center;
        }
        
        .modal.show { display: flex; }
        
        .modal-content {
            background: var(--card-bg);
            border-radius: 12px;
            padding: 30px;
            max-width: 600px;
            width: 90%;
            max-height: 80vh;
            overflow-y: auto;
        }
        
        .modal-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
        }
        
        .modal-header h2 { font-size: 1.5rem; }
        
        .modal-close {
            background: none;
            border: none;
            color: var(--text-secondary);
            font-size: 1.5rem;
            cursor: pointer;
        }
        
        footer {
            text-align: center;
            padding: 30px 0;
            color: var(--text-secondary);
            font-size: 0.9rem;
            border-top: 1px solid var(--border-color);
            margin-top: 40px;
        }
        
        .rpc-hint {
            background: rgba(99, 102, 241, 0.1);
            border: 1px solid var(--primary-color);
            border-radius: 8px;
            padding: 15px;
            margin-top: 15px;
        }
        
        .rpc-hint code {
            background: rgba(0,0,0,0.3);
            padding: 2px 6px;
            border-radius: 4px;
            font-family: monospace;
        }
    </style>
</head>
<body>
    <header>
        <div class="container">
            <div class="logo">
                <h1>🏆 Challenger Rewards</h1>
                <span>DAO Dashboard</span>
            </div>
            <div class="address-input">
                <input type="text" id="walletAddress" placeholder="Enter your wallet address (e.g., CYour...)" />
                <button class="btn btn-primary" onclick="loadRewards()">Load Rewards</button>
            </div>
        </div>
    </header>
    
    <div class="container">
        <!-- Stats Grid -->
        <div class="grid">
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Total Claimable</span>
                    <div class="card-icon green">💰</div>
                </div>
                <div class="stat-value" id="totalClaimable">0.00 CAS</div>
                <div class="stat-label">Pending Rewards</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Pending Rewards</span>
                    <div class="card-icon purple">🎁</div>
                </div>
                <div class="stat-value" id="pendingCount">0</div>
                <div class="stat-label">Unclaimed Rewards</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Active Disputes</span>
                    <div class="card-icon yellow">⚖️</div>
                </div>
                <div class="stat-value" id="activeDisputes">0</div>
                <div class="stat-label">Ongoing Challenges</div>
            </div>
            
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Resolved Disputes</span>
                    <div class="card-icon green">✅</div>
                </div>
                <div class="stat-value" id="resolvedDisputes">0</div>
                <div class="stat-label">Completed</div>
            </div>
        </div>
        
        <!-- Tabs -->
        <div class="tabs">
            <button class="tab active" onclick="showTab('rewards')">My Rewards</button>
            <button class="tab" onclick="showTab('disputes')">Disputes</button>
            <button class="tab" onclick="showTab('history')">Claim History</button>
        </div>
        
        <!-- Rewards Tab -->
        <div id="rewards-tab" class="tab-content active">
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Pending Rewards</span>
                    <button class="btn btn-secondary" onclick="claimAllRewards()">Claim All</button>
                </div>
                <div id="rewardsList">
                    <div class="empty-state">
                        <div class="icon">🔍</div>
                        <p>Enter your wallet address above to view pending rewards</p>
                    </div>
                </div>
                <div class="rpc-hint">
                    <strong>💡 To claim rewards:</strong> Use the RPC command: 
                    <code>claimreward "reward_id" "your_address"</code> or 
                    <code>claimallrewards "your_address"</code>
                </div>
            </div>
        </div>
        
        <!-- Disputes Tab -->
        <div id="disputes-tab" class="tab-content">
            <div class="card table-card">
                <div class="card-header">
                    <span class="card-title">DAO Disputes</span>
                    <select id="disputeFilter" onchange="loadDisputes()">
                        <option value="">All Disputes</option>
                        <option value="pending">Pending</option>
                        <option value="resolved">Resolved</option>
                    </select>
                </div>
                <table>
                    <thead>
                        <tr>
                            <th>Dispute ID</th>
                            <th>Challenger</th>
                            <th>Bond</th>
                            <th>Status</th>
                            <th>Decision</th>
                            <th>Actions</th>
                        </tr>
                    </thead>
                    <tbody id="disputesTable">
                        <tr><td colspan="6" class="loading"><div class="spinner"></div></td></tr>
                    </tbody>
                </table>
            </div>
        </div>
        
        <!-- History Tab -->
        <div id="history-tab" class="tab-content">
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Claim History</span>
                </div>
                <div id="historyList">
                    <div class="empty-state">
                        <div class="icon">📜</div>
                        <p>Claim history will appear here after you claim rewards</p>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <!-- Dispute Detail Modal -->
    <div class="modal" id="disputeModal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Dispute Details</h2>
                <button class="modal-close" onclick="closeModal()">&times;</button>
            </div>
            <div id="disputeDetail">
                <div class="loading"><div class="spinner"></div></div>
            </div>
        </div>
    </div>
    
    <footer>
        <p>Cascoin Challenger Reward System Dashboard</p>
        <p style="margin-top: 8px;">Last updated: <span id="lastUpdate">-</span></p>
    </footer>
    
    <script>
        const API = {
            pending: '/rewards/api/pending',
            distribution: '/rewards/api/distribution',
            claim: '/rewards/api/claim',
            claimAll: '/rewards/api/claimall',
            disputes: '/rewards/api/disputes',
            dispute: '/rewards/api/dispute',
            history: '/rewards/api/history'
        };
        
        let currentAddress = '';
        let pendingRewards = [];
        let claimedRewards = [];
        
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
        
        function formatHash(hash, len = 8) {
            if (!hash || hash.length < len * 2) return hash || '-';
            return hash.substring(0, len) + '...' + hash.substring(hash.length - len);
        }
        
        function showTab(tabName) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            
            event.target.classList.add('active');
            document.getElementById(tabName + '-tab').classList.add('active');
            
            if (tabName === 'disputes') loadDisputes();
            if (tabName === 'history' && currentAddress) loadHistory();
        }
        
        async function loadRewards() {
            const address = document.getElementById('walletAddress').value.trim();
            if (!address) {
                alert('Please enter a wallet address');
                return;
            }
            
            currentAddress = address;
            document.getElementById('rewardsList').innerHTML = '<div class="loading"><div class="spinner"></div></div>';
            
            const data = await fetchJSON(`${API.pending}?address=${encodeURIComponent(address)}`);
            
            if (!data || !data.success) {
                document.getElementById('rewardsList').innerHTML = `
                    <div class="empty-state">
                        <div class="icon">❌</div>
                        <p>${data?.error || 'Failed to load rewards'}</p>
                    </div>`;
                return;
            }
            
            pendingRewards = data.rewards || [];
            document.getElementById('totalClaimable').textContent = data.total_pending_formatted || '0.00 CAS';
            document.getElementById('pendingCount').textContent = data.count || 0;
            
            if (pendingRewards.length === 0) {
                document.getElementById('rewardsList').innerHTML = `
                    <div class="empty-state">
                        <div class="icon">🎉</div>
                        <p>No pending rewards for this address</p>
                    </div>`;
            } else {
                let html = '';
                for (const reward of pendingRewards) {
                    html += `
                        <div class="reward-item">
                            <div class="reward-info">
                                <h4>${reward.type.replace(/_/g, ' ')}</h4>
                                <span>Dispute: ${formatHash(reward.dispute_id)}</span><br>
                                <span>Created: ${reward.created_time_formatted}</span>
                            </div>
                            <div class="reward-amount">
                                <div class="amount">${reward.amount_formatted}</div>
                                <button class="btn btn-primary" onclick="claimReward('${reward.reward_id}')">Claim</button>
                            </div>
                        </div>`;
                }
                document.getElementById('rewardsList').innerHTML = html;
            }
            
            // Also load history when address is set
            loadHistory();
            updateLastRefresh();
        }
        
        async function loadHistory() {
            if (!currentAddress) {
                document.getElementById('historyList').innerHTML = `
                    <div class="empty-state">
                        <div class="icon">🔍</div>
                        <p>Enter your wallet address above to view claim history</p>
                    </div>`;
                return;
            }
            
            document.getElementById('historyList').innerHTML = '<div class="loading"><div class="spinner"></div></div>';
            
            const data = await fetchJSON(`${API.history}?address=${encodeURIComponent(currentAddress)}`);
            
            if (!data || !data.success) {
                document.getElementById('historyList').innerHTML = `
                    <div class="empty-state">
                        <div class="icon">❌</div>
                        <p>${data?.error || 'Failed to load claim history'}</p>
                    </div>`;
                return;
            }
            
            claimedRewards = data.rewards || [];
            
            if (claimedRewards.length === 0) {
                document.getElementById('historyList').innerHTML = `
                    <div class="empty-state">
                        <div class="icon">📜</div>
                        <p>No claimed rewards yet. Claim your pending rewards to see them here.</p>
                    </div>`;
                return;
            }
            
            let html = `<p style="margin-bottom: 15px; color: var(--text-secondary);">Total claimed: <strong style="color: var(--success-color)">${data.total_claimed_formatted}</strong></p>`;
            
            for (const reward of claimedRewards) {
                html += `
                    <div class="reward-item" style="border-color: var(--success-color);">
                        <div class="reward-info">
                            <h4>${reward.type.replace(/_/g, ' ')} <span class="badge success">Claimed</span></h4>
                            <span>Dispute: ${formatHash(reward.dispute_id)}</span><br>
                            <span>Created: ${reward.created_time_formatted}</span><br>
                            <span>Claimed: ${reward.claimed_time_formatted}</span>
                        </div>
                        <div class="reward-amount">
                            <div class="amount" style="color: var(--text-secondary);">${reward.amount_formatted}</div>
                            ${reward.claim_tx_hash && reward.claim_tx_hash !== '0000000000000000000000000000000000000000000000000000000000000000' ? 
                                `<span class="hash" style="font-size: 0.75rem;">TX: ${formatHash(reward.claim_tx_hash)}</span>` : ''}
                        </div>
                    </div>`;
            }
            
            document.getElementById('historyList').innerHTML = html;
        }
        
        async function loadDisputes() {
            const filter = document.getElementById('disputeFilter').value;
            const tbody = document.getElementById('disputesTable');
            tbody.innerHTML = '<tr><td colspan="6" class="loading"><div class="spinner"></div></td></tr>';
            
            const data = await fetchJSON(`${API.disputes}?status=${filter}&limit=50`);
            
            if (!data || !data.success) {
                tbody.innerHTML = '<tr><td colspan="6" style="text-align:center">Failed to load disputes</td></tr>';
                return;
            }
            
            const disputes = data.disputes || [];
            
            // Update stats
            const active = disputes.filter(d => !d.resolved).length;
            const resolved = disputes.filter(d => d.resolved).length;
            document.getElementById('activeDisputes').textContent = active;
            document.getElementById('resolvedDisputes').textContent = resolved;
            
            if (disputes.length === 0) {
                tbody.innerHTML = '<tr><td colspan="6" style="text-align:center">No disputes found</td></tr>';
                return;
            }
            
            tbody.innerHTML = disputes.map(d => `
                <tr>
                    <td><span class="hash">${formatHash(d.dispute_id)}</span></td>
                    <td><span class="hash">${formatHash(d.challenger, 6)}</span></td>
                    <td>${d.challenge_bond_formatted}</td>
                    <td><span class="badge ${d.resolved ? 'success' : 'warning'}">${d.resolved ? 'Resolved' : 'Pending'}</span></td>
                    <td>${d.resolved ? (d.slash_decision ? '<span class="badge danger">Slash</span>' : '<span class="badge info">Keep</span>') : '-'}</td>
                    <td><button class="btn btn-secondary" onclick="viewDispute('${d.dispute_id}')">View</button></td>
                </tr>
            `).join('');
            
            updateLastRefresh();
        }
        
        async function viewDispute(disputeId) {
            document.getElementById('disputeModal').classList.add('show');
            document.getElementById('disputeDetail').innerHTML = '<div class="loading"><div class="spinner"></div></div>';
            
            const data = await fetchJSON(`${API.dispute}?dispute_id=${disputeId}`);
            
            if (!data || !data.success) {
                document.getElementById('disputeDetail').innerHTML = `<p>Failed to load dispute details</p>`;
                return;
            }
            
            let html = `
                <p><strong>Dispute ID:</strong> <span class="hash">${data.dispute_id}</span></p>
                <p><strong>Challenger:</strong> ${data.challenger}</p>
                <p><strong>Challenge Bond:</strong> ${data.challenge_bond_formatted}</p>
                <p><strong>Created:</strong> ${data.created_time_formatted}</p>
                <p><strong>Status:</strong> <span class="badge ${data.resolved ? 'success' : 'warning'}">${data.resolved ? 'Resolved' : 'Pending'}</span></p>
            `;
            
            if (data.resolved) {
                html += `
                    <p><strong>Decision:</strong> <span class="badge ${data.slash_decision ? 'danger' : 'info'}">${data.slash_decision ? 'Slash' : 'Keep'}</span></p>
                    <p><strong>Resolved:</strong> ${data.resolved_time_formatted}</p>
                `;
                
                if (data.reward_distribution) {
                    const dist = data.reward_distribution;
                    html += `
                        <h3 style="margin-top: 20px; margin-bottom: 15px;">Reward Distribution</h3>
                        <div class="distribution-breakdown">
                            <div class="breakdown-item">
                                <div class="label">Challenger Bond Return</div>
                                <div class="value" style="color: var(--success-color)">${dist.challenger_bond_return_formatted}</div>
                            </div>
                            <div class="breakdown-item">
                                <div class="label">Challenger Bounty</div>
                                <div class="value" style="color: var(--success-color)">${dist.challenger_bounty_formatted}</div>
                            </div>
                            <div class="breakdown-item">
                                <div class="label">DAO Voter Rewards</div>
                                <div class="value" style="color: var(--primary-color)">${dist.total_dao_voter_rewards_formatted}</div>
                            </div>
                            <div class="breakdown-item">
                                <div class="label">Burned</div>
                                <div class="value" style="color: var(--danger-color)">${dist.burned_amount_formatted}</div>
                            </div>
                        </div>
                    `;
                }
            }
            
            if (data.use_commit_reveal) {
                html += `
                    <p style="margin-top: 15px;"><strong>Voting Mode:</strong> Commit-Reveal</p>
                    <p><strong>Slash Votes:</strong> ${data.slash_votes}</p>
                    <p><strong>Keep Votes:</strong> ${data.keep_votes}</p>
                `;
            }
            
            document.getElementById('disputeDetail').innerHTML = html;
        }
        
        function closeModal() {
            document.getElementById('disputeModal').classList.remove('show');
        }
        
        async function claimReward(rewardId) {
            alert(`To claim this reward, use the RPC command:\n\nclaimreward "${rewardId}" "${currentAddress}"`);
        }
        
        async function claimAllRewards() {
            if (!currentAddress) {
                alert('Please enter your wallet address first');
                return;
            }
            alert(`To claim all rewards, use the RPC command:\n\nclaimallrewards "${currentAddress}"`);
        }
        
        function updateLastRefresh() {
            document.getElementById('lastUpdate').textContent = new Date().toLocaleString();
        }
        
        // Initial load
        loadDisputes();
        
        // Auto-refresh every 30 seconds
        setInterval(() => {
            if (currentAddress) {
                loadRewards();
                loadHistory();
            }
            loadDisputes();
        }, 30000);
        
        // Close modal on escape
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') closeModal();
        });
        
        // Close modal on outside click
        document.getElementById('disputeModal').addEventListener('click', (e) => {
            if (e.target.id === 'disputeModal') closeModal();
        });
    </script>
</body>
</html>
)HTML";

} // namespace RewardDashboardHTML
} // namespace reward

#endif // CASCOIN_HTTPSERVER_REWARD_DASHBOARD_HTML_H
