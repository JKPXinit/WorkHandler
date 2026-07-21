#ifndef _UIMANAGER_H
#define _UIMANAGER_H

#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QObject>
#include <QTranslator>

#include "public.h"


#define UI_DEBUUG   0

class MainWindow;
class ThemeManager;
class ui_AdminOptions;
class logViewerDialog;
class HttpServerManagerDialog;
class QSystemTrayIcon;
class QSplashScreen;
namespace ads { class CDockWidget; }

class uiManager : public QObject
{
    Q_OBJECT

public:
    uiManager(MainWindow *mainWindow);
    void closeSplash();
    bool onCloseEvent(QCloseEvent *);

public:
    QDialog *emptyViewDialog;
    QDialog *logVieweDialog;
    QDialog *httpServerManagerView;

    QIcon appIcon;
    QIcon OptionsIcon;
    QIcon AdministratorIcon;
    QIcon PasswordIcon;
    QIcon AccountIcon;
    QIcon BasicIcon;
    QIcon ThemeIcon;
    QIcon LanguageIcon;
    QIcon DeleteIcon;
    QIcon SystemIcon;
    QIcon ShortcutIcon;


public slots:
    void setupUI ();
    void retranslateUi();
    void languageComBox_currenIndexChanged(int index);
    void themeComboBox_currenIndexChanged(int index);

private slots:

    void onMenuBar_New();
    void onMenuBar_Options();
    void onMenuBar_Exit();
    void onMenuBar_Help();
    void initMenuBar ();
    void initDockWindows();

private:
    ThemeManager *m_themeManager;
    ui_AdminOptions *m_options;

    QMainWindow *parent;
    MainWindow *m_mainWindow;  // 指向 MainWindow 的指针

    QStatusBar *statusbar;
    QTranslator *translator;
    QMenu *fileMenu;        // “文件”菜单
    QMenu *displayMenu;     // “显示”菜单
    QMenu *windowMenu;      // “窗口”菜单
    QMenu *optionsMenu;     // “选项”菜单
    QMenu *helpMenu;        // “帮助”菜单

    // 菜单 Action 指针（用于 retranslateUi）
    QAction *m_newAction        {nullptr};
    QAction *m_exitAction       {nullptr};
    QAction *m_fullScreenAction {nullptr};
    QAction *m_lockManageAction {nullptr};
    QAction *m_saveLayoutAction {nullptr};
    QAction *m_optionsAction    {nullptr};
    QAction *m_aboutAction      {nullptr};

    // Dock 窗口指针（用于 retranslateUi）
    ads::CDockWidget *m_emptyViewDock {nullptr};
    ads::CDockWidget *m_logViewDock   {nullptr};
    ads::CDockWidget *m_httpServerManagerDock {nullptr};

    bool newWindow_offest = true;

    void initSplash();
    void initTray();


    QSplashScreen *m_splash{nullptr};
    QSystemTrayIcon *m_trayIcon{nullptr};
    QMenu *m_trayMenu{nullptr};

    void initIcon();
};

#endif // _UIMANAGER_H
