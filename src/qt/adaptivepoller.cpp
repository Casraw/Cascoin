// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/adaptivepoller.h>
#include <QDebug>

AdaptivePoller::AdaptivePoller(QObject *parent)
    : QObject(parent)
    , timer(new QTimer(this))
    , baseInterval(250)
    , currentInterval(250)
    , highLoadMode(false)
{
    connect(timer, &QTimer::timeout, this, &AdaptivePoller::onTimerTimeout);
    timer->setSingleShot(false);
}

void AdaptivePoller::start(int baseIntervalMs)
{
    baseInterval = baseIntervalMs;
    currentInterval = baseInterval;
    timer->setInterval(currentInterval);
    timer->start();
}

void AdaptivePoller::stop()
{
    timer->stop();
}

void AdaptivePoller::adjustForLoad(bool highLoad)
{
    if (highLoad != highLoadMode) {
        highLoadMode = highLoad;
        
        int newInterval;
        if (highLoad) {
            // Slow down polling under high load
            newInterval = baseInterval * HIGH_LOAD_MULTIPLIER;
            qDebug() << "AdaptivePoller: Switching to high-load mode, interval:" << newInterval << "ms";
        } else {
            // Return to normal polling
            newInterval = baseInterval;
            qDebug() << "AdaptivePoller: Switching to normal mode, interval:" << newInterval << "ms";
        }
        
        if (newInterval != currentInterval) {
            currentInterval = newInterval;
            timer->setInterval(currentInterval);
        }
    }
}

void AdaptivePoller::onTimerTimeout()
{
    Q_EMIT timeout();
}

#include <qt/moc_adaptivepoller.cpp>