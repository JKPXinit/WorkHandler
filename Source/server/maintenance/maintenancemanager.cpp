#include "maintenance/maintenancemanager.h"

#include "maintenance/maintenancedao.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 DailyIntervalMilliseconds = 24LL * 60 * 60 * 1000;
constexpr qint64 VacuumIntervalSeconds = 30LL * 24 * 60 * 60;
constexpr qint64 StagedFileRetentionSeconds = 24LL * 60 * 60;
constexpr qint64 OrphanRetentionSeconds = 7LL * 24 * 60 * 60;
const QString DeletingMarker = QStringLiteral(".deleting-");

QString withTrailingSlash(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path));
    if (!normalized.endsWith(QLatin1Char('/'))) {
        normalized.append(QLatin1Char('/'));
    }
    return normalized;
}

bool isOlderThan(const QFileInfo &info,
                 const QDateTime &now,
                 qint64 seconds)
{
    return info.lastModified().toUTC().secsTo(now) > seconds;
}
}

MaintenanceManager::MaintenanceManager(
    MaintenanceDao &dao,
    const QString &uploadRoot,
    ConfigProvider configProvider,
    RunningProvider runningProvider,
    LogHandler logHandler,
    QObject *parent,
    NowProvider nowProvider)
    : QObject(parent)
    , m_dao(dao)
    , m_uploadRoot(QDir::cleanPath(uploadRoot))
    , m_configProvider(std::move(configProvider))
    , m_runningProvider(std::move(runningProvider))
    , m_logHandler(std::move(logHandler))
    , m_nowProvider(std::move(nowProvider))
    , m_dailyTimer(new QTimer(this))
{
    m_dailyTimer->setInterval(int(DailyIntervalMilliseconds));
    m_dailyTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(m_dailyTimer, &QTimer::timeout,
            this, [this]() { runDailyMaintenance(); });
}

void MaintenanceManager::runStartupMaintenance()
{
    if (!beginTask()) {
        return;
    }
    runQuickCheck();
    const AttachmentMaintenanceSummary summary = maintainAttachments();
    log(MaintenanceLogLevel::Info,
        QStringLiteral("Attachment maintenance: scanned=%1 restored=%2 "
                       "deleted=%3 candidates=%4 missing=%5 backfilled=%6 "
                       "failures=%7")
            .arg(summary.scanned)
            .arg(summary.restored)
            .arg(summary.deleted)
            .arg(summary.candidates)
            .arg(summary.missingReferences)
            .arg(summary.backfilledOriginals)
            .arg(summary.failures));
    runVacuumIfSafe(false);
    endTask();
}

void MaintenanceManager::runDailyMaintenance()
{
    if (!beginTask()) {
        return;
    }
    cleanExpiredLogs();
    const AttachmentMaintenanceSummary summary = maintainAttachments();
    log(MaintenanceLogLevel::Info,
        QStringLiteral("Daily attachment maintenance: scanned=%1 restored=%2 "
                       "deleted=%3 candidates=%4 missing=%5 backfilled=%6 "
                       "failures=%7")
            .arg(summary.scanned)
            .arg(summary.restored)
            .arg(summary.deleted)
            .arg(summary.candidates)
            .arg(summary.missingReferences)
            .arg(summary.backfilledOriginals)
            .arg(summary.failures));
    runVacuumIfSafe(m_runningProvider && m_runningProvider());
    endTask();
}

void MaintenanceManager::onServerStopped()
{
    if (!beginTask()) {
        return;
    }
    runVacuumIfSafe(false);
    endTask();
}

void MaintenanceManager::startDailyTimer()
{
    if (!m_dailyTimer->isActive()) {
        m_dailyTimer->start();
    }
}

void MaintenanceManager::stopDailyTimer()
{
    m_dailyTimer->stop();
}

bool MaintenanceManager::isRunning() const
{
    return m_running;
}

bool MaintenanceManager::beginTask()
{
    if (m_running) {
        log(MaintenanceLogLevel::Warning,
            QStringLiteral("Maintenance request skipped because another task is running."));
        return false;
    }
    m_running = true;
    return true;
}

void MaintenanceManager::endTask()
{
    m_running = false;
}

void MaintenanceManager::runQuickCheck()
{
    QStringList results;
    QString errorMessage;
    if (!m_dao.quickCheck(&results, &errorMessage)) {
        log(MaintenanceLogLevel::Error,
            QStringLiteral("Database quick_check failed: %1").arg(errorMessage));
        return;
    }
    if (results.size() != 1
        || results.constFirst().compare(QStringLiteral("ok"),
                                        Qt::CaseInsensitive) != 0) {
        log(MaintenanceLogLevel::Error,
            QStringLiteral("Database quick_check reported: %1")
                .arg(results.join(QStringLiteral("; "))));
    }
}

void MaintenanceManager::cleanExpiredLogs()
{
    const MaintenanceConfig config = m_configProvider
        ? m_configProvider() : MaintenanceConfig();
    const int retentionDays = qBound(1, config.logRetentionDays, 3650);
    const QString targetPath = !config.logFilePath.isEmpty()
        ? config.logFilePath : config.activeLogFilePath;
    if (targetPath.isEmpty()) {
        return;
    }

    const QFileInfo targetInfo(targetPath);
    QDir directory(targetInfo.absolutePath());
    if (!directory.exists()) {
        return;
    }
    const QString activePath = QDir::fromNativeSeparators(
        QFileInfo(config.activeLogFilePath).absoluteFilePath());
    const QDateTime cutoff = nowUtc().addDays(-retentionDays);
    qint64 deleted = 0;
    qint64 failures = 0;
    const QFileInfoList files = directory.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &file : files) {
        if (file.isSymLink()
            || file.suffix().compare(QStringLiteral("log"),
                                     Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString path = QDir::fromNativeSeparators(file.absoluteFilePath());
        if ((!activePath.isEmpty()
             && path.compare(activePath, Qt::CaseInsensitive) == 0)
            || file.lastModified().toUTC() >= cutoff) {
            continue;
        }
        if (QFile::remove(path)) {
            ++deleted;
        } else {
            ++failures;
            log(MaintenanceLogLevel::Warning,
                QStringLiteral("Expired log could not be removed: %1").arg(path));
        }
    }
    log(MaintenanceLogLevel::Info,
        QStringLiteral("Log retention: deleted=%1 failures=%2 retention_days=%3")
            .arg(deleted).arg(failures).arg(retentionDays));
}

AttachmentMaintenanceSummary MaintenanceManager::maintainAttachments()
{
    AttachmentMaintenanceSummary summary;
    QList<MaintenanceAttachmentReference> references;
    QString errorMessage;
    if (!m_dao.attachmentReferences(&references, &errorMessage)) {
        log(MaintenanceLogLevel::Error,
            QStringLiteral("Attachment references could not be read: %1")
                .arg(errorMessage));
        ++summary.failures;
        return summary;
    }

    QSet<QString> referencedPaths;
    QHash<QString, QList<qint64>> referenceOwners;
    QSet<QString> existingOriginalPaths;
    QHash<QString, int> derivedOriginalCounts;
    for (const MaintenanceAttachmentReference &reference : references) {
        if (!reference.originalPath.isEmpty()) {
            const QString path = safeUploadPath(reference.originalPath);
            if (!path.isEmpty()) {
                existingOriginalPaths.insert(path);
            }
        } else if (reference.storagePath.endsWith(
                       QStringLiteral(".webp"), Qt::CaseInsensitive)) {
            const QString candidate = reference.storagePath.left(
                reference.storagePath.size() - 5)
                + QStringLiteral("_original.bin");
            const QString path = safeUploadPath(candidate);
            if (!path.isEmpty()) {
                derivedOriginalCounts[path] += 1;
            }
        }
    }

    for (MaintenanceAttachmentReference &reference : references) {
        if (reference.originalPath.isEmpty()
            && reference.storagePath.endsWith(
                QStringLiteral(".webp"), Qt::CaseInsensitive)) {
            const QString candidate = reference.storagePath.left(
                reference.storagePath.size() - 5)
                + QStringLiteral("_original.bin");
            const QString candidatePath = safeUploadPath(candidate);
            const QFileInfo candidateInfo(candidatePath);
            if (!candidatePath.isEmpty() && candidateInfo.exists()
                && candidateInfo.isFile() && !candidateInfo.isSymLink()) {
                if (derivedOriginalCounts.value(candidatePath) == 1
                    && !existingOriginalPaths.contains(candidatePath)) {
                    errorMessage.clear();
                    if (m_dao.updateOriginalPath(reference.id, candidate,
                                                 &errorMessage)) {
                        reference.originalPath = candidate;
                        existingOriginalPaths.insert(candidatePath);
                        ++summary.backfilledOriginals;
                    } else {
                        ++summary.failures;
                        log(MaintenanceLogLevel::Warning,
                            QStringLiteral("Original path backfill failed for attachment %1: %2")
                                .arg(reference.id).arg(errorMessage));
                    }
                } else {
                    referencedPaths.insert(candidatePath);
                    referenceOwners[candidatePath].append(reference.id);
                    log(MaintenanceLogLevel::Warning,
                        QStringLiteral("Ambiguous original file retained for attachment %1: %2")
                            .arg(reference.id).arg(candidate));
                }
            }
        }

        for (const QString &relativePath : {reference.storagePath,
                                            reference.thumbnailPath,
                                            reference.originalPath}) {
            if (relativePath.isEmpty()) {
                continue;
            }
            const QString path = safeUploadPath(relativePath);
            if (path.isEmpty()) {
                ++summary.failures;
                log(MaintenanceLogLevel::Warning,
                    QStringLiteral("Rejected attachment path for id %1: %2")
                        .arg(reference.id).arg(relativePath));
                continue;
            }
            referencedPaths.insert(path);
            referenceOwners[path].append(reference.id);
        }
    }

    QDir uploadDirectory(m_uploadRoot);
    if (!uploadDirectory.exists()) {
        return summary;
    }
    const QDateTime now = nowUtc();
    QStringList directories;
    QDirIterator iterator(m_uploadRoot,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isDir()) {
            if (!info.isSymLink()) {
                directories.append(info.absoluteFilePath());
            }
            continue;
        }
        ++summary.scanned;
        if (!info.isFile() || info.isSymLink()) {
            ++summary.failures;
            continue;
        }

        const QString path = QDir::fromNativeSeparators(info.absoluteFilePath());
        const int markerIndex = path.lastIndexOf(DeletingMarker);
        if (markerIndex >= 0) {
            const QString original = path.left(markerIndex);
            const bool referenced = referencedPaths.contains(original);
            if (referenced && !QFile::exists(original)) {
                if (QFile::rename(path, original)) {
                    ++summary.restored;
                } else {
                    ++summary.failures;
                }
            } else if (isOlderThan(info, now, StagedFileRetentionSeconds)) {
                if (referenced) {
                    log(MaintenanceLogLevel::Warning,
                        QStringLiteral("Referenced file and staged deletion copy both exist: %1")
                            .arg(original));
                }
                if (QFile::remove(path)) {
                    ++summary.deleted;
                } else {
                    ++summary.failures;
                }
            } else {
                if (referenced) {
                    log(MaintenanceLogLevel::Warning,
                        QStringLiteral("Referenced file and staged deletion copy both exist: %1")
                            .arg(original));
                }
                ++summary.candidates;
            }
            continue;
        }

        if (referencedPaths.contains(path)) {
            continue;
        }
        if (isOlderThan(info, now, OrphanRetentionSeconds)) {
            if (QFile::remove(path)) {
                ++summary.deleted;
            } else {
                ++summary.failures;
            }
        } else {
            ++summary.candidates;
        }
    }

    for (const QString &path : std::as_const(referencedPaths)) {
        if (!QFileInfo::exists(path)) {
            ++summary.missingReferences;
            QStringList attachmentIds;
            for (qint64 id : referenceOwners.value(path)) {
                attachmentIds.append(QString::number(id));
            }
            log(MaintenanceLogLevel::Warning,
                QStringLiteral("Referenced attachment file is missing: ids=%1 path=%2")
                    .arg(attachmentIds.join(QLatin1Char(',')), path));
        }
    }

    std::sort(directories.begin(), directories.end(),
              [](const QString &left, const QString &right) {
                  return left.size() > right.size();
              });
    for (const QString &directory : std::as_const(directories)) {
        QDir parent = QFileInfo(directory).dir();
        parent.rmdir(QFileInfo(directory).fileName());
    }
    return summary;
}

bool MaintenanceManager::isVacuumDue()
{
    std::optional<QDateTime> lastVacuum;
    QString errorMessage;
    if (!m_dao.lastVacuumUtc(&lastVacuum, &errorMessage)) {
        log(MaintenanceLogLevel::Warning,
            QStringLiteral("VACUUM state could not be read: %1").arg(errorMessage));
        return false;
    }
    return !lastVacuum
        || lastVacuum->secsTo(nowUtc()) >= VacuumIntervalSeconds;
}

void MaintenanceManager::runVacuumIfSafe(bool serverRunning)
{
    if (!m_vacuumPending && !isVacuumDue()) {
        return;
    }
    if (serverRunning) {
        // HTTP handlers and maintenance share this thread and SQLite connection,
        // so the listener can stay bound while request handling briefly pauses.
        log(MaintenanceLogLevel::Info,
            QStringLiteral("VACUUM is due. HTTP request handling will pause "
                           "until maintenance completes."));
    }

    QString errorMessage;
    if (!m_dao.vacuum(&errorMessage)) {
        m_vacuumPending = true;
        log(MaintenanceLogLevel::Warning,
            QStringLiteral("VACUUM failed: %1").arg(errorMessage));
        return;
    }
    if (!m_dao.setLastVacuumUtc(nowUtc(), &errorMessage)) {
        m_vacuumPending = true;
        log(MaintenanceLogLevel::Warning,
            QStringLiteral("VACUUM completed but its timestamp was not saved: %1")
                .arg(errorMessage));
        return;
    }
    m_vacuumPending = false;
    log(MaintenanceLogLevel::Info, QStringLiteral("VACUUM completed."));
}

QString MaintenanceManager::safeUploadPath(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return {};
    }
    const QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(relativePath));
    if (QDir::isAbsolutePath(normalized)
        || normalized == QStringLiteral("..")
        || normalized.startsWith(QStringLiteral("../"))) {
        return {};
    }
    const QString root = withTrailingSlash(QDir(m_uploadRoot).absolutePath());
    const QString candidate = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(m_uploadRoot).absoluteFilePath(normalized)));
    if (!candidate.startsWith(root, Qt::CaseInsensitive)) {
        return {};
    }
    const QFileInfo info(candidate);
    if (info.exists()) {
        const QString canonicalRoot = withTrailingSlash(
            QFileInfo(m_uploadRoot).canonicalFilePath());
        const QString canonicalCandidate = QDir::fromNativeSeparators(
            info.canonicalFilePath());
        if (info.isSymLink() || canonicalRoot.isEmpty()
            || !canonicalCandidate.startsWith(canonicalRoot,
                                              Qt::CaseInsensitive)) {
            return {};
        }
    }
    return candidate;
}

void MaintenanceManager::log(MaintenanceLogLevel level,
                             const QString &message) const
{
    if (m_logHandler) {
        m_logHandler(level, message);
    }
}

QDateTime MaintenanceManager::nowUtc() const
{
    return (m_nowProvider ? m_nowProvider() : QDateTime::currentDateTimeUtc())
        .toUTC();
}
