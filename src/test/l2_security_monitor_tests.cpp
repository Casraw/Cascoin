// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_security_monitor_tests.cpp
 * @brief Unit tests for L2 Security Monitoring System
 * 
 * Tests for anomaly detection, alert system, audit logging,
 * and circuit breaker functionality.
 * 
 * Requirements: 33.1, 33.2, 33.5, 33.6, 36.6
 */

#include <l2/security_monitor.h>
#include <l2/l2_chainparams.h>
#include <key.h>
#include <random.h>
#include <uint256.h>
#include <hash.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <chrono>

namespace {

// Local random context for tests
FastRandomContext g_test_rand_ctx(true);

uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

uint256 RandomUint256() {
    uint256 result;
    for (int i = 0; i < 8; ++i) {
        uint32_t val = TestRand32();
        memcpy(result.begin() + i * 4, &val, 4);
    }
    return result;
}

CKey RandomKey() {
    CKey key;
    key.MakeNewKey(true);
    return key;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(l2_security_monitor_tests, BasicTestingSetup)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(security_monitor_construction)
{
    l2::SecurityMonitor monitor(1);
    
    BOOST_CHECK_EQUAL(monitor.GetChainId(), 1u);
    BOOST_CHECK(!monitor.IsCircuitBreakerTriggered());
    BOOST_CHECK_EQUAL(monitor.GetAuditLogCount(), 0u);
}

BOOST_AUTO_TEST_CASE(security_alert_serialization)
{
    l2::SecurityAlert alert;
    alert.alertId = RandomUint256();
    alert.type = l2::AlertType::WARNING;
    alert.category = l2::SecurityEventCategory::TRANSACTION_ANOMALY;
    alert.message = "Test alert";
    alert.details = "Test details";
    alert.timestamp = TestRand64();
    alert.involvedAddresses.push_back(RandomKey().GetPubKey().GetID());
    alert.relatedTxHashes.push_back(RandomUint256());
    alert.acknowledged = true;
    alert.resolved = false;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << alert;
    
    // Deserialize
    l2::SecurityAlert restored;
    ss >> restored;
    
    BOOST_CHECK(alert.alertId == restored.alertId);
    BOOST_CHECK(alert.type == restored.type);
    BOOST_CHECK(alert.category == restored.category);
    BOOST_CHECK_EQUAL(alert.message, restored.message);
    BOOST_CHECK_EQUAL(alert.details, restored.details);
    BOOST_CHECK_EQUAL(alert.timestamp, restored.timestamp);
    BOOST_CHECK_EQUAL(alert.involvedAddresses.size(), restored.involvedAddresses.size());
    BOOST_CHECK_EQUAL(alert.relatedTxHashes.size(), restored.relatedTxHashes.size());
    BOOST_CHECK_EQUAL(alert.acknowledged, restored.acknowledged);
    BOOST_CHECK_EQUAL(alert.resolved, restored.resolved);
}

BOOST_AUTO_TEST_CASE(audit_log_entry_serialization)
{
    l2::AuditLogEntry entry;
    entry.entryId = RandomUint256();
    entry.timestamp = TestRand64();
    entry.category = l2::SecurityEventCategory::SEQUENCER_BEHAVIOR;
    entry.action = "test_action";
    entry.actor = "test_actor";
    entry.target = "test_target";
    entry.details = "test_details";
    entry.metadata["key1"] = "value1";
    entry.metadata["key2"] = "value2";
    entry.relatedTxHash = RandomUint256();
    entry.success = true;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << entry;
    
    // Deserialize
    l2::AuditLogEntry restored;
    ss >> restored;
    
    BOOST_CHECK(entry.entryId == restored.entryId);
    BOOST_CHECK_EQUAL(entry.timestamp, restored.timestamp);
    BOOST_CHECK(entry.category == restored.category);
    BOOST_CHECK_EQUAL(entry.action, restored.action);
    BOOST_CHECK_EQUAL(entry.actor, restored.actor);
    BOOST_CHECK_EQUAL(entry.target, restored.target);
    BOOST_CHECK_EQUAL(entry.details, restored.details);
    BOOST_CHECK_EQUAL(entry.metadata.size(), restored.metadata.size());
    BOOST_CHECK(entry.relatedTxHash == restored.relatedTxHash);
    BOOST_CHECK_EQUAL(entry.success, restored.success);
}

BOOST_AUTO_TEST_CASE(transaction_stats_serialization)
{
    l2::TransactionStats stats;
    stats.windowStart = TestRand64();
    stats.windowEnd = stats.windowStart + 3600;
    stats.transactionCount = 100;
    stats.totalValue = 1000 * COIN;
    stats.avgValue = 10 * COIN;
    stats.maxValue = 50 * COIN;
    stats.uniqueSenders = 50;
    stats.uniqueReceivers = 75;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << stats;
    
    // Deserialize
    l2::TransactionStats restored;
    ss >> restored;
    
    BOOST_CHECK_EQUAL(stats.windowStart, restored.windowStart);
    BOOST_CHECK_EQUAL(stats.windowEnd, restored.windowEnd);
    BOOST_CHECK_EQUAL(stats.transactionCount, restored.transactionCount);
    BOOST_CHECK_EQUAL(stats.totalValue, restored.totalValue);
    BOOST_CHECK_EQUAL(stats.avgValue, restored.avgValue);
    BOOST_CHECK_EQUAL(stats.maxValue, restored.maxValue);
    BOOST_CHECK_EQUAL(stats.uniqueSenders, restored.uniqueSenders);
    BOOST_CHECK_EQUAL(stats.uniqueReceivers, restored.uniqueReceivers);
}

BOOST_AUTO_TEST_CASE(sequencer_metrics_serialization)
{
    l2::SequencerMetrics metrics;
    metrics.sequencerAddress = RandomKey().GetPubKey().GetID();
    metrics.blocksProposed = 100;
    metrics.blocksMissed = 5;
    metrics.votesAccept = 90;
    metrics.votesReject = 8;
    metrics.votesAbstain = 2;
    metrics.lastActivityTimestamp = TestRand64();
    metrics.uptimePercent = 95.24;
    metrics.reputationScore = 85;
    metrics.previousReputationScore = 80;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << metrics;
    
    // Deserialize
    l2::SequencerMetrics restored;
    ss >> restored;
    
    BOOST_CHECK(metrics.sequencerAddress == restored.sequencerAddress);
    BOOST_CHECK_EQUAL(metrics.blocksProposed, restored.blocksProposed);
    BOOST_CHECK_EQUAL(metrics.blocksMissed, restored.blocksMissed);
    BOOST_CHECK_EQUAL(metrics.votesAccept, restored.votesAccept);
    BOOST_CHECK_EQUAL(metrics.votesReject, restored.votesReject);
    BOOST_CHECK_EQUAL(metrics.votesAbstain, restored.votesAbstain);
    BOOST_CHECK_EQUAL(metrics.lastActivityTimestamp, restored.lastActivityTimestamp);
    BOOST_CHECK_CLOSE(metrics.uptimePercent, restored.uptimePercent, 0.01);
    BOOST_CHECK_EQUAL(metrics.reputationScore, restored.reputationScore);
    BOOST_CHECK_EQUAL(metrics.previousReputationScore, restored.previousReputationScore);
}

BOOST_AUTO_TEST_CASE(circuit_breaker_status_serialization)
{
    l2::CircuitBreakerStatus status;
    status.state = l2::CircuitBreakerState::TRIGGERED;
    status.triggeredAt = TestRand64();
    status.lastStateChange = status.triggeredAt;
    status.triggerReason = "Test trigger";
    status.tvlAtTrigger = 1000000 * COIN;
    status.withdrawalVolumeAtTrigger = 150000 * COIN;
    status.cooldownEndsAt = status.triggeredAt + 3600;
    
    // Serialize
    CDataStream ss(SER_DISK, 0);
    ss << status;
    
    // Deserialize
    l2::CircuitBreakerStatus restored;
    ss >> restored;
    
    BOOST_CHECK(status.state == restored.state);
    BOOST_CHECK_EQUAL(status.triggeredAt, restored.triggeredAt);
    BOOST_CHECK_EQUAL(status.lastStateChange, restored.lastStateChange);
    BOOST_CHECK_EQUAL(status.triggerReason, restored.triggerReason);
    BOOST_CHECK_EQUAL(status.tvlAtTrigger, restored.tvlAtTrigger);
    BOOST_CHECK_EQUAL(status.withdrawalVolumeAtTrigger, restored.withdrawalVolumeAtTrigger);
    BOOST_CHECK_EQUAL(status.cooldownEndsAt, restored.cooldownEndsAt);
}

// ============================================================================
// Transaction Recording and Anomaly Detection Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(record_transaction)
{
    l2::SecurityMonitor monitor(1);
    
    uint256 txHash = RandomUint256();
    uint160 sender = RandomKey().GetPubKey().GetID();
    uint160 receiver = RandomKey().GetPubKey().GetID();
    CAmount value = 100 * COIN;
    uint64_t timestamp = 1000000;
    
    monitor.RecordTransaction(txHash, sender, receiver, value, timestamp);
    
    // Should have created an audit log entry
    BOOST_CHECK_GE(monitor.GetAuditLogCount(), 1u);
}

BOOST_AUTO_TEST_CASE(transaction_stats)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t baseTime = 1000000;
    
    // Record 10 transactions
    for (int i = 0; i < 10; ++i) {
        uint256 txHash = RandomUint256();
        uint160 sender = RandomKey().GetPubKey().GetID();
        uint160 receiver = RandomKey().GetPubKey().GetID();
        CAmount value = (i + 1) * COIN;
        
        monitor.RecordTransaction(txHash, sender, receiver, value, baseTime + i * 100);
    }
    
    // Get stats for 1 hour window
    l2::TransactionStats stats = monitor.GetTransactionStats(3600, baseTime + 1000);
    
    BOOST_CHECK_EQUAL(stats.transactionCount, 10u);
    BOOST_CHECK_EQUAL(stats.totalValue, 55 * COIN);  // 1+2+3+...+10 = 55
    BOOST_CHECK_EQUAL(stats.maxValue, 10 * COIN);
    BOOST_CHECK_EQUAL(stats.uniqueSenders, 10u);
    BOOST_CHECK_EQUAL(stats.uniqueReceivers, 10u);
}

BOOST_AUTO_TEST_CASE(frequency_anomaly_detection)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 spammer = RandomKey().GetPubKey().GetID();
    uint64_t baseTime = 1000000;
    
    // Record 150 transactions from same address (exceeds 100/hour threshold)
    for (int i = 0; i < 150; ++i) {
        uint256 txHash = RandomUint256();
        uint160 receiver = RandomKey().GetPubKey().GetID();
        
        monitor.RecordTransaction(txHash, spammer, receiver, 1 * COIN, baseTime + i * 10);
    }
    
    // Should detect frequency anomaly
    bool anomaly = monitor.DetectFrequencyAnomaly(spammer, baseTime + 1500);
    BOOST_CHECK(anomaly);
    
    // Should have created an alert
    auto alerts = monitor.GetAlertsByCategory(l2::SecurityEventCategory::TRANSACTION_ANOMALY);
    BOOST_CHECK_GE(alerts.size(), 1u);
}

// ============================================================================
// Sequencer Monitoring Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(record_sequencer_action)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 sequencer = RandomKey().GetPubKey().GetID();
    uint64_t timestamp = 1000000;
    
    monitor.RecordSequencerAction(sequencer, "test_action", timestamp, true);
    
    l2::SequencerMetrics metrics = monitor.GetSequencerMetrics(sequencer);
    BOOST_CHECK(metrics.sequencerAddress == sequencer);
    BOOST_CHECK_EQUAL(metrics.lastActivityTimestamp, timestamp);
}

BOOST_AUTO_TEST_CASE(record_block_proposal)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 sequencer = RandomKey().GetPubKey().GetID();
    uint256 blockHash = RandomUint256();
    uint64_t timestamp = 1000000;
    
    monitor.RecordBlockProposal(sequencer, blockHash, timestamp, true);
    
    l2::SequencerMetrics metrics = monitor.GetSequencerMetrics(sequencer);
    BOOST_CHECK_EQUAL(metrics.blocksProposed, 1u);
}

BOOST_AUTO_TEST_CASE(record_missed_block)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 sequencer = RandomKey().GetPubKey().GetID();
    uint64_t timestamp = 1000000;
    
    // Record some successful blocks first
    for (int i = 0; i < 10; ++i) {
        monitor.RecordBlockProposal(sequencer, RandomUint256(), timestamp + i * 100, true);
    }
    
    // Record missed blocks
    for (int i = 0; i < 5; ++i) {
        monitor.RecordMissedBlock(sequencer, i, timestamp + 1000 + i * 100);
    }
    
    l2::SequencerMetrics metrics = monitor.GetSequencerMetrics(sequencer);
    BOOST_CHECK_EQUAL(metrics.blocksProposed, 10u);
    BOOST_CHECK_EQUAL(metrics.blocksMissed, 5u);
    // Uptime should be 10/(10+5) = 66.67%
    BOOST_CHECK_CLOSE(metrics.uptimePercent, 66.67, 1.0);
}

BOOST_AUTO_TEST_CASE(record_sequencer_vote)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 sequencer = RandomKey().GetPubKey().GetID();
    uint64_t timestamp = 1000000;
    
    monitor.RecordSequencerVote(sequencer, RandomUint256(), l2::VoteType::ACCEPT, timestamp);
    monitor.RecordSequencerVote(sequencer, RandomUint256(), l2::VoteType::ACCEPT, timestamp + 100);
    monitor.RecordSequencerVote(sequencer, RandomUint256(), l2::VoteType::REJECT, timestamp + 200);
    monitor.RecordSequencerVote(sequencer, RandomUint256(), l2::VoteType::ABSTAIN, timestamp + 300);
    
    l2::SequencerMetrics metrics = monitor.GetSequencerMetrics(sequencer);
    BOOST_CHECK_EQUAL(metrics.votesAccept, 2u);
    BOOST_CHECK_EQUAL(metrics.votesReject, 1u);
    BOOST_CHECK_EQUAL(metrics.votesAbstain, 1u);
}

BOOST_AUTO_TEST_CASE(get_all_sequencer_metrics)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 seq1 = RandomKey().GetPubKey().GetID();
    uint160 seq2 = RandomKey().GetPubKey().GetID();
    uint64_t timestamp = 1000000;
    
    monitor.RecordBlockProposal(seq1, RandomUint256(), timestamp, true);
    monitor.RecordBlockProposal(seq2, RandomUint256(), timestamp + 100, true);
    
    auto allMetrics = monitor.GetAllSequencerMetrics();
    BOOST_CHECK_EQUAL(allMetrics.size(), 2u);
    BOOST_CHECK(allMetrics.find(seq1) != allMetrics.end());
    BOOST_CHECK(allMetrics.find(seq2) != allMetrics.end());
}

// ============================================================================
// Bridge Monitoring Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(record_bridge_balance_no_discrepancy)
{
    l2::SecurityMonitor monitor(1);
    
    CAmount expected = 1000 * COIN;
    CAmount actual = 1000 * COIN;
    uint64_t timestamp = 1000000;
    
    monitor.RecordBridgeBalance(expected, actual, timestamp);
    
    BOOST_CHECK(!monitor.HasBridgeDiscrepancy());
    BOOST_CHECK_EQUAL(monitor.GetBridgeDiscrepancy(), 0);
}

BOOST_AUTO_TEST_CASE(record_bridge_balance_with_discrepancy)
{
    l2::SecurityMonitor monitor(1);
    
    CAmount expected = 1000 * COIN;
    CAmount actual = 900 * COIN;  // 10% discrepancy
    uint64_t timestamp = 1000000;
    
    monitor.RecordBridgeBalance(expected, actual, timestamp);
    
    BOOST_CHECK(monitor.HasBridgeDiscrepancy());
    BOOST_CHECK_EQUAL(monitor.GetBridgeDiscrepancy(), -100 * COIN);
    
    // Should have created a critical alert
    auto alerts = monitor.GetAlertsByCategory(l2::SecurityEventCategory::BRIDGE_DISCREPANCY);
    BOOST_CHECK_GE(alerts.size(), 1u);
    BOOST_CHECK(alerts[0].type == l2::AlertType::CRITICAL);
}

// ============================================================================
// Reputation Monitoring Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(record_reputation_change_normal)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 address = RandomKey().GetPubKey().GetID();
    uint64_t timestamp = 1000000;
    
    // Small reputation change (not significant)
    monitor.RecordReputationChange(address, 80, 75, timestamp);
    
    BOOST_CHECK(!monitor.HasSignificantReputationDrop(address));
}

BOOST_AUTO_TEST_CASE(record_reputation_change_significant_drop)
{
    l2::SecurityMonitor monitor(1);
    
    uint160 address = RandomKey().GetPubKey().GetID();
    uint64_t timestamp = 1000000;
    
    // First record a baseline
    monitor.RecordReputationChange(address, 90, 85, timestamp);
    
    // Then record a significant drop (>= 20 points)
    monitor.RecordReputationChange(address, 85, 60, timestamp + 100);
    
    BOOST_CHECK(monitor.HasSignificantReputationDrop(address));
    
    // Should have created a warning alert
    auto alerts = monitor.GetAlertsByCategory(l2::SecurityEventCategory::REPUTATION_CHANGE);
    BOOST_CHECK_GE(alerts.size(), 1u);
}

// ============================================================================
// Alert System Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(create_alert)
{
    l2::SecurityMonitor monitor(1);
    
    l2::SecurityAlert alert = monitor.CreateAlert(
        l2::AlertType::WARNING,
        l2::SecurityEventCategory::SYSTEM_ERROR,
        "Test alert message",
        "Test details");
    
    BOOST_CHECK(alert.type == l2::AlertType::WARNING);
    BOOST_CHECK(alert.category == l2::SecurityEventCategory::SYSTEM_ERROR);
    BOOST_CHECK_EQUAL(alert.message, "Test alert message");
    BOOST_CHECK_EQUAL(alert.details, "Test details");
    BOOST_CHECK(!alert.acknowledged);
    BOOST_CHECK(!alert.resolved);
}

BOOST_AUTO_TEST_CASE(get_active_alerts)
{
    l2::SecurityMonitor monitor(1);
    
    // Create some alerts
    monitor.CreateAlert(l2::AlertType::INFO, l2::SecurityEventCategory::SYSTEM_ERROR, "Info 1");
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Warning 1");
    monitor.CreateAlert(l2::AlertType::CRITICAL, l2::SecurityEventCategory::SYSTEM_ERROR, "Critical 1");
    
    auto active = monitor.GetActiveAlerts();
    BOOST_CHECK_EQUAL(active.size(), 3u);
}

BOOST_AUTO_TEST_CASE(acknowledge_and_resolve_alert)
{
    l2::SecurityMonitor monitor(1);
    
    l2::SecurityAlert alert = monitor.CreateAlert(
        l2::AlertType::WARNING,
        l2::SecurityEventCategory::SYSTEM_ERROR,
        "Test alert");
    
    // Acknowledge
    BOOST_CHECK(monitor.AcknowledgeAlert(alert.alertId));
    
    // Resolve
    BOOST_CHECK(monitor.ResolveAlert(alert.alertId, "Fixed the issue"));
    
    // Should no longer be in active alerts
    auto active = monitor.GetActiveAlerts();
    BOOST_CHECK_EQUAL(active.size(), 0u);
}

BOOST_AUTO_TEST_CASE(get_alerts_by_type)
{
    l2::SecurityMonitor monitor(1);
    
    monitor.CreateAlert(l2::AlertType::INFO, l2::SecurityEventCategory::SYSTEM_ERROR, "Info 1");
    monitor.CreateAlert(l2::AlertType::INFO, l2::SecurityEventCategory::SYSTEM_ERROR, "Info 2");
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Warning 1");
    
    auto infoAlerts = monitor.GetAlertsByType(l2::AlertType::INFO);
    BOOST_CHECK_EQUAL(infoAlerts.size(), 2u);
    
    auto warningAlerts = monitor.GetAlertsByType(l2::AlertType::WARNING);
    BOOST_CHECK_EQUAL(warningAlerts.size(), 1u);
}

BOOST_AUTO_TEST_CASE(get_alert_counts)
{
    l2::SecurityMonitor monitor(1);
    
    monitor.CreateAlert(l2::AlertType::INFO, l2::SecurityEventCategory::SYSTEM_ERROR, "Info 1");
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Warning 1");
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Warning 2");
    monitor.CreateAlert(l2::AlertType::CRITICAL, l2::SecurityEventCategory::SYSTEM_ERROR, "Critical 1");
    
    auto counts = monitor.GetAlertCounts();
    BOOST_CHECK_EQUAL(counts[l2::AlertType::INFO], 1u);
    BOOST_CHECK_EQUAL(counts[l2::AlertType::WARNING], 2u);
    BOOST_CHECK_EQUAL(counts[l2::AlertType::CRITICAL], 1u);
    BOOST_CHECK_EQUAL(counts[l2::AlertType::EMERGENCY], 0u);
}

BOOST_AUTO_TEST_CASE(alert_callback)
{
    l2::SecurityMonitor monitor(1);
    
    bool callbackCalled = false;
    l2::SecurityAlert receivedAlert;
    
    monitor.RegisterAlertCallback([&](const l2::SecurityAlert& alert) {
        callbackCalled = true;
        receivedAlert = alert;
    });
    
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Test");
    
    BOOST_CHECK(callbackCalled);
    BOOST_CHECK_EQUAL(receivedAlert.message, "Test");
}

// ============================================================================
// Audit Logging Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(log_audit_entry)
{
    l2::SecurityMonitor monitor(1);
    
    std::map<std::string, std::string> metadata;
    metadata["key1"] = "value1";
    
    l2::AuditLogEntry entry = monitor.LogAudit(
        l2::SecurityEventCategory::SEQUENCER_BEHAVIOR,
        "test_action",
        "test_actor",
        "test_target",
        "test_details",
        metadata,
        RandomUint256(),
        true);
    
    BOOST_CHECK(entry.category == l2::SecurityEventCategory::SEQUENCER_BEHAVIOR);
    BOOST_CHECK_EQUAL(entry.action, "test_action");
    BOOST_CHECK_EQUAL(entry.actor, "test_actor");
    BOOST_CHECK_EQUAL(entry.target, "test_target");
    BOOST_CHECK_EQUAL(entry.details, "test_details");
    BOOST_CHECK_EQUAL(entry.metadata.size(), 1u);
    BOOST_CHECK(entry.success);
    
    BOOST_CHECK_EQUAL(monitor.GetAuditLogCount(), 1u);
}

BOOST_AUTO_TEST_CASE(get_audit_log_by_time_range)
{
    l2::SecurityMonitor monitor(1);
    
    // Log entries at different times
    for (int i = 0; i < 10; ++i) {
        monitor.LogAudit(
            l2::SecurityEventCategory::SYSTEM_ERROR,
            "action_" + std::to_string(i),
            "actor",
            "target");
    }
    
    uint64_t now = GetTime();
    auto entries = monitor.GetAuditLog(now - 3600, now + 3600);
    BOOST_CHECK_EQUAL(entries.size(), 10u);
}

BOOST_AUTO_TEST_CASE(get_audit_log_by_category)
{
    l2::SecurityMonitor monitor(1);
    
    monitor.LogAudit(l2::SecurityEventCategory::SEQUENCER_BEHAVIOR, "action1", "actor", "target");
    monitor.LogAudit(l2::SecurityEventCategory::SEQUENCER_BEHAVIOR, "action2", "actor", "target");
    monitor.LogAudit(l2::SecurityEventCategory::BRIDGE_DISCREPANCY, "action3", "actor", "target");
    
    auto seqEntries = monitor.GetAuditLogByCategory(l2::SecurityEventCategory::SEQUENCER_BEHAVIOR);
    BOOST_CHECK_EQUAL(seqEntries.size(), 2u);
    
    auto bridgeEntries = monitor.GetAuditLogByCategory(l2::SecurityEventCategory::BRIDGE_DISCREPANCY);
    BOOST_CHECK_EQUAL(bridgeEntries.size(), 1u);
}

BOOST_AUTO_TEST_CASE(get_audit_log_by_actor)
{
    l2::SecurityMonitor monitor(1);
    
    monitor.LogAudit(l2::SecurityEventCategory::SYSTEM_ERROR, "action1", "actor1", "target");
    monitor.LogAudit(l2::SecurityEventCategory::SYSTEM_ERROR, "action2", "actor1", "target");
    monitor.LogAudit(l2::SecurityEventCategory::SYSTEM_ERROR, "action3", "actor2", "target");
    
    auto actor1Entries = monitor.GetAuditLogByActor("actor1");
    BOOST_CHECK_EQUAL(actor1Entries.size(), 2u);
    
    auto actor2Entries = monitor.GetAuditLogByActor("actor2");
    BOOST_CHECK_EQUAL(actor2Entries.size(), 1u);
}

BOOST_AUTO_TEST_CASE(prune_audit_log)
{
    l2::SecurityMonitor monitor(1);
    
    // Log some entries
    for (int i = 0; i < 10; ++i) {
        monitor.LogAudit(l2::SecurityEventCategory::SYSTEM_ERROR, "action", "actor", "target");
    }
    
    BOOST_CHECK_EQUAL(monitor.GetAuditLogCount(), 10u);
    
    // Prune with very short retention (should remove all)
    uint64_t futureTime = GetTime() + 1000000;
    size_t pruned = monitor.PruneAuditLog(futureTime, 1);
    
    BOOST_CHECK_EQUAL(pruned, 10u);
    BOOST_CHECK_EQUAL(monitor.GetAuditLogCount(), 0u);
}

// ============================================================================
// Circuit Breaker Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(circuit_breaker_initial_state)
{
    l2::SecurityMonitor monitor(1);
    
    BOOST_CHECK(!monitor.IsCircuitBreakerTriggered());
    
    l2::CircuitBreakerStatus status = monitor.GetCircuitBreakerStatus();
    BOOST_CHECK(status.state == l2::CircuitBreakerState::NORMAL);
}

BOOST_AUTO_TEST_CASE(record_withdrawal)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    monitor.RecordWithdrawal(100 * COIN, timestamp);
    monitor.RecordWithdrawal(200 * COIN, timestamp + 100);
    
    CAmount dailyVolume = monitor.GetDailyWithdrawalVolume(timestamp + 200);
    BOOST_CHECK_EQUAL(dailyVolume, 300 * COIN);
}

BOOST_AUTO_TEST_CASE(circuit_breaker_trigger_on_high_withdrawal)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    // Set TVL
    monitor.UpdateTVL(1000 * COIN, timestamp);
    
    // Record withdrawals exceeding 10% of TVL
    monitor.RecordWithdrawal(50 * COIN, timestamp);
    monitor.RecordWithdrawal(60 * COIN, timestamp + 100);  // Total 110 CAS = 11% of TVL
    
    BOOST_CHECK(monitor.IsCircuitBreakerTriggered());
    
    l2::CircuitBreakerStatus status = monitor.GetCircuitBreakerStatus();
    BOOST_CHECK(status.state == l2::CircuitBreakerState::TRIGGERED);
    BOOST_CHECK_EQUAL(status.tvlAtTrigger, 1000 * COIN);
}

BOOST_AUTO_TEST_CASE(circuit_breaker_no_trigger_below_threshold)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    // Set TVL
    monitor.UpdateTVL(1000 * COIN, timestamp);
    
    // Record withdrawals below 10% of TVL
    monitor.RecordWithdrawal(50 * COIN, timestamp);
    monitor.RecordWithdrawal(40 * COIN, timestamp + 100);  // Total 90 CAS = 9% of TVL
    
    BOOST_CHECK(!monitor.IsCircuitBreakerTriggered());
}

BOOST_AUTO_TEST_CASE(circuit_breaker_manual_trigger)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    monitor.TriggerCircuitBreaker("Manual trigger for testing", timestamp);
    
    BOOST_CHECK(monitor.IsCircuitBreakerTriggered());
    
    l2::CircuitBreakerStatus status = monitor.GetCircuitBreakerStatus();
    BOOST_CHECK_EQUAL(status.triggerReason, "Manual trigger for testing");
}

BOOST_AUTO_TEST_CASE(circuit_breaker_reset)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    // Trigger circuit breaker
    monitor.TriggerCircuitBreaker("Test trigger", timestamp);
    BOOST_CHECK(monitor.IsCircuitBreakerTriggered());
    
    // Try to reset during cooldown (should fail)
    BOOST_CHECK(!monitor.ResetCircuitBreaker(timestamp + 100));
    
    // Reset after cooldown
    uint64_t afterCooldown = timestamp + l2::CIRCUIT_BREAKER_COOLDOWN + 1;
    BOOST_CHECK(monitor.ResetCircuitBreaker(afterCooldown));
    BOOST_CHECK(!monitor.IsCircuitBreakerTriggered());
}

BOOST_AUTO_TEST_CASE(circuit_breaker_callback)
{
    l2::SecurityMonitor monitor(1);
    
    bool callbackCalled = false;
    l2::CircuitBreakerState receivedState;
    std::string receivedReason;
    
    monitor.RegisterCircuitBreakerCallback([&](l2::CircuitBreakerState state, const std::string& reason) {
        callbackCalled = true;
        receivedState = state;
        receivedReason = reason;
    });
    
    monitor.TriggerCircuitBreaker("Test trigger", 1000000);
    
    BOOST_CHECK(callbackCalled);
    BOOST_CHECK(receivedState == l2::CircuitBreakerState::TRIGGERED);
    BOOST_CHECK_EQUAL(receivedReason, "Test trigger");
}

// ============================================================================
// Dashboard Metrics Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(get_dashboard_metrics)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    // Set up some data
    monitor.UpdateTVL(10000 * COIN, timestamp);
    monitor.RecordWithdrawal(500 * COIN, timestamp);
    
    // Create some alerts
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Warning 1");
    monitor.CreateAlert(l2::AlertType::CRITICAL, l2::SecurityEventCategory::SYSTEM_ERROR, "Critical 1");
    
    // Record sequencer activity
    uint160 seq = RandomKey().GetPubKey().GetID();
    monitor.RecordBlockProposal(seq, RandomUint256(), timestamp, true);
    
    l2::SecurityDashboardMetrics metrics = monitor.GetDashboardMetrics(timestamp + 100);
    
    BOOST_CHECK_EQUAL(metrics.activeAlerts, 2u);
    BOOST_CHECK_EQUAL(metrics.criticalAlerts, 1u);
    BOOST_CHECK_EQUAL(metrics.totalValueLocked, 10000 * COIN);
    BOOST_CHECK_EQUAL(metrics.dailyWithdrawalVolume, 500 * COIN);
    BOOST_CHECK_CLOSE(metrics.withdrawalToTvlRatio, 0.05, 0.001);
    BOOST_CHECK_EQUAL(metrics.activeSequencers, 1u);
    BOOST_CHECK(metrics.circuitBreakerState == l2::CircuitBreakerState::NORMAL);
}

BOOST_AUTO_TEST_CASE(get_anomalies_detected_24h)
{
    l2::SecurityMonitor monitor(1);
    
    uint64_t timestamp = 1000000;
    
    // Create some anomaly-triggering conditions
    uint160 spammer = RandomKey().GetPubKey().GetID();
    
    // Record 150 transactions from same address
    for (int i = 0; i < 150; ++i) {
        monitor.RecordTransaction(RandomUint256(), spammer, RandomKey().GetPubKey().GetID(), 
                                  1 * COIN, timestamp + i * 10);
    }
    
    // Detect the anomaly
    monitor.DetectFrequencyAnomaly(spammer, timestamp + 1500);
    
    uint64_t anomalies = monitor.GetAnomaliesDetected24h(timestamp + 2000);
    BOOST_CHECK_GE(anomalies, 1u);
}

// ============================================================================
// Configuration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(set_thresholds)
{
    l2::SecurityMonitor monitor(1);
    
    monitor.SetVolumeSpikeTreshold(10.0);
    monitor.SetValueSpikeThreshold(20.0);
    monitor.SetCircuitBreakerThreshold(0.15);
    
    // Verify thresholds are applied (indirectly through behavior)
    // The thresholds affect anomaly detection and circuit breaker triggering
    
    uint64_t timestamp = 1000000;
    monitor.UpdateTVL(1000 * COIN, timestamp);
    
    // With 15% threshold, 140 CAS withdrawal should trigger
    monitor.RecordWithdrawal(140 * COIN, timestamp);
    monitor.RecordWithdrawal(20 * COIN, timestamp + 100);  // Total 160 = 16% > 15%
    
    BOOST_CHECK(monitor.IsCircuitBreakerTriggered());
}

BOOST_AUTO_TEST_CASE(clear_monitor)
{
    l2::SecurityMonitor monitor(1);
    
    // Add some data
    monitor.CreateAlert(l2::AlertType::WARNING, l2::SecurityEventCategory::SYSTEM_ERROR, "Test");
    monitor.LogAudit(l2::SecurityEventCategory::SYSTEM_ERROR, "action", "actor", "target");
    monitor.RecordTransaction(RandomUint256(), RandomKey().GetPubKey().GetID(), 
                              RandomKey().GetPubKey().GetID(), 100 * COIN, 1000000);
    
    BOOST_CHECK_GE(monitor.GetActiveAlerts().size(), 1u);
    BOOST_CHECK_GE(monitor.GetAuditLogCount(), 1u);
    
    // Clear
    monitor.Clear();
    
    BOOST_CHECK_EQUAL(monitor.GetActiveAlerts().size(), 0u);
    BOOST_CHECK_EQUAL(monitor.GetAuditLogCount(), 0u);
    BOOST_CHECK(!monitor.IsCircuitBreakerTriggered());
}

// ============================================================================
// Global Instance Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(global_instance)
{
    // Initialize
    l2::InitSecurityMonitor(1);
    BOOST_CHECK(l2::IsSecurityMonitorInitialized());
    
    // Get instance
    l2::SecurityMonitor& monitor = l2::GetSecurityMonitor();
    BOOST_CHECK_EQUAL(monitor.GetChainId(), 1u);
}

BOOST_AUTO_TEST_SUITE_END()
