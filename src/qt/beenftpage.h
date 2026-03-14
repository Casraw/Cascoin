// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_QT_MOUSENFTPAGE_H
#define CASCOIN_QT_MOUSENFTPAGE_H

#include <QWidget>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QTimer>

#include <bctdb.h>  // For BCTDatabaseSQLite

class WalletModel;
class MouseNFTTableModel;
class PlatformStyle;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Widget for managing Mouse NFTs */
class MouseNFTPage : public QWidget
{
    Q_OBJECT

public:
    explicit MouseNFTPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~MouseNFTPage();

    void setModel(WalletModel *model);

private Q_SLOTS:
    void tokenizeMouse();
    void transferMouseNFT();
    void refreshMouseNFTs();
    void showMouseNFTDetails();
    void onMouseNFTSelectionChanged();
    void generateNewAddress();
    void loadAvailableMice();
    void loadAvailableMiceFromWallet();
    void loadSampleBCTData();
    void updateMouseNFTCombo();

private:
    WalletModel *walletModel;
    MouseNFTTableModel *mouseNFTModel;
    const PlatformStyle *platformStyle;
    // BCTDatabaseSQLite is accessed via singleton - no member needed

    // UI Components
    QTabWidget *tabWidget;
    
    // Mouse NFT List Tab
    QWidget *listTab;
    QTableView *mouseNFTView;
    QPushButton *refreshButton;
    QPushButton *detailsButton;
    QCheckBox *showExpiredCheckBox;
    QTimer *refreshTimer;  // Timer for debouncing checkbox state changes
    
    // Tokenize Tab
    QWidget *tokenizeTab;
    QComboBox *mouseSelectionCombo;
    QPushButton *refreshMiceButton;
    QLineEdit *ownerAddressEdit;
    QPushButton *tokenizeButton;
    QPushButton *generateAddressButton;
    QLabel *bctStatusLabel;
    QProgressBar *bctProgressBar;
    bool bctLoading = false;
    
    // Transfer Tab
    QWidget *transferTab;
    QComboBox *mouseNFTCombo;
    QLineEdit *recipientAddressEdit;
    QPushButton *transferButton;
    
    void setupUI();
    void showMouseSelectionDialog(const QString& bctId, const QString& ownerAddress);
    void executeTokenization(const QString& bctId, int mouseIndex, const QString& ownerAddress);
    void executeTokenizationBatch(const QString& bctId, int quantity, const QString& ownerAddress);
    void executeCompleteBCTTokenization(const QString& bctId, const QString& ownerAddress);
    void updateTableModelWithRealData(const QString& jsonString);
    void loadRealNFTData();
};

#endif // CASCOIN_QT_MOUSENFTPAGE_H
