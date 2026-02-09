// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_QT_SAFEINVOKE_H
#define CASCOIN_QT_SAFEINVOKE_H

#include <QMetaObject>
#include <QPointer>
#include <functional>

/**
 * Post a callable back to the Qt main thread, but only execute it if the
 * target QObject is still alive.  This prevents the use-after-free crashes
 * that occur when a std::thread::detach()'d thread posts a lambda via
 * Qt::QueuedConnection to an object that has since been destroyed.
 *
 * Usage (inside a detached std::thread that captured `this`):
 *
 *   QPointer<MyWidget> guard(this);   // capture BEFORE detaching
 *   std::thread([guard, data]() {
 *       // ... background work ...
 *       safeInvoke(guard, [data]() { ... update UI ... });
 *   }).detach();
 */
template <typename Func>
void safeInvoke(QPointer<QObject> guard, Func&& fn)
{
    if (guard.isNull()) return;
    QMetaObject::invokeMethod(guard.data(), [guard, f = std::forward<Func>(fn)]() {
        if (!guard.isNull()) {
            f();
        }
    }, Qt::QueuedConnection);
}

#endif // CASCOIN_QT_SAFEINVOKE_H
