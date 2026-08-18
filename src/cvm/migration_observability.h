// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_MIGRATION_OBSERVABILITY_H
#define CASCOIN_CVM_MIGRATION_OBSERVABILITY_H

#include <cstdint>
#include <string>

namespace CVM {

// Observability categories for the TrustNodeId full-migration clean development
// state / rebuild behavior (bugfix.md 5.2-5.4, design.md §6). Each category is
// counted independently and logged under BCLog::CVM with rate limiting so that a
// database full of stale/malformed not-live records cannot flood the log.
enum class MigrationEvent {
    // A current typed key or record could not be parsed as the one canonical
    // layout and was skipped (never reinterpreted). This is the signal that a
    // clean rebuild of the affected namespace is required.
    MalformedRecord,
    // A secondary-index write failed after its primary write succeeded. The
    // logical primary state is intact; a repeated idempotent upsert repairs the
    // index without duplicating state (clause 5.2).
    FailedIndexWrite,
    // A deterministic/idempotent rebuild of a downstream namespace has started.
    RebuildStart,
    // A deterministic/idempotent rebuild of a downstream namespace has finished.
    RebuildComplete,
    // A newly-changed identity-bearing OP_RETURN payload was rejected because it
    // did not match the one canonical typed layout.
    RejectedPayload,
    // A repeated write/processing operation produced no new logical state and was
    // skipped (idempotent replay).
    IdempotentSkip,
};

// Record one occurrence of `event`. Increments the per-event counter and emits a
// bounded, rate-limited BCLog::CVM log line that names the affected current
// namespace (`nsContext`) and, optionally, a bounded natural identifier (`id`,
// e.g. a ToKeyString() segment or a txid string). Callers MUST NOT pass wallet
// secrets or full cluster member lists as `id`.
void RecordMigrationEvent(MigrationEvent event,
                          const std::string& nsContext,
                          const std::string& id = std::string());

// Return the total number of times `event` has been recorded since process start
// (or since the last ResetMigrationEventCounters call). Intended for tests and
// diagnostics.
uint64_t GetMigrationEventCount(MigrationEvent event);

// Reset all migration event counters and rate-limit state. Intended for tests.
void ResetMigrationEventCounters();

} // namespace CVM

#endif // CASCOIN_CVM_MIGRATION_OBSERVABILITY_H
