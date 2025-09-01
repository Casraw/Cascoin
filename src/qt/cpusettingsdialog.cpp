// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/cpusettingsdialog.h>
#include <qt/cpumanager.h>
#include <util.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QApplication>
#include <QSettings>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

CPUSettingsDialog::CPUSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("CPU Settings"));
    setModal(true);
    resize(400, 300);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Mining threads group
    QGroupBox* miningGroup = new QGroupBox(tr("Mining Thread Configuration"), this);
    QVBoxLayout* miningLayout = new QVBoxLayout(miningGroup);
    
    // Mining threads slider
    QHBoxLayout* threadsLayout = new QHBoxLayout();
    threadsLayout->addWidget(new QLabel(tr("Mining Threads:"), this));
    
    miningThreadsSlider = new QSlider(Qt::Horizontal, this);
    miningThreadsSlider->setMinimum(1);
    miningThreadsSlider->setMaximum(getSystemCoreCount());
    miningThreadsSlider->setValue(CPUManager::GetRecommendedWorkerThreads(1));
    connect(miningThreadsSlider, &QSlider::valueChanged, this, &CPUSettingsDialog::onMiningThreadsChanged);
    
    miningThreadsLabel = new QLabel(this);
    onMiningThreadsChanged(miningThreadsSlider->value());
    
    threadsLayout->addWidget(miningThreadsSlider);
    threadsLayout->addWidget(miningThreadsLabel);
    miningLayout->addLayout(threadsLayout);
    
    // GUI priority checkbox
    guiPriorityCheckbox = new QCheckBox(tr("Prioritize GUI responsiveness"), this);
    guiPriorityCheckbox->setChecked(true);
    guiPriorityCheckbox->setToolTip(tr("When enabled, mining threads run at lower priority to keep the GUI responsive"));
    connect(guiPriorityCheckbox, &QCheckBox::toggled, this, &CPUSettingsDialog::onGuiPriorityChanged);
    miningLayout->addWidget(guiPriorityCheckbox);
    
    // Adaptive polling checkbox
    adaptivePollingCheckbox = new QCheckBox(tr("Enable adaptive polling"), this);
    adaptivePollingCheckbox->setChecked(true);
    adaptivePollingCheckbox->setToolTip(tr("Automatically reduce update frequency when system is under high load"));
    connect(adaptivePollingCheckbox, &QCheckBox::toggled, this, &CPUSettingsDialog::onAdaptivePollingChanged);
    miningLayout->addWidget(adaptivePollingCheckbox);
    
    mainLayout->addWidget(miningGroup);
    
    // Recommendation label
    recommendationLabel = new QLabel(this);
    recommendationLabel->setWordWrap(true);
    recommendationLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    updateRecommendation();
    mainLayout->addWidget(recommendationLabel);
    
    // Button box
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CPUSettingsDialog::applySettings);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &CPUSettingsDialog::resetToDefaults);
    
    mainLayout->addWidget(buttonBox);
}

void CPUSettingsDialog::onMiningThreadsChanged(int value)
{
    miningThreadsLabel->setText(QString::number(value));
    updateRecommendation();
}

void CPUSettingsDialog::onGuiPriorityChanged(bool enabled)
{
    updateRecommendation();
}

void CPUSettingsDialog::onAdaptivePollingChanged(bool enabled)
{
    updateRecommendation();
}

void CPUSettingsDialog::applySettings()
{
    // Save settings
    QSettings settings;
    settings.setValue("mining/threads", miningThreadsSlider->value());
    settings.setValue("gui/prioritize", guiPriorityCheckbox->isChecked());
    settings.setValue("gui/adaptivePolling", adaptivePollingCheckbox->isChecked());
    
    // Apply to command line args (temporary for this session)
    gArgs.ForceSetArg("-hivecheckthreads", std::to_string(miningThreadsSlider->value()));
    
    accept();
}

void CPUSettingsDialog::resetToDefaults()
{
    int defaultThreads = CPUManager::GetRecommendedWorkerThreads(1);
    miningThreadsSlider->setValue(defaultThreads);
    guiPriorityCheckbox->setChecked(true);
    adaptivePollingCheckbox->setChecked(true);
}

void CPUSettingsDialog::updateRecommendation()
{
    QString recommendation;
    int coreCount = getSystemCoreCount();
    int selectedThreads = miningThreadsSlider->value();
    
    if (selectedThreads >= coreCount) {
        recommendation = tr("⚠️ Using all CPU cores may cause GUI freezing. Consider reducing to %1 threads.").arg(coreCount - 1);
    } else if (selectedThreads == coreCount - 1) {
        recommendation = tr("✅ Good balance between mining performance and GUI responsiveness.");
    } else {
        recommendation = tr("ℹ️ Conservative setting. You could increase threads for better mining performance.");
    }
    
    if (guiPriorityCheckbox->isChecked()) {
        recommendation += tr(" GUI priority is enabled for better responsiveness.");
    }
    
    recommendationLabel->setText(recommendation);
}

int CPUSettingsDialog::getSystemCoreCount()
{
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

#include <qt/moc_cpusettingsdialog.cpp>