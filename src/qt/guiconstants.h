// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_GUICONSTANTS_H
#define BITCOIN_QT_GUICONSTANTS_H

/* Milliseconds between model updates - increased to reduce CPU load */
static const int MODEL_UPDATE_DELAY = 500;

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* BitcoinGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

static const bool DEFAULT_SPLASHSCREEN = true;

/* Invalid field background style - updated for dark theme */
#define STYLE_INVALID "background:#e53e3e; color:#ffffff; border: 1px solid #c53030;"

/* Transaction list -- unconfirmed transaction - light grey for dark theme */
#define COLOR_UNCONFIRMED QColor(160, 174, 192)
/* Transaction list -- negative amount - bright red for visibility */
#define COLOR_NEGATIVE QColor(248, 113, 113)
/* Transaction list -- bare address (without label) - medium grey */
#define COLOR_BAREADDRESS QColor(203, 213, 224)
/* Transaction list -- TX status decoration - open until date - bright blue */
#define COLOR_TX_STATUS_OPENUNTILDATE QColor(99, 179, 237)
/* Transaction list -- TX status decoration - offline - light grey */
#define COLOR_TX_STATUS_OFFLINE QColor(203, 213, 224)
/* Transaction list -- TX status decoration - danger, tx needs attention - bright red */
#define COLOR_TX_STATUS_DANGER QColor(248, 113, 113)
/* Transaction list -- TX status decoration - default color - white for dark theme */
#define COLOR_BLACK QColor(255, 255, 255)

/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Maximum allowed URI length */
static const int MAX_URI_LENGTH = 255;

/* QRCodeDialog -- size of exported QR Code image */
#define QR_IMAGE_SIZE 300

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "Cascoin"
#define QAPP_ORG_DOMAIN "cascoin.net"
#define QAPP_APP_NAME_DEFAULT "Cascoin-Qt"
#define QAPP_APP_NAME_TESTNET "Cascoin-Qt-testnet"

#endif // BITCOIN_QT_GUICONSTANTS_H
