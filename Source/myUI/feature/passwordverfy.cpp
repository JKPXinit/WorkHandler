#include "passwordverfy.h"
#include "mainwindow.h"
#include "uiAnimation.h"

#include <QMessageBox>

PasswordVerfy::PasswordVerfy(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;

    InitializePWHash();

}

void PasswordVerfy::pathError (const QString &path) {

    // 配置文件创建失败，弹出消息框并退出程序
    QMessageBox::critical(nullptr, QObject::tr("Error"),
                          QObject::tr("Failed to create file: %1").arg(path),
                          QMessageBox::Ok);
    std::exit(1); // 退出程序，返回值为1表示异常退出

    return ;
}

/*************************************************************
 *  修改密码
 *  @param newPassword 新密码
 *  @return true 如果密码修改成功，false 如果密码修改失败
 * **********************************************************/
bool PasswordVerfy::modifiedPassword(const QString &newPassword) {

    // 检查新密码是否为空
    if (newPassword.isEmpty()) {
        QMessageBox *warningBox = new QMessageBox(m_mainWindow);
        warningBox->setWindowTitle(tr("Password Change"));
        warningBox->setText(tr("Password cannot be empty."));
        warningBox->setIcon(QMessageBox::Warning);
        warningBox->setStandardButtons(QMessageBox::Ok);

        // 先显示对话框
        warningBox->show();

        // 应用震动动画（增加震动强度）
        QSequentialAnimationGroup *shakeAnim = uiAnimation::shake(warningBox, 20);
        if (shakeAnim) {
            shakeAnim->start(QAbstractAnimation::DeleteWhenStopped);
        }

        warningBox->exec();
        delete warningBox;
        return false;
    }

    // 生成新密码的哈希值
    QByteArray newHash = QCryptographicHash::hash(newPassword.toUtf8(), QCryptographicHash::Sha256).toHex();

    // 将新的哈希值保存到文件中
    saveHashToFile(PWHashFilePath, newHash);

    // 更新存储的哈希值
    storedHash = newHash;

    return true;
}


/*
* 初始化密码哈希数文件
*
*/
void PasswordVerfy::InitializePWHash() {

    QString PWHashFolderPath = Practical_Function::ResourcePath("/Hash/");
    PWHashFilePath = PWHashFolderPath + "/password.hash";

    QDir dir;
    if (!dir.mkpath(PWHashFolderPath)) {
        pathError (PWHashFolderPath);
    } else {

#ifdef Q_OS_WIN // 隐藏 Hash/ 目录
        DWORD attributes = GetFileAttributesW(PWHashFolderPath.toStdWString().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(PWHashFolderPath.toStdWString().c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
        }
#endif

        QFile frq(PWHashFilePath);
        if (frq.exists()) {  // 文件存在
            readStoredHash();  // 直接读取配置文件
        } else {  // 文件不存在
            if (frq.open(QIODevice::ReadWrite)) { // 尝试新建配置文件
                defaultStoredHash();
                frq.close();
            } else {
                pathError (PWHashFilePath);
            }
        }
    }
}

bool PasswordVerfy::pwinputDialog () {

    storedHash = readStoredHash();  // 读取本地的密码哈希数

    // 创建密码输入对话框（使用 m_mainWindow 作为 parent，因为 optionsDialog 已经关闭）
    QInputDialog *inputDialog = new QInputDialog(m_mainWindow);
    inputDialog->setWindowTitle(tr("Admin Mode"));
    inputDialog->setLabelText(tr("Enter password:"));
    inputDialog->setTextEchoMode(QLineEdit::Password);
    inputDialog->setTextValue("");

    // 显示对话框
    bool ok = (inputDialog->exec() == QDialog::Accepted);
    QString password = inputDialog->textValue();
    delete inputDialog;

    if (ok && !password.isEmpty()) {
        // 生成输入密码的哈希值
        QByteArray passwordHash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex();

        // 检查哈希值是否匹配
        if (passwordHash == storedHash) {
            emit adminModeGranted();
            return true;
        } else {
            QMessageBox *warningBox = new QMessageBox(m_mainWindow);
            warningBox->setWindowTitle(tr("Admin Mode"));
            warningBox->setText(tr("Incorrect password."));
            warningBox->setIcon(QMessageBox::Warning);
            warningBox->setStandardButtons(QMessageBox::Ok);

            // 先显示对话框
            warningBox->show();

            // 应用震动动画（增加震动强度）
            QSequentialAnimationGroup *shakeAnim = uiAnimation::shake(warningBox, 20);
            if (shakeAnim) {
                shakeAnim->start(QAbstractAnimation::DeleteWhenStopped);
            }

            warningBox->exec();
            delete warningBox;

            emit adminModeRejected();
        }
    } else {
        emit adminModeRejected();
    }

    return false;
}

QString PasswordVerfy::readStoredHash() {

    QFile file(PWHashFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString storedHash = in.readLine();
        file.close();
        return storedHash;
    } else {     // 如果文件不存在，使用默认密码生成哈希数
        QByteArray storedHash = QCryptographicHash::hash(defaultAdminPassword.toUtf8(), QCryptographicHash::Sha256).toHex();
        saveHashToFile(PWHashFilePath, storedHash);
        return storedHash;
    }
}

void PasswordVerfy::defaultStoredHash() {

    QByteArray storedHash = QCryptographicHash::hash(defaultAdminPassword.toUtf8(), QCryptographicHash::Sha256).toHex();
    saveHashToFile(PWHashFilePath, storedHash);

    return ;
}

void PasswordVerfy::saveHashToFile(const QString &filePath, const QByteArray &hash) {
    QFile file(filePath);

    if (!file.exists()) {
        pathError(filePath); // 如果文件无法打开，调用 pathError 函数
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << hash;
        file.close();
    }
}

