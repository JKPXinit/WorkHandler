#include "attachments/attachmentdao.h"

#include "databasemanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
AttachmentDaoResult success()
{
    return {};
}

AttachmentDaoResult failure(const QSqlError &error, bool allowConflict)
{
    const QString nativeCode = error.nativeErrorCode();
    const QString detail = error.text();
    const bool conflict = allowConflict
        && (nativeCode == QStringLiteral("19")
            || detail.contains(QStringLiteral("constraint"),
                               Qt::CaseInsensitive));
    return {
        conflict ? AttachmentDaoError::Conflict : AttachmentDaoError::Database,
        detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail
    };
}

QString attachmentProjection()
{
    return QStringLiteral(
        "SELECT id, issue_id, comment_id, uploader_id, filename, content_type, storage_path, "
        "COALESCE(thumb_path, ''), COALESCE(original_path, ''), "
        "COALESCE(file_size, 0), created_at "
        "FROM attachments ");
}
}

QJsonObject AttachmentRecord::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("issue_id"), issueId},
        {QStringLiteral("comment_id"),
         commentId ? QJsonValue(*commentId) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("uploader_id"), uploaderId},
        {QStringLiteral("filename"), filename},
        {QStringLiteral("content_type"), contentType},
        {QStringLiteral("is_image"), image},
        {QStringLiteral("file_size"), fileSize},
        {QStringLiteral("created_at"), createdAt}
    };
}

QStringList AttachmentRecord::filePaths() const
{
    QStringList paths;
    for (const QString &path : {storagePath, thumbnailPath, originalPath}) {
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.append(path);
        }
    }
    return paths;
}

bool AttachmentDaoResult::ok() const
{
    return error == AttachmentDaoError::None;
}

AttachmentDao::AttachmentDao(DatabaseManager &database)
    : m_database(database)
{
}

AttachmentDaoResult AttachmentDao::attachments(
    qint64 issueId, QList<AttachmentRecord> *attachments) const
{
    return queryAttachments(
        attachmentProjection()
            + QStringLiteral(
                "WHERE issue_id = ? ORDER BY created_at ASC, id ASC"),
        issueId, attachments);
}

AttachmentDaoResult AttachmentDao::issueAttachments(
    qint64 issueId, QList<AttachmentRecord> *attachments) const
{
    return queryAttachments(
        attachmentProjection()
            + QStringLiteral(
                "WHERE issue_id = ? AND comment_id IS NULL "
                "ORDER BY created_at ASC, id ASC"),
        issueId, attachments);
}

AttachmentDaoResult AttachmentDao::attachmentsForBlock(
    qint64 blockId, QList<AttachmentRecord> *attachments) const
{
    return queryAttachments(
        QStringLiteral(
            "SELECT a.id, a.issue_id, a.comment_id, a.uploader_id, a.filename, "
            "a.content_type, a.storage_path, COALESCE(a.thumb_path, ''), "
            "COALESCE(a.original_path, ''), COALESCE(a.file_size, 0), "
            "a.created_at "
            "FROM attachments a JOIN issues i ON i.id = a.issue_id "
            "WHERE i.block_id = ? ORDER BY a.id ASC"),
        blockId, attachments);
}

AttachmentDaoResult AttachmentDao::attachmentById(
    qint64 id, std::optional<AttachmentRecord> *attachment) const
{
    if (attachment) {
        attachment->reset();
    }

    QSqlQuery query(m_database.connection());
    query.prepare(attachmentProjection() + QStringLiteral("WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (query.next() && attachment) {
        *attachment = readAttachment(query);
    }
    return success();
}

AttachmentDaoResult AttachmentDao::queryAttachments(
    const QString &statement,
    qint64 id,
    QList<AttachmentRecord> *attachments) const
{
    if (attachments) {
        attachments->clear();
    }
    QSqlQuery query(m_database.connection());
    query.prepare(statement);
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (attachments) {
        while (query.next()) {
            attachments->append(readAttachment(query));
        }
    }
    return success();
}

AttachmentRecord AttachmentDao::readAttachment(const QSqlQuery &query)
{
    AttachmentRecord attachment;
    attachment.id = query.value(0).toLongLong();
    attachment.issueId = query.value(1).toLongLong();
    if (!query.value(2).isNull()) {
        attachment.commentId = query.value(2).toLongLong();
    }
    attachment.uploaderId = query.value(3).toLongLong();
    attachment.filename = query.value(4).toString();
    attachment.contentType = query.value(5).toString();
    attachment.storagePath = query.value(6).toString();
    attachment.thumbnailPath = query.value(7).toString();
    attachment.originalPath = query.value(8).toString();
    attachment.fileSize = query.value(9).toLongLong();
    attachment.createdAt = query.value(10).toString();
    attachment.image = attachment.contentType.startsWith(QStringLiteral("image/"));
    return attachment;
}
