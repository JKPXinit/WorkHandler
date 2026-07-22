#ifndef APICONTEXT_H
#define APICONTEXT_H

#include "databasemanager.h"
#include "tokenhelper.h"

#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QString>

class ApiContext
{
public:
    struct AuthorizationResult {
        bool authorized {false};
        UserRecord user;
        QString code;
        QString message;
    };

    ApiContext(DatabaseManager &database, const TokenHelper &tokenHelper);

    AuthorizationResult authorize(const QHttpServerRequest &request,
                                  bool adminRequired) const;

    static QHttpServerResponse successResponse(
        const QJsonObject &data = QJsonObject(),
        QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::Ok);
    static QHttpServerResponse errorResponse(
        QHttpServerResponse::StatusCode status,
        const QString &code,
        const QString &message);
    static QHttpServerResponse authorizationError(
        const AuthorizationResult &authorization);
    static bool parseJsonObject(const QHttpServerRequest &request,
                                QJsonObject *object,
                                QString *errorMessage);
    static void addCorsHeaders(QHttpServerResponse *response);

private:
    DatabaseManager &m_database;
    const TokenHelper &m_tokenHelper;
};

#endif // APICONTEXT_H
