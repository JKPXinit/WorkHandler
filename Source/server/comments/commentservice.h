#ifndef COMMENTSERVICE_H
#define COMMENTSERVICE_H

#include "comments/commentdao.h"

#include <QJsonObject>
#include <QList>
#include <QString>

struct UserRecord;

enum class CommentServiceError {
    None,
    InvalidInput,
    NotFound,
    Forbidden,
    Conflict,
    Database
};

struct CommentServiceResult
{
    CommentServiceError error {CommentServiceError::None};
    QString code;
    QString message;
    QList<CommentRecord> comments;
    CommentRecord comment;

    bool ok() const;
};

class CommentService
{
public:
    explicit CommentService(CommentDao &dao);

    CommentServiceResult list(qint64 issueId) const;
    CommentServiceResult create(qint64 issueId,
                                const QJsonObject &values,
                                const UserRecord &currentUser) const;

private:
    CommentServiceResult validateIssue(qint64 issueId) const;
    static CommentServiceResult daoFailure(const CommentDaoResult &result);

    CommentDao &m_dao;
};

#endif // COMMENTSERVICE_H
