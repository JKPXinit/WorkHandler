#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "uiManager.h"
#include "thememanager.h"
#include "languagemanager.h"
#include "softwareconfig.h"
#include "passwordverfy.h"
#include "exitmodedialog.h"
#include "myLogger.h"
#include "shortcutmanager.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_DockManager = new ads::CDockManager(this);

    m_softwareconfig = new SoftwareConfig(this);
    m_thememanager = new ThemeManager();
    m_Languagemanager = new LanguageManager(this);
    m_passwordverfy = new PasswordVerfy(this);
    m_shortcutManager = new ShortcutManager(this);
    m_aboutDialog = nullptr;

    initConfigUI();     // 初始化软件配置文件（含日志系统），必须在 uiManager 之前

    m_UI = new uiManager(this);

    m_UI->setupUI();    // 显示 UI 界面

    // PasswordVerfy 信号解耦连线
    connect(m_passwordverfy, &PasswordVerfy::adminModeGranted,
            this, &MainWindow::onAdminModeGranted);
    connect(m_passwordverfy, &PasswordVerfy::adminModeRejected,
            this, &MainWindow::onAdminModeRejected);

    // uiManager ↔ PasswordVerfy 解耦连线
    connect(m_UI, &uiManager::requestAdminAuth,
            this, &MainWindow::onRequestAdminAuth);
    connect(m_UI, &uiManager::adminModeExited,
            this, &MainWindow::onAdminModeExited);
    connect(this, &MainWindow::adminModeChanged,
            m_UI, &uiManager::onAdminAuthResult);

    // ShortcutManager 信号解耦连线
    connect(m_shortcutManager, &ShortcutManager::shortcutConfigSaved,
            m_softwareconfig, &SoftwareConfig::Write_config);

}

void MainWindow::initConfigUI() {

    m_softwareconfig->setAdminMode(false);  // 软件重启后自动变为非管理员模式

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



void MainWindow::onAdminModeGranted()
{
    m_softwareconfig->setAdminMode(true);
}

void MainWindow::onAdminModeRejected()
{
    m_UI->adminModeCheckBox->setChecked(false);
}

void MainWindow::onRequestAdminAuth()
{
    // 同步调用密码验证（pwinputDialog 内部会 emit adminModeGranted/Rejected）
    // 验证结果通过 adminModeChanged 信号通知 uiManager 决定是否打开 Admin 对话框
    bool granted = m_passwordverfy->pwinputDialog();
    emit adminModeChanged(granted);
}

void MainWindow::onAdminModeExited()
{
    m_softwareconfig->setAdminMode(false);
}

MainWindow::~MainWindow(){
    LOG_INFO("Application shutting down...");
    m_softwareconfig->setAdminMode(false);
    m_softwareconfig->Write_config();   // 主窗口析构时，管理员模式恢复初始

    // 关闭日志系统
    myLogger::instance()->shutdown();

    delete ui;
}


