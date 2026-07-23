#include <QSplashScreen>
#include <QSystemTrayIcon>
#include <QDebug>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMessageBox>
#include <QCoreApplication>
#include <QWidget>
#include <QString>
#include <QSettings>
#include <QWindow>

#include "uiManager.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "myLogger.h"
#include "logviewerdialog.h"
#include "httpservermanagerdialog.h"
#include "aboutdialog.h"
#include "softwareconfig.h"
#include "thememanager.h"
#include "languagemanager.h"
#include "uiAdminoptions.h"
#include "shortcutmanager.h"
#include "logviewerdialog.h"

uiManager::uiManager(MainWindow *mainWindow)
{
    LOG_DEBUG("uiManager constructor called");

    initIcon();     // 初始化图标

    parent = mainWindow;  // 设置 parent 成员变量

    m_mainWindow = mainWindow;

    m_themeManager = new ThemeManager(this);
    m_options = new ui_AdminOptions(mainWindow);

    statusbar = mainWindow->statusBar();    // 初始化状态栏

    LOG_DEBUG("uiManager initialized");
}

void uiManager::initIcon() {

    appIcon           = QIcon(AppIcons::App);
    OptionsIcon       = QIcon(AppIcons::Options);
    AdministratorIcon = QIcon(AppIcons::Administrator);
    PasswordIcon      = QIcon(AppIcons::Password);
    AccountIcon       = QIcon(AppIcons::Account);
    BasicIcon         = QIcon(AppIcons::Basic);
    ThemeIcon         = QIcon(AppIcons::Theme);
    LanguageIcon      = QIcon(AppIcons::Language);
    DeleteIcon        = QIcon(AppIcons::Delete);
    SystemIcon        = QIcon(AppIcons::System);
    ShortcutIcon      = QIcon(AppIcons::Shortcut);

    return ;
}

void uiManager::setupUI() {

    LOG_INFO("Setting up UI...");

#if !UI_DEBUUG
    parent->setWindowTitle(QCoreApplication::applicationName());
    parent->setWindowIcon(appIcon);

    LOG_DEBUG("Initializing menu bar");
    // 初始化菜单栏
    initMenuBar ();

    LOG_DEBUG("Initializing dock windows");
    // 初始化 Dock
    initDockWindows();

    LOG_DEBUG("Restoring layout config");
    // 初始化软件布局
    m_mainWindow->m_softwareconfig->restoreLayoutConfig();

    LOG_DEBUG("Setting up status bar");
    // 初始化状态栏
    parent->setStatusBar(statusbar);
    statusbar->showMessage(tr("Program is running ..."), 3000);

    LOG_DEBUG("Initializing splash screen");
    // 新增：一次性初始化
    initSplash();   // 构造即显示

    LOG_DEBUG("Initializing system tray");
    initTray();     // 构造托盘，注册关闭事件转发

    LOG_INFO("Closing splash screen");
    // 关闭动画时机 ,初始化完成后
    closeSplash();

    LOG_INFO("UI setup completed");

#else
    QDialog *optionsDialog = m_options->setupOptionsDialog();

    LOG_DEBUG("setupOptionsDialog stepover");

    // 显示对话框
    optionsDialog->exec();
#endif

    return;
}

// 创建窗口
void uiManager::initDockWindows() {

    m_httpServerManagerDock = new ads::CDockWidget(tr("HTTP Server"), parent);
    m_httpServerManagerDock->setObjectName("HttpServerManagerDock");
    m_mainWindow->m_httpServerManagerDialog = new HttpServerManagerDialog(m_mainWindow);
    httpServerManagerView =
        m_mainWindow->m_httpServerManagerDialog->setupHttpServerManagerDialog();
    m_httpServerManagerDock->setWidget(httpServerManagerView);
    m_mainWindow->m_DockManager->addDockWidget(ads::LeftDockWidgetArea,
                                                m_httpServerManagerDock);
    displayMenu->addAction(m_httpServerManagerDock->toggleViewAction());

    m_logViewDock = new ads::CDockWidget(tr("Log viewer") ,parent);
    m_logViewDock->setObjectName("LogViewDock");
    m_mainWindow->m_logViewerDialog = new logViewerDialog(m_mainWindow);
    logVieweDialog = m_mainWindow->m_logViewerDialog->setupLogViewerDialog();
    m_logViewDock->setWidget(logVieweDialog);
    m_mainWindow->m_DockManager->addDockWidget(ads::BottomDockWidgetArea, m_logViewDock);
    displayMenu->addAction(m_logViewDock->toggleViewAction());

    return ;
}

void uiManager::initMenuBar () {
    LOG_DEBUG("Initializing menu bar");

    // 初始化菜单栏
    QMenuBar *menubar = parent->menuBar();
    if (!menubar->actions().isEmpty()) {
        LOG_WARNING("Menu bar already initialized, skipping");
        return; // 已经初始化过了
    }

    // File 菜单
    fileMenu = menubar->addMenu(tr("File"));
    m_newAction  = fileMenu->addAction(tr("Open new window"));
    m_exitAction = fileMenu->addAction(tr("Exit"));

    // 使用 ShortcutManager 注册快捷键
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_NewWindow, m_newAction);
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_Exit, m_exitAction);

    connect(m_newAction,  &QAction::triggered, this, &uiManager::onMenuBar_New);
    connect(m_exitAction, &QAction::triggered, this, &uiManager::onMenuBar_Exit);

    LOG_DEBUG("File menu created");

    // Display 菜单
    displayMenu = menubar->addMenu(tr("Display"));

    // Window 菜单
    windowMenu = menubar->addMenu(tr("Window"));
    m_fullScreenAction = windowMenu->addAction(tr("Full screen"));   // 全屏显示
    m_fullScreenAction->setCheckable(true);

    // 使用 ShortcutManager 注册快捷键
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_FullScreen, m_fullScreenAction);

    connect(m_fullScreenAction, &QAction::triggered, this, [this](bool checked){
        checked ? m_mainWindow->showFullScreen() : m_mainWindow->showMaximized();
    });
    m_lockManageAction = windowMenu->addAction(tr("Lock the layout"));

    // 使用 ShortcutManager 注册快捷键
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_LockLayout, m_lockManageAction);

    m_lockManageAction->setCheckable(true);
    m_mainWindow->m_DockManager->lockDockWidgetFeaturesGlobally();    //默认锁定布局
    m_lockManageAction->setChecked(true);
    connect(m_lockManageAction ,&QAction::triggered ,this ,[this](bool checked){
        if (checked)
            m_mainWindow->m_DockManager->lockDockWidgetFeaturesGlobally();
        else
            m_mainWindow->m_DockManager->lockDockWidgetFeaturesGlobally(ads::CDockWidget::NoDockWidgetFeatures);
    });

    m_saveLayoutAction = windowMenu->addAction(tr("Save the layout"));

    // 使用 ShortcutManager 注册快捷键
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_SaveLayout, m_saveLayoutAction);

    connect(m_saveLayoutAction, &QAction::triggered, this, [this]() {
        m_mainWindow->m_softwareconfig->saveLayoutConfig();
        statusbar->showMessage(tr("Layout saved"), 3000);
    });

    LOG_DEBUG("Window menu created");

    // Options 菜单
    optionsMenu = menubar->addMenu(tr("Options"));
    m_optionsAction = optionsMenu->addAction(tr("Settings"));

    // 使用 ShortcutManager 注册快捷键
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_Settings, m_optionsAction);

    connect(m_optionsAction, &QAction::triggered, this, &uiManager::onMenuBar_Options);

    // Help 菜单
    helpMenu = menubar->addMenu(tr("Help"));
    m_aboutAction = helpMenu->addAction(tr("About"));

    // 使用 ShortcutManager 注册快捷键
    m_mainWindow->m_shortcutManager->registerShortcutToAction(Shortcut_Help, m_aboutAction);

    connect(m_aboutAction, &QAction::triggered, this, &uiManager::onMenuBar_Help);

    LOG_INFO("Menu bar initialized successfully");

    return ;
}

void uiManager::onMenuBar_New() {

    LOG_INFO("Creating new window");

    // 创建一个新的 MainWindow 对象
    MainWindow *newWindow = new MainWindow();

    if (newWindow_offest) {
        // 获取当前窗口的位置
        QPoint currentPos = parent->pos();

        // 设置偏移量
        int offsetX = 50;  // X方向偏移
        int offsetY = 50;  // Y方向偏移

        // 设置新窗口的位置为当前窗口的位置加上偏移量
        newWindow->move(currentPos.x() + offsetX, currentPos.y() + offsetY);
    }

    // 在新窗口的状态栏中显示"New window"
    QStatusBar *newWindowStatusbar = newWindow->statusBar(); // 获取新窗口的状态栏
    if (newWindowStatusbar) {
        newWindowStatusbar->showMessage(tr("New window"), 3000); // 显示3秒
    }

    // 显示新窗口
    newWindow->show();

    newWindow_offest = true;    // 默认每次新建窗口发生偏移

    LOG_INFO("New window created successfully");

    return ;
}

void uiManager::retranslateUi() {
    // 窗口标题
    parent->setWindowTitle(QCoreApplication::applicationName());

    // 菜单标题
    fileMenu->setTitle(tr("File"));
    displayMenu->setTitle(tr("Display"));
    windowMenu->setTitle(tr("Window"));
    optionsMenu->setTitle(tr("Options"));
    helpMenu->setTitle(tr("Help"));

    // File 菜单 actions
    m_newAction->setText(tr("Open new window"));
    m_exitAction->setText(tr("Exit"));

    // Window 菜单 actions
    m_fullScreenAction->setText(tr("Full screen"));
    m_lockManageAction->setText(tr("Lock the layout"));
    m_saveLayoutAction->setText(tr("Save the layout"));

    // Options 菜单 actions
    m_optionsAction->setText(tr("Settings"));

    // Help 菜单 actions
    m_aboutAction->setText(tr("About"));

    // Dock 窗口标题
    if (m_logViewDock)   m_logViewDock->setWindowTitle(tr("Log viewer"));
    if (m_httpServerManagerDock) {
        m_httpServerManagerDock->setWindowTitle(tr("HTTP Server"));
    }

    LOG_INFO("UI retranslated");
}

void uiManager::languageComBox_currenIndexChanged(int index) {

    switch (index) {
    case English_UK:
        m_mainWindow->m_softwareconfig->setLanguage(English_UK);
        m_mainWindow->m_Languagemanager->loadLanguage(English_UK);
        break;
    case Chinese_simpled:
        m_mainWindow->m_softwareconfig->setLanguage(Chinese_simpled);
        m_mainWindow->m_Languagemanager->loadLanguage(Chinese_simpled);
        break;
    default:
        m_mainWindow->m_softwareconfig->setLanguage(English_UK);
        m_mainWindow->m_Languagemanager->loadLanguage(English_UK);
        break;
    }

    m_mainWindow->m_softwareconfig->Write_config();     // 保存至配置文件

    statusbar->showMessage(tr("Language: %1").arg(languageList[index]), 3000);

    return ;
}

void uiManager::themeComboBox_currenIndexChanged(int index) {

    switch (index) {
    case theme_default:
        m_mainWindow->m_softwareconfig->setTheme(theme_default);
        m_mainWindow->m_thememanager->loadTheme(theme_default);
        break;
    case theme_Ubuntu:
        m_mainWindow->m_softwareconfig->setTheme(theme_Ubuntu);
        m_mainWindow->m_thememanager->loadTheme(theme_Ubuntu);
        break;
    case theme_Aqua:
        m_mainWindow->m_softwareconfig->setTheme(theme_Aqua);
        m_mainWindow->m_thememanager->loadTheme(theme_Aqua);
        break;
    case theme_ElegantDark:
        m_mainWindow->m_softwareconfig->setTheme(theme_ElegantDark);
        m_mainWindow->m_thememanager->loadTheme(theme_ElegantDark);
        break;
    case theme_MacOS:
        m_mainWindow->m_softwareconfig->setTheme(theme_MacOS);
        m_mainWindow->m_thememanager->loadTheme(theme_MacOS);
        break;
    case theme_ManjaroMix:
        m_mainWindow->m_softwareconfig->setTheme(theme_ManjaroMix);
        m_mainWindow->m_thememanager->loadTheme(theme_ManjaroMix);
        break;
    case theme_NeonButtons:
        m_mainWindow->m_softwareconfig->setTheme(theme_NeonButtons);
        m_mainWindow->m_thememanager->loadTheme(theme_NeonButtons);
        break;
    case theme_ProfessionalDark:
        m_mainWindow->m_softwareconfig->setTheme(theme_ProfessionalDark);
        m_mainWindow->m_thememanager->loadTheme(theme_ProfessionalDark);
        break;
    case theme_NVIDIA:
        m_mainWindow->m_softwareconfig->setTheme(theme_NVIDIA);
        m_mainWindow->m_thememanager->loadTheme(theme_NVIDIA);
        break;
    default:
        break;
    }

    m_mainWindow->m_softwareconfig->Write_config();     // 保存至配置文件

    statusbar->showMessage(tr("Theme: %1").arg(themeList[index]), 3000);

    return ;
}

void uiManager::onMenuBar_Options() {
    QDialog *optionsDialog = m_options->setupOptionsDialog();
    optionsDialog->exec();
}

void uiManager::onMenuBar_Exit() {
    parent->close();
}

void uiManager::onMenuBar_Help() {

    // 创建并显示关于对话框
    if (!m_mainWindow->m_aboutDialog) {
        m_mainWindow->m_aboutDialog = new aboutDialog(m_mainWindow);
    }

    QDialog *aboutDlg = m_mainWindow->m_aboutDialog->setupAboutDialog();
    aboutDlg->exec();

}

void uiManager::initSplash()
{
    QPixmap pixmap(AppIcons::Administrator);
    m_splash = new QSplashScreen(pixmap);
    m_splash->show();
    m_splash->showMessage("Loading...", Qt::AlignBottom | Qt::AlignCenter, Qt::white);
}

void uiManager::closeSplash()
{
    if (m_splash) {
        m_splash->close();
        delete m_splash;
        m_splash = nullptr;
    }
}

void uiManager::initTray()
{
    m_trayMenu = new QMenu(parent);

    QAction *showAction = m_trayMenu->addAction(tr("Show"));
    m_trayOpenWebAction = m_trayMenu->addAction(tr("Open Web Panel"));
    m_trayServerAction = m_trayMenu->addAction(tr("Start Server"));
    m_trayMarkAllReadAction = m_trayMenu->addAction(
        tr("Mark All Notifications Read"));
    m_trayMenu->addSeparator();
    QAction *quitAction = m_trayMenu->addAction(tr("Quit"));

    connect(showAction, &QAction::triggered, this, [this]() {
        m_mainWindow->showNormal();
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
    });
    connect(m_trayOpenWebAction, &QAction::triggered,
            this, &uiManager::openWebPanelRequested);
    connect(m_trayServerAction, &QAction::triggered, this, [this]() {
        if (m_trayServerState == TrayServerState::Running) {
            emit stopServerRequested();
        } else {
            emit startServerRequested();
        }
    });
    connect(m_trayMarkAllReadAction, &QAction::triggered,
            this, &uiManager::markAllNotificationsReadRequested);
    connect(quitAction, &QAction::triggered,
            this, &uiManager::quitRequested);

    m_trayIcon = new QSystemTrayIcon(parent);
    m_trayIcon->setIcon(QIcon(AppIcons::App));
    m_trayIcon->setToolTip(QCoreApplication::applicationName());
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, m_mainWindow, [this](QSystemTrayIcon::ActivationReason r){
        if (r == QSystemTrayIcon::Trigger) {
            m_mainWindow->showNormal();
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        }
    });
    connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, [this]() {
        if (m_lastToastIssueId > 0) {
            emit openIssueRequested(m_lastToastIssueId);
        }
    });

    refreshTrayPresentation();
    m_trayIcon->show();
}

void uiManager::setServerState(TrayServerState state)
{
    m_trayServerState = state;
    refreshTrayPresentation();
}

void uiManager::setUnreadCount(qint64 unreadCount)
{
    m_trayUnreadCount = qMax<qint64>(0, unreadCount);
    refreshTrayPresentation();
}

void uiManager::showAdminNotification(qint64 issueId,
                                      const QString &title,
                                      const QString &content)
{
    if (!m_trayIcon || !m_trayIcon->isVisible()) {
        return;
    }
    m_lastToastIssueId = issueId;
    m_trayIcon->showMessage(title, content,
                            QSystemTrayIcon::Information, 5000);
}

void uiManager::refreshTrayPresentation()
{
    if (!m_trayIcon) {
        return;
    }
    const qreal dpr = m_mainWindow->windowHandle()
        ? m_mainWindow->windowHandle()->devicePixelRatio() : 1.0;
    m_trayIcon->setIcon(QIcon(TrayIconRenderer::render(
        QIcon(AppIcons::App), m_trayServerState, m_trayUnreadCount, 32, dpr)));

    const TrayActionState actions = TrayIconRenderer::actionState(
        m_trayServerState, m_trayUnreadCount);
    m_trayOpenWebAction->setEnabled(actions.openWebEnabled);
    m_trayServerAction->setEnabled(actions.serverActionEnabled);
    m_trayMarkAllReadAction->setEnabled(actions.markAllReadEnabled);
    switch (actions.serverAction) {
    case TrayServerAction::Start:
        m_trayServerAction->setText(tr("Start Server"));
        break;
    case TrayServerAction::Stop:
        m_trayServerAction->setText(tr("Stop Server"));
        break;
    case TrayServerAction::Starting:
        m_trayServerAction->setText(tr("Starting Server"));
        break;
    case TrayServerAction::Stopping:
        m_trayServerAction->setText(tr("Stopping Server"));
        break;
    case TrayServerAction::Retry:
        m_trayServerAction->setText(tr("Retry Server"));
        break;
    }
}

bool uiManager::onCloseEvent(QCloseEvent *)
{
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_mainWindow->hide();
        if (!m_backgroundMessageShown) {
            m_trayIcon->showMessage(QCoreApplication::applicationName(),
                                    tr("Running in background"),
                                    QSystemTrayIcon::Information, 2000);
            m_backgroundMessageShown = true;
        }
        return false; // 不真正退出
    }
    return true;      // 真正退出
}


