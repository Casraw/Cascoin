Cascoin Core version 3.2.1.2 is now available.

This is a hotfix release that significantly improves the logging system to provide a cleaner user experience while maintaining comprehensive debugging capabilities for developers.

## Logging Improvements

### Reduced Log Spam

- **Removed GetEffectivePowTypeForHashing spam**: Completely eliminated the verbose logging from the `GetEffectivePowTypeForHashing` function that was cluttering normal operation logs.

- **Converted development logs to debug-only**: Moved numerous verbose development logs from always-on (`LogPrintf`) to debug-only (`LogPrint`) with appropriate categories:
  - MinotaurX algorithm logs now use `-debug=minotaurx`
  - Hive mining logs now use `-debug=hive`
  - Network connection logs now use `-debug=net`
  - RPC authentication logs now use `-debug=rpc`
  - Qt wallet logs now use `-debug=qt`

### Enhanced Debug Categories

The following debug categories are now available for detailed troubleshooting:

- `hive` - Hive mining and bee creation transaction logs
- `minotaurx` - MinotaurX algorithm and difficulty adjustment logs
- `net` - Network connection, socket, and peer management logs
- `rpc` - RPC server and authentication logs
- `qt` - Qt GUI and wallet interface logs
- `rialto` - Rialto messaging system logs

### Usage Examples

```bash
# Normal operation - clean, user-friendly logs
./cascoind

# Debug specific components
./cascoind -debug=hive -debug=minotaurx

# Debug all network issues
./cascoind -debug=net

# Enable all debug logging
./cascoind -debug=all
```

## Technical Changes

- Converted 25+ verbose `LogPrintf` statements to appropriate `LogPrint` categories
- Maintained all important user-facing warnings and error messages
- Improved consistency with Bitcoin Core logging practices
- Enhanced debugging capabilities for developers and advanced users

## Benefits

- **Cleaner logs**: Normal operation now shows only essential information
- **Better debugging**: Developers can enable specific debug categories as needed
- **Improved performance**: Reduced I/O overhead when debug logging is disabled
- **Bitcoin Core compatibility**: Follows established logging patterns from upstream

## Upgrade Notes

This is a recommended upgrade for all users. The logging improvements take effect immediately with no configuration changes required. Users who previously found the logs too verbose will notice a significant improvement in readability.
