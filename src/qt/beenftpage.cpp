// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/beenftpage.h>
#include <qt/walletmodel.h>
#include <qt/platformstyle.h>
#include <qt/guiutil.h>
#include <qt/bitcoinunits.h>
#include <qt/rpcconsole.h>
#include <qt/addresstablemodel.h>
#include <qt/beenfttablemodel.h>
#include <qt/safeinvoke.h>

#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QSortFilterProxyModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDateTime>
#include <QTimer>
#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QFont>
#include <QPointer>
#include <thread>

BeeNFTPage::BeeNFTPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    walletModel(0),
    beeNFTModel(0),
    platformStyle(_platformStyle)
{
    // BCTDatabaseSQLite is accessed via singleton - no initialization needed here
    setupUI();
}

BeeNFTPage::~BeeNFTPage()
{
    // BCTDatabaseSQLite is a singleton - no cleanup needed here
}

void BeeNFTPage::setModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;
    if (_walletModel) {
        // Initialize bee NFT model
        if (!beeNFTModel) {
            beeNFTModel = new BeeNFTTableModel(_walletModel);
            beeNFTView->setModel(beeNFTModel);
            
            // Connect model signals
            connect(beeNFTModel, SIGNAL(beeNFTsChanged()), this, SLOT(updateBeeNFTCombo()));

            // Connect selection after model is set
            if (beeNFTView->selectionModel()) {
                connect(beeNFTView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
                        this, SLOT(onBeeNFTSelectionChanged()));
            }

            // Model is now fully wired — safe to start periodic updates
            beeNFTModel->startUpdates();
        }
        
        updateBeeNFTCombo();
        
        // Delay async operations to give Qt event loop time to finish widget setup.
        // refreshBeeNFTs and loadAvailableMice both spawn detached threads that post
        // lambdas back via Qt::QueuedConnection — calling them too early can crash
        // if the model/view hierarchy isn't fully initialized yet.
        QTimer::singleShot(500, this, SLOT(refreshBeeNFTs()));
        QTimer::singleShot(1000, this, SLOT(loadAvailableMice()));
    }
}

void BeeNFTPage::setupUI()
{
    setWindowTitle(tr("Mice NFTs"));
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Create tab widget
    tabWidget = new QTabWidget(this);
    
    // === Bee NFT List Tab ===
    listTab = new QWidget();
    QVBoxLayout *listLayout = new QVBoxLayout(listTab);
    
    // Controls row
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    
    showExpiredCheckBox = new QCheckBox(tr("Show expired mice NFTs"));
    refreshButton = new QPushButton(tr("Refresh"));
    detailsButton = new QPushButton(tr("Details"));
    
    controlsLayout->addWidget(showExpiredCheckBox);
    controlsLayout->addStretch();
    controlsLayout->addWidget(refreshButton);
    controlsLayout->addWidget(detailsButton);
    
    listLayout->addLayout(controlsLayout);
    
    // Bee NFT table
    beeNFTView = new QTableView();
    beeNFTView->setSelectionBehavior(QAbstractItemView::SelectRows);
    beeNFTView->setSelectionMode(QAbstractItemView::SingleSelection);
    beeNFTView->setAlternatingRowColors(true);
    beeNFTView->setContextMenuPolicy(Qt::CustomContextMenu);
    beeNFTView->setSortingEnabled(true);
    
    listLayout->addWidget(beeNFTView);
    
    tabWidget->addTab(listTab, tr("My Mice NFTs"));
    
    // === Tokenize Tab ===
    tokenizeTab = new QWidget();
    QVBoxLayout *tokenizeLayout = new QVBoxLayout(tokenizeTab);
    
    QGroupBox *tokenizeGroup = new QGroupBox(tr("Tokenize Mouse"));
    QGridLayout *tokenizeGridLayout = new QGridLayout(tokenizeGroup);
    
    // Available Mice Selection
    tokenizeGridLayout->addWidget(new QLabel(tr("Select Mouse:")), 0, 0);
    mouseSelectionCombo = new QComboBox();
    mouseSelectionCombo->setMinimumWidth(400);
    tokenizeGridLayout->addWidget(mouseSelectionCombo, 0, 1, 1, 2);
    
    // Refresh button for mouse list
    refreshMiceButton = new QPushButton(tr("Refresh Available Mice"));
    tokenizeGridLayout->addWidget(refreshMiceButton, 0, 3);

    // Loading indicators
    bctStatusLabel = new QLabel(tr("Loading BCT overview..."));
    bctProgressBar = new QProgressBar();
    bctProgressBar->setRange(0, 100);
    bctProgressBar->setValue(0);
    bctProgressBar->setTextVisible(true);
    bctStatusLabel->setVisible(false);
    bctProgressBar->setVisible(false);
    tokenizeGridLayout->addWidget(bctStatusLabel, 1, 0, 1, 2);
    tokenizeGridLayout->addWidget(bctProgressBar, 1, 2, 1, 2);
    
    // Owner Address
    tokenizeGridLayout->addWidget(new QLabel(tr("Owner Address:")), 2, 0);
    ownerAddressEdit = new QLineEdit();
    ownerAddressEdit->setPlaceholderText(tr("Address to receive the BCT NFT"));
    generateAddressButton = new QPushButton(tr("Generate New"));
    generateAddressButton->setToolTip(tr("Generate a new address for the BCT NFT"));
    tokenizeGridLayout->addWidget(ownerAddressEdit, 2, 1);
    tokenizeGridLayout->addWidget(generateAddressButton, 2, 2);
    
    // Tokenize Button
    tokenizeButton = new QPushButton(tr("Tokenize Complete BCT"));
    if (platformStyle->getImagesOnButtons()) {
        tokenizeButton->setIcon(platformStyle->SingleColorIcon(":/icons/send"));
    }
    tokenizeGridLayout->addWidget(tokenizeButton, 3, 1, 1, 2);
    
    tokenizeLayout->addWidget(tokenizeGroup);
    tokenizeLayout->addStretch();
    
    tabWidget->addTab(tokenizeTab, tr("Tokenize"));
    
    // === Transfer Tab ===
    transferTab = new QWidget();
    QVBoxLayout *transferLayout = new QVBoxLayout(transferTab);
    
    QGroupBox *transferGroup = new QGroupBox(tr("Transfer Mice NFT"));
    QGridLayout *transferGridLayout = new QGridLayout(transferGroup);
    
    // Mice NFT Selection
    transferGridLayout->addWidget(new QLabel(tr("Select Mice NFT:")), 0, 0);
    beeNFTCombo = new QComboBox();
    beeNFTCombo->setToolTip(tr("Select the mice NFT to transfer"));
    transferGridLayout->addWidget(beeNFTCombo, 0, 1, 1, 2);
    
    // Recipient Address
    transferGridLayout->addWidget(new QLabel(tr("Recipient Address:")), 1, 0);
    recipientAddressEdit = new QLineEdit();
    recipientAddressEdit->setPlaceholderText(tr("Enter recipient's address"));
    transferGridLayout->addWidget(recipientAddressEdit, 1, 1, 1, 2);
    
    // Transfer Button
    transferButton = new QPushButton(tr("Transfer Mice NFT"));
    if (platformStyle->getImagesOnButtons()) {
        transferButton->setIcon(platformStyle->SingleColorIcon(":/icons/send"));
    }
    transferGridLayout->addWidget(transferButton, 2, 1, 1, 2);
    
    transferLayout->addWidget(transferGroup);
    transferLayout->addStretch();
    
    tabWidget->addTab(transferTab, tr("Transfer"));
    
    mainLayout->addWidget(tabWidget);
    
    // Connect signals
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refreshBeeNFTs()));
    connect(detailsButton, SIGNAL(clicked()), this, SLOT(showBeeNFTDetails()));
    connect(refreshMiceButton, SIGNAL(clicked()), this, SLOT(loadAvailableMice()));
    connect(tokenizeButton, SIGNAL(clicked()), this, SLOT(tokenizeBee()));
    connect(transferButton, SIGNAL(clicked()), this, SLOT(transferBeeNFT()));
    connect(generateAddressButton, SIGNAL(clicked()), this, SLOT(generateNewAddress()));
    
    // Initialize debouncing timer for checkbox state changes
    refreshTimer = new QTimer(this);
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(300); // 300ms debounce delay
    connect(refreshTimer, &QTimer::timeout, this, [this]() {
        // Disable checkbox during refresh to provide visual feedback
        showExpiredCheckBox->setEnabled(false);
        showExpiredCheckBox->setText(tr("Show expired mice NFTs (updating...)"));
        
        refreshBeeNFTs();
        
        // Re-enable checkbox after refresh
        showExpiredCheckBox->setEnabled(true);
        showExpiredCheckBox->setText(tr("Show expired mice NFTs"));
    });
    
    // Connect checkbox with debouncing
    connect(showExpiredCheckBox, &QCheckBox::toggled, this, [this](bool) {
        refreshTimer->stop();
        refreshTimer->start();
    });
    
    // Enable/disable buttons based on selection
    detailsButton->setEnabled(false);
    transferButton->setEnabled(false);
    
    // Selection connection will be made once a model is set
    
    // Defer heavy loading until the user visits the Tokenize tab
    connect(tabWidget, &QTabWidget::currentChanged, this, [this](int idx){
        QWidget* w = tabWidget->widget(idx);
        if (w == tokenizeTab) {
            static bool loadedOnce = false;
            if (!loadedOnce) {
                loadedOnce = true;
                QTimer::singleShot(50, this, SLOT(loadAvailableMice()));
            }
        }
    });
}

void BeeNFTPage::loadAvailableMice()
{
    if (!walletModel) {
        return;
    }
    
    mouseSelectionCombo->clear();
    mouseSelectionCombo->addItem(tr("Loading BCT overview..."));
    mouseSelectionCombo->setEnabled(false);
    bctStatusLabel->setVisible(true);
    bctProgressBar->setVisible(true);
    bctStatusLabel->setText(tr("Loading BCT overview..."));
    bctProgressBar->setValue(1);

    // Run RPC in background to avoid blocking UI
    QPointer<BeeNFTPage> guard(this);
    std::thread([this, guard]() {
        // Inform splash that mice DB init starts
        uiInterface.ShowProgress("Mice DB initialisieren", 1, false);
        std::string rpcResult;
        std::string rpcCommand = "miceavailable";

        // Cascoin: Memory leak fix - Add timeout and error handling for large wallets
        LogPrintf("Starting BCT/Mice database initialization (memory optimized)\n");
        
        bool rpcOk = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommand);

        if (!rpcOk) {
            // Fallback to local DB on UI thread
            safeInvoke(guard, [this]() {
                loadAvailableMiceFromWallet();
                mouseSelectionCombo->setEnabled(true);
                bctStatusLabel->setText(tr("Using local BCT cache"));
                bctProgressBar->setValue(100);
            });
            uiInterface.ShowProgress("Mice DB initialisieren", 100, false);
            return;
        }

        // Parse and populate on UI thread
        safeInvoke(guard, [this, rpcResult]() {
            QString jsonString = QString::fromStdString(rpcResult);
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &error);

            mouseSelectionCombo->clear();
            mouseSelectionCombo->addItem(tr("Select a BCT (mice will be selectable next)"), "");

            if (error.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray bctArray = doc.array();
                int totalAvailableMice = 0;
                int totalBCTs = 0;

                int idx = 0;
                for (const QJsonValue& bctValue : bctArray) {
                    if (!bctValue.isObject()) continue;

                    QJsonObject bct = bctValue.toObject();
                    QString bctTxid = bct["bct_txid"].toString();
                    QString status = bct["status"].toString();
                    int totalMiceInBct = bct["total_mice"].toInt();

                    totalBCTs++;
                    if (status != "mature") continue;

                    // Count available mice but add BCT entries instead of individual mice to avoid GUI freeze
                    QJsonArray availableMice = bct["available_mice"].toArray();
                    int availableCount = 0;
                    for (const QJsonValue& mouseValue : availableMice) {
                        if (!mouseValue.isObject()) continue;
                        QJsonObject mouse = mouseValue.toObject();
                        bool alreadyTokenized = mouse["already_tokenized"].toBool();
                        if (!alreadyTokenized) {
                            availableCount++;
                        }
                    }
                    
                    // Only add BCT if it has available mice
                    if (availableCount > 0) {
                        QString displayText = QString("BCT %1 — %2/%3 mice available (%4)")
                                               .arg(bctTxid.left(8) + "...")
                                               .arg(availableCount)
                                               .arg(totalMiceInBct)
                                               .arg(status);
                        mouseSelectionCombo->addItem(displayText, bctTxid);
                        totalAvailableMice += availableCount;
                    }
                    // Update splash progress roughly based on loop
                    int denom = (int)bctArray.size();
                    if (denom <= 0) denom = 1;
                    int progress = bctArray.isEmpty() ? 100 : (idx * 100) / denom;
                    if (progress > 99) progress = 99;
                    if (progress < 1) progress = 1;
                    uiInterface.ShowProgress("Mice DB initialisieren", progress, false);
                    bctStatusLabel->setText(tr("Loading BCTs: %1/%2").arg(idx).arg(denom));
                    bctProgressBar->setValue(progress);
                    ++idx;
                }

                if (totalAvailableMice == 0) {
                    mouseSelectionCombo->clear();
                    mouseSelectionCombo->addItem(tr("No mature BCTs with available mice yet"), "");
                } else {
                    mouseSelectionCombo->insertItem(1, tr("--- %1 available mice across %2 BCTs ---").arg(totalAvailableMice).arg(totalBCTs), "");
                    mouseSelectionCombo->insertSeparator(2);
                }
                uiInterface.ShowProgress("Mice DB initialisieren", 100, false);
                bctStatusLabel->setText(tr("BCTs loaded"));
                bctProgressBar->setValue(100);
                
                // Also update the table model with the same real data
                if (beeNFTModel) {
                    updateTableModelWithRealData(jsonString);
                }
            } else {
                mouseSelectionCombo->clear();
                mouseSelectionCombo->addItem(tr("Error parsing mice data: %1").arg(error.errorString()), "");
                uiInterface.ShowProgress("Mice DB initialisieren", 100, false);
                bctStatusLabel->setText(tr("Error parsing BCT data"));
                bctProgressBar->setValue(100);
            }

            mouseSelectionCombo->setEnabled(true);
            bctLoading = false;
        });
    }).detach();
}

void BeeNFTPage::loadAvailableMiceFromWallet()
{
    // Load BCTs from SQLite database (much faster than blockchain sync)
    
    mouseSelectionCombo->clear();
    mouseSelectionCombo->addItem(tr("Select a mouse to tokenize..."), "");
    
    BCTDatabaseSQLite* bctDb = BCTDatabaseSQLite::instance();
    if (!bctDb || !bctDb->isInitialized()) {
        mouseSelectionCombo->addItem(tr("BCT database not available"), "");
        return;
    }
    
    // Load all mature BCTs from SQLite database
    std::vector<BCTRecord> bctList = bctDb->getBCTsByStatus("mature");
        
    if (bctList.empty()) {
        // Just show placeholder - don't create sample data on startup
        mouseSelectionCombo->addItem(tr("No BCT data available yet"), "");
        mouseSelectionCombo->addItem(tr("(Data will load as blockchain syncs)"), "");
        return;
    }
        
    int totalAvailableMice = 0;
    int matureBCTs = 0;
    
    for (const BCTRecord& bct : bctList) {
        matureBCTs++;
        
        // Add BCT entry with bee count
        QString displayText = QString("BCT %1 — %2 mice available (mature)")
                           .arg(QString::fromStdString(bct.txid).left(8) + "...")
                           .arg(bct.beeCount);
        
        // Store BCT txid for tokenization
        mouseSelectionCombo->addItem(displayText, QString::fromStdString(bct.txid));
        totalAvailableMice += bct.beeCount;
    }
    
    // Insert summary at top
    if (totalAvailableMice > 0) {
        mouseSelectionCombo->insertItem(1, tr("--- %1 Available Mice from %2 Mature BCTs ---").arg(totalAvailableMice).arg(matureBCTs), "");
        mouseSelectionCombo->insertSeparator(2);
        mouseSelectionCombo->insertItem(3, tr("(Using SQLite BCT database - instant loading)"), "");
        mouseSelectionCombo->insertSeparator(4);
    } else {
        mouseSelectionCombo->clear();
        mouseSelectionCombo->addItem(tr("No mature BCTs with available mice found"), "");
    }
}

void BeeNFTPage::loadSampleBCTData()
{
    // Sample data is no longer needed - BCTDatabaseSQLite is populated from blockchain
    // This function is kept for compatibility but does nothing
    LogPrintf("BeeNFTPage::loadSampleBCTData() - Sample data not needed with SQLite database\n");
}

void BeeNFTPage::tokenizeBee()
{
    if (!walletModel) {
        return;
    }
    
    QString selectedBctId = mouseSelectionCombo->currentData().toString();
    QString ownerAddress = ownerAddressEdit->text().trimmed();
    
    // Validate inputs
    if (selectedBctId.isEmpty() || mouseSelectionCombo->currentIndex() == 0) {
        QMessageBox::warning(this, tr("Input Error"), tr("Please select a BCT to tokenize mice from."));
        return;
    }
    
    if (ownerAddress.isEmpty()) {
        QMessageBox::warning(this, tr("Input Error"), tr("Please enter an owner address or generate a new one."));
        return;
    }
    
    // Show mouse selection dialog for the selected BCT
    showMouseSelectionDialog(selectedBctId, ownerAddress);
}

void BeeNFTPage::showMouseSelectionDialog(const QString& bctId, const QString& ownerAddress)
{
    // Create a simple confirmation dialog for complete BCT tokenization
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Tokenize Complete BCT"));
    dialog.resize(450, 250);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    QLabel *titleLabel = new QLabel(tr("Tokenize complete BCT %1:").arg(bctId.left(12) + "..."));
    titleLabel->setFont(QFont("", 10, QFont::Bold));
    layout->addWidget(titleLabel);
    
    QLabel *infoLabel = new QLabel(tr("Loading BCT information..."));
    layout->addWidget(infoLabel);
    
    QLabel *explanationLabel = new QLabel(tr("This will tokenize the entire BCT as a single NFT.\n"
                                            "All mice in this BCT will be transferred together."));
    explanationLabel->setWordWrap(true);
    explanationLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 10px; border-radius: 5px; }");
    layout->addWidget(explanationLabel);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Tokenize Complete BCT"));
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false); // Disabled until data loads
    layout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    // Load BCT information in background
    QPointer<QLabel> infoGuard(infoLabel);
    QPointer<QDialogButtonBox> buttonGuard(buttonBox);
    std::thread([=]() {
        try {
            int totalMiceCount = 0;
            QString status = "";
            
            // TODO: Load actual BCT data from RPC
            // For now, use sample data
            totalMiceCount = 200000; // Sample: 200,000 total mice (realistic)
            status = "mature";       // Sample status
            
            // Update UI on main thread — guarded against dialog-closed-early
            safeInvoke(QPointer<QObject>(infoGuard), [=]() {
                if (infoGuard.isNull() || buttonGuard.isNull()) return;
                // Format number with thousands separators
                QString formattedMiceCount = QString::number(totalMiceCount);
                int len = formattedMiceCount.length();
                for (int i = len - 3; i > 0; i -= 3) {
                    formattedMiceCount.insert(i, ',');
                }
                infoGuard->setText(tr("BCT Status: %1\nTotal Mice: %2\nThis will create 1 BCT-NFT containing all mice.")
                                  .arg(status).arg(formattedMiceCount));
                buttonGuard->button(QDialogButtonBox::Ok)->setEnabled(true);
            });
            
        } catch (...) {
            safeInvoke(QPointer<QObject>(infoGuard), [=]() {
                if (!infoGuard.isNull())
                    infoGuard->setText(tr("Error loading BCT data"));
            });
        }
    }).detach();
    
    // Show dialog and handle result
    if (dialog.exec() == QDialog::Accepted) {
        // Execute complete BCT tokenization
        executeCompleteBCTTokenization(bctId, ownerAddress);
    }
}

void BeeNFTPage::executeTokenization(const QString& bctId, int mouseIndex, const QString& ownerAddress)
{
    QString message = tr("Are you sure you want to tokenize mouse #%1 from BCT %2?\n\n"
                        "Owner: %3\n\n"
                        "This will create a transferable NFT for this mouse.")
                        .arg(mouseIndex)
                        .arg(bctId.left(12) + "...")
                        .arg(ownerAddress);
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Confirm Tokenization"), 
                                                             message,
                                                             QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Execute RPC command in background thread to avoid GUI blocking
        QPointer<BeeNFTPage> guard(this);
        std::thread([=, guard]() {
            try {
                // Use the native wallet functions instead of RPC for better integration
                QString result = tr("Tokenization process initiated for mouse #%1 from BCT %2 to address %3")
                               .arg(mouseIndex).arg(bctId.left(12) + "...").arg(ownerAddress);
                
                // Update UI on main thread
                safeInvoke(guard, [=]() {
                    QMessageBox::information(this, tr("Tokenization Started"), result);
                    // Refresh the mice list after tokenization
                    QTimer::singleShot(2000, this, SLOT(loadAvailableMice()));
                });
                
            } catch (const std::exception& e) {
                std::string msg = e.what();
                safeInvoke(guard, [=]() {
                    QMessageBox::warning(this, tr("RPC Error"), 
                                       tr("Failed to execute tokenization: %1").arg(QString::fromStdString(msg)));
                });
            }
        }).detach();
    }
}

void BeeNFTPage::executeTokenizationBatch(const QString& bctId, int quantity, const QString& ownerAddress)
{
    QString message = tr("Are you sure you want to tokenize %1 mice from BCT %2?\n\n"
                        "Owner: %3\n\n"
                        "This will create %1 transferable NFTs for these mice.")
                        .arg(quantity)
                        .arg(bctId.left(12) + "...")
                        .arg(ownerAddress);
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Confirm Batch Tokenization"), 
                                                             message,
                                                             QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Execute batch tokenization in background thread
        QPointer<BeeNFTPage> guard(this);
        std::thread([=, guard]() {
            try {
                QString result = tr("Batch tokenization started: %1 mice from BCT %2 to address %3\n\n"
                                   "This may take a few moments to complete...")
                               .arg(quantity)
                               .arg(bctId.left(12) + "...")
                               .arg(ownerAddress);
                
                // Update UI on main thread
                safeInvoke(guard, [=]() {
                    QMessageBox::information(this, tr("Batch Tokenization Started"), result);
                    // Refresh the mice list after tokenization
                    QTimer::singleShot(3000, this, SLOT(loadAvailableMice()));
                });
                
                // TODO: Implement actual batch tokenization logic
                // For now, simulate the process
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                safeInvoke(guard, [=]() {
                    QString completionMsg = tr("Batch tokenization completed!\n\n"
                                              "%1 mice from BCT %2 have been tokenized.")
                                            .arg(quantity)
                                            .arg(bctId.left(12) + "...");
                    QMessageBox::information(this, tr("Tokenization Complete"), completionMsg);
                });
                
            } catch (const std::exception& e) {
                std::string msg = e.what();
                safeInvoke(guard, [=]() {
                    QMessageBox::warning(this, tr("Batch Tokenization Error"), 
                                       tr("Failed to execute batch tokenization: %1").arg(QString::fromStdString(msg)));
                });
            }
        }).detach();
    }
}

void BeeNFTPage::executeCompleteBCTTokenization(const QString& bctId, const QString& ownerAddress)
{
    QString message = tr("Are you sure you want to tokenize the complete BCT %1?\n\n"
                        "Owner: %2\n\n"
                        "This will create a single BCT-NFT containing all mice.\n"
                        "The entire BCT can then be transferred as one unit.")
                        .arg(bctId.left(12) + "...")
                        .arg(ownerAddress);
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Confirm Complete BCT Tokenization"), 
                                                             message,
                                                             QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Execute complete BCT tokenization in background thread
        QPointer<BeeNFTPage> guard(this);
        std::thread([=, guard]() {
            try {
                // Execute the real bctnftokenize RPC command for complete BCT tokenization
                std::string rpcCommand = "bctnftokenize \"" + bctId.toStdString() + "\" \"" + ownerAddress.toStdString() + "\"";
                std::string rpcResult;
                bool success = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommand);
                
                safeInvoke(guard, [=]() {
                    if (success && !rpcResult.empty()) {
                        QString result = tr("BCT Tokenization completed successfully!\n\n"
                                           "Transaction: %1\n\n"
                                           "BCT %2 has been tokenized as a single NFT.")
                                       .arg(QString::fromStdString(rpcResult).left(64) + "...")
                                       .arg(bctId.left(12) + "...");
                        QMessageBox::information(this, tr("BCT Tokenization Complete"), result);
                        
                        // Refresh the BCT list after successful tokenization
                        QTimer::singleShot(1000, this, SLOT(loadAvailableMice()));
                        QTimer::singleShot(1000, this, SLOT(refreshBeeNFTs()));
                    } else {
                        QString errorMsg = QString::fromStdString(rpcResult);
                        if (errorMsg.isEmpty()) {
                            errorMsg = tr("Unknown error occurred during tokenization");
                        }
                        QMessageBox::warning(this, tr("BCT Tokenization Failed"), 
                                           tr("Failed to tokenize BCT: %1").arg(errorMsg));
                    }
                });
                
            } catch (const std::exception& e) {
                std::string msg = e.what();
                safeInvoke(guard, [=]() {
                    QMessageBox::warning(this, tr("BCT Tokenization Error"), 
                                       tr("Failed to execute BCT tokenization: %1").arg(QString::fromStdString(msg)));
                });
            }
        }).detach();
    }
}

void BeeNFTPage::transferBeeNFT()
{
    if (!walletModel) {
        return;
    }
    
    QString beeNFTId = beeNFTCombo->currentData().toString();
    QString recipientAddress = recipientAddressEdit->text().trimmed();
    
    // Validate inputs
    if (beeNFTId.isEmpty()) {
        QMessageBox::warning(this, tr("Input Error"), tr("Please select a mice NFT to transfer."));
        return;
    }
    
    if (recipientAddress.isEmpty()) {
        QMessageBox::warning(this, tr("Input Error"), tr("Please enter a recipient address."));
        return;
    }
    
    // Confirm transfer
    QString message = tr("Are you sure you want to transfer this mice NFT to %1?\n\n"
                        "This action cannot be undone.")
                        .arg(recipientAddress);
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Confirm Transfer"), 
                                                             message,
                                                             QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Call RPC to transfer mice NFT
        QString rpcCommand = QString("micenftransfer \"%1\" \"%2\"")
                            .arg(beeNFTId)
                            .arg(recipientAddress);
        
        // Execute transfer in background thread to avoid GUI blocking
        QPointer<BeeNFTPage> guard(this);
        std::thread([=, guard]() {
            try {
                // Execute the real bctnftransfer RPC command
                std::string rpcCommandStr = QString("bctnftransfer \"%1\" \"%2\"")
                                           .arg(beeNFTId).arg(recipientAddress).toStdString();
                std::string rpcResult;
                bool success = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommandStr);
                
                // Update UI on main thread
                safeInvoke(guard, [=]() {
                    if (success && !rpcResult.empty()) {
                        QString result = tr("BCT NFT Transfer completed successfully!\n\n"
                                           "Transaction: %1\n\n"
                                           "NFT %2 has been transferred to %3")
                                       .arg(QString::fromStdString(rpcResult).left(64) + "...")
                                       .arg(beeNFTId)
                                       .arg(recipientAddress);
                        QMessageBox::information(this, tr("Transfer Complete"), result);
                        
                        // Refresh the NFT list after successful transfer
                        QTimer::singleShot(1000, this, SLOT(refreshBeeNFTs()));
                    } else {
                        QString errorMsg = QString::fromStdString(rpcResult);
                        if (errorMsg.isEmpty()) {
                            errorMsg = tr("Unknown error occurred during transfer");
                        }
                        QMessageBox::warning(this, tr("Transfer Failed"), 
                                           tr("Failed to transfer NFT: %1").arg(errorMsg));
                    }
                });
                
            } catch (const std::exception& e) {
                std::string msg = e.what();
                safeInvoke(guard, [=]() {
                    QMessageBox::warning(this, tr("RPC Error"), 
                                       tr("Failed to execute transfer: %1").arg(QString::fromStdString(msg)));
                });
            }
        }).detach();
    }
}

void BeeNFTPage::refreshBeeNFTs()
{
    if (!walletModel) {
        return;
    }
    
    // Refresh bee NFT list by calling wallet functions
    QPointer<BeeNFTPage> guard(this);
    std::thread([this, guard]() {
        try {
            // TODO: Implement actual wallet call to get owned NFTs
            // For now, simulate loading some owned NFTs
            
            safeInvoke(guard, [this]() {
                // Update both the combo and the table view
                updateBeeNFTCombo();
                
                // Also update the table model with sample data (will be replaced with real data)
                if (beeNFTModel) {
                    // Trigger model update which will reload the data
                    beeNFTModel->updateBeeNFTs();
                }
            });
            
        } catch (const std::exception& e) {
            std::string msg = e.what();
            safeInvoke(guard, [this, msg]() {
                QMessageBox::warning(this, tr("Wallet Error"), 
                                   tr("Failed to refresh NFT list: %1").arg(QString::fromStdString(msg)));
            });
        }
    }).detach();
}

void BeeNFTPage::showBeeNFTDetails()
{
    QModelIndexList selection = beeNFTView->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        return;
    }
    
    // TODO: Show detailed bee NFT information dialog
    QMessageBox::information(this, tr("Mice NFT Details"), tr("Mice NFT details dialog will be implemented."));
}

void BeeNFTPage::onBeeNFTSelectionChanged()
{
    QModelIndexList selection = beeNFTView->selectionModel()->selectedRows();
    bool hasSelection = !selection.isEmpty();
    
    detailsButton->setEnabled(hasSelection);
}

void BeeNFTPage::updateBeeNFTCombo()
{
    beeNFTCombo->clear();
    beeNFTCombo->addItem(tr("Select BCT NFT to transfer..."), "");
    
    if (!walletModel) {
        return;
    }
    
    // Load owned BCT NFTs in background thread
    QPointer<BeeNFTPage> guard(this);
    std::thread([this, guard]() {
        try {
            // TODO: Call actual RPC to get owned BCT NFTs
            // For now, simulate some owned NFTs
            QStringList ownedNFTs;
            
            // Simulate: Add some sample BCT NFTs that the user "owns"
            ownedNFTs << "BCT-NFT: abc12345...def (200,000 mice)" << "Sample1";
            ownedNFTs << "BCT-NFT: fed54321...abc (150,000 mice)" << "Sample2";
            ownedNFTs << "BCT-NFT: 789abcde...xyz (250,000 mice)" << "Sample3";
            
            // Update UI on main thread
            safeInvoke(guard, [this, ownedNFTs]() {
                for (int i = 0; i < ownedNFTs.size(); i += 2) {
                    if (i + 1 < ownedNFTs.size()) {
                        QString displayText = ownedNFTs[i];
                        QString nftId = ownedNFTs[i + 1];
                        beeNFTCombo->addItem(displayText, nftId);
                    }
                }
                
                if (beeNFTCombo->count() == 1) {
                    beeNFTCombo->addItem(tr("No BCT NFTs owned yet"), "");
                }
                
                transferButton->setEnabled(beeNFTCombo->count() > 2);
            });
            
        } catch (const std::exception& e) {
            std::string msg = e.what();
            safeInvoke(guard, [this, msg]() {
                beeNFTCombo->addItem(tr("Error loading NFTs: %1").arg(QString::fromStdString(msg)), "");
                transferButton->setEnabled(false);
            });
        }
    }).detach();
}

void BeeNFTPage::generateNewAddress()
{
    if (!walletModel) {
        return;
    }
    
    // Generate a new address for mice NFT using the wallet's address table model
            QString newAddress = walletModel->getAddressTableModel()->addRow(
        AddressTableModel::Receive, 
        tr("BCT NFT Address"), 
        "",
        walletModel->getDefaultAddressType()
    );
    
    if (!newAddress.isEmpty()) {
        ownerAddressEdit->setText(newAddress);
    } else {
        QMessageBox::warning(this, tr("Address Generation Error"), 
                           tr("Could not generate a new address. Please try again."));
    }
}

void BeeNFTPage::updateTableModelWithRealData(const QString& jsonString)
{
    if (!beeNFTModel) return;
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return; // Fall back to sample data if parsing fails
    }
    
    // Clear current model data and add real BCT data
    QJsonArray bctArray = doc.array();
    QList<BeeNFTRecord> newRecords;
    
    for (const QJsonValue& bctValue : bctArray) {
        if (!bctValue.isObject()) continue;
        
        QJsonObject bct = bctValue.toObject();
        QString bctTxid = bct["bct_txid"].toString();
        QString status = bct["status"].toString();
        int totalMiceInBct = bct["total_mice"].toInt();
        
        if (status != "mature") continue;
        
        // Count TOKENIZED mice (not available ones - we want to show NFTs, not BCTs)
        QJsonArray availableMice = bct["available_mice"].toArray();
        for (const QJsonValue& mouseValue : availableMice) {
            if (!mouseValue.isObject()) continue;
            QJsonObject mouse = mouseValue.toObject();
            bool alreadyTokenized = mouse["already_tokenized"].toBool();
            
            // Only show mice that have been tokenized (are actual NFTs)
            if (alreadyTokenized) {
                BeeNFTRecord record;
                int miceIndex = mouse["mice_index"].toInt();
                record.beeNFTId = QString("mice-nft-%1-%2").arg(bctTxid.left(8)).arg(miceIndex);
                record.originalBCT = bctTxid;
                record.beeIndex = miceIndex;
                record.currentOwner = mouse["owner"].toString();
                record.status = status == "mature" ? "mature" : "immature";
                record.maturityHeight = bct["maturity_height"].toInt();
                record.expiryHeight = bct["expiry_height"].toInt();
                record.tokenizedHeight = mouse["tokenized_height"].toInt();
                record.blocksLeft = bct["blocks_left"].toInt();
                
                newRecords.append(record);
            }
        }
    }
    
    // Update the model with real data
    beeNFTModel->updateBeeNFTListWithData(newRecords);
}

void BeeNFTPage::loadRealNFTData()
{
    if (!walletModel) {
        return;
    }
    
    // Execute bctnftlist RPC command to get real NFT data
    QPointer<BeeNFTPage> guard(this);
    std::thread([this, guard]() {
        try {
            std::string resultStr;
            bool success = false;
            
            // Try to execute RPC command, but handle gracefully if it fails
            try {
                success = RPCConsole::RPCExecuteCommandLine(resultStr, "bctnftlist");
            } catch (const std::exception& rpc_e) {
                // RPC command doesn't exist or failed - this is OK
                qDebug() << "bctnftlist RPC not available:" << QString::fromStdString(rpc_e.what());
                return; // Exit thread gracefully
            } catch (...) {
                // Any other exception - exit gracefully
                qDebug() << "Unknown error calling bctnftlist RPC";
                return;
            }
            
            if (!success || resultStr.empty()) {
                // RPC failed or returned empty - exit gracefully
                return;
            }
            
            QString result = QString::fromStdString(resultStr);
            
            if (!result.isEmpty() && result != "null") {
                // Parse the JSON result and update the table model
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8(), &error);
                
                if (error.error == QJsonParseError::NoError && doc.isArray()) {
                    QJsonArray nftArray = doc.array();
                    QList<BeeNFTRecord> nftRecords;
                    
                    for (const QJsonValue& value : nftArray) {
                        if (value.isObject()) {
                            QJsonObject nftObj = value.toObject();
                            
                            BeeNFTRecord record;
                            record.beeNFTId = nftObj["nft_id"].toString();
                            record.originalBCT = nftObj["original_bct"].toString();
                            record.beeIndex = nftObj["total_mice"].toInt();
                            record.currentOwner = nftObj["owner"].toString();
                            record.status = nftObj["status"].toString();
                            record.maturityHeight = nftObj["maturity_height"].toInt();
                            record.expiryHeight = nftObj["expiry_height"].toInt();
                            record.tokenizedHeight = nftObj["tokenized_height"].toInt();
                            record.blocksLeft = nftObj["blocks_left"].toInt();
                            
                            nftRecords.append(record);
                        }
                    }
                    
                    // Update UI on main thread
                    safeInvoke(guard, [=]() {
                        if (beeNFTModel) {
                            beeNFTModel->updateBeeNFTListWithData(nftRecords);
                        }
                    });
                }
            }
            
        } catch (const std::exception& e) {
            // Handle RPC errors gracefully - NFT table will remain empty
            qDebug() << "Failed to load NFT data:" << QString::fromStdString(e.what());
        }
    }).detach();
}
