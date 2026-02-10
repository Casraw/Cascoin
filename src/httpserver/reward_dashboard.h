// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_REWARD_DASHBOARD_H
#define CASCOIN_HTTPSERVER_REWARD_DASHBOARD_H

/**
 * @file reward_dashboard.h
 * @brief Challenger Reward System Web Dashboard HTTP handlers
 * 
 * This file implements HTTP endpoints for the reward dashboard including:
 * - /rewards - Main rewards dashboard page
 * - /rewards/api/pending - Get pending rewards for an address
 * - /rewards/api/distribution - Get reward distribution for a dispute
 * - /rewards/api/claim - Claim a reward
 * - /rewards/api/claimall - Batch claim all rewards
 * - /rewards/api/disputes - Get disputes with reward info
 * 
 * Requirements: 7.1, 7.2, 7.3, 7.4, 10.1, 10.2, 10.3, 10.4, 10.5
 */

#include <string>
#include <cstdint>

class HTTPRequest;

namespace reward {

/**
 * @brief Initialize Reward Dashboard HTTP handlers
 * 
 * Registers all HTTP handlers for the reward dashboard endpoints.
 * Should be called during HTTP server initialization.
 */
void InitRewardDashboardHandlers();

/**
 * @brief Handler for reward dashboard main page
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 */
bool RewardDashboardRequestHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/pending endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with pending rewards for an address.
 * Query param: address (required)
 */
bool RewardApiPendingHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/distribution endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with reward distribution for a dispute.
 * Query param: dispute_id (required)
 */
bool RewardApiDistributionHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/claim endpoint (POST)
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Claims a specific reward.
 * POST body: { "reward_id": "...", "address": "..." }
 */
bool RewardApiClaimHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/claimall endpoint (POST)
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Batch claims all pending rewards for an address.
 * POST body: { "address": "..." }
 */
bool RewardApiClaimAllHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/disputes endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with disputes and their reward info.
 * Query params: status (optional), limit (optional)
 */
bool RewardApiDisputesHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/dispute endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with detailed dispute info including rewards.
 * Query param: dispute_id (required)
 */
bool RewardApiDisputeDetailHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Handler for /rewards/api/history endpoint
 * @param req HTTP request
 * @param strReq Request path
 * @return true if request was handled
 * 
 * Returns JSON with claimed rewards history for an address.
 * Query param: address (required)
 */
bool RewardApiHistoryHandler(HTTPRequest* req, const std::string& strReq);

/**
 * @brief Build complete reward dashboard HTML
 * @return Complete HTML string for the dashboard
 */
std::string BuildRewardDashboardHTML();

/**
 * @brief Get pending rewards as JSON string
 * @param address Address to query (hex format)
 * @return JSON string with pending rewards
 */
std::string GetPendingRewardsJSON(const std::string& address);

/**
 * @brief Get claimed rewards history as JSON string
 * @param address Address to query (hex format)
 * @return JSON string with claimed rewards history
 */
std::string GetClaimedRewardsJSON(const std::string& address);

/**
 * @brief Get reward distribution as JSON string
 * @param disputeId Dispute ID (hex format)
 * @return JSON string with reward distribution
 */
std::string GetRewardDistributionJSON(const std::string& disputeId);

/**
 * @brief Get disputes list as JSON string
 * @param status Filter by status (optional, empty for all)
 * @param limit Maximum number of disputes to return
 * @return JSON string with disputes
 */
std::string GetDisputesJSON(const std::string& status, size_t limit);

/**
 * @brief Get dispute detail as JSON string
 * @param disputeId Dispute ID (hex format)
 * @return JSON string with dispute details including rewards
 */
std::string GetDisputeDetailJSON(const std::string& disputeId);

/**
 * @brief Format amount in CAS for display
 * @param satoshis Amount in satoshis
 * @return Formatted string (e.g., "123.45 CAS")
 */
std::string FormatCAS(int64_t satoshis);

/**
 * @brief Format timestamp for display
 * @param timestamp Unix timestamp
 * @return Formatted string (e.g., "2025-01-15 14:30:00 UTC")
 */
std::string FormatTimestamp(uint64_t timestamp);

/**
 * @brief Format hash for display (truncated)
 * @param hash Full hash string
 * @param length Display length (default 16)
 * @return Truncated hash (e.g., "0x1234...abcd")
 */
std::string FormatHash(const std::string& hash, size_t length = 16);

} // namespace reward

#endif // CASCOIN_HTTPSERVER_REWARD_DASHBOARD_H
