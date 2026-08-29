// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_L2_DASHBOARD_H
#define CASCOIN_HTTPSERVER_L2_DASHBOARD_H

/**
 * @file l2_dashboard.h
 * @brief L2 Web Dashboard HTTP handlers
 * 
 * This file implements HTTP endpoints for the L2 dashboard including:
 * - /l2/status - Chain status and health
 * - /l2/sequencers - Sequencer list and performance
 * - /l2/blocks - Block explorer data
 * - /l2/api/* - JSON API endpoints
 * 
 * Requirements: 25.4, 33.8, 39.1, 39.2, 39.3
 */

#include <string>
#include <cstdint>

class HTTPRequest;

namespace l2 {

// Forward declarations
struct BridgeStats;
struct SecurityDashboardMetrics;
struct SequencerInfo;
struct L2Block;

/**
 * @brief Initialize L2 Dashboard HTTP handlers
 * 
 * Registers all HTTP handlers for the L2 dashboard endpoints.
 * Should be called during HTTP server initialization.
 */
void InitL2DashboardHandlers();

/**
 * @brief Handler for L2 dashboard main page
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 */
bool L2DashboardRequestHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/status endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with:
 * - Chain ID, block height, state root
 * - TPS, gas usage, uptime
 * - Health status
 */
bool L2StatusHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/sequencers endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with:
 * - List of sequencers with address, stake, HAT score
 * - Current leader
 * - Performance metrics (uptime, blocks produced)
 */
bool L2SequencersHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/blocks endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with:
 * - Recent blocks (configurable limit)
 * - Block details (hash, height, txCount, gasUsed)
 */
bool L2BlocksHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/api/stats endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON statistics including:
 * - Total transactions, blocks
 * - Average TPS, gas price
 * - TVL, pending withdrawals
 */
bool L2ApiStatsHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/api/sequencers endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns detailed sequencer JSON data
 */
bool L2ApiSequencersHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/api/transactions endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns recent transactions JSON
 */
bool L2ApiTransactionsHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/api/withdrawals endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns pending withdrawals JSON
 */
bool L2ApiWithdrawalsHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /l2/api/alerts endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns security alerts JSON
 */
bool L2ApiAlertsHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Build complete L2 dashboard HTML
 * @return Complete HTML string for the dashboard
 */
std::string BuildL2DashboardHTML();

/**
 * @brief Get L2 chain status as JSON string
 * @return JSON string with chain status
 */
std::string GetL2StatusJSON();

/**
 * @brief Get sequencers list as JSON string
 * @return JSON string with sequencer data
 */
std::string GetSequencersJSON();

/**
 * @brief Get recent blocks as JSON string
 * @param limit Maximum number of blocks to return
 * @return JSON string with block data
 */
std::string GetBlocksJSON(size_t limit = 10);

/**
 * @brief Get statistics as JSON string
 * @return JSON string with statistics
 */
std::string GetStatsJSON();

/**
 * @brief Get recent transactions as JSON string
 * @param limit Maximum number of transactions to return
 * @return JSON string with transaction data
 */
std::string GetTransactionsJSON(size_t limit = 20);

/**
 * @brief Get pending withdrawals as JSON string
 * @return JSON string with withdrawal data
 */
std::string GetWithdrawalsJSON();

/**
 * @brief Get security alerts as JSON string
 * @return JSON string with alert data
 */
std::string GetAlertsJSON();

/**
 * @brief Format amount in CAS for display
 * @param satoshis Amount in satoshis
 * @return Formatted string (e.g., "123.45 CAS")
 */
std::string FormatCAS(int64_t satoshis);

/**
 * @brief Format timestamp for display
 * @param timestamp Unix timestamp
 * @return Formatted string (e.g., "2024-01-15 14:30:00")
 */
std::string FormatTimestamp(uint64_t timestamp);

/**
 * @brief Format hash for display (truncated)
 * @param hash Full hash string
 * @param length Display length (default 16)
 * @return Truncated hash (e.g., "0x1234...abcd")
 */
std::string FormatHash(const std::string& hash, size_t length = 16);

} // namespace l2

#endif // CASCOIN_HTTPSERVER_L2_DASHBOARD_H
