Cascoin Core version 3.2.1 is now available.

## Bug Fixes

### Labyrinth Tab - BCT Database Improvements

- **Fixed foreign BCT detection**: The SQLite database now correctly validates that all stored BCTs belong to the wallet. Foreign BCTs (from other wallets) are automatically detected and the database is rebuilt on startup.

- **Fixed BCT status updates**: BCT status (immature/mature/expired) and "Estimated Time" now update correctly when new blocks arrive. Status is calculated dynamically based on current chain height.

- **Fixed reward tracking**: Added `rescanRewardsOnly()` function that runs on every startup to catch any missed mining rewards. This ensures rewards are properly attributed to BCTs even if they were missed during normal operation.

- **Improved summary display**: The Labyrinth tab summary now always shows total rewards from all BCTs (including expired ones), even when the "Include expired mice" checkbox is unchecked.

### Technical Changes

- Added `validateWalletOwnership()` function to detect BCTs that don't belong to the current wallet
- Added `creationHeight`, `maturityHeight`, `expirationHeight` fields to `CBeeCreationTransactionInfo` for accurate status calculation
- Improved transaction ordering in database operations to ensure BCTs are committed before reward scanning
- Added debug logging for reward matching to aid troubleshooting

## Upgrade Notes

This is a recommended upgrade for all users. The database will automatically validate and rebuild if necessary on first startup.
