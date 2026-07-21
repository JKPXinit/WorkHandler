#ifndef MYLOGGER_H
#define MYLOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>
#include <QString>

// 日志级别枚举
enum LogLevel {
    LogLevel_Debug = 0,     // 调试信息
    LogLevel_Info,          // 一般信息
    LogLevel_Warning,       // 警告
    LogLevel_Error,         // 错误
    LogLevel_Fatal          // 致命错误
};

// 日志配置结构
struct LogConfig {
    bool enableFileLogging;     // 启用文件日志
    bool enableConsoleLogging;  // 启用控制台日志
    QString logFilePath;        // 自定义日志文件路径（空则使用默认）
    LogLevel minLevel;          // 最小日志级别

    // 日志轮转配置
    bool enableRotation;        // 是否启用轮转（默认 false）
    int  rotationMode;          // 0=按大小, 1=按时间
    int  maxFileSizeMB;         // 按大小时的阈值（MB，默认 10）
    int  rotationIntervalDays;  // 按时间时的间隔（天，默认 1）
    int  maxBackupCount;        // 最多保留的备份文件数（默认 5）
};

class myLogger : public QObject
{
    Q_OBJECT

private:
    // 单例模式：私有构造函数
    explicit myLogger(QObject *parent = nullptr);
    ~myLogger();

    // 禁止拷贝和赋值
    myLogger(const myLogger&) = delete;
    myLogger& operator=(const myLogger&) = delete;

public:
    // 获取单例实例
    static myLogger* instance();

    // 初始化日志系统
    void initialize(const LogConfig &config);

    // 设置日志配置
    void setConfig(const LogConfig &config);

    // 获取当前配置
    LogConfig getConfig() const;

    // 写入日志（核心函数）
    void log(LogLevel level, const QString &message,
             const char *file, int line, const char *function);

    // 便捷日志函数
    void debug(const QString &message, const char *file, int line, const char *function);
    void info(const QString &message, const char *file, int line, const char *function);
    void warning(const QString &message, const char *file, int line, const char *function);
    void error(const QString &message, const char *file, int line, const char *function);
    void fatal(const QString &message, const char *file, int line, const char *function);

    // 关闭日志系统
    void shutdown();

    // 获取默认日志文件路径
    static QString getDefaultLogFilePath();

signals:
    // 日志消息信号（可用于 UI 显示）
    void logMessageEmitted(LogLevel level, const QString &message);

private:
    static myLogger *m_instance;        // 单例实例
    static QMutex m_instanceMutex;      // 实例创建互斥锁

    QFile *m_logFile;                   // 日志文件
    QTextStream *m_logStream;           // 日志流
    QMutex m_logMutex;                  // 日志写入互斥锁
    LogConfig m_config;                 // 日志配置
    QDateTime m_startTime;              // 程序启动时间
    QDateTime m_fileOpenTime;           // 当前日志文件的打开时间

    // 格式化日志消息
    QString formatLogMessage(LogLevel level, const QString &message,
                             const char *file, int line, const char *function);

    // 获取日志级别字符串
    QString logLevelToString(LogLevel level) const;

    // 打开日志文件
    bool openLogFile(const QString &filePath);

    // 关闭日志文件
    void closeLogFile();

    // 生成默认日志文件名
    QString generateDefaultLogFileName();

    // 检查是否需要轮转
    void checkAndRotate();

    // 执行日志轮转
    void rotateLogFile();
};

// ============================================
// 便捷宏定义（自动捕获文件名、行号、函数名）
// ============================================

#define LOG_DEBUG(msg) \
    myLogger::instance()->debug(msg, __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_INFO(msg) \
    myLogger::instance()->info(msg, __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_WARNING(msg) \
    myLogger::instance()->warning(msg, __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_ERROR(msg) \
    myLogger::instance()->error(msg, __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_FATAL(msg) \
    myLogger::instance()->fatal(msg, __FILE__, __LINE__, Q_FUNC_INFO)

#endif // MYLOGGER_H
