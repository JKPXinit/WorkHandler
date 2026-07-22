#include "comments/commentdao.h"

#include "databasemanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("issue_id"), issueId},
        {QStringLiteral("user_id"), userId},
        {QStringLiteral("content"), content},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("user"), user.toJson()}
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
            comments->append(readComment(query));
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
