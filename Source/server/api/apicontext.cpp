#include "api/apicontext.h"

#include <QCoreApplication>
#include <QHttpHeaders>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;

QString databaseErrorMessage(const QString &detail)
{
    return detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail;
}

QString httpServerText(const char *sourceText)
{
    return QCoreApplication::translate("HttpServer", sourceText);
}
}

ApiContext::ApiContext(DatabaseManager &database, const TokenHelper &tokenHelper)
    : m_database(database)
    , m_tokenHelper(tokenHelper)
{
}

ApiContext::AuthorizationResult ApiContext::authorize(
    const QHttpServerRequest &request, bool adminRequired) const
{
    AuthorizationResult result;
    const QByteArray authorization = request.value("Authorization").trimmed();
    constexpr char Prefix[] = "Bearer ";
    if (!authorization.startsWith(Prefix)) {
        result.code = QStringLiteral("unauthorized");
        result.message = httpServerText("A Bearer token is required.");
        return result;
    }

    const TokenHelper::Claims claims = m_tokenHelper.validate(
        QString::fromLatin1(authorization.mid(int(sizeof(Prefix) - 1))).trimmed());
    if (!claims.valid) {
        result.code = QStringLiteral("unauthorized");
        result.message = claims.error;
        return result;
    }

    QString databaseError;
    const auto user = m_database.userById(claims.userId, &databaseError);
    if (!databaseError.isEmpty() || !user) {
        result.code = QStringLiteral("unauthorized");
        result.message = databaseError.isEmpty()
            ? httpServerText("The token user no longer exists.")
            : databaseErrorMessage(databaseError);
        return result;
    }
    if (claims.tokenVersion != user->tokenVersion) {
        result.code = QStringLiteral("unauthorized");
        result.message = httpServerText("The token is no longer valid.");
        return result;
    }
    if (adminRequired && user->role != QStringLiteral("admin")) {
        result.code = QStringLiteral("forbidden");
        result.message = httpServerText("Administrator permission is required.");
        return result;
    }

    result.authorized = true;
    result.user = *user;
    return result;
}

QHttpServerResponse ApiContext::successResponse(const QJsonObject &data,
                                                StatusCode status)
{
    QHttpServerResponse response(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("data"), data}
    }, status);
    addCorsHeaders(&response);
    return response;
}

QHttpServerResponse ApiContext::errorResponse(StatusCode status,
                                              const QString &code,
                                              const QString &message)
{
    QHttpServerResponse response(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{
            {QStringLiteral("code"), code},
            {QStringLiteral("message"), message}
        }}
    }, status);
    addCorsHeaders(&response);
    return response;
}

QHttpServerResponse ApiContext::authorizationError(
    const AuthorizationResult &authorization)
{
    const StatusCode status = authorization.code == QStringLiteral("forbidden")
        ? StatusCode::Forbidden
        : StatusCode::Unauthorized;
    return errorResponse(status, authorization.code, authorization.message);
}

bool ApiContext::parseJsonObject(const QHttpServerRequest &request,
                                 QJsonObject *object,
                                 QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("The request body must be a JSON object.")
                : parseError.errorString();
        }
        return false;
    }
    if (object) {
        *object = document.object();
    }
    return true;
}

void ApiContext::addCorsHeaders(QHttpServerResponse *response)
{
    QHttpHeaders headers = response->headers();
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                   QStringLiteral("*"));
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                   QStringLiteral("Authorization, Content-Type"));
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                   QStringLiteral("GET, POST, PUT, DELETE, OPTIONS"));
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlMaxAge,
                   QStringLiteral("600"));
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl,
                   QStringLiteral("no-store"));
    response->setHeaders(std::move(headers));
}
