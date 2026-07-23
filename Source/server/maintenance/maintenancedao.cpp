#include "maintenance/maintenancedao.h"

#include "databasemanager.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message.isEmpty()
            ? QStringLiteral("Database operation failed.") : message;
    }
}
}

MaintenanceDao::MaintenanceDao(DatabaseManager &database)
    : m_database(database)
{
}

bool MaintenanceDao::quickCheck(QStringList *results,
                                QString *errorMessage) const
{
    if (results) {
        results->clear();
    }
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    while (query.next()) {
        if (results) {
            results->append(query.value(0).toString());
        }
    }
    return true;
}

bool MaintenanceDao::lastVacuumUtc(std::optional<QDateTime> *value,
                                   QString *errorMessage) const
{
    if (value) {
        value->reset();
    }
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT value FROM security_state "
        "WHERE key = 'maintenance.last_vacuum_utc'"));
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        return true;
    }
    QDateTime parsed = QDateTime::fromString(query.value(0).toString(),
                                             Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
    }
    if (value && parsed.isValid()) {
        *value = parsed.toUTC();
    }
    return true;
}

bool MaintenanceDao::setLastVacuumUtc(const QDateTime &value,
                                      QString *errorMessage) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO security_state(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(QStringLiteral("maintenance.last_vacuum_utc"));
    query.addBindValue(value.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    return true;
}

bool MaintenanceDao::vacuum(QString *errorMessage) const
{
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral("VACUUM"))) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    return true;
}

bool MaintenanceDao::attachmentReferences(
    QList<MaintenanceAttachmentReference> *references,
    QString *errorMessage) const
{
    if (references) {
        references->clear();
    }
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral(
            "SELECT id, storage_path, COALESCE(thumb_path, ''), "
            "COALESCE(original_path, '') FROM attachments ORDER BY id"))) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (references) {
        while (query.next()) {
            references->append({query.value(0).toLongLong(),
                                query.value(1).toString(),
                                query.value(2).toString(),
                                query.value(3).toString()});
        }
    }
    return true;
}

bool MaintenanceDao::updateOriginalPath(qint64 attachmentId,
                                        const QString &originalPath,
                                        QString *errorMessage) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "UPDATE attachments SET original_path = ? "
        "WHERE id = ? AND (original_path IS NULL OR original_path = '')"));
    query.addBindValue(originalPath);
    query.addBindValue(attachmentId);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    return true;
}
