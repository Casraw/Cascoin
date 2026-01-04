// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/security_rpc.h>
#include <cvm/security_audit.h>
#include <cvm/anomaly_detector.h>
#include <cvm/access_control_audit.h>
#include <cvm/dos_protection.h>
#include <cvm/cvmdb.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <univalue.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validation.h>

/**
 * RPC: getsecuritymetrics
 * 
 * Get current security metrics for the CVM system.
 * Implements requirement 10.3: Track consensus validation success/failure rates,
 * monitor validator participation and response times.
 */
UniValue getsecuritymetrics(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getsecuritymetrics ( startblock endblock )\n"
            "\nGet security metrics for the CVM system.\n"
            "\nArguments:\n"
            "1. startblock    (numeric, optional) Start block height for metrics window\n"
            "2. endblock      (numeric, optional) End block height for metrics window\n"
            "\nResult:\n"
            "{\n"
            "  \"consensus\": {\n"
            "    \"total_validations\": n,\n"
            "    \"successful_validations\": n,\n"
            "    \"failed_validations\": n,\n"
            "    \"success_rate\": n\n"
            "  },\n"
            "  \"validators\": {\n"
            "    \"active_validators\": n,\n"
            "    \"total_responses\": n,\n"
            "    \"average_response_time_ms\": n,\n"
            "    \"average_accuracy\": n\n"
            "  },\n"
            "  \"reputation\": {\n"
            "    \"total_changes\": n,\n"
            "    \"penalties_applied\": n,\n"
            "    \"bonuses_applied\": n,\n"
            "    \"average_change\": n\n"
            "  },\n"
            "  \"fraud\": {\n"
            "    \"attempts_detected\": n,\n"
            "    \"records_created\": n,\n"
            "    \"sybil_attacks_detected\": n\n"
            "  },\n"
            "  \"anomalies\": {\n"
            "    \"total_detected\": n,\n"
            "    \"reputation_anomalies\": n,\n"
            "    \"validator_anomalies\": n,\n"
            "    \"trust_graph_anomalies\": n\n"
            "  },\n"
            "  \"access_control\": {\n"
            "    \"total_attempts\": n,\n"
            "    \"access_granted\": n,\n"
            "    \"access_denied\": n,\n"
            "    \"denial_rate\": n\n"
            "  },\n"
            "  \"window\": {\n"
            "    \"start_block\": n,\n"
            "    \"end_block\": n,\n"
            "    \"start_time\": n,\n"
            "    \"end_time\": n\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getsecuritymetrics", "")
            + HelpExampleCli("getsecuritymetrics", "100000 100100")
            + HelpExampleRpc("getsecuritymetrics", "100000, 100100")
        );

    if (!CVM::g_securityAudit) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Security audit system not initialized");
    }

    CVM::SecurityMetrics metrics;
    
    if (request.params.size() >= 2) {
        int startBlock = request.params[0].get_int();
        int endBlock = request.params[1].get_int();
        metrics = CVM::g_securityAudit->GetMetricsForBlockRange(startBlock, endBlock);
    } else {
        metrics = CVM::g_securityAudit->GetCurrentMetrics();
    }

    UniValue result(UniValue::VOBJ);
    
    // Consensus metrics
    UniValue consensus(UniValue::VOBJ);
    consensus.pushKV("total_validations", (uint64_t)metrics.totalValidations);
    consensus.pushKV("successful_validations", (uint64_t)metrics.successfulValidations);
    consensus.pushKV("failed_validations", (uint64_t)metrics.failedValidations);
    consensus.pushKV("success_rate", metrics.validationSuccessRate);
    result.pushKV("consensus", consensus);
    
    // Validator metrics
    UniValue validators(UniValue::VOBJ);
    validators.pushKV("active_validators", (uint64_t)metrics.activeValidators);
    validators.pushKV("total_responses", (uint64_t)metrics.totalValidatorResponses);
    validators.pushKV("average_response_time_ms", metrics.averageResponseTime);
    validators.pushKV("average_accuracy", metrics.averageValidatorAccuracy);
    result.pushKV("validators", validators);
    
    // Reputation metrics
    UniValue reputation(UniValue::VOBJ);
    reputation.pushKV("total_changes", (uint64_t)metrics.reputationChanges);
    reputation.pushKV("penalties_applied", (uint64_t)metrics.reputationPenalties);
    reputation.pushKV("bonuses_applied", (uint64_t)metrics.reputationBonuses);
    reputation.pushKV("average_change", metrics.averageReputationChange);
    result.pushKV("reputation", reputation);
    
    // Fraud metrics
    UniValue fraud(UniValue::VOBJ);
    fraud.pushKV("attempts_detected", (uint64_t)metrics.fraudAttemptsDetected);
    fraud.pushKV("records_created", (uint64_t)metrics.fraudRecordsCreated);
    fraud.pushKV("sybil_attacks_detected", (uint64_t)metrics.sybilAttacksDetected);
    result.pushKV("fraud", fraud);
    
    // Anomaly metrics
    UniValue anomalies(UniValue::VOBJ);
    anomalies.pushKV("total_detected", (uint64_t)metrics.anomaliesDetected);
    anomalies.pushKV("reputation_anomalies", (uint64_t)metrics.reputationAnomalies);
    anomalies.pushKV("validator_anomalies", (uint64_t)metrics.validatorAnomalies);
    anomalies.pushKV("trust_graph_anomalies", (uint64_t)metrics.trustGraphAnomalies);
    result.pushKV("anomalies", anomalies);
    
    // Access control metrics
    UniValue accessControl(UniValue::VOBJ);
    accessControl.pushKV("total_attempts", (uint64_t)metrics.accessAttempts);
    accessControl.pushKV("access_granted", (uint64_t)metrics.accessGranted);
    accessControl.pushKV("access_denied", (uint64_t)metrics.accessDenied);
    accessControl.pushKV("denial_rate", metrics.accessDenialRate);
    result.pushKV("access_control", accessControl);
    
    // Window info
    UniValue window(UniValue::VOBJ);
    window.pushKV("start_block", metrics.startBlockHeight);
    window.pushKV("end_block", metrics.endBlockHeight);
    window.pushKV("start_time", metrics.windowStart);
    window.pushKV("end_time", metrics.windowEnd);
    result.pushKV("window", window);

    return result;
}


/**
 * RPC: getsecurityevents
 * 
 * Get recent security events.
 */
UniValue getsecurityevents(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getsecurityevents ( count \"type\" )\n"
            "\nGet recent security events.\n"
            "\nArguments:\n"
            "1. count    (numeric, optional, default=100) Number of events to return\n"
            "2. \"type\"   (string, optional) Filter by event type\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"event_id\": n,\n"
            "    \"type\": \"xxx\",\n"
            "    \"severity\": \"xxx\",\n"
            "    \"timestamp\": n,\n"
            "    \"block_height\": n,\n"
            "    \"description\": \"xxx\",\n"
            "    \"primary_address\": \"xxx\",\n"
            "    \"tx_hash\": \"xxx\",\n"
            "    \"metadata\": {...}\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getsecurityevents", "")
            + HelpExampleCli("getsecurityevents", "50")
            + HelpExampleCli("getsecurityevents", "50 \"FRAUD_ATTEMPT_DETECTED\"")
        );

    if (!CVM::g_securityAudit) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Security audit system not initialized");
    }

    size_t count = 100;
    if (request.params.size() >= 1) {
        count = request.params[0].get_int();
    }

    std::vector<CVM::SecurityEvent> events = CVM::g_securityAudit->GetRecentEvents(count);

    UniValue result(UniValue::VARR);
    for (const auto& event : events) {
        UniValue eventObj(UniValue::VOBJ);
        eventObj.pushKV("event_id", (uint64_t)event.eventId);
        eventObj.pushKV("type", event.GetTypeString());
        eventObj.pushKV("severity", event.GetSeverityString());
        eventObj.pushKV("timestamp", event.timestamp);
        eventObj.pushKV("block_height", event.blockHeight);
        eventObj.pushKV("description", event.description);
        
        if (!event.primaryAddress.IsNull()) {
            eventObj.pushKV("primary_address", event.primaryAddress.GetHex());
        }
        if (!event.secondaryAddress.IsNull()) {
            eventObj.pushKV("secondary_address", event.secondaryAddress.GetHex());
        }
        if (!event.txHash.IsNull()) {
            eventObj.pushKV("tx_hash", event.txHash.GetHex());
        }
        
        if (event.delta != 0) {
            eventObj.pushKV("old_value", event.oldValue);
            eventObj.pushKV("new_value", event.newValue);
            eventObj.pushKV("delta", event.delta);
        }
        
        UniValue metadata(UniValue::VOBJ);
        for (const auto& pair : event.metadata) {
            metadata.pushKV(pair.first, pair.second);
        }
        eventObj.pushKV("metadata", metadata);
        
        result.push_back(eventObj);
    }

    return result;
}

/**
 * RPC: getanomalyalerts
 * 
 * Get active anomaly alerts.
 */
UniValue getanomalyalerts(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getanomalyalerts ( \"address\" )\n"
            "\nGet active anomaly alerts.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, optional) Filter by address\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alert_id\": n,\n"
            "    \"type\": \"xxx\",\n"
            "    \"primary_address\": \"xxx\",\n"
            "    \"severity\": n,\n"
            "    \"confidence\": n,\n"
            "    \"description\": \"xxx\",\n"
            "    \"evidence\": [...],\n"
            "    \"timestamp\": n,\n"
            "    \"block_height\": n,\n"
            "    \"acknowledged\": true|false,\n"
            "    \"resolved\": true|false\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getanomalyalerts", "")
            + HelpExampleCli("getanomalyalerts", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\"")
        );

    if (!CVM::g_anomalyDetector) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Anomaly detector not initialized");
    }

    std::vector<CVM::AnomalyAlert> alerts;
    
    if (request.params.size() >= 1) {
        std::string addressStr = request.params[0].get_str();
        uint160 address;
        address.SetHex(addressStr);
        alerts = CVM::g_anomalyDetector->GetAlertsForAddress(address);
    } else {
        alerts = CVM::g_anomalyDetector->GetActiveAlerts();
    }

    UniValue result(UniValue::VARR);
    for (const auto& alert : alerts) {
        UniValue alertObj(UniValue::VOBJ);
        alertObj.pushKV("alert_id", (uint64_t)alert.alertId);
        
        std::string typeStr;
        switch (alert.type) {
            case CVM::AnomalyType::REPUTATION_SPIKE: typeStr = "REPUTATION_SPIKE"; break;
            case CVM::AnomalyType::REPUTATION_DROP: typeStr = "REPUTATION_DROP"; break;
            case CVM::AnomalyType::REPUTATION_OSCILLATION: typeStr = "REPUTATION_OSCILLATION"; break;
            case CVM::AnomalyType::VALIDATOR_SLOW_RESPONSE: typeStr = "VALIDATOR_SLOW_RESPONSE"; break;
            case CVM::AnomalyType::VALIDATOR_ERRATIC_TIMING: typeStr = "VALIDATOR_ERRATIC_TIMING"; break;
            case CVM::AnomalyType::VALIDATOR_BIAS: typeStr = "VALIDATOR_BIAS"; break;
            case CVM::AnomalyType::VOTE_MANIPULATION: typeStr = "VOTE_MANIPULATION"; break;
            case CVM::AnomalyType::VOTE_EXTREME_BIAS: typeStr = "VOTE_EXTREME_BIAS"; break;
            case CVM::AnomalyType::TRUST_GRAPH_MANIPULATION: typeStr = "TRUST_GRAPH_MANIPULATION"; break;
            case CVM::AnomalyType::SYBIL_CLUSTER: typeStr = "SYBIL_CLUSTER"; break;
            case CVM::AnomalyType::COORDINATED_ATTACK: typeStr = "COORDINATED_ATTACK"; break;
            default: typeStr = "UNKNOWN"; break;
        }
        alertObj.pushKV("type", typeStr);
        
        alertObj.pushKV("primary_address", alert.primaryAddress.GetHex());
        alertObj.pushKV("severity", alert.severity);
        alertObj.pushKV("confidence", alert.confidence);
        alertObj.pushKV("description", alert.description);
        
        UniValue evidence(UniValue::VARR);
        for (const auto& e : alert.evidence) {
            evidence.push_back(e);
        }
        alertObj.pushKV("evidence", evidence);
        
        if (!alert.relatedAddresses.empty()) {
            UniValue related(UniValue::VARR);
            for (const auto& addr : alert.relatedAddresses) {
                related.push_back(addr.GetHex());
            }
            alertObj.pushKV("related_addresses", related);
        }
        
        alertObj.pushKV("timestamp", alert.timestamp);
        alertObj.pushKV("block_height", alert.blockHeight);
        alertObj.pushKV("acknowledged", alert.acknowledged);
        alertObj.pushKV("resolved", alert.resolved);
        
        result.push_back(alertObj);
    }

    return result;
}

/**
 * RPC: acknowledgeanomalyalert
 * 
 * Acknowledge an anomaly alert.
 */
UniValue acknowledgeanomalyalert(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "acknowledgeanomalyalert alertid\n"
            "\nAcknowledge an anomaly alert.\n"
            "\nArguments:\n"
            "1. alertid    (numeric, required) Alert ID to acknowledge\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"alert_id\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("acknowledgeanomalyalert", "123")
        );

    if (!CVM::g_anomalyDetector) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Anomaly detector not initialized");
    }

    uint64_t alertId = request.params[0].get_int64();
    bool success = CVM::g_anomalyDetector->AcknowledgeAlert(alertId);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("alert_id", alertId);

    return result;
}

/**
 * RPC: resolveanomalyalert
 * 
 * Resolve an anomaly alert.
 */
UniValue resolveanomalyalert(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "resolveanomalyalert alertid \"resolution\"\n"
            "\nResolve an anomaly alert.\n"
            "\nArguments:\n"
            "1. alertid       (numeric, required) Alert ID to resolve\n"
            "2. \"resolution\"  (string, required) Resolution description\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"alert_id\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("resolveanomalyalert", "123 \"False positive - legitimate activity\"")
        );

    if (!CVM::g_anomalyDetector) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Anomaly detector not initialized");
    }

    uint64_t alertId = request.params[0].get_int64();
    std::string resolution = request.params[1].get_str();
    bool success = CVM::g_anomalyDetector->ResolveAlert(alertId, resolution);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("alert_id", alertId);

    return result;
}

/**
 * RPC: getvalidatorstats
 * 
 * Get validator statistics for security monitoring.
 */
UniValue getvalidatorstats_security(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getvalidatorstats_security ( \"address\" )\n"
            "\nGet validator statistics for security monitoring.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, optional) Validator address\n"
            "\nResult:\n"
            "{\n"
            "  \"validator_address\": \"xxx\",\n"
            "  \"total_validations\": n,\n"
            "  \"accurate_validations\": n,\n"
            "  \"inaccurate_validations\": n,\n"
            "  \"abstentions\": n,\n"
            "  \"accuracy_rate\": n,\n"
            "  \"reputation\": n,\n"
            "  \"last_activity\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getvalidatorstats_security", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\"")
        );

    // This would integrate with the HAT consensus validator
    // For now, return placeholder data
    UniValue result(UniValue::VOBJ);
    result.pushKV("message", "Validator stats available through HAT consensus system");
    
    return result;
}

/**
 * RPC: setsecurityconfig
 * 
 * Configure security monitoring settings.
 */
UniValue setsecurityconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
            "setsecurityconfig \"setting\" value\n"
            "\nConfigure security monitoring settings.\n"
            "\nArguments:\n"
            "1. \"setting\"   (string, required) Setting name\n"
            "2. value       (varies, required) Setting value\n"
            "\nAvailable settings:\n"
            "  reputation_threshold - Z-score threshold for reputation anomalies (default: 2.5)\n"
            "  validator_threshold - Z-score threshold for validator anomalies (default: 2.0)\n"
            "  coordination_threshold - Threshold for coordinated attack detection (default: 0.8)\n"
            "  logging_level - Minimum severity level (DEBUG, INFO, WARNING, ERROR, CRITICAL)\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"setting\": \"xxx\",\n"
            "  \"value\": xxx\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("setsecurityconfig", "\"reputation_threshold\" 3.0")
            + HelpExampleCli("setsecurityconfig", "\"logging_level\" \"WARNING\"")
        );

    std::string setting = request.params[0].get_str();
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("setting", setting);
    
    if (setting == "reputation_threshold" || setting == "validator_threshold" || 
        setting == "coordination_threshold") {
        double value = request.params[1].get_real();
        
        if (CVM::g_anomalyDetector) {
            if (setting == "reputation_threshold") {
                CVM::g_anomalyDetector->SetThresholds(value, 2.0, 0.8);
            } else if (setting == "validator_threshold") {
                CVM::g_anomalyDetector->SetThresholds(2.5, value, 0.8);
            } else {
                CVM::g_anomalyDetector->SetThresholds(2.5, 2.0, value);
            }
        }
        
        result.pushKV("success", true);
        result.pushKV("value", value);
    } else if (setting == "logging_level") {
        std::string levelStr = request.params[1].get_str();
        CVM::SecuritySeverity level;
        
        if (levelStr == "DEBUG") level = CVM::SecuritySeverity::DEBUG;
        else if (levelStr == "INFO") level = CVM::SecuritySeverity::INFO;
        else if (levelStr == "WARNING") level = CVM::SecuritySeverity::WARNING;
        else if (levelStr == "ERROR") level = CVM::SecuritySeverity::ERROR;
        else if (levelStr == "CRITICAL") level = CVM::SecuritySeverity::CRITICAL;
        else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid logging level");
        }
        
        if (CVM::g_securityAudit) {
            CVM::g_securityAudit->SetLoggingLevel(level);
        }
        
        result.pushKV("success", true);
        result.pushKV("value", levelStr);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown setting: " + setting);
    }

    return result;
}

/**
 * RPC: getaccesscontrolstats
 * 
 * Get access control statistics.
 * Implements requirement 10.4: Log all trust score queries and modifications,
 * record all reputation-gated operation attempts.
 */
UniValue getaccesscontrolstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getaccesscontrolstats ( startblock endblock )\n"
            "\nGet access control statistics for the CVM system.\n"
            "\nArguments:\n"
            "1. startblock    (numeric, optional) Start block height for stats window\n"
            "2. endblock      (numeric, optional) End block height for stats window\n"
            "\nResult:\n"
            "{\n"
            "  \"total_access_attempts\": n,\n"
            "  \"total_granted\": n,\n"
            "  \"total_denied\": n,\n"
            "  \"overall_grant_rate\": n,\n"
            "  \"average_reputation_deficit\": n,\n"
            "  \"by_operation_type\": {\n"
            "    \"TRUST_SCORE_QUERY\": { \"total\": n, \"granted\": n, \"denied\": n },\n"
            "    ...\n"
            "  },\n"
            "  \"by_decision\": {\n"
            "    \"GRANTED\": n,\n"
            "    \"DENIED_INSUFFICIENT_REPUTATION\": n,\n"
            "    ...\n"
            "  },\n"
            "  \"window\": {\n"
            "    \"start_block\": n,\n"
            "    \"end_block\": n,\n"
            "    \"start_time\": n,\n"
            "    \"end_time\": n\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccesscontrolstats", "")
            + HelpExampleCli("getaccesscontrolstats", "100000 100100")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    CVM::AccessControlStats stats;
    
    if (request.params.size() >= 2) {
        int startBlock = request.params[0].get_int();
        int endBlock = request.params[1].get_int();
        stats = CVM::g_accessControlAuditor->GetStatisticsForBlockRange(startBlock, endBlock);
    } else {
        stats = CVM::g_accessControlAuditor->GetStatistics();
    }

    UniValue result(UniValue::VOBJ);
    
    result.pushKV("total_access_attempts", (uint64_t)stats.totalAccessAttempts);
    result.pushKV("total_granted", (uint64_t)stats.totalGranted);
    result.pushKV("total_denied", (uint64_t)stats.totalDenied);
    result.pushKV("overall_grant_rate", stats.overallGrantRate);
    result.pushKV("average_reputation_deficit", stats.averageReputationDeficit);
    
    // By operation type
    UniValue byOpType(UniValue::VOBJ);
    for (const auto& pair : stats.totalRequests) {
        UniValue opStats(UniValue::VOBJ);
        opStats.pushKV("total", (uint64_t)pair.second);
        
        auto grantedIt = stats.grantedRequests.find(pair.first);
        opStats.pushKV("granted", grantedIt != stats.grantedRequests.end() ? (uint64_t)grantedIt->second : 0);
        
        auto deniedIt = stats.deniedRequests.find(pair.first);
        opStats.pushKV("denied", deniedIt != stats.deniedRequests.end() ? (uint64_t)deniedIt->second : 0);
        
        // Get operation type string
        CVM::AccessControlAuditEntry tempEntry;
        tempEntry.operationType = pair.first;
        byOpType.pushKV(tempEntry.GetOperationTypeString(), opStats);
    }
    result.pushKV("by_operation_type", byOpType);
    
    // By decision
    UniValue byDecision(UniValue::VOBJ);
    for (const auto& pair : stats.decisionCounts) {
        CVM::AccessControlAuditEntry tempEntry;
        tempEntry.decision = pair.first;
        byDecision.pushKV(tempEntry.GetDecisionString(), (uint64_t)pair.second);
    }
    result.pushKV("by_decision", byDecision);
    
    // Window info
    UniValue window(UniValue::VOBJ);
    window.pushKV("start_block", stats.startBlockHeight);
    window.pushKV("end_block", stats.endBlockHeight);
    window.pushKV("start_time", stats.windowStart);
    window.pushKV("end_time", stats.windowEnd);
    result.pushKV("window", window);

    return result;
}

/**
 * RPC: getaccesscontrolentries
 * 
 * Get recent access control audit entries.
 */
UniValue getaccesscontrolentries(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getaccesscontrolentries ( count \"filter\" )\n"
            "\nGet recent access control audit entries.\n"
            "\nArguments:\n"
            "1. count     (numeric, optional, default=100) Number of entries to return\n"
            "2. \"filter\"  (string, optional) Filter: \"denied\", \"granted\", or operation type\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"entry_id\": n,\n"
            "    \"operation_type\": \"xxx\",\n"
            "    \"decision\": \"xxx\",\n"
            "    \"requester_address\": \"xxx\",\n"
            "    \"target_address\": \"xxx\",\n"
            "    \"operation_name\": \"xxx\",\n"
            "    \"required_reputation\": n,\n"
            "    \"actual_reputation\": n,\n"
            "    \"reputation_deficit\": n,\n"
            "    \"denial_reason\": \"xxx\",\n"
            "    \"timestamp\": n,\n"
            "    \"block_height\": n,\n"
            "    \"tx_hash\": \"xxx\",\n"
            "    \"metadata\": {...}\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccesscontrolentries", "")
            + HelpExampleCli("getaccesscontrolentries", "50 \"denied\"")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    size_t count = 100;
    if (request.params.size() >= 1) {
        count = request.params[0].get_int();
    }

    std::vector<CVM::AccessControlAuditEntry> entries;
    
    if (request.params.size() >= 2) {
        std::string filter = request.params[1].get_str();
        if (filter == "denied") {
            entries = CVM::g_accessControlAuditor->GetDeniedEntries(count);
        } else {
            // Try to parse as operation type
            entries = CVM::g_accessControlAuditor->GetRecentEntries(count);
        }
    } else {
        entries = CVM::g_accessControlAuditor->GetRecentEntries(count);
    }

    UniValue result(UniValue::VARR);
    for (const auto& entry : entries) {
        UniValue entryObj(UniValue::VOBJ);
        entryObj.pushKV("entry_id", (uint64_t)entry.entryId);
        entryObj.pushKV("operation_type", entry.GetOperationTypeString());
        entryObj.pushKV("decision", entry.GetDecisionString());
        
        if (!entry.requesterAddress.IsNull()) {
            entryObj.pushKV("requester_address", entry.requesterAddress.GetHex());
        }
        if (!entry.targetAddress.IsNull()) {
            entryObj.pushKV("target_address", entry.targetAddress.GetHex());
        }
        if (!entry.contractAddress.IsNull()) {
            entryObj.pushKV("contract_address", entry.contractAddress.GetHex());
        }
        
        entryObj.pushKV("operation_name", entry.operationName);
        entryObj.pushKV("resource_id", entry.resourceId);
        entryObj.pushKV("required_reputation", entry.requiredReputation);
        entryObj.pushKV("actual_reputation", entry.actualReputation);
        entryObj.pushKV("reputation_deficit", entry.reputationDeficit);
        
        if (!entry.denialReason.empty()) {
            entryObj.pushKV("denial_reason", entry.denialReason);
        }
        
        entryObj.pushKV("timestamp", entry.timestamp);
        entryObj.pushKV("block_height", entry.blockHeight);
        
        if (!entry.txHash.IsNull()) {
            entryObj.pushKV("tx_hash", entry.txHash.GetHex());
        }
        
        if (entry.requestsInWindow > 0) {
            entryObj.pushKV("requests_in_window", (uint64_t)entry.requestsInWindow);
            entryObj.pushKV("max_requests_allowed", (uint64_t)entry.maxRequestsAllowed);
        }
        
        UniValue metadata(UniValue::VOBJ);
        for (const auto& pair : entry.metadata) {
            metadata.pushKV(pair.first, pair.second);
        }
        entryObj.pushKV("metadata", metadata);
        
        result.push_back(entryObj);
    }

    return result;
}

/**
 * RPC: getaccesscontrolforaddress
 * 
 * Get access control entries for a specific address.
 */
UniValue getaccesscontrolforaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getaccesscontrolforaddress \"address\" ( count )\n"
            "\nGet access control entries for a specific address.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, required) Address to query\n"
            "2. count       (numeric, optional, default=100) Number of entries to return\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"entry_id\": n,\n"
            "    \"operation_type\": \"xxx\",\n"
            "    \"decision\": \"xxx\",\n"
            "    ...\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccesscontrolforaddress", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\"")
            + HelpExampleCli("getaccesscontrolforaddress", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\" 50")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    uint160 address;
    address.SetHex(addressStr);
    
    size_t count = 100;
    if (request.params.size() >= 2) {
        count = request.params[1].get_int();
    }

    std::vector<CVM::AccessControlAuditEntry> entries = 
        CVM::g_accessControlAuditor->GetEntriesForAddress(address, count);

    UniValue result(UniValue::VARR);
    for (const auto& entry : entries) {
        UniValue entryObj(UniValue::VOBJ);
        entryObj.pushKV("entry_id", (uint64_t)entry.entryId);
        entryObj.pushKV("operation_type", entry.GetOperationTypeString());
        entryObj.pushKV("decision", entry.GetDecisionString());
        entryObj.pushKV("operation_name", entry.operationName);
        entryObj.pushKV("required_reputation", entry.requiredReputation);
        entryObj.pushKV("actual_reputation", entry.actualReputation);
        entryObj.pushKV("timestamp", entry.timestamp);
        entryObj.pushKV("block_height", entry.blockHeight);
        
        if (!entry.denialReason.empty()) {
            entryObj.pushKV("denial_reason", entry.denialReason);
        }
        
        result.push_back(entryObj);
    }

    return result;
}

/**
 * RPC: getblacklist
 * 
 * Get the current blacklist of addresses.
 */
UniValue getblacklist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getblacklist\n"
            "\nGet the current blacklist of addresses.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",\n"
            "    \"reason\": \"xxx\"\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblacklist", "")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    auto entries = CVM::g_accessControlAuditor->GetBlacklistEntries();

    UniValue result(UniValue::VARR);
    for (const auto& entry : entries) {
        UniValue entryObj(UniValue::VOBJ);
        entryObj.pushKV("address", entry.first.GetHex());
        entryObj.pushKV("reason", entry.second);
        result.push_back(entryObj);
    }

    return result;
}

/**
 * RPC: addtoblacklist
 * 
 * Add an address to the blacklist.
 */
UniValue addtoblacklist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "addtoblacklist \"address\" \"reason\" ( duration )\n"
            "\nAdd an address to the blacklist.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, required) Address to blacklist\n"
            "2. \"reason\"    (string, required) Reason for blacklisting\n"
            "3. duration    (numeric, optional, default=-1) Duration in seconds (-1 = permanent)\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"address\": \"xxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("addtoblacklist", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\" \"Fraud attempt\"")
            + HelpExampleCli("addtoblacklist", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\" \"Temporary ban\" 86400")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    uint160 address;
    address.SetHex(addressStr);
    
    std::string reason = request.params[1].get_str();
    
    int64_t duration = -1;
    if (request.params.size() >= 3) {
        duration = request.params[2].get_int64();
    }

    CVM::g_accessControlAuditor->AddToBlacklist(address, reason, duration);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("address", addressStr);

    return result;
}

/**
 * RPC: removefromblacklist
 * 
 * Remove an address from the blacklist.
 */
UniValue removefromblacklist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "removefromblacklist \"address\"\n"
            "\nRemove an address from the blacklist.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, required) Address to remove from blacklist\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"address\": \"xxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("removefromblacklist", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\"")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    uint160 address;
    address.SetHex(addressStr);

    CVM::g_accessControlAuditor->RemoveFromBlacklist(address);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("address", addressStr);

    return result;
}

/**
 * RPC: setratelimit
 * 
 * Set rate limit for an operation type.
 */
UniValue setratelimit(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "setratelimit \"operation\" maxrequests windowseconds\n"
            "\nSet rate limit for an operation type.\n"
            "\nArguments:\n"
            "1. \"operation\"     (string, required) Operation type\n"
            "2. maxrequests     (numeric, required) Maximum requests allowed\n"
            "3. windowseconds   (numeric, required) Time window in seconds\n"
            "\nAvailable operations:\n"
            "  TRUST_SCORE_QUERY, TRUST_SCORE_MODIFICATION, REPUTATION_GATED_CALL,\n"
            "  GAS_DISCOUNT_CLAIM, FREE_GAS_CLAIM, VALIDATOR_REGISTRATION,\n"
            "  CONTRACT_DEPLOYMENT, CONTRACT_CALL, DAO_VOTE\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"operation\": \"xxx\",\n"
            "  \"max_requests\": n,\n"
            "  \"window_seconds\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("setratelimit", "\"CONTRACT_DEPLOYMENT\" 10 3600")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    std::string operationStr = request.params[0].get_str();
    uint32_t maxRequests = request.params[1].get_int();
    int64_t windowSeconds = request.params[2].get_int64();

    // Map operation string to enum
    CVM::AccessOperationType opType;
    if (operationStr == "TRUST_SCORE_QUERY") opType = CVM::AccessOperationType::TRUST_SCORE_QUERY;
    else if (operationStr == "TRUST_SCORE_MODIFICATION") opType = CVM::AccessOperationType::TRUST_SCORE_MODIFICATION;
    else if (operationStr == "REPUTATION_GATED_CALL") opType = CVM::AccessOperationType::REPUTATION_GATED_CALL;
    else if (operationStr == "GAS_DISCOUNT_CLAIM") opType = CVM::AccessOperationType::GAS_DISCOUNT_CLAIM;
    else if (operationStr == "FREE_GAS_CLAIM") opType = CVM::AccessOperationType::FREE_GAS_CLAIM;
    else if (operationStr == "VALIDATOR_REGISTRATION") opType = CVM::AccessOperationType::VALIDATOR_REGISTRATION;
    else if (operationStr == "CONTRACT_DEPLOYMENT") opType = CVM::AccessOperationType::CONTRACT_DEPLOYMENT;
    else if (operationStr == "CONTRACT_CALL") opType = CVM::AccessOperationType::CONTRACT_CALL;
    else if (operationStr == "DAO_VOTE") opType = CVM::AccessOperationType::DAO_VOTE;
    else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown operation type: " + operationStr);
    }

    CVM::g_accessControlAuditor->SetRateLimit(opType, maxRequests, windowSeconds);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("operation", operationStr);
    result.pushKV("max_requests", (uint64_t)maxRequests);
    result.pushKV("window_seconds", windowSeconds);

    return result;
}

/**
 * RPC: setminreputation
 * 
 * Set minimum reputation requirement for an operation type.
 */
UniValue setminreputation(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "setminreputation \"operation\" minreputation\n"
            "\nSet minimum reputation requirement for an operation type.\n"
            "\nArguments:\n"
            "1. \"operation\"     (string, required) Operation type\n"
            "2. minreputation   (numeric, required) Minimum reputation (0-100)\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"operation\": \"xxx\",\n"
            "  \"min_reputation\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("setminreputation", "\"CONTRACT_DEPLOYMENT\" 50")
        );

    if (!CVM::g_accessControlAuditor) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Access control auditor not initialized");
    }

    std::string operationStr = request.params[0].get_str();
    int16_t minReputation = request.params[1].get_int();

    if (minReputation < 0 || minReputation > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum reputation must be between 0 and 100");
    }

    // Map operation string to enum
    CVM::AccessOperationType opType;
    if (operationStr == "TRUST_SCORE_QUERY") opType = CVM::AccessOperationType::TRUST_SCORE_QUERY;
    else if (operationStr == "TRUST_SCORE_MODIFICATION") opType = CVM::AccessOperationType::TRUST_SCORE_MODIFICATION;
    else if (operationStr == "REPUTATION_GATED_CALL") opType = CVM::AccessOperationType::REPUTATION_GATED_CALL;
    else if (operationStr == "GAS_DISCOUNT_CLAIM") opType = CVM::AccessOperationType::GAS_DISCOUNT_CLAIM;
    else if (operationStr == "FREE_GAS_CLAIM") opType = CVM::AccessOperationType::FREE_GAS_CLAIM;
    else if (operationStr == "VALIDATOR_REGISTRATION") opType = CVM::AccessOperationType::VALIDATOR_REGISTRATION;
    else if (operationStr == "CONTRACT_DEPLOYMENT") opType = CVM::AccessOperationType::CONTRACT_DEPLOYMENT;
    else if (operationStr == "CONTRACT_CALL") opType = CVM::AccessOperationType::CONTRACT_CALL;
    else if (operationStr == "DAO_VOTE") opType = CVM::AccessOperationType::DAO_VOTE;
    else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown operation type: " + operationStr);
    }

    CVM::g_accessControlAuditor->SetMinimumReputation(opType, minReputation);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("operation", operationStr);
    result.pushKV("min_reputation", minReputation);

    return result;
}

/**
 * RPC: getdosprotectionstats
 * 
 * Get DoS protection statistics.
 * Implements requirement 10.2, 16.1, 16.4: Network security and DoS protection.
 */
UniValue getdosprotectionstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getdosprotectionstats\n"
            "\nGet DoS protection statistics.\n"
            "\nResult:\n"
            "{\n"
            "  \"total_transactions_checked\": n,\n"
            "  \"transactions_rate_limited\": n,\n"
            "  \"deployments_rate_limited\": n,\n"
            "  \"malicious_contracts_detected\": n,\n"
            "  \"validation_requests_rate_limited\": n,\n"
            "  \"validator_timeouts\": n,\n"
            "  \"p2p_messages_rate_limited\": n,\n"
            "  \"rpc_calls_rate_limited\": n,\n"
            "  \"tracked_addresses\": n,\n"
            "  \"banned_addresses\": n,\n"
            "  \"tracked_validators\": n,\n"
            "  \"pending_validator_responses\": n,\n"
            "  \"tracked_peers\": n,\n"
            "  \"current_bandwidth_usage\": n,\n"
            "  \"malicious_patterns_registered\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getdosprotectionstats", "")
            + HelpExampleRpc("getdosprotectionstats", "")
        );

    if (!CVM::g_dosProtection) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DoS protection system not initialized");
    }

    return CVM::g_dosProtection->GetStatistics();
}

/**
 * RPC: getbannedaddresses
 * 
 * Get list of banned addresses.
 */
UniValue getbannedaddresses(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getbannedaddresses\n"
            "\nGet list of banned addresses.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",\n"
            "    \"ban_until\": n\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getbannedaddresses", "")
        );

    if (!CVM::g_dosProtection) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DoS protection system not initialized");
    }

    auto banned = CVM::g_dosProtection->GetBannedAddresses();

    UniValue result(UniValue::VARR);
    for (const auto& entry : banned) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("address", entry.first.GetHex());
        obj.pushKV("ban_until", entry.second);
        result.push_back(obj);
    }

    return result;
}

/**
 * RPC: banaddress
 * 
 * Ban an address from submitting transactions.
 */
UniValue banaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "banaddress \"address\" duration ( \"reason\" )\n"
            "\nBan an address from submitting transactions.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, required) Address to ban\n"
            "2. duration    (numeric, required) Ban duration in seconds\n"
            "3. \"reason\"    (string, optional) Reason for ban\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"address\": \"xxx\",\n"
            "  \"duration\": n\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("banaddress", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\" 3600")
            + HelpExampleCli("banaddress", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\" 3600 \"Spam transactions\"")
        );

    if (!CVM::g_dosProtection) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DoS protection system not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    uint160 address;
    address.SetHex(addressStr);
    
    uint32_t duration = request.params[1].get_int();
    
    std::string reason = "Manual ban via RPC";
    if (request.params.size() >= 3) {
        reason = request.params[2].get_str();
    }

    CVM::g_dosProtection->BanAddress(address, duration, reason);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("address", addressStr);
    result.pushKV("duration", (uint64_t)duration);

    return result;
}

/**
 * RPC: unbanaddress
 * 
 * Remove ban from an address.
 */
UniValue unbanaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "unbanaddress \"address\"\n"
            "\nRemove ban from an address.\n"
            "\nArguments:\n"
            "1. \"address\"   (string, required) Address to unban\n"
            "\nResult:\n"
            "{\n"
            "  \"success\": true|false,\n"
            "  \"address\": \"xxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("unbanaddress", "\"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\"")
        );

    if (!CVM::g_dosProtection) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DoS protection system not initialized");
    }

    std::string addressStr = request.params[0].get_str();
    uint160 address;
    address.SetHex(addressStr);

    // Clear ban by setting duration to 0
    CVM::g_dosProtection->BanAddress(address, 0, "Unbanned via RPC");

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", true);
    result.pushKV("address", addressStr);

    return result;
}

/**
 * RPC: analyzebytecode
 * 
 * Analyze bytecode for malicious patterns.
 */
UniValue analyzebytecode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "analyzebytecode \"bytecode\"\n"
            "\nAnalyze bytecode for malicious patterns.\n"
            "\nArguments:\n"
            "1. \"bytecode\"   (string, required) Hex-encoded bytecode to analyze\n"
            "\nResult:\n"
            "{\n"
            "  \"is_malicious\": true|false,\n"
            "  \"has_infinite_loop\": true|false,\n"
            "  \"has_resource_exhaustion\": true|false,\n"
            "  \"has_reentrancy\": true|false,\n"
            "  \"has_self_destruct\": true|false,\n"
            "  \"has_unbounded_loop\": true|false,\n"
            "  \"risk_score\": n,\n"
            "  \"detected_patterns\": [...],\n"
            "  \"analysis_report\": \"xxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("analyzebytecode", "\"6080604052...\"")
        );

    if (!CVM::g_dosProtection) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DoS protection system not initialized");
    }

    std::string bytecodeHex = request.params[0].get_str();
    
    // Convert hex to bytes
    std::vector<uint8_t> bytecode;
    for (size_t i = 0; i < bytecodeHex.length(); i += 2) {
        std::string byteStr = bytecodeHex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
        bytecode.push_back(byte);
    }

    auto analysisResult = CVM::g_dosProtection->AnalyzeBytecode(bytecode);

    UniValue result(UniValue::VOBJ);
    result.pushKV("is_malicious", analysisResult.isMalicious);
    result.pushKV("has_infinite_loop", analysisResult.hasInfiniteLoop);
    result.pushKV("has_resource_exhaustion", analysisResult.hasResourceExhaustion);
    result.pushKV("has_reentrancy", analysisResult.hasReentrancy);
    result.pushKV("has_self_destruct", analysisResult.hasSelfDestruct);
    result.pushKV("has_unbounded_loop", analysisResult.hasUnboundedLoop);
    result.pushKV("risk_score", analysisResult.riskScore);
    
    UniValue patterns(UniValue::VARR);
    for (const auto& pattern : analysisResult.detectedPatterns) {
        patterns.push_back(pattern);
    }
    result.pushKV("detected_patterns", patterns);
    
    result.pushKV("analysis_report", analysisResult.analysisReport);

    return result;
}

// RPC command table
static const CRPCCommand commands[] =
{
    //  category              name                          actor (function)           argNames
    //  --------------------- ---------------------------- -------------------------- ----------
    { "security",           "getsecuritymetrics",         &getsecuritymetrics,       {"startblock", "endblock"} },
    { "security",           "getsecurityevents",          &getsecurityevents,        {"count", "type"} },
    { "security",           "getanomalyalerts",           &getanomalyalerts,         {"address"} },
    { "security",           "acknowledgeanomalyalert",    &acknowledgeanomalyalert,  {"alertid"} },
    { "security",           "resolveanomalyalert",        &resolveanomalyalert,      {"alertid", "resolution"} },
    { "security",           "getvalidatorstats_security", &getvalidatorstats_security, {"address"} },
    { "security",           "setsecurityconfig",          &setsecurityconfig,        {"setting", "value"} },
    { "security",           "getaccesscontrolstats",      &getaccesscontrolstats,    {"startblock", "endblock"} },
    { "security",           "getaccesscontrolentries",    &getaccesscontrolentries,  {"count", "filter"} },
    { "security",           "getaccesscontrolforaddress", &getaccesscontrolforaddress, {"address", "count"} },
    { "security",           "getblacklist",               &getblacklist,             {} },
    { "security",           "addtoblacklist",             &addtoblacklist,           {"address", "reason", "duration"} },
    { "security",           "removefromblacklist",        &removefromblacklist,      {"address"} },
    { "security",           "setratelimit",               &setratelimit,             {"operation", "maxrequests", "windowseconds"} },
    { "security",           "setminreputation",           &setminreputation,         {"operation", "minreputation"} },
    { "security",           "getdosprotectionstats",      &getdosprotectionstats,    {} },
    { "security",           "getbannedaddresses",         &getbannedaddresses,       {} },
    { "security",           "banaddress",                 &banaddress,               {"address", "duration", "reason"} },
    { "security",           "unbanaddress",               &unbanaddress,             {"address"} },
    { "security",           "analyzebytecode",            &analyzebytecode,          {"bytecode"} },
};

void RegisterSecurityRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
