#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include "databasemanager.h"
#include "notifications/notificationdao.h"

#include <functional>

class IssueDao;
struct CommentRecord;
struct IssueRecord;

class NotificationManager
{
public:
    using ChangeCallback = std::function<void(
        const QList<NotificationRecord> &createdNotifications,
        const QList<qint64> &changedRecipients)>;

    NotificationManager(DatabaseManager &database,
                        NotificationDao &dao,
                        IssueDao &issueDao,
                        ChangeCallback changeCallback);

    void issueCreated(const IssueRecord &issue,
                      const UserRecord &actor);
    void issueUpdated(const IssueRecord &before,
                      const IssueRecord &after,
                      const UserRecord &actor);
    void commentAdded(qint64 issueId,
                      const CommentRecord &comment,
                      const UserRecord &actor);
    void statusChanged(const IssueRecord &before,
                       const IssueRecord &after,
                       const UserRecord &actor);

    NotificationDaoResult unreadForRecipient(
        qint64 recipientId,
        QList<NotificationRecord> *notifications) const;
    NotificationDaoResult unreadCount(qint64 recipientId,
                                      qint64 *count) const;
    NotificationDaoResult removeUnread(qint64 id,
                                       qint64 recipientId,
                                       bool *removed);
    NotificationDaoResult removeAllUnread(qint64 recipientId,
                                          qint64 *removedCount);

    bool localAdminUnreadCount(qint64 *count,
                               QString *errorMessage) const;
    bool localAdminUnreadNotifications(
        QList<NotificationRecord> *notifications,
        QString *errorMessage) const;
    bool markLocalAdminNotificationRead(qint64 notificationId,
                                        QString *errorMessage);
    bool markAllLocalAdminNotificationsRead(qint64 *deletedCount,
                                            QString *errorMessage);
    bool issueExists(qint64 issueId, QString *errorMessage) const;
    void notifyLocalAdminCountChanged();

private:
    void deliver(const QString &type,
                 const QString &title,
                 const QString &content,
                 qint64 issueId,
                 qint64 senderId,
                 const QList<qint64> &recipients);
    std::optional<UserRecord> localAdmin(QString *errorMessage) const;
    static void appendRecipient(QList<qint64> *recipients,
                                qint64 recipientId,
                                qint64 actorId);

    DatabaseManager &m_database;
    NotificationDao &m_dao;
    IssueDao &m_issueDao;
    ChangeCallback m_changeCallback;
};

#endif // NOTIFICATIONMANAGER_H
