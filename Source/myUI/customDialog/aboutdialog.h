#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QObject>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QGroupBox>
#include <QTextEdit>
#include <QPixmap>
#include <QApplication>
#include <QSysInfo>

class MainWindow;

class aboutDialog : public QObject
{
    Q_OBJECT

public:
    explicit aboutDialog(MainWindow *mainWindow);
    ~aboutDialog();

    QDialog* setupAboutDialog();

private:
    MainWindow *m_mainWindow;
    QDialog *m_dialog = nullptr;

    // UI组件
    void setupUI();
    QWidget* createAboutTab();
    QWidget* createSystemInfoTab();
    QWidget* createLicenseTab();

    // 辅助函数
    QString getAppVersion() const;
    QString getAppBuildDate() const;
    QString getQtVersion() const;
    QString getSystemInfo() const;
    QString getLicenseText() const;
};

#endif // ABOUTDIALOG_H
