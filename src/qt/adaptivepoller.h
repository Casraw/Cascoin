// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_ADAPTIVEPOLLER_H
#define BITCOIN_QT_ADAPTIVEPOLLER_H

#include <QTimer>
#include <QObject>

/**
 * Adaptive polling timer that adjusts frequency based on system load
 */
class AdaptivePoller : public QObject
{
    Q_OBJECT

public:
    explicit AdaptivePoller(QObject *parent = nullptr);
    
    /**
     * Start adaptive polling with base interval
     * @param baseIntervalMs Base polling interval in milliseconds
     */
    void start(int baseIntervalMs);
    
    /**
     * Stop polling
     */
    void stop();
    
    /**
     * Adjust polling frequency based on system load
     * @param highLoad True if system is under high load
     */
    void adjustForLoad(bool highLoad);

Q_SIGNALS:
    /**
     * Emitted when it's time to poll
     */
    void timeout();

private Q_SLOTS:
    void onTimerTimeout();

private:
    QTimer* timer;
    int baseInterval;
    int currentInterval;
    bool highLoadMode;
    
    static const int HIGH_LOAD_MULTIPLIER = 3;  // Slow down polling by 3x under high load
};

#endif // BITCOIN_QT_ADAPTIVEPOLLER_H