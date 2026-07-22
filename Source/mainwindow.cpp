#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "uiManager.h"
#include "thememanager.h"
#include "languagemanager.h"
#include "softwareconfig.h"
#include "exitmodedialog.h"
#include "myLogger.h"
#include "shortcutmanager.h"
#include "httpservermanagerdialog.h"
#include "httpserver.h"
#include "public.h"

#include <QHostAddress>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_DockManager = new ads::CDockManager(this);

    m_softwareconfig = new SoftwareConfig(this);
    m_thememanager = new ThemeManager();
    m_Languagemanager = new LanguageManager(this);
    m_shortcutManager = new ShortcutManager(this);
    m_aboutDialog = nullptr;

    initConfigUI();     // 初始化软件配置文件（含日志系统），必须在 uiManager 之前

    m_httpServer = new HttpServer(
        Practical_Function::ResourcePath(QStringLiteral("/data/issue_panel.db")), this);

    m_httpServer->setConfigurationProvider([this](QString *errorMessage) {
        ServerConfig config;
        config.serverInterface = m_softwareconfig->httpServerBindAllInterfaces()
            ? QStringLiteral("0.0.0.0")
            : m_softwareconfig->httpServerSelectedAddress();
        if (config.serverInterface.isEmpty()) {
            config.serverInterface = QStringLiteral("127.0.0.1");
        }
        config.serverPort = m_softwareconfig->httpServerPort();
        config.autoStart = m_softwareconfig->httpServerAutoStart();
        config.keepOriginal = m_softwareconfig->httpServerKeepOriginal();
        config.maxImageWidth = m_softwareconfig->httpServerMaxImageWidth();
        if (errorMessage) {
            errorMessage->clear();
        }
        return config;
    });

    m_UI = new uiManager(this);

    m_UI->setupUI();    // 显示 UI 界面

    // ShortcutManager 信号解耦连线
    connect(m_shortcutManager, &ShortcutManager::shortcutConfigSaved,
            m_softwareconfig, &SoftwareConfig::Write_config);

    connect(m_httpServerManagerDialog,
            &HttpServerManagerDialog::startServerRequested,
            m_httpServer,
            &HttpServer::startServer);
    connect(m_httpServerManagerDialog,
            &HttpServerManagerDialog::stopServerRequested,
            m_httpServer,
            &HttpServer::stopServer);
    connect(m_httpServerManagerDialog,
            &HttpServerManagerDialog::restartServerRequested,
            m_httpServer,
            &HttpServer::restartServer);
    connect(m_httpServerManagerDialog,
            &HttpServerManagerDialog::reachabilityTestRequested,
            m_httpServer,
            &HttpServer::testReachability);

    connect(m_httpServer,
            &HttpServer::stateChanged,
            this,
            [this](HttpServer::State state, const QString &detail) {
                HttpServerManagerDialog::ServerState dialogState =
                    HttpServerManagerDialog::ServerState::Stopped;
                switch (state) {
                case HttpServer::State::Stopped:
                    dialogState = HttpServerManagerDialog::ServerState::Stopped;
                    break;
                case HttpServer::State::Starting:
                    dialogState = HttpServerManagerDialog::ServerState::Starting;
                    break;
                case HttpServer::State::Running:
                    dialogState = HttpServerManagerDialog::ServerState::Running;
                    break;
                case HttpServer::State::Stopping:
                    dialogState = HttpServerManagerDialog::ServerState::Stopping;
                    break;
                case HttpServer::State::Error:
                    dialogState = HttpServerManagerDialog::ServerState::Error;
                    break;
                }
                m_httpServerManagerDialog->setServerState(dialogState, detail);
                LOG_INFO(QStringLiteral("HTTP server state changed: %1")
                             .arg(detail.isEmpty() ? QString::number(int(state)) : detail));
            });
    connect(m_httpServer,
            &HttpServer::reachabilityTested,
            this,
            [this](bool reachable, const QString &detail) {
                m_httpServerManagerDialog->setReachabilityState(
                    reachable
                        ? HttpServerManagerDialog::ReachabilityState::Reachable
                        : HttpServerManagerDialog::ReachabilityState::Unreachable,
                    detail);
            });
    connect(m_httpServer,
            &HttpServer::bootstrapAdminCreated,
            this,
            [this](const QString &username, const QString &password) {
                LOG_WARNING(QStringLiteral(
                    "Initial HTTP administrator credentials: %1 / %2")
                                .arg(username, password));
                m_httpServerManagerDialog->showBootstrapCredentials(username, password);
            });
    connect(m_httpServer,
            &HttpServer::configurationChanged,
            this,
            [this](const QString &serverInterface,
                   quint16 serverPort,
                   bool autoStart,
                   bool keepOriginal,
                   int maxImageWidth) {
                const QString anyAddress =
                    QHostAddress(QHostAddress::AnyIPv4).toString();
                const bool bindAll = serverInterface == anyAddress;
                m_softwareconfig->setHttpServerBindAllInterfaces(bindAll);
                if (!bindAll) {
                    m_softwareconfig->setHttpServerSelectedAddress(serverInterface);
                }
                m_softwareconfig->setHttpServerPort(serverPort);
                m_softwareconfig->setHttpServerAutoStart(autoStart);
                m_softwareconfig->setHttpServerKeepOriginal(keepOriginal);
                m_softwareconfig->setHttpServerMaxImageWidth(maxImageWidth);
                m_softwareconfig->Write_config();
                m_httpServerManagerDialog->refreshConfiguration();
            });

    QString httpServerError;
    if (m_httpServer->initialize(&httpServerError)) {
        ServerConfig config = m_httpServer->configuration(&httpServerError);
        m_httpServer->updateConfiguration(config, &httpServerError);

        if (httpServerError.isEmpty() && config.autoStart) {
            QTimer::singleShot(0, m_httpServer, [this, config]() {
                m_httpServer->startServer(config.serverInterface, config.serverPort);
            });
        }
    }
    if (!httpServerError.isEmpty()) {
        LOG_ERROR(QStringLiteral("HTTP server initialization failed: %1")
                      .arg(httpServerError));
        m_httpServerManagerDialog->setServerState(
            HttpServerManagerDialog::ServerState::Error, httpServerError);
    }

}

void MainWindow::initConfigUI() {
    // 初始化日志系统
    LogConfig logConfig;
    int logMode = m_softwareconfig->logOutputMode();

    // 根据配置设置日志输出模式
    if (logMode == LogOutputConsole) {
        // 控制台模式
        logConfig.enableFileLogging = false;
        logConfig.enableConsoleLogging = true;
        logConfig.logFilePath = "";
    } else {
        // 文件模式（默认）
        logConfig.enableFileLogging = true;
        logConfig.enableConsoleLogging = false;
        logConfig.logFilePath = m_softwareconfig->logFilePath();
    }

    logConfig.minLevel = LogLevel_Debug;    // 最小日志级别
    logConfig.enableRotation        = m_softwareconfig->logRotationEnabled();
    logConfig.rotationMode          = m_softwareconfig->logRotationMode();
    logConfig.maxFileSizeMB         = m_softwareconfig->logRotationMaxSizeMB();
    logConfig.rotationIntervalDays  = m_softwareconfig->logRotationIntervalDays();
    logConfig.maxBackupCount        = m_softwareconfig->logRotationMaxBackups();
    myLogger::instance()->initialize(logConfig);

    LOG_INFO("Application starting...");
    LOG_INFO(QString("Qt Version: %1").arg(QT_VERSION_STR));
    LOG_INFO(QString("Log output mode: %1").arg(logMode == LogOutputConsole ? "Console" : "File"));

    // 软件的主题
    if(m_softwareconfig->theme() != 0 && m_softwareconfig->theme() < ENUM_MAX(themeType)) {
        m_thememanager->loadTheme(m_softwareconfig->theme());
        LOG_INFO(QString("Theme loaded: %1").arg(m_softwareconfig->theme()));
    }

    // 软件的语言
    if(m_softwareconfig->language() != 0 && m_softwareconfig->language() < ENUM_MAX(languageType)) {
        m_Languagemanager->loadLanguage(m_softwareconfig->language());
        LOG_INFO(QString("Language loaded: %1").arg(m_softwareconfig->language()));
    }

    return ;
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::LanguageChange && m_UI)
        m_UI->retranslateUi();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 首次运行 -> 让用户选退出方式
    if (!m_softwareconfig->isFirstRun())
    {
        ExitModeDialog dlg(this);
        dlg.showWithAnimation(this);
        softwareExitMode mode = dlg.selectedMode();

        // 写入配置
        m_softwareconfig->setFirstRun(true);
        m_softwareconfig->setExitMode(mode);
        m_softwareconfig->Write_config();   // 立即落盘
    }

    // 按配置决定是退出还是进托盘
    if (m_softwareconfig->exitMode() == systray &&
        !m_UI->onCloseEvent(event))
    {
        event->ignore();   // 隐藏到托盘
        return;
    }

    event->accept();       // 真正退出
}



MainWindow::~MainWindow(){
    LOG_INFO("Application shutting down...");
    if (m_httpServer) {
        m_httpServer->stopServer();
    }
    m_softwareconfig->Write_config();

    // 关闭日志系统
    myLogger::instance()->shutdown();

    delete ui;
}


