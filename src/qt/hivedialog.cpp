// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Cascoin: Labyrinth

#include <wallet/wallet.h>
#include <wallet/fees.h>

#include <qt/hivedialog.h>
#include <qt/forms/ui_hivedialog.h>
#include <qt/clientmodel.h>
#include <qt/sendcoinsdialog.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receiverequestdialog.h>

#include <thread>
#include <qt/hivetablemodel.h>
#include <qt/walletmodel.h>
#include <qt/tinypie.h>
#include <qt/qcustomplot.h>
#include <bctdb.h>  // For BCTDatabaseSQLite summary

#include <qt/optionsdialog.h> // Cascoin: Labyrinth: Mining optimisations

#include <QAction>
#include <QCursor>
#include <QHideEvent>
#include <QMessageBox>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextDocument>

#include <util.h>
#include <validation.h>

HiveDialog::HiveDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HiveDialog),
    columnResizingFixer(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons())
        ui->createMiceButton->setIcon(QIcon());
    else
        ui->createMiceButton->setIcon(_platformStyle->SingleColorIcon(":/icons/mouse"));

    mouseCost = totalCost = rewardsPaid = cost = profit = 0;
    immature = mature = dead = blocksFound = 0;
    lastGlobalCheckHeight = 0;
    potentialRewards = 0;
    currentBalance = 0;
    mousePopIndex = 0;

    ui->globalHiveSummaryError->hide();

    ui->mousePopIndexPie->foregroundCol = Qt::red;

    // Swap cols for labyrinth weight pie
    QColor temp = ui->labyrinthWeightPie->foregroundCol;
    ui->labyrinthWeightPie->foregroundCol = ui->labyrinthWeightPie->backgroundCol;
    ui->labyrinthWeightPie->backgroundCol = temp;
    ui->labyrinthWeightPie->borderCol = palette().color(backgroundRole());

    // Initialize debouncing timer for checkbox state changes
    updateTimer = new QTimer(this);
    updateTimer->setSingleShot(true);
    updateTimer->setInterval(300); // 300ms debounce delay
    connect(updateTimer, SIGNAL(timeout()), this, SLOT(onUpdateTimerTimeout()));

    // Initialize periodic refresh timer to ensure labyrinth stays current
    // Only refresh when dialog is visible to avoid unnecessary background work
    periodicRefreshTimer = new QTimer(this);
    periodicRefreshTimer->setInterval(120000); // 120 seconds (reduced frequency)
    connect(periodicRefreshTimer, SIGNAL(timeout()), this, SLOT(onPeriodicRefresh()));
    // Don't start automatically - will be started when dialog becomes visible

    initGraph();
    ui->mousePopGraph->hide();
}

void HiveDialog::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(onBlocksChanged()));
        // Removed: numConnectionsChanged was triggering expensive updateData() calls
        // The status icon update is now handled more efficiently in updateHiveSummary()
    }
}

void HiveDialog::setModel(WalletModel *_model) {
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        _model->getLabyrinthTableModel()->sort(LabyrinthTableModel::Created, Qt::DescendingOrder);
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(), _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        
        if (_model->getEncryptionStatus() != WalletModel::Locked)
            ui->releaseSwarmButton->hide();
        connect(_model, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        QTableView* tableView = ui->currentHiveView;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tableView->setModel(_model->getLabyrinthTableModel());
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tableView->setColumnWidth(LabyrinthTableModel::Created, CREATED_COLUMN_WIDTH);
        tableView->setColumnWidth(LabyrinthTableModel::Count, COUNT_COLUMN_WIDTH);
        tableView->setColumnWidth(LabyrinthTableModel::Status, STATUS_COLUMN_WIDTH);
        tableView->setColumnWidth(LabyrinthTableModel::EstimatedTime, TIME_COLUMN_WIDTH);
        tableView->setColumnWidth(LabyrinthTableModel::Cost, COST_COLUMN_WIDTH);
        tableView->setColumnWidth(LabyrinthTableModel::Rewards, REWARDS_COLUMN_WIDTH);
        //tableView->setColumnWidth(LabyrinthTableModel::Profit, PROFIT_COLUMN_WIDTH);

        // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.
        //columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, PROFIT_COLUMN_WIDTH, HIVE_COL_MIN_WIDTH, this);
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, REWARDS_COLUMN_WIDTH, HIVE_COL_MIN_WIDTH, this);

        // Connect signal to update local summary like OverviewPage
        connect(_model, SIGNAL(newLabyrinthSummaryAvailable()), this, SLOT(updateHiveSummary()));

        // Populate initial data: trigger background load
        if (_model->getLabyrinthTableModel()) {
            _model->getLabyrinthTableModel()->updateBCTs(ui->includeDeadMiceCheckbox->isChecked());
        }
    }
}

HiveDialog::~HiveDialog() {
    delete ui;
}

void HiveDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance) {
    currentBalance = balance;
    setAmountField(ui->currentBalance, currentBalance);
}

void HiveDialog::setEncryptionStatus(int status) {
    switch(status) {
        case WalletModel::Unencrypted:
        case WalletModel::Unlocked:
            ui->releaseSwarmButton->hide();
            break;
        case WalletModel::Locked:
            ui->releaseSwarmButton->show();
            break;
    }
    updateData();
}

void HiveDialog::setAmountField(QLabel *field, CAmount value) {
    field->setText(
        BitcoinUnits::format(model->getOptionsModel()->getDisplayUnit(), value)
        + " "
        + BitcoinUnits::shortName(model->getOptionsModel()->getDisplayUnit())
    );
}

QString HiveDialog::formatLargeNoLocale(int i) {
    QString i_str = QString::number(i);

    // Use SI-style thin space separators as these are locale independent and can't be confused with the decimal marker.
    QChar thin_sp(THIN_SP_CP);
    int i_size = i_str.size();
    for (int i = 3; i < i_size; i += 3)
        i_str.insert(i_size - i, thin_sp);

    return i_str;
}

void HiveDialog::updateData(bool forceGlobalSummaryUpdate) {
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Always ensure local summary UI reflects latest cached values
    updateHiveSummary();

    // Update cost-related UI regardless of sync
    mouseCost = GetMouseCost(chainActive.Tip()->nHeight, consensusParams);
    setAmountField(ui->mouseCostLabel, mouseCost);
    updateTotalCostDisplay();

    // Global summary only when not IBD
    if(IsInitialBlockDownload() || chainActive.Height() == 0) {
        ui->globalHiveSummary->hide();
        ui->globalHiveSummaryError->show();
        return;
    }

    if (forceGlobalSummaryUpdate || chainActive.Tip()->nHeight >= lastGlobalCheckHeight + 10) { // Don't update global summary every block
        int globalImmatureMice, globalImmatureBCTs, globalMatureMice, globalMatureBCTs;
        if (!GetNetworkLabyrinthInfo(globalImmatureMice, globalImmatureBCTs, globalMatureMice, globalMatureBCTs, potentialRewards, consensusParams, true)) {
            ui->globalHiveSummary->hide();
            ui->globalHiveSummaryError->show();
        } else {
            ui->globalHiveSummaryError->hide();
            ui->globalHiveSummary->show();
            if (globalImmatureMice == 0)
                ui->globalImmatureLabel->setText("0");
            else
                ui->globalImmatureLabel->setText(formatLargeNoLocale(globalImmatureMice) + " (" + QString::number(globalImmatureBCTs) + " transactions)");

            if (globalMatureMice == 0)
                ui->globalMatureLabel->setText("0");
            else
                ui->globalMatureLabel->setText(formatLargeNoLocale(globalMatureMice) + " (" + QString::number(globalMatureBCTs) + " transactions)");

            updateGraph();
        }

        setAmountField(ui->potentialRewardsLabel, potentialRewards);

        double hiveWeight = (globalMatureMice == 0) ? 0.0 : mature / (double)globalMatureMice;
        ui->localLabyrinthWeightLabel->setText(QString::number(hiveWeight, 'f', 3));
        ui->labyrinthWeightPie->setValue(hiveWeight);

        mousePopIndex = ((mouseCost * globalMatureMice) / (double)potentialRewards) * 100.0;
        if (mousePopIndex > 200) mousePopIndex = 200;
        ui->mousePopIndexLabel->setText(QString::number(floor(mousePopIndex)));
        ui->mousePopIndexPie->setValue(mousePopIndex / 100);
        
        lastGlobalCheckHeight = chainActive.Tip()->nHeight;
    }

    ui->blocksTillGlobalRefresh->setText(QString::number(10 - (chainActive.Tip()->nHeight - lastGlobalCheckHeight)));
}

void HiveDialog::updateHiveSummary() {
    if(!(model && model->getLabyrinthTableModel())) return;

    model->getLabyrinthTableModel()->getSummaryValues(immature, mature, dead, blocksFound, cost, rewardsPaid, profit);

    // IMPORTANT: Always get total rewards from database (including expired BCTs)
    // This ensures the summary shows all rewards even when expired BCTs are hidden in the table
    BCTDatabaseSQLite* bctDb = BCTDatabaseSQLite::instance();
    if (bctDb && bctDb->isInitialized()) {
        BCTSummary dbSummary = bctDb->getSummary();
        // Use database totals for rewards-related fields (includes all BCTs, even expired)
        // The table model only loads non-expired BCTs by default, so it would miss
        // blocksFound/rewards from expired mice without this database query.
        // Note: With PRAGMA busy_timeout=5000, getSummary() will wait up to 5s
        // if another thread holds a write lock, instead of returning empty results.
        blocksFound = dbSummary.blocksFound;
        rewardsPaid = dbSummary.totalRewards;
        cost = dbSummary.totalCost;
        profit = dbSummary.totalProfit;
        // Use mice counts (not transaction counts) for display
        dead = dbSummary.expiredMice;
        immature = dbSummary.immatureMice;
        mature = dbSummary.matureMice;
    }

    // Update labels
    setAmountField(ui->rewardsPaidLabel, rewardsPaid);
    setAmountField(ui->costLabel, cost);
    setAmountField(ui->profitLabel, profit);
    ui->matureLabel->setText(formatLargeNoLocale(mature));
    ui->immatureLabel->setText(formatLargeNoLocale(immature));
    ui->blocksFoundLabel->setText(QString::number(blocksFound));

    if(dead == 0) {
        ui->deadLabel->hide();
        ui->deadTitleLabel->hide();
        ui->deadLabelSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    } else {
        ui->deadLabel->setText(formatLargeNoLocale(dead));
        ui->deadLabel->show();
        ui->deadTitleLabel->show();
        ui->deadLabelSpacer->changeSize(ui->immatureLabelSpacer->geometry().width(), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    // Status icon
    QString tooltip, icon;
    if (clientModel && clientModel->getNumConnections() == 0) {
        tooltip = tr("Cascoin is not connected");
        icon = ":/icons/hivestatus_disabled";
    } else if (!model->isLabyrinthEnabled()) {
        tooltip = tr("The Labyrinth is not enabled on the network");
        icon = ":/icons/hivestatus_disabled";
    } else {
        if (mature + immature == 0) {
            tooltip = tr("No live mice currently in wallet");
            icon = ":/icons/hivestatus_clear";
        } else if (mature == 0) {
            tooltip = tr("Only immature mice currently in wallet");
            icon = ":/icons/hivestatus_orange";
        } else {
            if (model->getEncryptionStatus() == WalletModel::Locked) {
                tooltip = tr("WARNING: Mice mature but not mining because wallet is locked");
                icon = ":/icons/hivestatus_red";
            } else {
                tooltip = tr("Mice mature and mining");
                icon = ":/icons/hivestatus_green";
            }
        }
    }
    Q_EMIT labyrinthStatusIconChanged(icon, tooltip);
}

void HiveDialog::updateDisplayUnit() {
    if(model && model->getOptionsModel()) {
        setAmountField(ui->mouseCostLabel, mouseCost);
        setAmountField(ui->rewardsPaidLabel, rewardsPaid);
        setAmountField(ui->costLabel, cost);
        setAmountField(ui->profitLabel, profit);
        setAmountField(ui->potentialRewardsLabel, potentialRewards);
        setAmountField(ui->currentBalance, currentBalance);
        setAmountField(ui->totalCostLabel, totalCost);
    }

    updateTotalCostDisplay();
}

void HiveDialog::updateTotalCostDisplay() {    
    totalCost = mouseCost * ui->mouseCountSpinner->value();

    if(model && model->getOptionsModel()) {
        setAmountField(ui->totalCostLabel, totalCost);
        
        if (totalCost > model->getBalance())
            ui->mouseCountSpinner->setStyleSheet("QSpinBox{background:#FF8080;}");
        else
            ui->mouseCountSpinner->setStyleSheet("QSpinBox{background:white;}");
    }
}

void HiveDialog::on_mouseCountSpinner_valueChanged(int i) {
    updateTotalCostDisplay();
}

void HiveDialog::on_includeDeadMiceCheckbox_stateChanged() {
    // Use debounced timer to prevent rapid toggling from causing multiple expensive operations
    updateTimer->stop();
    updateTimer->start();
}

void HiveDialog::onUpdateTimerTimeout() {
    // Disable checkbox during update to provide visual feedback
    ui->includeDeadMiceCheckbox->setEnabled(false);
    ui->includeDeadMiceCheckbox->setText(tr("Include expired mice (updating...)"));

    if(model && model->getLabyrinthTableModel()) {
        model->getLabyrinthTableModel()->updateBCTs(ui->includeDeadMiceCheckbox->isChecked());
    }

    // Re-enable checkbox after update
    ui->includeDeadMiceCheckbox->setEnabled(true);
    ui->includeDeadMiceCheckbox->setText(tr("Include expired mice"));
}

void HiveDialog::onPeriodicRefresh() {
    // Only refresh if dialog is visible to avoid unnecessary background work
    if (!isVisible()) {
        return;
    }
    
    // Periodic refresh to ensure labyrinth stays current even if some notifications are missed
    if (model && model->getLabyrinthTableModel()) {
        model->getLabyrinthTableModel()->updateBCTs(ui->includeDeadMiceCheckbox->isChecked());
    }
    
    // Also update the global summary
    updateData();
}

void HiveDialog::on_showAdvancedStatsCheckbox_stateChanged() {
    if(ui->showAdvancedStatsCheckbox->isChecked()) {
        ui->mousePopGraph->show();
        ui->walletHiveStatsFrame->hide();
    } else {
        ui->mousePopGraph->hide();
        ui->walletHiveStatsFrame->show();
    }
}

void HiveDialog::on_retryGlobalSummaryButton_clicked() {
    updateData(true);
}

void HiveDialog::on_refreshGlobalSummaryButton_clicked() {
    updateData(true);
}

void HiveDialog::on_releaseSwarmButton_clicked() {
    if(model)
        model->requestUnlock(true);
}

void HiveDialog::on_createMiceButton_clicked() {
    if (model) {
        if (totalCost > model->getBalance()) {
            QMessageBox::critical(this, tr("Error"), tr("Insufficient balance to create mice."));
            return;
        }
		WalletModel::UnlockContext ctx(model->requestUnlock());
		if(!ctx.isValid())
			return;     // Unlock wallet was cancelled
        model->createMice(ui->mouseCountSpinner->value(), clientModel->getOptionsModel()->getHiveContribCF(), this, mousePopIndex); // Cascoin: MinotaurX+Hive1.2
    }
}

// Cascoin: Auto-update labyrinth when new blocks are found
void HiveDialog::onBlocksChanged() {
    // Only update if this dialog is currently visible to avoid unnecessary work
    if (!isVisible()) {
        return;
    }
    
    // Use debounced update to prevent rapid successive calls from blocking UI
    // The updateTimer already handles debouncing, reuse it for block changes too
    if (!blockUpdateTimer) {
        blockUpdateTimer = new QTimer(this);
        blockUpdateTimer->setSingleShot(true);
        blockUpdateTimer->setInterval(500); // 500ms debounce for block updates
        connect(blockUpdateTimer, &QTimer::timeout, this, [this]() {
            // Update global summary (lightweight)
            updateData();
            
            // Refresh labyrinth table in background (already threaded in updateBCTs)
            if (model && model->getLabyrinthTableModel()) {
                model->getLabyrinthTableModel()->updateBCTs(ui->includeDeadMiceCheckbox->isChecked());
            }
        });
    }
    blockUpdateTimer->start();
}

// Cascoin: Labyrinth: Mining optimisations: Shortcut to Labyrinth mining options
void HiveDialog::on_showHiveOptionsButton_clicked() {
    if(!clientModel || !clientModel->getOptionsModel())
        return;

    OptionsDialog dlg(this, model->isWalletEnabled());
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void HiveDialog::initGraph() {
    ui->mousePopGraph->addGraph();
    ui->mousePopGraph->graph(0)->setLineStyle(QCPGraph::lsLine);
    ui->mousePopGraph->graph(0)->setPen(QPen(Qt::blue));
    QColor color(42, 67, 182);
    color.setAlphaF(0.35);
    ui->mousePopGraph->graph(0)->setBrush(QBrush(color));

    ui->mousePopGraph->addGraph();
    ui->mousePopGraph->graph(1)->setLineStyle(QCPGraph::lsLine);
    ui->mousePopGraph->graph(1)->setPen(QPen(Qt::black));
    QColor color1(42, 182, 67);
    color1.setAlphaF(0.35);
    ui->mousePopGraph->graph(1)->setBrush(QBrush(color1));

    QSharedPointer<QCPAxisTickerDateTime> dateTicker(new QCPAxisTickerDateTime);
    dateTicker->setTickStepStrategy(QCPAxisTicker::TickStepStrategy::tssMeetTickCount);
    dateTicker->setTickCount(8);
    dateTicker->setDateTimeFormat("ddd d MMM");
    ui->mousePopGraph->xAxis->setTicker(dateTicker);

    ui->mousePopGraph->yAxis->setLabel("Mice");

    giTicker = QSharedPointer<QCPAxisTickerGI>(new QCPAxisTickerGI);
    ui->mousePopGraph->yAxis2->setTicker(giTicker);
    ui->mousePopGraph->yAxis2->setLabel("Global index");
    ui->mousePopGraph->yAxis2->setVisible(true);

    ui->mousePopGraph->xAxis->setTickLabelFont(QFont(QFont().family(), 8));
    ui->mousePopGraph->xAxis2->setTickLabelFont(QFont(QFont().family(), 8));
    ui->mousePopGraph->yAxis->setTickLabelFont(QFont(QFont().family(), 8));
    ui->mousePopGraph->yAxis2->setTickLabelFont(QFont(QFont().family(), 8));

    connect(ui->mousePopGraph->xAxis, SIGNAL(rangeChanged(QCPRange)), ui->mousePopGraph->xAxis2, SLOT(setRange(QCPRange)));
    connect(ui->mousePopGraph->yAxis, SIGNAL(rangeChanged(QCPRange)), ui->mousePopGraph->yAxis2, SLOT(setRange(QCPRange)));
    connect(ui->mousePopGraph, SIGNAL(mouseMove(QMouseEvent*)), this, SLOT(onMouseMove(QMouseEvent*)));

    globalMarkerLine = new QCPItemLine(ui->mousePopGraph);
    globalMarkerLine->setPen(QPen(Qt::blue, 1, Qt::DashLine));
    
    graphTracerImmature = new QCPItemTracer(ui->mousePopGraph);
    graphTracerImmature->setGraph(ui->mousePopGraph->graph(0));
    graphTracerMature = new QCPItemTracer(ui->mousePopGraph);
    graphTracerMature->setGraph(ui->mousePopGraph->graph(1));

    graphMouseoverText = new QCPItemText(ui->mousePopGraph);
}

void HiveDialog::updateGraph() {
    const Consensus::Params& consensusParams = Params().GetConsensus();

    ui->mousePopGraph->graph()->data()->clear();
    double now = QDateTime::currentDateTime().toSecsSinceEpoch();
    int totalLifespan = consensusParams.mouseGestationBlocks + consensusParams.mouseLifespanBlocks;
    QVector<QCPGraphData> dataMature(totalLifespan);
    QVector<QCPGraphData> dataImmature(totalLifespan);
    for (int i = 0; i < totalLifespan; i++) {
        dataImmature[i].key = now + consensusParams.nPowTargetSpacing / 2 * i;
        dataImmature[i].value = (double)mousePopGraph[i].immaturePop;

        dataMature[i].key = dataImmature[i].key;
        dataMature[i].value = (double)mousePopGraph[i].maturePop;
    }
    ui->mousePopGraph->graph(0)->data()->set(dataImmature);
    ui->mousePopGraph->graph(1)->data()->set(dataMature);

    double global100 = (double)potentialRewards / mouseCost;
    globalMarkerLine->start->setCoords(now, global100);
    globalMarkerLine->end->setCoords(now + consensusParams.nPowTargetSpacing / 2 * totalLifespan, global100);
    giTicker->global100 = global100;
    ui->mousePopGraph->rescaleAxes();
    ui->mousePopGraph->replot();
}

void HiveDialog::onMouseMove(QMouseEvent *event) {
    QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(sender());

    int x = (int)customPlot->xAxis->pixelToCoord(event->pos().x());
    int y = (int)customPlot->yAxis->pixelToCoord(event->pos().y());

    graphTracerImmature->setGraphKey(x);
    graphTracerMature->setGraphKey(x);
    int mouseCountImmature = (int)graphTracerImmature->position->value();
    int mouseCountMature = (int)graphTracerMature->position->value();      

    QDateTime xDateTime = QDateTime::fromSecsSinceEpoch(x);
    int global100 = (int)((double)potentialRewards / mouseCost);
    QColor traceColMature = mouseCountMature >= global100 ? Qt::red : Qt::black;
    QColor traceColImmature = mouseCountImmature >= global100 ? Qt::red : Qt::black;

    graphTracerImmature->setPen(QPen(traceColImmature, 1, Qt::DashLine));    
    graphTracerMature->setPen(QPen(traceColMature, 1, Qt::DashLine));

    graphMouseoverText->setText(xDateTime.toString("ddd d MMM") + " " + xDateTime.time().toString() + ":\n" + 
                               formatLargeNoLocale(mouseCountMature) + " " + tr("adventure mice") + "\n" + 
                               formatLargeNoLocale(mouseCountImmature) + " " + tr("resting mice"));
    graphMouseoverText->setColor(traceColMature);
    graphMouseoverText->position->setCoords(QPointF(x, y));
    QPointF pixelPos = graphMouseoverText->position->pixelPosition();

    int xoffs, yoffs;
    if (ui->mousePopGraph->height() > 150) {
        graphMouseoverText->setFont(QFont(font().family(), 10));
        xoffs = 80;
        yoffs = 30;
    } else {
        graphMouseoverText->setFont(QFont(font().family(), 8));
        xoffs = 70;
        yoffs = 20;
    }

    if (pixelPos.y() > ui->mousePopGraph->height() / 2)
        pixelPos.setY(pixelPos.y() - yoffs);
    else
        pixelPos.setY(pixelPos.y() + yoffs);

    if (pixelPos.x() > ui->mousePopGraph->width() / 2)
        pixelPos.setX(pixelPos.x() - xoffs);
    else
        pixelPos.setX(pixelPos.x() + xoffs);

    
    graphMouseoverText->position->setPixelPosition(pixelPos);

    customPlot->replot();
}

void HiveDialog::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(LabyrinthTableModel::Rewards);
}

void HiveDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // Start periodic refresh when dialog becomes visible
    if (periodicRefreshTimer && !periodicRefreshTimer->isActive()) {
        periodicRefreshTimer->start();
    }
}

void HiveDialog::hideEvent(QHideEvent *event) {
    QDialog::hideEvent(event);
    // Stop periodic refresh when dialog is hidden to save resources
    if (periodicRefreshTimer && periodicRefreshTimer->isActive()) {
        periodicRefreshTimer->stop();
    }
}
