Cascoin Core version 3.2.1.1 is now available.

This is a hotfix release that improves logging behavior to provide a cleaner user experience.

## Logging Improvements

### Reduced Log Spam

- **Removed GetEffectivePowTypeForHashing spam**: Eliminated verbose logging from the `GetEffectivePowTypeForHashing` function that was cluttering logs during normal operation.

- **Converted development logs to debug-only**: Many technical development logs have been converted from always-visible (`LogPrintf`) to debug-only (`LogPrint`) with appropriate categories:
  - MinotaurX algorithm logs now use `-debug=minotaurx`
  - Hive mining logs now use `-debug=hive`
  - Network connection logs now use `-debug=net`
  - RPC authentication logs now use `-debug=rpc`
  - Qt wallet logs now use `-debug=qt`

### Debug Categories Available

Users can now control logging verbosity with these debug categories:
- `hive` - Hive mining and bee-related operations
- `minotaurx` - MinotaurX algorithm and difficulty adjustment
- `net` - Network connections, timeouts, and UPnP
- `rpc` - RPC server operations
- `qt` - Qt GUI operations
- `mempool` - Memory pool operations
- `db` - Database operations
- And many others from Bitcoin Core

### Usage Examples

```bash
# Normal operation - only important messages
./cascoind

# Debug specific areas
./cascoind -debug=hive -debug=net

# All debug information
./cascoind -debug=all

# All except specific categories
./cascoind -debug=all -nodebug=qt
```

## Benefits

- **Cleaner default logs**: Users see only important operational messages during normal use
- **Better debugging**: Developers can enable specific debug categories when troubleshooting
- **Bitcoin Core compatibility**: Follows the same logging patterns as Bitcoin Core
- **Improved performance**: Less I/O overhead when debug logging is disabled

## Upgrade Notes

This is a recommended upgrade for all users who want cleaner log output. No database changes or other breaking changes are included.
