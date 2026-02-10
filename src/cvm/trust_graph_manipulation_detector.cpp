// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_graph_manipulation_detector.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/walletcluster.h>
#include <hash.h>
#include <util.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace CVM {

// Helper function to create database key
static std::string MakeDBKey(char prefix, const uint160& addr) {
    return std::string(1, prefix) + addr.ToString();
}

// Helper function to write serializable data to database
template<typename T>
static bool WriteToDatabase(CVMDatabase& db, const std::string& key, const T& value) {
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << value;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    return db.WriteGeneric(key, data);
}

// Helper function to read serializable data from database
template<typename T>
static bool ReadFromDatabase(CVMDatabase& db, const std::string& key, T& value) {
    std::vector<uint8_t> data;
    if (!db.ReadGeneric(key, data)) {
        return false;
    }
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> value;
        return true;
    } catch (...) {
        return false;
    }
}

TrustGraphManipulationDetector::TrustGraphManipulationDetector(
    CVMDatabase& db,
    TrustGraph& trustGraph,
    WalletClusterer& clusterer)
    : m_db(db), m_trustGraph(trustGraph), m_clusterer(clusterer)
{
    LoadFlaggedAddresses();
}


TrustManipulationResult TrustGraphManipulationDetector::AnalyzeAddress(const uint160& address) {
    TrustManipulationResult result;
    
    // Run all detection algorithms and return the most significant result
    std::vector<TrustManipulationResult> results;
    
    results.push_back(DetectArtificialPathCreation(address));
    results.push_back(DetectCircularTrustRing(address));
    results.push_back(DetectRapidTrustAccumulation(address));
    results.push_back(DetectCoordinatedTrustBoost(address));
    results.push_back(DetectSybilTrustNetwork(address));
    results.push_back(DetectTrustWashing(address));
    results.push_back(DetectReciprocalTrustAbuse(address));
    
    // Find the result with highest confidence
    for (const auto& r : results) {
        if (r.type != TrustManipulationResult::NONE && r.confidence > result.confidence) {
            result = r;
        }
    }
    
    // Flag address if manipulation detected with high confidence
    if (result.type != TrustManipulationResult::NONE && result.confidence >= 0.70) {
        FlagAddress(address, result);
    }
    
    return result;
}

TrustManipulationResult TrustGraphManipulationDetector::AnalyzeTrustEdge(const TrustEdge& edge) {
    TrustManipulationResult result;
    
    // Check if either address is already flagged
    if (IsAddressFlagged(edge.fromAddress) || IsAddressFlagged(edge.toAddress)) {
        result.type = TrustManipulationResult::SYBIL_TRUST_NETWORK;
        result.confidence = 0.80;
        result.involvedAddresses.push_back(edge.fromAddress);
        result.involvedAddresses.push_back(edge.toAddress);
        result.suspiciousEdges.push_back(edge);
        result.description = "Trust edge involves flagged address";
        result.escalateToDAO = true;
        return result;
    }
    
    // Check if addresses are in same wallet cluster
    if (AreInSameCluster(edge.fromAddress, edge.toAddress)) {
        result.type = TrustManipulationResult::SYBIL_TRUST_NETWORK;
        result.confidence = 0.95;
        result.involvedAddresses.push_back(edge.fromAddress);
        result.involvedAddresses.push_back(edge.toAddress);
        result.suspiciousEdges.push_back(edge);
        result.description = "Trust edge between addresses in same wallet cluster";
        result.escalateToDAO = true;
        return result;
    }
    
    // Check for reciprocal trust
    TrustEdge reverseEdge;
    if (m_trustGraph.GetTrustEdge(edge.toAddress, edge.fromAddress, reverseEdge)) {
        // Check if weights are suspiciously similar
        int16_t weightDiff = std::abs(edge.trustWeight - reverseEdge.trustWeight);
        uint32_t timeDiff = std::abs((int64_t)edge.timestamp - (int64_t)reverseEdge.timestamp);
        
        if (weightDiff <= 5 && timeDiff <= 3600) {  // Similar weights, within 1 hour
            result.type = TrustManipulationResult::RECIPROCAL_TRUST_ABUSE;
            result.confidence = 0.75;
            result.involvedAddresses.push_back(edge.fromAddress);
            result.involvedAddresses.push_back(edge.toAddress);
            result.suspiciousEdges.push_back(edge);
            result.suspiciousEdges.push_back(reverseEdge);
            result.description = "Suspicious reciprocal trust with similar weights and timing";
            result.escalateToDAO = false;
        }
    }
    
    return result;
}


TrustManipulationResult TrustGraphManipulationDetector::DetectArtificialPathCreation(
    const uint160& targetAddress)
{
    TrustManipulationResult result;
    
    // Get all incoming trust edges
    std::vector<TrustEdge> incomingEdges = m_trustGraph.GetIncomingTrust(targetAddress);
    
    if (incomingEdges.size() < MIN_EDGES_FOR_PATTERN) {
        return result;  // Not enough data
    }
    
    // Analyze patterns
    double timeClusterScore = CalculateTimeClusteringScore(incomingEdges);
    double weightSimilarityScore = CalculateWeightSimilarityScore(incomingEdges);
    
    // Check for addresses with no genuine history
    size_t suspiciousSourceCount = 0;
    std::vector<uint160> suspiciousSources;
    
    for (const auto& edge : incomingEdges) {
        if (!HasGenuineTransactionHistory(edge.fromAddress)) {
            suspiciousSourceCount++;
            suspiciousSources.push_back(edge.fromAddress);
        }
    }
    
    double suspiciousRatio = (double)suspiciousSourceCount / incomingEdges.size();
    
    // Calculate overall confidence
    double confidence = (timeClusterScore * 0.3) + 
                       (weightSimilarityScore * 0.3) + 
                       (suspiciousRatio * 0.4);
    
    if (confidence >= 0.60) {
        result.type = TrustManipulationResult::ARTIFICIAL_PATH_CREATION;
        result.confidence = confidence;
        result.involvedAddresses = suspiciousSources;
        result.involvedAddresses.push_back(targetAddress);
        
        for (const auto& edge : incomingEdges) {
            for (const auto& suspicious : suspiciousSources) {
                if (edge.fromAddress == suspicious) {
                    result.suspiciousEdges.push_back(edge);
                    break;
                }
            }
        }
        
        result.description = "Artificial trust paths detected: " +
                            std::to_string(suspiciousSourceCount) + " suspicious sources, " +
                            "time clustering: " + std::to_string((int)(timeClusterScore * 100)) + "%, " +
                            "weight similarity: " + std::to_string((int)(weightSimilarityScore * 100)) + "%";
        result.escalateToDAO = confidence >= 0.80;
    }
    
    return result;
}

TrustManipulationResult TrustGraphManipulationDetector::DetectCircularTrustRing(
    const uint160& address,
    size_t maxRingSize)
{
    TrustManipulationResult result;
    
    std::vector<uint160> path;
    std::set<uint160> visited;
    
    path.push_back(address);
    visited.insert(address);
    
    if (FindCircularPath(address, address, path, visited, maxRingSize)) {
        result.type = TrustManipulationResult::CIRCULAR_TRUST_RING;
        result.confidence = CIRCULAR_RING_CONFIDENCE_THRESHOLD + 
                           (0.30 * (1.0 - (double)path.size() / maxRingSize));
        result.involvedAddresses = path;
        
        // Collect edges in the ring
        for (size_t i = 0; i < path.size(); i++) {
            size_t nextIdx = (i + 1) % path.size();
            TrustEdge edge;
            if (m_trustGraph.GetTrustEdge(path[i], path[nextIdx], edge)) {
                result.suspiciousEdges.push_back(edge);
            }
        }
        
        result.description = "Circular trust ring detected with " + 
                            std::to_string(path.size()) + " addresses";
        result.escalateToDAO = true;
    }
    
    return result;
}


TrustManipulationResult TrustGraphManipulationDetector::DetectRapidTrustAccumulation(
    const uint160& address,
    uint64_t timeWindow)
{
    TrustManipulationResult result;
    
    std::vector<TrustEdge> incomingEdges = m_trustGraph.GetIncomingTrust(address);
    
    if (incomingEdges.empty()) {
        return result;
    }
    
    // Sort by timestamp
    std::sort(incomingEdges.begin(), incomingEdges.end(),
              [](const TrustEdge& a, const TrustEdge& b) {
                  return a.timestamp < b.timestamp;
              });
    
    uint64_t currentTime = GetTime();
    uint64_t windowStart = currentTime - timeWindow;
    
    // Count edges within time window
    size_t recentEdgeCount = 0;
    int64_t totalRecentWeight = 0;
    std::vector<TrustEdge> recentEdges;
    
    for (const auto& edge : incomingEdges) {
        if (edge.timestamp >= windowStart) {
            recentEdgeCount++;
            totalRecentWeight += edge.trustWeight;
            recentEdges.push_back(edge);
        }
    }
    
    // Calculate accumulation rate
    double edgesPerHour = (double)recentEdgeCount / (timeWindow / 3600.0);
    double weightPerHour = (double)totalRecentWeight / (timeWindow / 3600.0);
    
    // Thresholds for suspicious activity
    const double SUSPICIOUS_EDGES_PER_HOUR = 5.0;
    const double SUSPICIOUS_WEIGHT_PER_HOUR = 200.0;
    
    double edgeRateScore = std::min(1.0, edgesPerHour / SUSPICIOUS_EDGES_PER_HOUR);
    double weightRateScore = std::min(1.0, weightPerHour / SUSPICIOUS_WEIGHT_PER_HOUR);
    
    double confidence = (edgeRateScore * 0.5) + (weightRateScore * 0.5);
    
    if (confidence >= RAPID_ACCUMULATION_THRESHOLD) {
        result.type = TrustManipulationResult::RAPID_TRUST_ACCUMULATION;
        result.confidence = confidence;
        result.involvedAddresses.push_back(address);
        
        for (const auto& edge : recentEdges) {
            result.involvedAddresses.push_back(edge.fromAddress);
            result.suspiciousEdges.push_back(edge);
        }
        
        result.description = "Rapid trust accumulation: " +
                            std::to_string(recentEdgeCount) + " edges in " +
                            std::to_string(timeWindow / 3600) + " hours, " +
                            "total weight: " + std::to_string(totalRecentWeight);
        result.escalateToDAO = confidence >= 0.90;
    }
    
    return result;
}

TrustManipulationResult TrustGraphManipulationDetector::DetectCoordinatedTrustBoost(
    const uint160& targetAddress)
{
    TrustManipulationResult result;
    
    std::vector<TrustEdge> incomingEdges = m_trustGraph.GetIncomingTrust(targetAddress);
    
    if (incomingEdges.size() < MIN_EDGES_FOR_PATTERN) {
        return result;
    }
    
    // Group edges by time window
    std::map<uint64_t, std::vector<TrustEdge>> timeGroups;
    
    for (const auto& edge : incomingEdges) {
        uint64_t timeSlot = edge.timestamp / COORDINATED_TIME_WINDOW;
        timeGroups[timeSlot].push_back(edge);
    }
    
    // Find suspicious time groups
    std::vector<TrustEdge> suspiciousEdges;
    std::set<uint160> suspiciousAddresses;
    
    for (const auto& group : timeGroups) {
        if (group.second.size() >= 3) {  // 3+ edges in same time window
            // Check if sources are in same cluster
            size_t clusterMatches = 0;
            for (size_t i = 0; i < group.second.size(); i++) {
                for (size_t j = i + 1; j < group.second.size(); j++) {
                    if (AreInSameCluster(group.second[i].fromAddress, 
                                        group.second[j].fromAddress)) {
                        clusterMatches++;
                    }
                }
            }
            
            double clusterRatio = (double)clusterMatches / 
                                 (group.second.size() * (group.second.size() - 1) / 2);
            
            if (clusterRatio >= 0.30 || group.second.size() >= 5) {
                for (const auto& edge : group.second) {
                    suspiciousEdges.push_back(edge);
                    suspiciousAddresses.insert(edge.fromAddress);
                }
            }
        }
    }
    
    if (!suspiciousEdges.empty()) {
        double confidence = std::min(1.0, (double)suspiciousEdges.size() / 10.0);
        
        if (confidence >= COORDINATED_BOOST_THRESHOLD) {
            result.type = TrustManipulationResult::COORDINATED_TRUST_BOOST;
            result.confidence = confidence;
            result.involvedAddresses.push_back(targetAddress);
            for (const auto& addr : suspiciousAddresses) {
                result.involvedAddresses.push_back(addr);
            }
            result.suspiciousEdges = suspiciousEdges;
            result.description = "Coordinated trust boost: " +
                                std::to_string(suspiciousEdges.size()) + " edges from " +
                                std::to_string(suspiciousAddresses.size()) + " addresses";
            result.escalateToDAO = confidence >= 0.85;
        }
    }
    
    return result;
}


TrustManipulationResult TrustGraphManipulationDetector::DetectSybilTrustNetwork(
    const uint160& address)
{
    TrustManipulationResult result;
    
    // Get wallet cluster for address
    std::set<uint160> cluster = m_clusterer.GetClusterMembers(address);
    
    if (cluster.size() <= 1) {
        return result;  // Not in a cluster
    }
    
    // Check for trust edges within the cluster
    std::vector<TrustEdge> intraClusterEdges;
    
    for (const auto& addr1 : cluster) {
        for (const auto& addr2 : cluster) {
            if (addr1 != addr2) {
                TrustEdge edge;
                if (m_trustGraph.GetTrustEdge(addr1, addr2, edge)) {
                    intraClusterEdges.push_back(edge);
                }
            }
        }
    }
    
    // Calculate intra-cluster trust density
    size_t maxPossibleEdges = cluster.size() * (cluster.size() - 1);
    double trustDensity = (double)intraClusterEdges.size() / maxPossibleEdges;
    
    // High density of trust within a wallet cluster is suspicious
    if (trustDensity >= 0.30 && intraClusterEdges.size() >= 3) {
        result.type = TrustManipulationResult::SYBIL_TRUST_NETWORK;
        result.confidence = std::min(1.0, trustDensity + 0.50);
        
        for (const auto& addr : cluster) {
            result.involvedAddresses.push_back(addr);
        }
        result.suspiciousEdges = intraClusterEdges;
        
        result.description = "Sybil trust network: " +
                            std::to_string(cluster.size()) + " addresses in cluster, " +
                            std::to_string(intraClusterEdges.size()) + " intra-cluster edges, " +
                            "density: " + std::to_string((int)(trustDensity * 100)) + "%";
        result.escalateToDAO = true;
    }
    
    return result;
}

TrustManipulationResult TrustGraphManipulationDetector::DetectTrustWashing(
    const uint160& address)
{
    TrustManipulationResult result;
    
    // Get incoming trust edges
    std::vector<TrustEdge> incomingEdges = m_trustGraph.GetIncomingTrust(address);
    
    if (incomingEdges.empty()) {
        return result;
    }
    
    // For each incoming edge, check if the source is an intermediary
    std::vector<TrustEdge> suspiciousEdges;
    std::set<uint160> intermediaries;
    
    for (const auto& edge : incomingEdges) {
        uint160 source = edge.fromAddress;
        
        // Check if source was recently created
        uint64_t sourceCreationTime = GetAddressCreationTime(source);
        uint64_t edgeCreationTime = edge.timestamp;
        
        // If source was created shortly before the trust edge, suspicious
        if (edgeCreationTime - sourceCreationTime < 86400) {  // Within 24 hours
            // Check if source has incoming trust from others
            std::vector<TrustEdge> sourceIncoming = m_trustGraph.GetIncomingTrust(source);
            
            if (!sourceIncoming.empty()) {
                // Source received trust and immediately passed it on
                // This is a trust washing pattern
                suspiciousEdges.push_back(edge);
                intermediaries.insert(source);
            }
        }
    }
    
    if (!suspiciousEdges.empty()) {
        double confidence = std::min(1.0, (double)suspiciousEdges.size() / 5.0);
        
        if (confidence >= TRUST_WASHING_THRESHOLD) {
            result.type = TrustManipulationResult::TRUST_WASHING;
            result.confidence = confidence;
            result.involvedAddresses.push_back(address);
            for (const auto& addr : intermediaries) {
                result.involvedAddresses.push_back(addr);
            }
            result.suspiciousEdges = suspiciousEdges;
            result.description = "Trust washing detected: " +
                                std::to_string(intermediaries.size()) + " intermediary addresses";
            result.escalateToDAO = confidence >= 0.85;
        }
    }
    
    return result;
}

TrustManipulationResult TrustGraphManipulationDetector::DetectReciprocalTrustAbuse(
    const uint160& address)
{
    TrustManipulationResult result;
    
    // Get outgoing trust edges
    std::vector<TrustEdge> outgoingEdges = m_trustGraph.GetOutgoingTrust(address);
    
    std::vector<std::pair<TrustEdge, TrustEdge>> reciprocalPairs;
    
    for (const auto& outEdge : outgoingEdges) {
        TrustEdge inEdge;
        if (m_trustGraph.GetTrustEdge(outEdge.toAddress, address, inEdge)) {
            // Found reciprocal trust
            // Check if it's suspicious
            int16_t weightDiff = std::abs(outEdge.trustWeight - inEdge.trustWeight);
            uint32_t timeDiff = std::abs((int64_t)outEdge.timestamp - (int64_t)inEdge.timestamp);
            
            // Suspicious if:
            // 1. Weights are very similar (within 10 points)
            // 2. Created within short time window (1 hour)
            // 3. Both addresses have limited other activity
            bool weightSuspicious = weightDiff <= 10;
            bool timeSuspicious = timeDiff <= 3600;
            bool activitySuspicious = GetAddressActivityCount(outEdge.toAddress) < 10;
            
            if (weightSuspicious && timeSuspicious && activitySuspicious) {
                reciprocalPairs.push_back({outEdge, inEdge});
            }
        }
    }
    
    if (!reciprocalPairs.empty()) {
        double confidence = std::min(1.0, (double)reciprocalPairs.size() / 3.0);
        
        if (confidence >= RECIPROCAL_ABUSE_THRESHOLD) {
            result.type = TrustManipulationResult::RECIPROCAL_TRUST_ABUSE;
            result.confidence = confidence;
            result.involvedAddresses.push_back(address);
            
            for (const auto& pair : reciprocalPairs) {
                result.involvedAddresses.push_back(pair.first.toAddress);
                result.suspiciousEdges.push_back(pair.first);
                result.suspiciousEdges.push_back(pair.second);
            }
            
            result.description = "Reciprocal trust abuse: " +
                                std::to_string(reciprocalPairs.size()) + " suspicious pairs";
            result.escalateToDAO = confidence >= 0.80;
        }
    }
    
    return result;
}


TrustEdgePattern TrustGraphManipulationDetector::GetTrustEdgePattern(
    const uint160& address,
    bool incoming)
{
    TrustEdgePattern pattern;
    pattern.sourceAddress = address;
    
    if (incoming) {
        pattern.edges = m_trustGraph.GetIncomingTrust(address);
    } else {
        pattern.edges = m_trustGraph.GetOutgoingTrust(address);
    }
    
    if (pattern.edges.empty()) {
        return pattern;
    }
    
    pattern.edgeCount = pattern.edges.size();
    
    // Calculate statistics
    uint64_t minTime = UINT64_MAX;
    uint64_t maxTime = 0;
    int64_t totalWeight = 0;
    
    for (const auto& edge : pattern.edges) {
        if (edge.timestamp < minTime) minTime = edge.timestamp;
        if (edge.timestamp > maxTime) maxTime = edge.timestamp;
        totalWeight += edge.trustWeight;
    }
    
    pattern.firstEdgeTime = minTime;
    pattern.lastEdgeTime = maxTime;
    pattern.averageWeight = (double)totalWeight / pattern.edgeCount;
    
    return pattern;
}

int16_t TrustGraphManipulationDetector::CalculateTrustGraphHealthScore(const uint160& address) {
    int16_t score = 100;  // Start with perfect score
    
    // Run all detection algorithms
    TrustManipulationResult result = AnalyzeAddress(address);
    
    // Deduct points based on manipulation type and confidence
    if (result.type != TrustManipulationResult::NONE) {
        int16_t deduction = static_cast<int16_t>(result.confidence * 50);
        
        // Additional deductions for severe manipulation types
        switch (result.type) {
            case TrustManipulationResult::SYBIL_TRUST_NETWORK:
                deduction += 30;
                break;
            case TrustManipulationResult::CIRCULAR_TRUST_RING:
                deduction += 25;
                break;
            case TrustManipulationResult::COORDINATED_TRUST_BOOST:
                deduction += 20;
                break;
            case TrustManipulationResult::ARTIFICIAL_PATH_CREATION:
                deduction += 15;
                break;
            case TrustManipulationResult::TRUST_WASHING:
                deduction += 15;
                break;
            case TrustManipulationResult::RAPID_TRUST_ACCUMULATION:
                deduction += 10;
                break;
            case TrustManipulationResult::RECIPROCAL_TRUST_ABUSE:
                deduction += 10;
                break;
            default:
                break;
        }
        
        score -= deduction;
    }
    
    // Check if address is flagged
    if (IsAddressFlagged(address)) {
        score -= 20;
    }
    
    return std::max(int16_t(0), std::min(int16_t(100), score));
}

void TrustGraphManipulationDetector::FlagAddress(
    const uint160& address,
    const TrustManipulationResult& result)
{
    m_flaggedAddresses[address] = result;
    
    // Persist to database
    WriteToDatabase(m_db, MakeDBKey(DB_FLAGGED_ADDRESS, address), result);
    
    LogPrint(BCLog::CVM, "Trust Graph Manipulation: Flagged address %s - Type: %d, Confidence: %.2f\n",
             address.ToString(), (int)result.type, result.confidence);
}

bool TrustGraphManipulationDetector::IsAddressFlagged(const uint160& address) const {
    return m_flaggedAddresses.find(address) != m_flaggedAddresses.end();
}

std::set<uint160> TrustGraphManipulationDetector::GetFlaggedAddresses() const {
    std::set<uint160> addresses;
    for (const auto& pair : m_flaggedAddresses) {
        addresses.insert(pair.first);
    }
    return addresses;
}

void TrustGraphManipulationDetector::UnflagAddress(const uint160& address) {
    m_flaggedAddresses.erase(address);
    
    // Remove from database
    std::string key = MakeDBKey(DB_FLAGGED_ADDRESS, address);
    std::vector<uint8_t> emptyData;
    m_db.WriteGeneric(key, emptyData);  // Write empty to effectively delete
    
    LogPrint(BCLog::CVM, "Trust Graph Manipulation: Unflagged address %s\n", address.ToString());
}

void TrustGraphManipulationDetector::SaveFlaggedAddresses() {
    for (const auto& pair : m_flaggedAddresses) {
        WriteToDatabase(m_db, MakeDBKey(DB_FLAGGED_ADDRESS, pair.first), pair.second);
    }
}

void TrustGraphManipulationDetector::LoadFlaggedAddresses() {
    // Load all flagged addresses from database
    std::vector<std::string> keys = m_db.ListKeysWithPrefix(std::string(1, DB_FLAGGED_ADDRESS));
    
    for (const auto& key : keys) {
        if (key.length() > 1) {
            std::string addrHex = key.substr(1);
            uint160 address;
            address.SetHex(addrHex);
            
            TrustManipulationResult result;
            if (ReadFromDatabase(m_db, key, result)) {
                m_flaggedAddresses[address] = result;
            }
        }
    }
    
    LogPrint(BCLog::CVM, "Trust Graph Manipulation: Loaded %zu flagged addresses\n",
             m_flaggedAddresses.size());
}


// Private helper methods

bool TrustGraphManipulationDetector::FindCircularPath(
    const uint160& start,
    const uint160& current,
    std::vector<uint160>& path,
    std::set<uint160>& visited,
    size_t maxDepth)
{
    if (path.size() > maxDepth) {
        return false;
    }
    
    // Get outgoing edges from current
    std::vector<TrustEdge> outgoing = m_trustGraph.GetOutgoingTrust(current);
    
    for (const auto& edge : outgoing) {
        // Check if we've found a cycle back to start
        if (edge.toAddress == start && path.size() >= 3) {
            return true;  // Found circular path
        }
        
        // Continue searching if not visited
        if (visited.find(edge.toAddress) == visited.end()) {
            visited.insert(edge.toAddress);
            path.push_back(edge.toAddress);
            
            if (FindCircularPath(start, edge.toAddress, path, visited, maxDepth)) {
                return true;
            }
            
            path.pop_back();
            visited.erase(edge.toAddress);
        }
    }
    
    return false;
}

double TrustGraphManipulationDetector::CalculateTimeClusteringScore(
    const std::vector<TrustEdge>& edges)
{
    if (edges.size() < 2) {
        return 0.0;
    }
    
    // Calculate time differences between consecutive edges
    std::vector<uint32_t> timestamps;
    for (const auto& edge : edges) {
        timestamps.push_back(edge.timestamp);
    }
    std::sort(timestamps.begin(), timestamps.end());
    
    std::vector<uint32_t> timeDiffs;
    for (size_t i = 1; i < timestamps.size(); i++) {
        timeDiffs.push_back(timestamps[i] - timestamps[i-1]);
    }
    
    // Calculate mean and standard deviation
    double mean = 0;
    for (const auto& diff : timeDiffs) {
        mean += diff;
    }
    mean /= timeDiffs.size();
    
    double variance = 0;
    for (const auto& diff : timeDiffs) {
        variance += (diff - mean) * (diff - mean);
    }
    variance /= timeDiffs.size();
    double stdDev = std::sqrt(variance);
    
    // Low standard deviation relative to mean indicates clustering
    // Normalize to 0-1 score
    if (mean == 0) {
        return 1.0;  // All at same time = maximum clustering
    }
    
    double coefficientOfVariation = stdDev / mean;
    return std::max(0.0, 1.0 - coefficientOfVariation);
}

double TrustGraphManipulationDetector::CalculateWeightSimilarityScore(
    const std::vector<TrustEdge>& edges)
{
    if (edges.size() < 2) {
        return 0.0;
    }
    
    // Calculate mean weight
    double mean = 0;
    for (const auto& edge : edges) {
        mean += edge.trustWeight;
    }
    mean /= edges.size();
    
    // Calculate standard deviation
    double variance = 0;
    for (const auto& edge : edges) {
        variance += (edge.trustWeight - mean) * (edge.trustWeight - mean);
    }
    variance /= edges.size();
    double stdDev = std::sqrt(variance);
    
    // Low standard deviation indicates similar weights
    // Normalize to 0-1 score (max weight range is 200: -100 to +100)
    return std::max(0.0, 1.0 - (stdDev / 50.0));
}

bool TrustGraphManipulationDetector::AreInSameCluster(
    const uint160& addr1,
    const uint160& addr2)
{
    uint160 cluster1 = m_clusterer.GetClusterForAddress(addr1);
    uint160 cluster2 = m_clusterer.GetClusterForAddress(addr2);
    
    // If either is not in a cluster, they're not in the same cluster
    if (cluster1.IsNull() || cluster2.IsNull()) {
        return false;
    }
    
    return cluster1 == cluster2;
}

uint64_t TrustGraphManipulationDetector::GetAddressCreationTime(const uint160& address) {
    // Query database for first seen time
    std::string key = "first_seen_" + address.ToString();
    std::vector<uint8_t> data;
    
    if (m_db.ReadGeneric(key, data) && data.size() >= 8) {
        uint64_t timestamp;
        memcpy(&timestamp, data.data(), sizeof(timestamp));
        return timestamp;
    }
    
    // Default to current time if not found
    return GetTime();
}

uint32_t TrustGraphManipulationDetector::GetAddressActivityCount(const uint160& address) {
    // Query database for activity count
    std::string key = "activity_count_" + address.ToString();
    std::vector<uint8_t> data;
    
    if (m_db.ReadGeneric(key, data) && data.size() >= 4) {
        uint32_t count;
        memcpy(&count, data.data(), sizeof(count));
        return count;
    }
    
    // Default to 0 if not found
    return 0;
}

bool TrustGraphManipulationDetector::HasGenuineTransactionHistory(const uint160& address) {
    // An address has genuine history if:
    // 1. It has been active for more than 7 days
    // 2. It has more than 5 transactions
    // 3. It has interacted with more than 3 unique addresses
    
    uint64_t creationTime = GetAddressCreationTime(address);
    uint64_t currentTime = GetTime();
    uint64_t ageSeconds = currentTime - creationTime;
    
    if (ageSeconds < 7 * 24 * 3600) {  // Less than 7 days old
        return false;
    }
    
    uint32_t activityCount = GetAddressActivityCount(address);
    if (activityCount < 5) {
        return false;
    }
    
    // Check unique interactions
    std::vector<TrustEdge> outgoing = m_trustGraph.GetOutgoingTrust(address);
    std::vector<TrustEdge> incoming = m_trustGraph.GetIncomingTrust(address);
    
    std::set<uint160> uniqueInteractions;
    for (const auto& edge : outgoing) {
        uniqueInteractions.insert(edge.toAddress);
    }
    for (const auto& edge : incoming) {
        uniqueInteractions.insert(edge.fromAddress);
    }
    
    return uniqueInteractions.size() >= 3;
}

} // namespace CVM
