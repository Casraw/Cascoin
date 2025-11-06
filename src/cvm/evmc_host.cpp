// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef ENABLE_EVMC

#include <cvm/evmc_host.h>
#include <evmone/evmone.h>
#include <util.h>
#include <hash.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <cstring>
#include <algorithm>

namespace CVM {

// Static host interface definition
const evmc_host_interface EVMCHost::host_interface = {
    EVMCHost::account_exists_fn,
    EVMCHost::get_storage_fn,
    EVMCHost::set_storage_fn,
    EVMCHost::get_balance_fn,
    EVMCHost::get_code_size_fn,
    EVMCHost::get_code_hash_fn,
    EVMCHost::copy_code_fn,
    EVMCHost::selfdestruct_fn,
    EVMCHost::call_fn,
    EVMCHost::get_tx_context_fn,
    EVMCHost::get_block_hash_fn,
    EVMCHost::emit_log_fn,
    EVMCHost::access_account_fn,
    EVMCHost::access_storage_fn
};

EVMCHost::EVMCHost(CVMDatabase* db, const TrustContext& trust_ctx)
    : database(db), trust_context(trust_ctx), vm_instance(nullptr)
{
    // Initialize EVMC VM (evmone)
    vm_instance = evmc_create_evmone();
    if (!vm_instance) {
        throw std::runtime_error("Failed to create EVMC VM instance");
    }
    
    // Initialize block context with default values
    block_context.timestamp = 0;
    block_context.number = 0;
    block_context.hash = uint256();
    block_context.difficulty = uint256();
    block_context.gas_limit = 10000000; // 10M gas limit
    block_context.chain_id = uint256(Params().GetConsensus().nChainId);
    
    // Initialize transaction context
    tx_context.hash = uint256();
    tx_context.origin = uint160();
    tx_context.gas_price = 0;
}

EVMCHost::~EVMCHost() {
    if (vm_instance) {
        vm_instance->destroy(vm_instance);
    }
}

const evmc_host_interface* EVMCHost::GetInterface() {
    return &host_interface;
}

evmc_result EVMCHost::Execute(const evmc_message& msg, const uint8_t* code, size_t code_size) {
    if (!vm_instance) {
        evmc_result result = {};
        result.status_code = EVMC_INTERNAL_ERROR;
        return result;
    }
    
    // Create host context
    EVMCHostContext host_ctx(this);
    
    // Apply trust-based gas adjustments
    evmc_message adjusted_msg = msg;
    adjusted_msg.gas = ApplyTrustBasedGasAdjustment(msg.gas, msg.sender);
    
    // Inject trust context into message
    InjectTrustContext(adjusted_msg);
    
    // Execute via EVMC
    return vm_instance->execute(vm_instance, &host_interface, &host_ctx, EVMC_LONDON, &adjusted_msg, code, code_size);
}

void EVMCHost::SetBlockContext(int64_t timestamp, int64_t number, const uint256& hash, 
                              const uint256& difficulty, int64_t gas_limit) {
    block_context.timestamp = timestamp;
    block_context.number = number;
    block_context.hash = hash;
    block_context.difficulty = difficulty;
    block_context.gas_limit = gas_limit;
}

void EVMCHost::SetTxContext(const uint256& tx_hash, const uint160& tx_origin, int64_t gas_price) {
    tx_context.hash = tx_hash;
    tx_context.origin = tx_origin;
    tx_context.gas_price = gas_price;
}

// EVMC Callback Implementations

bool EVMCHost::account_exists_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->AccountExists(*address);
}

evmc_bytes32 EVMCHost::get_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->GetStorage(*address, *key);
}

enum evmc_storage_status EVMCHost::set_storage_fn(evmc_host_context* context, const evmc_address* address, 
                                                 const evmc_bytes32* key, const evmc_bytes32* value) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    
    // Get current value for gas calculation
    evmc_bytes32 current = ctx->host->GetStorage(*address, *key);
    
    // Set new value
    ctx->host->SetStorage(*address, *key, *value);
    
    // Determine storage status for gas calculation
    bool is_zero_current = std::all_of(current.bytes, current.bytes + 32, [](uint8_t b) { return b == 0; });
    bool is_zero_new = std::all_of(value->bytes, value->bytes + 32, [](uint8_t b) { return b == 0; });
    
    if (is_zero_current && !is_zero_new) {
        return EVMC_STORAGE_ADDED;
    } else if (!is_zero_current && is_zero_new) {
        return EVMC_STORAGE_DELETED;
    } else if (memcmp(current.bytes, value->bytes, 32) != 0) {
        return EVMC_STORAGE_MODIFIED;
    } else {
        return EVMC_STORAGE_UNCHANGED;
    }
}

evmc_uint256be EVMCHost::get_balance_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->GetBalance(*address);
}

size_t EVMCHost::get_code_size_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->GetCodeSize(*address);
}

evmc_bytes32 EVMCHost::get_code_hash_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->GetCodeHash(*address);
}

size_t EVMCHost::copy_code_fn(evmc_host_context* context, const evmc_address* address, 
                             size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->CopyCode(*address, code_offset, buffer_data, buffer_size);
}

void EVMCHost::selfdestruct_fn(evmc_host_context* context, const evmc_address* address, const evmc_address* beneficiary) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    ctx->host->Selfdestruct(*address, *beneficiary);
}

struct evmc_result EVMCHost::call_fn(evmc_host_context* context, const evmc_message* msg) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->Call(*msg);
}

struct evmc_tx_context EVMCHost::get_tx_context_fn(evmc_host_context* context) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    
    evmc_tx_context tx_ctx = {};
    tx_ctx.tx_gas_price = ctx->host->GetTxGasPrice();
    tx_ctx.tx_origin = ctx->host->GetTxOrigin();
    tx_ctx.block_coinbase = {}; // Not used in Cascoin
    tx_ctx.block_number = ctx->host->GetBlockNumber();
    tx_ctx.block_timestamp = ctx->host->GetBlockTimestamp();
    tx_ctx.block_gas_limit = ctx->host->GetBlockGasLimit();
    tx_ctx.block_difficulty = ctx->host->GetBlockDifficulty();
    tx_ctx.chain_id = ctx->host->GetChainId();
    tx_ctx.block_base_fee = {}; // EIP-1559 base fee (not implemented yet)
    
    return tx_ctx;
}

evmc_bytes32 EVMCHost::get_block_hash_fn(evmc_host_context* context, int64_t number) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->GetBlockHash(number);
}

void EVMCHost::emit_log_fn(evmc_host_context* context, const evmc_address* address, 
                          const uint8_t* data, size_t data_size, 
                          const evmc_bytes32 topics[], size_t topics_count) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    ctx->host->EmitLog(*address, data, data_size, topics, topics_count);
}

enum evmc_access_status EVMCHost::access_account_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->AccessAccount(*address);
}

enum evmc_access_status EVMCHost::access_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key) {
    EVMCHostContext* ctx = static_cast<EVMCHostContext*>(context);
    return ctx->host->AccessStorage(*address, *key);
}

// Host Implementation Methods

bool EVMCHost::AccountExists(const evmc_address& addr) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    return database->Exists(cascoin_addr);
}

evmc_bytes32 EVMCHost::GetStorage(const evmc_address& addr, const evmc_bytes32& key) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    uint256 cascoin_key = EvmcBytes32ToUint256(key);
    uint256 value;
    
    if (database->Load(cascoin_addr, cascoin_key, value)) {
        return Uint256ToEvmcBytes32(value);
    }
    
    // Return zero if not found
    evmc_bytes32 zero = {};
    return zero;
}

void EVMCHost::SetStorage(const evmc_address& addr, const evmc_bytes32& key, const evmc_bytes32& value) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    uint256 cascoin_key = EvmcBytes32ToUint256(key);
    uint256 cascoin_value = EvmcBytes32ToUint256(value);
    
    database->Store(cascoin_addr, cascoin_key, cascoin_value);
}

evmc_uint256be EVMCHost::GetBalance(const evmc_address& addr) {
    // TODO: Implement balance lookup from UTXO set or account state
    // For now, return zero balance
    evmc_uint256be balance = {};
    return balance;
}

size_t EVMCHost::GetCodeSize(const evmc_address& addr) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    std::vector<uint8_t> code;
    
    if (database->LoadContract(cascoin_addr, code)) {
        return code.size();
    }
    
    return 0;
}

evmc_bytes32 EVMCHost::GetCodeHash(const evmc_address& addr) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    std::vector<uint8_t> code;
    
    if (database->LoadContract(cascoin_addr, code)) {
        uint256 hash = Hash(code.begin(), code.end());
        return Uint256ToEvmcBytes32(hash);
    }
    
    // Return empty hash for non-existent contracts
    evmc_bytes32 empty_hash = {};
    return empty_hash;
}

size_t EVMCHost::CopyCode(const evmc_address& addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    std::vector<uint8_t> code;
    
    if (!database->LoadContract(cascoin_addr, code)) {
        return 0;
    }
    
    if (code_offset >= code.size()) {
        return 0;
    }
    
    size_t copy_size = std::min(buffer_size, code.size() - code_offset);
    std::memcpy(buffer_data, code.data() + code_offset, copy_size);
    
    return copy_size;
}

evmc_access_status EVMCHost::AccessAccount(const evmc_address& addr) {
    auto it = accessed_accounts.find(addr);
    if (it != accessed_accounts.end()) {
        return EVMC_ACCESS_WARM;
    }
    
    accessed_accounts[addr] = true;
    return EVMC_ACCESS_COLD;
}

evmc_access_status EVMCHost::AccessStorage(const evmc_address& addr, const evmc_bytes32& key) {
    auto storage_key = std::make_pair(addr, key);
    auto it = accessed_storage.find(storage_key);
    if (it != accessed_storage.end()) {
        return EVMC_ACCESS_WARM;
    }
    
    accessed_storage[storage_key] = true;
    return EVMC_ACCESS_COLD;
}

evmc_bytes32 EVMCHost::GetTxHash() {
    return Uint256ToEvmcBytes32(tx_context.hash);
}

evmc_uint256be EVMCHost::GetTxGasPrice() {
    return Uint256ToEvmcUint256be(uint256(tx_context.gas_price));
}

evmc_address EVMCHost::GetTxOrigin() {
    return Uint160ToEvmcAddress(tx_context.origin);
}

evmc_bytes32 EVMCHost::GetBlockHash(int64_t number) {
    // TODO: Implement block hash lookup
    // For now, return current block hash if number matches
    if (number == block_context.number) {
        return Uint256ToEvmcBytes32(block_context.hash);
    }
    
    evmc_bytes32 zero = {};
    return zero;
}

int64_t EVMCHost::GetBlockTimestamp() {
    return block_context.timestamp;
}

int64_t EVMCHost::GetBlockNumber() {
    return block_context.number;
}

int64_t EVMCHost::GetBlockGasLimit() {
    return block_context.gas_limit;
}

evmc_uint256be EVMCHost::GetBlockDifficulty() {
    return Uint256ToEvmcUint256be(block_context.difficulty);
}

evmc_uint256be EVMCHost::GetChainId() {
    return Uint256ToEvmcUint256be(block_context.chain_id);
}

void EVMCHost::EmitLog(const evmc_address& addr, const uint8_t* data, size_t data_size,
                      const evmc_bytes32 topics[], size_t topics_count) {
    VMState::LogEntry log;
    log.contractAddress = EvmcAddressToUint160(addr);
    
    // Copy topics
    for (size_t i = 0; i < topics_count; ++i) {
        log.topics.push_back(EvmcBytes32ToUint256(topics[i]));
    }
    
    // Copy data
    log.data.assign(data, data + data_size);
    
    logs.push_back(log);
}

evmc_result EVMCHost::Call(const evmc_message& msg) {
    // TODO: Implement contract calls
    // This would recursively call Execute for the target contract
    evmc_result result = {};
    result.status_code = EVMC_REVERT;
    return result;
}

void EVMCHost::Selfdestruct(const evmc_address& addr, const evmc_address& beneficiary) {
    selfdestructed_accounts.insert(addr);
    // TODO: Transfer balance to beneficiary
}

// Helper Functions

uint160 EVMCHost::EvmcAddressToUint160(const evmc_address& addr) {
    uint160 result;
    std::memcpy(result.begin(), addr.bytes, 20);
    return result;
}

evmc_address EVMCHost::Uint160ToEvmcAddress(const uint160& addr) {
    evmc_address result = {};
    std::memcpy(result.bytes, addr.begin(), 20);
    return result;
}

uint256 EVMCHost::EvmcBytes32ToUint256(const evmc_bytes32& bytes) {
    uint256 result;
    std::memcpy(result.begin(), bytes.bytes, 32);
    return result;
}

evmc_bytes32 EVMCHost::Uint256ToEvmcBytes32(const uint256& value) {
    evmc_bytes32 result = {};
    std::memcpy(result.bytes, value.begin(), 32);
    return result;
}

evmc_uint256be EVMCHost::Uint256ToEvmcUint256be(const uint256& value) {
    evmc_uint256be result = {};
    // Convert to big-endian
    for (int i = 0; i < 32; ++i) {
        result.bytes[i] = value.begin()[31 - i];
    }
    return result;
}

uint256 EVMCHost::EvmcUint256beToUint256(const evmc_uint256be& value) {
    uint256 result;
    // Convert from big-endian
    for (int i = 0; i < 32; ++i) {
        result.begin()[i] = value.bytes[31 - i];
    }
    return result;
}

// Trust-aware operations

uint64_t EVMCHost::ApplyTrustBasedGasAdjustment(uint64_t base_gas, const evmc_address& caller) {
    uint160 cascoin_addr = EvmcAddressToUint160(caller);
    uint32_t reputation = trust_context.GetReputation(cascoin_addr);
    
    // Apply reputation-based gas discount
    return EVMCUtils::ApplyReputationGasDiscount(base_gas, reputation);
}

bool EVMCHost::CheckTrustGatedOperation(const evmc_address& caller, const std::string& operation) {
    uint160 cascoin_addr = EvmcAddressToUint160(caller);
    return trust_context.CheckTrustGate(cascoin_addr, operation);
}

void EVMCHost::InjectTrustContext(evmc_message& msg) {
    // Trust context is automatically available through the host interface
    // The trust system can be queried during execution via custom opcodes or precompiles
}

// Utility Functions

namespace EVMCUtils {

evmc_address AddressFromHex(const std::string& hex) {
    evmc_address addr = {};
    // TODO: Implement hex string to address conversion
    return addr;
}

std::string AddressToHex(const evmc_address& addr) {
    // TODO: Implement address to hex string conversion
    return "";
}

evmc_bytes32 HashFromHex(const std::string& hex) {
    evmc_bytes32 hash = {};
    // TODO: Implement hex string to hash conversion
    return hash;
}

std::string HashToHex(const evmc_bytes32& hash) {
    // TODO: Implement hash to hex string conversion
    return "";
}

uint64_t CalculateIntrinsicGas(const uint8_t* data, size_t data_size, bool is_creation) {
    uint64_t gas = 21000; // Base transaction cost
    
    if (is_creation) {
        gas += 32000; // Contract creation cost
    }
    
    // Data cost
    for (size_t i = 0; i < data_size; ++i) {
        if (data[i] == 0) {
            gas += 4; // Zero byte cost
        } else {
            gas += 16; // Non-zero byte cost
        }
    }
    
    return gas;
}

uint64_t CalculateMemoryGas(size_t memory_size) {
    uint64_t memory_words = (memory_size + 31) / 32;
    return 3 * memory_words + (memory_words * memory_words) / 512;
}

uint64_t ApplyReputationGasDiscount(uint64_t base_gas, uint32_t reputation_score) {
    if (reputation_score >= 80) {
        // High reputation: 50% discount
        return base_gas / 2;
    } else if (reputation_score >= 60) {
        // Medium reputation: 25% discount
        return (base_gas * 3) / 4;
    } else if (reputation_score >= 40) {
        // Low reputation: 10% discount
        return (base_gas * 9) / 10;
    }
    
    // Very low reputation: no discount
    return base_gas;
}

bool IsHighReputationAddress(const evmc_address& addr, const TrustContext& ctx) {
    // TODO: Implement reputation lookup
    return false;
}

} // namespace EVMCUtils

} // namespace CVM

#endif // ENABLE_EVMC