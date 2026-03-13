// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/beenfttablemodel.h>
#include <qt/walletmodel.h>
#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>

#include <QTimer>
#include <QIcon>
#include <QDateTime>
#include <algorithm>

// Column alignments for the table
static int column_alignments[] = {
    Qt::AlignLeft|Qt::AlignVCenter,   /* MouseNFTId */
    Qt::AlignLeft|Qt::AlignVCenter,   /* OriginalBCT */
    Qt::AlignRight|Qt::AlignVCenter,  /* MouseIndex */
    Qt::AlignLeft|Qt::AlignVCenter,   /* Status */
    Qt::AlignLeft|Qt::AlignVCenter,   /* Owner */
    Qt::AlignRight|Qt::AlignVCenter,  /* BlocksLeft */
    Qt::AlignRight|Qt::AlignVCenter,  /* MaturityHeight */
    Qt::AlignRight|Qt::AlignVCenter   /* ExpiryHeight */
};

MouseNFTTableModel::MouseNFTTableModel(WalletModel *parent) :
    QAbstractTableModel(parent),
    walletModel(parent),
    includeExpired(false)
{
    columns << tr("BCT NFT ID") << tr("Original BCT") << tr("Total Mice") 
            << tr("Status") << tr("Owner") << tr("Blocks Left")
            << tr("Created Height") << tr("Expiry Height");

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateBeeNFTs()));
    timer->start(30000); // Update every 30 seconds
    
    updateMouseNFTList();
}

MouseNFTTableModel::~MouseNFTTableModel()
{
    if (timer) {
        timer->stop();
    }
}

int MouseNFTTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    // Cascoin: Memory leak fix - Limit cached NFT list size to prevent memory overflow
    return std::min(cachedMouseNFTList.size(), static_cast<qsizetype>(100)); // Reduced to 100 entries for better memory usage
}

int MouseNFTTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant MouseNFTTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= cachedMouseNFTList.size()) {
        return QVariant();
    }

    const MouseNFTRecord &rec = cachedMouseNFTList[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch(index.column()) {
        case MouseNFTId:
            return rec.mouseNFTId.left(16) + "..."; // Truncate for display
        case OriginalBCT:
            return rec.originalBCT.left(16) + "..."; // Truncate for display
        case MouseIndex: // Now "Total Mice"
            {
                // Format number with thousands separators
                QString numStr = QString::number(rec.mouseIndex);
                int len = numStr.length();
                for (int i = len - 3; i > 0; i -= 3) {
                    numStr.insert(i, ',');
                }
                return numStr;
            }
        case Status:
            return rec.status;
        case Owner:
            return rec.currentOwner.left(16) + "..."; // Truncate for display
        case BlocksLeft:
            return rec.blocksLeft;
        case MaturityHeight:
            return rec.maturityHeight;
        case ExpiryHeight:
            return rec.expiryHeight;
        }
    } else if (role == Qt::TextAlignmentRole) {
        return column_alignments[index.column()];
    } else if (role == Qt::ToolTipRole) {
        switch(index.column()) {
        case MouseNFTId:
            return tr("Unique mice NFT identifier: %1").arg(rec.mouseNFTId);
        case OriginalBCT:
            return tr("Original BCT transaction: %1").arg(rec.originalBCT);
        case Owner:
            return tr("Current owner: %1").arg(rec.currentOwner);
        case Status:
            if (rec.status == "mature") {
                return tr("This mouse is mature and can be used for mining");
            } else if (rec.status == "immature") {
                return tr("This mouse is still gestating and cannot mine yet");
            } else if (rec.status == "expired") {
                return tr("This mouse has expired and can no longer mine");
            }
            return rec.status;
        }
    } else if (role == Qt::DecorationRole) {
        if (index.column() == Status) {
            if (rec.status == "mature") {
                return QIcon(":/icons/tx_confirmed");
            } else if (rec.status == "immature") {
                return QIcon(":/icons/tx_unconfirmed");
            } else if (rec.status == "expired") {
                return QIcon(":/icons/tx_conflicted");
            }
        }
    } else if (role == Qt::BackgroundRole) {
        if (rec.status == "expired") {
            return QColor(255, 0, 0, 25); // Light red background for expired
        } else if (rec.status == "immature") {
            return QColor(255, 255, 0, 25); // Light yellow background for immature
        } else if (rec.status == "mature") {
            return QColor(0, 255, 0, 25); // Light green background for mature
        }
    }

    return QVariant();
}

QVariant MouseNFTTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole && section < columns.size()) {
            return columns[section];
        }
        if (role == Qt::ToolTipRole) {
            switch(section) {
            case MouseNFTId:
                return tr("Unique identifier for this mice NFT");
            case OriginalBCT:
                return tr("Transaction ID of the original BCT that created this mouse");
            case MouseIndex:
                return tr("Index of this mouse within the original BCT");
            case Status:
                return tr("Current status of the mouse (immature/mature/expired)");
            case Owner:
                return tr("Current owner of this mice NFT");
            case BlocksLeft:
                return tr("Number of blocks until this mouse expires");
            case MaturityHeight:
                return tr("Block height when this mouse matures for mining");
            case ExpiryHeight:
                return tr("Block height when this mouse expires");
            }
        }
    }
    return QVariant();
}

QModelIndex MouseNFTTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    if (row >= 0 && row < cachedMouseNFTList.size() && column >= 0 && column < columns.size()) {
        return createIndex(row, column);
    }
    return QModelIndex();
}

Qt::ItemFlags MouseNFTTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void MouseNFTTableModel::setIncludeExpired(bool _includeExpired)
{
    if (includeExpired != _includeExpired) {
        includeExpired = _includeExpired;
        updateMouseNFTList();
    }
}

void MouseNFTTableModel::updateMouseNFTList()
{
    if (!walletModel) {
        return;
    }

    // Don't clear the list here - keep existing NFTs visible until new data arrives
    // This prevents the "flickering" effect where NFTs appear and disappear
    // The list will be updated when updateMouseNFTListWithData() is called with fresh data
    
    // Just emit the signal to notify listeners that an update was requested
    Q_EMIT mouseNFTsChanged();
}

void MouseNFTTableModel::updateMouseNFTs()
{
    updateMouseNFTList();
}

void MouseNFTTableModel::updateMouseNFTListWithData(const QList<MouseNFTRecord>& newRecords)
{
    beginResetModel();
    
    // Cascoin: Memory leak fix with intelligent cleanup - keep 4KB NFT data size but manage memory better
    if (newRecords.size() > 500) {
        qDebug() << "NFT list size (" << newRecords.size() << ") exceeded 500 entries, using smart truncation to prevent memory leak";
        
        // Sort by priority: active NFTs first, then by blocks left (descending)
        QList<MouseNFTRecord> sortedRecords = newRecords;
        std::sort(sortedRecords.begin(), sortedRecords.end(), [](const MouseNFTRecord& a, const MouseNFTRecord& b) {
            // Active NFTs have priority
            if (a.status == "mature" && b.status != "mature") return true;
            if (a.status != "mature" && b.status == "mature") return false;
            
            // Then sort by blocks left (more blocks left = higher priority)
            return a.blocksLeft > b.blocksLeft;
        });
        
        cachedMouseNFTList = sortedRecords.mid(0, 500); // Keep only top 500 entries
    } else {
        cachedMouseNFTList = newRecords;
    }
    
    endResetModel();
    
    Q_EMIT mouseNFTsChanged();
}
