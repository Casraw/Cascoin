// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CPUSETTINGSDIALOG_H
#define BITCOIN_QT_CPUSETTINGSDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>

/**
 * Dialog for configuring CPU usage settings to prevent GUI freezing
 */
class CPUSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CPUSettingsDialog(QWidget *parent = nullptr);

private Q_SLOTS:
    void onMiningThreadsChanged(int value);
    void onGuiPriorityChanged(bool enabled);
    void onAdaptivePollingChanged(bool enabled);
    void applySettings();
    void resetToDefaults();

private:
    QSlider* miningThreadsSlider;
    QLabel* miningThreadsLabel;
    QCheckBox* guiPriorityCheckbox;
    QCheckBox* adaptivePollingCheckbox;
    QLabel* recommendationLabel;
    
    void updateRecommendation();
    int getSystemCoreCount();
};

#endif // BITCOIN_QT_CPUSETTINGSDIALOG_H