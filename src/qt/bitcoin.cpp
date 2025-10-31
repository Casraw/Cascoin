// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/bitcoingui.h>

#include <chainparams.h>
#include <qt/clientmodel.h>
#include <fs.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/intro.h>
#include <qt/networkstyle.h>
#include <qt/bctdatabase.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/splashscreen.h>
#include <qt/utilitydialog.h>
#include <qt/winshutdownmonitor.h>

#ifdef ENABLE_WALLET
#include <qt/paymentserver.h>
#include <qt/walletmodel.h>
#endif

#include <init.h>
#include <rpc/server.h>
#include <ui_interface.h>
#include <util.h>
#include <warnings.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif

#include <stdint.h>

#include <boost/thread.hpp>

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QPalette>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QtNetwork/QSslConfiguration>
#include <atomic>
#include <thread>
#include <chrono>

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
#if QT_VERSION >= 0x060000
// Qt6 static plugins
#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(Q_OS_LINUX)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
#elif defined(Q_OS_MACOS)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin);
#endif
#else
// Qt5 static plugins
#if defined(QT_QPA_PLATFORM_XCB)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_WINDOWS)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_COCOA)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin);
#endif
#endif
#endif

#if QT_VERSION < 0x050000
#include <QTextCodec>
#endif

// Declare meta types used for QMetaObject::invokeMethod
Q_DECLARE_METATYPE(bool*)
Q_DECLARE_METATYPE(CAmount)

// Cascoin: Track readiness of mice/BCT DB init so we can keep splash visible
static std::atomic<bool> g_miceDbReady{false};

static void InitMessage(const std::string &message)
{
    LogPrintf("init message: %s\n", message);
}

/*
   Translate string to current locale using Qt.
 */
static std::string Translate(const char* psz)
{
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
}

static QString GetLangTerritory()
{
    QSettings settings;
    // Get desired locale (e.g. "de_DE")
    // 1) Default to system locale to match user's system language
    QString lang_territory = QLocale::system().name();
    // Fallback to English only if system locale detection fails
    if (lang_territory.isEmpty() || lang_territory == "C") {
        lang_territory = "en";
    }
    // 2) Language from QSettings (user override)
    QString lang_territory_qsettings = settings.value("language", "").toString();
    if(!lang_territory_qsettings.isEmpty())
        lang_territory = lang_territory_qsettings;
    // 3) -lang command line argument (highest priority)
    lang_territory = QString::fromStdString(gArgs.GetArg("-lang", lang_territory.toStdString()));
    return lang_territory;
}

/** Set up translations */
static void initTranslations(QTranslator &qtTranslatorBase, QTranslator &qtTranslator, QTranslator &translatorBase, QTranslator &translator)
{
    // Remove old translators
    QApplication::removeTranslator(&qtTranslatorBase);
    QApplication::removeTranslator(&qtTranslator);
    QApplication::removeTranslator(&translatorBase);
    QApplication::removeTranslator(&translator);

    // Get desired locale (e.g. "de_DE")
    // 1) Default language (English unless overridden)
    QString lang_territory = GetLangTerritory();

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    // Load e.g. bitcoin_de.qm (shortcut "de" needs to be defined in bitcoin.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        QApplication::installTranslator(&translatorBase);

    // Load e.g. bitcoin_de_DE.qm (shortcut "de_DE" needs to be defined in bitcoin.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        QApplication::installTranslator(&translator);
}

/* qDebug() message handler --> debug.log */
#if QT_VERSION < 0x050000
void DebugMessageHandler(QtMsgType type, const char *msg)
{
    if (type == QtDebugMsg) {
        LogPrint(BCLog::QT, "GUI: %s\n", msg);
    } else {
        LogPrintf("GUI: %s\n", msg);
    }
}
#else
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    Q_UNUSED(context);
    if (type == QtDebugMsg) {
        LogPrint(BCLog::QT, "GUI: %s\n", msg.toStdString());
    } else {
        LogPrintf("GUI: %s\n", msg.toStdString());
    }
}
#endif

/** Class encapsulating Bitcoin Core startup and shutdown.
 * Allows running startup and shutdown in a different thread from the UI thread.
 */
class BitcoinCore: public QObject
{
    Q_OBJECT
public:
    explicit BitcoinCore();
    /** Basic initialization, before starting initialization/shutdown thread.
     * Return true on success.
     */
    static bool baseInitialize();

public Q_SLOTS:
    void initialize();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success);
    void shutdownResult();
    void runawayException(const QString &message);

private:

    /// Pass fatal exception message to UI thread
    void handleRunawayException(const std::exception *e);
};

/** Main Bitcoin application object */
class BitcoinApplication: public QApplication
{
    Q_OBJECT
public:
    explicit BitcoinApplication(int &argc, char **argv);
    ~BitcoinApplication();

#ifdef ENABLE_WALLET
    /// Create payment server
    void createPaymentServer();
#endif
    /// parameter interaction/setup based on rules
    void parameterSetup();
    /// Create options model
    void createOptionsModel(bool resetSettings);
    /// Create main window
    void createWindow(const NetworkStyle *networkStyle);
    /// Create splash screen
    void createSplashScreen(const NetworkStyle *networkStyle);

    /// Request core initialization
    void requestInitialize();
    /// Request core shutdown
    void requestShutdown();

    /// Get process return value
    int getReturnValue() const { return returnValue; }

    /// Get window identifier of QMainWindow (BitcoinGUI)
    WId getMainWinId() const;

public Q_SLOTS:
    void initializeResult(bool success);
    void shutdownResult();
    /// Handle runaway exceptions. Shows a message box with the problem and quits the program.
    void handleRunawayException(const QString &message);
    void forceQuitIfShutdownStuck();
    void hardExit();

Q_SIGNALS:
    void requestedInitialize();
    void requestedShutdown();
    void stopThread();
    void splashFinished(QWidget *window);

private:
    QThread *coreThread;
    OptionsModel *optionsModel;
    ClientModel *clientModel;
    BitcoinGUI *window;
    QTimer *pollShutdownTimer;
#ifdef ENABLE_WALLET
    PaymentServer* paymentServer;
    WalletModel *walletModel;
#endif
    int returnValue;
    const PlatformStyle *platformStyle;
    std::unique_ptr<QWidget> shutdownWindow;

    void startThread();
};

#include <qt/bitcoin.moc>

BitcoinCore::BitcoinCore():
    QObject()
{
}

void BitcoinCore::handleRunawayException(const std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    Q_EMIT runawayException(QString::fromStdString(GetWarnings("gui")));
}

bool BitcoinCore::baseInitialize()
{
    if (!AppInitBasicSetup())
    {
        return false;
    }
    if (!AppInitParameterInteraction())
    {
        return false;
    }
    if (!AppInitSanityChecks())
    {
        return false;
    }
    if (!AppInitLockDataDirectory())
    {
        return false;
    }
    return true;
}

void BitcoinCore::initialize()
{
    try
    {
        qDebug() << __func__ << ": Running initialization in thread";
        bool rv = AppInitMain();
        Q_EMIT initializeResult(rv);
    } catch (const std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(nullptr);
    }
}

void BitcoinCore::shutdown()
{
    try
    {
        qDebug() << __func__ << ": Running Shutdown in thread";
        Interrupt();
        Shutdown();
        qDebug() << __func__ << ": Shutdown finished";
        Q_EMIT shutdownResult();
    } catch (const std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(nullptr);
    }
}

BitcoinApplication::BitcoinApplication(int &argc, char **argv):
    QApplication(argc, argv),
    coreThread(0),
    optionsModel(0),
    clientModel(0),
    window(0),
    pollShutdownTimer(0),
#ifdef ENABLE_WALLET
    paymentServer(0),
    walletModel(0),
#endif
    returnValue(0)
{
    setQuitOnLastWindowClosed(false);

    // UI per-platform customization
    // This must be done inside the BitcoinApplication constructor, or after it, because
    // PlatformStyle::instantiate requires a QApplication
    std::string platformName;
    platformName = gArgs.GetArg("-uiplatform", BitcoinGUI::DEFAULT_UIPLATFORM);
    platformStyle = PlatformStyle::instantiate(QString::fromStdString(platformName));
    if (!platformStyle) // Fall back to "other" if specified name not found
        platformStyle = PlatformStyle::instantiate("other");
    assert(platformStyle);

    // Modern dark theme palette matching the mockup design
    QPalette appPalette = QApplication::palette();
    appPalette.setColor(QPalette::Window, QColor("#1e1e1e"));          // dark background
    appPalette.setColor(QPalette::Base, QColor("#2d3748"));            // input field background
    appPalette.setColor(QPalette::AlternateBase, QColor("#4a5568"));   // alternating rows
    appPalette.setColor(QPalette::Highlight, QColor("#48bb78"));       // green accent highlight
    appPalette.setColor(QPalette::HighlightedText, QColor("#ffffff")); // white highlighted text
    appPalette.setColor(QPalette::Button, QColor("#2d3748"));          // dark button background
    appPalette.setColor(QPalette::ButtonText, QColor("#ffffff"));      // white button text
    appPalette.setColor(QPalette::WindowText, QColor("#ffffff"));      // white window text
    appPalette.setColor(QPalette::Text, QColor("#ffffff"));            // white text
    QApplication::setPalette(appPalette);

    // Apply modern dark theme stylesheet - optimized for faster loading
    const QString appStyle =
        // Main window and background - simplified for performance
        "QMainWindow { background-color: #1e1e1e; color: #ffffff; }\n"
        "QWidget { background-color: #1e1e1e; color: #ffffff; }\n"
        "QFrame { background-color: #1e1e1e; color: #ffffff; }\n"
        // Menu bar - dark header style
        "QMenuBar { background: #2d3748; color: #ffffff; border-bottom: 1px solid #4a5568; padding: 4px; }\n"
        "QMenuBar::item { background: transparent; padding: 8px 12px; border-radius: 4px; }\n"
        "QMenuBar::item:selected { background: #4a5568; }\n"
        "QMenuBar::item:pressed { background: #2d3748; }\n"
        "QMenu { background: #2d3748; color: #ffffff; border: 1px solid #4a5568; border-radius: 8px; padding: 4px; }\n"
        "QMenu::item { padding: 8px 16px; border-radius: 4px; }\n"
        "QMenu::item:selected { background: #4a5568; }\n"
        "QMenu::separator { height: 1px; background: #4a5568; margin: 4px 8px; }\n"
        // Toolbars - modern sidebar style
        "QToolBar { background: #2d3748; border: none; spacing: 8px; padding: 8px; }\n"
        "QToolBar QToolButton { background: transparent; color: #cbd5e0; border: none; border-radius: 8px; padding: 12px; min-width: 48px; min-height: 48px; }\n"
        "QToolBar QToolButton:hover { background: #4a5568; color: #ffffff; }\n"
        "QToolBar QToolButton:checked { background: #48bb78; color: #ffffff; }\n"
        "QToolBar QToolButton:pressed { background: #38a169; }\n"
        // Status bar
        "QStatusBar { background: #2d3748; color: #cbd5e0; border-top: 1px solid #4a5568; }\n"
        // Cards and frames - modern card design with specific padding only for certain frames
        "QFrame { background: #2d3748; border: 1px solid #4a5568; border-radius: 12px; }\n"
        "QFrame[frameShape=\"4\"] { background: #2d3748; border: 1px solid #4a5568; border-radius: 12px; }\n"
        "QFrame.card { padding: 16px; }\n" /* Only apply padding to frames with 'card' class */
        // Buttons - modern green accent
        "QPushButton { background: #48bb78; color: #ffffff; border: none; border-radius: 8px; padding: 12px 24px; font-weight: bold; }\n"
        "QPushButton:hover { background: #38a169; }\n"
        "QPushButton:pressed { background: #2f855a; }\n"
        "QPushButton:disabled { background: #4a5568; color: #a0aec0; }\n"
        "QPushButton[flat=\"true\"] { background: transparent; border: none; }\n"
        "QPushButton[flat=\"true\"]:hover { background: #4a5568; border-radius: 4px; }\n"
        // Labels - enhanced typography
        "QLabel { color: #ffffff; }\n"
        "QLabel[styleClass=\"title\"] { font-size: 24px; font-weight: bold; color: #ffffff; }\n"
        "QLabel[styleClass=\"balance\"] { font-size: 32px; font-weight: bold; color: #48bb78; }\n"
        "QLabel[styleClass=\"section-header\"] { font-size: 18px; font-weight: bold; color: #e2e8f0; margin: 16px 0 8px 0; }\n"
        // Input fields - enhanced visibility and contrast
        "QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox { background: #718096; color: #ffffff; border: 2px solid #a0aec0; border-radius: 8px; padding: 10px 14px; font-size: 14px; }\n"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { background: #4a5568; border-color: #48bb78; }\n"
        "QLineEdit:hover, QTextEdit:hover, QPlainTextEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #cbd5e0; }\n"
        "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: #2d3748; color: #a0aec0; border-color: #4a5568; }\n"
        "QLineEdit::placeholder, QTextEdit::placeholder, QPlainTextEdit::placeholder { color: #cbd5e0; }\n"
        // Checkboxes and radio buttons - clear visibility
        "QCheckBox { color: #ffffff; spacing: 8px; }\n"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid #718096; border-radius: 4px; background: #4a5568; }\n"
        "QCheckBox::indicator:hover { border-color: #48bb78; background: #718096; }\n"
        "QCheckBox::indicator:checked { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #48bb78, stop:1 #38a169); border-color: #48bb78; }\n"
        /* Removed unsupported content/transform pseudo-element styling */
        "QCheckBox::indicator:checked:hover { background: #38a169; }\n"
        "QCheckBox::indicator:disabled { background: #2d3748; border-color: #4a5568; }\n"
        "QRadioButton { color: #ffffff; spacing: 8px; }\n"
        "QRadioButton::indicator { width: 18px; height: 18px; border: 2px solid #718096; border-radius: 9px; background: #4a5568; }\n"
        "QRadioButton::indicator:hover { border-color: #48bb78; background: #718096; }\n"
        "QRadioButton::indicator:checked { background: #48bb78; border-color: #48bb78; }\n"
        /* Removed unsupported content pseudo-element styling */
        // Tables and lists - modern design with proper contrast and visibility
        "QHeaderView::section { background: #2d3748; color: #e2e8f0; padding: 8px 12px; border: none; border-bottom: 1px solid #4a5568; font-weight: bold; min-height: 20px; }\n"
        "QTableView, QListView { background: #1e1e1e; color: #ffffff; border: none; alternate-background-color: #2d3748; gridline-color: #4a5568; outline: none; }\n"
        "QTableView::item, QListView::item { padding: 6px 8px; border-bottom: 1px solid #4a5568; color: #ffffff; min-height: 18px; }\n"
        "QTableView::item:selected, QListView::item:selected { background: #48bb78; color: #ffffff; }\n"
        "QTableView::item:hover, QListView::item:hover { background: #4a5568; color: #ffffff; }\n"
        "QTableView#transactionView { background: #1e1e1e; color: #ffffff; }\n"
        "QTableView#transactionView::item { color: #ffffff; }\n"
        "QTableView#recentRequestsView { background: #1e1e1e; color: #ffffff; }\n"
        "QTableView#recentRequestsView::item { color: #ffffff; min-height: 22px; }\n"
        // Combo boxes - enhanced to match input fields
        "QComboBox { background: #718096; color: #ffffff; border: 2px solid #a0aec0; border-radius: 8px; padding: 10px 14px; font-size: 14px; min-width: 120px; }\n"
        "QComboBox:hover { border-color: #cbd5e0; }\n"
        "QComboBox:focus { background: #4a5568; border-color: #48bb78; }\n"
        "QComboBox:disabled { background: #2d3748; color: #a0aec0; border-color: #4a5568; }\n"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border-left-width: 2px; border-left-color: #a0aec0; border-left-style: solid; border-top-right-radius: 8px; border-bottom-right-radius: 8px; background: #a0aec0; }\n"
        "QComboBox::down-arrow { width: 12px; height: 12px; background: #2d3748; }\n"
        "QComboBox QAbstractItemView { background: #2d3748; color: #ffffff; selection-background-color: #48bb78; border: 2px solid #4a5568; border-radius: 8px; padding: 4px; }\n"
        "QComboBox QAbstractItemView::item { padding: 8px 12px; border-radius: 4px; }\n"
        "QComboBox QAbstractItemView::item:selected { background: #48bb78; }\n"
        // Scrollbars - modern thin scrollbars
        "QScrollBar:vertical { background: #2d3748; width: 8px; border-radius: 4px; }\n"
        "QScrollBar::handle:vertical { background: #4a5568; border-radius: 4px; min-height: 20px; }\n"
        "QScrollBar::handle:vertical:hover { background: #718096; }\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }\n"
        "QScrollBar:horizontal { background: #2d3748; height: 8px; border-radius: 4px; }\n"
        "QScrollBar::handle:horizontal { background: #4a5568; border-radius: 4px; min-width: 20px; }\n"
        "QScrollBar::handle:horizontal:hover { background: #718096; }\n"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { border: none; background: none; }\n";
    this->setStyleSheet(appStyle);
}

BitcoinApplication::~BitcoinApplication()
{
    if(coreThread)
    {
        qDebug() << __func__ << ": Stopping thread";
        Q_EMIT stopThread();
        coreThread->wait();
        qDebug() << __func__ << ": Stopped thread";
    }

    delete window;
    window = 0;
#ifdef ENABLE_WALLET
    delete paymentServer;
    paymentServer = 0;
#endif
    delete optionsModel;
    optionsModel = 0;
    delete platformStyle;
    platformStyle = 0;
}

#ifdef ENABLE_WALLET
void BitcoinApplication::createPaymentServer()
{
    paymentServer = new PaymentServer(this);
}
#endif

void BitcoinApplication::createOptionsModel(bool resetSettings)
{
    optionsModel = new OptionsModel(nullptr, resetSettings);
}

void BitcoinApplication::createWindow(const NetworkStyle *networkStyle)
{
    window = new BitcoinGUI(platformStyle, networkStyle, 0);
    
    // Ensure the window inherits the dark theme properly
    window->setAttribute(Qt::WA_OpaquePaintEvent, false);
    window->setAttribute(Qt::WA_NoSystemBackground, false);
    
    // Force immediate style application to prevent black screen
    window->style()->unpolish(window);
    window->style()->polish(window);
    window->update();

    pollShutdownTimer = new QTimer(window);
    connect(pollShutdownTimer, SIGNAL(timeout()), window, SLOT(detectShutdown()));
}

void BitcoinApplication::createSplashScreen(const NetworkStyle *networkStyle)
{
    SplashScreen *splash = new SplashScreen(Qt::WindowFlags(), networkStyle);
    // We don't hold a direct pointer to the splash screen after creation, but the splash
    // screen will take care of deleting itself when slotFinish happens.
    splash->show();
    connect(this, SIGNAL(splashFinished(QWidget*)), splash, SLOT(slotFinish(QWidget*)));
    connect(this, SIGNAL(requestedShutdown()), splash, SLOT(close()));

    // Cascoin: Start mice/BCT DB init while splash is visible so user sees progress
    // Memory leak fix: Use shared_ptr to ensure proper cleanup and avoid detached thread memory leaks
    auto dbInitTask = std::make_shared<std::thread>([](){
        try {
            uiInterface.ShowProgress("Mice DB initialisieren", 1, false);
            BCTDatabase db;
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Reduced delay to prevent memory buildup
            uiInterface.ShowProgress("Mice DB initialisieren", 20, false);
            (void)db.initialize();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            uiInterface.ShowProgress("Mice DB initialisieren", 50, false);
            (void)db.getTotalBCTs();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            uiInterface.ShowProgress("Mice DB initialisieren", 80, false);
            (void)db.getTotalAvailableMice();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            uiInterface.ShowProgress("Mice DB initialisieren", 100, false);
            g_miceDbReady.store(true);
        } catch (...) {
            uiInterface.ShowProgress("Mice DB initialisieren", 100, false);
            g_miceDbReady.store(true);
        }
    });
    
    // Cascoin: Memory leak fix - Clean up old finished threads before adding new ones
    static std::vector<std::shared_ptr<std::thread>> splashThreads;
    
    // Remove finished threads to prevent memory leak
    auto it = splashThreads.begin();
    while (it != splashThreads.end()) {
        if ((*it)->joinable()) {
            ++it;
        } else {
            it = splashThreads.erase(it);
        }
    }
    
    // Limit maximum concurrent threads to prevent memory overflow
    if (splashThreads.size() < 3) {
        splashThreads.push_back(dbInitTask);
        dbInitTask->detach();
    } else {
        LogPrintf("Warning: Too many splash threads running, skipping DB init task\n");
    }
}

void BitcoinApplication::startThread()
{
    if(coreThread)
        return;
    coreThread = new QThread(this);
    BitcoinCore *executor = new BitcoinCore();
    executor->moveToThread(coreThread);

    /*  communication to and from thread */
    connect(executor, SIGNAL(initializeResult(bool)), this, SLOT(initializeResult(bool)));
    connect(executor, SIGNAL(shutdownResult()), this, SLOT(shutdownResult()));
    connect(executor, SIGNAL(runawayException(QString)), this, SLOT(handleRunawayException(QString)));
    connect(this, SIGNAL(requestedInitialize()), executor, SLOT(initialize()));
    connect(this, SIGNAL(requestedShutdown()), executor, SLOT(shutdown()));
    /*  make sure executor object is deleted in its own thread */
    connect(this, SIGNAL(stopThread()), executor, SLOT(deleteLater()));
    connect(this, SIGNAL(stopThread()), coreThread, SLOT(quit()));

    coreThread->start();
}

void BitcoinApplication::parameterSetup()
{
    InitLogging();
    InitParameterInteraction();
}

void BitcoinApplication::requestInitialize()
{
    qDebug() << __func__ << ": Requesting initialize";
    startThread();
    Q_EMIT requestedInitialize();
}

void BitcoinApplication::requestShutdown()
{
    // Show a simple window indicating shutdown status
    // Do this first as some of the steps may take some time below,
    // for example the RPC console may still be executing a command.
    shutdownWindow.reset(ShutdownWindow::showShutdownWindow(window));

    qDebug() << __func__ << ": Requesting shutdown";
    startThread();
    window->hide();
    window->setClientModel(0);
    // Keep polling for ShutdownRequested() so we can quit promptly
    if (pollShutdownTimer) {
        pollShutdownTimer->start(200);
    }

#ifdef ENABLE_WALLET
    window->removeAllWallets();
    delete walletModel;
    walletModel = 0;
#endif
    delete clientModel;
    clientModel = 0;

    StartShutdown();

    // Request shutdown from core thread
    Q_EMIT requestedShutdown();

    // Failsafe: force quit if shutdown doesn't signal completion in time
    QTimer::singleShot(10000, this, SLOT(forceQuitIfShutdownStuck()));
}

void BitcoinApplication::initializeResult(bool success)
{
    qDebug() << __func__ << ": Initialization result: " << success;
    // Set exit result.
    returnValue = success ? EXIT_SUCCESS : EXIT_FAILURE;
    if(success)
    {
        // Log this only after AppInitMain finishes, as then logging setup is guaranteed complete
        qWarning() << "Platform customization:" << platformStyle->getName();
#ifdef ENABLE_WALLET
        PaymentServer::LoadRootCAs();
        paymentServer->setOptionsModel(optionsModel);
#endif

        clientModel = new ClientModel(optionsModel);
        window->setClientModel(clientModel);

#ifdef ENABLE_WALLET
        // TODO: Expose secondary wallets
        if (!vpwallets.empty())
        {
            walletModel = new WalletModel(platformStyle, vpwallets[0], optionsModel);

            window->addWallet(BitcoinGUI::DEFAULT_WALLET, walletModel);
            window->setCurrentWallet(BitcoinGUI::DEFAULT_WALLET);

            connect(walletModel, SIGNAL(coinsSent(CWallet*,SendCoinsRecipient,QByteArray)),
                             paymentServer, SLOT(fetchPaymentACK(CWallet*,const SendCoinsRecipient&,QByteArray)));
        }
#endif

        // Keep splash until mice DB init finished, THEN show window
        if (!g_miceDbReady.load()) {
            QElapsedTimer timer; timer.start();
            while (!g_miceDbReady.load() && timer.elapsed() < 10000) {
                QThread::msleep(50);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
        }
        
        // Ensure all widgets are properly initialized before showing
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        
        // Show window
        if(gArgs.GetBoolArg("-min", false))
        {
            window->showMinimized();
        }
        else
        {
            window->show();
            window->raise();
            window->activateWindow();
        }
        
        // Immediately close splash - let window render naturally
        Q_EMIT splashFinished(window);

#ifdef ENABLE_WALLET
        // Now that initialization/startup is done, process any command-line
        // bitcoin: URIs or payment requests:
        connect(paymentServer, SIGNAL(receivedPaymentRequest(SendCoinsRecipient)),
                         window, SLOT(handlePaymentRequest(SendCoinsRecipient)));
        connect(window, SIGNAL(receivedURI(QString)),
                         paymentServer, SLOT(handleURIOrFile(QString)));
        connect(paymentServer, SIGNAL(message(QString,QString,unsigned int)),
                         window, SLOT(message(QString,QString,unsigned int)));
        QTimer::singleShot(100, paymentServer, SLOT(uiReady()));
#endif
        pollShutdownTimer->start(200);
    } else {
        Q_EMIT splashFinished(window); // Make sure splash screen doesn't stick around during shutdown
        quit(); // Exit first main loop invocation
    }
}

void BitcoinApplication::shutdownResult()
{
    // Ensure the core thread is asked to stop before quitting the event loop
    Q_EMIT stopThread();
    quit(); // Exit second main loop invocation after shutdown finished
}

void BitcoinApplication::forceQuitIfShutdownStuck()
{
    // If shutdown appears stuck, ensure the core thread is asked to stop and try again
    Q_EMIT stopThread();
    Q_EMIT requestedShutdown();
    quit();
    // As a last resort, force process exit after a short grace period
    QTimer::singleShot(3000, this, SLOT(hardExit()));
}

void BitcoinApplication::hardExit()
{
    ::exit(EXIT_SUCCESS);
}

void BitcoinApplication::handleRunawayException(const QString &message)
{
    QMessageBox::critical(0, "Runaway exception", BitcoinGUI::tr("A fatal error occurred. Cascoin can no longer continue safely and will quit.") + QString("\n\n") + message);
    ::exit(EXIT_FAILURE);
}

WId BitcoinApplication::getMainWinId() const
{
    if (!window)
        return 0;

    return window->winId();
}

#ifndef BITCOIN_QT_TEST
int main(int argc, char *argv[])
{
    SetupEnvironment();

    /// 1. Parse command-line options. These take precedence over anything else.
    // Command-line options take precedence:
    gArgs.ParseParameters(argc, argv);

    // Do not refer to data directory yet, this can be overridden by Intro::pickDataDirectory

    /// 2. Basic Qt initialization (not dependent on parameters or configuration)
#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    Q_INIT_RESOURCE(bitcoin);
    Q_INIT_RESOURCE(bitcoin_locale);

    // Remove platform environment variables to allow automatic detection
    qunsetenv("QT_QPA_PLATFORM");
    
    // Fix D-Bus signature errors - comprehensive D-Bus disabling
    qputenv("QT_LOGGING_RULES", "dbus.debug=false;qt.qpa.dbus.debug=false");
    qputenv("QT_QPA_PLATFORMTHEME", "");
    qputenv("DBUS_SESSION_BUS_ADDRESS", "disabled");
    qputenv("QT_DBUS_NO_ACTIVATION", "1");
    
#ifdef Q_OS_LINUX
    // Force Qt to use XCB instead of D-Bus on Linux
    qputenv("QT_QPA_PLATFORM", "xcb");
#elif defined(Q_OS_WIN)
    // On Windows, explicitly set the platform to ensure correct plugin is used
    qputenv("QT_QPA_PLATFORM", "windows");
#endif
    // On macOS, let Qt auto-detect the platform
    
    BitcoinApplication app(argc, argv);
#if QT_VERSION > 0x050100 && QT_VERSION < 0x060000
    // Generate high-dpi pixmaps (deprecated in Qt6, always enabled)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#if QT_VERSION >= 0x050600 && QT_VERSION < 0x060000
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#ifdef Q_OS_MAC
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
#if QT_VERSION >= 0x050500 && QT_VERSION < 0x060000
    // Because of the POODLE attack it is recommended to disable SSLv3 (https://disablessl3.com/),
    // so set SSL protocols to TLS1.0+. In Qt6, configure per-socket instead.
    QSslConfiguration sslconf = QSslConfiguration::defaultConfiguration();
    sslconf.setProtocol(QSsl::TlsV1_2OrLater);
    QSslConfiguration::setDefaultConfiguration(sslconf);
#endif

    // Register meta types used for QMetaObject::invokeMethod
    qRegisterMetaType< bool* >();
    //   Need to pass name here as CAmount is a typedef (see http://qt-project.org/doc/qt-5/qmetatype.html#qRegisterMetaType)
    //   IMPORTANT if it is no longer a typedef use the normal variant above
    qRegisterMetaType< CAmount >("CAmount");
    qRegisterMetaType< std::function<void(void)> >("std::function<void(void)>");

    /// 3. Application identification
    // must be set before OptionsModel is initialized or translations are loaded,
    // as it is used to locate QSettings
    QApplication::setOrganizationName(QAPP_ORG_NAME);
    QApplication::setOrganizationDomain(QAPP_ORG_DOMAIN);
    QApplication::setApplicationName(QAPP_APP_NAME_DEFAULT);
    GUIUtil::SubstituteFonts(GetLangTerritory());

    /// 4. Initialization of translations, so that intro dialog is in user's language
    // Now that QSettings are accessible, initialize translations
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);
    translationInterface.Translate.connect(Translate);

    // Show help message immediately after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen.
    if (gArgs.IsArgSet("-?") || gArgs.IsArgSet("-h") || gArgs.IsArgSet("-help") || gArgs.IsArgSet("-version"))
    {
        HelpMessageDialog help(nullptr, gArgs.IsArgSet("-version"));
        help.showOrPrint();
        return EXIT_SUCCESS;
    }

    /// 5. Now that settings and translations are available, ask user for data directory
    // User language is set up: pick a data directory
    if (!Intro::pickDataDirectory())
        return EXIT_SUCCESS;

    /// 6. Determine availability of data directory and parse bitcoin.conf
    /// - Do not call GetDataDir(true) before this step finishes
    if (!fs::is_directory(GetDataDir(false)))
    {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(gArgs.GetArg("-datadir", ""))));
        return EXIT_FAILURE;
    }
    try {
        gArgs.ReadConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME));
    } catch (const std::exception& e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Cannot parse configuration file: %1. Only use key=value syntax.").arg(e.what()));
        return EXIT_FAILURE;
    }

    /// 7. Determine network (and switch to network specific options)
    // - Do not call Params() before this step
    // - Do this after parsing the configuration file, as the network can be switched there
    // - QSettings() will use the new application name after this, resulting in network-specific settings
    // - Needs to be done before createOptionsModel

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    try {
        SelectParams(ChainNameFromCommandLine());
    } catch(std::exception &e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg(e.what()));
        return EXIT_FAILURE;
    }
#ifdef ENABLE_WALLET
    // Parse URIs on command line -- this can affect Params()
    PaymentServer::ipcParseCommandLine(argc, argv);
#endif

    QScopedPointer<const NetworkStyle> networkStyle(NetworkStyle::instantiate(QString::fromStdString(Params().NetworkIDString())));
    assert(!networkStyle.isNull());
    // Allow for separate UI settings for testnets
    QApplication::setApplicationName(networkStyle->getAppName());
    // Re-initialize translations after changing application name (language in network-specific settings can be different)
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);

#ifdef ENABLE_WALLET
    /// 8. URI IPC sending
    // - Do this early as we don't want to bother initializing if we are just calling IPC
    // - Do this *after* setting up the data directory, as the data directory hash is used in the name
    // of the server.
    // - Do this after creating app and setting up translations, so errors are
    // translated properly.
    if (PaymentServer::ipcSendCommandLine())
        exit(EXIT_SUCCESS);

    // Start up the payment server early, too, so impatient users that click on
    // bitcoin: links repeatedly have their payment requests routed to this process:
    app.createPaymentServer();
#endif

    /// 9. Main GUI initialization
    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));
#if QT_VERSION < 0x050000
    // Install qDebug() message handler to route to debug.log
    qInstallMsgHandler(DebugMessageHandler);
#else
#if defined(Q_OS_WIN)
    // Install global event filter for processing Windows session related Windows messages (WM_QUERYENDSESSION and WM_ENDSESSION)
    qApp->installNativeEventFilter(new WinShutdownMonitor());
#endif
    // Install qDebug() message handler to route to debug.log
    qInstallMessageHandler(DebugMessageHandler);
#endif
    // Allow parameter interaction before we create the options model
    app.parameterSetup();
    // Load GUI settings from QSettings
    app.createOptionsModel(gArgs.IsArgSet("-resetguisettings"));

    // Subscribe to global signals from core
    uiInterface.InitMessage.connect(InitMessage);

    if (gArgs.GetBoolArg("-splash", DEFAULT_SPLASHSCREEN) && !gArgs.GetBoolArg("-min", false))
        app.createSplashScreen(networkStyle.data());

    int rv = EXIT_SUCCESS;
    try
    {
        app.createWindow(networkStyle.data());
        // Perform base initialization before spinning up initialization/shutdown thread
        // This is acceptable because this function only contains steps that are quick to execute,
        // so the GUI thread won't be held up.
        if (BitcoinCore::baseInitialize()) {
            app.requestInitialize();
#if defined(Q_OS_WIN) && QT_VERSION >= 0x050000
            WinShutdownMonitor::registerShutdownBlockReason(QObject::tr("%1 didn't yet exit safely...").arg(QObject::tr(PACKAGE_NAME)), (HWND)app.getMainWinId());
#endif
            app.exec();
            app.requestShutdown();
            app.exec();
            rv = app.getReturnValue();
        } else {
            // A dialog with detailed error will have been shown by InitError()
            rv = EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "Runaway exception");
        app.handleRunawayException(QString::fromStdString(GetWarnings("gui")));
    } catch (...) {
        PrintExceptionContinue(nullptr, "Runaway exception");
        app.handleRunawayException(QString::fromStdString(GetWarnings("gui")));
    }
    return rv;
}
#endif // BITCOIN_QT_TEST
