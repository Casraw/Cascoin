// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file reward_dashboard.cpp
 * @brief Challenger Reward System Web Dashboard HTTP handlers implementation
 * 
 * Requirements: 7.1, 7.2, 7.3, 7.4, 10.1, 10.2, 10.3, 10.4, 10.5
 */

#include <httpserver/reward_dashboard.h>
#include <httpserver/reward_dashboard_html.h>
#include <httpserver.h>
#include <rpc/protocol.h>
#include <util.h>
#include <utilstrencodings.h>
#include <chainparamsbase.h>
#include <amount.h>
#include <base58.h>
#include <streams.h>
#include <version.h>

#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/reward_types.h>
#include <cvm/reward_distributor.h>

#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

namespace reward {

// Cache for dashboard HTML
static std::string g_rewardDashboardHTML;
static bool g_rewardDashboardInitialized = false;

std::string FormatCAS(int64_t satoshis) {
    double cas = static_cast<double>(satoshis) / COIN;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8) << cas << " CAS";
    return ss.str();
}

std::string FormatTimestamp(uint64_t timestamp) {
    if (timestamp == 0) return "N/A";
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

// Parse query string parameters
static std::map<std::string, std::string> ParseQueryString(const std::string& uri) {
    std::map<std::string, std::string> params;
    size_t queryPos = uri.find('?');
    if (queryPos == std::string::npos) return params;
    
    std::string query = uri.substr(queryPos + 1);
    std::istringstream iss(query);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = pair.substr(0, eqPos);
            std::string value = pair.substr(eqPos + 1);
            params[key] = value;
        }
    }
    
    return params;
}

// Get current timestamp
static uint64_t GetCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GetPendingRewardsJSON(const std::string& addressStr) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"address\": \"" << EscapeJSON(addressStr) << "\",\n";
    json << "  \"rewards\": [\n";
    
    if (CVM::g_cvmdb) {
        try {
            // Parse address
            CTxDestination dest = DecodeDestination(addressStr);
            if (IsValidDestination(dest)) {
                uint160 address;
                if (boost::get<CKeyID>(&dest)) {
                    address = uint160(boost::get<CKeyID>(dest));
                } else if (boost::get<CScriptID>(&dest)) {
                    address = uint160(boost::get<CScriptID>(dest));
                }
                
                if (!address.IsNull()) {
                    CVM::TrustGraph tg(*CVM::g_cvmdb);
                    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
                    
                    if (distributor) {
                        std::vector<CVM::PendingReward> rewards = distributor->GetPendingRewards(address);
                        CAmount totalPending = 0;
                        
                        for (size_t i = 0; i < rewards.size(); ++i) {
                            const auto& reward = rewards[i];
                            totalPending += reward.amount;
                            
                            json << "    {\n";
                            json << "      \"reward_id\": \"" << reward.rewardId.GetHex() << "\",\n";
                            json << "      \"dispute_id\": \"" << reward.disputeId.GetHex() << "\",\n";
                            json << "      \"amount\": " << reward.amount << ",\n";
                            json << "      \"amount_formatted\": \"" << FormatCAS(reward.amount) << "\",\n";
                            json << "      \"type\": \"" << CVM::RewardTypeToString(reward.type) << "\",\n";
                            json << "      \"created_time\": " << reward.createdTime << ",\n";
                            json << "      \"created_time_formatted\": \"" << FormatTimestamp(reward.createdTime) << "\",\n";
                            json << "      \"claimed\": " << (reward.claimed ? "true" : "false") << "\n";
                            json << "    }";
                            if (i < rewards.size() - 1) json << ",";
                            json << "\n";
                        }
                        
                        json << "  ],\n";
                        json << "  \"total_pending\": " << totalPending << ",\n";
                        json << "  \"total_pending_formatted\": \"" << FormatCAS(totalPending) << "\",\n";
                        json << "  \"count\": " << rewards.size() << ",\n";
                        json << "  \"success\": true\n";
                        json << "}";
                        return json.str();
                    }
                }
            }
        } catch (const std::exception& e) {
            LogPrintf("Reward Dashboard: Error getting pending rewards: %s\n", e.what());
        }
    }
    
    json << "  ],\n";
    json << "  \"total_pending\": 0,\n";
    json << "  \"total_pending_formatted\": \"0.00000000 CAS\",\n";
    json << "  \"count\": 0,\n";
    json << "  \"success\": false,\n";
    json << "  \"error\": \"Unable to retrieve rewards\"\n";
    json << "}";
    
    return json.str();
}

std::string GetClaimedRewardsJSON(const std::string& addressStr) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"address\": \"" << EscapeJSON(addressStr) << "\",\n";
    json << "  \"rewards\": [\n";
    
    if (CVM::g_cvmdb) {
        try {
            // Parse address
            CTxDestination dest = DecodeDestination(addressStr);
            if (IsValidDestination(dest)) {
                uint160 address;
                if (boost::get<CKeyID>(&dest)) {
                    address = uint160(boost::get<CKeyID>(dest));
                } else if (boost::get<CScriptID>(&dest)) {
                    address = uint160(boost::get<CScriptID>(dest));
                }
                
                if (!address.IsNull()) {
                    CVM::TrustGraph tg(*CVM::g_cvmdb);
                    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
                    
                    if (distributor) {
                        std::vector<CVM::PendingReward> rewards = distributor->GetClaimedRewards(address);
                        CAmount totalClaimed = 0;
                        
                        for (size_t i = 0; i < rewards.size(); ++i) {
                            const auto& reward = rewards[i];
                            totalClaimed += reward.amount;
                            
                            json << "    {\n";
                            json << "      \"reward_id\": \"" << reward.rewardId.GetHex() << "\",\n";
                            json << "      \"dispute_id\": \"" << reward.disputeId.GetHex() << "\",\n";
                            json << "      \"amount\": " << reward.amount << ",\n";
                            json << "      \"amount_formatted\": \"" << FormatCAS(reward.amount) << "\",\n";
                            json << "      \"type\": \"" << CVM::RewardTypeToString(reward.type) << "\",\n";
                            json << "      \"created_time\": " << reward.createdTime << ",\n";
                            json << "      \"created_time_formatted\": \"" << FormatTimestamp(reward.createdTime) << "\",\n";
                            json << "      \"claimed\": true,\n";
                            json << "      \"claimed_time\": " << reward.claimedTime << ",\n";
                            json << "      \"claimed_time_formatted\": \"" << FormatTimestamp(reward.claimedTime) << "\",\n";
                            json << "      \"claim_tx_hash\": \"" << reward.claimTxHash.GetHex() << "\"\n";
                            json << "    }";
                            if (i < rewards.size() - 1) json << ",";
                            json << "\n";
                        }
                        
                        json << "  ],\n";
                        json << "  \"total_claimed\": " << totalClaimed << ",\n";
                        json << "  \"total_claimed_formatted\": \"" << FormatCAS(totalClaimed) << "\",\n";
                        json << "  \"count\": " << rewards.size() << ",\n";
                        json << "  \"success\": true\n";
                        json << "}";
                        return json.str();
                    }
                }
            }
        } catch (const std::exception& e) {
            LogPrintf("Reward Dashboard: Error getting claimed rewards: %s\n", e.what());
        }
    }
    
    json << "  ],\n";
    json << "  \"total_claimed\": 0,\n";
    json << "  \"total_claimed_formatted\": \"0.00000000 CAS\",\n";
    json << "  \"count\": 0,\n";
    json << "  \"success\": false,\n";
    json << "  \"error\": \"Unable to retrieve claim history\"\n";
    json << "}";
    
    return json.str();
}

std::string GetRewardDistributionJSON(const std::string& disputeIdStr) {
    std::ostringstream json;
    json << "{\n";
    
    if (CVM::g_cvmdb) {
        try {
            uint256 disputeId;
            disputeId.SetHex(disputeIdStr);
            
            CVM::TrustGraph tg(*CVM::g_cvmdb);
            CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
            
            if (distributor) {
                CVM::RewardDistribution dist = distributor->GetRewardDistribution(disputeId);
                
                if (dist.IsValid()) {
                    json << "  \"dispute_id\": \"" << dist.disputeId.GetHex() << "\",\n";
                    json << "  \"slash_decision\": " << (dist.slashDecision ? "true" : "false") << ",\n";
                    json << "  \"total_slashed_bond\": " << dist.totalSlashedBond << ",\n";
                    json << "  \"total_slashed_bond_formatted\": \"" << FormatCAS(dist.totalSlashedBond) << "\",\n";
                    json << "  \"challenger_bond_return\": " << dist.challengerBondReturn << ",\n";
                    json << "  \"challenger_bond_return_formatted\": \"" << FormatCAS(dist.challengerBondReturn) << "\",\n";
                    json << "  \"challenger_bounty\": " << dist.challengerBounty << ",\n";
                    json << "  \"challenger_bounty_formatted\": \"" << FormatCAS(dist.challengerBounty) << "\",\n";
                    json << "  \"total_dao_voter_rewards\": " << dist.totalDaoVoterRewards << ",\n";
                    json << "  \"total_dao_voter_rewards_formatted\": \"" << FormatCAS(dist.totalDaoVoterRewards) << "\",\n";
                    json << "  \"burned_amount\": " << dist.burnedAmount << ",\n";
                    json << "  \"burned_amount_formatted\": \"" << FormatCAS(dist.burnedAmount) << "\",\n";
                    json << "  \"distributed_time\": " << dist.distributedTime << ",\n";
                    json << "  \"distributed_time_formatted\": \"" << FormatTimestamp(dist.distributedTime) << "\",\n";
                    
                    // Voter rewards breakdown
                    json << "  \"voter_rewards\": [\n";
                    size_t voterIdx = 0;
                    for (const auto& [voter, amount] : dist.voterRewards) {
                        json << "    {\n";
                        json << "      \"address\": \"" << EncodeDestination(CKeyID(voter)) << "\",\n";
                        json << "      \"amount\": " << amount << ",\n";
                        json << "      \"amount_formatted\": \"" << FormatCAS(amount) << "\"\n";
                        json << "    }";
                        if (voterIdx < dist.voterRewards.size() - 1) json << ",";
                        json << "\n";
                        voterIdx++;
                    }
                    json << "  ],\n";
                    json << "  \"success\": true\n";
                    json << "}";
                    return json.str();
                }
            }
        } catch (const std::exception& e) {
            LogPrintf("Reward Dashboard: Error getting distribution: %s\n", e.what());
        }
    }
    
    json << "  \"success\": false,\n";
    json << "  \"error\": \"Distribution not found\"\n";
    json << "}";
    
    return json.str();
}

std::string GetDisputesJSON(const std::string& status, size_t limit) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"disputes\": [\n";
    
    if (CVM::g_cvmdb) {
        try {
            // Get all dispute keys from database (prefix scan)
            std::vector<std::string> keys = CVM::g_cvmdb->ListKeysWithPrefix("dispute_");
            
            std::vector<CVM::DAODispute> filtered;
            for (const auto& key : keys) {
                // Skip secondary index entries
                if (key.compare(0, std::string("dispute_by_vote_").size(), "dispute_by_vote_") == 0) {
                    continue;
                }
                
                std::vector<uint8_t> data;
                if (CVM::g_cvmdb->Read(key, data)) {
                    CVM::DAODispute dispute;
                    CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                    ss >> dispute;
                    
                    // Filter by status if specified
                    if (status.empty()) {
                        filtered.push_back(dispute);
                    } else if (status == "resolved" && dispute.resolved) {
                        filtered.push_back(dispute);
                    } else if (status == "pending" && !dispute.resolved) {
                        filtered.push_back(dispute);
                    }
                    
                    if (filtered.size() >= limit) break;
                }
            }
            
            for (size_t i = 0; i < filtered.size(); ++i) {
                const auto& dispute = filtered[i];
                json << "    {\n";
                json << "      \"dispute_id\": \"" << dispute.disputeId.GetHex() << "\",\n";
                json << "      \"challenger\": \"" << EncodeDestination(CKeyID(dispute.challenger)) << "\",\n";
                json << "      \"target_vote\": \"" << dispute.targetVote.GetHex() << "\",\n";
                json << "      \"challenge_bond\": " << dispute.challengeBond << ",\n";
                json << "      \"challenge_bond_formatted\": \"" << FormatCAS(dispute.challengeBond) << "\",\n";
                json << "      \"created_time\": " << dispute.createdTime << ",\n";
                json << "      \"created_time_formatted\": \"" << FormatTimestamp(dispute.createdTime) << "\",\n";
                json << "      \"resolved\": " << (dispute.resolved ? "true" : "false") << ",\n";
                json << "      \"slash_decision\": " << (dispute.slashDecision ? "true" : "false") << ",\n";
                json << "      \"rewards_distributed\": " << (dispute.rewardsDistributed ? "true" : "false") << ",\n";
                json << "      \"use_commit_reveal\": " << (dispute.useCommitReveal ? "true" : "false") << ",\n";
                json << "      \"slash_votes\": " << dispute.slashVotes << ",\n";
                json << "      \"keep_votes\": " << dispute.keepVotes << "\n";
                json << "    }";
                if (i < filtered.size() - 1) json << ",";
                json << "\n";
            }
            
            json << "  ],\n";
            json << "  \"total\": " << filtered.size() << ",\n";
            json << "  \"success\": true\n";
        } catch (const std::exception& e) {
            json << "  ],\n";
            json << "  \"total\": 0,\n";
            json << "  \"success\": false,\n";
            json << "  \"error\": \"" << EscapeJSON(e.what()) << "\"\n";
        }
    } else {
        json << "  ],\n";
        json << "  \"total\": 0,\n";
        json << "  \"success\": false,\n";
        json << "  \"error\": \"CVM database not initialized\"\n";
    }
    
    json << "}";
    return json.str();
}

std::string GetDisputeDetailJSON(const std::string& disputeIdStr) {
    std::ostringstream json;
    json << "{\n";
    
    if (CVM::g_cvmdb) {
        try {
            uint256 disputeId;
            disputeId.SetHex(disputeIdStr);
            
            std::string key = "dispute_" + disputeId.ToString();
            std::vector<uint8_t> data;
            
            if (CVM::g_cvmdb->Read(key, data)) {
                CVM::DAODispute dispute;
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ss >> dispute;
                
                json << "  \"dispute_id\": \"" << dispute.disputeId.GetHex() << "\",\n";
                json << "  \"challenger\": \"" << EncodeDestination(CKeyID(dispute.challenger)) << "\",\n";
                json << "  \"target_vote\": \"" << dispute.targetVote.GetHex() << "\",\n";
                json << "  \"challenge_bond\": " << dispute.challengeBond << ",\n";
                json << "  \"challenge_bond_formatted\": \"" << FormatCAS(dispute.challengeBond) << "\",\n";
                json << "  \"created_time\": " << dispute.createdTime << ",\n";
                json << "  \"created_time_formatted\": \"" << FormatTimestamp(dispute.createdTime) << "\",\n";
                json << "  \"resolved\": " << (dispute.resolved ? "true" : "false") << ",\n";
                json << "  \"resolved_time\": " << dispute.resolvedTime << ",\n";
                json << "  \"resolved_time_formatted\": \"" << FormatTimestamp(dispute.resolvedTime) << "\",\n";
                json << "  \"slash_decision\": " << (dispute.slashDecision ? "true" : "false") << ",\n";
                json << "  \"rewards_distributed\": " << (dispute.rewardsDistributed ? "true" : "false") << ",\n";
                json << "  \"use_commit_reveal\": " << (dispute.useCommitReveal ? "true" : "false") << ",\n";
                json << "  \"commit_phase_start\": " << dispute.commitPhaseStart << ",\n";
                json << "  \"reveal_phase_start\": " << dispute.revealPhaseStart << ",\n";
                json << "  \"slash_votes\": " << dispute.slashVotes << ",\n";
                json << "  \"keep_votes\": " << dispute.keepVotes << ",\n";
                
                // Add reward distribution if available
                if (dispute.resolved && dispute.rewardsDistributed) {
                    CVM::TrustGraph tg(*CVM::g_cvmdb);
                    CVM::RewardDistributor* distributor = tg.GetRewardDistributor();
                    if (distributor) {
                        CVM::RewardDistribution dist = distributor->GetRewardDistribution(disputeId);
                        if (dist.IsValid()) {
                            json << "  \"reward_distribution\": {\n";
                            json << "    \"challenger_bond_return\": " << dist.challengerBondReturn << ",\n";
                            json << "    \"challenger_bond_return_formatted\": \"" << FormatCAS(dist.challengerBondReturn) << "\",\n";
                            json << "    \"challenger_bounty\": " << dist.challengerBounty << ",\n";
                            json << "    \"challenger_bounty_formatted\": \"" << FormatCAS(dist.challengerBounty) << "\",\n";
                            json << "    \"total_dao_voter_rewards\": " << dist.totalDaoVoterRewards << ",\n";
                            json << "    \"total_dao_voter_rewards_formatted\": \"" << FormatCAS(dist.totalDaoVoterRewards) << "\",\n";
                            json << "    \"burned_amount\": " << dist.burnedAmount << ",\n";
                            json << "    \"burned_amount_formatted\": \"" << FormatCAS(dist.burnedAmount) << "\",\n";
                            json << "    \"voter_count\": " << dist.voterRewards.size() << "\n";
                            json << "  },\n";
                        }
                    }
                }
                
                json << "  \"success\": true\n";
                json << "}";
                return json.str();
            }
        } catch (const std::exception& e) {
            LogPrintf("Reward Dashboard: Error getting dispute detail: %s\n", e.what());
        }
    }
    
    json << "  \"success\": false,\n";
    json << "  \"error\": \"Dispute not found\"\n";
    json << "}";
    
    return json.str();
}

// HTTP Handlers

bool RewardApiPendingHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    auto params = ParseQueryString(req->GetURI());
    std::string address = params["address"];
    
    if (address.empty()) {
        req->WriteHeader("Content-Type", "application/json");
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteReply(HTTP_BAD_REQUEST, "{\"success\": false, \"error\": \"Missing address parameter\"}");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetPendingRewardsJSON(address));
    return true;
}

bool RewardApiDistributionHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    auto params = ParseQueryString(req->GetURI());
    std::string disputeId = params["dispute_id"];
    
    if (disputeId.empty()) {
        req->WriteHeader("Content-Type", "application/json");
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteReply(HTTP_BAD_REQUEST, "{\"success\": false, \"error\": \"Missing dispute_id parameter\"}");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetRewardDistributionJSON(disputeId));
    return true;
}

bool RewardApiClaimHandler(HTTPRequest* req, const std::string& strReq) {
    // Handle CORS preflight
    if (req->GetRequestMethod() == HTTPRequest::OPTIONS) {
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        req->WriteHeader("Access-Control-Allow-Headers", "Content-Type");
        req->WriteReply(HTTP_OK, "");
        return true;
    }
    
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "Only POST requests allowed");
        return false;
    }
    
    std::ostringstream json;
    json << "{\n";
    
    // Note: In production, this would parse the POST body and call the RPC
    // For now, return a message indicating the user should use RPC
    json << "  \"success\": false,\n";
    json << "  \"message\": \"Please use the claimreward RPC command to claim rewards. Dashboard claiming requires wallet integration.\",\n";
    json << "  \"rpc_command\": \"claimreward \\\"reward_id\\\" \\\"your_address\\\"\"\n";
    json << "}";
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, json.str());
    return true;
}

bool RewardApiClaimAllHandler(HTTPRequest* req, const std::string& strReq) {
    // Handle CORS preflight
    if (req->GetRequestMethod() == HTTPRequest::OPTIONS) {
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        req->WriteHeader("Access-Control-Allow-Headers", "Content-Type");
        req->WriteReply(HTTP_OK, "");
        return true;
    }
    
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "Only POST requests allowed");
        return false;
    }
    
    std::ostringstream json;
    json << "{\n";
    json << "  \"success\": false,\n";
    json << "  \"message\": \"Please use the claimallrewards RPC command to batch claim rewards. Dashboard claiming requires wallet integration.\",\n";
    json << "  \"rpc_command\": \"claimallrewards \\\"your_address\\\"\"\n";
    json << "}";
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, json.str());
    return true;
}

bool RewardApiDisputesHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    auto params = ParseQueryString(req->GetURI());
    std::string status = params["status"];
    size_t limit = 50;
    
    if (!params["limit"].empty()) {
        try {
            limit = std::stoul(params["limit"]);
            if (limit > 100) limit = 100;
        } catch (...) {
            limit = 50;
        }
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetDisputesJSON(status, limit));
    return true;
}

bool RewardApiDisputeDetailHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    auto params = ParseQueryString(req->GetURI());
    std::string disputeId = params["dispute_id"];
    
    if (disputeId.empty()) {
        req->WriteHeader("Content-Type", "application/json");
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteReply(HTTP_BAD_REQUEST, "{\"success\": false, \"error\": \"Missing dispute_id parameter\"}");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetDisputeDetailJSON(disputeId));
    return true;
}

bool RewardApiHistoryHandler(HTTPRequest* req, const std::string& strReq) {
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    auto params = ParseQueryString(req->GetURI());
    std::string address = params["address"];
    
    if (address.empty()) {
        req->WriteHeader("Content-Type", "application/json");
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteReply(HTTP_BAD_REQUEST, "{\"success\": false, \"error\": \"Missing address parameter\"}");
        return false;
    }
    
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(HTTP_OK, GetClaimedRewardsJSON(address));
    return true;
}

std::string BuildRewardDashboardHTML() {
    return RewardDashboardHTML::INDEX_HTML;
}

bool RewardDashboardRequestHandler(HTTPRequest* req, const std::string& strReq) {
    LogPrint(BCLog::HTTP, "Reward Dashboard: Request for %s\n", strReq);
    
    // Only allow GET requests
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Only GET requests allowed");
        return false;
    }
    
    // Build dashboard HTML on first request (lazy initialization)
    if (!g_rewardDashboardInitialized) {
        g_rewardDashboardHTML = BuildRewardDashboardHTML();
        g_rewardDashboardInitialized = true;
        LogPrint(BCLog::HTTP, "Reward Dashboard: Built HTML (%d bytes)\n", 
                 g_rewardDashboardHTML.length());
    }
    
    // Serve embedded HTML page
    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    req->WriteHeader("Pragma", "no-cache");
    req->WriteHeader("Expires", "0");
    req->WriteReply(HTTP_OK, g_rewardDashboardHTML);
    
    LogPrint(BCLog::HTTP, "Reward Dashboard: Served HTML (%d bytes)\n", 
             g_rewardDashboardHTML.length());
    
    return true;
}

void InitRewardDashboardHandlers() {
    LogPrintf("Initializing Reward Dashboard handlers...\n");
    
    // Register main dashboard handler
    RegisterHTTPHandler("/rewards", false, RewardDashboardRequestHandler);
    RegisterHTTPHandler("/rewards/dashboard", false, RewardDashboardRequestHandler);
    
    // Register API endpoints
    RegisterHTTPHandler("/rewards/api/pending", true, RewardApiPendingHandler);
    RegisterHTTPHandler("/rewards/api/distribution", true, RewardApiDistributionHandler);
    RegisterHTTPHandler("/rewards/api/claim", true, RewardApiClaimHandler);
    RegisterHTTPHandler("/rewards/api/claimall", true, RewardApiClaimAllHandler);
    RegisterHTTPHandler("/rewards/api/disputes", true, RewardApiDisputesHandler);
    RegisterHTTPHandler("/rewards/api/dispute", true, RewardApiDisputeDetailHandler);
    RegisterHTTPHandler("/rewards/api/history", true, RewardApiHistoryHandler);
    
    try {
        LogPrintf("Reward Dashboard available at http://localhost:%d/rewards/\n", 
                  gArgs.GetArg("-rpcport", BaseParams().RPCPort()));
    } catch (const std::exception& e) {
        LogPrintf("Reward Dashboard: Could not get RPC port: %s\n", e.what());
    }
}

} // namespace reward
