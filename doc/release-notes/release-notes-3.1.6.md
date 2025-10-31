## Cascoin Core v3.1.6 – Mice NFT System Stabilization and Critical Fixes

### Overview
- **Critical bug fix release**: Resolves major issues preventing Mice NFT functionality
- **NFT transaction validation**: Fixed parser bugs causing wallet load errors
- **Network compatibility**: NFT transactions now propagate and confirm correctly
- **GUI stability**: Eliminated flickering and disappearing NFT entries
- **Memory optimization**: Removed aggressive limits that broke large-scale operations

### Highlights
- **Fixed Wallet Load Error**: Resolved "Error reading wallet.dat" caused by invalid Mice NFT transactions
- **NFT Transaction Broadcasting**: Mice NFT transactions now successfully broadcast and mine
- **Parser Overhaul**: Corrected OP_RETURN data parsing for CASTOK/CASXFR format
- **Large Labyrinth Support**: Removed 1,000 mice limit, now supports up to 1 million mice per labyrinth
- **GUI Stability**: Fixed flickering NFT entries and incorrect labyrinth display
- **Build System**: Resolved lrelease path issues for successful compilation

### Critical Bug Fixes

#### Wallet Load Failure - "Error reading wallet.dat"
- **Severity**: CRITICAL - Prevented wallet from loading with Mice NFT transactions
- **Symptoms**: 
  - "Error reading wallet.dat! All keys read correctly, but transaction data or address book entries might be missing or incorrect."
  - Mice NFT transactions marked as invalid during wallet load
  - Warning: "Wallet contains invalid transaction dcb067... (validation: invalid-bee-nft-token)"
- **Root Causes**:
  1. **OP_RETURN Parser Bug**: NFT data read from wrong byte offset (offset 7 instead of 9)
  2. **Bee Index Limit**: Hard limit of 1,000 mice rejected labyrinths with 200,000 mice
  3. **Magic Byte Detection**: Validation function used wrong offset (1-7 instead of 2-8)
- **Solution**:
  - Corrected OP_RETURN format parsing: `OP_RETURN(0x6a) + PUSH(6) + "CASTOK" + OP_PUSHDATA1(0x4c) + length + data`
  - Parser now correctly reads data starting at offset 10 (after OP_PUSHDATA1 and length byte)
  - Increased bee index limit from 1,000 to 1,000,000 for future-proofing
  - Updated validation function to match parser format (offset 2-8 for magic bytes)
- **Impact**: All Mice NFT transactions now load correctly without wallet errors

#### NFT Transaction Broadcasting Failure
- **Severity**: CRITICAL - Mice NFT transactions stuck as "0/unconfirmed, not in memory pool"
- **Symptoms**:
  - Transactions created successfully but never broadcast to network
  - "error code: -26, scriptpubkey" rejection from mempool
  - Transactions marked as "abandoned" or stuck unconfirmed indefinitely
- **Root Cause**: Policy check rejected CASTOK/CASXFR transactions as "non-standard"
  - Only `OP_RETURN + OP_BEE` was whitelisted as standard
  - Mice NFT format `OP_RETURN + "CASTOK"` was treated as non-standard
- **Solution**: Extended `IsStandardTx()` policy to accept CASTOK/CASXFR magic bytes
  - Added explicit check for Mice NFT transactions (CASTOK = tokenize, CASXFR = transfer)
  - Transactions now pass mempool validation and broadcast to network
- **Impact**: Mice NFT transactions successfully propagate and can be mined

#### GUI Flickering - Disappearing NFT Entries
- **Severity**: HIGH - NFTs appeared and disappeared randomly in GUI
- **Symptoms**:
  - NFT entries briefly visible then vanish
  - Empty NFT list after periodic refresh
  - Inconsistent display between refreshes
- **Root Cause**: `updateBeeNFTList()` called `beginResetModel()` + `clear()` + `endResetModel()` before new data loaded
  - Race condition: periodic scan clears list, new data arrives later
  - User sees empty list between clear and data reload
- **Solution**: Removed premature list clearing
  - List only updates when new data arrives via `updateBeeNFTListWithData()`
  - NFTs remain visible until fresh data replaces them (no flickering)
- **Impact**: Stable, flicker-free NFT display

#### Incorrect NFT Display - Showing Labyrinths Instead of NFTs
- **Severity**: HIGH - GUI showed all labyrinths as "NFTs" even when not tokenized
- **Symptoms**:
  - 8 "NFT" entries displayed immediately after creating 8 labyrinths
  - No actual tokenization performed, but GUI showed NFTs anyway
- **Root Cause**: `updateTableModelWithRealData()` filtered for `!alreadyTokenized` (available mice) instead of tokenized ones
  - Logic inverted: showed labyrinths with available mice, not actual NFTs
- **Solution**: Corrected filter to show only `alreadyTokenized == true` mice
  - Each tokenized mouse displayed as individual NFT entry
  - Labyrinths without tokenized mice correctly show 0 NFTs
- **Impact**: NFT list accurately reflects actual tokenized mice only

### Technical Improvements

#### NFT Parser Rewrite
**Previous Implementation (Broken)**:
```cpp
// Wrong offsets - read length from position 7 ('K' in "CASTOK")
std::vector<unsigned char> magicBytes(scriptPubKey.begin() + 1, scriptPubKey.begin() + 7);
unsigned char dataLen = scriptPubKey[7];
std::vector<unsigned char> rawData(scriptPubKey.begin() + 8, scriptPubKey.begin() + 8 + dataLen);
```

**New Implementation (Fixed)**:
```cpp
// Correct OP_RETURN format: OP_RETURN(0x6a) + PUSH(6) + "CASTOK" + OP_PUSHDATA1(0x4c) + length + data
// Check magic bytes at offset 2-8
std::vector<unsigned char> magicBytes(scriptPubKey.begin() + 2, scriptPubKey.begin() + 8);
// Read length byte after OP_PUSHDATA1 (offset 9)
unsigned char dataLen = scriptPubKey[9];
// Extract data starting at offset 10
std::vector<unsigned char> rawData(scriptPubKey.begin() + 10, scriptPubKey.begin() + 10 + dataLen);
```

**Parsing Results Before/After**:
- **Before**: `beeIndex=51491, maturity=3117219840, expiry=3203203074` (garbage data)
- **After**: `beeIndex=0, maturity=178637, expiry=704237` (correct values)

#### Policy Enhancement for NFT Transactions
```cpp
// Added to IsStandardTx() in policy.cpp
// Special handling for Mice NFT transactions (CASTOK = tokenize, CASXFR = transfer)
if (txout.scriptPubKey.size() >= 8 && 
    txout.scriptPubKey[0] == OP_RETURN &&
    txout.scriptPubKey[1] == 0x06) {  // PUSH 6 bytes
    std::vector<unsigned char> magic(txout.scriptPubKey.begin() + 2, txout.scriptPubKey.begin() + 8);
    std::vector<unsigned char> castok = {'C', 'A', 'S', 'T', 'O', 'K'};
    std::vector<unsigned char> casxfr = {'C', 'A', 'S', 'X', 'F', 'R'};
    if (magic == castok || magic == casxfr) {
        // Accept Mice NFT transactions as standard
        return true;
    }
}
```

#### Bee Index Limit Adjustment
- **Old Limit**: 1,000 mice per labyrinth (arbitrary low cap)
- **New Limit**: 1,000,000 mice per labyrinth (future-proof)
- **Rationale**: Users creating 200,000-mouse labyrinths hit hard limit
- **Impact**: Large-scale labyrinth operations now fully supported

### Additional Bug Fixes

#### Build System Issues
- **Fixed**: Build failing at `qt/locale/bitcoin_de.qm` generation
- **Root Cause**: Hardcoded `LRELEASE=/usr/bin/lrelease` path incorrect for some systems
- **Solution**: Build now accepts `LRELEASE=/usr/local/bin/lrelease` override
- **Updated**: `doc/build-linux.md` with proper lrelease path detection

#### Excessive Debug Logging
- **Fixed**: Thousands of PoW validation log entries causing slow startup
- **Removed**: Verbose `GetPoWHash` and `CheckProofOfWork` logs
- **Solution**: Logging now gated behind `-debug=minotaurx` flag
- **Impact**: Cleaner logs and faster startup times

#### GUI Warning Messages
- **Fixed**: "Unknown property box-shadow" warnings
- **Fixed**: "Unknown property content/transform" warnings  
- **Root Cause**: CSS properties not supported by Qt stylesheet engine
- **Solution**: Removed unsupported CSS pseudo-element styling
- **Impact**: Clean startup without CSS warnings

#### BCT Update Log Spam
- **Fixed**: "BCT update already in progress, skipping duplicate request" spam
- **Root Cause**: Concurrent updates discarded instead of queued
- **Solution**: Pending-flag system queues updates instead of dropping them
- **Impact**: No more warning spam, reliable update completion

#### BeeNFT Selection Connection Error
- **Fixed**: "Cannot connect (nullptr)::selectionChanged"
- **Root Cause**: Connection attempt before model assignment
- **Solution**: Connection established after `setModel()` call
- **Impact**: No more connection errors on startup

#### Wallet Lock Retry Mechanism
- **Fixed**: "Could not acquire wallet lock for BCT update after 3 retries"
- **Enhancement**: Increased retries from 3 to 10 with delays
- **Impact**: More resilient concurrent wallet operations

#### RPC Terminology Consistency (fromport)
- **Fixed**: Inconsistent use of "bee_count" in RPC responses
- **Changes**: Renamed `bee_count` to `mice_count` in RPC code for consistency
- **Files Updated**: 
  - `src/rpc/rawtransaction.cpp`
  - `src/rpc/client.cpp`
  - `src/wallet/rpcwallet.cpp`
- **Impact**: RPC output now uses consistent "mice" terminology matching GUI

### Memory Optimization Removals

Previous memory "optimizations" were too aggressive and broke functionality:

**Removed Artificial Limits**:
- ~~`MAX_COINBASE_PROCESS = 5000`~~ - Stopped reward calculation prematurely
- ~~`MAX_BCT_PROCESS = 10000`~~ - Limited BCT scanning arbitrarily
- ~~`MAX_BCTS_RESULT = 1000`~~ - Capped results artificially
- ~~`hiveCoinbaseMap.clear()` when size > 500~~ - Deleted needed data mid-processing

**New Approach**:
- Pre-filter relevant transactions before processing (memory-bounded naturally)
- Only accumulate data for owned BCTs (significantly smaller dataset)
- Let natural data structures manage memory without artificial caps
- Proper algorithmic optimization instead of hard limits

### User Impact

**Before v3.1.6**:
- ❌ Wallet fails to load with "Error reading wallet.dat"
- ❌ Mice NFT transactions stuck unconfirmed forever
- ❌ GUI shows all labyrinths as "NFTs" incorrectly
- ❌ NFT entries flicker and disappear randomly
- ❌ Cannot tokenize mice from large labyrinths (>1,000 mice)
- ❌ Build fails on many systems due to lrelease path

**After v3.1.6**:
- ✅ Wallet loads cleanly with Mice NFT transactions
- ✅ NFT transactions broadcast and confirm successfully
- ✅ GUI accurately shows only actual tokenized NFTs
- ✅ Stable NFT display without flickering
- ✅ Full support for labyrinths with up to 1M mice
- ✅ Builds successfully on all systems

### Network Compatibility

**Important**: Mice NFT transactions can only be mined by nodes running v3.1.6+

- **Nodes < v3.1.6**: Reject CASTOK/CASXFR transactions as "non-standard"
- **Nodes ≥ v3.1.6**: Accept and mine Mice NFT transactions normally

**Recommendation**: Upgrade all mining nodes to v3.1.6 for full NFT system support

### Changelog

- [Critical][NFT] Fixed wallet load failure with Mice NFT transactions
- [Critical][NFT] Fixed NFT transaction broadcasting and mempool acceptance
- [Critical][Parser] Corrected OP_RETURN data offset parsing (7→10)
- [Critical][Parser] Fixed magic byte detection offset (1-7 → 2-8)
- [Critical][Validation] Increased bee index limit from 1,000 to 1,000,000
- [Critical][Policy] Added CASTOK/CASXFR to standard transaction whitelist
- [Fix][GUI] Eliminated NFT list flickering during periodic updates
- [Fix][GUI] Corrected NFT display to show only tokenized mice
- [Fix][Build] Resolved lrelease path issues for successful compilation
- [Fix][Logging] Gated PoW debug logs behind -debug=minotaurx flag
- [Fix][GUI] Removed unsupported CSS properties causing warnings
- [Fix][BCT] Implemented pending-flag system for concurrent update handling
- [Fix][GUI] Fixed BeeNFT selection connection timing issue
- [Enhancement][Wallet] Increased wallet lock retry from 3 to 10 attempts
- [Optimization][Memory] Removed aggressive artificial limits breaking large operations
- [Optimization][Memory] Implemented smart pre-filtering for natural memory bounds
- [Maintenance] Updated NFT validation with detailed error logging
- [Cleanup][RPC] Renamed bee_count to mice_count for terminology consistency (fromport)

---

### Upgrade Notes

**Immediate Benefits**:
- Existing stuck Mice NFT transactions will be rebroadcast automatically
- Wallet will load without errors
- NFT GUI will display correctly

**Mining Consideration**:
- Miners should upgrade to v3.1.6 to include Mice NFT transactions in blocks
- Old miners will skip these transactions (they remain unconfirmed until new miner processes them)

**No Data Migration Required**:
- All existing wallet data remains compatible
- Previously stuck transactions will automatically retry broadcasting
- No manual intervention needed

### Developer Notes

**Modified Files**:
- `src/beenft.cpp`: Parser rewrite, validation fixes, bee index limit increase
- `src/policy/policy.cpp`: NFT transaction standard policy addition
- `src/wallet/walletdb.cpp`: Enhanced error logging for transaction load failures
- `src/qt/beenfttablemodel.cpp`: Removed premature list clearing causing flicker
- `src/qt/beenftpage.cpp`: Corrected NFT filtering logic
- `src/qt/bitcoin.cpp`: Removed unsupported CSS properties
- `src/qt/beenftpage.cpp`: Fixed selection connection timing
- `src/qt/walletmodel.cpp`: Increased wallet lock retry count
- `src/qt/hivetablemodel.cpp`: Implemented pending-update coalescing
- `src/wallet/wallet.cpp`: Removed artificial memory limits
- `src/primitives/block.cpp`: Gated PoW logging behind debug flag
- `src/pow.cpp`: Gated proof-of-work logging behind debug flag
- `src/rpc/rawtransaction.cpp`: Terminology consistency (fromport)
- `src/rpc/client.cpp`: Terminology consistency (fromport)
- `src/wallet/rpcwallet.cpp`: Terminology consistency (fromport)
- `doc/build-linux.md`: Dynamic lrelease path detection

**Testing Recommendations**:
- Test Mice NFT tokenization with large labyrinths (>1,000 mice)
- Verify wallet loads without errors after NFT transactions
- Confirm NFT transactions broadcast and receive confirmations
- Check NFT GUI displays only tokenized mice accurately
- Validate no flickering occurs during periodic refreshes

For the complete list of changes, see the [commit history](https://github.com/casraw/cascoin/commits/master).

### Installation Notes
- **Standard Update**: Replace existing binaries with v3.1.6
- **Critical Upgrade**: Strongly recommended for all users with Mice NFT transactions
- **Miner Priority**: Mining nodes should upgrade immediately for NFT transaction support
- **Clean Startup**: Logs will be significantly cleaner without PoW debug spam
- **Data Preservation**: All existing wallet and labyrinth data remains intact

---

### Credits

Thanks to everyone who directly contributed to this release:

- Alexander Bergmann
- fromport

