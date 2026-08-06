#ifndef ATTACHMENTDAO_H
#define ATTACHMENTDAO_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class DatabaseManager;
class QSqlQuery;

struct AttachmentRecord
{
    qint64 id {0};
    qint64 issueId {0};
    qint64 commentId {0};
    qint64 uploaderId {0};
    QString filename;
    QString contentType;
    bool image {false};
    QString storagePath;
    QString thumbnailPath;
    QString originalPath;
    qint64 fileSize {0};
    QString createdAt;

    QJsonObject toJson() const;
    QStringList filePaths() const;
};

enum class AttachmentDaoError {
    None,
    Conflict,
    Database
};

struct AttachmentDaoResult
{
    AttachmentDaoError error {AttachmentDaoError::None};
    QString message;

    bool ok() const;
};

class AttachmentDao
{
public:
    explicit AttachmentDao(DatabaseManager &database);

    AttachmentDaoResult attachmentsForBlock(
        qint64 blockId, QList<AttachmentRecord> *attachments) const;
    AttachmentDaoResult attachments(qint64 issueId,
                                    QList<AttachmentRecord> *attachments) const;
    AttachmentDaoResult attachmentById(
        qint64 id, std::optional<AttachmentRecord> *attachment) const;

private:
    AttachmentDaoResult queryAttachments(
        const QString &statement,
        qint64 id,
        QList<AttachmentRecord> *attachments) const;
    static AttachmentRecord readAttachment(const QSqlQuery &query);

    DatabaseManager &m_database;
};

#endif // ATTACHMENTDAO_H
