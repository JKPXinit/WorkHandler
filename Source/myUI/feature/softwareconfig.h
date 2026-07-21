#ifndef SORTWARECONFIG_H
#define SORTWARECONFIG_H

#include <QObject>
#include <QFile>
#include <QMap>
#include <QtGlobal>

#include "public.h"

class MainWindow;

// 日志输出模式枚举
enum LogOutputMode {
    LogOutputConsole = 0,   // 仅控制台输出
    LogOutputFile = 1       // 仅文件输出
};

typedef struct m_Options {

    struct uiConfig_t {
        int language;       // 语言
        int theme;          // 主题
    };
    uiConfig_t m_uiConfig;

    struct systemConfig_t {
        bool first;             // 是否首次启用软件
        int exitMode;           // 软件退出模式
        int logOutputMode;      // 日志输出模式 (0:控制台, 1:文件)
        QString logFilePath;    // 日志文件路径（空则使用默认）

        // 日志轮转配置
        bool logRotationEnabled;        // 是否启用轮转
        int  logRotationMode;           // 0=按大小, 1=按时间
        int  logRotationMaxSizeMB;      // 按大小时的阈值（MB）
        int  logRotationIntervalDays;   // 按时间时的间隔（天）
        int  logRotationMaxBackups;     // 最多保留的备份文件数
    };
    systemConfig_t m_systemConfig;

    struct httpServerConfig_t {
        QString interfaceName;       // 上次选择的网卡系统名称
        QString selectedAddress;     // 用于绑定或展示的 IPv4 地址
        quint16 port;                // HTTP 监听端口
        bool bindAllInterfaces;      // 是否绑定 0.0.0.0
        bool autoStart;              // 应用启动时自动启动 HTTP 服务
        bool keepOriginal;           // 图片处理后是否保留原图
        int maxImageWidth;           // 图片处理最大宽度
    };
    httpServerConfig_t m_httpServerConfig;

    struct shortcutConfig_t {
        QMap<int, QString> keys;    // ShortcutId(int) → QKeySequence 字符串
    };
    shortcutConfig_t m_shortcutConfig;

}m_Options_t;

class SoftwareConfig : public QObject
{
    Q_OBJECT
public:
    explicit SoftwareConfig(QObject *parent = nullptr);

    // UI config
    int language() const { return m_Config->m_uiConfig.language; }
    void setLanguage(int lang) { m_Config->m_uiConfig.language = lang; }
    int theme() const { return m_Config->m_uiConfig.theme; }
    void setTheme(int t) { m_Config->m_uiConfig.theme = t; }

    // System config
    bool isFirstRun() const { return m_Config->m_systemConfig.first; }
    void setFirstRun(bool first) { m_Config->m_systemConfig.first = first; }
    int exitMode() const { return m_Config->m_systemConfig.exitMode; }
    void setExitMode(int mode) { m_Config->m_systemConfig.exitMode = mode; }
    int logOutputMode() const { return m_Config->m_systemConfig.logOutputMode; }
    void setLogOutputMode(int mode) { m_Config->m_systemConfig.logOutputMode = mode; }
    QString logFilePath() const { return m_Config->m_systemConfig.logFilePath; }
    void setLogFilePath(const QString &path) { m_Config->m_systemConfig.logFilePath = path; }
    bool logRotationEnabled() const { return m_Config->m_systemConfig.logRotationEnabled; }
    void setLogRotationEnabled(bool en) { m_Config->m_systemConfig.logRotationEnabled = en; }
    int logRotationMode() const { return m_Config->m_systemConfig.logRotationMode; }
    void setLogRotationMode(int mode) { m_Config->m_systemConfig.logRotationMode = mode; }
    int logRotationMaxSizeMB() const { return m_Config->m_systemConfig.logRotationMaxSizeMB; }
    void setLogRotationMaxSizeMB(int mb) { m_Config->m_systemConfig.logRotationMaxSizeMB = mb; }
    int logRotationIntervalDays() const { return m_Config->m_systemConfig.logRotationIntervalDays; }
    void setLogRotationIntervalDays(int days) { m_Config->m_systemConfig.logRotationIntervalDays = days; }
    int logRotationMaxBackups() const { return m_Config->m_systemConfig.logRotationMaxBackups; }
    void setLogRotationMaxBackups(int count) { m_Config->m_systemConfig.logRotationMaxBackups = count; }

    // HTTP server config
    QString httpServerInterfaceName() const { return m_Config->m_httpServerConfig.interfaceName; }
    void setHttpServerInterfaceName(const QString &name) { m_Config->m_httpServerConfig.interfaceName = name; }
    QString httpServerSelectedAddress() const { return m_Config->m_httpServerConfig.selectedAddress; }
    void setHttpServerSelectedAddress(const QString &address) { m_Config->m_httpServerConfig.selectedAddress = address; }
    quint16 httpServerPort() const { return m_Config->m_httpServerConfig.port; }
    void setHttpServerPort(quint16 port) { m_Config->m_httpServerConfig.port = port; }
    bool httpServerBindAllInterfaces() const { return m_Config->m_httpServerConfig.bindAllInterfaces; }
    void setHttpServerBindAllInterfaces(bool enabled) { m_Config->m_httpServerConfig.bindAllInterfaces = enabled; }
    bool httpServerAutoStart() const { return m_Config->m_httpServerConfig.autoStart; }
    void setHttpServerAutoStart(bool enabled) { m_Config->m_httpServerConfig.autoStart = enabled; }
    bool httpServerKeepOriginal() const { return m_Config->m_httpServerConfig.keepOriginal; }
    void setHttpServerKeepOriginal(bool enabled) { m_Config->m_httpServerConfig.keepOriginal = enabled; }
    int httpServerMaxImageWidth() const { return m_Config->m_httpServerConfig.maxImageWidth; }
    void setHttpServerMaxImageWidth(int width) { m_Config->m_httpServerConfig.maxImageWidth = width; }

    // Shortcut config
    const QMap<int, QString>& shortcutKeys() const { return m_Config->m_shortcutConfig.keys; }
    void setShortcutKeys(const QMap<int, QString> &keys) { m_Config->m_shortcutConfig.keys = keys; }

public slots:
    void Read_config();
    void Write_config();
    void saveLayoutConfig();
    void restoreLayoutConfig();

signals:

private:
    m_Options_t *m_Config{nullptr};

    QFile *m_xmlFile;
    QString m_strContentXml;
    QString configFilePath;     // 软件配置文件路径

    MainWindow *m_mainWindow;  // 指向 MainWindow 的指针

private slots:
    void InitializeXmlConfig();
    void Default_Config();
    void pathError (const QString &path);
    m_Options_t *strXml_to_Options(QString strxml);
    QString Options_to_strXml(m_Options_t *OptionsTmp);
};

#endif // SORTWARECONFIG_H
