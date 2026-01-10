// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2page.cpp
 * @brief L2 Page widget implementation for cascoin-qt
 * 
 * Requirements: 40.3
 */

#include <qt/l2page.h>
#include <qt/clientmodel.h>
#include <qt/walletmodel.h>
#include <qt/platformstyle.h>
#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QProgressBar>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>

L2Page::L2Page(const PlatformStyle *_platformStyle, QWidget *parent)
    : QWidget(parent)
    , clientModel(nullptr)
    , walletModel(nullptr)
    , platformStyle(_platformStyle)
    , l2Balance(0)
    , l1Balance(0)
{
    setupUI();
    
    // Setup auto-refresh timer (every 10 seconds)
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &L2Page::onRefreshTimer);
    refreshTimer->start(10000);
}

L2Page::~L2Page()
{
    if (refreshTimer) {
        refreshTimer->stop();
    }
}

void L2Page::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Layer 2 Operations"));
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #2563eb;");
    mainLayout->addWidget(titleLabel);
    
    // Top row: Balance and Actions
    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->addWidget(createBalanceSection());
    topRow->addWidget(createDepositSection());
    topRow->addWidget(createWithdrawSection());
    mainLayout->addLayout(topRow);
    
    // Middle row: Transaction History
    mainLayout->addWidget(createTransactionSection());
    
    // Bottom row: Pending Withdrawals
    mainLayout->addWidget(createWithdrawalSection());
    
    // Status bar
    mainLayout->addWidget(createStatusSection());
    
    mainLayout->addStretch();
}

QWidget* L2Page::createBalanceSection()
{
    QGroupBox *group = new QGroupBox(tr("Balances"));
    QVBoxLayout *layout = new QVBoxLayout(group);
    
    // L2 Balance
    QHBoxLayout *l2Row = new QHBoxLayout();
    l2BalanceLabel = new QLabel(tr("L2 Balance:"));
    l2BalanceLabel->setStyleSheet("font-weight: bold;");
    l2BalanceValue = new QLabel(tr("0.00000000 CAS"));
    l2BalanceValue->setStyleSheet("font-size: 18px; color: #10b981;");
    l2Row->addWidget(l2BalanceLabel);
    l2Row->addWidget(l2BalanceValue);
    l2Row->addStretch();
    layout->addLayout(l2Row);
    
    // L1 Balance (for reference)
    QHBoxLayout *l1Row = new QHBoxLayout();
    l1BalanceLabel = new QLabel(tr("L1 Balance:"));
    l1BalanceValue = new QLabel(tr("0.00000000 CAS"));
    l1BalanceValue->setStyleSheet("color: #94a3b8;");
    l1Row->addWidget(l1BalanceLabel);
    l1Row->addWidget(l1BalanceValue);
    l1Row->addStretch();
    layout->addLayout(l1Row);
    
    return group;
}

QWidget* L2Page::createDepositSection()
{
    QGroupBox *group = new QGroupBox(tr("Deposit to L2"));
    QVBoxLayout *layout = new QVBoxLayout(group);
    
    QHBoxLayout *inputRow = new QHBoxLayout();
    depositAmountEdit = new QLineEdit();
    depositAmountEdit->setPlaceholderText(tr("Amount in CAS"));
    depositAmountEdit->setMaximumWidth(200);
    inputRow->addWidget(depositAmountEdit);
    
    depositButton = new QPushButton(tr("Deposit"));
    depositButton->setStyleSheet("background-color: #10b981; color: white; padding: 8px 16px;");
    connect(depositButton, &QPushButton::clicked, this, &L2Page::on_depositButton_clicked);
    inputRow->addWidget(depositButton);
    inputRow->addStretch();
    layout->addLayout(inputRow);
    
    depositStatusLabel = new QLabel();
    depositStatusLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");
    layout->addWidget(depositStatusLabel);
    
    return group;
}

QWidget* L2Page::createWithdrawSection()
{
    QGroupBox *group = new QGroupBox(tr("Withdraw to L1"));
    QVBoxLayout *layout = new QVBoxLayout(group);
    
    QHBoxLayout *inputRow = new QHBoxLayout();
    withdrawAmountEdit = new QLineEdit();
    withdrawAmountEdit->setPlaceholderText(tr("Amount in CAS"));
    withdrawAmountEdit->setMaximumWidth(200);
    inputRow->addWidget(withdrawAmountEdit);
    
    withdrawButton = new QPushButton(tr("Withdraw"));
    withdrawButton->setStyleSheet("background-color: #2563eb; color: white; padding: 8px 16px;");
    connect(withdrawButton, &QPushButton::clicked, this, &L2Page::on_withdrawButton_clicked);
    inputRow->addWidget(withdrawButton);
    inputRow->addStretch();
    layout->addLayout(inputRow);
    
    withdrawStatusLabel = new QLabel(tr("Challenge period: 7 days"));
    withdrawStatusLabel->setStyleSheet("color: #f59e0b; font-size: 12px;");
    layout->addWidget(withdrawStatusLabel);
    
    return group;
}

QWidget* L2Page::createTransactionSection()
{
    QGroupBox *group = new QGroupBox(tr("L2 Transaction History"));
    QVBoxLayout *layout = new QVBoxLayout(group);
    
    transactionTable = new QTableWidget();
    transactionTable->setColumnCount(5);
    transactionTable->setHorizontalHeaderLabels({
        tr("Date"), tr("Type"), tr("Amount"), tr("To/From"), tr("Status")
    });
    transactionTable->horizontalHeader()->setStretchLastSection(true);
    transactionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    transactionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    transactionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    transactionTable->setMinimumHeight(150);
    transactionTable->setMaximumHeight(200);
    
    layout->addWidget(transactionTable);
    
    return group;
}

QWidget* L2Page::createWithdrawalSection()
{
    QGroupBox *group = new QGroupBox(tr("Pending Withdrawals"));
    QVBoxLayout *layout = new QVBoxLayout(group);
    
    withdrawalTable = new QTableWidget();
    withdrawalTable->setColumnCount(4);
    withdrawalTable->setHorizontalHeaderLabels({
        tr("Amount"), tr("Initiated"), tr("Challenge Ends"), tr("Status")
    });
    withdrawalTable->horizontalHeader()->setStretchLastSection(true);
    withdrawalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    withdrawalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    withdrawalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    withdrawalTable->setMinimumHeight(100);
    withdrawalTable->setMaximumHeight(150);
    
    layout->addWidget(withdrawalTable);
    
    return group;
}

QWidget* L2Page::createStatusSection()
{
    QWidget *widget = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 10, 0, 0);
    
    // Chain status
    chainStatusLabel = new QLabel(tr("L2 Chain: Connecting..."));
    chainStatusLabel->setStyleSheet("color: #f59e0b;");
    layout->addWidget(chainStatusLabel);
    
    layout->addStretch();
    
    // Sequencer status
    sequencerStatusLabel = new QLabel(tr("Sequencers: 0"));
    sequencerStatusLabel->setStyleSheet("color: #94a3b8;");
    layout->addWidget(sequencerStatusLabel);
    
    layout->addSpacing(20);
    
    // Last update
    lastUpdateLabel = new QLabel(tr("Last update: Never"));
    lastUpdateLabel->setStyleSheet("color: #94a3b8; font-size: 11px;");
    layout->addWidget(lastUpdateLabel);
    
    layout->addSpacing(20);
    
    // Refresh button
    refreshButton = new QPushButton(tr("Refresh"));
    connect(refreshButton, &QPushButton::clicked, this, &L2Page::on_refreshButton_clicked);
    layout->addWidget(refreshButton);
    
    // Dashboard button
    dashboardButton = new QPushButton(tr("Open Dashboard"));
    dashboardButton->setStyleSheet("background-color: #2563eb; color: white;");
    connect(dashboardButton, &QPushButton::clicked, this, &L2Page::on_dashboardButton_clicked);
    layout->addWidget(dashboardButton);
    
    return widget;
}

void L2Page::setClientModel(ClientModel *_clientModel)
{
    clientModel = _clientModel;
    if (clientModel) {
        refreshAll();
    }
}

void L2Page::setWalletModel(WalletModel *_walletModel)
{
    walletModel = _walletModel;
    if (walletModel) {
        // Connect to balance changed signal
        connect(walletModel, &WalletModel::balanceChanged, this, &L2Page::updateL2Balance);
        refreshAll();
    }
}

void L2Page::updateL2Balance()
{
    // In a real implementation, this would query the L2 state manager
    // For now, show placeholder values
    l2BalanceValue->setText(formatAmount(l2Balance));
    
    if (walletModel) {
        // Get L1 balance from wallet model
        l1Balance = walletModel->getBalance();
        l1BalanceValue->setText(formatAmount(l1Balance));
    }
}

void L2Page::updateL2Transactions()
{
    // Clear existing rows
    transactionTable->setRowCount(0);
    
    // In a real implementation, this would query L2 transaction history
    // For now, show empty table with placeholder message
    transactionTable->setRowCount(1);
    QTableWidgetItem *item = new QTableWidgetItem(tr("No L2 transactions yet"));
    item->setTextAlignment(Qt::AlignCenter);
    transactionTable->setItem(0, 0, item);
    transactionTable->setSpan(0, 0, 1, 5);
}

void L2Page::updatePendingWithdrawals()
{
    // Clear existing rows
    withdrawalTable->setRowCount(0);
    
    // In a real implementation, this would query pending withdrawals
    // For now, show empty table
    withdrawalTable->setRowCount(1);
    QTableWidgetItem *item = new QTableWidgetItem(tr("No pending withdrawals"));
    item->setTextAlignment(Qt::AlignCenter);
    withdrawalTable->setItem(0, 0, item);
    withdrawalTable->setSpan(0, 0, 1, 4);
}

void L2Page::refreshAll()
{
    updateL2Balance();
    updateL2Transactions();
    updatePendingWithdrawals();
    
    // Update status labels
    chainStatusLabel->setText(tr("L2 Chain: Active"));
    chainStatusLabel->setStyleSheet("color: #10b981;");
    
    sequencerStatusLabel->setText(tr("Sequencers: 0"));
    
    lastUpdateLabel->setText(tr("Last update: %1").arg(
        QDateTime::currentDateTime().toString("hh:mm:ss")));
}

void L2Page::on_depositButton_clicked()
{
    CAmount value;
    if (!validateDepositAmount(depositAmountEdit->text(), value)) {
        return;
    }
    
    // In a real implementation, this would initiate a deposit transaction
    showSuccess(tr("Deposit Initiated"), 
        tr("Your deposit of %1 CAS has been initiated. It will be credited to your L2 account after confirmation on L1.")
        .arg(formatAmount(value)));
    
    depositAmountEdit->clear();
    depositStatusLabel->setText(tr("Deposit pending..."));
    
    Q_EMIT depositInitiated(depositAmountEdit->text());
}

void L2Page::on_withdrawButton_clicked()
{
    CAmount value;
    if (!validateWithdrawAmount(withdrawAmountEdit->text(), value)) {
        return;
    }
    
    // Show warning about challenge period
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("Confirm Withdrawal"),
        tr("You are about to withdraw %1 CAS from L2 to L1.\n\n"
           "This withdrawal will be subject to a 7-day challenge period before you can claim the funds on L1.\n\n"
           "Do you want to proceed?").arg(formatAmount(value)),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // In a real implementation, this would initiate a withdrawal
    showSuccess(tr("Withdrawal Initiated"),
        tr("Your withdrawal of %1 CAS has been initiated. You can claim it on L1 after the 7-day challenge period.")
        .arg(formatAmount(value)));
    
    withdrawAmountEdit->clear();
    withdrawStatusLabel->setText(tr("Withdrawal pending..."));
    
    Q_EMIT withdrawalInitiated(withdrawAmountEdit->text());
    
    // Refresh to show pending withdrawal
    updatePendingWithdrawals();
}

void L2Page::on_dashboardButton_clicked()
{
    // Open L2 dashboard in browser
    // Default to localhost with RPC port
    QString url = QString("http://localhost:%1/l2/").arg(8332);  // Default RPC port
    QDesktopServices::openUrl(QUrl(url));
}

void L2Page::on_refreshButton_clicked()
{
    refreshAll();
}

void L2Page::onRefreshTimer()
{
    refreshAll();
}

QString L2Page::formatAmount(CAmount amount) const
{
    return BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, amount, false, BitcoinUnits::separatorAlways);
}

bool L2Page::validateDepositAmount(const QString& amountStr, CAmount& value) const
{
    if (amountStr.isEmpty()) {
        const_cast<L2Page*>(this)->showError(tr("Invalid Amount"), tr("Please enter an amount to deposit."));
        return false;
    }
    
    bool ok;
    double amount = amountStr.toDouble(&ok);
    if (!ok || amount <= 0) {
        const_cast<L2Page*>(this)->showError(tr("Invalid Amount"), tr("Please enter a valid positive amount."));
        return false;
    }
    
    value = static_cast<CAmount>(amount * COIN);
    
    if (value > l1Balance) {
        const_cast<L2Page*>(this)->showError(tr("Insufficient Balance"), 
            tr("You don't have enough L1 balance for this deposit."));
        return false;
    }
    
    return true;
}

bool L2Page::validateWithdrawAmount(const QString& amountStr, CAmount& value) const
{
    if (amountStr.isEmpty()) {
        const_cast<L2Page*>(this)->showError(tr("Invalid Amount"), tr("Please enter an amount to withdraw."));
        return false;
    }
    
    bool ok;
    double amount = amountStr.toDouble(&ok);
    if (!ok || amount <= 0) {
        const_cast<L2Page*>(this)->showError(tr("Invalid Amount"), tr("Please enter a valid positive amount."));
        return false;
    }
    
    value = static_cast<CAmount>(amount * COIN);
    
    if (value > l2Balance) {
        const_cast<L2Page*>(this)->showError(tr("Insufficient Balance"), 
            tr("You don't have enough L2 balance for this withdrawal."));
        return false;
    }
    
    return true;
}

void L2Page::showError(const QString& title, const QString& message)
{
    QMessageBox::warning(this, title, message);
}

void L2Page::showSuccess(const QString& title, const QString& message)
{
    QMessageBox::information(this, title, message);
}
