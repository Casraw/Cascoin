// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_SEQUENCER_DISCOVERY_H
#define CASCOIN_L2_SEQUENCER_DISCOVERY_H

/**
 * @file sequencer_discovery.h
 * @brief Sequencer Discovery and Management for Cascoin L2
 * 
 * This file implements the permissionless sequencer network for L2.
 * Sequencers are discovered via P2P gossip protocol and eligibility
 * is determined by HAT v2 score and stake requirements.
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5
 */

#include <l2/l2_common.h>
#include <l2/l2_chainparams.h>
#include <uint256.h>
#include <pubkey.h>
#include <key.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <optional>

// Forward declarations
class CNode;

namespace l2 {

/**
 * @brief Sequencer announcement message for P2P network
 * 
 * This message is broadcast via the SEQANNOUNCE P2P message type
 * to announce a node's availability as a sequencer candidate.
 * 
 * Requirements: 2.5
 */
struct SeqAnnounceMsg {
    /** Sequencer's address (derived from public key) */
    uint160 sequencerAddress;
    
    /** Staked CAS amount in satoshis */
    CAmount stakeAmount;
    
    /** Self-reported HAT v2 score (0-100) */
    uint32_t hatScore;
    
    /** Current L1 block height at announcement time */
    uint64_t blockHeight;
    
    /** Cryptographic signature of the message */
    std::vector<unsigned char> signature;
    
    /** Announcement timestamp (Unix time) */
    uint64_t timestamp;
    
    /** Optional public endpoint for direct connectivity */
    std::string publicEndpoint;
    
    /** Number of connected peers */
    uint32_t peerCount;
    
    /** L2 chain ID this announcement is for */
    uint64_t l2ChainId;
    
    /** Protocol version for compatibility checking */
    uint32_t protocolVersion;

    /** Default constructor */
    SeqAnnounceMsg()
        : stakeAmount(0)
        , hatScore(0)
        , blockHeight(0)
        , timestamp(0)
        , peerCount(0)
        , l2ChainId(DEFAULT_L2_CHAIN_ID)
        , protocolVersion(L2_PROTOCOL_VERSION)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(stakeAmount);
        READWRITE(hatScore);
        READWRITE(blockHeight);
        READWRITE(signature);
        READWRITE(timestamp);
        READWRITE(publicEndpoint);
        READWRITE(peerCount);
        READWRITE(l2ChainId);
        READWRITE(protocolVersion);
    }

    /**
     * @brief Get the hash of the message for signing
     * @return SHA256 hash of message content (excluding signature)
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << sequencerAddress;
        ss << stakeAmount;
        ss << hatScore;
        ss << blockHeight;
        ss << timestamp;
        ss << publicEndpoint;
        ss << peerCount;
        ss << l2ChainId;
        ss << protocolVersion;
        return ss.GetHash();
    }

    /**
     * @brief Sign the message with a private key
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key) {
        uint256 hash = GetSigningHash();
        return key.Sign(hash, signature);
    }

    /**
     * @brief Verify the message signature
     * @param pubkey The public key to verify against
     * @return true if signature is valid
     */
    bool VerifySignature(const CPubKey& pubkey) const {
        if (signature.empty()) {
            return false;
        }
        uint256 hash = GetSigningHash();
        return pubkey.Verify(hash, signature);
    }

    /**
     * @brief Check if the message is expired
     * @param maxAgeSeconds Maximum age in seconds
     * @return true if message is older than maxAgeSeconds
     */
    bool IsExpired(uint64_t maxAgeSeconds = 3600) const {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return (now > timestamp) && (now - timestamp > maxAgeSeconds);
    }

    /**
     * @brief Check if the message is from the future
     * @param maxFutureSeconds Maximum allowed future time
     * @return true if timestamp is too far in the future
     */
    bool IsFromFuture(uint64_t maxFutureSeconds = 60) const {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return timestamp > now + maxFutureSeconds;
    }
};

/**
 * @brief Sequencer attestation from another node
 * 
 * Used for distributed verification of sequencer eligibility.
 * Multiple attestations from different nodes provide confidence
 * in the sequencer's claimed properties.
 */
struct SequencerAttestation {
    /** Address of the sequencer being attested */
    uint160 sequencerAddress;
    
    /** Address of the attesting node */
    uint160 attesterAddress;
    
    /** Attested HAT v2 score */
    uint32_t attestedHatScore;
    
    /** Attested stake amount */
    CAmount attestedStake;
    
    /** L1 block height at attestation time */
    uint64_t blockHeight;
    
    /** Attestation timestamp */
    uint64_t timestamp;
    
    /** Signature of the attestation */
    std::vector<unsigned char> signature;

    SequencerAttestation()
        : attestedHatScore(0)
        , attestedStake(0)
        , blockHeight(0)
        , timestamp(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(attesterAddress);
        READWRITE(attestedHatScore);
        READWRITE(attestedStake);
        READWRITE(blockHeight);
        READWRITE(timestamp);
        READWRITE(signature);
    }

    /**
     * @brief Get the hash for signing
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << sequencerAddress;
        ss << attesterAddress;
        ss << attestedHatScore;
        ss << attestedStake;
        ss << blockHeight;
        ss << timestamp;
        return ss.GetHash();
    }
};

/**
 * @brief Information about a known sequencer
 * 
 * Aggregates announcement data with verification status
 * and performance metrics.
 */
struct SequencerInfo {
    /** Sequencer's address */
    uint160 address;
    
    /** Public key for signature verification */
    CPubKey pubkey;
    
    /** Verified stake amount on L1 */
    CAmount verifiedStake;
    
    /** Verified HAT v2 score */
    uint32_t verifiedHatScore;
    
    /** Number of connected peers */
    uint32_t peerCount;
    
    /** Public endpoint (if available) */
    std::string publicEndpoint;
    
    /** Last announcement timestamp */
    uint64_t lastAnnouncement;
    
    /** Last block produced (if any) */
    uint64_t lastBlockProduced;
    
    /** Number of blocks produced */
    uint64_t blocksProduced;
    
    /** Number of missed block opportunities */
    uint64_t blocksMissed;
    
    /** Whether eligibility has been verified */
    bool isVerified;
    
    /** Whether currently eligible to sequence */
    bool isEligible;
    
    /** Number of attestations received */
    uint32_t attestationCount;
    
    /** L2 chain ID */
    uint64_t l2ChainId;

    SequencerInfo()
        : verifiedStake(0)
        , verifiedHatScore(0)
        , peerCount(0)
        , lastAnnouncement(0)
        , lastBlockProduced(0)
        , blocksProduced(0)
        , blocksMissed(0)
        , isVerified(false)
        , isEligible(false)
        , attestationCount(0)
        , l2ChainId(DEFAULT_L2_CHAIN_ID)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(pubkey);
        READWRITE(verifiedStake);
        READWRITE(verifiedHatScore);
        READWRITE(peerCount);
        READWRITE(publicEndpoint);
        READWRITE(lastAnnouncement);
        READWRITE(lastBlockProduced);
        READWRITE(blocksProduced);
        READWRITE(blocksMissed);
        READWRITE(isVerified);
        READWRITE(isEligible);
        READWRITE(attestationCount);
        READWRITE(l2ChainId);
    }

    /**
     * @brief Calculate sequencer uptime percentage
     * @return Uptime as percentage (0-100)
     */
    double GetUptimePercent() const {
        uint64_t total = blocksProduced + blocksMissed;
        if (total == 0) return 100.0;
        return (static_cast<double>(blocksProduced) / total) * 100.0;
    }

    /**
     * @brief Calculate sequencer weight for leader election
     * 
     * Weight is based on reputation and stake, used for
     * weighted random selection in leader election.
     * 
     * @return Weight value (higher = more likely to be selected)
     */
    uint64_t GetWeight() const {
        // Weight = HAT score * sqrt(stake in CAS)
        // This balances reputation and economic stake
        uint64_t stakeInCas = verifiedStake / COIN;
        uint64_t sqrtStake = 1;
        if (stakeInCas > 0) {
            // Integer square root approximation
            sqrtStake = 1;
            while (sqrtStake * sqrtStake < stakeInCas) {
                sqrtStake++;
            }
        }
        return static_cast<uint64_t>(verifiedHatScore) * sqrtStake;
    }
};


/**
 * @brief Sequencer Discovery and Management
 * 
 * Manages the permissionless sequencer network through P2P gossip.
 * Handles sequencer announcements, eligibility verification, and
 * maintains a local registry of known sequencers.
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6
 */
class SequencerDiscovery {
public:
    /**
     * @brief Construct a new Sequencer Discovery instance
     * @param chainId The L2 chain ID
     */
    explicit SequencerDiscovery(uint64_t chainId);

    /**
     * @brief Announce this node as a sequencer candidate
     * 
     * Creates and broadcasts a SEQANNOUNCE message to the P2P network.
     * The node must meet minimum eligibility requirements.
     * 
     * @param signingKey The key to sign the announcement
     * @param stakeAmount The amount staked on L1
     * @param hatScore The node's HAT v2 score
     * @param publicEndpoint Optional public endpoint
     * @return true if announcement was created and broadcast
     * 
     * Requirements: 2.5
     */
    bool AnnounceAsSequencer(
        const CKey& signingKey,
        CAmount stakeAmount,
        uint32_t hatScore,
        const std::string& publicEndpoint = "");

    /**
     * @brief Process an incoming sequencer announcement
     * 
     * Validates the announcement and updates the local registry.
     * Invalid or expired announcements are rejected.
     * 
     * @param msg The announcement message
     * @param pfrom The peer that sent the message (may be nullptr for local)
     * @return true if announcement was accepted
     * 
     * Requirements: 2.2
     */
    bool ProcessSeqAnnounce(const SeqAnnounceMsg& msg, CNode* pfrom);

    /**
     * @brief Get list of all eligible sequencers
     * 
     * Returns sequencers that meet all eligibility criteria:
     * - HAT v2 score >= minimum threshold
     * - Stake >= minimum stake
     * - Recent announcement (not expired)
     * - Verified eligibility
     * 
     * @return Vector of eligible sequencer info
     * 
     * Requirements: 2.1, 2.3
     */
    std::vector<SequencerInfo> GetEligibleSequencers() const;

    /**
     * @brief Check if an address is an eligible sequencer
     * @param address The sequencer address to check
     * @return true if address is eligible
     * 
     * Requirements: 2.3
     */
    bool IsEligibleSequencer(const uint160& address) const;

    /**
     * @brief Verify sequencer eligibility via distributed attestation
     * 
     * Requests attestations from random peers to verify the
     * sequencer's claimed HAT score and stake.
     * 
     * @param address The sequencer address to verify
     * @return true if verification succeeded
     * 
     * Requirements: 2.3, 2.4
     */
    bool VerifySequencerEligibility(const uint160& address);

    /**
     * @brief Verify stake on L1 for a sequencer
     * @param address The sequencer address
     * @param minStake Minimum required stake
     * @return true if stake is verified and sufficient
     * 
     * Requirements: 2.3
     */
    bool VerifyStakeOnL1(const uint160& address, CAmount minStake);

    /**
     * @brief Verify HAT v2 score for a sequencer
     * @param address The sequencer address
     * @param minScore Minimum required score
     * @return true if score is verified and sufficient
     * 
     * Requirements: 2.4
     */
    bool VerifyHatScore(const uint160& address, uint32_t minScore);

    /**
     * @brief Process an attestation from another node
     * @param attestation The attestation to process
     * @return true if attestation was accepted
     */
    bool ProcessAttestation(const SequencerAttestation& attestation);

    /**
     * @brief Get sequencer info by address
     * @param address The sequencer address
     * @return Optional containing info if found
     */
    std::optional<SequencerInfo> GetSequencerInfo(const uint160& address) const;

    /**
     * @brief Get all known sequencers (including ineligible)
     * @return Vector of all sequencer info
     */
    std::vector<SequencerInfo> GetAllSequencers() const;

    /**
     * @brief Get the number of known sequencers
     * @return Count of sequencers in registry
     */
    size_t GetSequencerCount() const;

    /**
     * @brief Get the number of eligible sequencers
     * @return Count of eligible sequencers
     */
    size_t GetEligibleCount() const;

    /**
     * @brief Remove expired sequencer entries
     * @param maxAge Maximum age in seconds before removal
     * @return Number of entries removed
     */
    size_t PruneExpiredSequencers(uint64_t maxAge = 3600);

    /**
     * @brief Update sequencer performance metrics
     * @param address The sequencer address
     * @param producedBlock Whether a block was produced
     */
    void UpdateSequencerMetrics(const uint160& address, bool producedBlock);

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Clear all sequencer data (for testing)
     */
    void Clear();

    /**
     * @brief Check if this node is registered as a sequencer
     * @return true if this node has announced as sequencer
     */
    bool IsLocalSequencer() const { return isLocalSequencer_; }

    /**
     * @brief Get the local sequencer address (if registered)
     * @return Local sequencer address or empty
     */
    uint160 GetLocalSequencerAddress() const { return localSequencerAddress_; }

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Local registry of known sequencers */
    std::map<uint160, SequencerInfo> sequencerRegistry_;

    /** Attestation cache (sequencer address -> attestations) */
    std::map<uint160, std::vector<SequencerAttestation>> attestationCache_;

    /** Whether this node is a sequencer */
    bool isLocalSequencer_;

    /** Local sequencer address (if registered) */
    uint160 localSequencerAddress_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_sequencers_;

    /** Maximum attestations to cache per sequencer */
    static constexpr size_t MAX_ATTESTATIONS_PER_SEQUENCER = 100;

    /** Maximum sequencers to track */
    static constexpr size_t MAX_SEQUENCERS = 1000;

    /** Announcement expiry time in seconds */
    static constexpr uint64_t ANNOUNCEMENT_EXPIRY_SECONDS = 3600;

    /** Minimum attestations required for verification */
    static constexpr uint32_t MIN_ATTESTATIONS_FOR_VERIFICATION = 3;

    /**
     * @brief Request attestations from random peers
     * @param sequencerAddr The sequencer to get attestations for
     */
    void RequestAttestations(const uint160& sequencerAddr);

    /**
     * @brief Check if sequencer meets minimum requirements
     * @param info The sequencer info to check
     * @return true if meets requirements
     */
    bool MeetsMinimumRequirements(const SequencerInfo& info) const;

    /**
     * @brief Broadcast announcement to P2P network
     * @param msg The announcement message
     */
    void BroadcastAnnouncement(const SeqAnnounceMsg& msg);

    /**
     * @brief Get current L1 block height
     * @return Current L1 block height
     */
    uint64_t GetCurrentL1BlockHeight() const;

    /**
     * @brief Get current peer count
     * @return Number of connected peers
     */
    uint32_t GetCurrentPeerCount() const;
};

/**
 * @brief Global sequencer discovery instance
 * 
 * Provides access to the singleton sequencer discovery manager.
 * Must be initialized during node startup.
 */
SequencerDiscovery& GetSequencerDiscovery();

/**
 * @brief Initialize the global sequencer discovery
 * @param chainId The L2 chain ID
 */
void InitSequencerDiscovery(uint64_t chainId);

/**
 * @brief Check if sequencer discovery is initialized
 * @return true if initialized
 */
bool IsSequencerDiscoveryInitialized();

} // namespace l2

#endif // CASCOIN_L2_SEQUENCER_DISCOVERY_H
