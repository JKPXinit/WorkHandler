#ifndef ISSUESERVICE_H
#define ISSUESERVICE_H

#include "api/multipartparser.h"
#include "issues/issuedao.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

class AttachmentService;
class NotificationManager;
struct UserRecord;

struct IssueListInput
{
    std::optional<QString> blockId;
    std::optional<QString> status;
    std::optional<QString> priority;
    std::optional<QString> assigneeId;
    std::optional<QString> search;
    std::optional<QString> sort;
    bool requireBlock {false};
};

enum class IssueServiceError {
    None,
    InvalidInput,
    NotFound,
    Forbidden,
    Conflict,
    Database,
    Storage
};

struct IssueServiceResult
{
    IssueServiceError error {IssueServiceError::None};
    QString code;
    QString message;
    QList<IssueRecord> issues;
    QList<AttachmentRecord> attachments;
    IssueRecord issue;
    qint64 deletedId {0};

    bool ok() const;
};

class IssueService
{
public:
    IssueService(IssueDao &dao,
                 AttachmentService &attachmentService,
                 NotificationManager &notificationManager);

    IssueServiceResult list(const IssueListInput &input) const;
    IssueServiceResult get(qint64 id) const;
    IssueServiceResult create(const QJsonObject &values,
                              const UserRecord &currentUser);
    IssueServiceResult createWithAttachments(
        const QJsonObject &values,
        const QList<MultipartFile> &files,
        const UserRecord &currentUser);
    IssueServiceResult update(qint64 id,
                              const QJsonObject &values,
                              const UserRecord &currentUser);
    IssueServiceResult updateWithAttachments(
        qint64 id,
        const QJsonObject &values,
        const QList<MultipartFile> &files,
        const QList<qint64> &removeAttachmentIds,
        const UserRecord &currentUser);
    IssueServiceResult descriptionAttachments(qint64 id) const;
    IssueServiceResult changeStatus(qint64 id,
                                    const QJsonObject &values,
                                    const UserRecord &currentUser);
    IssueServiceResult remove(qint64 id,
                              const UserRecord &currentUser);

private:
    IssueServiceResult validateBlock(qint64 id) const;
    IssueServiceResult validateAssignee(
        const std::optional<qint64> &id) const;
    static IssueServiceResult daoFailure(const IssueDaoResult &result);

    IssueDao &m_dao;
    AttachmentService &m_attachmentService;
    NotificationManager &m_notificationManager;
};

#endif // ISSUESERVICE_H
