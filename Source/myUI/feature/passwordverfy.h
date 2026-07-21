#ifndef PASSWORDVERFY_H
#define PASSWORDVERFY_H

#include <QInputDialog>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#include <windows.h>    // 包含 Windows 头文件（仅在 Windows 平台上）
#endif

#include "public.h"

class MainWindow;

class PasswordVerfy : public QObject // 继承自 QObject
{
    Q_OBJECT // 宏，用于支持信号和槽机制

public:
    PasswordVerfy(MainWindow *mainWindow);

public slots:
    bool pwinputDialog ();
    void InitializePWHash();
    void defaultStoredHash();
    bool modifiedPassword (const QString &newPassword);

signals:
    void adminModeGranted();    // 密码验证通过
    void adminModeRejected();   // 密码验证失败或取消

private:
    MainWindow *m_mainWindow;
    QString storedHash = {};
    QString defaultAdminPassword = "admin123"; // 初始密码
    QString PWHashFilePath = {};

private slots:
    QString readStoredHash();
    void saveHashToFile(const QString &filePath, const QByteArray &hash);
    void pathError (const QString &path);

};

#endif // PASSWORDVERFY_H
