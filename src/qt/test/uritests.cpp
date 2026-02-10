// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

// Valid Cascoin mainnet address (prefix 40 = 'H')
static const QString testAddress = "HQYrRcUkzXrY49kMQ5XiXeDG7mHK2H81Y1";

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    uri.setUrl(QString("cascoin:") + testAddress + QString("?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cascoin:") + testAddress + QString("?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("cascoin:") + testAddress + QString("?label=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.label == QString("Wikipedia Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("cascoin:") + testAddress + QString("?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 10000);

    uri.setUrl(QString("cascoin:") + testAddress + QString("?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 10010000);

    uri.setUrl(QString("cascoin:") + testAddress + QString("?amount=100&label=Wikipedia Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.amount == 1000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    uri.setUrl(QString("cascoin:") + testAddress + QString("?message=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI(QString("cascoin://") + testAddress + QString("?message=Wikipedia Example Address"), &rv));
    QVERIFY(rv.address == testAddress);
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("cascoin:") + testAddress + QString("?req-message=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cascoin:") + testAddress + QString("?amount=1,000&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cascoin:") + testAddress + QString("?amount=1,000.0&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));
}
