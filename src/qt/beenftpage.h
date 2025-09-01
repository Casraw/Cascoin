// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_QT_BEENFTPAGE_H
#define CASCOIN_QT_BEENFTPAGE_H

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
#include <atomic>

#include <qt/bctdatabase.h>

class WalletModel;
class BeeNFTTableModel;
class PlatformStyle;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Widget for managing Bee NFTs */
class BeeNFTPage : public QWidget
{
    Q_OBJECT

public:
    explicit BeeNFTPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~BeeNFTPage();

    void setModel(WalletModel *model);

private Q_SLOTS:
    void tokenizeBee();
    void transferBeeNFT();
    void refreshBeeNFTs();
    void showBeeNFTDetails();
    void onBeeNFTSelectionChanged();
    void generateNewAddress();
    void loadAvailableMice();
    void loadAvailableMiceFromWallet();
    void loadSampleBCTData();
    void updateBeeNFTCombo();

private:
    WalletModel *walletModel;
    BeeNFTTableModel *beeNFTModel;
    const PlatformStyle *platformStyle;
    BCTDatabase *bctDatabase;

    // UI Components
    QTabWidget *tabWidget;
    
    // Bee NFT List Tab
    QWidget *listTab;
    QTableView *beeNFTView;
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
    
    // Memory leak protection
    std::atomic<int> activeDetachedThreads{0};
    
    // Transfer Tab
    QWidget *transferTab;
    QComboBox *beeNFTCombo;
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

#endif // CASCOIN_QT_BEENFTPAGE_H
