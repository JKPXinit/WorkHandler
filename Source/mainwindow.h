#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "DockManager.h"

class uiManager;
class SoftwareConfig;
class ThemeManager;
class LanguageManager;
class PasswordVerfy;
class emptyDialog;
class ShortcutManager;
class logViewerDialog;
class aboutDialog;
class HttpServerManagerDialog;
class HttpServer;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

Q_SIGNALS:
    void adminModeChanged(bool granted);    // Admin 模式状态变更

public:
    SoftwareConfig * m_softwareconfig;
    ThemeManager *m_thememanager;
    LanguageManager *m_Languagemanager;
    PasswordVerfy *m_passwordverfy;
    uiManager *m_UI;
    emptyDialog *m_emptyDialog;
    logViewerDialog *m_logViewerDialog;
    aboutDialog *m_aboutDialog;
    HttpServerManagerDialog *m_httpServerManagerDialog {nullptr};
    HttpServer *m_httpServer {nullptr};
    ShortcutManager *m_shortcutManager;

    ads::CDockManager *m_DockManager;

private slots:
    void initConfigUI();
    void onAdminModeGranted();
    void onAdminModeRejected();
    void onRequestAdminAuth();      // 响应 uiManager 的密码验证请求
    void onAdminModeExited();       // 响应 uiManager 的主动退出 Admin 模式

private:
    Ui::MainWindow *ui;

    void closeEvent(QCloseEvent *e);
    void changeEvent(QEvent *e);


};
#endif // MAINWINDOW_H
