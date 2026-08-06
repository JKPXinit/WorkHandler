#ifndef COMMENTSERVICE_H
#define COMMENTSERVICE_H

#include "api/multipartparser.h"
#include "comments/commentdao.h"

#include <QJsonObject>
#include <QList>
#include <QString>

struct UserRecord;
class NotificationManager;

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
    CommentService(CommentDao &dao,
                   class AttachmentService &attachmentService,
                   NotificationManager &notificationManager);

    CommentServiceResult list(qint64 issueId) const;
    CommentServiceResult create(qint64 issueId,
                                const QJsonObject &values,
                                const UserRecord &currentUser);
    CommentServiceResult createWithAttachments(
        qint64 issueId,
        const QString &content,
        const QList<MultipartFile> &files,
        const UserRecord &currentUser);

private:
    CommentServiceResult validateIssue(qint64 issueId) const;
    static CommentServiceResult daoFailure(const CommentDaoResult &result);

    CommentDao &m_dao;
    AttachmentService &m_attachmentService;
    NotificationManager &m_notificationManager;
};

#endif // COMMENTSERVICE_H
