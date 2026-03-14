// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_QT_BEENFTTABLEMODEL_H
#define CASCOIN_QT_BEENFTTABLEMODEL_H

#include <QAbstractTableModel>
#include <QStringList>
#include <mousenft.h>

class WalletModel;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

struct MouseNFTRecord
{
    QString mouseNFTId;
    QString originalBCT;
    int mouseIndex;
    QString currentOwner;
    QString status;
    int maturityHeight;
    int expiryHeight;
    int tokenizedHeight;
    int blocksLeft;
    
    MouseNFTRecord():
        mouseIndex(0), maturityHeight(0), expiryHeight(0), 
        tokenizedHeight(0), blocksLeft(0) {}
};

/** Model for the mouse NFT table in the GUI */
class MouseNFTTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MouseNFTTableModel(WalletModel *parent = 0);
    ~MouseNFTTableModel();

    enum ColumnIndex {
        MouseNFTId = 0,
        OriginalBCT = 1,
        MouseIndex = 2,
        Status = 3,
        Owner = 4,
        BlocksLeft = 5,
        MaturityHeight = 6,
        ExpiryHeight = 7
    };

    /** @name Methods overridden from QAbstractTableModel
        @{*/
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
    /*@}*/

    void setIncludeExpired(bool includeExpired);

private:
    WalletModel *walletModel;
    QStringList columns;
    QList<MouseNFTRecord> cachedMouseNFTList;
    bool includeExpired;
    QTimer *timer;

    /** Update cached mouse NFT list from wallet */
    void updateMouseNFTList();

public Q_SLOTS:
    void updateMouseNFTs();
    void updateMouseNFTListWithData(const QList<MouseNFTRecord>& newRecords);

Q_SIGNALS:
    void mouseNFTsChanged();
};

#endif // CASCOIN_QT_BEENFTTABLEMODEL_H
