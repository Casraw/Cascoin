Cascoin Core version *3.2.1.3* is now available from:

  <https://download.cascoin.net/cascoin-3.2.1.3/>

This is a hotfix release that addresses a critical bug in the Labyrinth tab
where displayed values (blocks found, rewards, mice counts) would drop to zero
after approximately 1-2 days of continuous operation. A wallet restart would
temporarily restore the correct values.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/casraw/cascoin/issues>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over `/Applications/Cascoin-Qt` (on Mac)
or `cascoind`/`cascoin-qt` (on Linux).

No database migration is required. This is a drop-in replacement for 3.2.1.2.

Notable changes
===============

Labyrinth display values going to zero after 1-2 days (fix)
------------------------------------------------------------

A combination of three issues caused the Labyrinth tab and Overview page to
show zero values for blocks found, rewards, costs, and mice counts after
extended uptime:

1. **SQLite busy timeout missing**: The BCT database did not set a
   `busy_timeout`, causing concurrent read queries from the GUI thread to
   fail silently with `SQLITE_BUSY` when the block processing thread held a
   write lock. This returned empty result sets (all zeros) to the display.
   A 5-second busy timeout has been added.

2. **Race condition in table model updates**: The `HiveTableModel` summary
   variables (immature, mature, blocks found, rewards, etc.) were reset to
   zero before new data was loaded from the background thread. If the GUI
   queried these values during the brief reset window, it would display zeros.
   Summary values are now accumulated in local variables and written atomically
   after processing completes.

3. **Expired mice excluded from totals**: The Labyrinth tab and Overview page
   obtained their blocks found and reward totals from the table model, which
   by default only loads non-expired mice. Once all mice expired, the displayed
   totals dropped to zero even though the database contained the correct values.
   Both pages now query the database summary directly, which includes all mice
   regardless of status.

Additional fixes
----------------

- Fixed missing `cs_main` lock in reward rescan during startup, which could
  cause assertion failures in debug builds.
- Extended `BCTSummary` with per-status mice counts (`immatureBees`,
  `matureBees`, `expiredBees`) so the GUI displays actual mice counts instead
  of transaction counts.
- Renamed internal comments from "Hive" / "Bee" terminology to "Labyrinth" /
  "Mice" for consistency with the user-facing naming.

3.2.1.3 Change log
===================

### Labyrinth / GUI
- Add `PRAGMA busy_timeout=5000` to BCT SQLite database initialization
- Fix race condition in `HiveTableModel::updateBCTs()` summary value updates
- Fix `HiveDialog::updateHiveSummary()` to query database summary directly
- Fix `OverviewPage::updateHiveSummary()` to query database summary directly
- Add `immatureBees`, `matureBees`, `expiredBees` fields to `BCTSummary`
- Fix missing `cs_main` lock in `BCTDatabaseSQLite::rescanRewardsOnly()`
- Rename "Cascoin: Hive" comments to "Cascoin: Labyrinth" in modified files

### Dependencies
- Update zlib from 1.3.1 to 1.3.2 (fixes 404 on cross-compilation for Windows)

Dependency updates
------------------

- Updated zlib from 1.3.1 to 1.3.2 in the depends build system. The previous
  version was removed from the upstream download server (zlib.net), causing
  404 errors during cross-compilation for Windows.

Credits
=======

Thanks to everyone who directly contributed to this release.
