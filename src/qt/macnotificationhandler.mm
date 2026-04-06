// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "macnotificationhandler.h"

#undef slots
#import <objc/runtime.h>
#include <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>

void MacNotificationHandler::showNotification(const QString &title, const QString &text)
{
    if (@available(macOS 10.14, *)) {
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];

        // Request permission (no-op after first grant/deny)
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                              completionHandler:^(BOOL granted, NSError * _Nullable error) {
            if (!granted) return;

            UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
            content.title = title.toNSString();
            content.body  = text.toNSString();
            content.sound = [UNNotificationSound defaultSound];

            UNNotificationRequest *request =
                [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                     content:content
                                                     trigger:nil];
            [center addNotificationRequest:request withCompletionHandler:nil];
        }];
    }
}

// UNUserNotificationCenter is available on macOS 10.14+
bool MacNotificationHandler::hasUserNotificationCenterSupport(void)
{
    if (@available(macOS 10.14, *)) {
        return true;
    }
    return false;
}


MacNotificationHandler *MacNotificationHandler::instance()
{
    static MacNotificationHandler *s_instance = nullptr;
    if (!s_instance) {
        s_instance = new MacNotificationHandler();
        // No more NSBundle swizzling needed - UNUserNotificationCenter
        // does not require a custom bundle identifier hack.
    }
    return s_instance;
}
