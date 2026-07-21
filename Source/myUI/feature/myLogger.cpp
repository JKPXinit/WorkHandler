#include "myLogger.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include "myLogger.h"

// 静态成员初始化
myLogger* myLogger::m_instance = nullptr;
QMutex myLogger::m_instanceMutex;

myLogger::myLogger(QObject *parent)
    : QObject(parent)
    , m_logFile(nullptr)
    , m_logStream(nullptr)
    , m_startTime(QDateTime::currentDateTime())
{
    // 默认配置
    m_config.enableFileLogging    = true;
    m_config.enableConsoleLogging = true;
    m_config.logFilePath          = "";
    m_config.minLevel             = LogLevel_Debug;
    m_config.enableRotation       = false;
    m_config.rotationMode         = 0;
    m_config.maxFileSizeMB        = 10;
    m_config.rotationIntervalDays = 1;
    m_config.maxBackupCount       = 5;
}

myLogger::~myLogger()
{
    shutdown();
}

myLogger* myLogger::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new myLogger();
        }
    }
    return m_instance;
}

void myLogger::initialize(const LogConfig &config)
{
    QMutexLocker locker(&m_logMutex);

    m_config = config;

    // 如果启用文件日志，打开日志文件
    if (m_config.enableFileLogging) {
        QString logPath = m_config.logFilePath.isEmpty()
                          ? getDefaultLogFilePath()
                          : m_config.logFilePath;

        if (!openLogFile(logPath)) {
            qWarning() << "Failed to open log file:" << logPath;
            m_config.enableFileLogging = false;
        }
    }

    // 初始化完成后记录日志
    locker.unlock(); // 解锁以便调用 log()
    LOG_INFO(QString("Logger initialized - File: %1, Console: %2")
             .arg(m_config.enableFileLogging ? "Enabled" : "Disabled")
             .arg(m_config.enableConsoleLogging ? "Enabled" : "Disabled"));
}

void myLogger::setConfig(const LogConfig &config)
{
    QMutexLocker locker(&m_logMutex);

    bool needReopenFile = (config.logFilePath != m_config.logFilePath) ||
                          (config.enableFileLogging != m_config.enableFileLogging);

    m_config = config;

    if (needReopenFile) {
        closeLogFile();

        if (m_config.enableFileLogging) {
            QString logPath = m_config.logFilePath.isEmpty()
                              ? getDefaultLogFilePath()
                              : m_config.logFilePath;

            if (!openLogFile(logPath)) {
                qWarning() << "Failed to reopen log file:" << logPath;
                m_config.enableFileLogging = false;
            }
        }
    }
}

LogConfig myLogger::getConfig() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_logMutex));
    return m_config;
}

void myLogger::log(LogLevel level, const QString &message,
                   const char *file, int line, const char *function)
{
    QMutexLocker locker(&m_logMutex);

    // 检查日志级别过滤
    if (level < m_config.minLevel) {
        return;
    }

    // 格式化日志消息
    QString formattedMessage = formatLogMessage(level, message, file, line, function);

    // 输出到控制台
    if (m_config.enableConsoleLogging) {
        switch (level) {
        case LogLevel_Debug:
            qDebug().noquote() << formattedMessage;
            break;
        case LogLevel_Info:
            qInfo().noquote() << formattedMessage;
            break;
        case LogLevel_Warning:
            qWarning().noquote() << formattedMessage;
            break;
        case LogLevel_Error:
        case LogLevel_Fatal:
            qCritical().noquote() << formattedMessage;
            break;
        }
    }

    // 输出到文件
    if (m_config.enableFileLogging && m_logStream) {
        *m_logStream << formattedMessage << "\n";
        m_logStream->flush();
        checkAndRotate();
    }

    // 发射信号（可用于 UI 显示）
    emit logMessageEmitted(level, formattedMessage);
}

void myLogger::debug(const QString &message, const char *file, int line, const char *function)
{
    log(LogLevel_Debug, message, file, line, function);
}

void myLogger::info(const QString &message, const char *file, int line, const char *function)
{
    log(LogLevel_Info, message, file, line, function);
}

void myLogger::warning(const QString &message, const char *file, int line, const char *function)
{
    log(LogLevel_Warning, message, file, line, function);
}

void myLogger::error(const QString &message, const char *file, int line, const char *function)
{
    log(LogLevel_Error, message, file, line, function);
}

void myLogger::fatal(const QString &message, const char *file, int line, const char *function)
{
    log(LogLevel_Fatal, message, file, line, function);
}

void myLogger::shutdown()
{
    QMutexLocker locker(&m_logMutex);

    // 直接写入关闭消息，避免递归调用
    if (m_config.enableConsoleLogging) {
        qInfo() << "Logger shutting down";
    }

    if (m_config.enableFileLogging && m_logStream) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        *m_logStream << QString("[%1] [INFO   ] Logger shutting down\n").arg(timestamp);
        m_logStream->flush();
    }

    closeLogFile();
}

QString myLogger::getDefaultLogFilePath()
{
    // 获取可执行文件目录
    QString exePath = QCoreApplication::applicationDirPath();
    QString logDir = exePath + "/log";

    // 确保 log 目录存在
    QDir dir;
    if (!dir.exists(logDir)) {
        dir.mkpath(logDir);
    }

    // 生成默认日志文件名（临时创建一个 myLogger 实例或直接生成）
    QString appName = QCoreApplication::applicationName();
    if (appName.isEmpty()) {
        appName = "App";
    }
    qint64 processId = QCoreApplication::applicationPid();
    QString startTime = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString logFileName = QString("[%1-%2]%3.log")
                          .arg(appName)
                          .arg(processId)
                          .arg(startTime);

    return logDir + "/" + logFileName;
}

QString myLogger::formatLogMessage(LogLevel level, const QString &message,
                                    const char *file, int line, const char *function)
{
    // 获取时间戳
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");

    // 获取文件名（去除路径）
    QString fileName = QFileInfo(file).fileName();

    // 获取函数名（简化 Qt 的函数签名）
    QString funcName = QString(function);
    // 简化 Qt 函数签名：移除返回类型和参数类型
    int parenPos = funcName.indexOf('(');
    if (parenPos > 0) {
        int spacePos = funcName.lastIndexOf(' ', parenPos);
        if (spacePos > 0) {
            funcName = funcName.mid(spacePos + 1);
        }
    }

    // 格式化日志：[时间戳] [级别] [文件:行号] [函数] 消息
    QString formatted = QString("[%1] [%2] [%3:%4] [%5] %6")
                        .arg(timestamp)
                        .arg(logLevelToString(level), -7)  // 左对齐，宽度7
                        .arg(fileName)
                        .arg(line, 4)                       // 行号宽度4
                        .arg(funcName)
                        .arg(message);

    return formatted;
}

QString myLogger::logLevelToString(LogLevel level) const
{
    switch (level) {
    case LogLevel_Debug:   return "DEBUG";
    case LogLevel_Info:    return "INFO";
    case LogLevel_Warning: return "WARNING";
    case LogLevel_Error:   return "ERROR";
    case LogLevel_Fatal:   return "FATAL";
    default:               return "UNKNOWN";
    }
}

bool myLogger::openLogFile(const QString &filePath)
{
    // 关闭已有文件
    closeLogFile();

    // 创建日志文件
    m_logFile = new QFile(filePath);

    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete m_logFile;
        m_logFile = nullptr;
        return false;
    }

    m_logStream = new QTextStream(m_logFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_logStream->setEncoding(QStringConverter::Utf8);
#else
    m_logStream->setCodec("UTF-8");
#endif

    m_fileOpenTime = QDateTime::currentDateTime();  // 记录文件打开时间

    // 写入分隔线（新会话开始）
    *m_logStream << "\n";
    *m_logStream << "========================================\n";
    *m_logStream << "Log session started at: "
                 << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    *m_logStream << "========================================\n";
    m_logStream->flush();

    return true;
}

void myLogger::closeLogFile()
{
    if (m_logStream) {
        delete m_logStream;
        m_logStream = nullptr;
    }

    if (m_logFile) {
        if (m_logFile->isOpen()) {
            m_logFile->close();
        }
        delete m_logFile;
        m_logFile = nullptr;
    }
}

QString myLogger::generateDefaultLogFileName()
{
    // 获取应用程序名称
    QString appName = QCoreApplication::applicationName();
    if (appName.isEmpty()) {
        appName = "App";
    }

    // 获取进程 ID
    qint64 processId = QCoreApplication::applicationPid();

    // 获取启动时间
    QString startTime = m_startTime.toString("yyyyMMdd_HHmmss");

    // 格式：[AppName-PID]StartTime.log
    QString fileName = QString("[%1-%2]%3.log")
                       .arg(appName)
                       .arg(processId)
                       .arg(startTime);

    return fileName;
}

void myLogger::checkAndRotate()
{
    if (!m_config.enableRotation || !m_config.enableFileLogging || !m_logFile)
        return;

    bool shouldRotate = false;

    if (m_config.rotationMode == 0) {
        // 按文件大小
        shouldRotate = (m_logFile->size() >= (qint64)m_config.maxFileSizeMB * 1024 * 1024);
    } else {
        // 按时间
        shouldRotate = (m_fileOpenTime.daysTo(QDateTime::currentDateTime()) >= m_config.rotationIntervalDays);
    }

    if (shouldRotate)
        rotateLogFile();
}

void myLogger::rotateLogFile()
{
    if (!m_logFile)
        return;

    QString currentPath = m_logFile->fileName();
    QFileInfo fi(currentPath);
    QString dir      = fi.absolutePath();
    QString baseName = fi.completeBaseName();
    QString suffix   = fi.suffix();

    // 关闭当前文件
    closeLogFile();

    // 重命名旧文件：原名_rotated_yyyyMMdd_HHmmss.log
    QString timestamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString rotatedName = QString("%1/%2_rotated_%3.%4")
                          .arg(dir).arg(baseName).arg(timestamp).arg(suffix);
    QFile::rename(currentPath, rotatedName);

    // 打开新文件
    openLogFile(currentPath);

    // 清理超出 maxBackupCount 的最旧备份
    QDir logDir(dir);
    QStringList filters;
    filters << QString("*_rotated_*.%1").arg(suffix);
    QFileInfoList backups = logDir.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);

    while (!backups.isEmpty() && backups.size() > m_config.maxBackupCount) {
        QFile::remove(backups.first().absoluteFilePath());
        backups.removeFirst();
    }
}
