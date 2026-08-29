// Cascoin CVM Dashboard JavaScript

class CVMDashboard {
    constructor() {
        // RPC Configuration
        this.rpcUrl = 'http://localhost:' + (this.detectRPCPort() || '8332');
        this.rpcUser = 'cascoinrpc';
        this.rpcPassword = this.getRPCPassword();
        
        // Update interval (5 seconds)
        this.updateInterval = 5000;
        this.updateTimer = null;
        
        // Connection status
        this.isConnected = false;
    }
    
    detectRPCPort() {
        // Try to detect port from URL or use default
        const urlParams = new URLSearchParams(window.location.search);
        return urlParams.get('rpcport') || '8332';
    }
    
    getRPCPassword() {
        // Try to get from localStorage
        let password = localStorage.getItem('cascoin_rpc_password');
        
        // If not found, try reading .cookie file via RPC
        // For now, we'll handle auth errors gracefully
        return password || '';
    }
    
    async init() {
        console.log('Initializing CVM Dashboard...');
        
        // Initial load
        await this.loadData();
        
        // Start auto-refresh
        this.startAutoRefresh();
        
        console.log('Dashboard initialized successfully');
    }
    
    startAutoRefresh() {
        if (this.updateTimer) {
            clearInterval(this.updateTimer);
        }
        
        this.updateTimer = setInterval(() => {
            this.loadData();
        }, this.updateInterval);
    }
    
    stopAutoRefresh() {
        if (this.updateTimer) {
            clearInterval(this.updateTimer);
            this.updateTimer = null;
        }
    }
    
    async rpcCall(method, params = []) {
        try {
            const response = await fetch(this.rpcUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'Authorization': 'Basic ' + btoa(this.rpcUser + ':' + this.rpcPassword)
                },
                body: JSON.stringify({
                    jsonrpc: '2.0',
                    id: 'dashboard-' + Date.now(),
                    method: method,
                    params: params
                })
            });
            
            if (!response.ok) {
                throw new Error('HTTP ' + response.status);
            }
            
            const data = await response.json();
            
            if (data.error) {
                throw new Error(data.error.message || 'RPC Error');
            }
            
            return data.result;
            
        } catch (error) {
            console.error('RPC Call failed:', method, error);
            throw error;
        }
    }
    
    async loadData() {
        try {
            // Update connection status
            this.updateConnectionStatus(true, 'Connected');
            
            // Load trust graph stats
            await this.loadTrustGraphStats();
            
            // Update timestamp
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
            
            // Update stat cards
            document.getElementById('trustCount').textContent = 
                Math.floor(stats.total_trust_edges / 2); // Divide by 2 (forward+reverse)
            
            document.getElementById('voteCount').textContent = stats.total_votes;
            
            document.getElementById('networkSize').textContent = 
                stats.total_trust_edges + stats.total_votes;
            
            // Update statistics table
            document.getElementById('totalEdges').textContent = stats.total_trust_edges;
            document.getElementById('totalVotes').textContent = stats.total_votes;
            document.getElementById('activeDisputes').textContent = stats.active_disputes;
            document.getElementById('slashedVotes').textContent = stats.slashed_votes;
            document.getElementById('minBond').textContent = stats.min_bond_amount + ' CAS';
            document.getElementById('bondPerPoint').textContent = stats.bond_per_vote_point + ' CAS';
            
            // Try to get my reputation
            try {
                // Get a wallet address
                const addresses = await this.rpcCall('listreceivedbyaddress', [0, true]);
                
                if (addresses && addresses.length > 0) {
                    const myAddr = addresses[0].address;
                    const rep = await this.rpcCall('getreputation', [myAddr]);
                    
                    const score = rep.average_score || 0;
                    document.getElementById('myReputation').textContent = Math.round(score);
                    
                    // Update progress bar
                    const progressBar = document.getElementById('repProgress');
                    progressBar.style.width = score + '%';
                }
            } catch (e) {
                // No addresses or reputation not available
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

// Global functions
function refreshGraph() {
    if (window.dashboard) {
        window.dashboard.loadData();
    }
}

// Initialize dashboard when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    console.log('DOM loaded, initializing dashboard...');
    
    window.dashboard = new CVMDashboard();
    window.dashboard.init();
    
    // Handle page visibility (pause updates when tab not visible)
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

