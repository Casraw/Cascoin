// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/migration_observability.h>

#include <util.h>
#include <utiltime.h>
#include <sync.h>

#include <array>
#include <atomic>

namespace CVM {

namespace {

// Number of distinct MigrationEvent categories.
constexpr size_t kNumMigrationEvents = 6;

// Minimum spacing between emitted log lines for a single category. A stale/
// malformed development database can contain many affected records; rate
// limiting keeps the log bounded while the running counters remain exact.
constexpr int64_t kRateLimitIntervalMs = 10000; // 10 seconds

// Running counters (exact, never rate limited).
std::array<std::atomic<uint64_t>, kNumMigrationEvents> g_eventCounts = {};

// Rate-limit bookkeeping. Guards the last-log timestamp and the count of
// occurrences that were suppressed since the previous emitted line.
CCriticalSection cs_migrationLog;
std::array<int64_t, kNumMigrationEvents> g_lastLogMs = {};
std::array<uint64_t, kNumMigrationEvents> g_suppressedSinceLog = {};

size_t EventIndex(MigrationEvent event) {
    return static_cast<size_t>(event);
}

const char* EventName(MigrationEvent event) {
    switch (event) {
        case MigrationEvent::MalformedRecord:  return "malformed-record";
        case MigrationEvent::FailedIndexWrite: return "failed-index-write";
        case MigrationEvent::RebuildStart:     return "rebuild-start";
        case MigrationEvent::RebuildComplete:  return "rebuild-complete";
        case MigrationEvent::RejectedPayload:  return "rejected-payload";
        case MigrationEvent::IdempotentSkip:   return "idempotent-skip";
    }
    return "unknown";
}

// A malformed current record is the signal that the affected namespace holds
// stale not-live data and must be rebuilt from a clean state rather than
// reinterpreted. Surface that guidance on the (rate-limited) log line.
bool EventRequiresRebuildHint(MigrationEvent event) {
    return event == MigrationEvent::MalformedRecord;
}

} // namespace

void RecordMigrationEvent(MigrationEvent event,
                          const std::string& nsContext,
                          const std::string& id) {
    const size_t idx = EventIndex(event);
    const uint64_t total = ++g_eventCounts[idx];

    bool emit = false;
    uint64_t suppressed = 0;
    {
        LOCK(cs_migrationLog);
        const int64_t nowMs = GetTimeMillis();
        if (g_lastLogMs[idx] == 0 || nowMs - g_lastLogMs[idx] >= kRateLimitIntervalMs) {
            emit = true;
            suppressed = g_suppressedSinceLog[idx];
            g_suppressedSinceLog[idx] = 0;
            g_lastLogMs[idx] = nowMs;
        } else {
            ++g_suppressedSinceLog[idx];
        }
    }

    if (!emit) {
        return;
    }

    // Build a bounded log line. Only namespace context and an optional bounded
    // natural identifier are logged — never wallet secrets or full member lists.
    std::string line = strprintf("CVM migration [%s] namespace=%s total=%d",
                                 EventName(event), nsContext, total);
    if (!id.empty()) {
        line += strprintf(" id=%s", id);
    }
    if (suppressed > 0) {
        line += strprintf(" (+%d suppressed since last log)", suppressed);
    }
    if (EventRequiresRebuildHint(event)) {
        line += " — clean rebuild of this namespace required (stale not-live records are skipped, not reinterpreted)";
    }
    LogPrint(BCLog::CVM, "%s\n", line);
}

uint64_t GetMigrationEventCount(MigrationEvent event) {
    return g_eventCounts[EventIndex(event)].load();
}

void ResetMigrationEventCounters() {
    LOCK(cs_migrationLog);
    for (size_t i = 0; i < kNumMigrationEvents; ++i) {
        g_eventCounts[i].store(0);
        g_lastLogMs[i] = 0;
        g_suppressedSinceLog[i] = 0;
    }
}

} // namespace CVM
