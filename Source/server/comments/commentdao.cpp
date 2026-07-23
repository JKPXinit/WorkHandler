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
        "u.username, COALESCE(u.display_name, '') "
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
    for (const AttachmentRecord &attachment : attachments) {
        attachmentArray.append(attachment.toJson());
    }
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("issue_id"), issueId},
        {QStringLiteral("user_id"), userId},
        {QStringLiteral("content"), content},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("user"), user.toJson()},
        {QStringLiteral("attachments"), attachmentArray}
    };
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
    return loadAttachments(createdComment);
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
        "storage_path, thumb_path, original_path, file_size) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
    for (qsizetype index = 0; index < attachments.size(); ++index) {
        const AttachmentRecord &attachment = attachments.at(index);
        attachmentQuery.clear();
        attachmentQuery.prepare(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
            "storage_path, thumb_path, original_path, file_size) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
        attachmentQuery.addBindValue(issueId);
        attachmentQuery.addBindValue(commentId);
        attachmentQuery.addBindValue(userId);
        attachmentQuery.addBindValue(attachment.filename);
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
    const CommentDaoResult attachmentResult = loadAttachments(createdComment);
    if (!attachmentResult.ok()) {
        database.rollback();
        return attachmentResult;
    }
    if (!database.commit()) {
        const QSqlError error = database.lastError();
        database.rollback();
        return failure(error, false);
    }
    return success();
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
        "SELECT id, issue_id, comment_id, uploader_id, filename, storage_path, "
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
        attachment.storagePath = query.value(5).toString();
        attachment.thumbnailPath = query.value(6).toString();
        attachment.originalPath = query.value(7).toString();
        attachment.fileSize = query.value(8).toLongLong();
        attachment.createdAt = query.value(9).toString();
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
    return comment;
}
