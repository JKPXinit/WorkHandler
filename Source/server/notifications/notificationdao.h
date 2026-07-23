#ifndef NOTIFICATIONDAO_H
#define NOTIFICATIONDAO_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

class DatabaseManager;
class QSqlQuery;

struct NotificationSender
{
    qint64 id {0};
    QString username;
    QString displayName;

    QJsonObject toJson() const;
};

struct NotificationRecord
{
    qint64 id {0};
    QString type;
    QString title;
    QString content;
    qint64 relatedId {0};
    std::optional<qint64> senderId;
    qint64 recipientId {0};
    QString createdAt;
    std::optional<NotificationSender> sender;

    QJsonObject toJson() const;
};

struct NotificationCreateInput
{
    QString type;
    QString title;
    QString content;
    qint64 relatedId {0};
    std::optional<qint64> senderId;
    qint64 recipientId {0};
};

enum class NotificationDaoError {
    None,
    Database
};

struct NotificationDaoResult
{
    NotificationDaoError error {NotificationDaoError::None};
    QString message;

    bool ok() const;
};

class NotificationDao
{
public:
    explicit NotificationDao(DatabaseManager &database);

    NotificationDaoResult createMany(
        const QList<NotificationCreateInput> &inputs,
        QList<NotificationRecord> *createdNotifications) const;
    NotificationDaoResult unreadForRecipient(
        qint64 recipientId,
        QList<NotificationRecord> *notifications) const;
    NotificationDaoResult unreadCount(qint64 recipientId,
                                      qint64 *count) const;
    NotificationDaoResult removeUnread(qint64 id,
                                       qint64 recipientId,
                                       bool *removed) const;
    NotificationDaoResult removeAllUnread(qint64 recipientId,
                                          qint64 *removedCount) const;

private:
    static NotificationRecord readNotification(const QSqlQuery &query);

    DatabaseManager &m_database;
};

#endif // NOTIFICATIONDAO_H
