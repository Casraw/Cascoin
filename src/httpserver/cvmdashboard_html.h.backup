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
.activity-list { display: flex; flex-direction: column; gap: 12px; }
.activity-item { padding: 12px; background: var(--bg-color); border-radius: 8px; border-left: 3px solid var(--primary-color); }
.activity-placeholder { text-align: center; padding: 40px; color: var(--text-secondary); }
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
                    <h3>My Reputation</h3>
                    <div class="stat-value" id="myReputation">--</div>
                    <div class="stat-progress"><div class="stat-progress-bar" id="repProgress"></div></div>
                    <p class="stat-label">out of 100</p>
                </div>
            </div>
            <div class="stat-card">
                <div class="stat-icon">🤝</div>
                <div class="stat-content">
                    <h3>Trust Relations</h3>
                    <div class="stat-value" id="trustCount">--</div>
                    <p class="stat-label">Active connections</p>
                </div>
            </div>
            <div class="stat-card">
                <div class="stat-icon">🗳️</div>
                <div class="stat-content">
                    <h3>Votes Submitted</h3>
                    <div class="stat-value" id="voteCount">--</div>
                    <p class="stat-label">Reputation votes</p>
                </div>
            </div>
            <div class="stat-card">
                <div class="stat-icon">🌐</div>
                <div class="stat-content">
                    <h3>Network Size</h3>
                    <div class="stat-value" id="networkSize">--</div>
                    <p class="stat-label">Total addresses</p>
                </div>
            </div>
        </div>

        <div class="content-grid">
            <div class="card large">
                <div class="card-header">
                    <h2>🌐 Trust Network Graph</h2>
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
                <div class="card-header"><h2>📜 Recent Activity</h2></div>
                <div class="card-body">
                    <div id="recentActivity" class="activity-list">
                        <div class="activity-placeholder">Loading recent transactions...</div>
                    </div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-header"><h2>📊 Network Statistics</h2></div>
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
            document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
        } catch (error) {
            console.error('Failed to load data:', error);
            this.updateConnectionStatus(false, 'Connection Failed: ' + error.message);
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
                    const rep = await this.rpcCall('getreputation', [myAddr]);
                    const score = rep.average_score || 0;
                    document.getElementById('myReputation').textContent = Math.round(score);
                    document.getElementById('repProgress').style.width = score + '%';
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

function refreshGraph() {
    if (window.dashboard) window.dashboard.loadData();
}

document.addEventListener('DOMContentLoaded', () => {
    console.log('DOM loaded, initializing dashboard...');
    window.dashboard = new CVMDashboard();
    window.dashboard.init();
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

