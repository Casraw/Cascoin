// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUST_GRAPH_MANIPULATION_DETECTOR_H
#define CASCOIN_CVM_TRUST_GRAPH_MANIPULATION_DETECTOR_H

#include <uint256.h>
#include <amount.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/walletcluster.h>
#include <map>
#include <vector>
#include <set>

namespace CVM {

/**
 * Trust Graph Manipulation Detection Result
 * 
 * Contains the analysis results for potential trust graph manipulation.
 */
struct TrustManipulationResult {
    enum ManipulationType {
        NONE,                           // No manipulation detected
        ARTIFICIAL_PATH_CREATION,       // Fake trust paths created
        CIRCULAR_TRUST_RING,            // Circular trust relationships
        RAPID_TRUST_ACCUMULATION,       // Trust gained too quickly
        COORDINATED_TRUST_BOOST,        // Multiple addresses boosting same target
        SYBIL_TRUST_NETWORK,            // Sybil addresses creating trust
        TRUST_WASHING,                  // Using intermediaries to launder trust
        RECIPROCAL_TRUST_ABUSE          // Mutual trust without genuine relationship
    };
    
    ManipulationType type;
    double confidence;                  // 0.0-1.0 confidence level
    std::vector<uint160> involvedAddresses;
    std::vector<TrustEdge> suspiciousEdges;
    std::string description;
    bool escalateToDAO;
    
    TrustManipulationResult() : type(NONE), confidence(0.0), escalateToDAO(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        unsigned char typeVal = static_cast<unsigned char>(type);
        READWRITE(typeVal);
        type = static_cast<ManipulationType>(typeVal);
        READWRITE(confidence);
        READWRITE(involvedAddresses);
        READWRITE(suspiciousEdges);
        READWRITE(description);
        READWRITE(escalateToDAO);
    }
};

/**
 * Trust Edge Pattern
 * 
 * Represents a pattern of trust edges for analysis.
 */
struct TrustEdgePattern {
    uint160 sourceAddress;
    std::vector<TrustEdge> edges;
    uint64_t firstEdgeTime;
    uint64_t lastEdgeTime;
    double averageWeight;
    size_t edgeCount;
    
    TrustEdgePattern() : firstEdgeTime(0), lastEdgeTime(0), averageWeight(0), edgeCount(0) {}
};

/**
 * Trust Graph Manipulation Detector
 * 
 * Detects various forms of trust graph manipulation including:
 * 1. Artificial trust path creation (fake paths to boost reputation)
 * 2. Circular trust rings (A trusts B trusts C trusts A)
 * 3. Rapid trust accumulation (gaining trust too quickly)
 * 4. Coordinated trust boosting (multiple addresses boosting same target)
 * 5. Sybil trust networks (fake addresses creating trust)
 * 6. Trust washing (using intermediaries to launder bad reputation)
 * 7. Reciprocal trust abuse (mutual trust without genuine relationship)
 */
class TrustGraphManipulationDetector {
public:
    // Detection thresholds
    static constexpr double CIRCULAR_RING_CONFIDENCE_THRESHOLD = 0.70;
    static constexpr double RAPID_ACCUMULATION_THRESHOLD = 0.80;
    static constexpr double COORDINATED_BOOST_THRESHOLD = 0.75;
    static constexpr double SYBIL_NETWORK_THRESHOLD = 0.85;
    static constexpr double TRUST_WASHING_THRESHOLD = 0.70;
    static constexpr double RECIPROCAL_ABUSE_THRESHOLD = 0.65;
    
    // Time-based thresholds (in seconds)
    static constexpr uint64_t RAPID_TRUST_WINDOW = 86400;      // 24 hours
    static constexpr uint64_t COORDINATED_TIME_WINDOW = 3600;  // 1 hour
    static constexpr size_t MIN_EDGES_FOR_PATTERN = 3;
    static constexpr size_t MAX_CIRCULAR_RING_SIZE = 10;
    
    TrustGraphManipulationDetector(CVMDatabase& db, TrustGraph& trustGraph, WalletClusterer& clusterer);
    
    /**
     * Analyze an address for trust graph manipulation
     * 
     * @param address Address to analyze
     * @return Detection result
     */
    TrustManipulationResult AnalyzeAddress(const uint160& address);
    
    /**
     * Analyze a specific trust edge for manipulation
     * 
     * @param edge Trust edge to analyze
     * @return Detection result
     */
    TrustManipulationResult AnalyzeTrustEdge(const TrustEdge& edge);
    
    /**
     * Detect artificial trust path creation
     * 
     * Looks for patterns where trust paths are created artificially
     * to boost an address's reputation without genuine relationships.
     * 
     * Indicators:
     * - Multiple new addresses creating trust to same target
     * - Trust edges created in rapid succession
     * - Addresses with no other activity besides trust creation
     * - Trust paths that bypass natural network growth
     * 
     * @param targetAddress Address to check
     * @return Detection result
     */
    TrustManipulationResult DetectArtificialPathCreation(const uint160& targetAddress);
    
    /**
     * Detect circular trust rings
     * 
     * Finds circular trust relationships where addresses trust each other
     * in a ring pattern to artificially boost reputation.
     * 
     * Example: A → B → C → A (each trusts the next)
     * 
     * @param address Starting address
     * @param maxRingSize Maximum ring size to search
     * @return Detection result
     */
    TrustManipulationResult DetectCircularTrustRing(
        const uint160& address,
        size_t maxRingSize = MAX_CIRCULAR_RING_SIZE
    );
    
    /**
     * Detect rapid trust accumulation
     * 
     * Identifies addresses that gain trust too quickly, which may indicate
     * manipulation rather than organic reputation building.
     * 
     * @param address Address to check
     * @param timeWindow Time window in seconds
     * @return Detection result
     */
    TrustManipulationResult DetectRapidTrustAccumulation(
        const uint160& address,
        uint64_t timeWindow = RAPID_TRUST_WINDOW
    );
    
    /**
     * Detect coordinated trust boosting
     * 
     * Finds patterns where multiple addresses coordinate to boost
     * a target address's trust score.
     * 
     * Indicators:
     * - Multiple trust edges created within short time window
     * - Similar trust weights from different sources
     * - Sources are in same wallet cluster
     * - Sources have similar creation times
     * 
     * @param targetAddress Address being boosted
     * @return Detection result
     */
    TrustManipulationResult DetectCoordinatedTrustBoost(const uint160& targetAddress);
    
    /**
     * Detect Sybil trust networks
     * 
     * Identifies networks of Sybil addresses creating trust relationships
     * to manipulate the trust graph.
     * 
     * Uses wallet clustering to identify addresses controlled by same entity.
     * 
     * @param address Address to check
     * @return Detection result
     */
    TrustManipulationResult DetectSybilTrustNetwork(const uint160& address);
    
    /**
     * Detect trust washing
     * 
     * Identifies patterns where addresses use intermediaries to
     * "wash" their reputation by creating indirect trust paths.
     * 
     * Example: Bad actor A creates intermediary B, gets trust from C to B,
     * then B trusts A, effectively laundering A's reputation.
     * 
     * @param address Address to check
     * @return Detection result
     */
    TrustManipulationResult DetectTrustWashing(const uint160& address);
    
    /**
     * Detect reciprocal trust abuse
     * 
     * Identifies mutual trust relationships that appear artificial
     * (e.g., A trusts B and B trusts A with similar weights, created
     * at similar times, without genuine interaction history).
     * 
     * @param address Address to check
     * @return Detection result
     */
    TrustManipulationResult DetectReciprocalTrustAbuse(const uint160& address);
    
    /**
     * Get trust edge patterns for an address
     * 
     * @param address Address to analyze
     * @param incoming If true, analyze incoming edges; otherwise outgoing
     * @return Trust edge pattern
     */
    TrustEdgePattern GetTrustEdgePattern(const uint160& address, bool incoming);
    
    /**
     * Calculate trust graph health score for an address
     * 
     * Returns a score (0-100) indicating how "healthy" the trust
     * relationships around an address appear.
     * 
     * @param address Address to analyze
     * @return Health score (0-100)
     */
    int16_t CalculateTrustGraphHealthScore(const uint160& address);
    
    /**
     * Flag an address for trust manipulation
     * 
     * @param address Address to flag
     * @param result Detection result
     */
    void FlagAddress(const uint160& address, const TrustManipulationResult& result);
    
    /**
     * Check if address is flagged for trust manipulation
     * 
     * @param address Address to check
     * @return true if flagged
     */
    bool IsAddressFlagged(const uint160& address) const;
    
    /**
     * Get all flagged addresses
     * 
     * @return Set of flagged addresses
     */
    std::set<uint160> GetFlaggedAddresses() const;
    
    /**
     * Unflag an address (after DAO review)
     * 
     * @param address Address to unflag
     */
    void UnflagAddress(const uint160& address);
    
    /**
     * Save flagged addresses to database
     */
    void SaveFlaggedAddresses();
    
    /**
     * Load flagged addresses from database
     */
    void LoadFlaggedAddresses();

private:
    CVMDatabase& m_db;
    TrustGraph& m_trustGraph;
    WalletClusterer& m_clusterer;
    
    // Flagged addresses with their detection results
    std::map<uint160, TrustManipulationResult> m_flaggedAddresses;
    
    // Database keys
    static const char DB_FLAGGED_ADDRESS = 'M';  // 'M' + address -> TrustManipulationResult
    
    /**
     * Helper: Find circular paths starting from an address
     */
    bool FindCircularPath(
        const uint160& start,
        const uint160& current,
        std::vector<uint160>& path,
        std::set<uint160>& visited,
        size_t maxDepth
    );
    
    /**
     * Helper: Calculate time clustering of trust edges
     * 
     * Returns a score (0-1) indicating how clustered in time the edges are.
     * Higher score = more clustered = more suspicious.
     */
    double CalculateTimeClusteringScore(const std::vector<TrustEdge>& edges);
    
    /**
     * Helper: Calculate weight similarity of trust edges
     * 
     * Returns a score (0-1) indicating how similar the weights are.
     * Higher score = more similar = more suspicious.
     */
    double CalculateWeightSimilarityScore(const std::vector<TrustEdge>& edges);
    
    /**
     * Helper: Check if addresses are in same wallet cluster
     */
    bool AreInSameCluster(const uint160& addr1, const uint160& addr2);
    
    /**
     * Helper: Get address creation time (first seen)
     */
    uint64_t GetAddressCreationTime(const uint160& address);
    
    /**
     * Helper: Get address activity count
     */
    uint32_t GetAddressActivityCount(const uint160& address);
    
    /**
     * Helper: Check if address has genuine transaction history
     */
    bool HasGenuineTransactionHistory(const uint160& address);
};

} // namespace CVM

#endif // CASCOIN_CVM_TRUST_GRAPH_MANIPULATION_DETECTOR_H
