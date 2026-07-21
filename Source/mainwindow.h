#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "DockManager.h"

class uiManager;
class SoftwareConfig;
class ThemeManager;
class LanguageManager;
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

public:
    SoftwareConfig * m_softwareconfig;
    ThemeManager *m_thememanager;
    LanguageManager *m_Languagemanager;
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

private:
    Ui::MainWindow *ui;

    void closeEvent(QCloseEvent *e);
    void changeEvent(QEvent *e);


};
#endif // MAINWINDOW_H
