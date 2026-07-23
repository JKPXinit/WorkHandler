#ifndef MAINTENANCEDAO_H
#define MAINTENANCEDAO_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class DatabaseManager;

struct MaintenanceAttachmentReference
{
    qint64 id {0};
    QString storagePath;
    QString thumbnailPath;
    QString originalPath;
};

class MaintenanceDao
{
public:
    explicit MaintenanceDao(DatabaseManager &database);

    bool quickCheck(QStringList *results, QString *errorMessage) const;
    bool lastVacuumUtc(std::optional<QDateTime> *value,
                       QString *errorMessage) const;
    bool setLastVacuumUtc(const QDateTime &value,
                          QString *errorMessage) const;
    bool vacuum(QString *errorMessage) const;
    bool attachmentReferences(
        QList<MaintenanceAttachmentReference> *references,
        QString *errorMessage) const;
    bool updateOriginalPath(qint64 attachmentId,
                            const QString &originalPath,
                            QString *errorMessage) const;

private:
    DatabaseManager &m_database;
};

#endif // MAINTENANCEDAO_H
