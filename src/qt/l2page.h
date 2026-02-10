// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_L2PAGE_H
#define BITCOIN_QT_L2PAGE_H

/**
 * @file l2page.h
 * @brief L2 Page widget for cascoin-qt
 * 
 * This file implements the L2 page in the Qt GUI including:
 * - L2 Balance display
 * - L2 Transaction history
 * - Deposit/Withdraw UI
 * 
 * Requirements: 40.3
 */

#include <amount.h>

#include <QWidget>
#include <QTimer>

class ClientModel;
class WalletModel;
class PlatformStyle;

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableWidget;
class QLineEdit;
class QComboBox;
class QProgressBar;
QT_END_NAMESPACE

/**
 * @brief L2 Page widget for Layer 2 operations
 * 
 * Provides UI for:
 * - Viewing L2 balance
 * - Depositing CAS from L1 to L2
 * - Withdrawing CAS from L2 to L1
 * - Viewing L2 transaction history
 * - Monitoring withdrawal status
 */
class L2Page : public QWidget
{
    Q_OBJECT

public:
    explicit L2Page(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~L2Page();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

public Q_SLOTS:
    /** Update L2 balance display */
    void updateL2Balance();
    
    /** Update L2 transaction history */
    void updateL2Transactions();
    
    /** Update pending withdrawals */
    void updatePendingWithdrawals();
    
    /** Refresh all L2 data */
    void refreshAll();

private Q_SLOTS:
    /** Handle deposit button click */
    void on_depositButton_clicked();
    
    /** Handle withdraw button click */
    void on_withdrawButton_clicked();
    
    /** Handle open dashboard button click */
    void on_dashboardButton_clicked();
    
    /** Handle refresh button click */
    void on_refreshButton_clicked();
    
    /** Timer tick for auto-refresh */
    void onRefreshTimer();

Q_SIGNALS:
    /** Signal emitted when deposit is initiated */
    void depositInitiated(const QString& amount);
    
    /** Signal emitted when withdrawal is initiated */
    void withdrawalInitiated(const QString& amount);

private:
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;
    
    // UI Elements - Balance Section
    QLabel *l2BalanceLabel;
    QLabel *l2BalanceValue;
    QLabel *l1BalanceLabel;
    QLabel *l1BalanceValue;
    
    // UI Elements - Deposit Section
    QLineEdit *depositAmountEdit;
    QPushButton *depositButton;
    QLabel *depositStatusLabel;
    
    // UI Elements - Withdraw Section
    QLineEdit *withdrawAmountEdit;
    QPushButton *withdrawButton;
    QLabel *withdrawStatusLabel;
    QProgressBar *withdrawProgressBar;
    
    // UI Elements - Transaction History
    QTableWidget *transactionTable;
    
    // UI Elements - Pending Withdrawals
    QTableWidget *withdrawalTable;
    
    // UI Elements - Actions
    QPushButton *refreshButton;
    QPushButton *dashboardButton;
    
    // UI Elements - Status
    QLabel *chainStatusLabel;
    QLabel *sequencerStatusLabel;
    QLabel *lastUpdateLabel;
    
    // Auto-refresh timer
    QTimer *refreshTimer;
    
    // Current balances
    CAmount l2Balance;
    CAmount l1Balance;
    
    /** Setup the UI layout */
    void setupUI();
    
    /** Create balance section */
    QWidget* createBalanceSection();
    
    /** Create deposit section */
    QWidget* createDepositSection();
    
    /** Create withdraw section */
    QWidget* createWithdrawSection();
    
    /** Create transaction history section */
    QWidget* createTransactionSection();
    
    /** Create pending withdrawals section */
    QWidget* createWithdrawalSection();
    
    /** Create status bar section */
    QWidget* createStatusSection();
    
    /** Format amount for display */
    QString formatAmount(CAmount amount) const;
    
    /** Validate deposit amount */
    bool validateDepositAmount(const QString& amount, CAmount& value) const;
    
    /** Validate withdrawal amount */
    bool validateWithdrawAmount(const QString& amount, CAmount& value) const;
    
    /** Show error message */
    void showError(const QString& title, const QString& message);
    
    /** Show success message */
    void showSuccess(const QString& title, const QString& message);
};

#endif // BITCOIN_QT_L2PAGE_H
