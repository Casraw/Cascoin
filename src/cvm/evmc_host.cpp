// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <config/bitcoin-config.h>

#ifdef ENABLE_EVMC

#include <cvm/evmc_host.h>
#include <cvm/contract.h>
#include <evmone/evmone.h>
#include <util.h>
#include <hash.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <coins.h>
#include <chain.h>
#include <validation.h>
#include <script/standard.h>
#include <key.h>
#include <pubkey.h>
#include <cstring>
#include <algorithm>

// Maximum contract code size (24KB)
static constexpr size_t MAX_CODE_SIZE = 24576;

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
    EVMCHost::access_storage_fn,
    nullptr, // get_transient_storage (not implemented yet)
    nullptr  // set_transient_storage (not implemented yet)
};

EVMCHost::EVMCHost(CVMDatabase* db, const TrustContext& trust_ctx,
                   CCoinsViewCache* coins_view, CBlockIndex* block_index)
    : database(db), trust_context(trust_ctx), coins_view(coins_view), 
      block_index(block_index), vm_instance(nullptr)
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
    // Chain ID for Cascoin (use a default value, can be configured)
    block_context.chain_id = uint256(); // Initialize to zero
    block_context.chain_id.begin()[0] = 1; // Set chain ID to 1
    
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
    return vm_instance->execute(vm_instance, &host_interface, reinterpret_cast<evmc_host_context*>(&host_ctx), EVMC_LONDON, &adjusted_msg, code, code_size);
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
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->AccountExists(*address);
}

evmc_bytes32 EVMCHost::get_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->GetStorage(*address, *key);
}

enum evmc_storage_status EVMCHost::set_storage_fn(evmc_host_context* context, const evmc_address* address, 
                                                 const evmc_bytes32* key, const evmc_bytes32* value) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    
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
        return EVMC_STORAGE_ASSIGNED; // Value unchanged but assigned
    }
}

evmc_uint256be EVMCHost::get_balance_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->GetBalance(*address);
}

size_t EVMCHost::get_code_size_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->GetCodeSize(*address);
}

evmc_bytes32 EVMCHost::get_code_hash_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->GetCodeHash(*address);
}

size_t EVMCHost::copy_code_fn(evmc_host_context* context, const evmc_address* address, 
                             size_t code_offset, uint8_t* buffer_data, size_t buffer_size) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->CopyCode(*address, code_offset, buffer_data, buffer_size);
}

bool EVMCHost::selfdestruct_fn(evmc_host_context* context, const evmc_address* address, const evmc_address* beneficiary) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    ctx->host->Selfdestruct(*address, *beneficiary);
    return true;
}

struct evmc_result EVMCHost::call_fn(evmc_host_context* context, const evmc_message* msg) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->Call(*msg);
}

struct evmc_tx_context EVMCHost::get_tx_context_fn(evmc_host_context* context) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    
    evmc_tx_context tx_ctx = {};
    tx_ctx.tx_gas_price = ctx->host->GetTxGasPrice();
    tx_ctx.tx_origin = ctx->host->GetTxOrigin();
    tx_ctx.block_coinbase = {}; // Not used in Cascoin
    tx_ctx.block_number = ctx->host->GetBlockNumber();
    tx_ctx.block_timestamp = ctx->host->GetBlockTimestamp();
    tx_ctx.block_gas_limit = ctx->host->GetBlockGasLimit();
    // block_difficulty removed in newer EVMC versions (replaced by prevrandao)
    tx_ctx.chain_id = ctx->host->GetChainId();
    tx_ctx.block_base_fee = {}; // EIP-1559 base fee (not implemented yet)
    
    return tx_ctx;
}

evmc_bytes32 EVMCHost::get_block_hash_fn(evmc_host_context* context, int64_t number) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->GetBlockHash(number);
}

void EVMCHost::emit_log_fn(evmc_host_context* context, const evmc_address* address, 
                          const uint8_t* data, size_t data_size, 
                          const evmc_bytes32 topics[], size_t topics_count) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    ctx->host->EmitLog(*address, data, data_size, topics, topics_count);
}

enum evmc_access_status EVMCHost::access_account_fn(evmc_host_context* context, const evmc_address* address) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
    return ctx->host->AccessAccount(*address);
}

enum evmc_access_status EVMCHost::access_storage_fn(evmc_host_context* context, const evmc_address* address, const evmc_bytes32* key) {
    EVMCHostContext* ctx = reinterpret_cast<EVMCHostContext*>(context);
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
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    
    // Try to get balance from contract database first (for contract accounts)
    uint64_t contract_balance = 0;
    if (database && database->ReadBalance(cascoin_addr, contract_balance)) {
        LogPrint(BCLog::CVM, "EVMCHost: Balance lookup for contract %s: %d\n",
                 cascoin_addr.ToString(), contract_balance);
        uint256 balance_uint256;
        memset(balance_uint256.begin(), 0, 32);
        memcpy(balance_uint256.begin(), &contract_balance, sizeof(contract_balance));
        return Uint256ToEvmcUint256be(balance_uint256);
    }
    
    // For non-contract addresses, query the UTXO set
    if (coins_view) {
        CAmount total_balance = 0;
        
        // Convert uint160 address to CKeyID for UTXO lookup
        CKeyID key_id;
        std::memcpy(key_id.begin(), cascoin_addr.begin(), 20);
        
        // Create a P2PKH script for this address
        CScript script_pubkey = GetScriptForDestination(key_id);
        
        // Iterate through potential UTXOs for this address
        // Note: This is a simplified approach. In production, you'd want to maintain
        // an address index or use a more efficient lookup method
        // For now, we'll check if there are any coins in the view
        
        // Since we don't have direct address->UTXO mapping, we check the contract balance
        // and return 0 for regular addresses (they're managed outside the EVM)
        LogPrint(BCLog::CVM, "EVMCHost: Balance lookup for non-contract address %s: 0 (regular addresses managed outside EVM)\n",
                 cascoin_addr.ToString());
        
        evmc_uint256be zero_balance = {};
        return zero_balance;
    }
    
    // If no coins view available, return zero
    LogPrint(BCLog::CVM, "EVMCHost: Balance lookup for %s returned 0 (no coins view available)\n",
             cascoin_addr.ToString());
    
    evmc_uint256be zero_balance = {};
    return zero_balance;
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
    uint256 gas_price_uint256;
    memset(gas_price_uint256.begin(), 0, 32);
    memcpy(gas_price_uint256.begin(), &tx_context.gas_price, sizeof(tx_context.gas_price));
    return Uint256ToEvmcUint256be(gas_price_uint256);
}

evmc_address EVMCHost::GetTxOrigin() {
    return Uint160ToEvmcAddress(tx_context.origin);
}

evmc_bytes32 EVMCHost::GetBlockHash(int64_t number) {
    // Return current block hash if number matches
    if (number == block_context.number) {
        return Uint256ToEvmcBytes32(block_context.hash);
    }
    
    // EVM BLOCKHASH opcode only returns hashes for the last 256 blocks
    int64_t current_block = block_context.number;
    if (number >= current_block || number < (current_block - 256)) {
        LogPrint(BCLog::CVM, "EVMCHost: Block hash request out of range (requested: %d, current: %d)\n",
                 number, current_block);
        evmc_bytes32 zero = {};
        return zero;
    }
    
    // Query the blockchain for historical block hashes using CBlockIndex
    if (block_index) {
        // Navigate backwards from current block to find the requested block
        CBlockIndex* pindex = block_index;
        int64_t blocks_to_go_back = current_block - number;
        
        // Walk back through the chain
        for (int64_t i = 0; i < blocks_to_go_back && pindex; ++i) {
            pindex = pindex->pprev;
        }
        
        // If we found the block, return its hash
        if (pindex && pindex->nHeight == number && pindex->phashBlock) {
            LogPrint(BCLog::CVM, "EVMCHost: Retrieved block hash for block %d from block index\n", number);
            return Uint256ToEvmcBytes32(*pindex->phashBlock);
        } else {
            LogPrint(BCLog::CVM, "EVMCHost: Failed to find block %d in block index (current: %d)\n",
                     number, current_block);
            evmc_bytes32 zero = {};
            return zero;
        }
    }
    
    // Fallback: If no block index available, return zero
    LogPrint(BCLog::CVM, "EVMCHost: Block hash lookup for block %d failed (no block index available)\n", number);
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
    LogPrint(BCLog::CVM, "EVMCHost: Executing contract call (kind: %d, depth: %d, gas: %d)\n",
             msg.kind, msg.depth, msg.gas);
    
    // Check call depth limit (EVM standard is 1024)
    if (msg.depth >= 1024) {
        LogPrint(BCLog::CVM, "EVMCHost: Call depth limit exceeded\n");
        evmc_result result = {};
        result.status_code = EVMC_CALL_DEPTH_EXCEEDED;
        result.gas_left = 0;
        return result;
    }
    
    // Check for sufficient gas
    if (msg.gas < 0) {
        LogPrint(BCLog::CVM, "EVMCHost: Insufficient gas for call\n");
        evmc_result result = {};
        result.status_code = EVMC_OUT_OF_GAS;
        result.gas_left = 0;
        return result;
    }
    
    // Handle different call types
    switch (msg.kind) {
        case EVMC_CALL:
        case EVMC_CALLCODE:
        case EVMC_DELEGATECALL:
            {
            // Load target contract code
            uint160 target_addr = EvmcAddressToUint160(msg.recipient);
            std::vector<uint8_t> code;
            
            if (!database) {
                LogPrint(BCLog::CVM, "EVMCHost: Database not available for contract call\n");
                evmc_result result = {};
                result.status_code = EVMC_INTERNAL_ERROR;
                result.gas_left = 0;
                return result;
            }
            
            if (!database->LoadContract(target_addr, code)) {
                LogPrint(BCLog::CVM, "EVMCHost: Target contract not found at %s\n",
                         target_addr.ToString());
                evmc_result result = {};
                result.status_code = EVMC_SUCCESS; // Empty account call succeeds with no output
                result.gas_left = msg.gas;
                return result;
            }
            
            // Check for empty code
            if (code.empty()) {
                LogPrint(BCLog::CVM, "EVMCHost: Target contract has empty code at %s\n",
                         target_addr.ToString());
                evmc_result result = {};
                result.status_code = EVMC_SUCCESS;
                result.gas_left = msg.gas;
                return result;
            }
            
            // Apply trust-based gas adjustment for the call
            evmc_message adjusted_msg = msg;
            adjusted_msg.gas = ApplyTrustBasedGasAdjustment(msg.gas, msg.sender);
            
            // Check trust gates for contract calls
            if (!CheckTrustGatedOperation(msg.sender, "contract_call")) {
                LogPrint(BCLog::CVM, "EVMCHost: Trust gate check failed for contract call\n");
                evmc_result result = {};
                result.status_code = EVMC_REVERT;
                result.gas_left = 0;
                return result;
            }
            
            // Recursively execute the target contract
            try {
                evmc_result result = Execute(adjusted_msg, code.data(), code.size());
                LogPrint(BCLog::CVM, "EVMCHost: Contract call completed with status %d, gas left: %d\n",
                         result.status_code, result.gas_left);
                return result;
            } catch (const std::exception& e) {
                LogPrint(BCLog::CVM, "EVMCHost: Exception during contract call: %s\n", e.what());
                evmc_result result = {};
                result.status_code = EVMC_INTERNAL_ERROR;
                result.gas_left = 0;
                return result;
            }
        }
        
        case EVMC_CREATE:
        case EVMC_CREATE2: {
            // Contract creation
            LogPrint(BCLog::CVM, "EVMCHost: Contract creation via %s (depth: %d, gas: %d)\n", 
                     msg.kind == EVMC_CREATE ? "CREATE" : "CREATE2", msg.depth, msg.gas);
            
            // Check database availability
            if (!database) {
                LogPrint(BCLog::CVM, "EVMCHost: Database not available for contract creation\n");
                evmc_result result = {};
                result.status_code = EVMC_INTERNAL_ERROR;
                result.gas_left = 0;
                return result;
            }
            
            // Check trust gates for contract deployment
            if (!CheckTrustGatedOperation(msg.sender, "contract_deployment")) {
                LogPrint(BCLog::CVM, "EVMCHost: Trust gate check failed for contract deployment\n");
                evmc_result result = {};
                result.status_code = EVMC_REVERT;
                result.gas_left = 0;
                return result;
            }
            
            // Validate init code
            if (!msg.input_data || msg.input_size == 0) {
                LogPrint(BCLog::CVM, "EVMCHost: Contract creation with empty init code\n");
                evmc_result result = {};
                result.status_code = EVMC_FAILURE;
                result.gas_left = 0;
                return result;
            }
            
            // Generate new contract address
            uint160 sender_addr = EvmcAddressToUint160(msg.sender);
            uint160 new_contract_addr;
            
            if (msg.kind == EVMC_CREATE) {
                // Standard CREATE: address = hash(sender, nonce)
                uint64_t nonce = database->GetNextNonce(sender_addr);
                new_contract_addr = GenerateContractAddress(sender_addr, nonce);
            } else {
                // CREATE2: address = hash(0xff, sender, salt, code_hash)
                // The salt is passed in msg.create2_salt
                // Calculate code hash from init code
                CHashWriter code_hasher(SER_GETHASH, 0);
                code_hasher.write((const char*)msg.input_data, msg.input_size);
                uint256 code_hash = code_hasher.GetHash();
                
                uint256 salt = EvmcBytes32ToUint256(msg.create2_salt);
                new_contract_addr = GenerateCreate2Address(sender_addr, salt, code_hash);
            }
            
            // Check if contract already exists
            if (database->Exists(new_contract_addr)) {
                LogPrint(BCLog::CVM, "EVMCHost: Contract already exists at generated address %s\n",
                         new_contract_addr.ToString());
                evmc_result result = {};
                result.status_code = EVMC_FAILURE;
                return result;
            }
            
            // Check contract size limit
            if (msg.input_size > MAX_CODE_SIZE) {
                LogPrint(BCLog::CVM, "EVMCHost: Init code size %d exceeds maximum %d\n",
                         msg.input_size, MAX_CODE_SIZE);
                evmc_result result = {};
                result.status_code = EVMC_FAILURE;
                return result;
            }
            
            // Execute constructor code
            const uint8_t* init_code = msg.input_data;
            size_t init_code_size = msg.input_size;
            
            evmc_message constructor_msg = msg;
            constructor_msg.recipient = Uint160ToEvmcAddress(new_contract_addr);
            constructor_msg.kind = EVMC_CALL; // Constructor executes as a call
            
            evmc_result result;
            try {
                result = Execute(constructor_msg, init_code, init_code_size);
            } catch (const std::exception& e) {
                LogPrint(BCLog::CVM, "EVMCHost: Exception during constructor execution: %s\n", e.what());
                evmc_result error_result = {};
                error_result.status_code = EVMC_INTERNAL_ERROR;
                error_result.gas_left = 0;
                return error_result;
            }
            
            // If successful, store the deployed code
            if (result.status_code == EVMC_SUCCESS) {
                if (result.output_data && result.output_size > 0) {
                    // Check deployed code size limit
                    if (result.output_size > MAX_CODE_SIZE) {
                        LogPrint(BCLog::CVM, "EVMCHost: Deployed code size %d exceeds maximum %d\n",
                                 result.output_size, MAX_CODE_SIZE);
                        result.status_code = EVMC_FAILURE;
                        return result;
                    }
                    
                    std::vector<uint8_t> deployed_code(result.output_data, result.output_data + result.output_size);
                    
                    Contract contract;
                    contract.address = new_contract_addr;
                    contract.code = deployed_code;
                    
                    if (database->WriteContract(new_contract_addr, contract)) {
                        LogPrint(BCLog::CVM, "EVMCHost: Contract deployed successfully at %s (code size: %d bytes)\n",
                                 new_contract_addr.ToString(), result.output_size);
                        
                        // Return the new contract address in the result
                        result.create_address = Uint160ToEvmcAddress(new_contract_addr);
                        
                        // Increment sender nonce for CREATE (not CREATE2)
                        if (msg.kind == EVMC_CREATE) {
                            uint64_t new_nonce = database->GetNextNonce(sender_addr);
                            database->WriteNonce(sender_addr, new_nonce);
                        }
                    } else {
                        LogPrint(BCLog::CVM, "EVMCHost: Failed to store deployed contract\n");
                        result.status_code = EVMC_FAILURE;
                    }
                } else {
                    // Empty code deployment (valid but unusual)
                    LogPrint(BCLog::CVM, "EVMCHost: Contract deployed with empty code at %s\n",
                             new_contract_addr.ToString());
                    
                    Contract contract;
                    contract.address = new_contract_addr;
                    contract.code = std::vector<uint8_t>();
                    
                    database->WriteContract(new_contract_addr, contract);
                    result.create_address = Uint160ToEvmcAddress(new_contract_addr);
                }
            } else {
                LogPrint(BCLog::CVM, "EVMCHost: Contract deployment failed with status %d\n",
                         result.status_code);
            }
            
            return result;
        }
        
        default: {
            LogPrint(BCLog::CVM, "EVMCHost: Unknown call kind: %d\n", msg.kind);
            evmc_result result = {};
            result.status_code = EVMC_FAILURE;
            return result;
        }
    }
    
    // Should not reach here
    evmc_result result = {};
    result.status_code = EVMC_INTERNAL_ERROR;
    return result;
}

void EVMCHost::Selfdestruct(const evmc_address& addr, const evmc_address& beneficiary) {
    uint160 cascoin_addr = EvmcAddressToUint160(addr);
    uint160 beneficiary_addr = EvmcAddressToUint160(beneficiary);
    
    // Mark account as self-destructed
    selfdestructed_accounts.insert(addr);
    
    // Transfer balance to beneficiary
    uint64_t balance = 0;
    if (database->ReadBalance(cascoin_addr, balance) && balance > 0) {
        // Get beneficiary's current balance
        uint64_t beneficiary_balance = 0;
        database->ReadBalance(beneficiary_addr, beneficiary_balance);
        
        // Transfer balance
        database->WriteBalance(beneficiary_addr, beneficiary_balance + balance);
        database->WriteBalance(cascoin_addr, 0);
        
        LogPrint(BCLog::CVM, "EVMCHost: Selfdestruct transferred %d from %s to %s\n",
                 balance, cascoin_addr.ToString(), beneficiary_addr.ToString());
    }
    
    // Note: The actual contract deletion happens after transaction execution
    // to maintain proper state consistency
    LogPrint(BCLog::CVM, "EVMCHost: Contract %s marked for self-destruct\n",
             cascoin_addr.ToString());
}

// Helper Functions

uint160 EVMCHost::EvmcAddressToUint160(const evmc_address& addr) {
    uint160 result;
    static_assert(sizeof(evmc_address::bytes) == 20, "EVMC address must be 20 bytes");
    static_assert(sizeof(uint160) == 20, "uint160 must be 20 bytes");
    std::memcpy(result.begin(), addr.bytes, 20);
    return result;
}

evmc_address EVMCHost::Uint160ToEvmcAddress(const uint160& addr) {
    evmc_address result = {};
    static_assert(sizeof(evmc_address::bytes) == 20, "EVMC address must be 20 bytes");
    static_assert(sizeof(uint160) == 20, "uint160 must be 20 bytes");
    std::memcpy(result.bytes, addr.begin(), 20);
    return result;
}

uint256 EVMCHost::EvmcBytes32ToUint256(const evmc_bytes32& bytes) {
    uint256 result;
    static_assert(sizeof(evmc_bytes32::bytes) == 32, "EVMC bytes32 must be 32 bytes");
    static_assert(sizeof(uint256) == 32, "uint256 must be 32 bytes");
    std::memcpy(result.begin(), bytes.bytes, 32);
    return result;
}

evmc_bytes32 EVMCHost::Uint256ToEvmcBytes32(const uint256& value) {
    evmc_bytes32 result = {};
    static_assert(sizeof(evmc_bytes32::bytes) == 32, "EVMC bytes32 must be 32 bytes");
    static_assert(sizeof(uint256) == 32, "uint256 must be 32 bytes");
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

uint160 EVMCHost::GenerateContractAddress(const uint160& sender, uint64_t nonce) {
    // Ethereum-style contract address generation: address = keccak256(rlp([sender, nonce]))[12:]
    // For Cascoin, we use a simpler approach: hash(sender || nonce)
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << sender;
    hasher << nonce;
    uint256 hash = hasher.GetHash();
    
    // Take the last 160 bits (20 bytes) of the hash
    uint160 contract_addr;
    std::memcpy(contract_addr.begin(), hash.begin(), 20);
    
    LogPrint(BCLog::CVM, "EVMCHost: Generated contract address %s from sender %s and nonce %d\n",
             contract_addr.ToString(), sender.ToString(), nonce);
    
    return contract_addr;
}

uint160 EVMCHost::GenerateCreate2Address(const uint160& sender, const uint256& salt, const uint256& code_hash) {
    // CREATE2 address generation: address = keccak256(0xff || sender || salt || keccak256(init_code))[12:]
    // For Cascoin, we use: hash(0xff || sender || salt || code_hash)
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << uint8_t(0xff);
    hasher << sender;
    hasher << salt;
    hasher << code_hash;
    uint256 hash = hasher.GetHash();
    
    // Take the last 160 bits (20 bytes) of the hash
    uint160 contract_addr;
    std::memcpy(contract_addr.begin(), hash.begin(), 20);
    
    LogPrint(BCLog::CVM, "EVMCHost: Generated CREATE2 contract address %s\n", contract_addr.ToString());
    
    return contract_addr;
}

// Utility Functions

namespace EVMCUtils {

evmc_address AddressFromHex(const std::string& hex) {
    evmc_address addr = {};
    
    // Remove "0x" prefix if present
    std::string clean_hex = hex;
    if (clean_hex.size() >= 2 && clean_hex[0] == '0' && (clean_hex[1] == 'x' || clean_hex[1] == 'X')) {
        clean_hex = clean_hex.substr(2);
    }
    
    // Pad with zeros if necessary (addresses are 20 bytes = 40 hex chars)
    if (clean_hex.size() < 40) {
        clean_hex = std::string(40 - clean_hex.size(), '0') + clean_hex;
    }
    
    // Convert hex string to bytes
    for (size_t i = 0; i < 20 && i * 2 < clean_hex.size(); ++i) {
        std::string byte_str = clean_hex.substr(i * 2, 2);
        addr.bytes[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
    
    return addr;
}

std::string AddressToHex(const evmc_address& addr) {
    std::string hex = "0x";
    const char* hex_chars = "0123456789abcdef";
    
    for (size_t i = 0; i < 20; ++i) {
        hex += hex_chars[(addr.bytes[i] >> 4) & 0xF];
        hex += hex_chars[addr.bytes[i] & 0xF];
    }
    
    return hex;
}

evmc_bytes32 HashFromHex(const std::string& hex) {
    evmc_bytes32 hash = {};
    
    // Remove "0x" prefix if present
    std::string clean_hex = hex;
    if (clean_hex.size() >= 2 && clean_hex[0] == '0' && (clean_hex[1] == 'x' || clean_hex[1] == 'X')) {
        clean_hex = clean_hex.substr(2);
    }
    
    // Pad with zeros if necessary (hashes are 32 bytes = 64 hex chars)
    if (clean_hex.size() < 64) {
        clean_hex = std::string(64 - clean_hex.size(), '0') + clean_hex;
    }
    
    // Convert hex string to bytes
    for (size_t i = 0; i < 32 && i * 2 < clean_hex.size(); ++i) {
        std::string byte_str = clean_hex.substr(i * 2, 2);
        hash.bytes[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
    
    return hash;
}

std::string HashToHex(const evmc_bytes32& hash) {
    std::string hex = "0x";
    const char* hex_chars = "0123456789abcdef";
    
    for (size_t i = 0; i < 32; ++i) {
        hex += hex_chars[(hash.bytes[i] >> 4) & 0xF];
        hex += hex_chars[hash.bytes[i] & 0xF];
    }
    
    return hex;
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
    // Convert EVMC address to Cascoin address
    uint160 cascoin_addr;
    std::memcpy(cascoin_addr.begin(), addr.bytes, 20);
    
    // Get reputation score
    uint32_t reputation = ctx.GetReputation(cascoin_addr);
    
    // High reputation threshold is 80+
    return reputation >= 80;
}

} // namespace EVMCUtils

} // namespace CVM

#endif // ENABLE_EVMC
