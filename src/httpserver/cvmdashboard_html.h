#ifndef CASCOIN_HTTPSERVER_CVMDASHBOARD_HTML_H
#define CASCOIN_HTTPSERVER_CVMDASHBOARD_HTML_H

#include <string>

namespace CVMDashboardHTML {

// Main HTML page (embedded)
static const std::string INDEX_HTML = std::string(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cascoin CVM Dashboard</title>
    <style>
/* Cascoin CVM Dashboard Styles */
* { margin: 0; padding: 0; box-sizing: border-box; }
:root {
    --primary-color: #2563eb; --secondary-color: #10b981; --danger-color: #ef4444;
    --warning-color: #f59e0b; --bg-color: #f8fafc; --card-bg: #ffffff;
    --text-primary: #1e293b; --text-secondary: #64748b; --border-color: #e2e8f0;
    --shadow: 0 1px 3px rgba(0, 0, 0, 0.1); --shadow-lg: 0 10px 15px -3px rgba(0, 0, 0, 0.1);
}
body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    background-color: var(--bg-color); color: var(--text-primary); line-height: 1.6;
}
.container { max-width: 1400px; margin: 0 auto; padding: 20px; }
.header {
    background: linear-gradient(135deg, var(--primary-color), #1d4ed8);
    color: white; padding: 30px; border-radius: 12px; margin-bottom: 30px;
    box-shadow: var(--shadow-lg); display: flex; justify-content: space-between; align-items: center;
}
.header h1 { font-size: 2.5rem; margin-bottom: 8px; }
.subtitle { opacity: 0.9; font-size: 1.1rem; }
.status { display: flex; align-items: center; gap: 10px; background: rgba(255, 255, 255, 0.1); padding: 10px 20px; border-radius: 8px; }
.status-indicator { width: 12px; height: 12px; border-radius: 50%; background-color: var(--warning-color); animation: pulse 2s infinite; }
.status-indicator.connected { background-color: var(--secondary-color); animation: none; }
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
.stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin-bottom: 30px; }
.stat-card { background: var(--card-bg); border-radius: 12px; padding: 25px; box-shadow: var(--shadow); display: flex; gap: 20px; transition: transform 0.2s, box-shadow 0.2s; }
.stat-card:hover { transform: translateY(-2px); box-shadow: var(--shadow-lg); }
.stat-icon { font-size: 3rem; line-height: 1; }
.stat-content { flex: 1; }
.stat-content h3 { font-size: 0.9rem; color: var(--text-secondary); margin-bottom: 8px; text-transform: uppercase; font-weight: 600; }
.stat-value { font-size: 2.5rem; font-weight: bold; color: var(--primary-color); margin-bottom: 8px; }
.stat-progress { height: 8px; background: var(--border-color); border-radius: 4px; overflow: hidden; margin-bottom: 8px; }
.stat-progress-bar { height: 100%; background: linear-gradient(90deg, var(--primary-color), var(--secondary-color)); width: 0%; transition: width 0.5s ease; }
.stat-label { font-size: 0.85rem; color: var(--text-secondary); }
.content-grid { display: grid; grid-template-columns: 2fr 1fr; gap: 20px; margin-bottom: 30px; }
.card { background: var(--card-bg); border-radius: 12px; box-shadow: var(--shadow); overflow: hidden; }
.card.large { grid-column: span 1; }
.card-header { padding: 20px; border-bottom: 1px solid var(--border-color); display: flex; justify-content: space-between; align-items: center; }
.card-header h2 { font-size: 1.25rem; font-weight: 600; }
.card-body { padding: 20px; }
.graph-container { min-height: 400px; display: flex; align-items: center; justify-content: center; background: var(--bg-color); border-radius: 8px; }
.graph-placeholder { text-align: center; color: var(--text-secondary); }
.activity-list { display: flex; flex-direction: column; gap: 12px; max-height: 500px; overflow-y: auto; }
.activity-item { padding: 12px; background: var(--bg-color); border-radius: 8px; border-left: 3px solid var(--primary-color); display: flex; gap: 12px; transition: all 0.2s ease; }
.activity-item:hover { background: rgba(16, 185, 129, 0.05); transform: translateX(2px); }
.activity-icon { font-size: 1.5rem; display: flex; align-items: center; }
.activity-content { flex: 1; min-width: 0; }
.activity-type { font-weight: 600; color: var(--primary-color); margin-bottom: 4px; }
.activity-desc { color: var(--text-secondary); font-size: 0.9rem; margin-bottom: 4px; }
.activity-reason { color: var(--text-color); font-style: italic; opacity: 0.8; }
.activity-time { color: var(--text-secondary); font-size: 0.8rem; }
.activity-trust { border-left-color: rgba(16, 185, 129, 0.8); }
.activity-vote-pos { border-left-color: rgba(34, 197, 94, 0.8); }
.activity-vote { border-left-color: rgba(59, 130, 246, 0.8); }
.activity-report { border-left-color: rgba(239, 68, 68, 0.8); }
.activity-placeholder { text-align: center; padding: 40px; color: var(--text-secondary); }
.badge { padding: 2px 6px; border-radius: 4px; font-size: 0.7rem; font-weight: 600; }
.badge-danger { background: rgba(239, 68, 68, 0.2); color: #ef4444; }
.badge-success { background: rgba(16, 185, 129, 0.2); color: #10b981; }
.badge-warning { background: rgba(245, 158, 11, 0.2); color: #f59e0b; }
.disputes-container { display: flex; flex-direction: column; gap: 16px; max-height: 600px; overflow-y: auto; }
.dispute-card { background: var(--bg-color); border: 1px solid var(--border-color); border-radius: 8px; padding: 16px; transition: all 0.2s ease; }
.dispute-card:hover { border-color: var(--primary-color); box-shadow: 0 4px 12px rgba(16, 185, 129, 0.1); }
.dispute-card.resolved { opacity: 0.7; }
.dispute-header { display: flex; justify-content: space-between; align-items: start; margin-bottom: 12px; }
.dispute-title { font-size: 1rem; font-weight: 600; color: var(--text-color); margin-bottom: 4px; }
.dispute-meta { font-size: 0.85rem; color: var(--text-secondary); }
.dispute-reason { font-style: italic; color: var(--text-secondary); margin: 8px 0; padding: 8px; background: rgba(16, 185, 129, 0.05); border-left: 3px solid var(--primary-color); }
.dispute-stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 12px 0; }
.dispute-stat { text-align: center; padding: 8px; background: rgba(16, 185, 129, 0.05); border-radius: 6px; }
.dispute-stat-label { font-size: 0.75rem; color: var(--text-secondary); margin-bottom: 4px; }
.dispute-stat-value { font-size: 1.1rem; font-weight: 600; color: var(--primary-color); }
.dispute-stat-value.slash { color: #ef4444; }
.dispute-stat-value.keep { color: #10b981; }
.dispute-progress { height: 24px; background: var(--bg-color); border-radius: 12px; overflow: hidden; display: flex; margin: 12px 0; border: 1px solid var(--border-color); }
.dispute-progress-slash { background: linear-gradient(90deg, #ef4444, #dc2626); display: flex; align-items: center; justify-content: center; color: white; font-size: 0.75rem; font-weight: 600; transition: width 0.3s ease; }
.dispute-progress-keep { background: linear-gradient(90deg, #10b981, #059669); display: flex; align-items: center; justify-content: center; color: white; font-size: 0.75rem; font-weight: 600; transition: width 0.3s ease; }
.dispute-actions { display: flex; gap: 8px; margin-top: 12px; padding-top: 12px; border-top: 1px solid var(--border-color); }
.dispute-vote-btn { flex: 1; padding: 10px; border-radius: 6px; border: none; font-weight: 600; cursor: pointer; transition: all 0.2s ease; display: flex; align-items: center; justify-content: center; gap: 6px; }
.dispute-vote-btn.slash { background: rgba(239, 68, 68, 0.2); color: #ef4444; border: 2px solid #ef4444; }
.dispute-vote-btn.slash:hover { background: #ef4444; color: white; transform: translateY(-2px); box-shadow: 0 4px 12px rgba(239, 68, 68, 0.3); }
.dispute-vote-btn.keep { background: rgba(16, 185, 129, 0.2); color: #10b981; border: 2px solid #10b981; }
.dispute-vote-btn.keep:hover { background: #10b981; color: white; transform: translateY(-2px); box-shadow: 0 4px 12px rgba(16, 185, 129, 0.3); }
.dispute-vote-btn:disabled { opacity: 0.5; cursor: not-allowed; }
.dispute-details-btn { background: transparent; border: 1px solid var(--border-color); color: var(--text-color); padding: 8px 16px; border-radius: 6px; cursor: pointer; font-size: 0.85rem; transition: all 0.2s; }
.dispute-details-btn:hover { border-color: var(--primary-color); color: var(--primary-color); }
.dispute-votes-list { margin-top: 12px; padding-top: 12px; border-top: 1px solid var(--border-color); max-height: 0; overflow: hidden; transition: max-height 0.3s ease; }
.dispute-votes-list.expanded { max-height: 400px; overflow-y: auto; }
.dispute-vote-item { display: flex; justify-content: space-between; padding: 8px; margin-bottom: 4px; background: rgba(16, 185, 129, 0.03); border-radius: 4px; }
.stats-table { width: 100%; border-collapse: collapse; }
.stats-table td { padding: 12px; border-bottom: 1px solid var(--border-color); }
.stats-table td:first-child { color: var(--text-secondary); }
.stats-table td:last-child { text-align: right; font-weight: 600; color: var(--primary-color); }
.stats-table tr:last-child td { border-bottom: none; }
.btn-secondary { background: transparent; border: 1px solid var(--border-color); padding: 8px 16px; border-radius: 6px; cursor: pointer; font-size: 0.9rem; transition: all 0.2s; }
.btn-secondary:hover { background: var(--bg-color); border-color: var(--primary-color); color: var(--primary-color); }
.footer { text-align: center; padding: 30px; color: var(--text-secondary); font-size: 0.9rem; }
.footer a { color: var(--primary-color); text-decoration: none; }
.footer a:hover { text-decoration: underline; }
.text-muted { color: var(--text-secondary); font-size: 0.85rem; }
.info-icon { display: inline-block; width: 16px; height: 16px; background: var(--primary-color); color: white; border-radius: 50%; text-align: center; line-height: 16px; font-size: 11px; font-weight: bold; margin-left: 5px; cursor: help; }
.tooltip { position: relative; }
.tooltip .tooltiptext { visibility: hidden; width: 300px; background-color: #333; color: #fff; text-align: left; border-radius: 6px; padding: 10px; position: absolute; z-index: 1; bottom: 125%; left: 50%; margin-left: -150px; opacity: 0; transition: opacity 0.3s; font-size: 0.8rem; line-height: 1.4; box-shadow: 0 4px 6px rgba(0,0,0,0.2); }
.tooltip .tooltiptext::after { content: ""; position: absolute; top: 100%; left: 50%; margin-left: -5px; border-width: 5px; border-style: solid; border-color: #333 transparent transparent transparent; }
.tooltip:hover .tooltiptext { visibility: visible; opacity: 1; }
.info-box { background: var(--bg-color); border-left: 3px solid var(--primary-color); padding: 12px; margin-bottom: 15px; border-radius: 4px; font-size: 0.85rem; color: var(--text-secondary); }
@media (max-width: 1024px) { .content-grid { grid-template-columns: 1fr; } .stats-grid { grid-template-columns: repeat(2, 1fr); } }
@media (max-width: 640px) { .header { flex-direction: column; text-align: center; gap: 20px; } .stats-grid { grid-template-columns: 1fr; } .header h1 { font-size: 2rem; } }
    </style>
</head>
<body>
    <div class="container">
        <header class="header">
            <div class="header-content">
                <h1>🔗 Cascoin CVM Dashboard</h1>
                <p class="subtitle">Interactive Trust Network & Reputation System</p>
            </div>
            <div class="status" id="connectionStatus">
                <span class="status-indicator"></span>
                <span class="status-text">Connecting...</span>
            </div>
        </header>

        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-icon">⭐</div>
                <div class="stat-content">
                    <h3>My HAT v2 Trust
                        <span class="tooltip info-icon" style="font-size: 0.65rem;">?
                            <span class="tooltiptext">
                                Your Hybrid Adaptive Trust score (0-100).<br>
                                Based on behavior, Web-of-Trust, economic stake, and activity.<br>
                                Higher scores unlock better trading conditions!
                            </span>
                        </span>
                    </h3>
                    <div class="stat-value" id="myReputation">--</div>
                    <div class="stat-progress"><div class="stat-progress-bar" id="repProgress"></div></div>
                    <p class="stat-label">Secure Multi-Layer Score (0-100)</p>
                </div>
            </div>
            <div class="stat-card">
                <div class="stat-icon">🤝</div>
                <div class="stat-content">
                    <h3>Trust Relations
                        <span class="tooltip info-icon" style="font-size: 0.65rem;">?
                            <span class="tooltiptext">
                                Number of trust relationships you've established.<br>
                                Building a diverse trust network improves your HAT v2 score.
                            </span>
                        </span>
                    </h3>
                    <div class="stat-value" id="trustCount">--</div>
                    <p class="stat-label">Active connections</p>
                </div>
            </div>
            <div class="stat-card">
                <div class="stat-icon">🗳️</div>
                <div class="stat-content">
                    <h3>Votes Submitted
                        <span class="tooltip info-icon" style="font-size: 0.65rem;">?
                            <span class="tooltiptext">
                                Total reputation votes you've cast (positive and negative).<br>
                                All votes are bonded with CAS.
                            </span>
                        </span>
                    </h3>
                    <div class="stat-value" id="voteCount">--</div>
                    <p class="stat-label">Reputation votes</p>
                </div>
            </div>
            <div class="stat-card">
                <div class="stat-icon">🌐</div>
                <div class="stat-content">
                    <h3>Network Size
                        <span class="tooltip info-icon" style="font-size: 0.65rem;">?
                            <span class="tooltiptext">
                                Total addresses participating in the Cascoin reputation network.<br>
                                A larger network means better trust analysis.
                            </span>
                        </span>
                    </h3>
                    <div class="stat-value" id="networkSize">--</div>
                    <p class="stat-label">Total addresses</p>
                </div>
            </div>
        </div>

        <div class="content-grid">
            <div class="card large">
                <div class="card-header">
                    <h2>🌐 Trust Network Graph
                        <span class="tooltip info-icon">?
                            <span class="tooltiptext">
                                <strong>Wallet Cluster Visualization</strong><br>
                                Each node represents a WALLET (cluster of addresses):<br>
                                • <strong>Single node = One wallet</strong> (may contain multiple addresses)<br>
                                • <strong>Node size</strong>: Number of addresses in wallet<br>
                                • <strong>Gold border</strong>: Multi-address wallet cluster<br>
                                • <strong>White border</strong>: Single-address wallet<br>
                                • <strong>Color</strong>: Effective trust score (lowest in cluster)<br><br>
                                Click on any wallet to see all its addresses and scores!
                            </span>
                        </span>
                    </h2>
                    <button class="btn-secondary" onclick="refreshGraph()">🔄 Refresh</button>
                </div>
                <div class="card-body">
                    <div id="trustGraph" class="graph-container">
                        <div class="graph-placeholder">
                            <p>Loading trust network...</p>
                            <p class="text-muted">Interactive graph will appear here</p>
                        </div>
                    </div>
                </div>
            </div>
            <div class="card">
                <div class="card-header">
                    <h2>📜 Recent Activity
                        <span class="tooltip info-icon">?
                            <span class="tooltiptext">
                                <strong>Recent Activity Feed</strong><br>
                                Live feed of recent CVM transactions:<br>
                                • Trust relations created<br>
                                • Reputation votes submitted<br>
                                • Reports filed<br>
                                • Network changes<br><br>
                                Updates automatically every 5 seconds.
                            </span>
                        </span>
                    </h2>
                </div>
                <div class="card-body">
                    <div id="recentActivity" class="activity-list">
                        <div class="activity-placeholder">Loading recent transactions...</div>
                    </div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-header">
                <h2>🔒 HAT v2 Trust Breakdown
                    <span class="tooltip info-icon">?
                        <span class="tooltiptext">
                            <strong>Hybrid Adaptive Trust v2</strong><br>
                            Your trust score is calculated from four components:<br>
                            • <strong>Behavior (40%)</strong>: Trading patterns and diversity<br>
                            • <strong>Web-of-Trust (30%)</strong>: Community endorsements<br>
                            • <strong>Economic (20%)</strong>: Staked CAS amount<br>
                            • <strong>Temporal (10%)</strong>: Account age and activity
                        </span>
                    </span>
                </h2>
            </div>
            <div class="card-body">
                <div class="info-box">
                    HAT v2 protects against Sybil attacks using multi-layered reputation analysis. Higher scores unlock better trading conditions.
                </div>
                <table class="stats-table">
                    <tbody>
                        <tr>
                            <td>🎯 Behavior Component (40%)</td>
                            <td id="hatBehavior">--</td>
                        </tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">├─ Base Score</td>
                            <td id="hatBehaviorBase" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">├─ Diversity Penalty</td>
                            <td id="hatDiversity" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">├─ Volume Penalty</td>
                            <td id="hatVolume" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">└─ Pattern Penalty</td>
                            <td id="hatPattern" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr><td>🤝 Web-of-Trust (30%)</td><td id="hatWot">--</td></tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">├─ Cluster Penalty</td>
                            <td id="hatCluster" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">└─ Centrality Multiplier</td>
                            <td id="hatCentrality" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr><td>💰 Economic Stake (20%)</td><td id="hatEconomic">--</td></tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">└─ Time Multiplier</td>
                            <td id="hatStakeTime" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr><td>⏰ Temporal Activity (10%)</td><td id="hatTemporal">--</td></tr>
                        <tr style="padding-left: 20px; font-size: 0.85rem;">
                            <td style="padding-left: 30px; color: var(--text-secondary);">└─ Activity Penalty</td>
                            <td id="hatActivity" style="color: var(--text-secondary);">--</td>
                        </tr>
                        <tr style="border-top: 2px solid var(--primary-color); font-weight: bold; font-size: 1.1rem;">
                            <td>🏆 Final HAT v2 Score</td>
                            <td id="hatFinal" style="color: var(--primary-color);">--</td>
                        </tr>
                    </tbody>
                </table>
                <p class="text-muted" style="margin-top: 15px; text-align: center;">
                    HAT v2 = Hybrid Adaptive Trust with multi-layer defense against Sybil attacks
                </p>
            </div>
        </div>

        <div class="card">
            <div class="card-header">
                <h2>⚖️ DAO Disputes
                    <span class="tooltip info-icon">?
                        <span class="tooltiptext">
                            <strong>Decentralized Dispute Resolution</strong><br>
                            When bonded votes are challenged, DAO members vote to decide:<br>
                            • <strong>Slash</strong>: Remove the bond (vote was fake/malicious)<br>
                            • <strong>Keep</strong>: Return the bond (vote was honest)<br><br>
                            <strong>How it works:</strong><br>
                            1. Someone challenges a vote (requires bond)<br>
                            2. DAO members stake CAS and vote<br>
                            3. Majority decision wins (weighted by stake)<br>
                            4. Winner's bond is returned, loser loses bond<br><br>
                            <strong>Requirements to vote:</strong><br>
                            • High reputation (trust score)<br>
                            • Active participation<br>
                            • Minimum 0.1 CAS stake per vote
                        </span>
                    </span>
                </h2>
                <button class="btn-secondary" onclick="refreshDisputes()">🔄 Refresh</button>
            </div>
            <div class="card-body">
                <div class="info-box">
                    <strong>🏛️ Community Governance:</strong> Help decide if challenged votes were honest or malicious. Your stake determines your voting power.
                </div>
                
                <div id="disputesList" class="disputes-container">
                    <div class="activity-placeholder">Loading disputes...</div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-header">
                <h2>⚡ User Actions
                    <span class="tooltip info-icon">?
                        <span class="tooltiptext">
                            <strong>User Actions</strong><br>
                            Interact with the Cascoin network:<br>
                            • <strong>Trust Relations</strong>: Build your Web-of-Trust network<br>
                            • <strong>Reputation Votes</strong>: Endorse or warn about users<br>
                            • <strong>Send CAS</strong>: Transfer funds to other addresses<br>
                            • <strong>Report Bad Actors</strong>: Protect the community<br><br>
                            All actions require CAS bonds that can be slashed if misused.
                        </span>
                    </span>
                </h2>
            </div>
            <div class="card-body">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 20px;">
                    
                    <!-- Add Trust Form -->
                    <div style="padding: 15px; background: var(--bg-color); border-radius: 8px;">
                        <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                            🤝 Add Trust Relation
                            <span class="tooltip info-icon">?
                                <span class="tooltiptext">
                                    <strong>Trust Relations</strong><br>
                                    Create a trust edge in the Web-of-Trust graph.<br>
                                    • <strong>Weight</strong>: -100 (distrust) to +100 (full trust)<br>
                                    • <strong>Bond</strong>: Your stake, can be slashed if abused<br>
                                    • Positive trust improves their HAT v2 score<br>
                                    • Negative trust acts as a warning to others
                                </span>
                            </span>
                        </h3>
                        <p class="help-text">Build your Web-of-Trust network by adding trusted (or distrusted) addresses.</p>
                        <form id="addTrustForm" onsubmit="return handleAddTrust(event);">
                            <div style="margin-bottom: 12px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Target Address:</label>
                                <input type="text" id="trustAddress" placeholder="QAddress..." required
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                            </div>
                            <div style="margin-bottom: 12px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Trust Weight (-100 to +100):</label>
                                <input type="number" id="trustWeight" min="-100" max="100" value="80" required
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <div style="margin-bottom: 12px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Bond Amount (CAS):</label>
                                <input type="number" id="trustBond" min="0.01" step="0.01" value="1.0" required
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <div style="margin-bottom: 15px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Reason (optional):</label>
                                <input type="text" id="trustReason" placeholder="Trustworthy trader"
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <button type="submit" style="width: 100%; padding: 10px; background: var(--primary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                                Add Trust Relation
                            </button>
                        </form>
                        <div id="trustResult" style="margin-top: 10px; font-size: 0.85rem;"></div>
                    </div>

                    <!-- Vote Reputation Form -->
                    <div style="padding: 15px; background: var(--bg-color); border-radius: 8px;">
                        <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                            ⭐ Vote Reputation
                            <span class="tooltip info-icon">?
                                <span class="tooltiptext">
                                    <strong>Reputation Voting</strong><br>
                                    Submit a bonded vote to rate a user's reputation.<br>
                                    • <strong>Positive votes</strong> (+1 to +100): Endorse trusted users<br>
                                    • <strong>Negative votes</strong> (-1 to -100): Warn about bad actors<br>
                                    • Higher bonds = more voting weight<br>
                                    • False votes can be slashed by the community
                                </span>
                            </span>
                        </h3>
                        <p class="help-text">Cast bonded votes to influence reputation scores. Your bond is at stake!</p>
                        <form id="voteRepForm" onsubmit="return handleVoteReputation(event);">
                            <div style="margin-bottom: 12px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Target Address:</label>
                                <input type="text" id="voteAddress" placeholder="QAddress..." required
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                            </div>
                            <div style="margin-bottom: 12px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Vote (-100 to +100):</label>
                                <input type="number" id="voteValue" min="-100" max="100" value="100" required
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <div style="margin-bottom: 12px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Bond Amount (CAS):</label>
                                <input type="number" id="voteBond" min="0.01" step="0.01" value="1.0" required
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <div style="margin-bottom: 15px;">
                                <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Reason (optional):</label>
                                <input type="text" id="voteReason" placeholder="Excellent trader"
                                       style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <button type="submit" style="width: 100%; padding: 10px; background: var(--secondary-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                                Submit Vote
                            </button>
                        </form>
                        <div id="voteResult" style="margin-top: 10px; font-size: 0.85rem;"></div>
                    </div>

                </div>
                
                <!-- Send Transaction Form -->
                <div style="margin-top: 20px; padding: 15px; background: var(--bg-color); border-radius: 8px;">
                    <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                        💸 Send CAS
                        <span class="tooltip info-icon">?
                            <span class="tooltiptext">
                                <strong>Send Cascoin</strong><br>
                                Transfer CAS to any address on the network.<br>
                                • Standard transaction fees apply<br>
                                • Optional comment for record-keeping<br>
                                • Transactions are irreversible once confirmed
                            </span>
                        </span>
                    </h3>
                    <p class="help-text">Send CAS to other users. Double-check the address before sending!</p>
                    <form id="sendForm" onsubmit="return handleSendTransaction(event);" style="display: grid; grid-template-columns: 1fr 1fr 1fr auto; gap: 10px; align-items: end;">
                        <div>
                            <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">To Address:</label>
                            <input type="text" id="sendAddress" placeholder="QAddress..." required
                                   style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.85rem;">
                        </div>
                        <div>
                            <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Amount (CAS):</label>
                            <input type="number" id="sendAmount" min="0.01" step="0.01" value="10" required
                                   style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                        </div>
                        <div>
                            <label style="display: block; font-size: 0.9rem; margin-bottom: 5px; color: var(--text-secondary);">Comment (optional):</label>
                            <input type="text" id="sendComment" placeholder="Payment"
                                   style="width: 100%; padding: 8px; border: 1px solid var(--border-color); border-radius: 4px;">
                        </div>
                        <button type="submit" style="padding: 10px 20px; background: var(--warning-color); color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: 600;">
                            Send
                        </button>
                    </form>
                    <div id="sendResult" style="margin-top: 10px; font-size: 0.85rem;"></div>
                </div>

                <!-- Negative Vote (Report/Dispute) -->
                <div style="margin-top: 20px; padding: 15px; background: var(--bg-color); border-radius: 8px;">
                    <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--danger-color);">
                        ⚠️ Report Bad Actor (Negative Vote)
                        <span class="tooltip info-icon">?
                            <span class="tooltiptext">
                                <strong>Report Bad Actors</strong><br>
                                Submit a negative reputation vote to warn the community.<br>
                                • <strong>Severity</strong>: Choose based on offense (-25 to -100)<br>
                                • <strong>Bond Required</strong>: Stake CAS to back your claim<br>
                                • <strong>Risk</strong>: False reports can be slashed by the community<br>
                                • <strong>Evidence</strong>: Always provide a clear reason<br><br>
                                Use this feature responsibly to protect others from scammers!
                            </span>
                        </span>
                    </h3>
                    <p class="help-text">⚠️ Warning: Only report genuine bad actors. False reports can result in your bond being slashed!</p>
                    <form id="reportForm" onsubmit="return handleReportBadActor(event);">
                        <div style="display: grid; grid-template-columns: 2fr 1fr 1fr 1fr auto; gap: 10px; align-items: end;">
                            <div>
                                <label style="display: block; font-size: 0.85rem; margin-bottom: 3px; color: var(--text-secondary);">Address to Report:</label>
                                <input type="text" id="reportAddress" placeholder="QAddress..." required
                                       style="width: 100%; padding: 6px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">
                            </div>
                            <div>
                                <label style="display: block; font-size: 0.85rem; margin-bottom: 3px; color: var(--text-secondary);">Severity:</label>
                                <select id="reportSeverity" required style="width: 100%; padding: 6px; border: 1px solid var(--border-color); border-radius: 4px; font-size: 0.85rem;">
                                    <option value="-100">Scammer (-100)</option>
                                    <option value="-75">Fraud (-75)</option>
                                    <option value="-50">Untrustwort hy (-50)</option>
                                    <option value="-25">Suspicious (-25)</option>
                                </select>
                            </div>
                            <div>
                                <label style="display: block; font-size: 0.85rem; margin-bottom: 3px; color: var(--text-secondary);">Bond (CAS):</label>
                                <input type="number" id="reportBond" min="0.01" step="0.01" value="1.0" required
                                       style="width: 100%; padding: 6px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <div>
                                <label style="display: block; font-size: 0.85rem; margin-bottom: 3px; color: var(--text-secondary);">Reason:</label>
                                <input type="text" id="reportReason" placeholder="Scammed me" required
                                       style="width: 100%; padding: 6px; border: 1px solid var(--border-color); border-radius: 4px;">
                            </div>
                            <button type="submit" style="padding: 7px 15px; background: var(--danger-color); color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: 600; white-space: nowrap;">
                                Report
                            </button>
                        </div>
                        <div id="reportResult" style="margin-top: 8px; font-size: 0.8rem;"></div>
                        <p style="margin-top: 8px; font-size: 0.75rem; color: var(--text-secondary);">
                            ⚠️ Report bad actors with negative votes. Your CAS will be bonded and can be slashed if the report is false.
                        </p>
                    </form>
                </div>

                <!-- Query & Check Actions -->
                <div style="margin-top: 20px; padding: 15px; background: var(--bg-color); border-radius: 8px;">
                    <h3 style="font-size: 1.1rem; margin-bottom: 15px; color: var(--primary-color);">
                        🔍 Query Information
                        <span class="tooltip info-icon">?
                            <span class="tooltiptext">
                                <strong>Query Tools</strong><br>
                                Look up information about any address on the network:<br>
                                • <strong>Reputation</strong>: Check basic reputation score (0-100)<br>
                                • <strong>HAT v2 Trust</strong>: View full multi-layer trust breakdown<br>
                                • <strong>Cluster Detection</strong>: Find Sybil attack patterns<br><br>
                                Use these tools before trading to assess counterparty risk!
                            </span>
                        </span>
                    </h3>
                    <p class="help-text">Look up reputation and trust scores for any address. Always check before trading!</p>
                    <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px;">
                        <!-- Get Reputation -->
                        <div>
                            <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">
                                Check Reputation:
                                <span class="tooltip info-icon" style="font-size: 0.6rem;">?
                                    <span class="tooltiptext">
                                        View the basic reputation score (0-100) calculated from community votes.
                                    </span>
                                </span>
                            </label>
                            <div style="display: flex; gap: 5px;">
                                <input type="text" id="checkRepAddress" placeholder="QAddress..."
                                       style="flex: 1; padding: 6px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">
                                <button onclick="handleCheckReputation()" style="padding: 6px 12px; background: var(--primary-color); color: white; border: none; border-radius: 4px; cursor: pointer; white-space: nowrap;">
                                    Check
                                </button>
                            </div>
                            <div id="repCheckResult" style="margin-top: 5px; font-size: 0.75rem; color: var(--text-secondary);"></div>
                        </div>

                        <!-- Get HAT v2 Trust -->
                        <div>
                            <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">
                                Check HAT v2 Trust:
                                <span class="tooltip info-icon" style="font-size: 0.6rem;">?
                                    <span class="tooltiptext">
                                        View the full HAT v2 trust breakdown with all four components: Behavior, Web-of-Trust, Economic, and Temporal.
                                    </span>
                                </span>
                            </label>
                            <div style="display: flex; gap: 5px;">
                                <input type="text" id="checkHATAddress" placeholder="QAddress..."
                                       style="flex: 1; padding: 6px; border: 1px solid var(--border-color); border-radius: 4px; font-family: monospace; font-size: 0.8rem;">
                                <button onclick="handleCheckHAT()" style="padding: 6px 12px; background: var(--secondary-color); color: white; border: none; border-radius: 4px; cursor: pointer; white-space: nowrap;">
                                    Check
                                </button>
                            </div>
                            <div id="hatCheckResult" style="margin-top: 5px; font-size: 0.75rem; color: var(--text-secondary);"></div>
                        </div>

                        <!-- Detect Clusters -->
                        <div>
                            <label style="display: block; font-size: 0.85rem; margin-bottom: 5px; color: var(--text-secondary);">
                                Detect Clusters:
                                <span class="tooltip info-icon" style="font-size: 0.6rem;">?
                                    <span class="tooltiptext">
                                        Scan the entire network for Sybil attack patterns. Shows clusters of interconnected accounts that may be controlled by a single entity.
                                    </span>
                                </span>
                            </label>
                            <button onclick="handleDetectClusters()" style="width: 100%; padding: 6px; background: var(--warning-color); color: white; border: none; border-radius: 4px; cursor: pointer;">
                                Run Cluster Detection
                            </button>
                            <div id="clusterResult" style="margin-top: 5px; font-size: 0.75rem; color: var(--text-secondary);"></div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-header">
                <h2>📊 Network Statistics
                    <span class="tooltip info-icon">?
                        <span class="tooltiptext">
                            <strong>Network Statistics</strong><br>
                            Overview of the entire Cascoin reputation network:<br>
                            • <strong>Trust Edges</strong>: Total trust relations between addresses<br>
                            • <strong>Votes</strong>: All reputation votes submitted<br>
                            • <strong>Disputes</strong>: Active negative votes / reports<br>
                            • <strong>Slashed</strong>: Votes penalized for being false<br>
                            • <strong>Bonds</strong>: Minimum stake requirements
                        </span>
                    </span>
                </h2>
            </div>
            <div class="card-body">
                <table class="stats-table">
                    <tbody>
                        <tr><td>Total Trust Edges</td><td id="totalEdges">--</td></tr>
                        <tr><td>Total Votes</td><td id="totalVotes">--</td></tr>
                        <tr><td>Active Disputes</td><td id="activeDisputes">--</td></tr>
                        <tr><td>Slashed Votes</td><td id="slashedVotes">--</td></tr>
                        <tr><td>Min Bond Amount</td><td id="minBond">--</td></tr>
                        <tr><td>Bond Per Vote Point</td><td id="bondPerPoint">--</td></tr>
                    </tbody>
                </table>
            </div>
        </div>

        <footer class="footer">
            <p>Cascoin CVM Dashboard v1.0 | <a href="https://cascoin.org" target="_blank">cascoin.org</a></p>
            <p class="text-muted">Last update: <span id="lastUpdate">Never</span></p>
        </footer>
    </div>

    <script>
// Cascoin CVM Dashboard JavaScript
class CVMDashboard {
    constructor() {
        this.rpcUrl = window.location.protocol + '//' + window.location.host;
        this.updateInterval = 5000;
        this.updateTimer = null;
        this.isConnected = false;
    }
    
    async init() {
        console.log('Initializing CVM Dashboard...');
        await this.loadData();
        this.startAutoRefresh();
        console.log('Dashboard initialized successfully');
    }
    
    startAutoRefresh() {
        if (this.updateTimer) clearInterval(this.updateTimer);
        this.updateTimer = setInterval(() => this.loadData(), this.updateInterval);
    }
    
    stopAutoRefresh() {
        if (this.updateTimer) { clearInterval(this.updateTimer); this.updateTimer = null; }
    }
    
    async rpcCall(method, params = []) {
        try {
            const response = await fetch(this.rpcUrl, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ jsonrpc: '2.0', id: 'dashboard-' + Date.now(), method: method, params: params })
            });
            if (!response.ok) throw new Error('HTTP ' + response.status);
            const data = await response.json();
            if (data.error) throw new Error(data.error.message || 'RPC Error');
            return data.result;
        } catch (error) {
            console.error('RPC Call failed:', method, error);
            throw error;
        }
    }
    
    async loadData() {
        try {
            this.updateConnectionStatus(true, 'Connected');
            await this.loadTrustGraphStats();
            await this.loadRecentActivity();
            await this.loadDisputes();
            document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
        } catch (error) {
            console.error('Failed to load data:', error);
            this.updateConnectionStatus(false, 'Connection Failed: ' + error.message);
        }
    }
    
    async loadRecentActivity() {
        try {
            const result = await this.rpcCall('listtrustrelations', [20]);
            console.log('Recent activity:', result);
            
            const activityDiv = document.getElementById('recentActivity');
            if (!result || !result.edges || result.edges.length === 0) {
                activityDiv.innerHTML = '<div class="activity-placeholder">No recent activity</div>';
                return;
            }
            
            // Sort by timestamp descending (newest first)
            const edges = result.edges.sort((a, b) => b.timestamp - a.timestamp);
            
            let html = '';
            edges.forEach(edge => {
                const date = new Date(edge.timestamp * 1000);
                const timeStr = date.toLocaleString();
                
                // Determine type and icon
                let typeLabel = '';
                let typeIcon = '';
                let typeClass = '';
                
                if (edge.weight >= 80) {
                    typeLabel = 'Trust Relation';
                    typeIcon = '🤝';
                    typeClass = 'activity-trust';
                } else if (edge.weight >= 50) {
                    typeLabel = 'Positive Vote';
                    typeIcon = '✅';
                    typeClass = 'activity-vote-pos';
                } else if (edge.weight > 0) {
                    typeLabel = 'Reputation Vote';
                    typeIcon = '⭐';
                    typeClass = 'activity-vote';
                } else {
                    typeLabel = 'Negative Report';
                    typeIcon = '⚠️';
                    typeClass = 'activity-report';
                }
                
                // Truncate addresses
                const fromShort = edge.from.substring(0, 8) + '...' + edge.from.substring(edge.from.length - 6);
                const toShort = edge.to.substring(0, 8) + '...' + edge.to.substring(edge.to.length - 6);
                
                // Show reason if available
                const reason = edge.reason ? `<br><span class="activity-reason">"${edge.reason}"</span>` : '';
                const slashed = edge.slashed ? ' <span class="badge badge-danger">SLASHED</span>' : '';
                
                html += `
                    <div class="activity-item ${typeClass}">
                        <div class="activity-icon">${typeIcon}</div>
                        <div class="activity-content">
                            <div class="activity-type">${typeLabel}${slashed}</div>
                            <div class="activity-desc">
                                <strong>${fromShort}</strong> → <strong>${toShort}</strong>
                                (Weight: ${edge.weight})
                                ${reason}
                            </div>
                            <div class="activity-time">${timeStr}</div>
                        </div>
                    </div>
                `;
            });
            
            activityDiv.innerHTML = html;
        } catch (error) {
            console.error('Failed to load recent activity:', error);
            document.getElementById('recentActivity').innerHTML = 
                '<div class="activity-placeholder">Failed to load activity</div>';
        }
    }
    
    async loadDisputes() {
        try {
            const result = await this.rpcCall('listdisputes', ['active']);
            console.log('Disputes:', result);
            
            const disputesDiv = document.getElementById('disputesList');
            if (!result || !result.disputes || result.disputes.length === 0) {
                disputesDiv.innerHTML = '<div class="activity-placeholder">✅ No active disputes - All votes are honest!</div>';
                return;
            }
            
            let html = '';
            result.disputes.forEach(dispute => {
                const totalStake = parseFloat(dispute.total_stake_support) + parseFloat(dispute.total_stake_oppose);
                const supportPercent = totalStake > 0 ? (parseFloat(dispute.total_stake_support) / totalStake * 100) : 0;
                const opposePercent = totalStake > 0 ? (parseFloat(dispute.total_stake_oppose) / totalStake * 100) : 0;
                
                const date = new Date(dispute.created_time * 1000).toLocaleString();
                const voteTxShort = dispute.original_vote_tx.substring(0, 10) + '...' + dispute.original_vote_tx.substring(dispute.original_vote_tx.length - 6);
                const disputeIdShort = dispute.dispute_id.substring(0, 10) + '...' + dispute.dispute_id.substring(dispute.dispute_id.length - 6);
                
                const resolvedBadge = dispute.resolved ? 
                    `<span class="badge ${dispute.slash_decision ? 'badge-danger' : 'badge-success'}">${dispute.slash_decision ? 'SLASHED' : 'KEPT'}</span>` : 
                    '<span class="badge badge-warning">ACTIVE</span>';
                
                html += `
                    <div class="dispute-card ${dispute.resolved ? 'resolved' : ''}" data-dispute-id="${dispute.dispute_id}">
                        <div class="dispute-header">
                            <div>
                                <div class="dispute-title">
                                    ⚖️ Dispute #${disputeIdShort}
                                    ${resolvedBadge}
                                </div>
                                <div class="dispute-meta">
                                    Vote TX: <code>${voteTxShort}</code><br>
                                    Challenger: ${dispute.challenger}<br>
                                    Created: ${date}
                                </div>
                            </div>
                        </div>
                        
                        ${dispute.challenge_reason ? `<div class="dispute-reason">"${dispute.challenge_reason}"</div>` : ''}
                        
                        <div class="dispute-stats">
                            <div class="dispute-stat">
                                <div class="dispute-stat-label">Challenge Bond</div>
                                <div class="dispute-stat-value">${dispute.challenge_bond} CAS</div>
                            </div>
                            <div class="dispute-stat">
                                <div class="dispute-stat-label">DAO Votes</div>
                                <div class="dispute-stat-value">${dispute.dao_votes}</div>
                            </div>
                            <div class="dispute-stat">
                                <div class="dispute-stat-label">Slash Support</div>
                                <div class="dispute-stat-value slash">${dispute.total_stake_support} CAS</div>
                            </div>
                            <div class="dispute-stat">
                                <div class="dispute-stat-label">Keep Support</div>
                                <div class="dispute-stat-value keep">${dispute.total_stake_oppose} CAS</div>
                            </div>
                        </div>
                        
                        <div class="dispute-progress">
                            <div class="dispute-progress-slash" style="width: ${supportPercent}%">
                                ${supportPercent > 15 ? supportPercent.toFixed(1) + '% Slash' : ''}
                            </div>
                            <div class="dispute-progress-keep" style="width: ${opposePercent}%">
                                ${opposePercent > 15 ? opposePercent.toFixed(1) + '% Keep' : ''}
                            </div>
                        </div>
                        
                        ${!dispute.resolved ? `
                        <div class="dispute-actions">
                            <button class="dispute-vote-btn slash" onclick="window.dashboard.voteOnDispute('${dispute.dispute_id}', true)">
                                ❌ Vote: SLASH (Remove Bond)
                            </button>
                            <button class="dispute-vote-btn keep" onclick="window.dashboard.voteOnDispute('${dispute.dispute_id}', false)">
                                ✅ Vote: KEEP (Return Bond)
                            </button>
                        </div>
                        <div style="text-align: center; margin-top: 8px;">
                            <button class="dispute-details-btn" onclick="window.dashboard.showDisputeDetails('${dispute.dispute_id}')">
                                📊 View All Votes
                            </button>
                        </div>
                        ` : ''}
                    </div>
                `;
            });
            
            disputesDiv.innerHTML = html;
        } catch (error) {
            console.error('Failed to load disputes:', error);
            document.getElementById('disputesList').innerHTML = 
                '<div class="activity-placeholder">Failed to load disputes</div>';
        }
    }
    
    async voteOnDispute(disputeId, supportSlash) {
        // Ask for wallet address
        const address = prompt(`Enter your Cascoin address to vote:\n\nThis address must be a DAO member with high reputation.\nYour stake will be locked during the dispute resolution.`);
        
        if (!address) return;
        
        // Ask for stake amount
        const stake = prompt(`How much CAS do you want to stake?\n\nYour stake determines your voting power.\nMinimum: 0.1 CAS\nRecommended: 1.0 - 5.0 CAS\n\nVoting: ${supportSlash ? 'SLASH (Remove Bond)' : 'KEEP (Return Bond)'}`, '1.0');
        
        if (!stake) return;
        
        const stakeAmount = parseFloat(stake);
        if (isNaN(stakeAmount) || stakeAmount < 0.1) {
            alert('Invalid stake amount. Minimum is 0.1 CAS');
            return;
        }
        
        try {
            const result = await this.rpcCall('votedispute', [disputeId, supportSlash, address, stakeAmount]);
            alert(`✅ Vote recorded successfully!\n\nDispute: ${result.dispute_id}\nYour Address: ${result.voter}\nYour Vote: ${supportSlash ? 'SLASH' : 'KEEP'}\nStake: ${result.stake} CAS`);
            await this.loadDisputes(); // Reload disputes
        } catch (error) {
            alert(`❌ Failed to vote: ${error.message}`);
        }
    }
    
    async showDisputeDetails(disputeId) {
        try {
            const dispute = await this.rpcCall('getdispute', [disputeId]);
            
            let votesHtml = '<h3>DAO Member Votes:</h3>';
            if (dispute.dao_votes && dispute.dao_votes.length > 0) {
                dispute.dao_votes.forEach(vote => {
                    const voteIcon = vote.support_slash ? '❌ SLASH' : '✅ KEEP';
                    const voteClass = vote.support_slash ? 'slash' : 'keep';
                    votesHtml += `
                        <div class="dispute-vote-item">
                            <span>${vote.dao_member.substring(0, 12)}...</span>
                            <span class="dispute-stat-value ${voteClass}">${voteIcon} (${vote.stake} CAS)</span>
                        </div>
                    `;
                });
            } else {
                votesHtml += '<p style="text-align: center; color: var(--text-secondary);">No votes yet</p>';
            }
            
            alert(`Dispute Details:\n\nDispute ID: ${dispute.dispute_id}\nOriginal Vote: ${dispute.original_vote_tx}\nChallenger: ${dispute.challenger}\nChallenge Bond: ${dispute.challenge_bond} CAS\n\nDAO Votes: ${dispute.dao_votes ? dispute.dao_votes.length : 0}\nSlash Support: ${dispute.total_stake_support} CAS\nKeep Support: ${dispute.total_stake_oppose} CAS\n\n${dispute.resolved ? 'Status: RESOLVED\nDecision: ' + (dispute.slash_decision ? 'SLASHED' : 'KEPT') : 'Status: ACTIVE - Voting in progress'}`);
        } catch (error) {
            alert(`Failed to load dispute details: ${error.message}`);
        }
    }
    
    async loadTrustGraphStats() {
        try {
            const stats = await this.rpcCall('gettrustgraphstats');
            console.log('Trust Graph Stats:', stats);
            
            document.getElementById('trustCount').textContent = Math.floor(stats.total_trust_edges / 2);
            document.getElementById('voteCount').textContent = stats.total_votes;
            document.getElementById('networkSize').textContent = stats.total_trust_edges + stats.total_votes;
            document.getElementById('totalEdges').textContent = stats.total_trust_edges;
            document.getElementById('totalVotes').textContent = stats.total_votes;
            document.getElementById('activeDisputes').textContent = stats.active_disputes;
            document.getElementById('slashedVotes').textContent = stats.slashed_votes;
            document.getElementById('minBond').textContent = stats.min_bond_amount + ' CAS';
            document.getElementById('bondPerPoint').textContent = stats.bond_per_vote_point + ' CAS';
            
            try {
                const addresses = await this.rpcCall('listreceivedbyaddress', [0, true]);
                if (addresses && addresses.length > 0) {
                    const myAddr = addresses[0].address;
                    
                    // Fetch HAT v2 trust score
                    try {
                        const trust = await this.rpcCall('getsecuretrust', [myAddr]);
                        const score = trust.trust_score || 0;
                        document.getElementById('myReputation').textContent = Math.round(score);
                        document.getElementById('repProgress').style.width = score + '%';
                        
                        // Fetch detailed breakdown
                        const breakdown = await this.rpcCall('gettrustbreakdown', [myAddr]);
                        this.updateHATBreakdown(breakdown);
                    } catch (hatError) {
                        console.log('HAT v2 not available, falling back to old reputation:', hatError.message);
                        // Fallback to old reputation system
                        const rep = await this.rpcCall('getreputation', [myAddr]);
                        const score = rep.average_score || 0;
                        document.getElementById('myReputation').textContent = Math.round(score);
                        document.getElementById('repProgress').style.width = score + '%';
                    }
                }
            } catch (e) {
                document.getElementById('myReputation').textContent = 'N/A';
                console.log('Could not fetch reputation:', e.message);
            }
        } catch (error) {
            console.error('Failed to load trust graph stats:', error);
            throw error;
        }
    }
    
    updateHATBreakdown(breakdown) {
        try {
            // Behavior Component (40%)
            const behaviorScore = (breakdown.behavior.secure_score * 100).toFixed(1);
            document.getElementById('hatBehavior').textContent = behaviorScore + '%';
            document.getElementById('hatBehaviorBase').textContent = (breakdown.behavior.base * 100).toFixed(1) + '%';
            document.getElementById('hatDiversity').textContent = (breakdown.behavior.diversity_penalty * 100).toFixed(1) + '%';
            document.getElementById('hatVolume').textContent = (breakdown.behavior.volume_penalty * 100).toFixed(1) + '%';
            document.getElementById('hatPattern').textContent = (breakdown.behavior.pattern_penalty * 100).toFixed(1) + '%';
            
            // Web-of-Trust Component (30%)
            const wotScore = (breakdown.wot.secure_score * 100).toFixed(1);
            document.getElementById('hatWot').textContent = wotScore + '%';
            document.getElementById('hatCluster').textContent = (breakdown.wot.cluster_penalty * 100).toFixed(1) + '%';
            document.getElementById('hatCentrality').textContent = breakdown.wot.centrality_bonus.toFixed(2) + 'x';
            
            // Economic Component (20%)
            const economicScore = (breakdown.economic.secure_score * 100).toFixed(1);
            document.getElementById('hatEconomic').textContent = economicScore + '%';
            document.getElementById('hatStakeTime').textContent = breakdown.economic.stake_time_weight.toFixed(2) + 'x';
            
            // Temporal Component (10%)
            const temporalScore = (breakdown.temporal.secure_score * 100).toFixed(1);
            document.getElementById('hatTemporal').textContent = temporalScore + '%';
            document.getElementById('hatActivity').textContent = (breakdown.temporal.activity_penalty * 100).toFixed(1) + '%';
            
            // Final Score
            document.getElementById('hatFinal').textContent = breakdown.final_score;
            
            console.log('HAT v2 breakdown updated:', breakdown);
        } catch (error) {
            console.error('Error updating HAT breakdown:', error);
            // Set N/A on error
            document.getElementById('hatBehavior').textContent = 'N/A';
            document.getElementById('hatWot').textContent = 'N/A';
            document.getElementById('hatEconomic').textContent = 'N/A';
            document.getElementById('hatTemporal').textContent = 'N/A';
            document.getElementById('hatFinal').textContent = 'N/A';
        }
    }
    
    updateConnectionStatus(connected, message) {
        this.isConnected = connected;
        const indicator = document.querySelector('.status-indicator');
        const statusText = document.querySelector('.status-text');
        if (connected) {
            indicator.classList.add('connected');
            statusText.textContent = message || 'Connected';
        } else {
            indicator.classList.remove('connected');
            statusText.textContent = message || 'Disconnected';
        }
    }
}

// Trust Graph Visualization (Beautiful, lightweight, no D3.js!)
class TrustGraphViz {
    constructor(id, w = 800, h = 400) {
        this.el = document.getElementById(id);
        this.w = w; this.h = h;
        this.nodes = []; this.links = [];
        this.hoveredNode = null;
    }
    init() {
        this.el.innerHTML = '';
        this.el.style.position = 'relative';
        
        // Create SVG with gradient background
        this.svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        this.svg.setAttribute('width', this.w);
        this.svg.setAttribute('height', this.h);
        this.svg.style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';
        this.svg.style.borderRadius = '12px';
        this.svg.style.boxShadow = '0 10px 30px rgba(0,0,0,0.3)';
        
        // Add glow filter for nodes
        const defs = document.createElementNS('http://www.w3.org/2000/svg', 'defs');
        const filter = document.createElementNS('http://www.w3.org/2000/svg', 'filter');
        filter.setAttribute('id', 'glow');
        filter.innerHTML = `
            <feGaussianBlur stdDeviation="4" result="coloredBlur"/>
            <feMerge>
                <feMergeNode in="coloredBlur"/>
                <feMergeNode in="SourceGraphic"/>
            </feMerge>
        `;
        defs.appendChild(filter);
        this.svg.appendChild(defs);
        
        this.el.appendChild(this.svg);
        
        // Add zoom & pan support
        this.zoom = { scale: 1, x: 0, y: 0 };
        this.isPanning = false;
        this.lastPanX = 0;
        this.lastPanY = 0;
        
        this.svg.addEventListener('wheel', (e) => {
            e.preventDefault();
            const delta = e.deltaY > 0 ? 0.9 : 1.1;
            this.zoom.scale = Math.max(0.3, Math.min(3, this.zoom.scale * delta));
            this.applyZoom();
        });
        
        this.svg.addEventListener('mousedown', (e) => {
            if (e.button === 0) { // Left click
                this.isPanning = true;
                this.lastPanX = e.clientX;
                this.lastPanY = e.clientY;
                this.svg.style.cursor = 'grabbing';
            }
        });
        
        document.addEventListener('mousemove', (e) => {
            if (this.isPanning) {
                const dx = e.clientX - this.lastPanX;
                const dy = e.clientY - this.lastPanY;
                this.zoom.x += dx;
                this.zoom.y += dy;
                this.lastPanX = e.clientX;
                this.lastPanY = e.clientY;
                this.applyZoom();
            }
        });
        
        document.addEventListener('mouseup', () => {
            this.isPanning = false;
            this.svg.style.cursor = 'default';
        });
        
        // Create tooltip
        this.tooltip = document.createElement('div');
        this.tooltip.style.cssText = `
            position: absolute;
            background: rgba(0, 0, 0, 0.95);
            color: white;
            padding: 12px 16px;
            border-radius: 8px;
            font-size: 13px;
            pointer-events: none;
            opacity: 0;
            transition: opacity 0.3s ease;
            box-shadow: 0 4px 12px rgba(0,0,0,0.5);
            z-index: 1000;
            backdrop-filter: blur(10px);
        `;
        this.el.appendChild(this.tooltip);
        
        // Create Trust Detail Modal
        this.modal = document.createElement('div');
        this.modal.style.cssText = `
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(0, 0, 0, 0.8);
            display: none;
            align-items: center;
            justify-content: center;
            z-index: 2000;
            backdrop-filter: blur(10px);
        `;
        this.modal.onclick = (e) => {
            if (e.target === this.modal) this.hideModal();
        };
        
        this.modalContent = document.createElement('div');
        this.modalContent.style.cssText = `
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            border-radius: 16px;
            padding: 0;
            max-width: 500px;
            width: 90%;
            max-height: 80vh;
            overflow-y: auto;
            box-shadow: 0 20px 60px rgba(0,0,0,0.5);
            position: relative;
        `;
        
        this.modal.appendChild(this.modalContent);
        document.body.appendChild(this.modal);
    }
    setData(nodes, links) {
        this.nodes = nodes.map((n, i) => ({...n, x: Math.random() * this.w, y: Math.random() * this.h, vx: 0, vy: 0, i}));
        this.links = links.map(l => ({...l, 
            source: typeof l.source === 'object' ? l.source : this.nodes.find(n => n.id === l.source),
            target: typeof l.target === 'object' ? l.target : this.nodes.find(n => n.id === l.target)
        }));
    }
    render() {
        if (!this.svg) this.init();
        
        // Clear ALL children except defs
        const defs = this.svg.querySelector('defs');
        const children = Array.from(this.svg.children);
        children.forEach(child => {
            if (child !== defs) {
                this.svg.removeChild(child);
            }
        });
        
        // Create main group for zoom/pan
        const mainGroup = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        mainGroup.setAttribute('id', 'main-group');
        
        const lg = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        const ng = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        
        // Draw links with labels
        this.links.forEach((l, idx) => {
            if (!l.source || !l.target) return;
            
            // Determine edge type, color and label
            const edgeType = l.type || 'trust';
            const edgeLabel = l.label || '';
            let edgeColor = 'rgba(255, 255, 255, 0.4)';
            let displayLabel = edgeLabel;
            
            // Set color based on type
            if (edgeType === 'trust') {
                edgeColor = 'rgba(16, 185, 129, 0.6)'; // Green
                if (!displayLabel) displayLabel = '🤝 Trust';
            } else if (edgeType === 'vote_positive') {
                edgeColor = 'rgba(34, 197, 94, 0.6)'; // Bright green
                if (!displayLabel) displayLabel = '✅ Vote';
            } else if (edgeType === 'vote') {
                edgeColor = 'rgba(59, 130, 246, 0.6)'; // Blue
                if (!displayLabel) displayLabel = '⭐ Vote';
            } else if (edgeType === 'negative') {
                edgeColor = 'rgba(239, 68, 68, 0.6)'; // Red
                if (!displayLabel) displayLabel = '⚠️ Report';
            }
            
            // Show slashed status
            if (l.slashed) {
                displayLabel += ' ❌';
                edgeColor = 'rgba(127, 29, 29, 0.8)'; // Dark red
            }
            
            // Add count if multiple
            if (l.count && l.count > 1) {
                displayLabel += ` (×${l.count})`;
            }
            
            // Draw line
            const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
            line.setAttribute('x1', l.source.x); 
            line.setAttribute('y1', l.source.y);
            line.setAttribute('x2', l.target.x); 
            line.setAttribute('y2', l.target.y);
            line.setAttribute('stroke', edgeColor);
            line.setAttribute('stroke-width', Math.max(2, Math.sqrt(Math.abs(l.weight) / 10) || 2));
            line.setAttribute('stroke-linecap', 'round');
            line.style.transition = 'all 0.3s ease';
            line.style.cursor = 'pointer';
            line.setAttribute('data-label', displayLabel);
            lg.appendChild(line);
            
            // Draw label on hover
            line.onmouseenter = (e) => {
                line.setAttribute('stroke-width', Math.max(4, Math.sqrt(Math.abs(l.weight) / 10) * 1.5));
                line.setAttribute('stroke', edgeColor.replace('0.6', '1.0'));
                
                // Show tooltip-like label
                this.showEdgeLabel(displayLabel, e);
            };
            
            line.onmouseleave = () => {
                line.setAttribute('stroke-width', Math.max(2, Math.sqrt(Math.abs(l.weight) / 10) || 2));
                line.setAttribute('stroke', edgeColor);
                this.hideTooltip();
            };
        });
        
        // Draw nodes with beautiful effects
        this.nodes.forEach((n, i) => {
            const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
            g.style.cursor = 'pointer';
            g.style.transition = 'all 0.3s ease';
            
            // Calculate node size based on cluster size
            const clusterSize = n.cluster_size || 1;
            const baseRadius = 22;
            const radius = baseRadius + Math.min(clusterSize - 1, 10) * 2; // Max +20px for large clusters
            const glowRadius = radius + 3;
            
            // Outer glow circle
            const glow = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            glow.setAttribute('cx', n.x); glow.setAttribute('cy', n.y);
            glow.setAttribute('r', glowRadius);
            glow.setAttribute('fill', this.getColor(n.rep || 50));
            glow.setAttribute('opacity', '0.3');
            glow.setAttribute('filter', 'url(#glow)');
            
            // Main circle
            const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            c.setAttribute('cx', n.x); c.setAttribute('cy', n.y);
            c.setAttribute('r', radius);
            c.setAttribute('fill', this.getColor(n.rep || 50));
            c.setAttribute('stroke', '#ffffff');
            c.setAttribute('stroke-width', '3');
            c.style.transition = 'all 0.3s ease';
            c.style.filter = 'drop-shadow(0 4px 8px rgba(0,0,0,0.3))';
            
            // Add cluster size indicator for multi-address clusters
            if (clusterSize > 1) {
                c.setAttribute('stroke', '#fbbf24'); // Gold border for clusters
                c.setAttribute('stroke-width', '4');
            }
            
            // Inner highlight
            const highlight = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            highlight.setAttribute('cx', n.x - 5); highlight.setAttribute('cy', n.y - 5);
            highlight.setAttribute('r', 6);
            highlight.setAttribute('fill', 'rgba(255, 255, 255, 0.4)');
            
            // Label with shadow
            const t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            t.setAttribute('x', n.x); t.setAttribute('y', n.y + 40);
            t.setAttribute('text-anchor', 'middle');
            t.setAttribute('font-size', '12');
            t.setAttribute('font-weight', 'bold');
            t.setAttribute('fill', '#ffffff');
            t.style.textShadow = '0 2px 4px rgba(0,0,0,0.5)';
            t.textContent = n.label || n.id.substring(0, 8) + '...';
            
            // Click handler - Show Trust Detail Modal
            g.onclick = (e) => {
                e.stopPropagation();
                this.showModal(n);
            };
            
            // Hover effects with dynamic radius
            g.onmouseenter = (e) => {
                c.setAttribute('r', radius + 6);
                c.setAttribute('stroke-width', clusterSize > 1 ? '5' : '4');
                glow.setAttribute('r', glowRadius + 10);
                glow.setAttribute('opacity', '0.6');
                this.showTooltip(n, e);
                this.highlightConnections(n);
            };
            g.onmouseleave = () => {
                c.setAttribute('r', radius);
                c.setAttribute('stroke-width', clusterSize > 1 ? '4' : '3');
                glow.setAttribute('r', glowRadius);
                glow.setAttribute('opacity', '0.3');
                this.hideTooltip();
                this.unhighlightConnections();
            };
            
            g.appendChild(glow);
            g.appendChild(c);
            g.appendChild(highlight);
            g.appendChild(t);
            ng.appendChild(g);
        });
        
        mainGroup.appendChild(lg);
        mainGroup.appendChild(ng);
        this.svg.appendChild(mainGroup);
        this.applyZoom();
    }
    
    applyZoom() {
        const mainGroup = this.svg.querySelector('#main-group');
        if (mainGroup) {
            mainGroup.setAttribute('transform', 
                `translate(${this.zoom.x}, ${this.zoom.y}) scale(${this.zoom.scale})`);
        }
    }
    
    highlightConnections(node) {
        const lines = this.svg.querySelectorAll('line');
        lines.forEach(line => {
            const x1 = parseFloat(line.getAttribute('x1'));
            const y1 = parseFloat(line.getAttribute('y1'));
            const x2 = parseFloat(line.getAttribute('x2'));
            const y2 = parseFloat(line.getAttribute('y2'));
            if ((Math.abs(x1 - node.x) < 1 && Math.abs(y1 - node.y) < 1) ||
                (Math.abs(x2 - node.x) < 1 && Math.abs(y2 - node.y) < 1)) {
                line.setAttribute('stroke', 'rgba(255, 255, 255, 0.9)');
                line.setAttribute('stroke-width', '4');
            }
        });
    }
    
    unhighlightConnections() {
        const lines = this.svg.querySelectorAll('line');
        lines.forEach(line => {
            line.setAttribute('stroke', 'rgba(255, 255, 255, 0.4)');
            const weight = parseFloat(line.getAttribute('data-weight')) || 80;
            line.setAttribute('stroke-width', Math.max(2, Math.sqrt(weight / 10)));
        });
    }
    
    showTooltip(node, event) {
        const connections = this.links.filter(l => 
            (l.source && l.source.id === node.id) || (l.target && l.target.id === node.id)
        ).length;
        
        const clusterSize = node.cluster_size || 1;
        const isCluster = clusterSize > 1;
        const penaltyWarning = node.penalty_applied ? '<div style="color: #fca5a5;">⚠️ Penalty Applied</div>' : '';
        
        this.tooltip.innerHTML = `
            <div style="font-weight: bold; margin-bottom: 6px; font-size: 14px;">
                ${isCluster ? '🏦' : '👤'} ${node.label || node.id.substring(0, 12) + '...'}
            </div>
            <div style="color: #a0aec0; font-size: 12px;">
                <div>💎 Effective Trust: <span style="color: ${this.getColor(node.rep || 50)}; font-weight: bold;">${Math.round(node.rep || 50)}</span></div>
                ${isCluster ? `<div>📦 Cluster Size: <span style="color: #fbbf24; font-weight: bold;">${clusterSize} addresses</span></div>` : ''}
                <div>🔗 Connections: <span style="color: #63b3ed; font-weight: bold;">${connections}</span></div>
                ${penaltyWarning}
                <div style="margin-top: 4px; padding-top: 4px; border-top: 1px solid rgba(255,255,255,0.1); font-size: 10px; color: #718096;">
                    Click for details
                </div>
            </div>
        `;
        this.tooltip.style.opacity = '1';
        this.tooltip.style.left = (event.pageX + 15) + 'px';
        this.tooltip.style.top = (event.pageY - 15) + 'px';
    }
    
    hideTooltip() {
        this.tooltip.style.opacity = '0';
    }
    
    showEdgeLabel(label, event) {
        this.tooltip.innerHTML = `<strong>${label}</strong>`;
        this.tooltip.style.opacity = '1';
        this.tooltip.style.left = (event.pageX + 15) + 'px';
        this.tooltip.style.top = (event.pageY - 15) + 'px';
    }
    
    showModal(node) {
        // Get connections
        const outgoing = this.links.filter(l => l.source && l.source.id === node.id);
        const incoming = this.links.filter(l => l.target && l.target.id === node.id);
        const totalConnections = outgoing.length + incoming.length;
        
        // Calculate average trust given/received
        const avgTrustGiven = outgoing.length > 0 
            ? Math.round(outgoing.reduce((sum, l) => sum + (l.weight || 0), 0) / outgoing.length)
            : 0;
        const avgTrustReceived = incoming.length > 0
            ? Math.round(incoming.reduce((sum, l) => sum + (l.weight || 0), 0) / incoming.length)
            : 0;
        
        // Generate cluster members section
        const clusterSize = node.cluster_size || 1;
        const isCluster = clusterSize > 1;
        const members = node.members || [node.id];
        const individualScores = node.individual_scores || {};
        
        let clusterMembersHTML = '';
        if (isCluster) {
            clusterMembersHTML = `
                <div style="background: rgba(255, 255, 255, 0.1); border-radius: 12px; padding: 16px; margin-bottom: 16px;">
                    <div style="color: rgba(255, 255, 255, 0.9); font-size: 14px; font-weight: bold; margin-bottom: 12px;">
                        📦 Wallet Cluster (${clusterSize} addresses)
                    </div>
                    ${node.penalty_applied ? `
                        <div style="background: rgba(239, 68, 68, 0.2); border-left: 3px solid #ef4444; padding: 8px 12px; margin-bottom: 12px; border-radius: 4px;">
                            <span style="color: #fca5a5; font-size: 12px;">
                                ⚠️ Cluster penalty applied! Effective score lowered by worst address.
                            </span>
                        </div>
                    ` : ''}
                    <div style="max-height: 200px; overflow-y: auto;">
                        ${members.map(addr => {
                            const score = individualScores[addr] || 50;
                            const isWorst = node.worst_address === addr;
                            const shortAddr = addr.substring(0, 24) + '...';
                            return `
                                <div style="
                                    background: ${isWorst ? 'rgba(239, 68, 68, 0.15)' : 'rgba(0, 0, 0, 0.2)'};
                                    padding: 10px;
                                    margin-bottom: 8px;
                                    border-radius: 8px;
                                    border-left: 3px solid ${this.getColor(score)};
                                    display: flex;
                                    justify-content: space-between;
                                    align-items: center;
                                ">
                                    <div>
                                        <div style="color: rgba(255, 255, 255, 0.9); font-family: monospace; font-size: 11px; margin-bottom: 2px;">
                                            ${shortAddr} ${isWorst ? '<span style="color: #fca5a5;">⚠️ WORST</span>' : ''}
                                        </div>
                                    </div>
                                    <div style="
                                        background: ${this.getColor(score)};
                                        color: white;
                                        padding: 4px 12px;
                                        border-radius: 12px;
                                        font-weight: bold;
                                        font-size: 13px;
                                    ">${Math.round(score)}</div>
                                </div>
                            `;
                        }).join('')}
                    </div>
                </div>
            `;
        }
        
        this.modalContent.innerHTML = `
            <div style="background: rgba(0, 0, 0, 0.3); padding: 24px; border-radius: 16px 16px 0 0;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
                    <h2 style="color: white; margin: 0; font-size: 24px; font-weight: bold;">
                        ${isCluster ? '🏦 Wallet Cluster' : '👤 User'}
                    </h2>
                    <button onclick="window.trustGraph.hideModal()" style="
                        background: rgba(255, 255, 255, 0.2);
                        border: none;
                        color: white;
                        width: 32px;
                        height: 32px;
                        border-radius: 8px;
                        cursor: pointer;
                        font-size: 20px;
                        font-weight: bold;
                    ">×</button>
                </div>
                <div style="color: rgba(255, 255, 255, 0.8); font-size: 13px; font-family: monospace;">
                    Cluster ID: ${node.id.substring(0, 40)}...
                </div>
            </div>
            
            <div style="padding: 24px; background: rgba(0, 0, 0, 0.2);">
                <!-- Reputation Score -->
                <div style="background: rgba(255, 255, 255, 0.1); border-radius: 12px; padding: 20px; margin-bottom: 16px;">
                    <div style="color: rgba(255, 255, 255, 0.7); font-size: 12px; text-transform: uppercase; margin-bottom: 8px;">
                        💎 Reputation Score
                    </div>
                    <div style="display: flex; align-items: center; gap: 16px;">
                        <div style="
                            font-size: 48px;
                            font-weight: bold;
                            color: ${this.getColor(node.rep || 50)};
                            text-shadow: 0 0 20px ${this.getColor(node.rep || 50)}80;
                        ">${node.rep || 50}</div>
                        <div style="flex: 1;">
                            <div style="background: rgba(0, 0, 0, 0.3); height: 12px; border-radius: 6px; overflow: hidden;">
                                <div style="
                                    background: ${this.getColor(node.rep || 50)};
                                    height: 100%;
                                    width: ${node.rep || 50}%;
                                    transition: width 0.5s ease;
                                    box-shadow: 0 0 10px ${this.getColor(node.rep || 50)};
                                "></div>
                            </div>
                            <div style="color: rgba(255, 255, 255, 0.6); font-size: 11px; margin-top: 4px;">
                                ${this.getReputationLabel(node.rep || 50)}
                            </div>
                        </div>
                    </div>
                </div>
                
                <!-- Wallet Cluster Members -->
                ${clusterMembersHTML}
                
                <!-- Trust Statistics -->
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 16px;">
                    <div style="background: rgba(255, 255, 255, 0.1); border-radius: 12px; padding: 16px;">
                        <div style="color: rgba(255, 255, 255, 0.7); font-size: 11px; margin-bottom: 4px;">
                            🔗 Total Connections
                        </div>
                        <div style="color: white; font-size: 28px; font-weight: bold;">
                            ${totalConnections}
                        </div>
                    </div>
                    <div style="background: rgba(255, 255, 255, 0.1); border-radius: 12px; padding: 16px;">
                        <div style="color: rgba(255, 255, 255, 0.7); font-size: 11px; margin-bottom: 4px;">
                            ⚡ Network Position
                        </div>
                        <div style="color: white; font-size: 28px; font-weight: bold;">
                            ${this.getNetworkPosition(totalConnections)}
                        </div>
                    </div>
                </div>
                
                <!-- Trust Given -->
                <div style="background: rgba(16, 185, 129, 0.2); border-radius: 12px; padding: 16px; margin-bottom: 12px; border-left: 4px solid #10b981;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
                        <div style="color: #10b981; font-weight: bold; font-size: 14px;">
                            ➡️ Trust Given (${outgoing.length})
                        </div>
                        <div style="color: #10b981; font-size: 18px; font-weight: bold;">
                            ${avgTrustGiven}%
                        </div>
                    </div>
                    ${outgoing.length > 0 ? outgoing.map(l => `
                        <div style="
                            background: rgba(255, 255, 255, 0.1);
                            padding: 8px 12px;
                            border-radius: 6px;
                            margin-bottom: 6px;
                            display: flex;
                            justify-content: space-between;
                            align-items: center;
                        ">
                            <span style="color: white; font-size: 12px;">
                                ${l.target ? (l.target.label || l.target.id.substring(0, 12) + '...') : 'Unknown'}
                            </span>
                            <span style="color: #10b981; font-weight: bold; font-size: 14px;">
                                ${l.weight || 0}%
                            </span>
                        </div>
                    `).join('') : '<div style="color: rgba(255, 255, 255, 0.5); font-size: 12px; text-align: center; padding: 8px;">No outgoing trust</div>'}
                </div>
                
                <!-- Trust Received -->
                <div style="background: rgba(59, 130, 246, 0.2); border-radius: 12px; padding: 16px; border-left: 4px solid #3b82f6;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
                        <div style="color: #3b82f6; font-weight: bold; font-size: 14px;">
                            ⬅️ Trust Received (${incoming.length})
                        </div>
                        <div style="color: #3b82f6; font-size: 18px; font-weight: bold;">
                            ${avgTrustReceived}%
                        </div>
                    </div>
                    ${incoming.length > 0 ? incoming.map(l => `
                        <div style="
                            background: rgba(255, 255, 255, 0.1);
                            padding: 8px 12px;
                            border-radius: 6px;
                            margin-bottom: 6px;
                            display: flex;
                            justify-content: space-between;
                            align-items: center;
                        ">
                            <span style="color: white; font-size: 12px;">
                                ${l.source ? (l.source.label || l.source.id.substring(0, 12) + '...') : 'Unknown'}
                            </span>
                            <span style="color: #3b82f6; font-weight: bold; font-size: 14px;">
                                ${l.weight || 0}%
                            </span>
                        </div>
                    `).join('') : '<div style="color: rgba(255, 255, 255, 0.5); font-size: 12px; text-align: center; padding: 8px;">No incoming trust</div>'}
                </div>
            </div>
        `;
        
        this.modal.style.display = 'flex';
        setTimeout(() => {
            this.modal.style.opacity = '1';
        }, 10);
    }
    
    hideModal() {
        this.modal.style.opacity = '0';
        setTimeout(() => {
            this.modal.style.display = 'none';
        }, 300);
    }
    
    getReputationLabel(rep) {
        if (rep < 25) return '🔴 Low Reputation';
        if (rep < 50) return '🟠 Fair Reputation';
        if (rep < 75) return '🟡 Good Reputation';
        if (rep < 90) return '🟢 Excellent Reputation';
        return '💎 Outstanding Reputation';
    }
    
    getNetworkPosition(connections) {
        if (connections === 0) return '🌱 New';
        if (connections < 3) return '👤 Member';
        if (connections < 6) return '⭐ Active';
        return '💫 Hub';
    }
    simulate(iters = 50) {
        for (let i = 0; i < iters; i++) {
            const cx = this.w / 2, cy = this.h / 2;
            this.nodes.forEach(n => {
                n.vx += (cx - n.x) * 0.01; n.vy += (cy - n.y) * 0.01;
            });
            for (let j = 0; j < this.nodes.length; j++) {
                for (let k = j + 1; k < this.nodes.length; k++) {
                    const a = this.nodes[j], b = this.nodes[k];
                    const dx = b.x - a.x, dy = b.y - a.y, d = Math.sqrt(dx*dx + dy*dy) || 1;
                    if (d < 100) {
                        const f = (100 - d) * 0.5, fx = (dx / d) * f, fy = (dy / d) * f;
                        a.vx -= fx; a.vy -= fy; b.vx += fx; b.vy += fy;
                    }
                }
            }
            this.links.forEach(l => {
                if (!l.source || !l.target) return;
                const dx = l.target.x - l.source.x, dy = l.target.y - l.source.y;
                const d = Math.sqrt(dx*dx + dy*dy) || 1, f = (d - 100) * 0.1;
                const fx = (dx / d) * f, fy = (dy / d) * f;
                l.source.vx += fx; l.source.vy += fy;
                l.target.vx -= fx; l.target.vy -= fy;
            });
            this.nodes.forEach(n => {
                n.x += n.vx; n.y += n.vy; n.vx *= 0.8; n.vy *= 0.8;
                n.x = Math.max(30, Math.min(this.w - 30, n.x));
                n.y = Math.max(30, Math.min(this.h - 30, n.y));
            });
            if (i % 10 === 0) setTimeout(() => this.render(), i * 10);
        }
        setTimeout(() => this.render(), iters * 10);
    }
    getColor(rep) {
        // Beautiful color gradient: Red -> Orange -> Yellow -> Green -> Cyan
        if (rep < 25) {
            // Red to Orange
            const t = rep / 25;
            return `rgb(${239}, ${Math.round(68 + 90 * t)}, 68)`;
        } else if (rep < 50) {
            // Orange to Yellow
            const t = (rep - 25) / 25;
            return `rgb(${Math.round(239 + 6 * t)}, ${Math.round(158 + 40 * t)}, 68)`;
        } else if (rep < 75) {
            // Yellow to Green
            const t = (rep - 50) / 25;
            return `rgb(${Math.round(245 - 129 * t)}, ${Math.round(198 - 13 * t)}, ${Math.round(68 + 61 * t)})`;
        } else {
            // Green to Cyan (highest reputation)
            const t = (rep - 75) / 25;
            return `rgb(${Math.round(116 - 100 * t)}, ${Math.round(185 + 50 * t)}, ${Math.round(129 + 50 * t)})`;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Form Handlers
// ═══════════════════════════════════════════════════════════════

async function handleAddTrust(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('trustResult');
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">⏳ Processing...</span>';
    
    try {
        const address = document.getElementById('trustAddress').value;
        const weight = parseInt(document.getElementById('trustWeight').value);
        const bond = parseFloat(document.getElementById('trustBond').value);
        const reason = document.getElementById('trustReason').value;
        
        const result = await window.dashboard.rpcCall('sendtrustrelation', [address, weight, bond, reason]);
        
        resultDiv.innerHTML = `<span style="color: var(--secondary-color);">✓ Trust added! TX: ${result.txid.substring(0, 10)}...</span>`;
        document.getElementById('addTrustForm').reset();
        
        // Refresh data
        setTimeout(() => window.dashboard.loadData(), 2000);
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">✗ Error: ${error.message}</span>`;
    }
    
    return false;
}

async function handleVoteReputation(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('voteResult');
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">⏳ Processing...</span>';
    
    try {
        const address = document.getElementById('voteAddress').value;
        const vote = parseInt(document.getElementById('voteValue').value);
        const bond = parseFloat(document.getElementById('voteBond').value);
        const reason = document.getElementById('voteReason').value;
        
        const result = await window.dashboard.rpcCall('sendbondedvote', [address, vote, bond, reason]);
        
        resultDiv.innerHTML = `<span style="color: var(--secondary-color);">✓ Vote submitted! TX: ${result.txid.substring(0, 10)}...</span>`;
        document.getElementById('voteRepForm').reset();
        
        setTimeout(() => window.dashboard.loadData(), 2000);
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">✗ Error: ${error.message}</span>`;
    }
    
    return false;
}

async function handleSendTransaction(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('sendResult');
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">⏳ Sending...</span>';
    
    try {
        const address = document.getElementById('sendAddress').value;
        const amount = parseFloat(document.getElementById('sendAmount').value);
        const comment = document.getElementById('sendComment').value;
        
        const result = await window.dashboard.rpcCall('sendtoaddress', [address, amount, comment]);
        
        resultDiv.innerHTML = `<span style="color: var(--secondary-color);">✓ Sent! TX: ${result.substring(0, 10)}...</span>`;
        document.getElementById('sendForm').reset();
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">✗ Error: ${error.message}</span>`;
    }
    
    return false;
}

async function handleReportBadActor(event) {
    event.preventDefault();
    const resultDiv = document.getElementById('reportResult');
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">⏳ Submitting report...</span>';
    
    try {
        const address = document.getElementById('reportAddress').value;
        const vote = parseInt(document.getElementById('reportSeverity').value); // Negative value
        const bond = parseFloat(document.getElementById('reportBond').value);
        const reason = document.getElementById('reportReason').value;
        
        // Use sendbondedvote with negative value to report
        const result = await window.dashboard.rpcCall('sendbondedvote', [address, vote, bond, reason]);
        
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">✓ Report submitted! TX: ${result.txid.substring(0, 10)}... (Vote: ${vote})</span>`;
        document.getElementById('reportForm').reset();
        
        setTimeout(() => window.dashboard.loadData(), 2000);
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">✗ Error: ${error.message}</span>`;
    }
    
    return false;
}

async function handleCheckReputation() {
    const address = document.getElementById('checkRepAddress').value;
    const resultDiv = document.getElementById('repCheckResult');
    
    if (!address) {
        resultDiv.innerHTML = '<span style="color: var(--danger-color);">Enter an address</span>';
        return;
    }
    
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">Checking...</span>';
    
    try {
        const result = await window.dashboard.rpcCall('getreputation', [address]);
        resultDiv.innerHTML = `<span style="color: var(--secondary-color);">Score: ${result.average_score}/100</span>`;
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">Error: ${error.message}</span>`;
    }
}

async function handleCheckHAT() {
    const address = document.getElementById('checkHATAddress').value;
    const resultDiv = document.getElementById('hatCheckResult');
    
    if (!address) {
        resultDiv.innerHTML = '<span style="color: var(--danger-color);">Enter an address</span>';
        return;
    }
    
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">Checking...</span>';
    
    try {
        const result = await window.dashboard.rpcCall('getsecuretrust', [address]);
        resultDiv.innerHTML = `<span style="color: var(--secondary-color);">HAT v2: ${result.trust_score}/100</span>`;
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">Error: ${error.message}</span>`;
    }
}

async function handleDetectClusters() {
    const resultDiv = document.getElementById('clusterResult');
    resultDiv.innerHTML = '<span style="color: var(--warning-color);">Analyzing network...</span>';
    
    try {
        const result = await window.dashboard.rpcCall('detectclusters', []);
        const count = result.count || 0;
        
        if (count === 0) {
            resultDiv.innerHTML = '<span style="color: var(--secondary-color);">✓ No suspicious clusters</span>';
        } else {
            resultDiv.innerHTML = `<span style="color: var(--danger-color);">⚠ Found ${count} suspicious addresses</span>`;
        }
    } catch (error) {
        resultDiv.innerHTML = `<span style="color: var(--danger-color);">Error: ${error.message}</span>`;
    }
}

// ═══════════════════════════════════════════════════════════════
// Graph Refresh
// ═══════════════════════════════════════════════════════════════

function refreshGraph() {
    if (window.dashboard) window.dashboard.loadData();
    if (window.loadRealTrustGraph) {
        console.log('Refreshing wallet cluster graph...');
        window.loadRealTrustGraph();
    }
}

function refreshDisputes() {
    if (window.dashboard) {
        console.log('Refreshing disputes...');
        window.dashboard.loadDisputes();
    }
}

document.addEventListener('DOMContentLoaded', () => {
    console.log('DOM loaded, initializing dashboard...');
    window.dashboard = new CVMDashboard();
    window.dashboard.init();
    
    // Initialize trust graph
    window.trustGraph = new TrustGraphViz('trustGraph', 750, 380);
    window.trustGraph.init();
    
    // Load REAL trust graph data with WALLET CLUSTERING (Optimized)
    async function loadRealTrustGraph() {
        try {
            console.log('Loading trust graph with wallet clustering...');
            const result = await window.dashboard.rpcCall('listtrustrelations', [100]);
            
            if (!result.edges || result.edges.length === 0) {
                console.log('No trust relationships found yet');
                const emptyNodes = [{id: 'empty', rep: 50, label: 'No Data Yet', cluster_size: 0}];
                const emptyLinks = [];
                window.trustGraph.setData(emptyNodes, emptyLinks);
                window.trustGraph.render();
                setTimeout(() => window.trustGraph.simulate(10), 100);
                return;
            }
            
            console.log('Building wallet clusters from', result.edges.length, 'edges...');
            
            // 1. Get all unique addresses
            const addresses = new Set();
            result.edges.forEach(edge => {
                addresses.add(edge.from);
                addresses.add(edge.to);
            });
            
            console.log(`Found ${addresses.size} unique addresses, fetching cluster info...`);
            
            // 2. Get wallet cluster info for each address (OPTIMIZED: reduced calls)
            const clusterMap = new Map(); // cluster_id -> {id, members[], rep, effectiveScore}
            const addressToCluster = new Map(); // address -> cluster_id
            const processedClusters = new Set(); // Track which clusters we've already processed
            
            for (const addr of addresses) {
                try {
                    // Only fetch cluster info if we haven't processed this cluster yet
                    const clusterInfo = await window.dashboard.rpcCall('getwalletcluster', [addr]);
                    const cluster_id = clusterInfo.cluster_id;
                    
                    addressToCluster.set(addr, cluster_id);
                    
                    // Skip if we already processed this cluster
                    if (processedClusters.has(cluster_id)) {
                        continue;
                    }
                    
                    processedClusters.add(cluster_id);
                    
                    // Use shared_hat_score from cluster info (already calculated)
                    const cluster_size = clusterInfo.member_count || 1;
                    const rep = clusterInfo.shared_hat_score || clusterInfo.shared_reputation || 50;
                    
                    clusterMap.set(cluster_id, {
                        id: cluster_id,
                        members: clusterInfo.members || [addr],
                        cluster_size: cluster_size,
                        rep: rep,
                        individual_scores: {},
                        penalty_applied: cluster_size > 1 && rep < 50, // Assume penalty if multi-address cluster with low score
                        worst_address: addr
                    });
                    
                    console.log(`Cluster ${cluster_id.substring(0, 8)}...: ${cluster_size} addresses, score ${rep}`);
                    
                } catch (error) {
                    console.log(`Address ${addr.substring(0, 8)}... clustering failed, treating as standalone`);
                    // If clustering fails, treat address as standalone
                    addressToCluster.set(addr, addr);
                    if (!clusterMap.has(addr)) {
                        clusterMap.set(addr, {
                            id: addr,
                            members: [addr],
                            cluster_size: 1,
                            rep: result.reputations[addr] || 50,
                            individual_scores: {[addr]: result.reputations[addr] || 50},
                            penalty_applied: false
                        });
                    }
                }
            }
            
            // 3. Build nodes (one per wallet cluster)
            const nodes = Array.from(clusterMap.values()).map(cluster => {
                const shortId = cluster.id.substring(0, 6) + '...';
                const sizeLabel = cluster.cluster_size > 1 ? ` (${cluster.cluster_size})` : '';
                return {
                    id: cluster.id,
                    rep: cluster.rep,
                    label: shortId + sizeLabel,
                    cluster_size: cluster.cluster_size,
                    members: cluster.members,
                    individual_scores: cluster.individual_scores,
                    penalty_applied: cluster.penalty_applied,
                    worst_address: cluster.worst_address
                };
            });
            
            // 4. Build edges (between clusters, not individual addresses)
            const clusterEdges = new Map();
            
            result.edges.forEach(edge => {
                const fromCluster = addressToCluster.get(edge.from) || edge.from;
                const toCluster = addressToCluster.get(edge.to) || edge.to;
                
                // Skip edges within same cluster
                if (fromCluster === toCluster) return;
                
                const edgeKey = `${fromCluster}->${toCluster}`;
                
                // Determine edge type and label from weight and reason
                let edgeType = 'trust';
                let edgeLabel = edge.reason || '';
                
                if (edge.weight >= 80) {
                    edgeType = 'trust';
                    edgeLabel = edgeLabel || 'Trust Relation';
                } else if (edge.weight >= 50) {
                    edgeType = 'vote_positive';
                    edgeLabel = edgeLabel || 'Positive Vote';
                } else if (edge.weight > 0) {
                    edgeType = 'vote';
                    edgeLabel = edgeLabel || 'Reputation Vote';
                } else {
                    edgeType = 'negative';
                    edgeLabel = edgeLabel || 'Negative Report';
                }
                
                if (!clusterEdges.has(edgeKey)) {
                    clusterEdges.set(edgeKey, {
                        source: fromCluster,
                        target: toCluster,
                        weight: edge.weight,
                        count: 1,
                        type: edgeType,
                        label: edgeLabel,
                        slashed: edge.slashed || false
                    });
                } else {
                    // Aggregate multiple edges between same clusters
                    const existing = clusterEdges.get(edgeKey);
                    existing.weight = Math.max(existing.weight, edge.weight);
                    existing.count++;
                    if (!existing.label && edgeLabel) {
                        existing.label = edgeLabel;
                    }
                }
            });
            
            const links = Array.from(clusterEdges.values());
            
            console.log(`✅ Loaded ${nodes.length} wallet clusters (${addresses.size} addresses) and ${links.length} inter-cluster edges`);
            
            window.trustGraph.setData(nodes, links);
            window.trustGraph.render();
            setTimeout(() => window.trustGraph.simulate(50), 500);
            
        } catch (error) {
            console.error('Failed to load clustered trust graph:', error);
            console.error('Error details:', error.message);
            // Fallback to empty state
            const emptyNodes = [{id: 'error', rep: 50, label: 'Error Loading', cluster_size: 0}];
            const emptyLinks = [];
            window.trustGraph.setData(emptyNodes, emptyLinks);
            window.trustGraph.render();
            setTimeout(() => window.trustGraph.simulate(10), 100);
        }
    }
    
    // Make loadRealTrustGraph globally accessible for refresh
    window.loadRealTrustGraph = loadRealTrustGraph;
    
    // Start loading real data
    loadRealTrustGraph();
    
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            console.log('Page hidden, stopping updates');
            window.dashboard.stopAutoRefresh();
        } else {
            console.log('Page visible, resuming updates');
            window.dashboard.startAutoRefresh();
        }
    });
});
    </script>
</body>
</html>
)HTML");

} // namespace CVMDashboardHTML

#endif // CASCOIN_HTTPSERVER_CVMDASHBOARD_HTML_H

