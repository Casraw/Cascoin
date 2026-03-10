// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_dashboard.cpp
 * @brief L2 Web Dashboard HTTP handlers implementation
 * 
 * Requirements: 25.4, 33.8, 39.1, 39.2, 39.3
 */

#include <httpserver/l2_dashboard.h>
#include <httpserver/l2_dashboard_html.h>
#include <httpserver.h>
#include <rpc/protocol.h>
#include <util.h>
#include <utilstrencodings.h>
#include <chainparamsbase.h>
#include <amount.h>

#include <l2/l2_common.h>
#include <l2/state_manager.h>
#include <l2/sequencer_discovery.h>
#include <l2/bridge_contract.h>
#include <l2/security_monitor.h>
#include <l2/l2_block.h>
#include <l2/leader_election.h>

#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

namespace l2 {

// Cache for dashboard HTML
static std::string g_l2DashboardHTML;
static bool g_l2DashboardInitialized = false;

// Mock data storage for demonstration (in production, these would come from actual L2 components)
static std::vector<L2Block> g_recentBlocks;
static uint64_t g_totalTransactions = 0;
static uint64_t g_currentBlockHeight = 0;

std::string FormatCAS(int64_t satoshis) {
    double cas = static_cast<double>(satoshis) / COIN;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8) << cas << " CAS";
    return ss.str();
}

std::string FormatTimestamp(uint64_t timestamp) {
    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm* tm = std::gmtime(&time);
    if (!tm) return "Invalid";
    
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buffer) + " UTC";
}

std::string FormatHash(const std::string& hash, size_t length) {
    if (hash.length() <= length) return hash;
    size_t half = (length - 3) / 2;
    return hash.substr(0, half + 2) + "..." + hash.substr(hash.length() - half);
}

// Helper to escape JSON strings
static std::string EscapeJSON(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (c < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

// Get current timestamp
static uint64_t GetCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GetL2StatusJSON() {
    std::ostringstream json;
    uint64_t currentTime = GetCurrentTimestamp();
    
    // Get data from L2 components if available
    uint64_t chainId = DEFAULT_L2_CHAIN_ID;
    uint64_t blockHeight = g_currentBlockHeight;
    std::string stateRoot = "0x0000000000000000000000000000000000000000000000000000000000000000";
    double tps = 0.0;
    uint64_t gasUsed = 0;
    uint64_t gasLimit = 30000000;
    bool isHealthy = true;
    std::string healthStatus = "healthy";
    uint64_t sequencerCount = 0;
    uint64_t eligibleSequencers = 0;
    
    // Try to get real data from sequencer discovery
    if (IsSequencerDiscoveryInitialized()) {
        SequencerDiscovery& discovery = GetSequencerDiscovery();
        chainId = discovery.GetChainId();
        sequencerCount = discovery.GetSequencerCount();
        eligibleSequencers = discovery.GetEligibleCount();
    }
    
    json << "{\n";
    json << "  \"chainId\": " << chainId << ",\n";
    json << "  \"blockHeight\": " << blockHeight << ",\n";
    json << "  \"stateRoot\": \"" << stateRoot << "\",\n";
    json << "  \"timestamp\": " << currentTime << ",\n";
    json << "  \"tps\": " << std::fixed << std::setprecision(2) << tps << ",\n";
    json << "  \"gasUsed\": " << gasUsed << ",\n";
    json << "  \"gasLimit\": " << gasLimit << ",\n";
    json << "  \"gasUtilization\": " << std::fixed << std::setprecision(2) 
         << (gasLimit > 0 ? (static_cast<double>(gasUsed) / gasLimit * 100) : 0) << ",\n";
    json << "  \"sequencerCount\": " << sequencerCount << ",\n";
    json << "  \"eligibleSequencers\": " << eligibleSequencers << ",\n";
    json << "  \"isHealthy\": " << (isHealthy ? "true" : "false") << ",\n";
    json << "  \"healthStatus\": \"" << healthStatus << "\",\n";
    json << "  \"uptime\": 100.0,\n";
    json << "  \"version\": \"" << L2_PROTOCOL_VERSION << "\"\n";
    json << "}";
    
    return json.str();
}

std::string GetSequencersJSON() {
    std::ostringstream json;
    json << "{\n";
    json << "  \"sequencers\": [\n";
    
    if (IsSequencerDiscoveryInitialized()) {
        SequencerDiscovery& discovery = GetSequencerDiscovery();
        std::vector<SequencerInfo> sequencers = discovery.GetAllSequencers();
        
        for (size_t i = 0; i < sequencers.size(); ++i) {
            const SequencerInfo& seq = sequencers[i];
            json << "    {\n";
            json << "      \"address\": \"" << seq.address.GetHex() << "\",\n";
            json << "      \"stake\": " << seq.verifiedStake << ",\n";
            json << "      \"stakeFormatted\": \"" << FormatCAS(seq.verifiedStake) << "\",\n";
            json << "      \"hatScore\": " << seq.verifiedHatScore << ",\n";
            json << "      \"peerCount\": " << seq.peerCount << ",\n";
            json << "      \"blocksProduced\": " << seq.blocksProduced << ",\n";
            json << "      \"blocksMissed\": " << seq.blocksMissed << ",\n";
            json << "      \"uptime\": " << std::fixed << std::setprecision(2) << seq.GetUptimePercent() << ",\n";
            json << "      \"isEligible\": " << (seq.isEligible ? "true" : "false") << ",\n";
            json << "      \"isVerified\": " << (seq.isVerified ? "true" : "false") << ",\n";
            json << "      \"lastAnnouncement\": " << seq.lastAnnouncement << ",\n";
            json << "      \"lastAnnouncementFormatted\": \"" << FormatTimestamp(seq.lastAnnouncement) << "\",\n";
            json << "      \"weight\": " << seq.GetWeight() << "\n";
            json << "    }";
            if (i < sequencers.size() - 1) json << ",";
            json << "\n";
        }
    }
    
    json << "  ],\n";
    json << "  \"currentLeader\": null,\n";
    json << "  \"totalSequencers\": " << (IsSequencerDiscoveryInitialized() ? 
        GetSequencerDiscovery().GetSequencerCount() : 0) << ",\n";
    json << "  \"eligibleCount\": " << (IsSequencerDiscoveryInitialized() ? 
        GetSequencerDiscovery().GetEligibleCount() : 0) << "\n";
    json << "}";
    
    return json.str();
}

std::string GetBlocksJSON(size_t limit) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"blocks\": [\n";
    
    // Return recent blocks (limited)
    size_t count = std::min(limit, g_recentBlocks.size());
    for (size_t i = 0; i < count; ++i) {
        const L2Block& block = g_recentBlocks[g_recentBlocks.size() - 1 - i];
        json << "    {\n";
        json << "      \"number\": " << block.GetBlockNumber() << ",\n";
        json << "      \"hash\": \"" << block.GetHash().GetHex() << "\",\n";
        json << "      \"parentHash\": \"" << block.header.parentHash.GetHex() << "\",\n";
        json << "      \"stateRoot\": \"" << block.GetStateRoot().GetHex() << "\",\n";
        json << "      \"transactionsRoot\": \"" << block.GetTransactionsRoot().GetHex() << "\",\n";
        json << "      \"sequencer\": \"" << block.GetSequencer().GetHex() << "\",\n";
        json << "      \"timestamp\": " << block.GetTimestamp() << ",\n";
        json << "      \"timestampFormatted\": \"" << FormatTimestamp(block.GetTimestamp()) << "\",\n";
        json << "      \"gasLimit\": " << block.header.gasLimit << ",\n";
        json << "      \"gasUsed\": " << block.header.gasUsed << ",\n";
        json << "      \"transactionCount\": " << block.GetTransactionCount() << ",\n";
        json << "      \"signatureCount\": " << block.GetSignatureCount() << ",\n";
        json << "      \"isFinalized\": " << (block.isFinalized ? "true" : "false") << "\n";
        json << "    }";
        if (i < count - 1) json << ",";
        json << "\n";
    }
    
    json << "  ],\n";
    json << "  \"latestBlock\": " << g_currentBlockHeight << ",\n";
    json << "  \"totalBlocks\": " << g_recentBlocks.size() << "\n";
    json << "}";
    
    return json.str();
}

std::string GetStatsJSON() {
    std::ostringstream json;
    uint64_t currentTime = GetCurrentTimestamp();
    
    CAmount tvl = 0;
    uint64_t pendingWithdrawals = 0;
    uint64_t totalDeposits = 0;
    uint64_t totalWithdrawals = 0;
    
    json << "{\n";
    json << "  \"timestamp\": " << currentTime << ",\n";
    json << "  \"totalTransactions\": " << g_totalTransactions << ",\n";
    json << "  \"totalBlocks\": " << g_currentBlockHeight << ",\n";
    json << "  \"averageTps\": 0.0,\n";
    json << "  \"averageGasPrice\": 1000000000,\n";
    json << "  \"tvl\": " << tvl << ",\n";
    json << "  \"tvlFormatted\": \"" << FormatCAS(tvl) << "\",\n";
    json << "  \"pendingWithdrawals\": " << pendingWithdrawals << ",\n";
    json << "  \"totalDeposits\": " << totalDeposits << ",\n";
    json << "  \"totalWithdrawals\": " << totalWithdrawals << ",\n";
    json << "  \"l2ChainId\": " << DEFAULT_L2_CHAIN_ID << ",\n";
    json << "  \"protocolVersion\": " << L2_PROTOCOL_VERSION << "\n";
    json << "}";
    
    return json.str();
}

std::string GetTransactionsJSON(size_t limit) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"transactions\": [],\n";
    json << "  \"total\": " << g_totalTransactions << ",\n";
    json << "  \"limit\": " << limit << "\n";
    json << "}";
    
    return json.str();
}

std::string GetWithdrawalsJSON() {
    std::ostringstream json;
    json << "{\n";
    json << "  \"withdrawals\": [],\n";
    json << "  \"pendingCount\": 0,\n";
    json << "  \"challengedCount\": 0,\n";
    json << "  \"readyCount\": 0\n";
    json << "}";
    
    return json.str();
}

std::string GetAlertsJSON() {
    std::ostringstream json;
    json << "{\n";
    json << "  \"alerts\": [],\n";
    json << "  \"activeCount\": 0,\n";
    json << "  \"criticalCount\": 0,\n";
    json << "  \"warningCount\": 0\n";
    json << "}";
    
    return json.str();
}

// HTTP Handlers

bool L2StatusHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetL2StatusJSON());
    return true;
}

bool L2SequencersHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetSequencersJSON());
    return true;
}

bool L2BlocksHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    // Parse limit from query string if present
    size_t limit = 10;
    std::string uri = req->GetURI();
    size_t queryPos = uri.find('?');
    if (queryPos != std::string::npos) {
        std::string query = uri.substr(queryPos + 1);
        size_t limitPos = query.find("limit=");
        if (limitPos != std::string::npos) {
            try {
                limit = std::stoul(query.substr(limitPos + 6));
                if (limit > 100) limit = 100;  // Cap at 100
            } catch (...) {
                limit = 10;
            }
        }
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetBlocksJSON(limit));
    return true;
}

bool L2ApiStatsHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetStatsJSON());
    return true;
}

bool L2ApiSequencersHandler(HTTPRequest* req, const std::string& strReq) {
    return L2SequencersHandler(req, strReq);
}

bool L2ApiTransactionsHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    size_t limit = 20;
    std::string uri = req->GetURI();
    size_t queryPos = uri.find('?');
    if (queryPos != std::string::npos) {
        std::string query = uri.substr(queryPos + 1);
        size_t limitPos = query.find("limit=");
        if (limitPos != std::string::npos) {
            try {
                limit = std::stoul(query.substr(limitPos + 6));
                if (limit > 100) limit = 100;
            } catch (...) {
                limit = 20;
            }
        }
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetTransactionsJSON(limit));
    return true;
}

bool L2ApiWithdrawalsHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetWithdrawalsJSON());
    return true;
}

bool L2ApiAlertsHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetAlertsJSON());
    return true;
}

std::string BuildL2DashboardHTML() {
    return L2DashboardHTML::INDEX_HTML;
}

bool L2DashboardRequestHandler(HTTPRequest* req, const std::string& strReq) {
    LogPrint(BCLog::HTTP, "L2 Dashboard: Request for %s\n", strReq);
    
    // Only allow GET requests
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    // Build dashboard HTML on first request (lazy initialization)
    if (!g_l2DashboardInitialized) {
        g_l2DashboardHTML = BuildL2DashboardHTML();
        g_l2DashboardInitialized = true;
        LogPrint(BCLog::HTTP, "L2 Dashboard: Built HTML (%d bytes)\n", 
                 g_l2DashboardHTML.length());
    }
    
    // Serve embedded HTML page
    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    req->WriteHeader("Pragma", "no-cache");
    req->WriteHeader("Expires", "0");
    req->WriteReply(HTTP_OK, g_l2DashboardHTML);
    
    LogPrint(BCLog::HTTP, "L2 Dashboard: Served HTML (%d bytes)\n", 
             g_l2DashboardHTML.length());
    
    return true;
}

void InitL2DashboardHandlers() {
    LogPrintf("Initializing L2 Dashboard handlers...\n");
    
    // Register main dashboard handler
    RegisterHTTPHandler("/l2", false, L2DashboardRequestHandler);
    RegisterHTTPHandler("/l2/dashboard", false, L2DashboardRequestHandler);
    
    // Register API endpoints
    RegisterHTTPHandler("/l2/status", true, L2StatusHandler);
    RegisterHTTPHandler("/l2/sequencers", true, L2SequencersHandler);
    RegisterHTTPHandler("/l2/blocks", true, L2BlocksHandler);
    RegisterHTTPHandler("/l2/api/stats", true, L2ApiStatsHandler);
    RegisterHTTPHandler("/l2/api/sequencers", true, L2ApiSequencersHandler);
    RegisterHTTPHandler("/l2/api/transactions", true, L2ApiTransactionsHandler);
    RegisterHTTPHandler("/l2/api/withdrawals", true, L2ApiWithdrawalsHandler);
    RegisterHTTPHandler("/l2/api/alerts", true, L2ApiAlertsHandler);
    
    try {
        LogPrintf("L2 Dashboard available at http://localhost:%d/l2/\n", 
                  gArgs.GetArg("-rpcport", BaseParams().RPCPort()));
    } catch (const std::exception& e) {
        LogPrintf("L2 Dashboard: Could not get RPC port: %s\n", e.what());
    }
}

} // namespace l2
