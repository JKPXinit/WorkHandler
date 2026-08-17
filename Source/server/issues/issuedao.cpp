#include "issues/issuedao.h"

#include "databasemanager.h"
#include "issues/issueidentifier.h"

#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {
IssueDaoResult success()
{
    return {};
}

IssueDaoResult failure(const QSqlError &error, bool allowConflict)
{
    const QString nativeCode = error.nativeErrorCode();
    const QString detail = error.text();
    const bool conflict = allowConflict
        && (nativeCode == QStringLiteral("19")
            || detail.contains(QStringLiteral("constraint"),
                               Qt::CaseInsensitive));
    return {
        conflict ? IssueDaoError::Conflict : IssueDaoError::Database,
        detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail
    };
}

IssueDaoResult insertIssueAttachments(
    QSqlDatabase database,
    qint64 issueId,
    const QList<AttachmentRecord> &attachments)
{
    QSqlQuery query(database);
    for (const AttachmentRecord &attachment : attachments) {
        query.prepare(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, "
            "content_type, storage_path, thumb_path, original_path, file_size) "
            "VALUES(?, NULL, ?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(issueId);
        query.addBindValue(attachment.uploaderId);
        query.addBindValue(attachment.filename);
        query.addBindValue(attachment.contentType);
        query.addBindValue(attachment.storagePath);
        query.addBindValue(attachment.thumbnailPath);
        query.addBindValue(attachment.originalPath);
        query.addBindValue(attachment.fileSize);
        if (!query.exec()) {
            return failure(query.lastError(), true);
        }
        query.finish();
    }
    return success();
}

QString issueProjection()
{
    return QStringLiteral(
        "SELECT i.id, i.block_id, i.title, COALESCE(i.description, ''), "
        "i.status, i.priority, i.reporter_id, i.assignee_id, i.created_at, "
        "reporter.username, COALESCE(reporter.display_name, ''), "
        "assignee.username, COALESCE(assignee.display_name, ''), "
        "(SELECT COUNT(*) FROM comments c WHERE c.issue_id = i.id), "
        "(SELECT COUNT(*) FROM attachments a WHERE a.issue_id = i.id) "
        "FROM issues i "
        "JOIN users reporter ON reporter.id = i.reporter_id "
        "LEFT JOIN users assignee ON assignee.id = i.assignee_id ");
}

QString orderBy(IssueSort sort)
{
    switch (sort) {
    case IssueSort::CreatedAscending:
        return QStringLiteral("ORDER BY i.created_at ASC, i.id ASC");
    case IssueSort::PriorityDescending:
        return QStringLiteral(
            "ORDER BY CASE i.priority "
            "WHEN 'high' THEN 3 WHEN 'medium' THEN 2 WHEN 'low' THEN 1 "
            "ELSE 0 END DESC, i.created_at DESC, i.id DESC");
    case IssueSort::TitleAscending:
        return QStringLiteral("ORDER BY i.title COLLATE NOCASE ASC, i.id ASC");
    case IssueSort::CreatedDescending:
        return QStringLiteral("ORDER BY i.created_at DESC, i.id DESC");
    }
    return QStringLiteral("ORDER BY i.created_at DESC, i.id DESC");
}
}

QJsonObject IssueUserReference::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("username"), username},
        {QStringLiteral("display_name"), displayName}
    };
}

QJsonObject IssueRecord::toJson() const
{
    QJsonObject object = {
        {QStringLiteral("id"), id},
        {QStringLiteral("task_id"), IssueIdentifier::format(id)},
        {QStringLiteral("block_id"), blockId},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), description},
        {QStringLiteral("status"), status},
        {QStringLiteral("priority"), priority},
        {QStringLiteral("reporter_id"), reporterId},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("reporter"), reporter.toJson()},
        {QStringLiteral("comment_count"), commentCount},
        {QStringLiteral("attachment_count"), attachmentCount}
    };
    object.insert(
        QStringLiteral("assignee_id"),
        assigneeId ? QJsonValue(*assigneeId) : QJsonValue(QJsonValue::Null));
    object.insert(
        QStringLiteral("assignee"),
        assignee ? QJsonValue(assignee->toJson()) : QJsonValue(QJsonValue::Null));
    return object;
}

bool IssueDaoResult::ok() const
{
    return error == IssueDaoError::None;
}

IssueDao::IssueDao(DatabaseManager &database)
    : m_database(database)
{
}

IssueDaoResult IssueDao::issues(const IssueFilter &filter,
                                QList<IssueRecord> *issues) const
{
    if (issues) {
        issues->clear();
    }

    QStringList conditions;
    QList<QVariant> bindings;
    if (filter.issueId) {
        conditions.append(QStringLiteral("i.id = ?"));
        bindings.append(*filter.issueId);
    }
    if (filter.blockId) {
        conditions.append(QStringLiteral("i.block_id = ?"));
        bindings.append(*filter.blockId);
    }
    if (filter.status) {
        conditions.append(QStringLiteral("i.status = ?"));
        bindings.append(*filter.status);
    }
    if (filter.priority) {
        conditions.append(QStringLiteral("i.priority = ?"));
        bindings.append(*filter.priority);
    }
    if (filter.assigneeId) {
        conditions.append(QStringLiteral("i.assignee_id = ?"));
        bindings.append(*filter.assigneeId);
    }
    if (!filter.search.isEmpty()) {
        conditions.append(QStringLiteral(
            "(instr(lower(i.title), lower(?)) > 0 "
            "OR instr(lower(COALESCE(i.description, '')), lower(?)) > 0)"));
        bindings.append(filter.search);
        bindings.append(filter.search);
    }

    QString statement = issueProjection();
    if (!conditions.isEmpty()) {
        statement += QStringLiteral("WHERE ")
            + conditions.join(QStringLiteral(" AND "))
            + QLatin1Char(' ');
    }
    statement += orderBy(filter.sort);

    QSqlQuery query(m_database.connection());
    query.prepare(statement);
    for (const QVariant &binding : bindings) {
        query.addBindValue(binding);
    }
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (issues) {
        while (query.next()) {
            issues->append(readIssue(query));
        }
    }
    return success();
}

IssueDaoResult IssueDao::issueById(
    qint64 id, std::optional<IssueRecord> *issue) const
{
    if (issue) {
        issue->reset();
    }

    QSqlQuery query(m_database.connection());
    query.prepare(issueProjection() + QStringLiteral("WHERE i.id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (query.next() && issue) {
        *issue = readIssue(query);
    }
    return success();
}

IssueDaoResult IssueDao::blockExists(qint64 id, bool *found) const
{
    return exists(QStringLiteral("SELECT 1 FROM blocks WHERE id = ?"),
                  id, found);
}

IssueDaoResult IssueDao::userExists(qint64 id, bool *found) const
{
    return exists(QStringLiteral("SELECT 1 FROM users WHERE id = ?"),
                  id, found);
}

IssueDaoResult IssueDao::create(const IssueRecord &values,
                                IssueRecord *createdIssue) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO issues(block_id, title, description, status, priority, "
        "reporter_id, assignee_id) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(values.blockId);
    query.addBindValue(values.title);
    query.addBindValue(values.description);
    query.addBindValue(values.status);
    query.addBindValue(values.priority);
    query.addBindValue(values.reporterId);
    query.addBindValue(values.assigneeId
                           ? QVariant::fromValue(*values.assigneeId)
                           : QVariant());
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }

    std::optional<IssueRecord> created;
    const IssueDaoResult readResult = issueById(
        query.lastInsertId().toLongLong(), &created);
    if (!readResult.ok()) {
        return readResult;
    }
    if (!created) {
        return {IssueDaoError::Database,
                QStringLiteral("The created issue could not be read back.")};
    }
    if (createdIssue) {
        *createdIssue = *created;
    }
    return success();
}

IssueDaoResult IssueDao::createWithAttachments(
    const IssueRecord &values,
    const QList<AttachmentRecord> &attachments,
    IssueRecord *createdIssue) const
{
    QSqlDatabase database = m_database.connection();
    if (!database.transaction()) {
        return failure(database.lastError(), false);
    }
    IssueRecord created;
    IssueDaoResult result = create(values, &created);
    if (result.ok()) {
        result = insertIssueAttachments(database, created.id, attachments);
    }
    std::optional<IssueRecord> refreshed;
    if (result.ok()) {
        result = issueById(created.id, &refreshed);
    }
    if (!result.ok() || !refreshed) {
        database.rollback();
        return result.ok()
            ? IssueDaoResult{IssueDaoError::Database,
                             QStringLiteral("The created issue could not be read back.")}
            : result;
    }
    if (!database.commit()) {
        const QSqlError error = database.lastError();
        database.rollback();
        return failure(error, false);
    }
    if (createdIssue) {
        *createdIssue = *refreshed;
    }
    return success();
}

IssueDaoResult IssueDao::update(const IssueRecord &values,
                                IssueRecord *updatedIssue) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "UPDATE issues SET block_id = ?, title = ?, description = ?, "
        "priority = ?, assignee_id = ? WHERE id = ?"));
    query.addBindValue(values.blockId);
    query.addBindValue(values.title);
    query.addBindValue(values.description);
    query.addBindValue(values.priority);
    query.addBindValue(values.assigneeId
                           ? QVariant::fromValue(*values.assigneeId)
                           : QVariant());
    query.addBindValue(values.id);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }

    std::optional<IssueRecord> updated;
    const IssueDaoResult readResult = issueById(values.id, &updated);
    if (!readResult.ok()) {
        return readResult;
    }
    if (!updated) {
        return {IssueDaoError::Database,
                QStringLiteral("The updated issue could not be read back.")};
    }
    if (updatedIssue) {
        *updatedIssue = *updated;
    }
    return success();
}

IssueDaoResult IssueDao::updateWithAttachments(
    const IssueRecord &values,
    const QList<AttachmentRecord> &attachments,
    const QList<qint64> &removeAttachmentIds,
    IssueRecord *updatedIssue) const
{
    QSqlDatabase database = m_database.connection();
    if (!database.transaction()) {
        return failure(database.lastError(), false);
    }
    IssueRecord updated;
    IssueDaoResult result = update(values, &updated);
    QSqlQuery removeQuery(database);
    for (qint64 attachmentId : removeAttachmentIds) {
        if (!result.ok()) {
            break;
        }
        removeQuery.prepare(QStringLiteral(
            "DELETE FROM attachments "
            "WHERE id = ? AND issue_id = ? AND comment_id IS NULL"));
        removeQuery.addBindValue(attachmentId);
        removeQuery.addBindValue(values.id);
        if (!removeQuery.exec()) {
            result = failure(removeQuery.lastError(), true);
        } else if (removeQuery.numRowsAffected() != 1) {
            result = {IssueDaoError::Conflict,
                      QStringLiteral("Description attachment could not be removed.")};
        }
        removeQuery.finish();
    }
    if (result.ok()) {
        result = insertIssueAttachments(database, values.id, attachments);
    }
    std::optional<IssueRecord> refreshed;
    if (result.ok()) {
        result = issueById(values.id, &refreshed);
    }
    if (!result.ok() || !refreshed) {
        database.rollback();
        return result.ok()
            ? IssueDaoResult{IssueDaoError::Database,
                             QStringLiteral("The updated issue could not be read back.")}
            : result;
    }
    if (!database.commit()) {
        const QSqlError error = database.lastError();
        database.rollback();
        return failure(error, false);
    }
    if (updatedIssue) {
        *updatedIssue = *refreshed;
    }
    return success();
}

IssueDaoResult IssueDao::updateStatus(qint64 id,
                                      const QString &status,
                                      IssueRecord *updatedIssue) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("UPDATE issues SET status = ? WHERE id = ?"));
    query.addBindValue(status);
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }

    std::optional<IssueRecord> updated;
    const IssueDaoResult readResult = issueById(id, &updated);
    if (!readResult.ok()) {
        return readResult;
    }
    if (!updated) {
        return {IssueDaoError::Database,
                QStringLiteral("The updated issue could not be read back.")};
    }
    if (updatedIssue) {
        *updatedIssue = *updated;
    }
    return success();
}

IssueDaoResult IssueDao::remove(qint64 id, bool *removed) const
{
    if (removed) {
        *removed = false;
    }

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("DELETE FROM issues WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }
    if (removed) {
        *removed = query.numRowsAffected() > 0;
    }
    return success();
}

IssueDaoResult IssueDao::exists(const QString &statement,
                                qint64 id,
                                bool *found) const
{
    if (found) {
        *found = false;
    }
    QSqlQuery query(m_database.connection());
    query.prepare(statement);
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (found) {
        *found = query.next();
    }
    return success();
}

IssueRecord IssueDao::readIssue(const QSqlQuery &query)
{
    IssueRecord issue;
    issue.id = query.value(0).toLongLong();
    issue.blockId = query.value(1).toLongLong();
    issue.title = query.value(2).toString();
    issue.description = query.value(3).toString();
    issue.status = query.value(4).toString();
    issue.priority = query.value(5).toString();
    issue.reporterId = query.value(6).toLongLong();
    if (!query.value(7).isNull()) {
        issue.assigneeId = query.value(7).toLongLong();
    }
    issue.createdAt = query.value(8).toString();
    issue.reporter = {
        issue.reporterId,
        query.value(9).toString(),
        query.value(10).toString()
    };
    if (issue.assigneeId) {
        issue.assignee = IssueUserReference{
            *issue.assigneeId,
            query.value(11).toString(),
            query.value(12).toString()
        };
    }
    issue.commentCount = query.value(13).toLongLong();
    issue.attachmentCount = query.value(14).toLongLong();
    return issue;
}
