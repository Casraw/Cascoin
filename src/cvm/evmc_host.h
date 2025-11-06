// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_EVMC_HOST_H
#define CASCOIN_CVM_EVMC_HOST_H

#ifdef ENABLE_EVMC

#include <evmc/evmc.h>
#include <evmc/utils.h>
#include <uint256.h>
#include <cvm/vmstate.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <map>
#include <memory>

namespace CVM {

/**
 * EVMC Host Interface Implementation for Cascoin
 * 
 * This class implements the EVMC host interface to provide EVM execution
 * capabilities within the Cascoin Virtual Machine. It bridges between
 * the EVM execution engine (evmone) and Cascoin's storage, trust system,
 * and blockchain context.
 */
class EVMCHost {
public:
    EVMCHost(CVMDatabase* db, const TrustContext& trust_ctx);
    ~EVMCHost();

    // Main execution interface
    evmc_result Execute(const evmc_message& msg, const uint8_t* code, size_t code_size);
    
    // Host interface implementation
    static const evmc_host_interface* GetInterface();
    
    // Context management
    void SetBlockContext(int64_t timestamp, int64_t number, const uint256& hash, 
                        const uint256& difficulty, int64_t gas_limit);
    void SetTxContext(const uint256& tx_hash, const uint160& tx_origin, int64_t gas_price);
    
    // Trust context integration
    void SetTrustContext(const TrustContext& ctx) { trust_context = ctx; }
    const TrustContext& GetTrustContext() const { return trust_context; }
    
    // Storage and state management
    void SetStorage(const evmc_address& addr, const evmc_bytes32& key, const evmc_bytes32& value);
    evmc_bytes32 GetStorage(const evmc_address& addr, const evmc_bytes32& key);
    
    // Account management
    evmc_uint256be GetBalance(const evmc_address& addr);
    size_t GetCodeSize(const evmc_address& addr);
    evmc_bytes32 GetCodeHash(const evmc_address& addr);
    size_t CopyCode(const evmc_address& addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size);
    
    // Contract lifecycle
    bool AccountExists(const evmc_address& addr);
    evmc_access_status AccessAccount(const evmc_address& addr);
    evmc_access_status AccessStorage(const evmc_address& addr, const evmc_bytes32& key);
    
    // Transaction context
    evmc_bytes32 GetTxHash();
    evmc_uint256be GetTxGasPrice();
    evmc_address GetTxOrigin();
    
    // Block context
    evmc_bytes32 GetBlockHash(int64_t number);
    int64_t GetBlockTimestamp();
    int64_t GetBlockNumber();
    int64_t GetBlockGasLimit();
    evmc_uint256be GetBlockDifficulty();
    evmc_uint256be GetChainId();
    
    // Logging and events
    void EmitLog(const evmc_address& addr, const uint8_t* data, size_t data_size,
                const evmc_bytes32 topics[], size_t topics_count);
    
    // Contract creation and calls
    evmc_result Call(const evmc_message& msg);
    
    // Self-destruct
    void Selfdestruct(const evmc_address& addr, const evmc_address& beneficiary);
    
    // Helper functions
    uint160 EvmcAddressToUint160(const evmc_address& addr);
    evmc_address Uint160ToEvmcAddress(const uint160& addr);
    uint256 EvmcBytes32ToUint256(const evmc_bytes32& bytes);
    evmc_bytes32 Uint256ToEvmcBytes32(const uint256& value);
    evmc_uint256be Uint256ToEvmcUint256be(const uint256& value);
    uint256 EvmcUint256beToUint256(const evmc_uint256be& value);

private:
    // EVMC callback implementations
    static bool account_exists_fn(evmc_host_context* context, const evmc_address* address);
    static evmc_bytes32 get_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key);
    static enum evmc_storage_status set_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key, const evmc_bytes32* value);
    static evmc_uint256be get_balance_fn(evmc_host_context* context, const evmc_address* address);
    static size_t get_code_size_fn(evmc_host_context* context, const evmc_address* address);
    static evmc_bytes32 get_code_hash_fn(evmc_host_context* context, const evmc_address* address);
    static size_t copy_code_fn(evmc_host_context* context, const evmc_address* address, size_t code_offset, uint8_t* buffer_data, size_t buffer_size);
    static void selfdestruct_fn(evmc_host_context* context, const evmc_address* address, const evmc_address* beneficiary);
    static struct evmc_result call_fn(evmc_host_context* context, const evmc_message* msg);
    static struct evmc_tx_context get_tx_context_fn(evmc_host_context* context);
    static evmc_bytes32 get_block_hash_fn(evmc_host_context* context, int64_t number);
    static void emit_log_fn(evmc_host_context* context, const evmc_address* address, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count);
    static enum evmc_access_status access_account_fn(evmc_host_context* context, const evmc_address* address);
    static enum evmc_access_status access_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key);
    
    // Trust-aware operations
    uint64_t ApplyTrustBasedGasAdjustment(uint64_t base_gas, const evmc_address& caller);
    bool CheckTrustGatedOperation(const evmc_address& caller, const std::string& operation);
    void InjectTrustContext(evmc_message& msg);
    
    // State management
    CVMDatabase* database;
    TrustContext trust_context;
    
    // Block context
    struct BlockContext {
        int64_t timestamp;
        int64_t number;
        uint256 hash;
        uint256 difficulty;
        int64_t gas_limit;
        uint256 chain_id;
    } block_context;
    
    // Transaction context
    struct TxContext {
        uint256 hash;
        uint160 origin;
        int64_t gas_price;
    } tx_context;
    
    // Access tracking for EIP-2929
    std::map<evmc_address, bool> accessed_accounts;
    std::map<std::pair<evmc_address, evmc_bytes32>, bool> accessed_storage;
    
    // Event logs
    std::vector<VMState::LogEntry> logs;
    
    // Self-destruct tracking
    std::set<evmc_address> selfdestructed_accounts;
    
    // EVMC VM instance
    evmc_vm* vm_instance;
    
    // Static host interface
    static const evmc_host_interface host_interface;
};

/**
 * EVMC Host Context Wrapper
 * 
 * Wraps EVMCHost instance for use with EVMC callbacks
 */
struct EVMCHostContext {
    EVMCHost* host;
    
    explicit EVMCHostContext(EVMCHost* h) : host(h) {}
};

/**
 * Utility functions for EVMC integration
 */
namespace EVMCUtils {
    // Convert between Cascoin and EVMC types
    evmc_address AddressFromHex(const std::string& hex);
    std::string AddressToHex(const evmc_address& addr);
    evmc_bytes32 HashFromHex(const std::string& hex);
    std::string HashToHex(const evmc_bytes32& hash);
    
    // Gas calculation helpers
    uint64_t CalculateIntrinsicGas(const uint8_t* data, size_t data_size, bool is_creation);
    uint64_t CalculateMemoryGas(size_t memory_size);
    
    // Trust-aware gas adjustments
    uint64_t ApplyReputationGasDiscount(uint64_t base_gas, uint32_t reputation_score);
    bool IsHighReputationAddress(const evmc_address& addr, const TrustContext& ctx);
}

} // namespace CVM

#endif // ENABLE_EVMC

#endif // CASCOIN_CVM_EVMC_HOST_H