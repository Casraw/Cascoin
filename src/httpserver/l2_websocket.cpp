// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_websocket.cpp
 * @brief WebSocket/SSE Server implementation for L2 Dashboard
 * 
 * Requirements: 33.1, 25.4
 */

#include <httpserver/l2_websocket.h>
#include <httpserver.h>
#include <netaddress.h>
#include <util.h>

#include <event2/http.h>

#include <sstream>
#include <iomanip>
#include <chrono>

namespace l2 {

// ============================================================================
// WSMessage Implementation
// ============================================================================

std::string WSMessage::ToJSON() const {
    std::ostringstream json;
    json << "{";
    json << "\"type\":" << static_cast<int>(type) << ",";
    json << "\"timestamp\":" << timestamp << ",";
    json << "\"data\":" << payload;
    json << "}";
    return json.str();
}

// ============================================================================
// L2WebSocketServer Implementation
// ============================================================================

L2WebSocketServer::L2WebSocketServer()
    : running_(false)
    , nextClientId_(1)
{
}

L2WebSocketServer::~L2WebSocketServer() {
    Shutdown();
}

L2WebSocketServer& L2WebSocketServer::GetInstance() {
    static L2WebSocketServer instance;
    return instance;
}

bool L2WebSocketServer::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_.load()) {
        return true;  // Already running
    }
    
    running_.store(true);
    LogPrintf("L2 WebSocket/SSE Server initialized\n");
    return true;
}

void L2WebSocketServer::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    clients_.clear();
    messageQueues_.clear();
    
    LogPrintf("L2 WebSocket/SSE Server shutdown\n");
}

uint64_t L2WebSocketServer::GetTimestamp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void L2WebSocketServer::BroadcastMessage(const WSMessage& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add message to all client queues
    for (const auto& client : clients_) {
        if (client.connected) {
            messageQueues_[client.id].push_back(msg);
            
            // Limit queue size to prevent memory issues
            if (messageQueues_[client.id].size() > 100) {
                messageQueues_[client.id].erase(messageQueues_[client.id].begin());
            }
        }
    }
}

void L2WebSocketServer::BroadcastNewBlock(
    uint64_t blockNumber,
    const std::string& blockHash,
    size_t txCount,
    uint64_t gasUsed,
    const std::string& sequencer)
{
    std::ostringstream payload;
    payload << "{";
    payload << "\"blockNumber\":" << blockNumber << ",";
    payload << "\"blockHash\":\"" << blockHash << "\",";
    payload << "\"txCount\":" << txCount << ",";
    payload << "\"gasUsed\":" << gasUsed << ",";
    payload << "\"sequencer\":\"" << sequencer << "\"";
    payload << "}";
    
    WSMessage msg(WSMessageType::NEW_BLOCK, payload.str(), GetTimestamp());
    BroadcastMessage(msg);
    
    LogPrint(BCLog::HTTP, "L2 WS: Broadcast new block #%d\n", blockNumber);
}

void L2WebSocketServer::BroadcastSequencerUpdate(
    const std::string& sequencerAddr,
    bool isEligible,
    double uptime,
    uint64_t blocksProduced)
{
    std::ostringstream payload;
    payload << "{";
    payload << "\"address\":\"" << sequencerAddr << "\",";
    payload << "\"isEligible\":" << (isEligible ? "true" : "false") << ",";
    payload << "\"uptime\":" << std::fixed << std::setprecision(2) << uptime << ",";
    payload << "\"blocksProduced\":" << blocksProduced;
    payload << "}";
    
    WSMessage msg(WSMessageType::SEQUENCER_UPDATE, payload.str(), GetTimestamp());
    BroadcastMessage(msg);
}

void L2WebSocketServer::BroadcastSecurityAlert(
    const std::string& alertType,
    const std::string& message,
    const std::string& details)
{
    // Escape JSON strings
    auto escape = [](const std::string& s) {
        std::ostringstream ss;
        for (char c : s) {
            switch (c) {
                case '"': ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default: ss << c;
            }
        }
        return ss.str();
    };
    
    std::ostringstream payload;
    payload << "{";
    payload << "\"alertType\":\"" << escape(alertType) << "\",";
    payload << "\"message\":\"" << escape(message) << "\",";
    payload << "\"details\":\"" << escape(details) << "\"";
    payload << "}";
    
    WSMessage msg(WSMessageType::SECURITY_ALERT, payload.str(), GetTimestamp());
    BroadcastMessage(msg);
    
    LogPrint(BCLog::HTTP, "L2 WS: Broadcast security alert: %s\n", alertType);
}

void L2WebSocketServer::BroadcastStatsUpdate(
    double tps,
    double gasUtilization,
    int64_t tvl)
{
    std::ostringstream payload;
    payload << "{";
    payload << "\"tps\":" << std::fixed << std::setprecision(2) << tps << ",";
    payload << "\"gasUtilization\":" << std::fixed << std::setprecision(2) << gasUtilization << ",";
    payload << "\"tvl\":" << tvl;
    payload << "}";
    
    WSMessage msg(WSMessageType::STATS_UPDATE, payload.str(), GetTimestamp());
    BroadcastMessage(msg);
}

void L2WebSocketServer::BroadcastWithdrawalUpdate(
    const std::string& withdrawalId,
    const std::string& status,
    int64_t amount)
{
    std::ostringstream payload;
    payload << "{";
    payload << "\"withdrawalId\":\"" << withdrawalId << "\",";
    payload << "\"status\":\"" << status << "\",";
    payload << "\"amount\":" << amount;
    payload << "}";
    
    WSMessage msg(WSMessageType::WITHDRAWAL_UPDATE, payload.str(), GetTimestamp());
    BroadcastMessage(msg);
}

void L2WebSocketServer::BroadcastLeaderChange(
    const std::string& newLeader,
    uint64_t slotNumber)
{
    std::ostringstream payload;
    payload << "{";
    payload << "\"newLeader\":\"" << newLeader << "\",";
    payload << "\"slotNumber\":" << slotNumber;
    payload << "}";
    
    WSMessage msg(WSMessageType::LEADER_CHANGE, payload.str(), GetTimestamp());
    BroadcastMessage(msg);
    
    LogPrint(BCLog::HTTP, "L2 WS: Broadcast leader change to %s\n", newLeader);
}

size_t L2WebSocketServer::GetClientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& client : clients_) {
        if (client.connected) count++;
    }
    return count;
}

std::vector<WSClient> L2WebSocketServer::GetClients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_;
}

void L2WebSocketServer::OnConnect(ConnectionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    connectCallbacks_.push_back(callback);
}

void L2WebSocketServer::OnDisconnect(DisconnectionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectCallbacks_.push_back(callback);
}

std::vector<WSMessage> L2WebSocketServer::GetPendingMessages(uint64_t clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<WSMessage> messages;
    auto it = messageQueues_.find(clientId);
    if (it != messageQueues_.end()) {
        messages = std::move(it->second);
        it->second.clear();
    }
    return messages;
}

uint64_t L2WebSocketServer::RegisterClient(const std::string& remoteAddr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    WSClient client;
    client.id = nextClientId_.fetch_add(1);
    client.connected = true;
    client.connectedAt = GetTimestamp();
    client.remoteAddr = remoteAddr;
    
    clients_.push_back(client);
    messageQueues_[client.id] = std::vector<WSMessage>();
    
    // Notify callbacks
    for (const auto& callback : connectCallbacks_) {
        callback(client.id);
    }
    
    LogPrint(BCLog::HTTP, "L2 WS: Client %d connected from %s\n", client.id, remoteAddr);
    return client.id;
}

void L2WebSocketServer::UnregisterClient(uint64_t clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& client : clients_) {
        if (client.id == clientId) {
            client.connected = false;
            break;
        }
    }
    
    messageQueues_.erase(clientId);
    
    // Notify callbacks
    for (const auto& callback : disconnectCallbacks_) {
        callback(clientId);
    }
    
    LogPrint(BCLog::HTTP, "L2 WS: Client %d disconnected\n", clientId);
}

// ============================================================================
// HTTP Handlers for SSE
// ============================================================================

bool L2SSEStreamHandler(::HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != ::HTTPRequest::GET) {
        req->WriteReply(HTTP_BADMETHOD, "Only GET requests allowed");
        return false;
    }
    
    // Get client info
    std::string remoteAddr = "unknown";
    try {
        ::CService peer = req->GetPeer();
        remoteAddr = peer.ToString();
    } catch (...) {}
    
    // Register client
    L2WebSocketServer& server = L2WebSocketServer::GetInstance();
    uint64_t clientId = server.RegisterClient(remoteAddr);
    
    // Set SSE headers
    req->WriteHeader("Content-Type", "text/event-stream");
    req->WriteHeader("Cache-Control", "no-cache");
    req->WriteHeader("Connection", "keep-alive");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    
    // Build initial response with connection info
    std::ostringstream response;
    response << "event: connected\n";
    response << "data: {\"clientId\":" << clientId << "}\n\n";
    
    // Get any pending messages
    auto messages = server.GetPendingMessages(clientId);
    for (const auto& msg : messages) {
        response << "event: message\n";
        response << "data: " << msg.ToJSON() << "\n\n";
    }
    
    req->WriteReply(HTTP_OK, response.str());
    
    // Note: In a real implementation, this would keep the connection open
    // and stream events. The current HTTP server doesn't support long-polling,
    // so clients should poll the /l2/api/events endpoint periodically.
    
    return true;
}

/**
 * @brief Handler for polling events (alternative to SSE)
 */
bool L2EventsHandler(::HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != ::HTTPRequest::GET) {
        req->WriteReply(HTTP_BADMETHOD, "Only GET requests allowed");
        return false;
    }
    
    // Parse client ID from query string
    uint64_t clientId = 0;
    std::string uri = req->GetURI();
    size_t queryPos = uri.find('?');
    if (queryPos != std::string::npos) {
        std::string query = uri.substr(queryPos + 1);
        size_t clientPos = query.find("clientId=");
        if (clientPos != std::string::npos) {
            try {
                clientId = std::stoull(query.substr(clientPos + 9));
            } catch (...) {}
        }
    }
    
    // If no client ID, register new client
    L2WebSocketServer& server = L2WebSocketServer::GetInstance();
    if (clientId == 0) {
        std::string remoteAddr = "unknown";
        try {
            ::CService peer = req->GetPeer();
            remoteAddr = peer.ToString();
        } catch (...) {}
        clientId = server.RegisterClient(remoteAddr);
    }
    
    // Get pending messages
    auto messages = server.GetPendingMessages(clientId);
    
    // Build JSON response
    std::ostringstream json;
    json << "{";
    json << "\"clientId\":" << clientId << ",";
    json << "\"events\":[";
    
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) json << ",";
        json << messages[i].ToJSON();
    }
    
    json << "]}";
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, json.str());
    
    return true;
}

void InitL2WebSocketHandlers() {
    LogPrintf("Initializing L2 WebSocket/SSE handlers...\n");
    
    // Initialize the WebSocket server
    L2WebSocketServer::GetInstance().Initialize();
    
    // Register SSE stream endpoint
    RegisterHTTPHandler("/l2/stream", true, L2SSEStreamHandler);
    
    // Register polling events endpoint (alternative to SSE)
    RegisterHTTPHandler("/l2/api/events", true, L2EventsHandler);
    
    LogPrintf("L2 WebSocket/SSE handlers registered\n");
}

} // namespace l2
