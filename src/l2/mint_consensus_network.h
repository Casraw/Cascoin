// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_MINT_CONSENSUS_NETWORK_H
#define CASCOIN_L2_MINT_CONSENSUS_NETWORK_H

/**
 * @file mint_consensus_network.h
 * @brief P2P Network Integration for Mint Consensus
 * 
 * This file implements the network layer for broadcasting and receiving
 * mint confirmations between sequencers. It integrates the MintConsensusManager
 * with the P2P network.
 * 
 * Requirements: 3.1, 3.3
 */

#include <l2/mint_consensus.h>
#include <l2/sequencer_discovery.h>
#include <l2/l2_common.h>
#include <net.h>
#include <netmessagemaker.h>
#include <protocol.h>
#include <serialize.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

// Forward declarations
class CNode;
class CConnman;

namespace l2 {

// ============================================================================
// P2P Message Types for Mint Consensus
// ============================================================================

/** P2P message type for mint confirmation */
static const char* const MSG_MINT_CONFIRMATION = "l2mintconf";

/** P2P message type for requesting confirmations */
static const char* const MSG_GET_MINT_CONFIRMATIONS = "l2getmconf";

/** P2P message type for confirmation inventory */
static const char* const MSG_MINT_CONF_INV = "l2mconfinv";

// ============================================================================
// MintConfirmationMessage Structure
// ============================================================================

/**
 * @brief P2P message wrapper for mint confirmation
 */
struct MintConfirmationMessage {
    /** The mint confirmation data */
    MintConfirmation confirmation;
    
    /** Protocol version */
    uint32_t protocolVersion;
    
    /** L2 chain ID */
    uint32_t chainId;
    
    MintConfirmationMessage()
        : protocolVersion(L2_PROTOCOL_VERSION)
        , chainId(0) {}
    
    MintConfirmationMessage(const MintConfirmation& conf, uint32_t chain)
        : confirmation(conf)
        , protocolVersion(L2_PROTOCOL_VERSION)
        , chainId(chain) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(confirmation);
        READWRITE(protocolVersion);
        READWRITE(chainId);
    }
};

// ============================================================================
// MintConsensusNetwork Class
// ============================================================================

/**
 * @brief Network layer for mint consensus P2P communication
 * 
 * This class handles broadcasting mint confirmations to other sequencers
 * and processing incoming confirmations from the network.
 * 
 * Requirements: 3.1, 3.3
 */
class MintConsensusNetwork {
public:
    /** Callback type for confirmation received notifications */
    using ConfirmationReceivedCallback = std::function<void(const MintConfirmation&, CNode*)>;
    
    /**
     * @brief Construct a MintConsensusNetwork
     * @param chainId The L2 chain ID
     * @param consensusManager Reference to the mint consensus manager
     */
    MintConsensusNetwork(uint32_t chainId, MintConsensusManager& consensusManager);
    
    /**
     * @brief Destructor
     */
    ~MintConsensusNetwork();
    
    /**
     * @brief Initialize the network layer
     * @param connman Pointer to the connection manager
     * @return true if initialization succeeded
     */
    bool Initialize(CConnman* connman);
    
    /**
     * @brief Shutdown the network layer
     */
    void Shutdown();
    
    /**
     * @brief Broadcast a mint confirmation to all sequencer peers
     * 
     * Sends the confirmation to all connected peers that are known
     * sequencers for this L2 chain.
     * 
     * @param confirmation The confirmation to broadcast
     * @return Number of peers the confirmation was sent to
     * 
     * Requirements: 3.1
     */
    size_t BroadcastConfirmation(const MintConfirmation& confirmation);
    
    /**
     * @brief Process an incoming mint confirmation message
     * 
     * Validates the message and forwards it to the consensus manager.
     * 
     * @param msg The confirmation message
     * @param pfrom The peer that sent the message
     * @return true if the confirmation was accepted
     * 
     * Requirements: 3.3
     */
    bool ProcessConfirmationMessage(const MintConfirmationMessage& msg, CNode* pfrom);
    
    /**
     * @brief Process a raw P2P message
     * 
     * Called by the message handler when a mint consensus message is received.
     * 
     * @param strCommand The message command
     * @param vRecv The message data
     * @param pfrom The peer that sent the message
     * @return true if the message was processed
     */
    bool ProcessMessage(const std::string& strCommand, CDataStream& vRecv, CNode* pfrom);
    
    /**
     * @brief Request confirmations for a burn from peers
     * 
     * Sends a request to peers for any confirmations they have for
     * the specified burn transaction.
     * 
     * @param l1TxHash The L1 burn transaction hash
     */
    void RequestConfirmations(const uint256& l1TxHash);
    
    /**
     * @brief Get the number of sequencer peers
     * @return Count of connected sequencer peers
     */
    size_t GetSequencerPeerCount() const;
    
    /**
     * @brief Register callback for confirmation received
     * @param callback Function to call when confirmation is received
     */
    void RegisterConfirmationReceivedCallback(ConfirmationReceivedCallback callback);
    
    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint32_t GetChainId() const { return chainId_; }
    
    /**
     * @brief Check if network layer is initialized
     * @return true if initialized
     */
    bool IsInitialized() const { return connman_ != nullptr; }
    
    /**
     * @brief Get statistics about network activity
     * @return Map of statistic name to value
     */
    std::map<std::string, uint64_t> GetStatistics() const;
    
    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

private:
    /** L2 chain ID */
    uint32_t chainId_;
    
    /** Reference to mint consensus manager */
    MintConsensusManager& consensusManager_;
    
    /** Pointer to connection manager */
    CConnman* connman_;
    
    /** Set of recently broadcast confirmations (to avoid re-broadcast) */
    std::set<uint256> recentlyBroadcast_;
    
    /** Confirmation received callbacks */
    std::vector<ConfirmationReceivedCallback> confirmationReceivedCallbacks_;
    
    /** Statistics */
    std::atomic<uint64_t> confirmationsSent_;
    std::atomic<uint64_t> confirmationsReceived_;
    std::atomic<uint64_t> confirmationsRejected_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_network_;
    
    /** Maximum recently broadcast entries */
    static constexpr size_t MAX_RECENTLY_BROADCAST = 10000;
    
    /**
     * @brief Get list of sequencer peer nodes
     * @return Vector of sequencer peer nodes
     */
    std::vector<CNode*> GetSequencerPeers() const;
    
    /**
     * @brief Send confirmation to a specific peer
     * @param confirmation The confirmation to send
     * @param pnode The peer to send to
     * @return true if sent successfully
     */
    bool SendConfirmation(const MintConfirmation& confirmation, CNode* pnode);
    
    /**
     * @brief Validate an incoming confirmation message
     * @param msg The message to validate
     * @param pfrom The peer that sent it
     * @return true if message is valid
     */
    bool ValidateMessage(const MintConfirmationMessage& msg, CNode* pfrom) const;
    
    /**
     * @brief Notify callbacks of confirmation received
     * @param confirmation The received confirmation
     * @param pfrom The peer that sent it
     */
    void NotifyConfirmationReceived(const MintConfirmation& confirmation, CNode* pfrom);
    
    /**
     * @brief Prune old recently broadcast entries
     */
    void PruneRecentlyBroadcast();
};

/**
 * @brief Global mint consensus network instance getter
 * @return Reference to the global MintConsensusNetwork
 */
MintConsensusNetwork& GetMintConsensusNetwork();

/**
 * @brief Initialize the global mint consensus network
 * @param chainId The L2 chain ID
 * @param consensusManager Reference to mint consensus manager
 */
void InitMintConsensusNetwork(uint32_t chainId, MintConsensusManager& consensusManager);

/**
 * @brief Check if mint consensus network is initialized
 * @return true if initialized
 */
bool IsMintConsensusNetworkInitialized();

} // namespace l2

#endif // CASCOIN_L2_MINT_CONSENSUS_NETWORK_H
