#include "issues/issueservice.h"

#include "attachments/attachmentservice.h"
#include "databasemanager.h"
#include "issues/issueidentifier.h"
#include "notifications/notificationmanager.h"

#include <QJsonValue>

#include <cmath>

namespace {
constexpr double MaximumJsonInteger = 9007199254740991.0;

IssueServiceResult invalid(const QString &code, const QString &message)
{
    return {IssueServiceError::InvalidInput, code, message};
}

IssueServiceResult notFound(const QString &code, const QString &message)
{
    return {IssueServiceError::NotFound, code, message};
}

IssueServiceResult forbidden(const QString &code, const QString &message)
{
    return {IssueServiceError::Forbidden, code, message};
}

bool validStatus(const QString &status)
{
    return status == QStringLiteral("open")
        || status == QStringLiteral("in_progress")
        || status == QStringLiteral("resolved")
        || status == QStringLiteral("closed");
}

bool validPriority(const QString &priority)
{
    return priority == QStringLiteral("low")
        || priority == QStringLiteral("medium")
        || priority == QStringLiteral("high");
}

bool readPositiveId(const QJsonValue &value, qint64 *id)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 1.0
        || number > MaximumJsonInteger || std::floor(number) != number) {
        return false;
    }
    if (id) {
        *id = qint64(number);
    }
    return true;
}

bool parsePositiveId(const QString &text, qint64 *id)
{
    if (text.isEmpty()) {
        return false;
    }
    bool ok = false;
    const qint64 parsed = text.toLongLong(&ok);
    if (!ok || parsed <= 0 || QString::number(parsed) != text) {
        return false;
    }
    if (id) {
        *id = parsed;
    }
    return true;
}

bool readTitle(const QJsonValue &value, QString *title)
{
    if (!value.isString()) {
        return false;
    }
    const QString normalized = value.toString().trimmed();
    if (normalized.isEmpty() || normalized.size() > 200) {
        return false;
    }
    if (title) {
        *title = normalized;
    }
    return true;
}

bool readDescription(const QJsonValue &value, QString *description)
{
    if (!value.isString() || value.toString().size() > 10000) {
        return false;
    }
    if (description) {
        *description = value.toString().trimmed();
    }
    return true;
}

bool readPriority(const QJsonValue &value, QString *priority)
{
    if (!value.isString() || !validPriority(value.toString())) {
        return false;
    }
    if (priority) {
        *priority = value.toString();
    }
    return true;
}

bool canCreate(const UserRecord &user)
{
    return user.role == QStringLiteral("admin")
        || user.role == QStringLiteral("user");
}

bool canEdit(const UserRecord &user, const IssueRecord &issue)
{
    return user.role == QStringLiteral("admin")
        || (user.role == QStringLiteral("user")
            && user.id == issue.reporterId);
}
}

bool IssueServiceResult::ok() const
{
    return error == IssueServiceError::None;
}

IssueService::IssueService(IssueDao &dao,
                           AttachmentService &attachmentService,
                           NotificationManager &notificationManager)
    : m_dao(dao)
    , m_attachmentService(attachmentService)
    , m_notificationManager(notificationManager)
{
}

IssueServiceResult IssueService::list(const IssueListInput &input) const
{
    IssueFilter filter;
    if (input.blockId) {
        qint64 blockId = 0;
        if (!parsePositiveId(*input.blockId, &blockId)) {
            return invalid(QStringLiteral("invalid_block_id"),
                           QStringLiteral("Block id must be a positive integer."));
        }
        filter.blockId = blockId;
    }
    if (input.status) {
        if (!validStatus(*input.status)) {
            return invalid(QStringLiteral("invalid_issue_status"),
                           QStringLiteral("Issue status is invalid."));
        }
        filter.status = *input.status;
    }
    if (input.priority) {
        if (!validPriority(*input.priority)) {
            return invalid(QStringLiteral("invalid_issue_priority"),
                           QStringLiteral("Issue priority is invalid."));
        }
        filter.priority = *input.priority;
    }
    if (input.assigneeId) {
        qint64 assigneeId = 0;
        if (!parsePositiveId(*input.assigneeId, &assigneeId)) {
            return invalid(QStringLiteral("invalid_assignee_id"),
                           QStringLiteral("Assignee id must be a positive integer."));
        }
        filter.assigneeId = assigneeId;
    }
    if (input.search) {
        const QString search = input.search->trimmed();
        if (search.size() > 200) {
            return invalid(QStringLiteral("invalid_issue_search"),
                           QStringLiteral("Issue search must not exceed 200 characters."));
        }
        qint64 issueId = 0;
        if (search.startsWith(QLatin1Char('T'), Qt::CaseInsensitive)
            && IssueIdentifier::parse(search, &issueId)) {
            filter.issueId = issueId;
        } else {
            filter.search = search;
        }
    }
    if (input.sort) {
        if (*input.sort == QStringLiteral("created_desc")) {
            filter.sort = IssueSort::CreatedDescending;
        } else if (*input.sort == QStringLiteral("created_asc")) {
            filter.sort = IssueSort::CreatedAscending;
        } else if (*input.sort == QStringLiteral("priority_desc")) {
            filter.sort = IssueSort::PriorityDescending;
        } else if (*input.sort == QStringLiteral("title_asc")) {
            filter.sort = IssueSort::TitleAscending;
        } else {
            return invalid(QStringLiteral("invalid_issue_sort"),
                           QStringLiteral("Issue sort mode is invalid."));
        }
    }

    if (input.requireBlock) {
        if (!filter.blockId) {
            return invalid(QStringLiteral("invalid_block_id"),
                           QStringLiteral("Block id is required."));
        }
        const IssueServiceResult blockResult = validateBlock(*filter.blockId);
        if (!blockResult.ok()) {
            return blockResult;
        }
    }

    IssueServiceResult result;
    const IssueDaoResult daoResult = m_dao.issues(filter, &result.issues);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

IssueServiceResult IssueService::get(qint64 id) const
{
    std::optional<IssueRecord> issue;
    const IssueDaoResult daoResult = m_dao.issueById(id, &issue);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    if (!issue) {
        return notFound(QStringLiteral("issue_not_found"),
                        QStringLiteral("Issue was not found."));
    }
    IssueServiceResult result;
    result.issue = *issue;
    return result;
}

IssueServiceResult IssueService::create(
    const QJsonObject &values, const UserRecord &currentUser)
{
    if (!canCreate(currentUser)) {
        return forbidden(QStringLiteral("issue_write_forbidden"),
                         QStringLiteral("Issue creation requires write permission."));
    }
    if (values.contains(QStringLiteral("status"))
        || values.contains(QStringLiteral("reporter_id"))
        || values.contains(QStringLiteral("created_at"))) {
        return invalid(QStringLiteral("immutable_issue_fields"),
                       QStringLiteral("Issue status and ownership use dedicated rules."));
    }

    IssueRecord issue;
    if (!values.contains(QStringLiteral("block_id"))
        || !readPositiveId(values.value(QStringLiteral("block_id")),
                           &issue.blockId)) {
        return invalid(QStringLiteral("invalid_block_id"),
                       QStringLiteral("Block id must be a positive integer."));
    }
    if (!values.contains(QStringLiteral("title"))
        || !readTitle(values.value(QStringLiteral("title")), &issue.title)) {
        return invalid(QStringLiteral("invalid_issue_title"),
                       QStringLiteral("Issue title must contain 1 to 200 characters."));
    }

    issue.description = QString();
    if (values.contains(QStringLiteral("description"))
        && !readDescription(values.value(QStringLiteral("description")),
                            &issue.description)) {
        return invalid(QStringLiteral("invalid_issue_description"),
                       QStringLiteral("Issue description must not exceed 10000 characters."));
    }

    issue.priority = QStringLiteral("medium");
    if (values.contains(QStringLiteral("priority"))
        && !readPriority(values.value(QStringLiteral("priority")),
                         &issue.priority)) {
        return invalid(QStringLiteral("invalid_issue_priority"),
                       QStringLiteral("Issue priority is invalid."));
    }

    if (values.contains(QStringLiteral("assignee_id"))
        && !values.value(QStringLiteral("assignee_id")).isNull()) {
        qint64 assigneeId = 0;
        if (!readPositiveId(values.value(QStringLiteral("assignee_id")),
                            &assigneeId)) {
            return invalid(QStringLiteral("invalid_assignee_id"),
                           QStringLiteral("Assignee id must be null or a positive integer."));
        }
        issue.assigneeId = assigneeId;
    }

    const IssueServiceResult blockResult = validateBlock(issue.blockId);
    if (!blockResult.ok()) {
        return blockResult;
    }
    const IssueServiceResult assigneeResult = validateAssignee(issue.assigneeId);
    if (!assigneeResult.ok()) {
        return assigneeResult;
    }

    issue.status = QStringLiteral("open");
    issue.reporterId = currentUser.id;
    IssueServiceResult result;
    const IssueDaoResult daoResult = m_dao.create(issue, &result.issue);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    m_notificationManager.issueCreated(result.issue, currentUser);
    return result;
}

IssueServiceResult IssueService::update(
    qint64 id, const QJsonObject &values, const UserRecord &currentUser)
{
    IssueServiceResult existingResult = get(id);
    if (!existingResult.ok()) {
        return existingResult;
    }
    if (!canEdit(currentUser, existingResult.issue)) {
        return forbidden(QStringLiteral("issue_edit_forbidden"),
                         QStringLiteral("Only the reporter or an administrator can edit this issue."));
    }
    if (values.contains(QStringLiteral("status"))
        || values.contains(QStringLiteral("reporter_id"))
        || values.contains(QStringLiteral("created_at"))) {
        return invalid(QStringLiteral("immutable_issue_fields"),
                       QStringLiteral("Issue status and ownership use dedicated rules."));
    }

    IssueRecord issue = existingResult.issue;
    bool hasUpdate = false;
    if (values.contains(QStringLiteral("block_id"))) {
        hasUpdate = true;
        if (!readPositiveId(values.value(QStringLiteral("block_id")),
                            &issue.blockId)) {
            return invalid(QStringLiteral("invalid_block_id"),
                           QStringLiteral("Block id must be a positive integer."));
        }
    }
    if (values.contains(QStringLiteral("title"))) {
        hasUpdate = true;
        if (!readTitle(values.value(QStringLiteral("title")), &issue.title)) {
            return invalid(QStringLiteral("invalid_issue_title"),
                           QStringLiteral("Issue title must contain 1 to 200 characters."));
        }
    }
    if (values.contains(QStringLiteral("description"))) {
        hasUpdate = true;
        if (!readDescription(values.value(QStringLiteral("description")),
                             &issue.description)) {
            return invalid(QStringLiteral("invalid_issue_description"),
                           QStringLiteral("Issue description must not exceed 10000 characters."));
        }
    }
    if (values.contains(QStringLiteral("priority"))) {
        hasUpdate = true;
        if (!readPriority(values.value(QStringLiteral("priority")),
                          &issue.priority)) {
            return invalid(QStringLiteral("invalid_issue_priority"),
                           QStringLiteral("Issue priority is invalid."));
        }
    }
    if (values.contains(QStringLiteral("assignee_id"))) {
        hasUpdate = true;
        if (values.value(QStringLiteral("assignee_id")).isNull()) {
            issue.assigneeId.reset();
        } else {
            qint64 assigneeId = 0;
            if (!readPositiveId(values.value(QStringLiteral("assignee_id")),
                                &assigneeId)) {
                return invalid(QStringLiteral("invalid_assignee_id"),
                               QStringLiteral("Assignee id must be null or a positive integer."));
            }
            issue.assigneeId = assigneeId;
        }
    }
    if (!hasUpdate) {
        return invalid(QStringLiteral("missing_issue_fields"),
                       QStringLiteral("At least one editable issue field is required."));
    }

    const IssueServiceResult blockResult = validateBlock(issue.blockId);
    if (!blockResult.ok()) {
        return blockResult;
    }
    const IssueServiceResult assigneeResult = validateAssignee(issue.assigneeId);
    if (!assigneeResult.ok()) {
        return assigneeResult;
    }

    IssueServiceResult result;
    const IssueDaoResult daoResult = m_dao.update(issue, &result.issue);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    m_notificationManager.issueUpdated(existingResult.issue,
                                       result.issue,
                                       currentUser);
    return result;
}

IssueServiceResult IssueService::changeStatus(
    qint64 id, const QJsonObject &values, const UserRecord &currentUser)
{
    IssueServiceResult existingResult = get(id);
    if (!existingResult.ok()) {
        return existingResult;
    }
    if (!canEdit(currentUser, existingResult.issue)) {
        return forbidden(QStringLiteral("issue_status_forbidden"),
                         QStringLiteral("Only the reporter or an administrator can change this issue status."));
    }
    if (values.size() != 1 || !values.contains(QStringLiteral("status"))
        || !values.value(QStringLiteral("status")).isString()
        || !validStatus(values.value(QStringLiteral("status")).toString())) {
        return invalid(QStringLiteral("invalid_issue_status"),
                       QStringLiteral("Issue status is invalid."));
    }

    IssueServiceResult result;
    const IssueDaoResult daoResult = m_dao.updateStatus(
        id, values.value(QStringLiteral("status")).toString(), &result.issue);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    m_notificationManager.statusChanged(existingResult.issue,
                                        result.issue,
                                        currentUser);
    return result;
}

IssueServiceResult IssueService::remove(
    qint64 id, const UserRecord &currentUser)
{
    if (currentUser.role != QStringLiteral("admin")) {
        return forbidden(QStringLiteral("issue_delete_forbidden"),
                         QStringLiteral("Administrator permission is required to delete an issue."));
    }
    const IssueServiceResult existingResult = get(id);
    if (!existingResult.ok()) {
        return existingResult;
    }

    StagedFileRemoval staged;
    const AttachmentServiceResult stageResult =
        m_attachmentService.stageIssueRemoval(id, &staged);
    if (!stageResult.ok()) {
        return {IssueServiceError::Database,
                stageResult.code,
                stageResult.message};
    }

    bool removed = false;
    const IssueDaoResult daoResult = m_dao.remove(id, &removed);
    if (!daoResult.ok()) {
        m_attachmentService.rollbackRemoval(&staged);
        return daoFailure(daoResult);
    }
    if (!removed) {
        m_attachmentService.rollbackRemoval(&staged);
        return notFound(QStringLiteral("issue_not_found"),
                        QStringLiteral("Issue was not found."));
    }
    m_attachmentService.commitRemoval(&staged);
    m_notificationManager.notifyLocalAdminCountChanged();
    IssueServiceResult result;
    result.deletedId = id;
    return result;
}

IssueServiceResult IssueService::validateBlock(qint64 id) const
{
    bool exists = false;
    const IssueDaoResult daoResult = m_dao.blockExists(id, &exists);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    return exists
        ? IssueServiceResult()
        : notFound(QStringLiteral("block_not_found"),
                   QStringLiteral("Block was not found."));
}

IssueServiceResult IssueService::validateAssignee(
    const std::optional<qint64> &id) const
{
    if (!id) {
        return {};
    }
    bool exists = false;
    const IssueDaoResult daoResult = m_dao.userExists(*id, &exists);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    return exists
        ? IssueServiceResult()
        : notFound(QStringLiteral("assignee_not_found"),
                   QStringLiteral("Assignee was not found."));
}

IssueServiceResult IssueService::daoFailure(const IssueDaoResult &result)
{
    if (result.error == IssueDaoError::Conflict) {
        return {IssueServiceError::Conflict,
                QStringLiteral("issue_conflict"),
                result.message};
    }
    return {IssueServiceError::Database,
            QStringLiteral("database_error"),
            result.message.isEmpty()
                ? QStringLiteral("Database operation failed.")
                : result.message};
}
