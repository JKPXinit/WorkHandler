#include "notifications/notificationcontroller.h"

#include "api/apicontext.h"
#include "notifications/notificationmanager.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonArray>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;

QHttpServerResponse databaseError(const NotificationDaoResult &result)
{
    return ApiContext::errorResponse(
        StatusCode::InternalServerError,
        QStringLiteral("database_error"),
        result.message.isEmpty() ? QStringLiteral("Database operation failed.")
                                 : result.message);
}

bool parsePositiveId(const QString &text, qint64 *id)
{
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
}

NotificationController::NotificationController(ApiContext &apiContext,
                                               NotificationManager &manager)
    : m_apiContext(apiContext)
    , m_manager(manager)
{
}

void NotificationController::registerRoutes(QHttpServer &server)
{
    using Method = QHttpServerRequest::Method;

    server.route(
        QStringLiteral("/api/notifications"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const auto authorization = m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }
            QList<NotificationRecord> records;
            const NotificationDaoResult result = m_manager.unreadForRecipient(
                authorization.user.id, &records);
            if (!result.ok()) {
                return databaseError(result);
            }
            QJsonArray notifications;
            for (const NotificationRecord &record : records) {
                notifications.append(record.toJson());
            }
            return ApiContext::successResponse({
                {QStringLiteral("notifications"), notifications}
            });
        });

    server.route(
        QStringLiteral("/api/notifications/unread-count"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const auto authorization = m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }
            qint64 count = 0;
            const NotificationDaoResult result = m_manager.unreadCount(
                authorization.user.id, &count);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("unread_count"), count}
                  })
                : databaseError(result);
        });

    server.route(
        QStringLiteral("/api/notifications/read-all"), Method::Put,
        [this](const QHttpServerRequest &request) {
            const auto authorization = m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }
            qint64 deletedCount = 0;
            const NotificationDaoResult result = m_manager.removeAllUnread(
                authorization.user.id, &deletedCount);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("deleted_count"), deletedCount}
                  })
                : databaseError(result);
        });

    server.route(
        QStringLiteral("/api/notifications/<arg>/read"), Method::Put,
        [this](const QString &idText, const QHttpServerRequest &request) {
            const auto authorization = m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }
            qint64 id = 0;
            if (!parsePositiveId(idText, &id)) {
                return ApiContext::errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("invalid_notification_id"),
                    QStringLiteral("Notification id must be a positive integer."));
            }
            bool removed = false;
            const NotificationDaoResult result = m_manager.removeUnread(
                id, authorization.user.id, &removed);
            if (!result.ok()) {
                return databaseError(result);
            }
            return removed
                ? ApiContext::successResponse({
                      {QStringLiteral("deleted_id"), id}
                  })
                : ApiContext::errorResponse(
                      StatusCode::NotFound,
                      QStringLiteral("notification_not_found"),
                      QStringLiteral("Notification was not found."));
        });
}
