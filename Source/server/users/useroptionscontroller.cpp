#include "users/useroptionscontroller.h"

#include "api/apicontext.h"
#include "users/useroptionsservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonArray>

void UserOptionsController::registerRoutes(QHttpServer &server)
{
    server.route(
        QStringLiteral("/api/users/options"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            QList<UserOption> options;
            QString errorMessage;
            if (!m_service.options(&options, &errorMessage)) {
                return ApiContext::errorResponse(
                    QHttpServerResponse::StatusCode::InternalServerError,
                    QStringLiteral("database_error"),
                    errorMessage.isEmpty()
                        ? QStringLiteral("Database operation failed.")
                        : errorMessage);
            }

            QJsonArray users;
            for (const UserOption &option : options) {
                users.append(option.toJson());
            }
            return ApiContext::successResponse({
                {QStringLiteral("users"), users}
            });
        });
}

UserOptionsController::UserOptionsController(ApiContext &apiContext,
                                             UserOptionsService &service)
    : m_apiContext(apiContext)
    , m_service(service)
{
}
