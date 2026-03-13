// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Cascoin: Labyrinth

#include <qt/hivetablemodel.h>
#include <bctdb.h>  // For BCTDatabaseSQLite

#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/hivedialog.h>  // For formatLargeNoLocale()

#include <clientversion.h>
#include <streams.h>

#include <util.h>
#include <validation.h>

#include <thread>

HiveTableModel::HiveTableModel(const PlatformStyle *_platformStyle, CWallet *wallet, WalletModel *parent) : platformStyle(_platformStyle), QAbstractTableModel(parent), walletModel(parent)
{
    Q_UNUSED(wallet);

    // Set column headings
    columns << tr("Created") << tr("Mouse count") << tr("Mouse status") << tr("Estimated time until status change") << tr("Mouse cost") << tr("Rewards earned");

    sortOrder = Qt::DescendingOrder;
    sortColumn = 0;

    rewardsPaid = cost = profit = 0;
    immature = mature = dead = blocksFound = 0;
    updateInProgress = false;
    pendingUpdate = false;
    lastIncludeDeadMice = false;
}

HiveTableModel::~HiveTableModel() {
    // Empty destructor
}

void HiveTableModel::updateBCTs(bool includeDeadMice) {
    if (!walletModel) {
        return;
    }

    // Remember the filter setting for refresh
    lastIncludeDeadMice = includeDeadMice;

    // Prevent concurrent updates: coalesce into a single follow-up run
    if (updateInProgress) {
        pendingUpdate = true;
        return;
    }
    updateInProgress = true;

    // Move database operations to background thread to prevent GUI hang
    std::thread([=]() {
        try {
            // Load entries from BCTDatabaseSQLite in background thread
            BCTDatabaseSQLite* bctDb = BCTDatabaseSQLite::instance();
            std::vector<BCTRecord> records;
            
            // Helper lambda to convert wallet BCTs to BCTRecords
            auto convertWalletBCTs = [](const std::vector<CMouseCreationTransactionInfo>& walletBCTs) {
                std::vector<BCTRecord> result;
                result.reserve(walletBCTs.size());
                for (const auto& bct : walletBCTs) {
                    BCTRecord record;
                    record.txid = bct.txid;
                    record.cheeseAddress = bct.cheeseAddress;
                    record.status = bct.mouseStatus;
                    record.mouseCount = bct.mouseCount;
                    record.creationHeight = bct.creationHeight;
                    record.maturityHeight = bct.maturityHeight;
                    record.expirationHeight = bct.expirationHeight;
                    record.timestamp = bct.time;
                    record.cost = bct.mouseFeePaid;
                    record.blocksFound = bct.blocksFound;
                    record.rewardsPaid = bct.rewardsPaid;
                    record.profit = bct.profit;
                    result.push_back(record);
                }
                return result;
            };
            
            if (bctDb && bctDb->isInitialized()) {
                // Use SQLite database for fast queries
                records = bctDb->getAllBCTs(includeDeadMice);
                LogPrintf("HiveTableModel: Loaded %zu BCT records from SQLite database (includeDeadMice=%d)\n", records.size(), includeDeadMice);
                
                // Debug: Count records with rewards
                int recordsWithRewards = 0;
                for (const auto& r : records) {
                    if (r.blocksFound > 0) recordsWithRewards++;
                }
                LogPrintf("HiveTableModel: %d of %zu records have blocksFound > 0\n", recordsWithRewards, records.size());
                
                // If database is empty, fall back to wallet scan
                if (records.empty()) {
                    LogPrintf("HiveTableModel: SQLite database is empty, falling back to wallet scan\n");
                    std::vector<CMouseCreationTransactionInfo> vMouseCreationTransactions;
                    walletModel->getBCTs(vMouseCreationTransactions, includeDeadMice);
                    records = convertWalletBCTs(vMouseCreationTransactions);
                    LogPrintf("HiveTableModel: Loaded %zu BCT records from wallet scan\n", records.size());
                }
            } else {
                // Fallback to wallet scan if database not initialized
                LogPrintf("HiveTableModel: BCTDatabaseSQLite not initialized (bctDb=%p, initialized=%d), falling back to wallet scan\n", 
                          bctDb, bctDb ? bctDb->isInitialized() : false);
                std::vector<CMouseCreationTransactionInfo> vMouseCreationTransactions;
                walletModel->getBCTs(vMouseCreationTransactions, includeDeadMice);
                records = convertWalletBCTs(vMouseCreationTransactions);
            }
            
            // Get current chain height for blocks left calculation
            int currentHeight = 0;
            {
                LOCK(cs_main);
                currentHeight = chainActive.Height();
            }
            const Consensus::Params& consensusParams = Params().GetConsensus();
            
            // Update UI on main thread
            QMetaObject::invokeMethod(this, [=]() {
                // Rebuild the entire model atomically to avoid incorrect row counts
                beginResetModel();

                list.clear();

                // Use local variables to accumulate summary values
                // Only update member variables AFTER all records are processed
                // This prevents getSummaryValues() from seeing intermediate 0 values
                int newImmature = 0;
                int newMature = 0;
                int newDead = 0;
                int newBlocksFound = 0;
                CAmount newCost = 0;
                CAmount newRewardsPaid = 0;
                CAmount newProfit = 0;

                for (const BCTRecord& record : records) {
                    // Convert BCTRecord to CMouseCreationTransactionInfo for display
                    CMouseCreationTransactionInfo bct;
                    bct.txid = record.txid;
                    bct.cheeseAddress = record.cheeseAddress;
                    bct.mouseCount = record.mouseCount;
                    bct.time = record.timestamp;
                    bct.mouseFeePaid = record.cost;
                    bct.blocksFound = record.blocksFound;
                    bct.rewardsPaid = record.rewardsPaid;
                    bct.profit = record.profit;
                    bct.creationHeight = record.creationHeight;
                    bct.maturityHeight = record.maturityHeight;
                    bct.expirationHeight = record.expirationHeight;
                    
                    // Debug: Log first few records with rewards
                    static int loggedCount = 0;
                    if (record.blocksFound > 0 && loggedCount < 3) {
                        LogPrintf("HiveTableModel: BCT %s has blocksFound=%d, rewardsPaid=%lld\n", 
                                  record.txid, record.blocksFound, record.rewardsPaid);
                        loggedCount++;
                    }
                    
                    // Calculate blocks left and status based on CURRENT height
                    // This ensures the UI always shows the correct status even if
                    // the database hasn't been updated yet
                    if (record.creationHeight > 0 && record.expirationHeight > 0) {
                        bct.blocksLeft = record.getBlocksLeft(currentHeight);
                        
                        // Recalculate status based on current height
                        if (currentHeight >= record.expirationHeight) {
                            bct.mouseStatus = "expired";
                        } else if (currentHeight >= record.maturityHeight) {
                            bct.mouseStatus = "mature";
                        } else {
                            bct.mouseStatus = "immature";
                        }
                    } else {
                        // Fallback to database status if heights not available
                        bct.mouseStatus = record.status;
                        bct.blocksLeft = 0;
                        bct.creationHeight = 0;
                        bct.maturityHeight = 0;
                        bct.expirationHeight = 0;
                    }

                    // Update summary counts
                    if (bct.mouseStatus == "mature") {
                        newMature += bct.mouseCount;
                    } else if (bct.mouseStatus == "immature") {
                        newImmature += bct.mouseCount;
                    } else if (bct.mouseStatus == "expired") {
                        newDead += bct.mouseCount;
                    }

                    newBlocksFound += bct.blocksFound;
                    newCost += bct.mouseFeePaid;
                    newRewardsPaid += bct.rewardsPaid;
                    newProfit += bct.profit;

                    // Prepend to keep most recent on top before sorting
                    list.prepend(bct);
                }

                endResetModel();

                // Now atomically update summary member variables
                // This ensures getSummaryValues() never sees intermediate 0 values
                immature = newImmature;
                mature = newMature;
                dead = newDead;
                blocksFound = newBlocksFound;
                cost = newCost;
                rewardsPaid = newRewardsPaid;
                profit = newProfit;

                // Maintain correct sorting
                sort(sortColumn, sortOrder);

                // Fire signal
                QMetaObject::invokeMethod(walletModel, "newHiveSummaryAvailable", Qt::QueuedConnection);

                // Reset update flag and process any pending request
                updateInProgress = false;
                if (pendingUpdate) {
                    pendingUpdate = false;
                    // Re-run with the same includeDeadMice value requested last
                    updateBCTs(lastIncludeDeadMice);
                }
            }, Qt::QueuedConnection);
            
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [=]() {
                LogPrint(BCLog::QT, "Error updating BCTs: %s\n", e.what());
                // Reset update flag on error too
                updateInProgress = false;
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void HiveTableModel::loadFromSQLiteDatabase(bool includeDeadMice) {
    // This method is called from the main thread for immediate cache access
    BCTDatabaseSQLite* bctDb = BCTDatabaseSQLite::instance();
    if (!bctDb || !bctDb->isInitialized()) {
        return;
    }

    // Get summary from database for immediate display
    BCTSummary summary = bctDb->getSummary();
    
    immature = summary.immatureMice;
    mature = summary.matureMice;
    dead = summary.expiredMice;
    blocksFound = summary.blocksFound;
    cost = summary.totalCost;
    rewardsPaid = summary.totalRewards;
    profit = summary.totalProfit;
}

void HiveTableModel::onDatabaseUpdated() {
    // Called when BCTDatabaseSQLite signals an update
    // Refresh the model with the last filter setting
    updateBCTs(lastIncludeDeadMice);
}

void HiveTableModel::getSummaryValues(int &_immature, int &_mature, int &_dead, int &_blocksFound, CAmount &_cost, CAmount &_rewardsPaid, CAmount &_profit) {
    _immature = immature;
    _mature = mature;
    _blocksFound = blocksFound;
    _cost = cost;
    _rewardsPaid = rewardsPaid;
    _dead = dead;
    _profit = profit;
}

int HiveTableModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);

    return list.length();
}

int HiveTableModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);

    return columns.length();
}

QVariant HiveTableModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() >= list.length())
        return QVariant();

    const CMouseCreationTransactionInfo *rec = &list[index.row()];
    if(role == Qt::DisplayRole || role == Qt::EditRole) {        
        switch(index.column()) {
            case Created:
                return (rec->time == 0) ? "Not in chain yet" : GUIUtil::dateTimeStr(rec->time);
            case Count:
                return HiveDialog::formatLargeNoLocale(rec->mouseCount);
            case Status:
                {
                    QString status = QString::fromStdString(rec->mouseStatus);
                    status[0] = status[0].toUpper();
                    return status;
                }
            case EstimatedTime:
                {
                    QString status = "";
                    if (rec->mouseStatus == "immature" && rec->maturityHeight > 0) {
                        // Calculate blocks until maturity using the stored maturity height
                        LOCK(cs_main);
                        int currentHeight = chainActive.Height();
                        int blocksTillMature = rec->maturityHeight - currentHeight;
                        if (blocksTillMature > 0) {
                            status = "Matures in " + QString::number(blocksTillMature) + " blocks (" + secondsToString(blocksTillMature * Params().GetConsensus().nPowTargetSpacing / 2) + ")";
                        } else {
                            status = "Maturing...";
                        }
                    } else if (rec->mouseStatus == "mature" && rec->expirationHeight > 0) {
                        // Calculate blocks until expiration using the stored expiration height
                        LOCK(cs_main);
                        int currentHeight = chainActive.Height();
                        int blocksLeft = rec->expirationHeight - currentHeight;
                        if (blocksLeft > 0) {
                            status = "Expires in " + QString::number(blocksLeft) + " blocks (" + secondsToString(blocksLeft * Params().GetConsensus().nPowTargetSpacing / 2) + ")";
                        } else {
                            status = "Expiring...";
                        }
                    } else if (rec->mouseStatus == "expired") {
                        status = "Expired";
                    }
                    return status;
                }
            case Cost:
                return BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rec->mouseFeePaid) + " " + BitcoinUnits::shortName(this->walletModel->getOptionsModel()->getDisplayUnit());
            case Rewards:
                if (rec->blocksFound == 0)
                    return "No blocks mined";
                return BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rec->rewardsPaid)
                    + " " + BitcoinUnits::shortName(this->walletModel->getOptionsModel()->getDisplayUnit()) 
                    + " (" + QString::number(rec->blocksFound) + " blocks mined)";
        }
    } else if (role == Qt::TextAlignmentRole) {
        /*if (index.column() == Rewards && rec->blocksFound == 0)
            return (int)(Qt::AlignCenter|Qt::AlignVCenter);
        else*/ if (index.column() == Cost || index.column() == Rewards || index.column() == Count)
            return (int)(Qt::AlignRight|Qt::AlignVCenter);
        else
            return (int)(Qt::AlignCenter|Qt::AlignVCenter);
    } else if (role == Qt::ForegroundRole) {
        const CMouseCreationTransactionInfo *rec = &list[index.row()];

        if (index.column() == Rewards) {
            if (rec->blocksFound == 0)
                return QColor(200, 0, 0);
            if (rec->profit < 0)
                return QColor(170, 70, 0);
            return QColor(27, 170, 45);
        }
        
        if (index.column() == Status) {
            if (rec->mouseStatus == "expired")
                return QColor(200, 0, 0);
            if (rec->mouseStatus == "immature")
                return QColor(170, 70, 0);
            return QColor(27, 170, 45);
        }

        return QColor(0, 0, 0);
    } else if (role == Qt::DecorationRole) {
        const CMouseCreationTransactionInfo *rec = &list[index.row()];
        if (index.column() == Status) {
            QString iconStr = ":/icons/mousestatus_dead";    // Dead
            if (rec->mouseStatus == "mature")
                iconStr = ":/icons/mousestatus_mature";
            else if (rec->mouseStatus == "immature")
                iconStr = ":/icons/mousestatus_immature";                
            return platformStyle->SingleColorIcon(iconStr);
        }
    }
    return QVariant();
}

bool HiveTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    return true;
}

QVariant HiveTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if(orientation == Qt::Horizontal)
        if(role == Qt::DisplayRole && section < columns.size())
            return columns[section];

    return QVariant();
}

void HiveTableModel::sort(int column, Qt::SortOrder order) {
    sortColumn = column;
    sortOrder = order;
    std::sort(list.begin(), list.end(), CMouseCreationTransactionInfoLessThan(column, order));
    Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(list.size() - 1, NUMBER_OF_COLUMNS - 1, QModelIndex()));
}

bool CMouseCreationTransactionInfoLessThan::operator()(CMouseCreationTransactionInfo &left, CMouseCreationTransactionInfo &right) const {
    CMouseCreationTransactionInfo *pLeft = &left;
    CMouseCreationTransactionInfo *pRight = &right;
    if (order == Qt::DescendingOrder)
        std::swap(pLeft, pRight);

    switch(column) {
        case HiveTableModel::Count:
            return pLeft->mouseCount < pRight->mouseCount;
        case HiveTableModel::Status:
        case HiveTableModel::EstimatedTime:
            return pLeft->blocksLeft < pRight->blocksLeft;
        case HiveTableModel::Cost:
            return pLeft->mouseFeePaid < pRight->mouseFeePaid;
        case HiveTableModel::Rewards:
            return pLeft->rewardsPaid < pRight->rewardsPaid;
        case HiveTableModel::Created:
        default:
            return pLeft->time < pRight->time;
    }
}

QString HiveTableModel::secondsToString(qint64 seconds) {
    const qint64 DAY = 86400;
    qint64 days = seconds / DAY;
    QTime t = QTime(0,0).addSecs(seconds % DAY);
    return QString("%1 days %2 hrs %3 mins").arg(days).arg(t.hour()).arg(t.minute());
}
