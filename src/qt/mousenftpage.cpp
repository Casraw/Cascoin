// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/mousenftpage.h>
#include <qt/walletmodel.h>
#include <qt/platformstyle.h>
#include <qt/guiutil.h>
#include <qt/bitcoinunits.h>
#include <qt/rpcconsole.h>
#include <qt/addresstablemodel.h>
#include <qt/mousenfttablemodel.h>

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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <thread>

MouseNFTPage::MouseNFTPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    walletModel(0),
    mouseNFTModel(0),
    platformStyle(_platformStyle)
{
    // BCTDatabaseSQLite is accessed via singleton - no initialization needed here
    setupUI();
}

MouseNFTPage::~MouseNFTPage()
{
    // BCTDatabaseSQLite is a singleton - no cleanup needed here
}

void MouseNFTPage::setModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;
    if (_walletModel) {
        // Initialize mouse NFT model
        if (!mouseNFTModel) {
            mouseNFTModel = new MouseNFTTableModel(_walletModel);
            mouseNFTView->setModel(mouseNFTModel);
            
            // Connect model signals
            connect(mouseNFTModel, SIGNAL(mouseNFTsChanged()), this, SLOT(updateMouseNFTCombo()));

            // Connect selection after model is set
            if (mouseNFTView->selectionModel()) {
                connect(mouseNFTView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
                        this, SLOT(onMouseNFTSelectionChanged()));
            }
        }
        
        updateMouseNFTCombo();
        refreshMouseNFTs();
        
        // Load mice asynchronously to not block GUI startup
        QTimer::singleShot(100, this, SLOT(loadAvailableMice()));
    }
}

void MouseNFTPage::setupUI()
{
    setWindowTitle(tr("Mice NFTs"));
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Create tab widget
    tabWidget = new QTabWidget(this);
    
    // === Mouse NFT List Tab ===
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
    
    // Mouse NFT table
    mouseNFTView = new QTableView();
    mouseNFTView->setSelectionBehavior(QAbstractItemView::SelectRows);
    mouseNFTView->setSelectionMode(QAbstractItemView::SingleSelection);
    mouseNFTView->setAlternatingRowColors(true);
    mouseNFTView->setContextMenuPolicy(Qt::CustomContextMenu);
    mouseNFTView->setSortingEnabled(true);
    
    listLayout->addWidget(mouseNFTView);
    
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
    mouseNFTCombo = new QComboBox();
    mouseNFTCombo->setToolTip(tr("Select the mice NFT to transfer"));
    transferGridLayout->addWidget(mouseNFTCombo, 0, 1, 1, 2);
    
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
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refreshMouseNFTs()));
    connect(detailsButton, SIGNAL(clicked()), this, SLOT(showMouseNFTDetails()));
    connect(refreshMiceButton, SIGNAL(clicked()), this, SLOT(loadAvailableMice()));
    connect(tokenizeButton, SIGNAL(clicked()), this, SLOT(tokenizeMouse()));
    connect(transferButton, SIGNAL(clicked()), this, SLOT(transferMouseNFT()));
    connect(generateAddressButton, SIGNAL(clicked()), this, SLOT(generateNewAddress()));
    
    // Initialize debouncing timer for checkbox state changes
    refreshTimer = new QTimer(this);
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(300); // 300ms debounce delay
    connect(refreshTimer, &QTimer::timeout, this, [this]() {
        // Disable checkbox during refresh to provide visual feedback
        showExpiredCheckBox->setEnabled(false);
        showExpiredCheckBox->setText(tr("Show expired mice NFTs (updating...)"));
        
        refreshMouseNFTs();
        
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

void MouseNFTPage::loadAvailableMice()
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
    std::thread([this]() {
        // Inform splash that mice DB init starts
        uiInterface.ShowProgress("Mice DB initialisieren", 1, false);
        std::string rpcResult;
        std::string rpcCommand = "miceavailable";

        // Cascoin: Memory leak fix - Add timeout and error handling for large wallets
        LogPrintf("Starting BCT/Mice database initialization (memory optimized)\n");
        
        bool rpcOk = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommand);

        if (!rpcOk) {
            // Fallback to local DB on UI thread
            QMetaObject::invokeMethod(this, "loadAvailableMiceFromWallet", Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, [this]() {
                mouseSelectionCombo->setEnabled(true);
                bctStatusLabel->setText(tr("Using local BCT cache"));
                bctProgressBar->setValue(100);
            }, Qt::QueuedConnection);
            uiInterface.ShowProgress("Mice DB initialisieren", 100, false);
            return;
        }

        // Parse and populate on UI thread
        QMetaObject::invokeMethod(this, [this, rpcResult]() {
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
                if (mouseNFTModel) {
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
        }, Qt::QueuedConnection);
    }).detach();
}

void MouseNFTPage::loadAvailableMiceFromWallet()
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
        
        // Add BCT entry with mouse count
        QString displayText = QString("BCT %1 — %2 mice available (mature)")
                           .arg(QString::fromStdString(bct.txid).left(8) + "...")
                           .arg(bct.mouseCount);
        
        // Store BCT txid for tokenization
        mouseSelectionCombo->addItem(displayText, QString::fromStdString(bct.txid));
        totalAvailableMice += bct.mouseCount;
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

void MouseNFTPage::loadSampleBCTData()
{
    // Sample data is no longer needed - BCTDatabaseSQLite is populated from blockchain
    // This function is kept for compatibility but does nothing
    LogPrintf("MouseNFTPage::loadSampleBCTData() - Sample data not needed with SQLite database\n");
}

void MouseNFTPage::tokenizeMouse()
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

void MouseNFTPage::showMouseSelectionDialog(const QString& bctId, const QString& ownerAddress)
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
    std::thread([=]() {
        try {
            int totalMiceCount = 0;
            QString status = "";
            bool dataLoaded = false;

            // Primary: Query BCTDatabaseSQLite for the selected BCT
            BCTDatabaseSQLite* bctDb = BCTDatabaseSQLite::instance();
            if (bctDb && bctDb->isInitialized()) {
                BCTRecord bctRecord = bctDb->getBCT(bctId.toStdString());
                if (!bctRecord.txid.empty()) {
                    totalMiceCount = bctRecord.mouseCount;
                    status = QString::fromStdString(bctRecord.status);
                    dataLoaded = true;
                }
            }

            // Fallback: Query miceavailable RPC if SQLite didn't have the data
            if (!dataLoaded) {
                std::string rpcResult;
                bool rpcOk = RPCConsole::RPCExecuteCommandLine(rpcResult, "miceavailable");
                if (rpcOk && !rpcResult.empty()) {
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(rpcResult).toUtf8(), &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
                        QJsonArray bctArray = doc.array();
                        for (const QJsonValue& bctValue : bctArray) {
                            if (!bctValue.isObject()) continue;
                            QJsonObject bctObj = bctValue.toObject();
                            if (bctObj["bct_txid"].toString() == bctId) {
                                totalMiceCount = bctObj["total_mice"].toInt();
                                status = bctObj["status"].toString();
                                dataLoaded = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (!dataLoaded) {
                QMetaObject::invokeMethod(infoLabel, [=]() {
                    infoLabel->setText(tr("Could not load BCT data. Please try again."));
                }, Qt::QueuedConnection);
                return;
            }
            
            // Update UI on main thread
            QMetaObject::invokeMethod(infoLabel, [=]() {
                // Format number with thousands separators
                QString formattedMiceCount = QString::number(totalMiceCount);
                int len = formattedMiceCount.length();
                for (int i = len - 3; i > 0; i -= 3) {
                    formattedMiceCount.insert(i, ',');
                }
                infoLabel->setText(tr("BCT Status: %1\nTotal Mice: %2\nThis will create 1 BCT-NFT containing all mice.")
                                  .arg(status).arg(formattedMiceCount));
                buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
            }, Qt::QueuedConnection);
            
        } catch (...) {
            QMetaObject::invokeMethod(infoLabel, [=]() {
                infoLabel->setText(tr("Error loading BCT data"));
            }, Qt::QueuedConnection);
        }
    }).detach();
    
    // Show dialog and handle result
    if (dialog.exec() == QDialog::Accepted) {
        // Execute complete BCT tokenization
        executeCompleteBCTTokenization(bctId, ownerAddress);
    }
}

void MouseNFTPage::executeCompleteBCTTokenization(const QString& bctId, const QString& ownerAddress)
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
        std::thread([=]() {
            try {
                // Execute the real bctnftokenize RPC command for complete BCT tokenization
                std::string rpcCommand = "bctnftokenize \"" + bctId.toStdString() + "\" \"" + ownerAddress.toStdString() + "\"";
                std::string rpcResult;
                bool success = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommand);
                
                QMetaObject::invokeMethod(this, [=]() {
                    if (success && !rpcResult.empty()) {
                        QString result = tr("BCT Tokenization completed successfully!\n\n"
                                           "Transaction: %1\n\n"
                                           "BCT %2 has been tokenized as a single NFT.")
                                       .arg(QString::fromStdString(rpcResult).left(64) + "...")
                                       .arg(bctId.left(12) + "...");
                        QMessageBox::information(this, tr("BCT Tokenization Complete"), result);
                        
                        // Refresh the BCT list after successful tokenization
                        QTimer::singleShot(1000, this, SLOT(loadAvailableMice()));
                        QTimer::singleShot(1000, this, SLOT(refreshMouseNFTs()));
                    } else {
                        QString errorMsg = QString::fromStdString(rpcResult);
                        if (errorMsg.isEmpty()) {
                            errorMsg = tr("Unknown error occurred during tokenization");
                        }
                        QMessageBox::warning(this, tr("BCT Tokenization Failed"), 
                                           tr("Failed to tokenize BCT: %1").arg(errorMsg));
                    }
                }, Qt::QueuedConnection);
                
            } catch (const std::exception& e) {
                QMetaObject::invokeMethod(this, [=]() {
                    QMessageBox::warning(this, tr("BCT Tokenization Error"), 
                                       tr("Failed to execute BCT tokenization: %1").arg(QString::fromStdString(e.what())));
                }, Qt::QueuedConnection);
            }
        }).detach();
    }
}

void MouseNFTPage::transferMouseNFT()
{
    if (!walletModel) {
        return;
    }
    
    QString mouseNFTId = mouseNFTCombo->currentData().toString();
    QString recipientAddress = recipientAddressEdit->text().trimmed();
    
    // Validate inputs
    if (mouseNFTId.isEmpty()) {
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
        // Execute transfer in background thread to avoid GUI blocking
        std::thread([=]() {
            try {
                // Use bctnftransfer RPC for BCT NFT transfers
                std::string rpcCommandStr = QString("bctnftransfer \"%1\" \"%2\"")
                                           .arg(mouseNFTId).arg(recipientAddress).toStdString();
                std::string rpcResult;
                bool success = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommandStr);
                
                // Update UI on main thread
                QMetaObject::invokeMethod(this, [=]() {
                    if (success && !rpcResult.empty()) {
                        QString result = tr("BCT NFT Transfer completed successfully!\n\n"
                                           "Transaction: %1\n\n"
                                           "NFT %2 has been transferred to %3")
                                       .arg(QString::fromStdString(rpcResult).left(64) + "...")
                                       .arg(mouseNFTId)
                                       .arg(recipientAddress);
                        QMessageBox::information(this, tr("Transfer Complete"), result);
                        
                        // Refresh the NFT list after successful transfer
                        QTimer::singleShot(1000, this, SLOT(refreshMouseNFTs()));
                    } else {
                        QString errorMsg = QString::fromStdString(rpcResult);
                        if (errorMsg.isEmpty()) {
                            errorMsg = tr("Unknown error occurred during transfer");
                        }
                        QMessageBox::warning(this, tr("Transfer Failed"), 
                                           tr("Failed to transfer NFT: %1").arg(errorMsg));
                    }
                }, Qt::QueuedConnection);
                
            } catch (const std::exception& e) {
                QMetaObject::invokeMethod(this, [=]() {
                    QMessageBox::warning(this, tr("RPC Error"), 
                                       tr("Failed to execute transfer: %1").arg(QString::fromStdString(e.what())));
                }, Qt::QueuedConnection);
            }
        }).detach();
    }
}

void MouseNFTPage::refreshMouseNFTs()
{
    if (!walletModel) {
        return;
    }
    
    // Refresh mouse NFT list by calling micenftlist RPC in background
    bool showExpired = showExpiredCheckBox->isChecked();
    std::thread([this, showExpired]() {
        try {
            std::string rpcResult;
            std::string rpcCommand = showExpired ? "micenftlist true" : "micenftlist";
            bool rpcOk = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommand);

            QMetaObject::invokeMethod(this, [this, rpcOk, rpcResult]() {
                // Update the transfer combo
                updateMouseNFTCombo();

                if (!rpcOk) {
                    // RPC failed — clear the model to show empty state (no crash)
                    if (mouseNFTModel) {
                        mouseNFTModel->updateMouseNFTListWithData(QList<MouseNFTRecord>());
                    }
                    return;
                }

                if (!mouseNFTModel) {
                    return;
                }

                // Parse the JSON array from the micenftlist RPC result
                QString jsonString = QString::fromStdString(rpcResult);
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);

                if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
                    // JSON parse error — clear the model and warn the user
                    mouseNFTModel->updateMouseNFTListWithData(QList<MouseNFTRecord>());
                    QMessageBox::warning(this, tr("Wallet Error"),
                                       tr("Failed to parse NFT list: %1").arg(parseError.errorString()));
                    return;
                }

                QJsonArray nftArray = doc.array();

                if (nftArray.isEmpty()) {
                    // Empty result — clear the model (informational empty state, no crash)
                    mouseNFTModel->updateMouseNFTListWithData(QList<MouseNFTRecord>());
                    return;
                }

                QList<MouseNFTRecord> records;

                for (const QJsonValue& value : nftArray) {
                    if (!value.isObject()) continue;

                    QJsonObject nftObj = value.toObject();

                    MouseNFTRecord record;
                    record.mouseNFTId = nftObj["nft_id"].toString();
                    if (record.mouseNFTId.isEmpty())
                        record.mouseNFTId = nftObj["mice_nft_id"].toString();
                    record.originalBCT = nftObj["original_bct"].toString();
                    record.mouseIndex = nftObj["mouse_index"].toInt();
                    record.currentOwner = nftObj["owner"].toString();
                    record.status = nftObj["status"].toString();
                    record.maturityHeight = nftObj["maturity_height"].toInt();
                    record.expiryHeight = nftObj["expiry_height"].toInt();
                    record.tokenizedHeight = nftObj["tokenized_height"].toInt();
                    record.blocksLeft = nftObj["blocks_left"].toInt();

                    records.append(record);
                }

                mouseNFTModel->updateMouseNFTListWithData(records);
            }, Qt::QueuedConnection);
            
        } catch (const std::exception& e) {
            std::string errMsg = e.what();
            QMetaObject::invokeMethod(this, [this, errMsg]() {
                if (mouseNFTModel) {
                    mouseNFTModel->updateMouseNFTListWithData(QList<MouseNFTRecord>());
                }
                QMessageBox::warning(this, tr("Wallet Error"), 
                                   tr("Failed to refresh NFT list: %1").arg(QString::fromStdString(errMsg)));
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this]() {
                if (mouseNFTModel) {
                    mouseNFTModel->updateMouseNFTListWithData(QList<MouseNFTRecord>());
                }
                QMessageBox::warning(this, tr("Wallet Error"),
                                   tr("Failed to refresh NFT list due to an unexpected error."));
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void MouseNFTPage::showMouseNFTDetails()
{
    QModelIndexList selection = mouseNFTView->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        return;
    }

    // Extract the full NFT ID from the selected row (column 0, UserRole for untruncated value)
    QModelIndex nftIdIndex = selection.first();
    QModelIndex col0Index = mouseNFTView->model()->index(nftIdIndex.row(), 0);
    QString nftId = col0Index.data(Qt::UserRole).toString();
    if (nftId.isEmpty()) {
        // Fallback: try ToolTipRole which contains the full ID in a formatted string
        nftId = col0Index.data(Qt::EditRole).toString().remove("...");
    }
    if (nftId.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Could not determine NFT ID from selection."));
        return;
    }

    // Call micenftinfo RPC in a background thread
    std::thread([this, nftId]() {
        try {
            std::string rpcResult;
            std::string rpcCommand = "micenftinfo \"" + nftId.toStdString() + "\"";
            bool rpcOk = RPCConsole::RPCExecuteCommandLine(rpcResult, rpcCommand);

            // Pass the result to the main thread for dialog construction (task 7.2)
            QMetaObject::invokeMethod(this, [this, rpcOk, rpcResult, nftId]() {
                if (!rpcOk || rpcResult.empty()) {
                    QMessageBox::warning(this, tr("NFT Details Error"),
                                       tr("Failed to retrieve details for NFT %1.").arg(nftId));
                    return;
                }

                // Parse the JSON response
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(rpcResult).toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    QMessageBox::warning(this, tr("NFT Details Error"),
                                       tr("Failed to parse NFT details: %1").arg(parseError.errorString()));
                    return;
                }

                QJsonObject nftData = doc.object();
                showMouseNFTDetailsDialog(nftData);
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            std::string errMsg = e.what();
            QMetaObject::invokeMethod(this, [this, errMsg, nftId]() {
                QMessageBox::warning(this, tr("NFT Details Error"),
                                   tr("Failed to retrieve details for NFT %1: %2")
                                   .arg(nftId).arg(QString::fromStdString(errMsg)));
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this, nftId]() {
                QMessageBox::warning(this, tr("NFT Details Error"),
                                   tr("An unexpected error occurred while retrieving details for NFT %1.").arg(nftId));
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void MouseNFTPage::onMouseNFTSelectionChanged()
{
    QModelIndexList selection = mouseNFTView->selectionModel()->selectedRows();
    bool hasSelection = !selection.isEmpty();
    
    detailsButton->setEnabled(hasSelection);
}

void MouseNFTPage::updateMouseNFTCombo()
{
    mouseNFTCombo->clear();
    mouseNFTCombo->addItem(tr("Select BCT NFT to transfer..."), "");
    
    if (!walletModel) {
        return;
    }
    
    // Load owned BCT NFTs using bctnftlist or micenftlist RPC
    std::thread([=]() {
        try {
            std::string rpcResult;
            bool success = false;
            
            // Try bctnftlist first, then fall back to micenftlist
            try {
                success = RPCConsole::RPCExecuteCommandLine(rpcResult, "micenftlist");
            } catch (...) {
                success = false;
            }
            
            // Update UI on main thread
            QMetaObject::invokeMethod(this, [=]() {
                if (success && !rpcResult.empty() && rpcResult != "[\n]\n" && rpcResult != "[]") {
                    // Parse JSON result
                    QJsonParseError error;
                    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(rpcResult).toUtf8(), &error);
                    
                    if (error.error == QJsonParseError::NoError && doc.isArray()) {
                        QJsonArray nftArray = doc.array();
                        for (const QJsonValue& value : nftArray) {
                            if (value.isObject()) {
                                QJsonObject nftObj = value.toObject();
                                QString nftId = nftObj["mice_nft_id"].toString();
                                if (nftId.isEmpty()) nftId = nftObj["nft_id"].toString();
                                QString bctId = nftObj["original_bct"].toString();
                                int totalMice = nftObj["total_mice"].toInt();
                                QString status = nftObj["status"].toString();
                                
                                if (!nftId.isEmpty() && status != "expired") {
                                    QString displayText = QString("BCT-NFT: %1 (%2 mice, %3)")
                                                         .arg(nftId.left(12) + "...")
                                                         .arg(totalMice)
                                                         .arg(status);
                                    mouseNFTCombo->addItem(displayText, nftId);
                                }
                            }
                        }
                    }
                }
                
                // If no NFTs found from RPC, scan wallet for NFT transactions
                if (mouseNFTCombo->count() <= 1 && walletModel) {
                    // Scan wallet transactions for CASTOK outputs we own
                    std::string scanResult;
                    bool scanOk = false;
                    try {
                        scanOk = RPCConsole::RPCExecuteCommandLine(scanResult, "miceavailable");
                    } catch (...) {
                        scanOk = false;
                    }
                    
                    if (scanOk && !scanResult.empty()) {
                        QJsonParseError scanError;
                        QJsonDocument scanDoc = QJsonDocument::fromJson(QString::fromStdString(scanResult).toUtf8(), &scanError);
                        if (scanError.error == QJsonParseError::NoError && scanDoc.isArray()) {
                            QJsonArray bctArray = scanDoc.array();
                            for (const QJsonValue& bctValue : bctArray) {
                                if (!bctValue.isObject()) continue;
                                QJsonObject bct = bctValue.toObject();
                                QString bctTxid = bct["bct_txid"].toString();
                                int totalMice = bct["total_mice"].toInt();
                                
                                // Check for tokenized mice in this BCT
                                QJsonArray availableMice = bct["available_mice"].toArray();
                                for (const QJsonValue& mouseValue : availableMice) {
                                    if (!mouseValue.isObject()) continue;
                                    QJsonObject mouse = mouseValue.toObject();
                                    bool alreadyTokenized = mouse["already_tokenized"].toBool();
                                    if (alreadyTokenized) {
                                        QString tokenTxid = mouse["token_txid"].toString();
                                        int miceIndex = mouse["mouse_index"].toInt();
                                        if (!tokenTxid.isEmpty()) {
                                            QString displayText = QString("BCT-NFT: %1 (mouse #%2 from BCT %3)")
                                                                 .arg(tokenTxid.left(12) + "...")
                                                                 .arg(miceIndex)
                                                                 .arg(bctTxid.left(8) + "...");
                                            mouseNFTCombo->addItem(displayText, tokenTxid);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                if (mouseNFTCombo->count() <= 1) {
                    mouseNFTCombo->addItem(tr("No BCT NFTs owned yet"), "");
                }
                
                transferButton->setEnabled(mouseNFTCombo->count() > 1);
            }, Qt::QueuedConnection);
            
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [=]() {
                mouseNFTCombo->addItem(tr("Error loading NFTs: %1").arg(QString::fromStdString(e.what())), "");
                transferButton->setEnabled(false);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void MouseNFTPage::generateNewAddress()
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

void MouseNFTPage::updateTableModelWithRealData(const QString& jsonString)
{
    if (!mouseNFTModel) return;
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return; // Fall back to sample data if parsing fails
    }
    
    // Clear current model data and add real BCT data
    QJsonArray bctArray = doc.array();
    QList<MouseNFTRecord> newRecords;
    
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
                MouseNFTRecord record;
                int miceIndex = mouse["mice_index"].toInt();
                record.mouseNFTId = QString("mice-nft-%1-%2").arg(bctTxid.left(8)).arg(miceIndex);
                record.originalBCT = bctTxid;
                record.mouseIndex = miceIndex;
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
    mouseNFTModel->updateMouseNFTListWithData(newRecords);
}

void MouseNFTPage::showMouseNFTDetailsDialog(const QJsonObject& nftData)
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Mouse NFT Details"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(520, 0); // width hint; height will adjust to content

    QVBoxLayout *mainLayout = new QVBoxLayout(dialog);

    // --- Grid of NFT fields ---
    QGridLayout *grid = new QGridLayout();
    int row = 0;

    auto addField = [&](const QString& label, const QString& value) {
        QLabel *lbl = new QLabel(QString("<b>%1:</b>").arg(label));
        QLabel *val = new QLabel(value);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setWordWrap(true);
        grid->addWidget(lbl, row, 0, Qt::AlignTop);
        grid->addWidget(val, row, 1);
        ++row;
        return val; // return so caller can style it
    };

    addField(tr("NFT ID"),          nftData["mice_nft_id"].toString());
    addField(tr("Original BCT"),    nftData["original_bct"].toString());
    addField(tr("Mouse Index"),     QString::number(nftData["mouse_index"].toInt()));
    addField(tr("Current Owner"),   nftData["current_owner"].toString());

    // Status with color coding
    QString status = nftData["status"].toString();
    QLabel *statusLabel = addField(tr("Status"), status);
    if (status == "mature") {
        statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
    } else if (status == "immature") {
        statusLabel->setStyleSheet("QLabel { color: #cc8800; font-weight: bold; }"); // yellow/orange
    } else if (status == "expired") {
        statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    }

    addField(tr("Maturity Height"), QString::number(nftData["maturity_height"].toInt()));
    addField(tr("Expiry Height"),   QString::number(nftData["expiry_height"].toInt()));
    addField(tr("Blocks Remaining"),QString::number(nftData["blocks_left"].toInt()));

    mainLayout->addLayout(grid);

    // --- Transfer history table (if present) ---
    if (nftData.contains("transfer_history") && nftData["transfer_history"].isArray()) {
        QJsonArray history = nftData["transfer_history"].toArray();
        if (!history.isEmpty()) {
            QLabel *historyLabel = new QLabel(tr("<b>Transfer History</b>"));
            mainLayout->addSpacing(8);
            mainLayout->addWidget(historyLabel);

            QTableWidget *table = new QTableWidget(history.size(), 4, dialog);
            table->setHorizontalHeaderLabels({tr("From"), tr("To"), tr("Height"), tr("TxID")});
            table->horizontalHeader()->setStretchLastSection(true);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->verticalHeader()->setVisible(false);

            for (int i = 0; i < history.size(); ++i) {
                QJsonObject entry = history[i].toObject();
                table->setItem(i, 0, new QTableWidgetItem(entry["from"].toString()));
                table->setItem(i, 1, new QTableWidgetItem(entry["to"].toString()));
                table->setItem(i, 2, new QTableWidgetItem(QString::number(entry["height"].toInt())));
                table->setItem(i, 3, new QTableWidgetItem(entry["txid"].toString()));
            }

            table->resizeColumnsToContents();
            mainLayout->addWidget(table);
        }
    }

    // --- Close button ---
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    dialog->exec();
}

void MouseNFTPage::loadRealNFTData()
{
    if (!walletModel) {
        return;
    }
    
    // Execute bctnftlist RPC command to get real NFT data
    std::thread([=]() {
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
                    QList<MouseNFTRecord> nftRecords;
                    
                    for (const QJsonValue& value : nftArray) {
                        if (value.isObject()) {
                            QJsonObject nftObj = value.toObject();
                            
                            MouseNFTRecord record;
                            record.mouseNFTId = nftObj["nft_id"].toString();
                            record.originalBCT = nftObj["original_bct"].toString();
                            record.mouseIndex = nftObj["total_mice"].toInt();
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
                    QMetaObject::invokeMethod(this, [=]() {
                        if (mouseNFTModel) {
                            mouseNFTModel->updateMouseNFTListWithData(nftRecords);
                        }
                    }, Qt::QueuedConnection);
                }
            }
            
        } catch (const std::exception& e) {
            // Handle RPC errors gracefully - NFT table will remain empty
            qDebug() << "Failed to load NFT data:" << QString::fromStdString(e.what());
        }
    }).detach();
}
