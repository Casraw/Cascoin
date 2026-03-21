// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <beenft.h>
#include <validation.h>
#include <chainparams.h>
#include <script/standard.h>
#include <utilstrencodings.h>
#include <consensus/validation.h>
#include <base58.h>
#include <util.h>

#include <algorithm>
#include <mutex>

// Cascoin: Static member definitions for NFTMemoryManager
std::map<uint256, std::shared_ptr<std::vector<unsigned char>>> NFTMemoryManager::nftDataPool;
std::mutex NFTMemoryManager::poolMutex;
size_t NFTMemoryManager::currentPoolSize = 0;

// Validate a bee NFT token transaction
bool IsValidBeeNFTTokenTransaction(const CTransaction& tx, std::string& error) {
    // Must have at least one OP_RETURN output with CASTOK magic bytes
    bool foundBeeToken = false;
    
    for (const CTxOut& txout : tx.vout) {
        // Check for NFT token magic bytes "CASTOK"
        // Format: OP_RETURN (0x6a) + PUSH(6) + "CASTOK" + OP_PUSHDATA1 (0x4c) + length + data
        if (txout.scriptPubKey.size() >= 10 && 
            txout.scriptPubKey[0] == OP_RETURN &&
            txout.scriptPubKey[1] == 0x06) {
            
            std::vector<unsigned char> magicBytes(txout.scriptPubKey.begin() + 2, txout.scriptPubKey.begin() + 8);
            std::vector<unsigned char> expectedMagic = {'C', 'A', 'S', 'T', 'O', 'K'};
            if (magicBytes == expectedMagic) {
                foundBeeToken = true;
                
                // Validate OP_RETURN data size
                if (txout.scriptPubKey.size() > BEE_NFT_MAX_DATA_SIZE + 10) {
                    error = "Bee NFT token data exceeds maximum size";
                    LogPrintf("NFT Validation Error (TX %s): %s\n", tx.GetHash().ToString(), error);
                    return false;
                }
            }
            
            // Parse and validate token data
            std::vector<BeeNFTToken> tokens;
            if (!ParseBeeNFTTokenTransaction(tx, tokens, error)) {
                LogPrintf("NFT Validation Error (TX %s): Parse failed - %s\n", tx.GetHash().ToString(), error);
                return false;
            }
            
            // Validate each token
            for (const BeeNFTToken& token : tokens) {
                // Check original BCT exists and is valid
                if (token.originalBCT.IsNull()) {
                    error = "Invalid original BCT hash";
                    LogPrintf("NFT Validation Error (TX %s): %s\n", tx.GetHash().ToString(), error);
                    return false;
                }
                
                // Check maturity and expiry heights make sense
                if (token.maturityHeight >= token.expiryHeight) {
                    error = strprintf("Invalid bee lifecycle: maturity (%d) >= expiry (%d)", token.maturityHeight, token.expiryHeight);
                    LogPrintf("NFT Validation Error (TX %s): %s\n", tx.GetHash().ToString(), error);
                    return false;
                }
                
                // Check bee index is reasonable (max 1 million bees per BCT to be future-proof)
                if (token.beeIndex >= 1000000) {
                    error = strprintf("Bee index too high: %d", token.beeIndex);
                    LogPrintf("NFT Validation Error (TX %s): %s\n", tx.GetHash().ToString(), error);
                    return false;
                }
                
                // Validate owner address format
                if (token.currentOwner.empty()) {
                    error = "Invalid owner address (empty)";
                    LogPrintf("NFT Validation Error (TX %s): %s\n", tx.GetHash().ToString(), error);
                    return false;
                }
                CTxDestination dest = DecodeDestination(token.currentOwner);
                if (!IsValidDestination(dest)) {
                    error = strprintf("Invalid owner address: %s", token.currentOwner);
                    LogPrintf("NFT Validation Error (TX %s): %s\n", tx.GetHash().ToString(), error);
                    return false;
                }
            }
            break;
        }
    }
    
    if (!foundBeeToken) {
        error = "No bee NFT token marker found";
        return false;
    }
    
    return true;
}

// Validate a bee NFT transfer transaction
bool IsValidBeeNFTTransferTransaction(const CTransaction& tx, std::string& error) {
    // Must have at least one OP_RETURN output with OP_BEE_TRANSFER
    bool foundBeeTransfer = false;
    
    for (const CTxOut& txout : tx.vout) {
        // Check for NFT transfer magic bytes "BEEXFR" instead of opcode
        if (txout.scriptPubKey.size() >= 8 && 
            txout.scriptPubKey[0] == OP_RETURN) {
            std::vector<unsigned char> transferMagic(txout.scriptPubKey.begin() + 1, txout.scriptPubKey.begin() + 7);
            std::vector<unsigned char> expectedTransfer = {'C', 'A', 'S', 'X', 'F', 'R'};
            if (transferMagic == expectedTransfer) {
                foundBeeTransfer = true;
                
                // Validate OP_RETURN data size (magic bytes + data)
                if (txout.scriptPubKey.size() > BEE_NFT_MAX_DATA_SIZE + 7) {
                    error = "Bee NFT transfer data exceeds maximum size";
                    return false;
                }
            }
            
            // Parse and validate transfer data
            std::vector<BeeNFTTransfer> transfers;
            if (!ParseBeeNFTTransferTransaction(tx, transfers, error)) {
                return false;
            }
            
            // Validate each transfer
            for (const BeeNFTTransfer& transfer : transfers) {
                // Check bee NFT ID is valid
                if (transfer.beeNFTId.IsNull()) {
                    error = "Invalid bee NFT ID";
                    return false;
                }
                
                // Validate address formats
                if (transfer.fromOwner.empty()) {
                    error = "Invalid from owner address";
                    return false;
                }
                CTxDestination fromDest = DecodeDestination(transfer.fromOwner);
                if (!IsValidDestination(fromDest)) {
                    error = "Invalid from owner address";
                    return false;
                }
                
                if (transfer.toOwner.empty()) {
                    error = "Invalid to owner address";
                    return false;
                }
                CTxDestination toDest = DecodeDestination(transfer.toOwner);
                if (!IsValidDestination(toDest)) {
                    error = "Invalid to owner address";
                    return false;
                }
                
                // Check transfer is not to self
                if (transfer.fromOwner == transfer.toOwner) {
                    error = "Cannot transfer bee NFT to same address";
                    return false;
                }
            }
            break;
        }
    }
    
    if (!foundBeeTransfer) {
        error = "No bee NFT transfer marker found";
        return false;
    }
    
    return true;
}

// Parse bee NFT token transaction data
bool ParseBeeNFTTokenTransaction(const CTransaction& tx, std::vector<BeeNFTToken>& tokens, std::string& error) {
    tokens.clear();
    
    for (const CTxOut& txout : tx.vout) {
        // Check for NFT token magic bytes "CASTOK"
        // Format: OP_RETURN (0x6a) + PUSH(6) + "CASTOK" + OP_PUSHDATA1 (0x4c) + length + data
        if (txout.scriptPubKey.size() >= 10 && 
            txout.scriptPubKey[0] == OP_RETURN &&
            txout.scriptPubKey[1] == 0x06) {
            
            std::vector<unsigned char> magicBytes(txout.scriptPubKey.begin() + 2, txout.scriptPubKey.begin() + 8);
            std::vector<unsigned char> expectedMagic = {'C', 'A', 'S', 'T', 'O', 'K'};
            if (magicBytes == expectedMagic) {
                // Check for OP_PUSHDATA1 (0x4c)
                if (txout.scriptPubKey[8] != 0x4c) {
                    error = "Expected OP_PUSHDATA1 after CASTOK magic bytes";
                    return false;
                }
                
                // Get data length (byte after OP_PUSHDATA1)
                unsigned char dataLen = txout.scriptPubKey[9];
                if (static_cast<size_t>(dataLen + 10) > txout.scriptPubKey.size()) {
                    error = "Invalid bee NFT token data length";
                    return false;
                }
                
                // Extract raw data (starts at offset 10)
                std::vector<unsigned char> rawData(
                    txout.scriptPubKey.begin() + 10,
                    txout.scriptPubKey.begin() + 10 + dataLen
                );
            
            // Parse token data (simplified format for MVP)
            if (rawData.size() < 32 + 4 + 4 + 4 + 4) { // BCT hash + 4 x uint32
                error = "Bee NFT token data too short for required fields";
                return false;
            }
            
            BeeNFTToken token;
            
            // Parse original BCT hash (32 bytes)
            std::copy(rawData.begin(), rawData.begin() + 32, token.originalBCT.begin());
            
            // Parse uint32 fields (4 bytes each)
            token.beeIndex = ReadLE32(&rawData[32]);
            token.maturityHeight = ReadLE32(&rawData[36]);
            token.expiryHeight = ReadLE32(&rawData[40]);
            token.tokenizedHeight = ReadLE32(&rawData[44]);
            
            LogPrintf("Parsed NFT Token: beeIndex=%u, maturity=%u, expiry=%u, tokenized=%u\n",
                      token.beeIndex, token.maturityHeight, token.expiryHeight, token.tokenizedHeight);
            
            // Parse owner address (remaining bytes as string)
            if (rawData.size() > 48) {
                token.currentOwner = std::string(rawData.begin() + 48, rawData.end());
            }
            
            tokens.push_back(token);
            break; // Only process first bee token output
            }
        }
    }
    
    return !tokens.empty();
}

// Parse bee NFT transfer transaction data
bool ParseBeeNFTTransferTransaction(const CTransaction& tx, std::vector<BeeNFTTransfer>& transfers, std::string& error) {
    transfers.clear();
    
    for (const CTxOut& txout : tx.vout) {
        // Check for NFT transfer magic bytes "BEEXFR" instead of opcode
        if (txout.scriptPubKey.size() >= 8 && 
            txout.scriptPubKey[0] == OP_RETURN) {
            std::vector<unsigned char> transferMagic(txout.scriptPubKey.begin() + 1, txout.scriptPubKey.begin() + 7);
            std::vector<unsigned char> expectedTransfer = {'C', 'A', 'S', 'X', 'F', 'R'};
            if (transferMagic == expectedTransfer) {
            
                // Extract data after OP_RETURN + magic bytes
                if (txout.scriptPubKey.size() < 9) {
                    error = "Bee NFT transfer data too short";
                    return false;
                }
            
                // Get data length (after magic bytes)
                unsigned char dataLen = txout.scriptPubKey[7];
                if (static_cast<size_t>(dataLen + 8) > txout.scriptPubKey.size()) {
                    error = "Invalid bee NFT transfer data length";
                    return false;
                }
                
                // Extract raw data (after magic bytes + length)
                std::vector<unsigned char> rawData(
                    txout.scriptPubKey.begin() + 8,
                    txout.scriptPubKey.begin() + 8 + dataLen
                );
            
                // Parse transfer data
                if (rawData.size() < 32 + 4 + 32 + 8) { // Bee NFT ID + height + tx ID + fee
                    error = "Bee NFT transfer data too short for required fields";
                    return false;
                }
                
                BeeNFTTransfer transfer;
                
                // Parse bee NFT ID (32 bytes)
                std::copy(rawData.begin(), rawData.begin() + 32, transfer.beeNFTId.begin());
                
                // Parse transfer height (4 bytes)
                transfer.transferHeight = ReadLE32(&rawData[32]);
                
                // Parse transfer tx ID (32 bytes)
                std::copy(rawData.begin() + 36, rawData.begin() + 68, transfer.transferTxId.begin());
                
                // Parse transfer fee (8 bytes)
                transfer.transferFee = ReadLE64(&rawData[68]);
                
                // Parse addresses (remaining bytes, split by null terminator)
                if (rawData.size() > 76) {
                    std::string addresses(rawData.begin() + 76, rawData.end());
                    size_t nullPos = addresses.find('\0');
                    if (nullPos != std::string::npos) {
                        transfer.fromOwner = addresses.substr(0, nullPos);
                        transfer.toOwner = addresses.substr(nullPos + 1);
                    } else {
                        error = "Invalid address format in transfer data";
                        return false;
                    }
                }
                
                transfers.push_back(transfer);
                break; // Only process first bee transfer output
            }
        }
    }
    
    return !transfers.empty();
}

// Serialize bee NFT token for transaction
std::vector<unsigned char> SerializeBeeNFTToken(const BeeNFTToken& token) {
    std::vector<unsigned char> data;
    data.reserve(80); // Reserve space for efficiency
    
    // Add original BCT hash (32 bytes)
    data.insert(data.end(), token.originalBCT.begin(), token.originalBCT.end());
    
    // Add uint32 fields (4 bytes each)
    unsigned char beeIndexBytes[4];
    WriteLE32(beeIndexBytes, token.beeIndex);
    data.insert(data.end(), beeIndexBytes, beeIndexBytes + 4);
    
    unsigned char maturityBytes[4];
    WriteLE32(maturityBytes, token.maturityHeight);
    data.insert(data.end(), maturityBytes, maturityBytes + 4);
    
    unsigned char expiryBytes[4];
    WriteLE32(expiryBytes, token.expiryHeight);
    data.insert(data.end(), expiryBytes, expiryBytes + 4);
    
    unsigned char tokenizedBytes[4];
    WriteLE32(tokenizedBytes, token.tokenizedHeight);
    data.insert(data.end(), tokenizedBytes, tokenizedBytes + 4);
    
    // Add owner address
    data.insert(data.end(), token.currentOwner.begin(), token.currentOwner.end());
    
    return data;
}

// Deserialize bee NFT token from data
bool DeserializeBeeNFTToken(const std::vector<unsigned char>& data, BeeNFTToken& token) {
    if (data.size() < 48) { // Minimum size for required fields
        return false;
    }
    
    // Parse original BCT hash
    std::copy(data.begin(), data.begin() + 32, token.originalBCT.begin());
    
    // Parse uint32 fields
    token.beeIndex = ReadLE32(&data[32]);
    token.maturityHeight = ReadLE32(&data[36]);
    token.expiryHeight = ReadLE32(&data[40]);
    token.tokenizedHeight = ReadLE32(&data[44]);
    
    // Parse owner address
    if (data.size() > 48) {
        token.currentOwner = std::string(data.begin() + 48, data.end());
    }
    
    return true;
}

// Serialize bee NFT transfer for transaction
std::vector<unsigned char> SerializeBeeNFTTransfer(const BeeNFTTransfer& transfer) {
    std::vector<unsigned char> data;
    data.reserve(80); // Reserve space for efficiency
    
    // Add bee NFT ID (32 bytes)
    data.insert(data.end(), transfer.beeNFTId.begin(), transfer.beeNFTId.end());
    
    // Add transfer height (4 bytes)
    unsigned char heightBytes[4];
    WriteLE32(heightBytes, transfer.transferHeight);
    data.insert(data.end(), heightBytes, heightBytes + 4);
    
    // Add transfer tx ID (32 bytes)
    data.insert(data.end(), transfer.transferTxId.begin(), transfer.transferTxId.end());
    
    // Add transfer fee (8 bytes)
    unsigned char feeBytes[8];
    WriteLE64(feeBytes, transfer.transferFee);
    data.insert(data.end(), feeBytes, feeBytes + 8);
    
    // Add addresses with null separator
    data.insert(data.end(), transfer.fromOwner.begin(), transfer.fromOwner.end());
    data.push_back('\0');
    data.insert(data.end(), transfer.toOwner.begin(), transfer.toOwner.end());
    
    return data;
}

// Deserialize bee NFT transfer from data
bool DeserializeBeeNFTTransfer(const std::vector<unsigned char>& data, BeeNFTTransfer& transfer) {
    if (data.size() < 76) { // Minimum size for required fields
        return false;
    }
    
    // Parse bee NFT ID
    std::copy(data.begin(), data.begin() + 32, transfer.beeNFTId.begin());
    
    // Parse transfer height
    transfer.transferHeight = ReadLE32(&data[32]);
    
    // Parse transfer tx ID
    std::copy(data.begin() + 36, data.begin() + 68, transfer.transferTxId.begin());
    
    // Parse transfer fee
    transfer.transferFee = ReadLE64(&data[68]);
    
    // Parse addresses
    if (data.size() > 76) {
        std::string addresses(data.begin() + 76, data.end());
        size_t nullPos = addresses.find('\0');
        if (nullPos != std::string::npos) {
            transfer.fromOwner = addresses.substr(0, nullPos);
            transfer.toOwner = addresses.substr(nullPos + 1);
        } else {
            return false;
        }
    }
    
    return true;
}

// Create bee NFT token transaction script
CScript CreateBeeNFTTokenScript(const std::vector<BeeNFTToken>& tokens) {
    CScript script;
    
    if (tokens.empty()) {
        return script;
    }
    
    // For MVP, only support single token per transaction
    const BeeNFTToken& token = tokens[0];
    std::vector<unsigned char> tokenData = SerializeBeeNFTToken(token);
    
    // Soft Fork: Use magic bytes instead of new opcodes for old node compatibility
    std::vector<unsigned char> nftMagic = {'C', 'A', 'S', 'T', 'O', 'K'};  // "CASTOK"
    script << OP_RETURN << nftMagic << tokenData;
    
    return script;
}

// Create bee NFT transfer transaction script
CScript CreateBeeNFTTransferScript(const std::vector<BeeNFTTransfer>& transfers) {
    CScript script;
    
    if (transfers.empty()) {
        return script;
    }
    
    // For MVP, only support single transfer per transaction
    const BeeNFTTransfer& transfer = transfers[0];
    std::vector<unsigned char> transferData = SerializeBeeNFTTransfer(transfer);
    
    // Soft Fork: Use magic bytes instead of new opcodes for old node compatibility  
    std::vector<unsigned char> transferMagic = {'C', 'A', 'S', 'X', 'F', 'R'};  // "CASXFR"
    script << OP_RETURN << transferMagic << transferData;
    
    return script;
}

// Generic NFT script creation
std::vector<unsigned char> CreateGenericNFTScript(const std::vector<GenericNFT>& nfts) {
    std::vector<unsigned char> script;
    
    // Add magic bytes
    std::string magic = NFT_MAGIC_GENERIC;
    script.insert(script.end(), magic.begin(), magic.end());
    
    // Add number of NFTs
    script.push_back(nfts.size());
    
    for (const auto& nft : nfts) {
        // Add NFT name length and data
        script.push_back(nft.name.length());
        script.insert(script.end(), nft.name.begin(), nft.name.end());
        
        // Add description length and data
        script.push_back(nft.description.length());
        script.insert(script.end(), nft.description.begin(), nft.description.end());
        
        // Add metadata length and data
        script.push_back(nft.metadata.length());
        script.insert(script.end(), nft.metadata.begin(), nft.metadata.end());
        
        // Add amount (8 bytes)
        for (int i = 0; i < 8; i++) {
            script.push_back((nft.amount >> (i * 8)) & 0xFF);
        }
        
        // Add owner address length and data
        script.push_back(nft.owner_address.length());
        script.insert(script.end(), nft.owner_address.begin(), nft.owner_address.end());
    }
    
    return script;
}

std::vector<unsigned char> CreateGenericNFTTransferScript(const std::vector<GenericNFTTransfer>& transfers) {
    std::vector<unsigned char> script;
    
    // Add magic bytes
    std::string magic = NFT_MAGIC_GENERIC_TRANSFER;
    script.insert(script.end(), magic.begin(), magic.end());
    
    // Add number of transfers
    script.push_back(transfers.size());
    
    for (const auto& transfer : transfers) {
        // Add NFT TXID length and data
        script.push_back(transfer.nft_txid.length());
        script.insert(script.end(), transfer.nft_txid.begin(), transfer.nft_txid.end());
        
        // Add recipient address length and data
        script.push_back(transfer.recipient_address.length());
        script.insert(script.end(), transfer.recipient_address.begin(), transfer.recipient_address.end());
        
        // Add amount (8 bytes)
        for (int i = 0; i < 8; i++) {
            script.push_back((transfer.amount >> (i * 8)) & 0xFF);
        }
    }
    
    return script;
}

// Generic NFT validation
bool IsValidGenericNFTTransaction(const CTransaction& tx, std::string& error) {
    if (tx.vout.empty()) {
        error = "No outputs in transaction";
        return false;
    }
    
    // Check for OP_RETURN output with generic NFT magic bytes
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey[0] == OP_RETURN) {
            std::vector<unsigned char> data(output.scriptPubKey.begin() + 2, output.scriptPubKey.end());
            if (data.size() >= 6) {
                std::string magic(data.begin(), data.begin() + 6);
                if (magic == NFT_MAGIC_GENERIC) {
                    return true;
                }
            }
        }
    }
    
    error = "No valid generic NFT output found";
    return false;
}

bool ParseGenericNFTTransaction(const CTransaction& tx, std::vector<GenericNFT>& nfts, std::string& error) {
    nfts.clear();
    
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey[0] == OP_RETURN) {
            std::vector<unsigned char> data(output.scriptPubKey.begin() + 2, output.scriptPubKey.end());
            if (data.size() >= 6) {
                std::string magic(data.begin(), data.begin() + 6);
                if (magic == NFT_MAGIC_GENERIC) {
                    size_t pos = 6;
                    
                    if (pos >= data.size()) {
                        error = "Invalid generic NFT data: missing NFT count";
                        return false;
                    }
                    
                    int nftCount = data[pos++];
                    
                    for (int i = 0; i < nftCount; i++) {
                        if (pos >= data.size()) {
                            error = "Invalid generic NFT data: incomplete NFT";
                            return false;
                        }
                        
                        // Read name
                        int nameLen = data[pos++];
                        if (pos + nameLen > data.size()) {
                            error = "Invalid generic NFT data: name length error";
                            return false;
                        }
                        std::string name(data.begin() + pos, data.begin() + pos + nameLen);
                        pos += nameLen;
                        
                        // Read description
                        if (pos >= data.size()) {
                            error = "Invalid generic NFT data: missing description length";
                            return false;
                        }
                        int descLen = data[pos++];
                        if (pos + descLen > data.size()) {
                            error = "Invalid generic NFT data: description length error";
                            return false;
                        }
                        std::string description(data.begin() + pos, data.begin() + pos + descLen);
                        pos += descLen;
                        
                        // Read metadata
                        if (pos >= data.size()) {
                            error = "Invalid generic NFT data: missing metadata length";
                            return false;
                        }
                        int metaLen = data[pos++];
                        if (pos + metaLen > data.size()) {
                            error = "Invalid generic NFT data: metadata length error";
                            return false;
                        }
                        std::string metadata(data.begin() + pos, data.begin() + pos + metaLen);
                        pos += metaLen;
                        
                        // Read amount
                        if (pos + 8 > data.size()) {
                            error = "Invalid generic NFT data: amount length error";
                            return false;
                        }
                        int64_t amount = 0;
                        for (int j = 0; j < 8; j++) {
                            amount |= ((int64_t)data[pos + j] << (j * 8));
                        }
                        pos += 8;
                        
                        // Read owner address
                        if (pos >= data.size()) {
                            error = "Invalid generic NFT data: missing owner address length";
                            return false;
                        }
                        int ownerLen = data[pos++];
                        if (pos + ownerLen > data.size()) {
                            error = "Invalid generic NFT data: owner address length error";
                            return false;
                        }
                        std::string owner(data.begin() + pos, data.begin() + pos + ownerLen);
                        pos += ownerLen;
                        
                        nfts.emplace_back(name, description, metadata, amount, owner);
                    }
                    
                    return true;
                }
            }
        }
    }
    
    error = "No generic NFT data found";
    return false;
}

bool IsValidGenericNFTTransferTransaction(const CTransaction& tx, std::string& error) {
    if (tx.vout.empty()) {
        error = "No outputs in transaction";
        return false;
    }
    
    // Check for OP_RETURN output with generic NFT transfer magic bytes
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey[0] == OP_RETURN) {
            std::vector<unsigned char> data(output.scriptPubKey.begin() + 2, output.scriptPubKey.end());
            if (data.size() >= 6) {
                std::string magic(data.begin(), data.begin() + 6);
                if (magic == NFT_MAGIC_GENERIC_TRANSFER) {
                    return true;
                }
            }
        }
    }
    
    error = "No valid generic NFT transfer output found";
    return false;
}

bool ParseGenericNFTTransferTransaction(const CTransaction& tx, std::vector<GenericNFTTransfer>& transfers, std::string& error) {
    transfers.clear();
    
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey[0] == OP_RETURN) {
            std::vector<unsigned char> data(output.scriptPubKey.begin() + 2, output.scriptPubKey.end());
            if (data.size() >= 6) {
                std::string magic(data.begin(), data.begin() + 6);
                if (magic == NFT_MAGIC_GENERIC_TRANSFER) {
                    size_t pos = 6;
                    
                    if (pos >= data.size()) {
                        error = "Invalid generic NFT transfer data: missing transfer count";
                        return false;
                    }
                    
                    int transferCount = data[pos++];
                    
                    for (int i = 0; i < transferCount; i++) {
                        if (pos >= data.size()) {
                            error = "Invalid generic NFT transfer data: incomplete transfer";
                            return false;
                        }
                        
                        // Read NFT TXID
                        int txidLen = data[pos++];
                        if (pos + txidLen > data.size()) {
                            error = "Invalid generic NFT transfer data: TXID length error";
                            return false;
                        }
                        std::string nftTxid(data.begin() + pos, data.begin() + pos + txidLen);
                        pos += txidLen;
                        
                        // Read recipient address
                        if (pos >= data.size()) {
                            error = "Invalid generic NFT transfer data: missing recipient length";
                            return false;
                        }
                        int recipientLen = data[pos++];
                        if (pos + recipientLen > data.size()) {
                            error = "Invalid generic NFT transfer data: recipient length error";
                            return false;
                        }
                        std::string recipient(data.begin() + pos, data.begin() + pos + recipientLen);
                        pos += recipientLen;
                        
                        // Read amount
                        if (pos + 8 > data.size()) {
                            error = "Invalid generic NFT transfer data: amount length error";
                            return false;
                        }
                        int64_t amount = 0;
                        for (int j = 0; j < 8; j++) {
                            amount |= ((int64_t)data[pos + j] << (j * 8));
                        }
                        pos += 8;
                        
                        transfers.emplace_back(nftTxid, recipient, amount);
                    }
                    
                    return true;
                }
            }
        }
    }
    
    error = "No generic NFT transfer data found";
    return false;
}
