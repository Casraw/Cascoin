// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Cascoin: Labyrinth

#ifndef BITCOIN_QT_LABYRINTHDIALOG_H
#define BITCOIN_QT_LABYRINTHDIALOG_H

#include <qt/guiutil.h>

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>
#include <QTimer>
#include <QVariant>

#include <pow.h>
#include <qt/qcustomplot.h>
#include <cmath>

class PlatformStyle;
class ClientModel;
class WalletModel;

namespace Ui {
    class LabyrinthDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

extern MousePopGraphPoint mousePopGraph[1024*40];

class QCPAxisTickerGI : public QCPAxisTicker
{
public:
    double global100;

    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) {
        if (global100 <= 0 || !std::isfinite(global100))
            return QString::number(tick, 'f', 0);
        double pct = tick / global100 * 100.0;
        if (!std::isfinite(pct))
            return QString::number(tick, 'f', 0);
        return QString::number((int)qBound(-1e9, (double)qRound(qBound(-1e8, pct, 1e8)), 1e9)) + "%";
    }

protected:
    // Generate ticks at positions corresponding to round percentage values
    double getTickStep(const QCPRange &range) Q_DECL_OVERRIDE {
        if (global100 <= 0) return QCPAxisTicker::getTickStep(range);
        double rangePct = (range.upper - range.lower) / global100 * 100.0;
        // Pick a nice percentage step (10%, 20%, 25%, 50%, etc.)
        double rawStep = rangePct / 6.0; // aim for ~6 ticks
        if (rawStep < 1e-9) return QCPAxisTicker::getTickStep(range);
        static const double niceSteps[] = {5, 10, 20, 25, 50, 100};
        double pctStep = niceSteps[0];
        for (double s : niceSteps) {
            if (s >= rawStep) { pctStep = s; break; }
        }
        return pctStep / 100.0 * global100;
    }

    QVector<double> createTickVector(double tickStep, const QCPRange &range) Q_DECL_OVERRIDE {
        QVector<double> ticks;
        if (global100 <= 0 || tickStep <= 0) return QCPAxisTicker::createTickVector(tickStep, range);
        double pctStep = tickStep / global100 * 100.0;
        if (pctStep < 1e-9) return QCPAxisTicker::createTickVector(tickStep, range);
        double endPct = range.upper / global100 * 100.0;
        double startPct = qCeil(range.lower / global100 * 100.0 / pctStep) * pctStep;
        if ((endPct - startPct) / pctStep > 20)
            pctStep = (endPct - startPct) / 6.0;
        if (pctStep < 1e-9) return QCPAxisTicker::createTickVector(tickStep, range);
        startPct = qCeil(range.lower / global100 * 100.0 / pctStep) * pctStep;
        for (double pct = startPct; pct <= endPct + 0.5; pct += pctStep) {
            double rounded = qBound(-1e8, pct, 1e8);
            ticks.append(qRound(rounded) / 100.0 * global100);
        }
        return ticks;
    }
};

class QCPAxisTickerHumanReadable : public QCPAxisTicker
{
public:
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) {
        double abs = qAbs(tick);
        if (abs >= 1e9)
            return QString::number(tick / 1e9, 'f', 1) + "B";
        if (abs >= 1e6)
            return QString::number(tick / 1e6, 'f', 1) + "M";
        if (abs >= 1e3)
            return QString::number(tick / 1e3, 'f', 1) + "K";
        return QString::number(tick, 'f', 0);
    }
};

class LabyrinthDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        CREATED_COLUMN_WIDTH = 130,
        COUNT_COLUMN_WIDTH = 110,
        STATUS_COLUMN_WIDTH = 110,
        TIME_COLUMN_WIDTH = 290,
        COST_COLUMN_WIDTH = 100,
        ROI_COLUMN_WIDTH = 65
    };

    explicit LabyrinthDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~LabyrinthDialog();

    void setClientModel(ClientModel *_clientModel);
    void setModel(WalletModel *model);
    static QString formatLargeNoLocale(int i);

public Q_SLOTS:
    void updateData(bool forceGlobalSummaryUpdate = false);
    void updateLabyrinthSummary();
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);
    void setEncryptionStatus(int status);
    void onBlocksChanged();  // Cascoin: Auto-update labyrinth when blocks change

Q_SIGNALS:
    void labyrinthStatusIconChanged(QString icon, QString tooltip);    

private:
    Ui::LabyrinthDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    ClientModel *clientModel;
    WalletModel *model;
    const PlatformStyle *platformStyle;
    CAmount mouseCost, totalCost;
    int immature, mature, dead, blocksFound;
    CAmount rewardsPaid, cost, profit;
    CAmount potentialRewards;
    CAmount currentBalance;
    double mousePopIndex;
    int lastGlobalCheckHeight;
    virtual void resizeEvent(QResizeEvent *event);
    virtual void showEvent(QShowEvent *event);
    virtual void hideEvent(QHideEvent *event);
    QCPItemText *graphMouseoverText;
    QCPItemTracer *graphTracerMature;
    QCPItemTracer *graphTracerImmature;
    QCPItemLine *globalMarkerLine;
    QSharedPointer<QCPAxisTickerGI> giTicker;
    QTimer *updateTimer;  // Timer for debouncing checkbox state changes
    QTimer *periodicRefreshTimer;  // Timer for periodic labyrinth refresh
    QTimer *blockUpdateTimer = nullptr;  // Timer for debouncing block change updates
    QLabel *syncOverlayLabel = nullptr;  // Shown while node is syncing/reindexing

    void updateTotalCostDisplay();
    void updateSyncOverlay();
    void initGraph();
    void updateGraph();
    void showPointToolTip(QMouseEvent *event);
    void setAmountField(QLabel *field, CAmount value);

private Q_SLOTS:
    void on_showLabyrinthOptionsButton_clicked();    // Cascoin: Labyrinth: Mining optimisations: Shortcut to Labyrinth mining options
    void on_createMiceButton_clicked();
    void on_mouseCountSpinner_valueChanged(int i);
    void on_includeDeadMiceCheckbox_stateChanged();
    void onUpdateTimerTimeout();
    void onPeriodicRefresh();  // Cascoin: Periodic refresh for labyrinth
    void on_showAdvancedStatsCheckbox_stateChanged();
    void updateDisplayUnit();
    void on_retryGlobalSummaryButton_clicked();
    void on_refreshGlobalSummaryButton_clicked();
    void on_releaseSwarmButton_clicked();
    void onMouseMove(QMouseEvent* event);
};

#endif // BITCOIN_QT_LABYRINTHDIALOG_H
