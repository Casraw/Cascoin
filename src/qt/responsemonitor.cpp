// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/responsemonitor.h>
#include <QApplication>
#include <QDebug>

ResponseMonitor::ResponseMonitor(QObject *parent)
    : QObject(parent)
    , monitorTimer(new QTimer(this))
{
    connect(monitorTimer, &QTimer::timeout, this, &ResponseMonitor::checkResponseTime);
    monitorTimer->setInterval(MONITOR_INTERVAL_MS);
}

ResponseMonitor::~ResponseMonitor()
{
    stopMonitoring();
}

void ResponseMonitor::startMonitoring()
{
    if (!monitorTimer->isActive()) {
        responseTimer.start();
        monitorTimer->start();
        qDebug() << "ResponseMonitor: Started GUI responsiveness monitoring";
    }
}

void ResponseMonitor::stopMonitoring()
{
    if (monitorTimer->isActive()) {
        monitorTimer->stop();
        qDebug() << "ResponseMonitor: Stopped GUI responsiveness monitoring";
    }
}

void ResponseMonitor::checkResponseTime()
{
    // Measure how long it takes for this timer event to be processed
    // If the GUI thread is busy, this will be delayed
    qint64 elapsed = responseTimer.restart();
    
    // Account for timer interval - anything significantly over the interval
    // indicates the GUI thread was blocked
    int responseDelay = elapsed - MONITOR_INTERVAL_MS;
    lastResponseTime.store(responseDelay);
    
    bool wasHighLoad = highLoadState.load();
    
    if (responseDelay > HIGH_LOAD_THRESHOLD_MS && !wasHighLoad) {
        highLoadState.store(true);
        qDebug() << "ResponseMonitor: High load detected (response delay:" << responseDelay << "ms)";
        Q_EMIT highLoadDetected();
    } else if (responseDelay < NORMAL_LOAD_THRESHOLD_MS && wasHighLoad) {
        highLoadState.store(false);
        qDebug() << "ResponseMonitor: Normal load restored (response delay:" << responseDelay << "ms)";
        Q_EMIT normalLoadRestored();
    }
}

#include "moc_responsemonitor.cpp"