#include "comments/commentdao.h"

#include "databasemanager.h"

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace {
CommentDaoResult success()
{
    return {};
}

CommentDaoResult failure(const QSqlError &error, bool allowConflict)
{
    const QString nativeCode = error.nativeErrorCode();
    const QString detail = error.text();
    const bool conflict = allowConflict
        && (nativeCode == QStringLiteral("19")
            || detail.contains(QStringLiteral("constraint"),
                               Qt::CaseInsensitive));
    return {
        conflict ? CommentDaoError::Conflict : CommentDaoError::Database,
        detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail
    };
}

QString commentProjection()
{
    return QStringLiteral(
        "SELECT c.id, c.issue_id, c.user_id, c.content, c.created_at, "
        "u.username, COALESCE(u.display_name, ''), "
        "COALESCE(c.deleted_at, ''), c.deleted_by_id, "
        "COALESCE(c.deleted_by_name, '') "
        "FROM comments c JOIN users u ON u.id = c.user_id ");
}
}

QJsonObject CommentUserReference::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("username"), username},
        {QStringLiteral("display_name"), displayName}
    };
}

QJsonObject CommentRecord::toJson() const
{
    QJsonArray attachmentArray;
    if (deletedAt.isEmpty()) {
        for (const AttachmentRecord &attachment : attachments) {
            attachmentArray.append(attachment.toJson());
        }
    }
    QJsonObject object = {
        {QStringLiteral("id"), id},
        {QStringLiteral("issue_id"), issueId},
        {QStringLiteral("user_id"), userId},
        {QStringLiteral("content"), deletedAt.isEmpty() ? content : QString()},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("user"), user.toJson()},
        {QStringLiteral("attachments"), attachmentArray},
        {QStringLiteral("is_deleted"), !deletedAt.isEmpty()},
        {QStringLiteral("deleted_at"), deletedAt.isEmpty()
             ? QJsonValue(QJsonValue::Null) : QJsonValue(deletedAt)}
    };
    if (deletedAt.isEmpty()) {
        object.insert(QStringLiteral("deleted_by"), QJsonValue(QJsonValue::Null));
    } else {
        QJsonObject deletedBy {
            {QStringLiteral("id"), deletedById
                 ? QJsonValue(*deletedById) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("display_name"), deletedByName}
        };
        object.insert(QStringLiteral("deleted_by"), deletedBy);
    }
    return object;
}

bool CommentDaoResult::ok() const
{
    return error == CommentDaoError::None;
}

CommentDao::CommentDao(DatabaseManager &database)
    : m_database(database)
{
}

CommentDaoResult CommentDao::comments(
    qint64 issueId, QList<CommentRecord> *comments) const
{
    if (comments) {
        comments->clear();
    }

    QSqlQuery query(m_database.connection());
    query.prepare(commentProjection()
                  + QStringLiteral(
                      "WHERE c.issue_id = ? "
                      "ORDER BY c.created_at ASC, c.id ASC"));
    query.addBindValue(issueId);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (comments) {
        while (query.next()) {
            CommentRecord comment = readComment(query);
            const CommentDaoResult attachmentResult = loadAttachments(&comment);
            if (!attachmentResult.ok()) {
                return attachmentResult;
            }
            comments->append(comment);
        }
    }
    return success();
}

CommentDaoResult CommentDao::issueExists(qint64 issueId, bool *exists) const
{
    if (exists) {
        *exists = false;
    }

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("SELECT 1 FROM issues WHERE id = ?"));
    query.addBindValue(issueId);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (exists) {
        *exists = query.next();
    }
    return success();
}

CommentDaoResult CommentDao::create(qint64 issueId,
                                    qint64 userId,
                                    const QString &content,
                                    CommentRecord *createdComment) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO comments(issue_id, user_id, content) VALUES(?, ?, ?)"));
    query.addBindValue(issueId);
    query.addBindValue(userId);
    query.addBindValue(content);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }

    bool found = false;
    const CommentDaoResult readResult = commentById(
        query.lastInsertId().toLongLong(), createdComment, &found);
    if (!readResult.ok()) {
        return readResult;
    }
    if (!found) {
        return {CommentDaoError::Database,
                QStringLiteral("The created comment could not be read back.")};
    }
    return success();
}

CommentDaoResult CommentDao::createWithAttachments(
    qint64 issueId,
    qint64 userId,
    const QString &content,
    const QList<AttachmentRecord> &attachments,
    CommentRecord *createdComment) const
{
    QSqlDatabase database = m_database.connection();
    if (!database.transaction()) {
        return failure(database.lastError(), false);
    }
    const auto rollback = [&database](const QSqlError &error) {
        database.rollback();
        return failure(error, true);
    };

    QSqlQuery commentQuery(database);
    commentQuery.prepare(QStringLiteral(
        "INSERT INTO comments(issue_id, user_id, content) VALUES(?, ?, '')"));
    commentQuery.addBindValue(issueId);
    commentQuery.addBindValue(userId);
    if (!commentQuery.exec()) {
        return rollback(commentQuery.lastError());
    }
    const qint64 commentId = commentQuery.lastInsertId().toLongLong();
    QString storedContent = content;
    QSqlQuery attachmentQuery(database);
    attachmentQuery.prepare(QStringLiteral(
        "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
        "content_type, storage_path, thumb_path, original_path, file_size) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (qsizetype index = 0; index < attachments.size(); ++index) {
        const AttachmentRecord &attachment = attachments.at(index);
        attachmentQuery.clear();
        attachmentQuery.prepare(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
            "content_type, storage_path, thumb_path, original_path, file_size) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        attachmentQuery.addBindValue(issueId);
        attachmentQuery.addBindValue(commentId);
        attachmentQuery.addBindValue(userId);
        attachmentQuery.addBindValue(attachment.filename);
        attachmentQuery.addBindValue(attachment.contentType);
        attachmentQuery.addBindValue(attachment.storagePath);
        attachmentQuery.addBindValue(attachment.thumbnailPath);
        attachmentQuery.addBindValue(attachment.originalPath);
        attachmentQuery.addBindValue(attachment.fileSize);
        if (!attachmentQuery.exec()) {
            return rollback(attachmentQuery.lastError());
        }
        storedContent.replace(
            QStringLiteral("upload:%1").arg(index),
            QStringLiteral("attachment:%1")
                .arg(attachmentQuery.lastInsertId().toLongLong()));
        attachmentQuery.finish();
    }
    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE comments SET content = ? WHERE id = ?"));
    updateQuery.addBindValue(storedContent);
    updateQuery.addBindValue(commentId);
    if (!updateQuery.exec()) {
        return rollback(updateQuery.lastError());
    }
    bool found = false;
    const CommentDaoResult readResult = commentById(
        commentId, createdComment, &found);
    if (!readResult.ok()) {
        database.rollback();
        return readResult;
    }
    if (!found || !createdComment) {
        database.rollback();
        return {CommentDaoError::Database,
                QStringLiteral("The created comment could not be read back.")};
    }
    if (!database.commit()) {
        const QSqlError error = database.lastError();
        database.rollback();
        return failure(error, false);
    }
    return success();
}

CommentDaoResult CommentDao::updateWithAttachments(
    qint64 id,
    const QString &content,
    const QList<AttachmentRecord> &attachments,
    const QList<qint64> &removeAttachmentIds,
    CommentRecord *updatedComment) const
{
    QSqlDatabase database = m_database.connection();
    if (!database.transaction()) {
        return failure(database.lastError(), false);
    }
    CommentDaoResult result;
    QSqlQuery removeQuery(database);
    for (qint64 attachmentId : removeAttachmentIds) {
        removeQuery.prepare(QStringLiteral(
            "DELETE FROM attachments WHERE id = ? AND comment_id = ?"));
        removeQuery.addBindValue(attachmentId);
        removeQuery.addBindValue(id);
        if (!removeQuery.exec()) {
            result = failure(removeQuery.lastError(), true);
            break;
        }
        if (removeQuery.numRowsAffected() != 1) {
            result = {CommentDaoError::Conflict,
                      QStringLiteral("Comment attachment could not be removed.")};
            break;
        }
        removeQuery.finish();
    }

    QString storedContent = content;
    QSqlQuery attachmentQuery(database);
    for (qsizetype index = 0; result.ok() && index < attachments.size(); ++index) {
        const AttachmentRecord &attachment = attachments.at(index);
        attachmentQuery.prepare(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
            "content_type, storage_path, thumb_path, original_path, file_size) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        attachmentQuery.addBindValue(attachment.issueId);
        attachmentQuery.addBindValue(id);
        attachmentQuery.addBindValue(attachment.uploaderId);
        attachmentQuery.addBindValue(attachment.filename);
        attachmentQuery.addBindValue(attachment.contentType);
        attachmentQuery.addBindValue(attachment.storagePath);
        attachmentQuery.addBindValue(attachment.thumbnailPath);
        attachmentQuery.addBindValue(attachment.originalPath);
        attachmentQuery.addBindValue(attachment.fileSize);
        if (!attachmentQuery.exec()) {
            result = failure(attachmentQuery.lastError(), true);
            break;
        }
        storedContent.replace(
            QStringLiteral("upload:%1").arg(index),
            QStringLiteral("attachment:%1")
                .arg(attachmentQuery.lastInsertId().toLongLong()));
        attachmentQuery.finish();
    }

    if (result.ok()) {
        QSqlQuery updateQuery(database);
        updateQuery.prepare(QStringLiteral(
            "UPDATE comments SET content = ? WHERE id = ? AND deleted_at IS NULL"));
        updateQuery.addBindValue(storedContent);
        updateQuery.addBindValue(id);
        if (!updateQuery.exec()) {
            result = failure(updateQuery.lastError(), true);
        } else if (updateQuery.numRowsAffected() != 1) {
            result = {CommentDaoError::Conflict,
                      QStringLiteral("Comment could not be updated.")};
        }
    }

    bool found = false;
    if (result.ok()) {
        result = commentById(id, updatedComment, &found);
    }
    if (!result.ok() || !found) {
        database.rollback();
        return result.ok()
            ? CommentDaoResult{CommentDaoError::Database,
                               QStringLiteral("The updated comment could not be read back.")}
            : result;
    }
    if (!database.commit()) {
        const QSqlError error = database.lastError();
        database.rollback();
        return failure(error, false);
    }
    return success();
}

CommentDaoResult CommentDao::softDelete(qint64 id,
                                        qint64 deletedById,
                                        const QString &deletedByName,
                                        CommentRecord *deletedComment) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "UPDATE comments SET deleted_at = CURRENT_TIMESTAMP, deleted_by_id = ?, "
        "deleted_by_name = ? WHERE id = ? AND deleted_at IS NULL"));
    query.addBindValue(deletedById);
    query.addBindValue(deletedByName);
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }
    if (query.numRowsAffected() != 1) {
        return {CommentDaoError::Conflict,
                QStringLiteral("Comment could not be deleted.")};
    }
    bool found = false;
    const CommentDaoResult result = commentById(id, deletedComment, &found);
    if (!result.ok()) {
        return result;
    }
    return found ? success()
                 : CommentDaoResult{CommentDaoError::Database,
                                    QStringLiteral("The deleted comment could not be read back.")};
}

CommentDaoResult CommentDao::commentById(qint64 id,
                                         CommentRecord *comment,
                                         bool *found) const
{
    if (found) {
        *found = false;
    }

    QSqlQuery query(m_database.connection());
    query.prepare(commentProjection() + QStringLiteral("WHERE c.id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (query.next()) {
        if (comment) {
            *comment = readComment(query);
            const CommentDaoResult result = loadAttachments(comment);
            if (!result.ok()) {
                return result;
            }
        }
        if (found) {
            *found = true;
        }
    }
    return success();
}

CommentDaoResult CommentDao::loadAttachments(CommentRecord *comment) const
{
    if (!comment) {
        return success();
    }
    comment->attachments.clear();
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT id, issue_id, comment_id, uploader_id, filename, content_type, storage_path, "
        "COALESCE(thumb_path, ''), COALESCE(original_path, ''), "
        "COALESCE(file_size, 0), created_at "
        "FROM attachments WHERE comment_id = ? ORDER BY id ASC"));
    query.addBindValue(comment->id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    while (query.next()) {
        AttachmentRecord attachment;
        attachment.id = query.value(0).toLongLong();
        attachment.issueId = query.value(1).toLongLong();
        attachment.commentId = query.value(2).toLongLong();
        attachment.uploaderId = query.value(3).toLongLong();
        attachment.filename = query.value(4).toString();
        attachment.contentType = query.value(5).toString();
        attachment.storagePath = query.value(6).toString();
        attachment.thumbnailPath = query.value(7).toString();
        attachment.originalPath = query.value(8).toString();
        attachment.fileSize = query.value(9).toLongLong();
        attachment.createdAt = query.value(10).toString();
        attachment.image = attachment.contentType.startsWith(QStringLiteral("image/"));
        comment->attachments.append(attachment);
    }
    return success();
}

CommentRecord CommentDao::readComment(const QSqlQuery &query)
{
    CommentRecord comment;
    comment.id = query.value(0).toLongLong();
    comment.issueId = query.value(1).toLongLong();
    comment.userId = query.value(2).toLongLong();
    comment.content = query.value(3).toString();
    comment.createdAt = query.value(4).toString();
    comment.user = {
        comment.userId,
        query.value(5).toString(),
        query.value(6).toString()
    };
    comment.deletedAt = query.value(7).toString();
    if (!query.value(8).isNull()) {
        comment.deletedById = query.value(8).toLongLong();
    }
    comment.deletedByName = query.value(9).toString();
    return comment;
}
