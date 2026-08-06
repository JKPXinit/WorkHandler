#include "notifications/notificationmanager.h"

#include "comments/commentdao.h"
#include "databasemanager.h"
#include "issues/issuedao.h"

#include <QDebug>
#include <QRegularExpression>

#include <utility>

namespace {
QString actorName(const UserRecord &actor)
{
    return actor.displayName.isEmpty() ? actor.username : actor.displayName;
}

QString commentSummary(const CommentRecord &comment)
{
    QString text = comment.content;
    text.remove(QRegularExpression(
        QStringLiteral("!?\\[[^\\]\\r\\n]*\\]\\(attachment:[0-9]+\\)")));
    text.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                 QStringLiteral(" "));
    text = text.simplified();
    if (text.isEmpty() && !comment.attachments.isEmpty()) {
        return QStringLiteral("%1 attachment%2")
            .arg(comment.attachments.size())
            .arg(comment.attachments.size() == 1 ? QString() : QStringLiteral("s"));
    }
    return text.left(120);
}

void logDeliveryFailure(const QString &type, const QString &message)
{
    qWarning().noquote()
        << QStringLiteral("Failed to create %1 notification: %2")
               .arg(type, message);
}
}

NotificationManager::NotificationManager(DatabaseManager &database,
                                         NotificationDao &dao,
                                         IssueDao &issueDao,
                                         ChangeCallback changeCallback)
    : m_database(database)
    , m_dao(dao)
    , m_issueDao(issueDao)
    , m_changeCallback(std::move(changeCallback))
{
}

void NotificationManager::issueCreated(const IssueRecord &issue,
                                       const UserRecord &actor)
{
    QString adminError;
    const auto admin = localAdmin(&adminError);
    if (!adminError.isEmpty()) {
        logDeliveryFailure(QStringLiteral("issue_created"), adminError);
    } else if (!admin) {
        qWarning() << "Fixed admin account was not found for issue notification.";
    } else {
        QList<qint64> recipients;
        appendRecipient(&recipients, admin->id, actor.id);
        deliver(QStringLiteral("issue_created"),
                QStringLiteral("New issue: %1").arg(issue.title),
                QStringLiteral("%1 created this issue.").arg(actorName(actor)),
                issue.id, actor.id, recipients);
    }

    if (issue.assigneeId) {
        QList<qint64> recipients;
        appendRecipient(&recipients, *issue.assigneeId, actor.id);
        deliver(QStringLiteral("issue_assigned"),
                QStringLiteral("Issue assigned: %1").arg(issue.title),
                QStringLiteral("%1 assigned this issue to you.")
                    .arg(actorName(actor)),
                issue.id, actor.id, recipients);
    }
}

void NotificationManager::issueUpdated(const IssueRecord &before,
                                       const IssueRecord &after,
                                       const UserRecord &actor)
{
    if (before.assigneeId == after.assigneeId || !after.assigneeId) {
        return;
    }
    QList<qint64> recipients;
    appendRecipient(&recipients, *after.assigneeId, actor.id);
    deliver(QStringLiteral("issue_assigned"),
            QStringLiteral("Issue assigned: %1").arg(after.title),
            QStringLiteral("%1 assigned this issue to you.")
                .arg(actorName(actor)),
            after.id, actor.id, recipients);
}

void NotificationManager::commentAdded(qint64 issueId,
                                       const CommentRecord &comment,
                                       const UserRecord &actor)
{
    std::optional<IssueRecord> issue;
    const IssueDaoResult issueResult = m_issueDao.issueById(issueId, &issue);
    if (!issueResult.ok()) {
        logDeliveryFailure(QStringLiteral("comment_added"), issueResult.message);
        return;
    }
    if (!issue) {
        qWarning() << "Issue was not found while creating comment notification:"
                   << issueId;
        return;
    }

    QList<qint64> recipients;
    appendRecipient(&recipients, issue->reporterId, actor.id);
    if (issue->assigneeId) {
        appendRecipient(&recipients, *issue->assigneeId, actor.id);
    }
    QString adminError;
    const auto admin = localAdmin(&adminError);
    if (!adminError.isEmpty()) {
        logDeliveryFailure(QStringLiteral("comment_added"), adminError);
    } else if (admin) {
        appendRecipient(&recipients, admin->id, actor.id);
    } else {
        qWarning() << "Fixed admin account was not found for comment notification.";
    }

    deliver(QStringLiteral("comment_added"),
            QStringLiteral("New comment: %1").arg(issue->title),
            QStringLiteral("%1: %2")
                .arg(actorName(actor), commentSummary(comment)),
            issue->id, actor.id, recipients);
}

void NotificationManager::statusChanged(const IssueRecord &before,
                                        const IssueRecord &after,
                                        const UserRecord &actor)
{
    if (before.status == after.status) {
        return;
    }

    QList<qint64> recipients;
    appendRecipient(&recipients, after.reporterId, actor.id);
    if (after.assigneeId) {
        appendRecipient(&recipients, *after.assigneeId, actor.id);
    }
    QString adminError;
    const auto admin = localAdmin(&adminError);
    if (!adminError.isEmpty()) {
        logDeliveryFailure(QStringLiteral("status_changed"), adminError);
    } else if (admin) {
        appendRecipient(&recipients, admin->id, actor.id);
    } else {
        qWarning() << "Fixed admin account was not found for status notification.";
    }

    deliver(QStringLiteral("status_changed"),
            QStringLiteral("Status changed: %1").arg(after.title),
            QStringLiteral("%1 changed %2 to %3.")
                .arg(actorName(actor), before.status, after.status),
            after.id, actor.id, recipients);
}

NotificationDaoResult NotificationManager::unreadForRecipient(
    qint64 recipientId,
    QList<NotificationRecord> *notifications) const
{
    return m_dao.unreadForRecipient(recipientId, notifications);
}

NotificationDaoResult NotificationManager::unreadCount(qint64 recipientId,
                                                        qint64 *count) const
{
    return m_dao.unreadCount(recipientId, count);
}

NotificationDaoResult NotificationManager::removeUnread(qint64 id,
                                                         qint64 recipientId,
                                                         bool *removed)
{
    bool didRemove = false;
    const NotificationDaoResult result = m_dao.removeUnread(
        id, recipientId, &didRemove);
    if (removed) {
        *removed = didRemove;
    }
    if (result.ok() && didRemove && m_changeCallback) {
        m_changeCallback(QList<NotificationRecord>(),
                         QList<qint64>{recipientId});
    }
    return result;
}

NotificationDaoResult NotificationManager::removeAllUnread(
    qint64 recipientId,
    qint64 *removedCount)
{
    const NotificationDaoResult result = m_dao.removeAllUnread(
        recipientId, removedCount);
    if (result.ok() && m_changeCallback) {
        m_changeCallback(QList<NotificationRecord>(),
                         QList<qint64>{recipientId});
    }
    return result;
}

bool NotificationManager::localAdminUnreadCount(
    qint64 *count, QString *errorMessage) const
{
    const auto admin = localAdmin(errorMessage);
    if (!admin) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Fixed admin account was not found.");
        }
        return false;
    }
    const NotificationDaoResult result = unreadCount(admin->id, count);
    if (!result.ok() && errorMessage) {
        *errorMessage = result.message;
    }
    return result.ok();
}

bool NotificationManager::markAllLocalAdminNotificationsRead(
    qint64 *deletedCount, QString *errorMessage)
{
    const auto admin = localAdmin(errorMessage);
    if (!admin) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Fixed admin account was not found.");
        }
        return false;
    }
    const NotificationDaoResult result = removeAllUnread(
        admin->id, deletedCount);
    if (!result.ok() && errorMessage) {
        *errorMessage = result.message;
    }
    return result.ok();
}

bool NotificationManager::issueExists(qint64 issueId,
                                      QString *errorMessage) const
{
    std::optional<IssueRecord> issue;
    const IssueDaoResult result = m_issueDao.issueById(issueId, &issue);
    if (!result.ok()) {
        if (errorMessage) {
            *errorMessage = result.message;
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return issue.has_value();
}

void NotificationManager::notifyLocalAdminCountChanged()
{
    QString errorMessage;
    const auto admin = localAdmin(&errorMessage);
    if (!errorMessage.isEmpty()) {
        logDeliveryFailure(QStringLiteral("count_changed"), errorMessage);
        return;
    }
    if (admin && m_changeCallback) {
        m_changeCallback(QList<NotificationRecord>(),
                         QList<qint64>{admin->id});
    }
}

void NotificationManager::deliver(const QString &type,
                                  const QString &title,
                                  const QString &content,
                                  qint64 issueId,
                                  qint64 senderId,
                                  const QList<qint64> &recipients)
{
    if (recipients.isEmpty()) {
        return;
    }
    QList<NotificationCreateInput> inputs;
    inputs.reserve(recipients.size());
    for (qint64 recipientId : recipients) {
        inputs.append({type, title, content, issueId, senderId, recipientId});
    }

    QList<NotificationRecord> created;
    const NotificationDaoResult result = m_dao.createMany(inputs, &created);
    if (!result.ok()) {
        logDeliveryFailure(type, result.message);
        return;
    }
    if (m_changeCallback) {
        m_changeCallback(created, recipients);
    }
}

std::optional<UserRecord> NotificationManager::localAdmin(
    QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }
    return m_database.userByUsername(QStringLiteral("admin"), errorMessage);
}

void NotificationManager::appendRecipient(QList<qint64> *recipients,
                                          qint64 recipientId,
                                          qint64 actorId)
{
    if (!recipients || recipientId <= 0 || recipientId == actorId
        || recipients->contains(recipientId)) {
        return;
    }
    recipients->append(recipientId);
}
