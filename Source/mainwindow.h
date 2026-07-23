#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUrl>

#include "DockManager.h"

class uiManager;
class SoftwareConfig;
class ThemeManager;
class LanguageManager;
class ShortcutManager;
class logViewerDialog;
class aboutDialog;
class HttpServerManagerDialog;
class HttpServer;
class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public:
    SoftwareConfig * m_softwareconfig;
    ThemeManager *m_thememanager;
    LanguageManager *m_Languagemanager;
    uiManager *m_UI;
    logViewerDialog *m_logViewerDialog;
    aboutDialog *m_aboutDialog;
    HttpServerManagerDialog *m_httpServerManagerDialog {nullptr};
    HttpServer *m_httpServer {nullptr};
    ShortcutManager *m_shortcutManager;

    ads::CDockManager *m_DockManager;

private slots:
    void initConfigUI();

private:
    Ui::MainWindow *ui;
    QTimer *m_notificationCalibrationTimer {nullptr};
    QUrl m_pendingWebUrl;

    void closeEvent(QCloseEvent *e);
    void changeEvent(QEvent *e);
    void refreshTrayUnreadCount();
    bool startConfiguredServer();
    void openWebPanel(qint64 issueId = 0);


};
#endif // MAINWINDOW_H
