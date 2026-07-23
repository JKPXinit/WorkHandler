#ifndef MAINTENANCEMANAGER_H
#define MAINTENANCEMANAGER_H

#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>

class MaintenanceDao;
class QTimer;

enum class MaintenanceLogLevel {
    Info,
    Warning,
    Error
};

struct MaintenanceConfig
{
    QString logFilePath;
    QString activeLogFilePath;
    int logRetentionDays {30};
};

struct AttachmentMaintenanceSummary
{
    qint64 scanned {0};
    qint64 restored {0};
    qint64 deleted {0};
    qint64 candidates {0};
    qint64 missingReferences {0};
    qint64 backfilledOriginals {0};
    qint64 failures {0};
};

class MaintenanceManager : public QObject
{
public:
    using ConfigProvider = std::function<MaintenanceConfig()>;
    using LogHandler = std::function<void(MaintenanceLogLevel,
                                          const QString &)>;
    using RunningProvider = std::function<bool()>;
    using NowProvider = std::function<QDateTime()>;

    MaintenanceManager(MaintenanceDao &dao,
                       const QString &uploadRoot,
                       ConfigProvider configProvider,
                       RunningProvider runningProvider,
                       LogHandler logHandler,
                       QObject *parent = nullptr,
                       NowProvider nowProvider = NowProvider());

    void runStartupMaintenance();
    void runDailyMaintenance();
    void onServerStopped();
    void startDailyTimer();
    void stopDailyTimer();
    bool isRunning() const;

private:
    bool beginTask();
    void endTask();
    void runQuickCheck();
    void cleanExpiredLogs();
    AttachmentMaintenanceSummary maintainAttachments();
    bool isVacuumDue();
    void runVacuumIfSafe(bool serverRunning);
    QString safeUploadPath(const QString &relativePath) const;
    void log(MaintenanceLogLevel level, const QString &message) const;
    QDateTime nowUtc() const;

    MaintenanceDao &m_dao;
    QString m_uploadRoot;
    ConfigProvider m_configProvider;
    RunningProvider m_runningProvider;
    LogHandler m_logHandler;
    NowProvider m_nowProvider;
    QTimer *m_dailyTimer {nullptr};
    bool m_running {false};
    bool m_vacuumPending {false};
};

#endif // MAINTENANCEMANAGER_H
