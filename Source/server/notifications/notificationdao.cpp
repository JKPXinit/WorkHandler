#include "notifications/notificationdao.h"

#include "databasemanager.h"

#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
NotificationDaoResult success()
{
    return {};
}

NotificationDaoResult failure(const QSqlError &error)
{
    const QString detail = error.text();
    return {NotificationDaoError::Database,
            detail.isEmpty() ? QStringLiteral("Database operation failed.")
                             : detail};
}

QString notificationProjection()
{
    return QStringLiteral(
        "SELECT n.id, n.type, n.title, COALESCE(n.content, ''), "
        "n.related_id, n.sender_id, n.recipient_id, n.created_at, "
        "sender.id, sender.username, COALESCE(sender.display_name, '') "
        "FROM notifications n "
        "LEFT JOIN users sender ON sender.id = n.sender_id ");
}
}

QJsonObject NotificationSender::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("username"), username},
        {QStringLiteral("display_name"), displayName}
    };
}

QJsonObject NotificationRecord::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), type},
        {QStringLiteral("title"), title},
        {QStringLiteral("content"), content},
        {QStringLiteral("related_id"), relatedId},
        {QStringLiteral("sender"),
         sender ? QJsonValue(sender->toJson())
                : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("created_at"), createdAt}
    };
}

bool NotificationDaoResult::ok() const
{
    return error == NotificationDaoError::None;
}

NotificationDao::NotificationDao(DatabaseManager &database)
    : m_database(database)
{
}

NotificationDaoResult NotificationDao::createMany(
    const QList<NotificationCreateInput> &inputs,
    QList<NotificationRecord> *createdNotifications) const
{
    if (createdNotifications) {
        createdNotifications->clear();
    }
    if (inputs.isEmpty()) {
        return success();
    }

    QSqlDatabase database = m_database.connection();
    if (!database.transaction()) {
        return failure(database.lastError());
    }

    QList<NotificationRecord> records;
    records.reserve(inputs.size());
    QSqlQuery query(database);
    for (const NotificationCreateInput &input : inputs) {
        query.clear();
        query.prepare(QStringLiteral(
            "INSERT INTO notifications(type, title, content, related_id, "
            "sender_id, recipient_id) VALUES(?, ?, ?, ?, ?, ?)"));
        query.addBindValue(input.type);
        query.addBindValue(input.title);
        query.addBindValue(input.content);
        query.addBindValue(input.relatedId);
        query.addBindValue(input.senderId
                               ? QVariant::fromValue(*input.senderId)
                               : QVariant());
        query.addBindValue(input.recipientId);
        if (!query.exec()) {
            const QSqlError error = query.lastError();
            database.rollback();
            return failure(error);
        }

        NotificationRecord record;
        record.id = query.lastInsertId().toLongLong();
        record.type = input.type;
        record.title = input.title;
        record.content = input.content;
        record.relatedId = input.relatedId;
        record.senderId = input.senderId;
        record.recipientId = input.recipientId;
        records.append(record);
    }

    if (!database.commit()) {
        const QSqlError error = database.lastError();
        database.rollback();
        return failure(error);
    }
    if (createdNotifications) {
        *createdNotifications = records;
    }
    return success();
}

NotificationDaoResult NotificationDao::unreadForRecipient(
    qint64 recipientId,
    QList<NotificationRecord> *notifications) const
{
    if (notifications) {
        notifications->clear();
    }

    QSqlQuery query(m_database.connection());
    query.prepare(notificationProjection() + QStringLiteral(
        "WHERE n.recipient_id = ? AND n.is_read = 0 "
        "ORDER BY n.created_at DESC, n.id DESC"));
    query.addBindValue(recipientId);
    if (!query.exec()) {
        return failure(query.lastError());
    }
    if (notifications) {
        while (query.next()) {
            notifications->append(readNotification(query));
        }
    }
    return success();
}

NotificationDaoResult NotificationDao::unreadCount(qint64 recipientId,
                                                     qint64 *count) const
{
    if (count) {
        *count = 0;
    }
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM notifications "
        "WHERE recipient_id = ? AND is_read = 0"));
    query.addBindValue(recipientId);
    if (!query.exec() || !query.next()) {
        return failure(query.lastError());
    }
    if (count) {
        *count = query.value(0).toLongLong();
    }
    return success();
}

NotificationDaoResult NotificationDao::removeUnread(qint64 id,
                                                      qint64 recipientId,
                                                      bool *removed) const
{
    if (removed) {
        *removed = false;
    }
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "DELETE FROM notifications "
        "WHERE id = ? AND recipient_id = ? AND is_read = 0"));
    query.addBindValue(id);
    query.addBindValue(recipientId);
    if (!query.exec()) {
        return failure(query.lastError());
    }
    if (removed) {
        *removed = query.numRowsAffected() > 0;
    }
    return success();
}

NotificationDaoResult NotificationDao::removeAllUnread(
    qint64 recipientId,
    qint64 *removedCount) const
{
    if (removedCount) {
        *removedCount = 0;
    }
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "DELETE FROM notifications "
        "WHERE recipient_id = ? AND is_read = 0"));
    query.addBindValue(recipientId);
    if (!query.exec()) {
        return failure(query.lastError());
    }
    if (removedCount) {
        *removedCount = query.numRowsAffected();
    }
    return success();
}

NotificationRecord NotificationDao::readNotification(const QSqlQuery &query)
{
    NotificationRecord notification;
    notification.id = query.value(0).toLongLong();
    notification.type = query.value(1).toString();
    notification.title = query.value(2).toString();
    notification.content = query.value(3).toString();
    notification.relatedId = query.value(4).toLongLong();
    if (!query.value(5).isNull()) {
        notification.senderId = query.value(5).toLongLong();
    }
    notification.recipientId = query.value(6).toLongLong();
    notification.createdAt = query.value(7).toString();
    if (!query.value(8).isNull()) {
        notification.sender = NotificationSender{
            query.value(8).toLongLong(),
            query.value(9).toString(),
            query.value(10).toString()
        };
    }
    return notification;
}
