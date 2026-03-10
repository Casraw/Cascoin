// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_burn.cpp
 * @brief RPC commands for L2 Burn-and-Mint Token Model
 * 
 * This file implements RPC commands for the burn-and-mint system:
 * - l2_createburntx: Create burn transaction with OP_RETURN
 * - l2_sendburntx: Broadcast signed burn transaction
 * - l2_getburnstatus: Get status of a burn transaction
 * - l2_getpendingburns: List burns waiting for consensus
 * - l2_getminthistory: Get mint history
 * - l2_gettotalsupply: Get L2 token total supply
 * - l2_verifysupply: Verify supply invariant
 * - l2_getburnsforaddress: Get burns for an address
 * 
 * Requirements: 1.5, 1.6, 5.5, 8.2, 9.1, 9.2, 9.3, 9.4, 9.5, 9.6
 */

#include <rpc/server.h>
#include <rpc/util.h>
#include <core_io.h>
#include <l2/l2_common.h>
#include <l2/burn_parser.h>
#include <l2/burn_validator.h>
#include <l2/burn_registry.h>
#include <l2/mint_consensus.h>
#include <l2/l2_minter.h>
#include <l2/state_manager.h>
#include <validation.h>
#include <consensus/validation.h>
#include <chain.h>
#include <net.h>
#include <net_processing.h>
#include <protocol.h>
#include <util.h>
#include <base58.h>
#include <utilstrencodings.h>
#include <utilmoneystr.h>
#include <univalue.h>
#include <key.h>
#include <pubkey.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#include <primitives/transaction.h>
#include <coins.h>
#include <txmempool.h>

#include <memory>
#include <chrono>

// Global L2 burn-and-mint components
namespace {
    std::unique_ptr<l2::BurnRegistry> g_burnRegistry;
    std::unique_ptr<l2::BurnValidator> g_burnValidator;
    CCriticalSection cs_burn;
}

// Helper function to check if L2 is enabled
static void EnsureL2Enabled()
{
    if (!l2::IsL2Enabled()) {
        throw JSONRPCError(RPC_MISC_ERROR, "L2 is not enabled. Start node with -l2 flag.");
    }
}

// Helper function to get or create burn registry
static l2::BurnRegistry& GetBurnRegistry()
{
    LOCK(cs_burn);
    if (!g_burnRegistry) {
        g_burnRegistry = std::make_unique<l2::BurnRegistry>();
    }
    return *g_burnRegistry;
}

// Helper function to get or create burn validator
static l2::BurnValidator& GetBurnValidator()
{
    LOCK(cs_burn);
    if (!g_burnValidator) {
        g_burnValidator = std::make_unique<l2::BurnValidator>(l2::GetL2ChainId());
        
        // Set up callbacks for the validator
        g_burnValidator->SetProcessedChecker([](const uint256& txHash) {
            return GetBurnRegistry().IsProcessed(txHash);
        });
    }
    return *g_burnValidator;
}

// Helper to parse address from hex or base58
static uint160 ParseL2Address(const std::string& addressStr)
{
    std::string addrHex = addressStr;
    
    // Handle 0x prefix
    if (addrHex.substr(0, 2) == "0x") {
        addrHex = addrHex.substr(2);
    }
    
    // If it's 40 hex chars, parse directly
    if (addrHex.length() == 40 && IsHex(addrHex)) {
        std::vector<unsigned char> addressBytes = ParseHex(addrHex);
        return uint160(addressBytes);
    }
    
    // Try base58 decode
    CTxDestination dest = DecodeDestination(addressStr);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address format");
    }
    
    if (const CKeyID* keyId = boost::get<CKeyID>(&dest)) {
        return uint160(*keyId);
    } else if (const CScriptID* scriptId = boost::get<CScriptID>(&dest)) {
        return uint160(*scriptId);
    }
    
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unsupported address type");
}

// Helper to parse public key from hex or address
static CPubKey ParseRecipientPubKey(const std::string& recipientStr, CWallet* pwallet)
{
    // Try to parse as hex public key first (33 or 65 bytes)
    if (IsHex(recipientStr)) {
        std::vector<unsigned char> pubkeyData = ParseHex(recipientStr);
        CPubKey pubkey(pubkeyData.begin(), pubkeyData.end());
        if (pubkey.IsValid()) {
            // Must be compressed for OP_RETURN burn format
            if (!pubkey.IsCompressed()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                    "Public key must be compressed (33 bytes). Uncompressed keys are not supported.");
            }
            return pubkey;
        }
    }
    
    // Try to parse as address and get pubkey from wallet
    CTxDestination dest = DecodeDestination(recipientStr);
    if (IsValidDestination(dest)) {
        if (const CKeyID* keyId = boost::get<CKeyID>(&dest)) {
            if (pwallet) {
                CPubKey pubkey;
                if (pwallet->GetPubKey(*keyId, pubkey)) {
                    if (!pubkey.IsCompressed()) {
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                            "Wallet key must be compressed. Please use a compressed key.");
                    }
                    return pubkey;
                }
            }
        }
    }
    
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
        "Invalid recipient. Provide a 33-byte compressed public key (hex) or an address from your wallet.");
}


// ============================================================================
// Task 10.1: l2_createburntx RPC
// Requirements: 1.5, 9.1
// ============================================================================

UniValue l2_createburntx(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "l2_createburntx amount \"l2_recipient\" ( \"change_address\" )\n"
            "\nCreate an unsigned burn transaction with OP_RETURN output.\n"
            "\nThis transaction burns CAS on L1 to mint L2 tokens after sequencer consensus.\n"
            "\nArguments:\n"
            "1. amount              (numeric, required) Amount of CAS to burn\n"
            "2. \"l2_recipient\"      (string, required) L2 recipient public key (33-byte hex) or wallet address\n"
            "3. \"change_address\"    (string, optional) Address for change output\n"
            "\nResult:\n"
            "{\n"
            "  \"hex\": \"xxx\",              (string) Unsigned transaction hex\n"
            "  \"burnAmount\": \"x.xx\",      (string) Amount being burned\n"
            "  \"l2Recipient\": \"xxx\",      (string) L2 recipient public key\n"
            "  \"l2RecipientAddress\": \"xxx\",(string) L2 recipient address (hash160)\n"
            "  \"chainId\": n,               (numeric) L2 chain ID\n"
            "  \"fee\": \"x.xx\",             (string) Estimated transaction fee\n"
            "  \"burnScript\": \"xxx\",       (string) OP_RETURN burn script (hex)\n"
            "  \"message\": \"xxx\"           (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_createburntx", "100 \"02abc123...def\"")
            + HelpExampleCli("l2_createburntx", "100 \"CASaddress...\" \"CASchangeaddress...\"")
            + HelpExampleRpc("l2_createburntx", "100, \"02abc123...def\"")
        );

    EnsureL2Enabled();

    // Get wallet for funding the transaction
    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Parse amount
    CAmount burnAmount = AmountFromValue(request.params[0]);
    if (burnAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Burn amount must be positive");
    }
    
    if (burnAmount > l2::MAX_BURN_AMOUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Burn amount exceeds maximum (%s CAS)", FormatMoney(l2::MAX_BURN_AMOUNT)));
    }

    // Parse recipient public key
    std::string recipientStr = request.params[1].get_str();
    CPubKey recipientPubKey = ParseRecipientPubKey(recipientStr, pwallet);
    
    if (!recipientPubKey.IsValid() || !recipientPubKey.IsCompressed()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
            "Recipient public key must be a valid 33-byte compressed public key");
    }

    // Get L2 chain ID
    uint32_t chainId = l2::GetL2ChainId();

    // Create the burn script
    CScript burnScript = l2::BurnTransactionParser::CreateBurnScript(chainId, recipientPubKey, burnAmount);
    if (burnScript.empty()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to create burn script");
    }

    // Create the transaction
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.nLockTime = 0;

    // Add OP_RETURN output (value = 0)
    mtx.vout.emplace_back(0, burnScript);

    // Select coins to fund the transaction
    // We need: burnAmount + fee
    std::vector<COutput> vAvailableCoins;
    pwallet->AvailableCoins(vAvailableCoins);

    // Estimate fee (use a reasonable estimate)
    CAmount estimatedFee = 10000; // 0.0001 CAS default fee estimate
    CAmount totalNeeded = burnAmount + estimatedFee;

    // Select coins
    std::set<std::pair<const CWalletTx*, unsigned int>> setCoins;
    CAmount nValueIn = 0;
    
    for (const COutput& out : vAvailableCoins) {
        if (nValueIn >= totalNeeded) break;
        
        setCoins.emplace(out.tx, out.i);
        nValueIn += out.tx->tx->vout[out.i].nValue;
    }


    if (nValueIn < totalNeeded) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, 
            strprintf("Insufficient funds. Need %s CAS, have %s CAS", 
                      FormatMoney(totalNeeded), FormatMoney(nValueIn)));
    }

    // Add inputs
    for (const auto& coin : setCoins) {
        mtx.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));
    }

    // Calculate change
    CAmount change = nValueIn - burnAmount - estimatedFee;
    
    // Add change output if significant
    if (change > 546) { // Dust threshold
        CTxDestination changeDest;
        
        if (request.params.size() > 2) {
            // Use provided change address
            changeDest = DecodeDestination(request.params[2].get_str());
            if (!IsValidDestination(changeDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid change address");
            }
        } else {
            // Get new change address from wallet
            CPubKey changeKey;
            if (!pwallet->GetKeyFromPool(changeKey)) {
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
            }
            changeDest = changeKey.GetID();
        }
        
        CScript changeScript = GetScriptForDestination(changeDest);
        mtx.vout.push_back(CTxOut(change, changeScript));
    }

    // Serialize the unsigned transaction
    CTransaction tx(mtx);
    std::string txHex = EncodeHexTx(tx);

    // Build response
    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", txHex);
    result.pushKV("burnAmount", ValueFromAmount(burnAmount));
    result.pushKV("l2Recipient", HexStr(recipientPubKey.begin(), recipientPubKey.end()));
    result.pushKV("l2RecipientAddress", "0x" + recipientPubKey.GetID().GetHex());
    result.pushKV("chainId", (int64_t)chainId);
    result.pushKV("fee", ValueFromAmount(estimatedFee));
    result.pushKV("burnScript", HexStr(burnScript.begin(), burnScript.end()));
    result.pushKV("message", "Unsigned burn transaction created. Sign with signrawtransaction and broadcast with l2_sendburntx.");
    
    return result;
}


// ============================================================================
// Task 10.2: l2_sendburntx RPC
// Requirements: 1.6, 9.1
// ============================================================================

UniValue l2_sendburntx(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_sendburntx \"hex\"\n"
            "\nBroadcast a signed burn transaction to the L1 network.\n"
            "\nArguments:\n"
            "1. \"hex\"    (string, required) Signed transaction hex\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",             (string) L1 transaction hash\n"
            "  \"burnAmount\": \"x.xx\",      (string) Amount burned\n"
            "  \"l2Recipient\": \"xxx\",      (string) L2 recipient address\n"
            "  \"chainId\": n,               (numeric) L2 chain ID\n"
            "  \"confirmationsNeeded\": n,   (numeric) Confirmations needed before minting\n"
            "  \"message\": \"xxx\"           (string) Status message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_sendburntx", "\"0100000001...\"")
            + HelpExampleRpc("l2_sendburntx", "\"0100000001...\"")
        );

    EnsureL2Enabled();

    // Parse the transaction
    std::string txHex = request.params[0].get_str();
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, txHex)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to decode transaction hex");
    }

    CTransactionRef tx = MakeTransactionRef(std::move(mtx));

    // Validate it's a burn transaction
    auto burnData = l2::BurnTransactionParser::ParseBurnTransaction(*tx);
    if (!burnData) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "Transaction is not a valid burn transaction. Missing or invalid OP_RETURN burn output.");
    }

    // Verify chain ID matches
    if (burnData->chainId != l2::GetL2ChainId()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Burn transaction chain ID (%d) does not match current L2 chain (%d)",
                      burnData->chainId, l2::GetL2ChainId()));
    }

    // Check if already processed
    if (GetBurnRegistry().IsProcessed(tx->GetHash())) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "This burn transaction has already been processed");
    }

    // Broadcast the transaction
    CValidationState state;
    bool fMissingInputs;
    
    if (!AcceptToMemoryPool(mempool, state, tx, &fMissingInputs, nullptr, false, 0)) {
        if (state.IsInvalid()) {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, 
                strprintf("Transaction rejected: %s", state.GetRejectReason()));
        } else if (fMissingInputs) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
        } else {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, 
                strprintf("Transaction not accepted: %s", state.GetRejectReason()));
        }
    }

    // Relay to network via CConnman
    if (g_connman) {
        CInv inv(MSG_TX, tx->GetHash());
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }

    // Build response
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", tx->GetHash().GetHex());
    result.pushKV("burnAmount", ValueFromAmount(burnData->amount));
    result.pushKV("l2Recipient", "0x" + burnData->GetRecipientAddress().GetHex());
    result.pushKV("chainId", (int64_t)burnData->chainId);
    result.pushKV("confirmationsNeeded", l2::REQUIRED_CONFIRMATIONS);
    result.pushKV("message", 
        strprintf("Burn transaction broadcast successfully. Wait for %d confirmations before L2 tokens are minted.",
                  l2::REQUIRED_CONFIRMATIONS));
    
    return result;
}


// ============================================================================
// Task 10.3: l2_getburnstatus RPC
// Requirements: 5.5, 9.2
// ============================================================================

UniValue l2_getburnstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_getburnstatus \"l1txhash\"\n"
            "\nGet the status of a burn transaction.\n"
            "\nArguments:\n"
            "1. \"l1txhash\"    (string, required) L1 burn transaction hash\n"
            "\nResult:\n"
            "{\n"
            "  \"found\": bool,              (boolean) Whether burn was found\n"
            "  \"l1TxHash\": \"xxx\",         (string) L1 transaction hash\n"
            "  \"confirmations\": n,         (numeric) L1 confirmations\n"
            "  \"confirmationsRequired\": n, (numeric) Confirmations required\n"
            "  \"consensusStatus\": \"xxx\",  (string) Consensus status (PENDING, REACHED, MINTED, FAILED)\n"
            "  \"consensusProgress\": n,     (numeric) Confirmations received / total sequencers\n"
            "  \"mintStatus\": \"xxx\",       (string) Mint status (NOT_MINTED, MINTED)\n"
            "  \"burnAmount\": \"x.xx\",      (string) Amount burned\n"
            "  \"l2Recipient\": \"xxx\",      (string) L2 recipient address\n"
            "  \"l2MintTxHash\": \"xxx\",     (string) L2 mint transaction hash (if minted)\n"
            "  \"l2MintBlock\": n,           (numeric) L2 block where minted (if minted)\n"
            "  \"timestamp\": n              (numeric) Processing timestamp (if minted)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getburnstatus", "\"abc123...\"")
            + HelpExampleRpc("l2_getburnstatus", "\"abc123...\"")
        );

    EnsureL2Enabled();

    uint256 l1TxHash = ParseHashV(request.params[0], "l1txhash");

    UniValue result(UniValue::VOBJ);
    result.pushKV("l1TxHash", l1TxHash.GetHex());

    // Check burn registry first (for processed burns)
    l2::BurnRegistry& registry = GetBurnRegistry();
    auto burnRecord = registry.GetBurnRecord(l1TxHash);

    if (burnRecord) {
        // Burn was processed and minted
        result.pushKV("found", true);
        result.pushKV("confirmations", l2::REQUIRED_CONFIRMATIONS); // At least 6 if minted
        result.pushKV("confirmationsRequired", l2::REQUIRED_CONFIRMATIONS);
        result.pushKV("consensusStatus", "MINTED");
        result.pushKV("consensusProgress", 1.0);
        result.pushKV("mintStatus", "MINTED");
        result.pushKV("burnAmount", ValueFromAmount(burnRecord->amount));
        result.pushKV("l2Recipient", "0x" + burnRecord->l2Recipient.GetHex());
        result.pushKV("l2MintTxHash", burnRecord->l2MintTxHash.GetHex());
        result.pushKV("l2MintBlock", (int64_t)burnRecord->l2MintBlock);
        result.pushKV("timestamp", (int64_t)burnRecord->timestamp);
        result.pushKV("l1BlockNumber", (int64_t)burnRecord->l1BlockNumber);
        result.pushKV("l1BlockHash", burnRecord->l1BlockHash.GetHex());
        return result;
    }

    // Check consensus manager for pending burns
    if (l2::IsMintConsensusManagerInitialized()) {
        l2::MintConsensusManager& consensus = l2::GetMintConsensusManager();
        auto consensusState = consensus.GetConsensusState(l1TxHash);
        
        if (consensusState) {
            result.pushKV("found", true);
            result.pushKV("confirmationsRequired", l2::REQUIRED_CONFIRMATIONS);
            result.pushKV("consensusStatus", consensusState->GetStatusString());
            result.pushKV("consensusConfirmations", (int64_t)consensusState->GetConfirmationCount());
            result.pushKV("mintStatus", "NOT_MINTED");
            result.pushKV("burnAmount", ValueFromAmount(consensusState->burnData.amount));
            result.pushKV("l2Recipient", "0x" + consensusState->burnData.GetRecipientAddress().GetHex());
            result.pushKV("firstSeenTime", (int64_t)consensusState->firstSeenTime);
            
            // Try to get L1 confirmation count
            l2::BurnValidator& validator = GetBurnValidator();
            if (validator.HasSufficientConfirmations(l1TxHash)) {
                result.pushKV("confirmations", l2::REQUIRED_CONFIRMATIONS);
            } else {
                result.pushKV("confirmations", 0); // Unknown
            }
            
            return result;
        }
    }

    // Try to find the transaction in the blockchain
    uint256 hashBlock;
    CTransactionRef tx;
    if (GetTransaction(l1TxHash, tx, Params().GetConsensus(), hashBlock, true)) {
        auto burnData = l2::BurnTransactionParser::ParseBurnTransaction(*tx);
        
        if (burnData) {
            result.pushKV("found", true);
            result.pushKV("burnAmount", ValueFromAmount(burnData->amount));
            result.pushKV("l2Recipient", "0x" + burnData->GetRecipientAddress().GetHex());
            result.pushKV("chainId", (int64_t)burnData->chainId);
            
            // Get confirmation count
            int confirmations = 0;
            if (!hashBlock.IsNull()) {
                LOCK(cs_main);
                BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
                if (mi != mapBlockIndex.end() && mi->second) {
                    CBlockIndex* pindex = mi->second;
                    if (chainActive.Contains(pindex)) {
                        confirmations = chainActive.Height() - pindex->nHeight + 1;
                    }
                }
            }
            
            result.pushKV("confirmations", confirmations);
            result.pushKV("confirmationsRequired", l2::REQUIRED_CONFIRMATIONS);
            
            if (confirmations >= l2::REQUIRED_CONFIRMATIONS) {
                result.pushKV("consensusStatus", "PENDING");
                result.pushKV("mintStatus", "NOT_MINTED");
                result.pushKV("message", "Burn has enough confirmations. Waiting for sequencer consensus.");
            } else {
                result.pushKV("consensusStatus", "WAITING_CONFIRMATIONS");
                result.pushKV("mintStatus", "NOT_MINTED");
                result.pushKV("message", 
                    strprintf("Waiting for %d more confirmations.", 
                              l2::REQUIRED_CONFIRMATIONS - confirmations));
            }
            
            return result;
        }
    }

    // Not found
    result.pushKV("found", false);
    result.pushKV("message", "Burn transaction not found");
    return result;
}


// ============================================================================
// Task 10.4: l2_getpendingburns RPC
// Requirements: 9.6
// ============================================================================

UniValue l2_getpendingburns(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_getpendingburns\n"
            "\nGet list of burns waiting for sequencer consensus.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"l1TxHash\": \"xxx\",         (string) L1 burn transaction hash\n"
            "    \"burnAmount\": \"x.xx\",      (string) Amount burned\n"
            "    \"l2Recipient\": \"xxx\",      (string) L2 recipient address\n"
            "    \"chainId\": n,               (numeric) L2 chain ID\n"
            "    \"status\": \"xxx\",           (string) Consensus status\n"
            "    \"confirmationCount\": n,     (numeric) Number of sequencer confirmations\n"
            "    \"firstSeenTime\": n,         (numeric) When first seen (Unix timestamp)\n"
            "    \"timeoutIn\": n              (numeric) Seconds until timeout\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getpendingburns", "")
            + HelpExampleRpc("l2_getpendingburns", "")
        );

    EnsureL2Enabled();

    UniValue result(UniValue::VARR);

    if (!l2::IsMintConsensusManagerInitialized()) {
        return result; // Empty array if not initialized
    }

    l2::MintConsensusManager& consensus = l2::GetMintConsensusManager();
    std::vector<l2::MintConsensusState> pendingBurns = consensus.GetPendingBurns();

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& state : pendingBurns) {
        UniValue burnObj(UniValue::VOBJ);
        burnObj.pushKV("l1TxHash", state.l1TxHash.GetHex());
        burnObj.pushKV("burnAmount", ValueFromAmount(state.burnData.amount));
        burnObj.pushKV("l2Recipient", "0x" + state.burnData.GetRecipientAddress().GetHex());
        burnObj.pushKV("chainId", (int64_t)state.burnData.chainId);
        burnObj.pushKV("status", state.GetStatusString());
        burnObj.pushKV("confirmationCount", (int64_t)state.GetConfirmationCount());
        burnObj.pushKV("firstSeenTime", (int64_t)state.firstSeenTime);
        
        // Calculate timeout
        uint64_t elapsed = now > state.firstSeenTime ? now - state.firstSeenTime : 0;
        uint64_t timeoutIn = elapsed < l2::MINT_CONSENSUS_TIMEOUT_SECONDS 
            ? l2::MINT_CONSENSUS_TIMEOUT_SECONDS - elapsed 
            : 0;
        burnObj.pushKV("timeoutIn", (int64_t)timeoutIn);
        
        result.push_back(burnObj);
    }

    return result;
}


// ============================================================================
// Task 10.5: l2_getminthistory RPC
// Requirements: 9.3
// ============================================================================

UniValue l2_getminthistory(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "l2_getminthistory ( from_block to_block )\n"
            "\nGet the history of L2 token mints from burns.\n"
            "\nArguments:\n"
            "1. from_block    (numeric, optional, default=0) Start L2 block (inclusive)\n"
            "2. to_block      (numeric, optional, default=current) End L2 block (inclusive)\n"
            "\nResult:\n"
            "{\n"
            "  \"fromBlock\": n,              (numeric) Start block\n"
            "  \"toBlock\": n,                (numeric) End block\n"
            "  \"count\": n,                  (numeric) Number of mints\n"
            "  \"totalMinted\": \"x.xx\",      (string) Total amount minted in range\n"
            "  \"mints\": [                   (array) Mint records\n"
            "    {\n"
            "      \"l1TxHash\": \"xxx\",       (string) L1 burn transaction hash\n"
            "      \"l2MintTxHash\": \"xxx\",   (string) L2 mint transaction hash\n"
            "      \"l2MintBlock\": n,         (numeric) L2 block where minted\n"
            "      \"l2Recipient\": \"xxx\",    (string) L2 recipient address\n"
            "      \"amount\": \"x.xx\",        (string) Amount minted\n"
            "      \"timestamp\": n            (numeric) Mint timestamp\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getminthistory", "")
            + HelpExampleCli("l2_getminthistory", "0 100")
            + HelpExampleRpc("l2_getminthistory", "0, 100")
        );

    EnsureL2Enabled();

    uint64_t fromBlock = 0;
    uint64_t toBlock = UINT64_MAX;

    if (request.params.size() > 0) {
        fromBlock = request.params[0].get_int64();
    }
    if (request.params.size() > 1) {
        toBlock = request.params[1].get_int64();
    }

    l2::BurnRegistry& registry = GetBurnRegistry();
    std::vector<l2::BurnRecord> records = registry.GetBurnHistory(fromBlock, toBlock);

    CAmount totalMinted = 0;
    UniValue mintsArray(UniValue::VARR);

    for (const auto& record : records) {
        UniValue mintObj(UniValue::VOBJ);
        mintObj.pushKV("l1TxHash", record.l1TxHash.GetHex());
        mintObj.pushKV("l2MintTxHash", record.l2MintTxHash.GetHex());
        mintObj.pushKV("l2MintBlock", (int64_t)record.l2MintBlock);
        mintObj.pushKV("l2Recipient", "0x" + record.l2Recipient.GetHex());
        mintObj.pushKV("amount", ValueFromAmount(record.amount));
        mintObj.pushKV("timestamp", (int64_t)record.timestamp);
        mintObj.pushKV("l1BlockNumber", (int64_t)record.l1BlockNumber);
        mintObj.pushKV("l1BlockHash", record.l1BlockHash.GetHex());
        mintsArray.push_back(mintObj);
        totalMinted += record.amount;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("fromBlock", (int64_t)fromBlock);
    result.pushKV("toBlock", (int64_t)(toBlock == UINT64_MAX ? 0 : toBlock));
    result.pushKV("count", (int64_t)records.size());
    result.pushKV("totalMinted", ValueFromAmount(totalMinted));
    result.pushKV("mints", mintsArray);

    return result;
}


// ============================================================================
// Task 10.6: l2_gettotalsupply RPC
// Requirements: 9.4
// ============================================================================

UniValue l2_gettotalsupply(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_gettotalsupply\n"
            "\nGet the current L2 token total supply.\n"
            "\nThe total supply equals the total amount of CAS burned on L1.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalSupply\": \"x.xx\",      (string) Total L2 token supply\n"
            "  \"totalBurnedL1\": \"x.xx\",    (string) Total CAS burned on L1\n"
            "  \"totalMintedL2\": \"x.xx\",    (string) Total tokens minted on L2\n"
            "  \"burnCount\": n,              (numeric) Number of burn transactions processed\n"
            "  \"chainId\": n,                (numeric) L2 chain ID\n"
            "  \"invariantValid\": bool       (boolean) Whether supply invariant holds\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_gettotalsupply", "")
            + HelpExampleRpc("l2_gettotalsupply", "")
        );

    EnsureL2Enabled();

    l2::BurnRegistry& registry = GetBurnRegistry();
    CAmount totalBurned = registry.GetTotalBurned();
    size_t burnCount = registry.GetBurnCount();

    UniValue result(UniValue::VOBJ);
    result.pushKV("totalSupply", ValueFromAmount(totalBurned));
    result.pushKV("totalBurnedL1", ValueFromAmount(totalBurned));
    
    // Get minted amount from L2TokenMinter if available
    if (l2::IsL2TokenMinterInitialized()) {
        l2::L2TokenMinter& minter = l2::GetL2TokenMinter();
        result.pushKV("totalMintedL2", ValueFromAmount(minter.GetTotalMintedL2()));
        result.pushKV("invariantValid", minter.VerifySupplyInvariant());
    } else {
        result.pushKV("totalMintedL2", ValueFromAmount(totalBurned));
        result.pushKV("invariantValid", true);
    }
    
    result.pushKV("burnCount", (int64_t)burnCount);
    result.pushKV("chainId", (int64_t)l2::GetL2ChainId());

    return result;
}


// ============================================================================
// Task 10.7: l2_verifysupply RPC
// Requirements: 8.2, 9.5
// ============================================================================

UniValue l2_verifysupply(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "l2_verifysupply\n"
            "\nVerify the L2 supply invariant.\n"
            "\nThe supply invariant states that:\n"
            "- Total L2 supply == Total CAS burned on L1\n"
            "- Sum of all L2 balances == Total L2 supply\n"
            "\nResult:\n"
            "{\n"
            "  \"valid\": bool,               (boolean) Whether invariant holds\n"
            "  \"totalSupply\": \"x.xx\",      (string) Total L2 token supply\n"
            "  \"totalBurnedL1\": \"x.xx\",    (string) Total CAS burned on L1\n"
            "  \"totalMintedL2\": \"x.xx\",    (string) Total tokens minted on L2\n"
            "  \"sumOfBalances\": \"x.xx\",    (string) Sum of all L2 balances\n"
            "  \"supplyMatchesBurned\": bool, (boolean) totalSupply == totalBurnedL1\n"
            "  \"balancesMatchSupply\": bool, (boolean) sumOfBalances == totalSupply\n"
            "  \"discrepancy\": \"x.xx\",      (string) Discrepancy amount (if any)\n"
            "  \"message\": \"xxx\"            (string) Verification result message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_verifysupply", "")
            + HelpExampleRpc("l2_verifysupply", "")
        );

    EnsureL2Enabled();

    l2::BurnRegistry& registry = GetBurnRegistry();
    CAmount totalBurned = registry.GetTotalBurned();

    UniValue result(UniValue::VOBJ);
    
    CAmount totalSupply = totalBurned;
    CAmount totalMinted = totalBurned;
    CAmount sumOfBalances = 0;
    bool invariantValid = true;
    
    if (l2::IsL2TokenMinterInitialized()) {
        l2::L2TokenMinter& minter = l2::GetL2TokenMinter();
        totalSupply = minter.GetTotalSupply();
        totalMinted = minter.GetTotalMintedL2();
        invariantValid = minter.VerifySupplyInvariant();
        
        // Calculate sum of balances from mint events
        std::vector<l2::MintEvent> events = minter.GetMintEvents();
        std::set<uint160> recipients;
        for (const auto& event : events) {
            recipients.insert(event.recipient);
        }
        for (const uint160& addr : recipients) {
            sumOfBalances += minter.GetBalance(addr);
        }
    }

    bool supplyMatchesBurned = (totalSupply == totalBurned);
    bool balancesMatchSupply = (sumOfBalances == totalSupply);
    
    CAmount discrepancy = 0;
    if (!supplyMatchesBurned) {
        discrepancy = totalSupply > totalBurned ? totalSupply - totalBurned : totalBurned - totalSupply;
    } else if (!balancesMatchSupply) {
        discrepancy = sumOfBalances > totalSupply ? sumOfBalances - totalSupply : totalSupply - sumOfBalances;
    }

    result.pushKV("valid", invariantValid && supplyMatchesBurned && balancesMatchSupply);
    result.pushKV("totalSupply", ValueFromAmount(totalSupply));
    result.pushKV("totalBurnedL1", ValueFromAmount(totalBurned));
    result.pushKV("totalMintedL2", ValueFromAmount(totalMinted));
    result.pushKV("sumOfBalances", ValueFromAmount(sumOfBalances));
    result.pushKV("supplyMatchesBurned", supplyMatchesBurned);
    result.pushKV("balancesMatchSupply", balancesMatchSupply);
    result.pushKV("discrepancy", ValueFromAmount(discrepancy));
    
    if (invariantValid && supplyMatchesBurned && balancesMatchSupply) {
        result.pushKV("message", "Supply invariant verified successfully");
    } else {
        std::string errorMsg = "Supply invariant VIOLATED: ";
        if (!supplyMatchesBurned) {
            errorMsg += strprintf("totalSupply (%s) != totalBurnedL1 (%s). ", 
                                  FormatMoney(totalSupply), FormatMoney(totalBurned));
        }
        if (!balancesMatchSupply) {
            errorMsg += strprintf("sumOfBalances (%s) != totalSupply (%s). ",
                                  FormatMoney(sumOfBalances), FormatMoney(totalSupply));
        }
        result.pushKV("message", errorMsg);
    }

    return result;
}


// ============================================================================
// Task 10.8: l2_getburnsforaddress RPC
// Requirements: 9.1
// ============================================================================

UniValue l2_getburnsforaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "l2_getburnsforaddress \"address\"\n"
            "\nGet all burns for a specific L2 address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) L2 address (hex or base58)\n"
            "\nResult:\n"
            "{\n"
            "  \"address\": \"xxx\",           (string) L2 address\n"
            "  \"count\": n,                  (numeric) Number of burns\n"
            "  \"totalBurned\": \"x.xx\",      (string) Total amount burned for this address\n"
            "  \"burns\": [                   (array) Burn records\n"
            "    {\n"
            "      \"l1TxHash\": \"xxx\",       (string) L1 burn transaction hash\n"
            "      \"l2MintTxHash\": \"xxx\",   (string) L2 mint transaction hash\n"
            "      \"l2MintBlock\": n,         (numeric) L2 block where minted\n"
            "      \"amount\": \"x.xx\",        (string) Amount burned/minted\n"
            "      \"timestamp\": n,           (numeric) Processing timestamp\n"
            "      \"l1BlockNumber\": n,       (numeric) L1 block number\n"
            "      \"l1BlockHash\": \"xxx\"     (string) L1 block hash\n"
            "    },\n"
            "    ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("l2_getburnsforaddress", "\"0xa1b2c3...\"")
            + HelpExampleRpc("l2_getburnsforaddress", "\"0xa1b2c3...\"")
        );

    EnsureL2Enabled();

    std::string addressStr = request.params[0].get_str();
    uint160 address = ParseL2Address(addressStr);

    l2::BurnRegistry& registry = GetBurnRegistry();
    std::vector<l2::BurnRecord> records = registry.GetBurnsForAddress(address);

    CAmount totalBurned = 0;
    UniValue burnsArray(UniValue::VARR);

    for (const auto& record : records) {
        UniValue burnObj(UniValue::VOBJ);
        burnObj.pushKV("l1TxHash", record.l1TxHash.GetHex());
        burnObj.pushKV("l2MintTxHash", record.l2MintTxHash.GetHex());
        burnObj.pushKV("l2MintBlock", (int64_t)record.l2MintBlock);
        burnObj.pushKV("amount", ValueFromAmount(record.amount));
        burnObj.pushKV("timestamp", (int64_t)record.timestamp);
        burnObj.pushKV("l1BlockNumber", (int64_t)record.l1BlockNumber);
        burnObj.pushKV("l1BlockHash", record.l1BlockHash.GetHex());
        burnsArray.push_back(burnObj);
        totalBurned += record.amount;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", "0x" + address.GetHex());
    result.pushKV("count", (int64_t)records.size());
    result.pushKV("totalBurned", ValueFromAmount(totalBurned));
    result.pushKV("burns", burnsArray);

    return result;
}


// ============================================================================
// Task 10.9: RPC Command Registration
// Requirements: 9.1-9.6
// ============================================================================

static const CRPCCommand commands[] =
{
    //  category              name                      actor (function)         argNames
    //  --------------------- ------------------------- ------------------------ ----------
    
    // Burn-and-Mint RPC Commands (Requirements: 1.5, 1.6, 5.5, 8.2, 9.1-9.6)
    { "l2burn",               "l2_createburntx",        &l2_createburntx,        {"amount", "l2_recipient", "change_address"} },
    { "l2burn",               "l2_sendburntx",          &l2_sendburntx,          {"hex"} },
    { "l2burn",               "l2_getburnstatus",       &l2_getburnstatus,       {"l1txhash"} },
    { "l2burn",               "l2_getpendingburns",     &l2_getpendingburns,     {} },
    { "l2burn",               "l2_getminthistory",      &l2_getminthistory,      {"from_block", "to_block"} },
    { "l2burn",               "l2_gettotalsupply",      &l2_gettotalsupply,      {} },
    { "l2burn",               "l2_verifysupply",        &l2_verifysupply,        {} },
    { "l2burn",               "l2_getburnsforaddress",  &l2_getburnsforaddress,  {"address"} },
};

void RegisterL2BurnRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

