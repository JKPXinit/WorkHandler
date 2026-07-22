#include "comments/commentservice.h"

#include "databasemanager.h"

namespace {
CommentServiceResult invalid(const QString &code, const QString &message)
{
    return {CommentServiceError::InvalidInput, code, message};
}

CommentServiceResult notFound()
{
    return {CommentServiceError::NotFound,
            QStringLiteral("issue_not_found"),
            QStringLiteral("Issue was not found.")};
}

CommentServiceResult forbidden()
{
    return {CommentServiceError::Forbidden,
            QStringLiteral("comment_write_forbidden"),
            QStringLiteral("Comment creation requires write permission.")};
}
}

bool CommentServiceResult::ok() const
{
    return error == CommentServiceError::None;
}

CommentService::CommentService(CommentDao &dao)
    : m_dao(dao)
{
}

CommentServiceResult CommentService::list(qint64 issueId) const
{
    const CommentServiceResult issueResult = validateIssue(issueId);
    if (!issueResult.ok()) {
        return issueResult;
    }

    CommentServiceResult result;
    const CommentDaoResult daoResult = m_dao.comments(issueId, &result.comments);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

CommentServiceResult CommentService::create(
    qint64 issueId,
    const QJsonObject &values,
    const UserRecord &currentUser) const
{
    if (currentUser.role != QStringLiteral("admin")
        && currentUser.role != QStringLiteral("user")) {
        return forbidden();
    }
    if (values.size() != 1 || !values.contains(QStringLiteral("content"))
        || !values.value(QStringLiteral("content")).isString()) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment content is required."));
    }
    const QString content = values.value(QStringLiteral("content"))
                                .toString().trimmed();
    if (content.isEmpty() || content.size() > 4000) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment content must contain 1 to 4000 characters."));
    }

    const CommentServiceResult issueResult = validateIssue(issueId);
    if (!issueResult.ok()) {
        return issueResult;
    }

    CommentServiceResult result;
    const CommentDaoResult daoResult = m_dao.create(
        issueId, currentUser.id, content, &result.comment);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

CommentServiceResult CommentService::validateIssue(qint64 issueId) const
{
    bool exists = false;
    const CommentDaoResult daoResult = m_dao.issueExists(issueId, &exists);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    return exists ? CommentServiceResult() : notFound();
}

CommentServiceResult CommentService::daoFailure(
    const CommentDaoResult &result)
{
    if (result.error == CommentDaoError::Conflict) {
        return {CommentServiceError::Conflict,
                QStringLiteral("comment_conflict"),
                result.message};
    }
    return {CommentServiceError::Database,
            QStringLiteral("database_error"),
            result.message.isEmpty()
                ? QStringLiteral("Database operation failed.")
                : result.message};
}
