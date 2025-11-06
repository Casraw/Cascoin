// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>
#include <qt/hivetablemodel.h>  // Cascoin: Hive
#include <qt/hivedialog.h>      // Cascoin: Hive: For formatLargeNoLocale()

#include <util.h>             // Cascoin: CVM: For gArgs
#include <chainparamsbase.h>  // Cascoin: CVM: For BaseParams()

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QMessageBox>        // Cascoin: CVM: For dialogs
#include <QPushButton>        // Cascoin: CVM: For custom buttons
#include <QClipboard>         // Cascoin: CVM: For copying URL
#include <QApplication>       // Cascoin: CVM: For clipboard access

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));

    // Cascoin: Hive
    cost = rewardsPaid = profit = 0;
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
}

// Cascoin: Rialto
void OverviewPage::setEncryptionStatus(int status) {
    switch(status) {
        case WalletModel::Unencrypted:
        case WalletModel::Unlocked:
            ui->unlockWalletButton->hide();
            break;
        case WalletModel::Locked:
            ui->unlockWalletButton->show();
            break;
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));

        // Cascoin: Hive: Connect summary updater
        connect(model, SIGNAL(newHiveSummaryAvailable()), this, SLOT(updateHiveSummary()));

        // Cascoin: Rialto: Connect wallet unlock button
        if (model->getEncryptionStatus() != WalletModel::Locked)
            ui->unlockWalletButton->hide();
        connect(model, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

// Cascoin: Hive: Update The Labyrinth summary
void OverviewPage::updateHiveSummary() {
    if (walletModel && walletModel->getHiveTableModel()) {
        int immature, mature, dead, blocksFound;
        walletModel->getHiveTableModel()->getSummaryValues(immature, mature, dead, blocksFound, cost, rewardsPaid, profit);

        ui->rewardsPaidLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rewardsPaid)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->costLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), cost)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->profitLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), profit)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        // Update the single mice status label with clean formatting using proper terminology
        QString miceText = QString("(%1 resting mice, %2 adventure mice")
            .arg(HiveDialog::formatLargeNoLocale(immature))
            .arg(HiveDialog::formatLargeNoLocale(mature));
        
        if (dead > 0) {
            miceText += QString(", %1 deceased").arg(HiveDialog::formatLargeNoLocale(dead));
        }
        miceText += ")";
        
        ui->miceStatusLabel->setText(miceText);
        
        // Update the blocks found status label with proper terminology
        QString blocksText = QString("(%1 cheese blocks found in labyrinth)")
            .arg(HiveDialog::formatLargeNoLocale(blocksFound));
        ui->blocksFoundStatusLabel->setText(blocksText);
    }
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();

        // Cascoin: Hive: Update CAmounts in hive summary
        ui->rewardsPaidLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rewardsPaid)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->costLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), cost)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->profitLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), profit)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

// Cascoin: Hive: Handle bee button click
void OverviewPage::on_beeButton_clicked() {
    Q_EMIT beeButtonClicked();
}

// Cascoin: Rialto: Handle unlock wallet button click
void OverviewPage::on_unlockWalletButton_clicked() {
    if(walletModel)
        walletModel->requestUnlock(true);
}

// Cascoin: CVM: Handle CVM Dashboard button click
void OverviewPage::on_cvmDashboardButton_clicked() {
    // Debug: Log button click
    LogPrintf("CVM Dashboard button clicked\n");
    
    // Check if dashboard is enabled
    if (!gArgs.GetBoolArg("-cvmdashboard", false)) {
        LogPrintf("CVM Dashboard is disabled in config\n");
        QMessageBox::warning(this, tr("CVM Dashboard"),
            tr("The CVM Dashboard is currently disabled.\n\n"
               "To enable it, add the following line to your cascoin.conf:\n"
               "cvmdashboard=1\n\n"
               "Then restart the wallet."),
            QMessageBox::Ok);
        return;
    }
    
    // Get RPC port from settings
    int rpcPort = gArgs.GetArg("-rpcport", BaseParams().RPCPort());
    LogPrintf("Using RPC port: %d\n", rpcPort);
    
    // Build dashboard URL
    QString dashboardUrl = QString("http://localhost:%1/dashboard/").arg(rpcPort);
    LogPrintf("Dashboard URL: %s\n", dashboardUrl.toStdString());
    
    // Show URL in a dialog with copy functionality
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("CVM Dashboard"));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(tr("CVM Dashboard URL:"));
    msgBox.setInformativeText(tr("Copy this URL and open it in your browser:\n\n%1").arg(dashboardUrl));
    msgBox.setDetailedText(tr("The CVM Dashboard provides:\n"
                             "• Smart contract deployment and management\n"
                             "• Contract execution and debugging\n"
                             "• Web-of-Trust reputation system\n"
                             "• Transaction and block explorer\n"
                             "• Real-time network statistics"));
    
    // Add copy button
    QPushButton *copyButton = msgBox.addButton(tr("Copy URL"), QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Ok);
    
    msgBox.exec();
    
    // Handle copy button click
    if (msgBox.clickedButton() == copyButton) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(dashboardUrl);
        LogPrintf("Dashboard URL copied to clipboard\n");
        
        // Show confirmation
        QMessageBox::information(this, tr("CVM Dashboard"), 
            tr("URL copied to clipboard!\n\nPaste it into your browser address bar."),
            QMessageBox::Ok);
    }
}