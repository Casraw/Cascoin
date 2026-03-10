// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_MINT_CONSENSUS_H
#define CASCOIN_L2_MINT_CONSENSUS_H

/**
 * @file mint_consensus.h
 * @brief Mint Consensus Manager for L2 Burn-and-Mint Token Model
 * 
 * This file implements the consensus protocol for minting L2 tokens after
 * CAS is burned on L1. The system requires 2/3 sequencer consensus before
 * tokens can be minted, ensuring no single actor can manipulate the system.
 * 
 * Flow:
 * 1. Sequencer detects valid burn on L1
 * 2. Sequencer submits MintConfirmation
 * 3. System collects confirmations from other sequencers
 * 4. When 2/3 consensus reached, tokens are minted
 * 
 * Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6
 */

#include <l2/burn_parser.h>
#include <l2/l2_common.h>
#include <amount.h>
#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <serialize.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Forward declaration
class CNode;

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Consensus threshold (2/3 = 0.6666...) */
static constexpr double MINT_CONSENSUS_THRESHOLD = 2.0 / 3.0;

/** Consensus timeout in seconds (10 minutes) */
static constexpr uint64_t MINT_CONSENSUS_TIMEOUT_SECONDS = 600;

/** Minimum number of sequencers required for consensus */
static constexpr size_t MIN_SEQUENCERS_FOR_CONSENSUS = 3;

// ============================================================================
// MintConfirmation Structure
// ============================================================================

/**
 * @brief Confirmation from a sequencer for a burn transaction
 * 
 * When a sequencer detects a valid burn transaction on L1, it creates
 * a MintConfirmation and broadcasts it to other sequencers. The confirmation
 * contains all information needed to verify the burn and mint tokens.
 * 
 * Requirements: 3.2
 */
struct MintConfirmation {
    /** L1 burn transaction hash - unique identifier */
    uint256 l1TxHash;
    
    /** L2 recipient address (Hash160 of public key) */
    uint160 l2Recipient;
    
    /** Amount to mint (must match burn amount) */
    CAmount amount;
    
    /** Address of the confirming sequencer */
    uint160 sequencerAddress;
    
    /** Cryptographic signature of the confirmation */
    std::vector<unsigned char> signature;
    
    /** Timestamp when confirmation was created (Unix time) */
    uint64_t timestamp;
    
    /** Default constructor */
    MintConfirmation()
        : amount(0)
        , timestamp(0) {}
    
    /** Full constructor */
    MintConfirmation(
        const uint256& txHash,
        const uint160& recipient,
        CAmount amt,
        const uint160& sequencer,
        uint64_t ts = 0)
        : l1TxHash(txHash)
        , l2Recipient(recipient)
        , amount(amt)
        , sequencerAddress(sequencer)
        , timestamp(ts)
    {
        if (timestamp == 0) {
            timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }
    
    /**
     * @brief Get the hash of this confirmation for signing
     * @return SHA256 hash of the confirmation data (excluding signature)
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << l1TxHash;
        ss << l2Recipient;
        ss << amount;
        ss << sequencerAddress;
        ss << timestamp;
        return ss.GetHash();
    }
    
    /**
     * @brief Sign the confirmation with a private key
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key) {
        uint256 hash = GetHash();
        return key.Sign(hash, signature);
    }
    
    /**
     * @brief Verify the confirmation signature
     * @param pubkey The public key to verify against
     * @return true if signature is valid
     */
    bool VerifySignature(const CPubKey& pubkey) const {
        if (signature.empty()) {
            return false;
        }
        uint256 hash = GetHash();
        return pubkey.Verify(hash, signature);
    }
    
    /**
     * @brief Check if the confirmation is valid (basic structure check)
     * @return true if all required fields are set
     */
    bool IsValid() const {
        return !l1TxHash.IsNull() &&
               !l2Recipient.IsNull() &&
               amount > 0 &&
               !sequencerAddress.IsNull() &&
               timestamp > 0;
    }
    
    /**
     * @brief Check if the confirmation has expired
     * @param maxAgeSeconds Maximum age in seconds
     * @return true if confirmation is older than maxAgeSeconds
     */
    bool IsExpired(uint64_t maxAgeSeconds = MINT_CONSENSUS_TIMEOUT_SECONDS) const {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return (now > timestamp) && (now - timestamp > maxAgeSeconds);
    }
    
    /**
     * @brief Serialize the confirmation to bytes
     * @return Serialized byte vector
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }
    
    /**
     * @brief Deserialize confirmation from bytes
     * @param data The serialized data
     * @return true if deserialization succeeded
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    /** Serialization support */
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l1TxHash);
        READWRITE(l2Recipient);
        READWRITE(amount);
        READWRITE(sequencerAddress);
        READWRITE(signature);
        READWRITE(timestamp);
    }
    
    /** Equality operator */
    bool operator==(const MintConfirmation& other) const {
        return l1TxHash == other.l1TxHash &&
               l2Recipient == other.l2Recipient &&
               amount == other.amount &&
               sequencerAddress == other.sequencerAddress &&
               timestamp == other.timestamp;
    }
    
    bool operator!=(const MintConfirmation& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// MintConsensusState Structure
// ============================================================================

/**
 * @brief Consensus state for a burn transaction
 * 
 * Tracks all confirmations received for a specific burn transaction
 * and determines when consensus has been reached.
 * 
 * Requirements: 3.3, 3.4
 */
struct MintConsensusState {
    /** Status of the consensus process */
    enum class Status {
        PENDING,        // Waiting for confirmations
        REACHED,        // 2/3 consensus reached
        MINTED,         // Tokens have been minted
        FAILED,         // Consensus failed (timeout)
        REJECTED        // Explicitly rejected (invalid burn)
    };
    
    /** L1 burn transaction hash */
    uint256 l1TxHash;
    
    /** Parsed burn data from L1 transaction */
    BurnData burnData;
    
    /** Map of sequencer address -> confirmation */
    std::map<uint160, MintConfirmation> confirmations;
    
    /** Timestamp when first confirmation was received */
    uint64_t firstSeenTime;
    
    /** Current status */
    Status status;
    
    /** Default constructor */
    MintConsensusState()
        : firstSeenTime(0)
        , status(Status::PENDING) {}
    
    /** Constructor with burn data */
    MintConsensusState(const uint256& txHash, const BurnData& data)
        : l1TxHash(txHash)
        , burnData(data)
        , firstSeenTime(std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch()).count())
        , status(Status::PENDING) {}
    
    /**
     * @brief Get the confirmation ratio
     * @param totalSequencers Total number of active sequencers
     * @return Ratio of confirmations to total sequencers (0.0 - 1.0)
     */
    double GetConfirmationRatio(size_t totalSequencers) const {
        if (totalSequencers == 0) {
            return 0.0;
        }
        return static_cast<double>(confirmations.size()) / static_cast<double>(totalSequencers);
    }
    
    /**
     * @brief Check if consensus has been reached
     * @param totalSequencers Total number of active sequencers
     * @return true if 2/3 or more sequencers have confirmed
     * 
     * Requirements: 3.4
     */
    bool HasReachedConsensus(size_t totalSequencers) const {
        // Need at least MIN_SEQUENCERS_FOR_CONSENSUS
        if (totalSequencers < MIN_SEQUENCERS_FOR_CONSENSUS) {
            return false;
        }
        return GetConfirmationRatio(totalSequencers) >= MINT_CONSENSUS_THRESHOLD;
    }
    
    /**
     * @brief Check if the consensus has timed out
     * @return true if more than MINT_CONSENSUS_TIMEOUT_SECONDS have passed
     */
    bool HasTimedOut() const {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return (now > firstSeenTime) && 
               (now - firstSeenTime > MINT_CONSENSUS_TIMEOUT_SECONDS);
    }
    
    /**
     * @brief Get the number of confirmations
     * @return Count of unique sequencer confirmations
     */
    size_t GetConfirmationCount() const {
        return confirmations.size();
    }
    
    /**
     * @brief Check if a sequencer has already confirmed
     * @param sequencerAddress The sequencer address to check
     * @return true if sequencer has already submitted a confirmation
     */
    bool HasConfirmation(const uint160& sequencerAddress) const {
        return confirmations.find(sequencerAddress) != confirmations.end();
    }
    
    /**
     * @brief Add a confirmation
     * @param confirmation The confirmation to add
     * @return true if added, false if duplicate
     */
    bool AddConfirmation(const MintConfirmation& confirmation) {
        if (HasConfirmation(confirmation.sequencerAddress)) {
            return false;  // Duplicate
        }
        confirmations[confirmation.sequencerAddress] = confirmation;
        return true;
    }
    
    /**
     * @brief Get status as string
     * @return Human-readable status string
     */
    std::string GetStatusString() const {
        switch (status) {
            case Status::PENDING: return "PENDING";
            case Status::REACHED: return "REACHED";
            case Status::MINTED: return "MINTED";
            case Status::FAILED: return "FAILED";
            case Status::REJECTED: return "REJECTED";
            default: return "UNKNOWN";
        }
    }
    
    /** Serialization support */
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l1TxHash);
        READWRITE(burnData);
        READWRITE(confirmations);
        READWRITE(firstSeenTime);
        uint8_t statusVal = static_cast<uint8_t>(status);
        READWRITE(statusVal);
        if (ser_action.ForRead()) {
            status = static_cast<Status>(statusVal);
        }
    }
};

/** Stream output operator for MintConsensusState::Status */
inline std::ostream& operator<<(std::ostream& os, MintConsensusState::Status status) {
    switch (status) {
        case MintConsensusState::Status::PENDING: return os << "PENDING";
        case MintConsensusState::Status::REACHED: return os << "REACHED";
        case MintConsensusState::Status::MINTED: return os << "MINTED";
        case MintConsensusState::Status::FAILED: return os << "FAILED";
        case MintConsensusState::Status::REJECTED: return os << "REJECTED";
        default: return os << "UNKNOWN";
    }
}

// ============================================================================
// MintConsensusManager Class
// ============================================================================

/**
 * @brief Manager for mint consensus process
 * 
 * Coordinates the consensus process for minting L2 tokens after burns.
 * Collects confirmations from sequencers and triggers minting when
 * 2/3 consensus is reached.
 * 
 * Requirements: 3.1, 3.3, 3.4, 3.5, 3.6
 */
class MintConsensusManager {
public:
    /** Callback type for consensus reached notifications */
    using ConsensusReachedCallback = std::function<void(const MintConsensusState&)>;
    
    /** Callback type for consensus failed notifications */
    using ConsensusFailedCallback = std::function<void(const uint256&, const std::string&)>;
    
    /** Callback type for getting active sequencer count */
    using SequencerCountGetter = std::function<size_t()>;
    
    /** Callback type for verifying sequencer eligibility */
    using SequencerVerifier = std::function<bool(const uint160&)>;
    
    /** Callback type for getting sequencer public key */
    using SequencerPubKeyGetter = std::function<std::optional<CPubKey>(const uint160&)>;

    /**
     * @brief Construct a MintConsensusManager
     * @param chainId The L2 chain ID
     */
    explicit MintConsensusManager(uint32_t chainId);
    
    /**
     * @brief Destructor
     */
    ~MintConsensusManager();
    
    /**
     * @brief Submit a confirmation from the local sequencer
     * @param confirmation The confirmation to submit
     * @return true if confirmation was accepted
     * 
     * This is called when the local sequencer detects a valid burn
     * and wants to confirm it. The confirmation is stored locally
     * and broadcast to other sequencers.
     * 
     * Requirements: 3.1
     */
    bool SubmitConfirmation(const MintConfirmation& confirmation);
    
    /**
     * @brief Process a confirmation received from the network
     * @param confirmation The confirmation to process
     * @param pfrom The peer that sent the confirmation (may be nullptr)
     * @return true if confirmation was accepted
     * 
     * Validates the confirmation and adds it to the consensus state.
     * Checks for duplicates and verifies the sequencer is eligible.
     * 
     * Requirements: 3.3, 3.6
     */
    bool ProcessConfirmation(const MintConfirmation& confirmation, CNode* pfrom = nullptr);
    
    /**
     * @brief Check if consensus has been reached for a burn
     * @param l1TxHash The L1 burn transaction hash
     * @return true if 2/3 consensus has been reached
     * 
     * Requirements: 3.4
     */
    bool HasConsensus(const uint256& l1TxHash) const;
    
    /**
     * @brief Get the consensus state for a burn
     * @param l1TxHash The L1 burn transaction hash
     * @return The consensus state if found, nullopt otherwise
     */
    std::optional<MintConsensusState> GetConsensusState(const uint256& l1TxHash) const;
    
    /**
     * @brief Get all pending burns (waiting for consensus)
     * @return Vector of pending consensus states
     * 
     * Requirements: 3.5
     */
    std::vector<MintConsensusState> GetPendingBurns() const;
    
    /**
     * @brief Process timeouts for pending burns
     * 
     * Checks all pending burns and marks those that have exceeded
     * the 10-minute timeout as FAILED.
     * 
     * Requirements: 3.5
     */
    void ProcessTimeouts();
    
    /**
     * @brief Get the chain ID
     * @return The L2 chain ID
     */
    uint32_t GetChainId() const { return chainId_; }
    
    /**
     * @brief Set the sequencer count getter callback
     * @param getter The callback function
     */
    void SetSequencerCountGetter(SequencerCountGetter getter) {
        sequencerCountGetter_ = std::move(getter);
    }
    
    /**
     * @brief Set the sequencer verifier callback
     * @param verifier The callback function
     */
    void SetSequencerVerifier(SequencerVerifier verifier) {
        sequencerVerifier_ = std::move(verifier);
    }
    
    /**
     * @brief Set the sequencer public key getter callback
     * @param getter The callback function
     */
    void SetSequencerPubKeyGetter(SequencerPubKeyGetter getter) {
        sequencerPubKeyGetter_ = std::move(getter);
    }
    
    /**
     * @brief Register callback for consensus reached
     * @param callback Function to call when consensus is reached
     */
    void RegisterConsensusReachedCallback(ConsensusReachedCallback callback);
    
    /**
     * @brief Register callback for consensus failed
     * @param callback Function to call when consensus fails
     */
    void RegisterConsensusFailedCallback(ConsensusFailedCallback callback);
    
    /**
     * @brief Mark a burn as minted (called after successful minting)
     * @param l1TxHash The L1 burn transaction hash
     * @return true if state was updated
     */
    bool MarkAsMinted(const uint256& l1TxHash);
    
    /**
     * @brief Get the number of pending burns
     * @return Count of pending burns
     */
    size_t GetPendingCount() const;
    
    /**
     * @brief Get the number of burns that reached consensus
     * @return Count of burns with consensus
     */
    size_t GetConsensusReachedCount() const;
    
    /**
     * @brief Clear all state (for testing)
     */
    void Clear();
    
    /**
     * @brief Set the active sequencer count for testing
     * @param count The number of active sequencers
     */
    void SetTestSequencerCount(size_t count);
    
    /**
     * @brief Add a test sequencer (for testing without SequencerDiscovery)
     * @param address The sequencer address
     * @param pubkey The sequencer's public key
     */
    void AddTestSequencer(const uint160& address, const CPubKey& pubkey);
    
    /**
     * @brief Clear test sequencers
     */
    void ClearTestSequencers();

private:
    /** L2 chain ID */
    uint32_t chainId_;
    
    /** Map of L1 TX hash -> consensus state */
    std::map<uint256, MintConsensusState> consensusStates_;
    
    /** Callback to get active sequencer count */
    SequencerCountGetter sequencerCountGetter_;
    
    /** Callback to verify sequencer eligibility */
    SequencerVerifier sequencerVerifier_;
    
    /** Callback to get sequencer public key */
    SequencerPubKeyGetter sequencerPubKeyGetter_;
    
    /** Consensus reached callbacks */
    std::vector<ConsensusReachedCallback> consensusReachedCallbacks_;
    
    /** Consensus failed callbacks */
    std::vector<ConsensusFailedCallback> consensusFailedCallbacks_;
    
    /** Test sequencer count (for testing) */
    std::optional<size_t> testSequencerCount_;
    
    /** Test sequencers (address -> pubkey) for testing */
    std::map<uint160, CPubKey> testSequencers_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_consensus_;
    
    /** Maximum consensus states to track */
    static constexpr size_t MAX_CONSENSUS_STATES = 10000;
    
    /**
     * @brief Get the number of active sequencers
     * @return Number of active sequencers
     */
    size_t GetActiveSequencerCount() const;
    
    /**
     * @brief Verify that an address is an eligible sequencer
     * @param address The address to verify
     * @return true if address is an eligible sequencer
     */
    bool IsEligibleSequencer(const uint160& address) const;
    
    /**
     * @brief Get the public key for a sequencer
     * @param address The sequencer address
     * @return The public key if found
     */
    std::optional<CPubKey> GetSequencerPubKey(const uint160& address) const;
    
    /**
     * @brief Broadcast a confirmation to other sequencers
     * @param confirmation The confirmation to broadcast
     */
    void BroadcastConfirmation(const MintConfirmation& confirmation);
    
    /**
     * @brief Notify callbacks that consensus was reached
     * @param state The consensus state
     */
    void NotifyConsensusReached(const MintConsensusState& state);
    
    /**
     * @brief Notify callbacks that consensus failed
     * @param l1TxHash The burn transaction hash
     * @param reason Reason for failure
     */
    void NotifyConsensusFailed(const uint256& l1TxHash, const std::string& reason);
    
    /**
     * @brief Check and update consensus status for a burn
     * @param l1TxHash The burn transaction hash
     */
    void CheckConsensusStatus(const uint256& l1TxHash);
    
    /**
     * @brief Prune old consensus states
     */
    void PruneOldStates();
};

/**
 * @brief Global mint consensus manager instance
 */
MintConsensusManager& GetMintConsensusManager();

/**
 * @brief Initialize the global mint consensus manager
 * @param chainId The L2 chain ID
 */
void InitMintConsensusManager(uint32_t chainId);

/**
 * @brief Check if mint consensus manager is initialized
 * @return true if initialized
 */
bool IsMintConsensusManagerInitialized();

} // namespace l2

#endif // CASCOIN_L2_MINT_CONSENSUS_H
