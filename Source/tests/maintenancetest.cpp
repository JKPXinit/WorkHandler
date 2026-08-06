#include "databasemanager.h"
#include "maintenance/maintenancedao.h"
#include "maintenance/maintenancemanager.h"
#include "passwordhasher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace {
bool writeFileAt(const QString &path,
                 const QDateTime &modifiedUtc,
                 const QByteArray &data = QByteArrayLiteral("data"))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        return false;
    }
    const bool timestampSet = file.setFileTime(
        modifiedUtc, QFileDevice::FileModificationTime);
    file.close();
    return timestampSet;
}

qint64 seedAttachment(DatabaseManager &database,
                      const QString &storagePath,
                      const QString &thumbnailPath)
{
    QSqlQuery query(database.connection());
    if (!query.exec(QStringLiteral(
            "SELECT id FROM users WHERE username = 'admin'"))
        || !query.next()) {
        return 0;
    }
    const qint64 adminId = query.value(0).toLongLong();
    if (!query.exec(QStringLiteral(
            "INSERT INTO blocks(title) VALUES('Maintenance Block')"))) {
        return 0;
    }
    const qint64 blockId = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral(
        "INSERT INTO issues(block_id, title, reporter_id) VALUES(?, ?, ?)"));
    query.addBindValue(blockId);
    query.addBindValue(QStringLiteral("Maintenance Issue"));
    query.addBindValue(adminId);
    if (!query.exec()) {
        return 0;
    }
    const qint64 issueId = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral(
        "INSERT INTO comments(issue_id, user_id, content) VALUES(?, ?, ?)"));
    query.addBindValue(issueId);
    query.addBindValue(adminId);
    query.addBindValue(QStringLiteral("Maintenance comment"));
    if (!query.exec()) {
        return 0;
    }
    const qint64 commentId = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral(
        "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
        "storage_path, thumb_path) VALUES(?, ?, ?, ?, ?, ?)"));
    query.addBindValue(issueId);
    query.addBindValue(commentId);
    query.addBindValue(adminId);
    query.addBindValue(QStringLiteral("maintenance.png"));
    query.addBindValue(storagePath);
    query.addBindValue(thumbnailPath);
    return query.exec() ? query.lastInsertId().toLongLong() : 0;
}
}

class MaintenanceTest : public QObject
{
    Q_OBJECT

private slots:
    void legacyOriginalPathMigration();
    void attachmentRecoveryAndCleanup();
    void logRetentionAndVacuumWindow();
};

void MaintenanceTest::legacyOriginalPathMigration()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString databasePath = temporary.filePath(QStringLiteral("legacy.db"));
    const QString connectionName = QStringLiteral("legacy_original_path");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE attachments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, issue_id INTEGER NOT NULL, "
            "comment_id INTEGER NOT NULL, uploader_id INTEGER NOT NULL, "
            "filename TEXT NOT NULL, storage_path TEXT NOT NULL, "
            "thumb_path TEXT, file_size INTEGER, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
            "storage_path, thumb_path) VALUES(1, 1, 1, 'legacy.png', "
            "'2026-07/legacy.webp', '2026-07/legacy_thumb.webp')")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    DatabaseManager database(databasePath);
    QString errorMessage;
    QVERIFY2(database.initialize(&errorMessage), qPrintable(errorMessage));
    QSqlQuery query(database.connection());
    QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(attachments)")));
    bool found = false;
    bool contentTypeFound = false;
    while (query.next()) {
        found = found
            || query.value(1).toString() == QStringLiteral("original_path");
        contentTypeFound = contentTypeFound
            || query.value(1).toString() == QStringLiteral("content_type");
    }
    QVERIFY(found);
    QVERIFY(contentTypeFound);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT content_type FROM attachments WHERE filename = 'legacy.png'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("image/webp"));
}

void MaintenanceTest::attachmentRecoveryAndCleanup()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString databasePath = temporary.filePath(QStringLiteral("issue_panel.db"));
    const QString uploadRoot = temporary.filePath(QStringLiteral("uploads"));
    DatabaseManager database(databasePath);
    QString errorMessage;
    QVERIFY2(database.initialize(&errorMessage), qPrintable(errorMessage));

    const QString storage = QStringLiteral("2026-07/recover.webp");
    const QString thumbnail = QStringLiteral("2026-07/missing_thumb.webp");
    const qint64 attachmentId = seedAttachment(database, storage, thumbnail);
    QVERIFY(attachmentId > 0);
    QSqlQuery malicious(database.connection());
    malicious.prepare(QStringLiteral(
        "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
        "storage_path) SELECT issue_id, comment_id, uploader_id, ?, ? "
        "FROM attachments WHERE id = ?"));
    malicious.addBindValue(QStringLiteral("outside.bin"));
    malicious.addBindValue(QStringLiteral("../outside.bin"));
    malicious.addBindValue(attachmentId);
    QVERIFY(malicious.exec());
    QSqlQuery ordinary(database.connection());
    ordinary.prepare(QStringLiteral(
        "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
        "content_type, storage_path) SELECT issue_id, comment_id, uploader_id, "
        "'runtime.log', 'text/plain', ? FROM attachments WHERE id = ?"));
    ordinary.addBindValue(QStringLiteral("2026-07/runtime.log"));
    ordinary.addBindValue(attachmentId);
    QVERIFY(ordinary.exec());

    const QDateTime now = QDateTime::fromString(
        QStringLiteral("2026-07-23T12:00:00.000Z"), Qt::ISODateWithMs);
    const QString staged = QDir(uploadRoot).filePath(
        storage + QStringLiteral(".deleting-crash"));
    const QString original = QDir(uploadRoot).filePath(
        QStringLiteral("2026-07/recover_original.bin"));
    const QString referencedLog = QDir(uploadRoot).filePath(
        QStringLiteral("2026-07/runtime.log"));
    const QString oldOrphan = QDir(uploadRoot).filePath(
        QStringLiteral("2026-07/old-orphan.bin"));
    const QString recentOrphan = QDir(uploadRoot).filePath(
        QStringLiteral("2026-07/recent-orphan.bin"));
    const QString staleDeleting = QDir(uploadRoot).filePath(
        QStringLiteral("2026-07/unreferenced.webp.deleting-stale"));
    const QString outside = temporary.filePath(QStringLiteral("outside.bin"));
    QVERIFY(writeFileAt(staged, now.addDays(-2)));
    QVERIFY(writeFileAt(original, now.addDays(-10)));
    QVERIFY(writeFileAt(referencedLog, now.addDays(-20), QByteArrayLiteral("log data")));
    QVERIFY(writeFileAt(oldOrphan, now.addDays(-8)));
    QVERIFY(writeFileAt(recentOrphan, now.addDays(-6)));
    QVERIFY(writeFileAt(staleDeleting, now.addDays(-2)));
    QVERIFY(writeFileAt(outside, now.addDays(-20)));

    QStringList logs;
    MaintenanceDao dao(database);
    MaintenanceManager manager(
        dao, uploadRoot, []() { return MaintenanceConfig(); },
        []() { return false; },
        [&logs](MaintenanceLogLevel, const QString &message) {
            logs.append(message);
        }, nullptr, [now]() { return now; });
    manager.runStartupMaintenance();

    QVERIFY(QFileInfo::exists(QDir(uploadRoot).filePath(storage)));
    QVERIFY(QFileInfo::exists(original));
    QVERIFY(QFileInfo::exists(referencedLog));
    QVERIFY(!QFileInfo::exists(oldOrphan));
    QVERIFY(QFileInfo::exists(recentOrphan));
    QVERIFY(!QFileInfo::exists(staleDeleting));
    QVERIFY(QFileInfo::exists(outside));

    QSqlQuery query(database.connection());
    query.prepare(QStringLiteral(
        "SELECT original_path FROM attachments WHERE id = ?"));
    query.addBindValue(attachmentId);
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(),
             QStringLiteral("2026-07/recover_original.bin"));
    QVERIFY(std::any_of(logs.cbegin(), logs.cend(), [](const QString &message) {
        return message.contains(QStringLiteral("Rejected attachment path"));
    }));
    QVERIFY(std::any_of(logs.cbegin(), logs.cend(), [](const QString &message) {
        return message.contains(QStringLiteral("missing"), Qt::CaseInsensitive);
    }));
}

void MaintenanceTest::logRetentionAndVacuumWindow()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString databasePath = temporary.filePath(QStringLiteral("issue_panel.db"));
    const QString uploadRoot = temporary.filePath(QStringLiteral("uploads"));
    const QString logRoot = temporary.filePath(QStringLiteral("logs"));
    DatabaseManager database(databasePath);
    QString errorMessage;
    QVERIFY2(database.initialize(&errorMessage), qPrintable(errorMessage));

    const QDateTime now = QDateTime::fromString(
        QStringLiteral("2026-07-23T12:00:00.000Z"), Qt::ISODateWithMs);
    const QString activeLog = QDir(logRoot).filePath(QStringLiteral("active.log"));
    const QString expiredLog = QDir(logRoot).filePath(QStringLiteral("expired.log"));
    const QString boundaryLog = QDir(logRoot).filePath(QStringLiteral("boundary.log"));
    QVERIFY(writeFileAt(activeLog, now.addDays(-40)));
    QVERIFY(writeFileAt(expiredLog, now.addDays(-31)));
    QVERIFY(writeFileAt(boundaryLog, now.addDays(-30)));

    MaintenanceDao dao(database);
    const QDateTime oldVacuum = now.addDays(-31);
    QVERIFY2(dao.setLastVacuumUtc(oldVacuum, &errorMessage),
             qPrintable(errorMessage));
    bool serverRunning = true;
    MaintenanceConfig config;
    config.logFilePath = activeLog;
    config.activeLogFilePath = activeLog;
    config.logRetentionDays = 30;
    QStringList logs;
    MaintenanceManager manager(
        dao, uploadRoot, [config]() { return config; },
        [&serverRunning]() { return serverRunning; },
        [&logs](MaintenanceLogLevel, const QString &message) {
            logs.append(message);
        }, nullptr,
        [now]() { return now; });

    manager.runDailyMaintenance();
    QVERIFY(QFileInfo::exists(activeLog));
    QVERIFY(!QFileInfo::exists(expiredLog));
    QVERIFY(QFileInfo::exists(boundaryLog));
    std::optional<QDateTime> stored;
    QVERIFY2(dao.lastVacuumUtc(&stored, &errorMessage), qPrintable(errorMessage));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->toUTC(), now.toUTC());
    QVERIFY(std::any_of(logs.cbegin(), logs.cend(), [](const QString &message) {
        return message.contains(QStringLiteral("HTTP request handling will pause"));
    }));

    QVERIFY2(dao.setLastVacuumUtc(oldVacuum, &errorMessage),
             qPrintable(errorMessage));
    QSqlDatabase connection = database.connection();
    QVERIFY(connection.transaction());
    manager.runDailyMaintenance();
    stored.reset();
    QVERIFY2(dao.lastVacuumUtc(&stored, &errorMessage), qPrintable(errorMessage));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->toUTC(), oldVacuum.toUTC());
    QVERIFY(connection.rollback());

    manager.runDailyMaintenance();
    stored.reset();
    QVERIFY2(dao.lastVacuumUtc(&stored, &errorMessage), qPrintable(errorMessage));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->toUTC(), now.toUTC());
}

QTEST_GUILESS_MAIN(MaintenanceTest)

#include "maintenancetest.moc"
