# Status Update - CVM Debugging

**Date**: November 3, 2025  
**Time**: 13:51 UTC  
**Status**: Still Debugging

---

## What We Know

### ✅ Working:
1. CVM transaction creation - perfect OP_RETURN format
2. Transaction broadcasting - reaches mempool
3. Mining - TXs are mined into blocks
4. OP_RETURN type - correct ("nulldata")

### ❌ Not Working:
1. Block processing - block.vtx still shows only 1 TX (coinbase)
2. Stats remain at 0
3. No CVM transactions are being processed

---

## Fixes Attempted

### Fix #1: OP_RETURN Format (SOLVED ✅)
- **Problem**: OpType was serialized with length prefix (`01 04` instead of `04`)
- **Solution**: Use `std::vector<unsigned char>` which automatically adds length when pushed
- **Result**: OP_RETURN type changed from "nonstandard" to "nulldata" ✅

### Fix #2: Always Read From Disk
- **Problem**: `pblock` parameter in `ConnectTip` might be witness-stripped
- **Solution**: Always call `ReadBlockFromDisk` to get full block
- **Result**: Still shows `block.vtx.size() = 1` ❌

---

## Current Investigation

**Question**: Why does `ReadBlockFromDisk` only load coinbase TX?

Possible causes:
1. Block on disk only has coinbase stored?
2. Deserialization issue in `ReadBlockFromDisk`?
3. Witness-stripping happening during read?
4. TXs stored separately from block?

---

## Next Steps

Need to check:
1. What `ReadBlockFromDisk` actually returns
2. How blocks are stored on disk (with/without TXs)
3. If TXs need to be loaded separately
4. Block serialization format

---

**The code is ready, just need to understand Bitcoin Core's block storage!**

