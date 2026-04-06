Cascoin Core version 3.3.0 is now available.

This is a minor release with security fixes, macOS 15 (Sequoia) compatibility, Labyrinth UI improvements, and stability fixes across the board. Upgrade is recommended for all users.

## Security Fixes (P0)

Critical security issues identified and fixed by **fromport**:

- **Out-of-bounds access**: Fixed an OOB read that could be triggered during block processing
- **Buffer overflow**: Fixed a buffer overflow in network message handling
- **Memory leak**: Fixed a memory leak in long-running node operations

## macOS 15 (Sequoia) Compatibility

Cascoin Core now builds and runs on macOS 15 Sequoia. The previous release crashed on startup due to deprecated macOS APIs.

- Replaced `NSUserNotification` (removed in macOS 15) with `UNUserNotificationCenter` for desktop notifications
- Replaced `NSAutoreleasePool` with modern `@autoreleasepool` blocks in Objective-C++ code
- Removed `NSBundle` method swizzling hack that was no longer needed
- Added `-framework UserNotifications` to the macOS build system
- Fixed Qt6 `processEvents()` crash in `QCocoaEventDispatcher` on macOS 15 by replacing the blocking initialization loop with a cooperative `QTimer`
- CI runner updated from `macos-14` to `macos-15`

## Labyrinth UI Improvements

Multiple Labyrinth graph and display fixes contributed by **fromport**:

- Y-axis now always scales to minimum 100% and shows human-readable tick labels
- Graph hover tooltip shows percentage of total labyrinth capacity
- Labyrinth weight is displayed as percentage instead of raw ratio
- Fixed `qRound` truncation issue causing incorrect 100% display
- Fixed infinite loop caused by near-zero `pctStep` in ticker
- Fixed OOM on startup caused by uncapped tick count

## Sync & Stability Fixes

- **Qt UI freeze at 99.99%**: Fixed GUI freeze after Initial Block Download completes (fromport)
- **Sync time estimation**: Fixed overly optimistic time remaining for the last few blocks during sync (fromport)
- **Labyrinth stats showing 0**: Fixed SQLite DB empty state overwriting correct wallet data

## Naming & Codebase Cleanup

- Completed rename from bee/honey/hive to mouse/cheese/labyrinth including filenames and icons (fromport)
- Updated build-linux.md documentation (fromport)
- Fixed German path placeholder, Discord typo, and added Codeberg link in docs (fromport)
- Fixed `uname -m` arch detection and absolute paths in Linux wrapper scripts (fromport)
- Removed build artifacts and distribution directory from repo tracking

## Checkpoints

- Integrated new checkpoints for improved initial sync performance

## Upgrade Notes

This is a recommended upgrade for all users, especially macOS users. No database migration is required. Drop-in replacement for 3.2.2.

## Credits

- fromport — security fixes, Labyrinth UI improvements, sync fixes, codebase cleanup
- Alexander Bergmann — macOS 15 compatibility, CI updates, checkpoints
