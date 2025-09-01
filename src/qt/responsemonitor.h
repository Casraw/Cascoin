// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RESPONSEMONITOR_H
#define BITCOIN_QT_RESPONSEMONITOR_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <atomic>

/**
 * Monitor GUI responsiveness and adjust system behavior accordingly
 */
class ResponseMonitor : public QObject
{
    Q_OBJECT

public:
    explicit ResponseMonitor(QObject *parent = nullptr);
    ~ResponseMonitor();
    
    /**
     * Start monitoring GUI responsiveness
     */
    void startMonitoring();
    
    /**
     * Stop monitoring
     */
    void stopMonitoring();
    
    /**
     * Check if system is currently under high load
     */
    bool isHighLoad() const { return highLoadState.load(); }
    
    /**
     * Get current response time in milliseconds
     */
    int getCurrentResponseTime() const { return lastResponseTime.load(); }

Q_SIGNALS:
    /**
     * Emitted when high CPU load is detected that affects GUI responsiveness
     */
    void highLoadDetected();
    
    /**
     * Emitted when system returns to normal responsiveness
     */
    void normalLoadRestored();

private Q_SLOTS:
    void checkResponseTime();

private:
    QTimer* monitorTimer;
    QElapsedTimer responseTimer;
    std::atomic<bool> highLoadState{false};
    std::atomic<int> lastResponseTime{0};
    
    static const int MONITOR_INTERVAL_MS = 500;  // Check every 500ms
    static const int HIGH_LOAD_THRESHOLD_MS = 100;  // Consider high load if response > 100ms
    static const int NORMAL_LOAD_THRESHOLD_MS = 50;  // Consider normal if response < 50ms
};

#endif // BITCOIN_QT_RESPONSEMONITOR_H