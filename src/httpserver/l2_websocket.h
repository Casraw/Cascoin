// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_L2_WEBSOCKET_H
#define CASCOIN_HTTPSERVER_L2_WEBSOCKET_H

/**
 * @file l2_websocket.h
 * @brief WebSocket Server for L2 Dashboard Live Updates
 * 
 * This file implements a WebSocket server for pushing real-time updates
 * to connected L2 dashboard clients including:
 * - New blocks
 * - Sequencer status changes
 * - Security alerts
 * 
 * Requirements: 33.1, 25.4
 */

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>

// Forward declarations for HTTP types (in global namespace)
class HTTPRequest;

namespace l2 {

/**
 * @brief WebSocket message types for L2 dashboard
 */
enum class WSMessageType : uint8_t {
    NEW_BLOCK = 1,          // New L2 block produced
    SEQUENCER_UPDATE = 2,   // Sequencer status changed
    SECURITY_ALERT = 3,     // Security alert triggered
    STATS_UPDATE = 4,       // Statistics update
    WITHDRAWAL_UPDATE = 5,  // Withdrawal status changed
    LEADER_CHANGE = 6       // Leader election result
};

/**
 * @brief WebSocket message structure
 */
struct WSMessage {
    WSMessageType type;
    std::string payload;    // JSON payload
    uint64_t timestamp;
    
    WSMessage() : type(WSMessageType::STATS_UPDATE), timestamp(0) {}
    WSMessage(WSMessageType t, const std::string& p, uint64_t ts)
        : type(t), payload(p), timestamp(ts) {}
    
    /**
     * @brief Serialize message to JSON string
     */
    std::string ToJSON() const;
};

/**
 * @brief WebSocket client connection (simplified)
 */
struct WSClient {
    uint64_t id;
    bool connected;
    uint64_t connectedAt;
    std::string remoteAddr;
    
    WSClient() : id(0), connected(false), connectedAt(0) {}
};

/**
 * @brief L2 WebSocket Server for live dashboard updates
 * 
 * Provides real-time push notifications to connected dashboard clients.
 * Uses Server-Sent Events (SSE) as a simpler alternative to full WebSocket
 * protocol, which is compatible with the existing HTTP server infrastructure.
 * 
 * Requirements: 33.1, 25.4
 */
class L2WebSocketServer {
public:
    /** Callback for new client connections */
    using ConnectionCallback = std::function<void(uint64_t clientId)>;
    
    /** Callback for client disconnections */
    using DisconnectionCallback = std::function<void(uint64_t clientId)>;

    /**
     * @brief Get singleton instance
     */
    static L2WebSocketServer& GetInstance();

    /**
     * @brief Initialize the WebSocket server
     * @return true if initialization succeeded
     */
    bool Initialize();

    /**
     * @brief Shutdown the WebSocket server
     */
    void Shutdown();

    /**
     * @brief Check if server is running
     */
    bool IsRunning() const { return running_.load(); }

    // ========================================================================
    // Broadcasting Methods
    // ========================================================================

    /**
     * @brief Broadcast new block to all connected clients
     * @param blockNumber Block number
     * @param blockHash Block hash
     * @param txCount Transaction count
     * @param gasUsed Gas used
     * @param sequencer Sequencer address
     */
    void BroadcastNewBlock(
        uint64_t blockNumber,
        const std::string& blockHash,
        size_t txCount,
        uint64_t gasUsed,
        const std::string& sequencer);

    /**
     * @brief Broadcast sequencer status update
     * @param sequencerAddr Sequencer address
     * @param isEligible Whether sequencer is eligible
     * @param uptime Uptime percentage
     * @param blocksProduced Blocks produced count
     */
    void BroadcastSequencerUpdate(
        const std::string& sequencerAddr,
        bool isEligible,
        double uptime,
        uint64_t blocksProduced);

    /**
     * @brief Broadcast security alert
     * @param alertType Alert type (info, warning, critical, emergency)
     * @param message Alert message
     * @param details Additional details
     */
    void BroadcastSecurityAlert(
        const std::string& alertType,
        const std::string& message,
        const std::string& details);

    /**
     * @brief Broadcast statistics update
     * @param tps Current TPS
     * @param gasUtilization Gas utilization percentage
     * @param tvl Total value locked
     */
    void BroadcastStatsUpdate(
        double tps,
        double gasUtilization,
        int64_t tvl);

    /**
     * @brief Broadcast withdrawal status update
     * @param withdrawalId Withdrawal ID
     * @param status New status
     * @param amount Amount
     */
    void BroadcastWithdrawalUpdate(
        const std::string& withdrawalId,
        const std::string& status,
        int64_t amount);

    /**
     * @brief Broadcast leader change
     * @param newLeader New leader address
     * @param slotNumber Slot number
     */
    void BroadcastLeaderChange(
        const std::string& newLeader,
        uint64_t slotNumber);

    // ========================================================================
    // Client Management
    // ========================================================================

    /**
     * @brief Get number of connected clients
     */
    size_t GetClientCount() const;

    /**
     * @brief Get list of connected clients
     */
    std::vector<WSClient> GetClients() const;

    /**
     * @brief Register connection callback
     */
    void OnConnect(ConnectionCallback callback);

    /**
     * @brief Register disconnection callback
     */
    void OnDisconnect(DisconnectionCallback callback);

    /**
     * @brief Get pending messages for SSE streaming
     * @param clientId Client ID
     * @return Vector of pending messages
     */
    std::vector<WSMessage> GetPendingMessages(uint64_t clientId);

    /**
     * @brief Register a new SSE client
     * @param remoteAddr Client remote address
     * @return Client ID
     */
    uint64_t RegisterClient(const std::string& remoteAddr);

    /**
     * @brief Unregister an SSE client
     * @param clientId Client ID
     */
    void UnregisterClient(uint64_t clientId);

private:
    L2WebSocketServer();
    ~L2WebSocketServer();
    
    // Prevent copying
    L2WebSocketServer(const L2WebSocketServer&) = delete;
    L2WebSocketServer& operator=(const L2WebSocketServer&) = delete;

    /** Running flag */
    std::atomic<bool> running_;

    /** Connected clients */
    std::vector<WSClient> clients_;

    /** Message queue per client */
    std::map<uint64_t, std::vector<WSMessage>> messageQueues_;

    /** Next client ID */
    std::atomic<uint64_t> nextClientId_;

    /** Mutex for thread safety */
    mutable std::mutex mutex_;

    /** Connection callbacks */
    std::vector<ConnectionCallback> connectCallbacks_;

    /** Disconnection callbacks */
    std::vector<DisconnectionCallback> disconnectCallbacks_;

    /**
     * @brief Broadcast message to all clients
     */
    void BroadcastMessage(const WSMessage& msg);

    /**
     * @brief Get current timestamp
     */
    uint64_t GetTimestamp() const;
};

/**
 * @brief Initialize L2 WebSocket/SSE handlers
 */
void InitL2WebSocketHandlers();

/**
 * @brief HTTP handler for SSE stream endpoint
 */
bool L2SSEStreamHandler(::HTTPRequest* req, const std::string& strReq);

/**
 * @brief HTTP handler for polling events endpoint
 */
bool L2EventsHandler(::HTTPRequest* req, const std::string& strReq);

} // namespace l2

#endif // CASCOIN_HTTPSERVER_L2_WEBSOCKET_H
