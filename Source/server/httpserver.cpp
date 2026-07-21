#include "httpserver.h"

#include "passwordhasher.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTimer>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;

QString databaseErrorMessage(const QString &detail)
{
    return detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail;
}
}

HttpServer::HttpServer(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_database(databasePath)
    , m_httpServer(this)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

HttpServer::~HttpServer()
{
    if (m_tcpServer) {
        m_tcpServer->close();
    }
}

bool HttpServer::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return true;
    }

    QString bootstrapPassword;
    if (!m_database.initialize(errorMessage, &bootstrapPassword)) {
        setState(State::Error, errorMessage ? *errorMessage
                                            : QStringLiteral("Database initialization failed."));
        return false;
    }

    QString secretError;
    const QByteArray secret = m_database.tokenSecret(&secretError);
    if (secret.isEmpty()) {
        if (errorMessage) {
            *errorMessage = databaseErrorMessage(secretError);
        }
        setState(State::Error, databaseErrorMessage(secretError));
        return false;
    }
    m_tokenHelper.setSecret(secret);

    registerRoutes();
    m_initialized = true;
    if (!bootstrapPassword.isEmpty()) {
        emit bootstrapAdminCreated(QStringLiteral("admin"), bootstrapPassword);
    }
    return true;
}

bool HttpServer::databaseWasCreated() const
{
    return m_database.wasCreated();
}

bool HttpServer::isRunning() const
{
    return m_tcpServer && m_tcpServer->isListening();
}

ServerConfig HttpServer::configuration(QString *errorMessage) const
{
    return m_database.serverConfig(errorMessage);
}

bool HttpServer::startServer(const QString &bindAddress, quint16 port)
{
    if (!m_initialized) {
        setState(State::Error, tr("HTTP server is not initialized."));
        return false;
    }
    if (isRunning()) {
        setState(State::Running,
                 tr("Listening on %1:%2").arg(m_bindAddress).arg(m_port));
        return true;
    }

    QHostAddress address;
    if (!address.setAddress(bindAddress) || address.protocol() != QAbstractSocket::IPv4Protocol) {
        setState(State::Error, tr("Invalid IPv4 bind address: %1").arg(bindAddress));
        return false;
    }
    if (port == 0) {
        setState(State::Error, tr("Invalid HTTP server port."));
        return false;
    }

    ServerConfig config = m_database.serverConfig();
    config.serverInterface = bindAddress;
    config.serverPort = port;
    QString configError;
    if (!updateConfiguration(config, &configError)) {
        setState(State::Error, databaseErrorMessage(configError));
        return false;
    }

    setState(State::Starting, tr("Binding %1:%2").arg(bindAddress).arg(port));
    auto *tcpServer = m_tcpServer;
    if (!tcpServer) {
        tcpServer = new QTcpServer(this);
    }
    if (!tcpServer->listen(address, port)) {
        const QString error = tcpServer->errorString();
        if (tcpServer != m_tcpServer) {
            tcpServer->deleteLater();
        }
        setState(State::Error, error);
        return false;
    }
    if (!m_tcpServer && !m_httpServer.bind(tcpServer)) {
        tcpServer->close();
        tcpServer->deleteLater();
        setState(State::Error, tr("QHttpServer could not bind the TCP listener."));
        return false;
    }

    m_tcpServer = tcpServer;
    m_bindAddress = bindAddress;
    m_port = tcpServer->serverPort();
    setState(State::Running,
             tr("Listening on %1:%2").arg(m_bindAddress).arg(m_port));
    return true;
}

void HttpServer::stopServer()
{
    if (!m_tcpServer) {
        setState(State::Stopped);
        return;
    }

    setState(State::Stopping, tr("Closing the HTTP listener."));
    m_tcpServer->close();
    m_bindAddress.clear();
    m_port = 0;
    setState(State::Stopped);
}

void HttpServer::restartServer(const QString &bindAddress, quint16 port)
{
    stopServer();
    startServer(bindAddress, port);
}

bool HttpServer::updateConfiguration(const ServerConfig &config, QString *errorMessage)
{
    QHostAddress address;
    if (!address.setAddress(config.serverInterface)
        || address.protocol() != QAbstractSocket::IPv4Protocol) {
        if (errorMessage) {
            *errorMessage = tr("Invalid IPv4 server interface.");
        }
        return false;
    }
    if (config.serverPort == 0) {
        if (errorMessage) {
            *errorMessage = tr("Server port must be between 1 and 65535.");
        }
        return false;
    }
    if (config.maxImageWidth < 320 || config.maxImageWidth > 16384) {
        if (errorMessage) {
            *errorMessage = tr("Maximum image width must be between 320 and 16384.");
        }
        return false;
    }

    if (!m_database.setServerConfig(config, errorMessage)) {
        return false;
    }
    emit configurationChanged(config.serverInterface,
                              config.serverPort,
                              config.autoStart,
                              config.keepOriginal,
                              config.maxImageWidth);
    return true;
}

void HttpServer::testReachability(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("http")) {
        emit reachabilityTested(false, tr("Invalid HTTP URL."));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("WorkHandler/%1")
                          .arg(QCoreApplication::applicationVersion()));
    QNetworkReply *reply = m_networkManager->get(request);
    QTimer::singleShot(5000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool reachable = reply->error() == QNetworkReply::NoError
            && status >= 200 && status < 300;
        const QString detail = reachable
            ? tr("HTTP %1").arg(status)
            : (reply->error() == QNetworkReply::OperationCanceledError
                   ? tr("Request timed out.")
                   : reply->errorString());
        emit reachabilityTested(reachable, detail);
        reply->deleteLater();
    });
}

void HttpServer::registerRoutes()
{
    using Method = QHttpServerRequest::Method;

    m_httpServer.route(QStringLiteral("/"), Method::Get,
                       [this]() { return staticFrontendResponse(); });
    m_httpServer.route(QStringLiteral("/index.html"), Method::Get,
                       [this]() { return staticFrontendResponse(); });
    m_httpServer.route(QStringLiteral("/api/health"), Method::Get,
                       [this]() { return healthResponse(); });

    m_httpServer.route(
        QStringLiteral("/api/auth/login"), Method::Post,
        [this](const QHttpServerRequest &request) {
            QJsonObject body;
            QString parseError;
            if (!parseJsonObject(request, &body, &parseError)) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_json"), parseError);
            }
            const QString username = body.value(QStringLiteral("username"))
                                         .toString().trimmed();
            const QString password = body.value(QStringLiteral("password")).toString();
            if (username.isEmpty() || password.isEmpty()) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("missing_credentials"),
                                     tr("Username and password are required."));
            }

            QString databaseError;
            const auto user = m_database.userByUsername(username, &databaseError);
            if (!databaseError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            if (!user || !PasswordHasher::verifyPassword(password, user->passwordHash)) {
                return errorResponse(StatusCode::Unauthorized,
                                     QStringLiteral("invalid_credentials"),
                                     tr("Invalid username or password."));
            }

            return successResponse({
                {QStringLiteral("token"), m_tokenHelper.issue(user->id, user->role)},
                {QStringLiteral("user"), user->toJson()}
            });
        });

    m_httpServer.route(
        QStringLiteral("/api/auth/logout"), Method::Post,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, false);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            return successResponse({
                {QStringLiteral("logged_out"), true}
            });
        });

    m_httpServer.route(
        QStringLiteral("/api/auth/me"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, false);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            return successResponse({
                {QStringLiteral("user"), authorization.user.toJson()}
            });
        });

    m_httpServer.route(
        QStringLiteral("/api/users"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QString databaseError;
            const QList<UserRecord> records = m_database.users(&databaseError);
            if (!databaseError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            QJsonArray users;
            for (const UserRecord &user : records) {
                users.append(user.toJson());
            }
            return successResponse({{QStringLiteral("users"), users}});
        });

    m_httpServer.route(
        QStringLiteral("/api/users"), Method::Post,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QJsonObject body;
            QString parseError;
            if (!parseJsonObject(request, &body, &parseError)) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_json"), parseError);
            }
            const QString username = body.value(QStringLiteral("username"))
                                         .toString().trimmed();
            const QString password = body.value(QStringLiteral("password")).toString();
            const QString role = body.value(QStringLiteral("role")).toString();
            const QString displayName = body.value(QStringLiteral("display_name"))
                                            .toString().trimmed();
            if (!validUsername(username) || password.size() < 8
                || password.size() > 256 || !validRole(role)
                || displayName.size() > 128) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_user"),
                                     tr("Provide a valid username, role and password of at least 8 characters."));
            }
            if (m_database.usernameExists(username)) {
                return errorResponse(StatusCode::Conflict,
                                     QStringLiteral("username_exists"),
                                     tr("The username already exists."));
            }

            UserRecord user;
            QString databaseError;
            if (!m_database.createUser(username,
                                       PasswordHasher::hashPassword(password),
                                       role,
                                       displayName,
                                       &user,
                                       &databaseError)) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            return successResponse({{QStringLiteral("user"), user.toJson()}},
                                   StatusCode::Created);
        });

    m_httpServer.route(
        QStringLiteral("/api/users/<arg>"), Method::Get,
        [this](qint64 id, const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QString databaseError;
            const auto user = m_database.userById(id, &databaseError);
            if (!databaseError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            if (!user) {
                return errorResponse(StatusCode::NotFound,
                                     QStringLiteral("user_not_found"),
                                     tr("User was not found."));
            }
            return successResponse({{QStringLiteral("user"), user->toJson()}});
        });

    m_httpServer.route(
        QStringLiteral("/api/users/<arg>"), Method::Put,
        [this](qint64 id, const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QString databaseError;
            const auto existing = m_database.userById(id, &databaseError);
            if (!databaseError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            if (!existing) {
                return errorResponse(StatusCode::NotFound,
                                     QStringLiteral("user_not_found"),
                                     tr("User was not found."));
            }

            QJsonObject body;
            QString parseError;
            if (!parseJsonObject(request, &body, &parseError)) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_json"), parseError);
            }
            const QString username = body.contains(QStringLiteral("username"))
                ? body.value(QStringLiteral("username")).toString().trimmed()
                : existing->username;
            const QString role = body.contains(QStringLiteral("role"))
                ? body.value(QStringLiteral("role")).toString()
                : existing->role;
            const QString displayName = body.contains(QStringLiteral("display_name"))
                ? body.value(QStringLiteral("display_name")).toString().trimmed()
                : existing->displayName;
            const QString password = body.value(QStringLiteral("password")).toString();
            if (!validUsername(username) || !validRole(role)
                || displayName.size() > 128
                || (!password.isEmpty() && (password.size() < 8 || password.size() > 256))) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_user"),
                                     tr("The user fields are invalid."));
            }
            if (m_database.usernameExists(username, id)) {
                return errorResponse(StatusCode::Conflict,
                                     QStringLiteral("username_exists"),
                                     tr("The username already exists."));
            }
            if (existing->role == QStringLiteral("admin")
                && role != QStringLiteral("admin")) {
                const int administrators = m_database.adminCount(&databaseError);
                if (!databaseError.isEmpty()) {
                    return errorResponse(StatusCode::InternalServerError,
                                         QStringLiteral("database_error"),
                                         databaseErrorMessage(databaseError));
                }
                if (administrators <= 1) {
                    return errorResponse(StatusCode::Conflict,
                                         QStringLiteral("last_admin"),
                                         tr("The last administrator cannot be demoted."));
                }
            }

            UserRecord updated;
            const QString passwordHash = password.isEmpty()
                ? QString()
                : PasswordHasher::hashPassword(password);
            if (!m_database.updateUser(id, username, role, displayName,
                                       passwordHash, &updated, &databaseError)) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            return successResponse({{QStringLiteral("user"), updated.toJson()}});
        });

    m_httpServer.route(
        QStringLiteral("/api/users/<arg>"), Method::Delete,
        [this](qint64 id, const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QString databaseError;
            const auto existing = m_database.userById(id, &databaseError);
            if (!databaseError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            if (!existing) {
                return errorResponse(StatusCode::NotFound,
                                     QStringLiteral("user_not_found"),
                                     tr("User was not found."));
            }
            if (existing->role == QStringLiteral("admin")) {
                const int administrators = m_database.adminCount(&databaseError);
                if (!databaseError.isEmpty()) {
                    return errorResponse(StatusCode::InternalServerError,
                                         QStringLiteral("database_error"),
                                         databaseErrorMessage(databaseError));
                }
                if (administrators <= 1) {
                    return errorResponse(StatusCode::Conflict,
                                         QStringLiteral("last_admin"),
                                         tr("The last administrator cannot be deleted."));
                }
            }
            if (!m_database.deleteUser(id, &databaseError)) {
                return errorResponse(StatusCode::Conflict,
                                     QStringLiteral("delete_failed"),
                                     databaseErrorMessage(databaseError));
            }
            return successResponse({{QStringLiteral("deleted_id"), id}});
        });

    m_httpServer.route(
        QStringLiteral("/api/server/config"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QString databaseError;
            const ServerConfig config = m_database.serverConfig(&databaseError);
            if (!databaseError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            return successResponse({{QStringLiteral("config"), config.toJson()}});
        });

    m_httpServer.route(
        QStringLiteral("/api/server/config"), Method::Put,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QJsonObject body;
            QString parseError;
            if (!parseJsonObject(request, &body, &parseError)) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_json"), parseError);
            }
            ServerConfig config = m_database.serverConfig();
            if (body.contains(QStringLiteral("server_interface"))) {
                config.serverInterface = body.value(
                    QStringLiteral("server_interface")).toString().trimmed();
            }
            if (body.contains(QStringLiteral("server_port"))) {
                const QJsonValue portValue = body.value(QStringLiteral("server_port"));
                const int port = portValue.isDouble() ? portValue.toInt(-1) : -1;
                if (port < 1 || port > 65535) {
                    return errorResponse(StatusCode::BadRequest,
                                         QStringLiteral("invalid_config"),
                                         tr("server_port must be between 1 and 65535."));
                }
                config.serverPort = static_cast<quint16>(port);
            }
            if (body.contains(QStringLiteral("auto_start"))) {
                if (!body.value(QStringLiteral("auto_start")).isBool()) {
                    return errorResponse(StatusCode::BadRequest,
                                         QStringLiteral("invalid_config"),
                                         tr("auto_start must be a boolean."));
                }
                config.autoStart = body.value(QStringLiteral("auto_start")).toBool();
            }
            if (body.contains(QStringLiteral("keep_original"))) {
                if (!body.value(QStringLiteral("keep_original")).isBool()) {
                    return errorResponse(StatusCode::BadRequest,
                                         QStringLiteral("invalid_config"),
                                         tr("keep_original must be a boolean."));
                }
                config.keepOriginal = body.value(QStringLiteral("keep_original")).toBool();
            }
            if (body.contains(QStringLiteral("max_image_width"))) {
                const QJsonValue widthValue = body.value(
                    QStringLiteral("max_image_width"));
                if (!widthValue.isDouble()) {
                    return errorResponse(StatusCode::BadRequest,
                                         QStringLiteral("invalid_config"),
                                         tr("max_image_width must be a number."));
                }
                config.maxImageWidth = widthValue.toInt(-1);
            }

            QString configError;
            if (!updateConfiguration(config, &configError)) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_config"), configError);
            }
            return successResponse({{QStringLiteral("config"), config.toJson()}});
        });

    m_httpServer.route(
        QStringLiteral("/api/server/restart"), Method::Post,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            const ServerConfig config = m_database.serverConfig();
            const QString url = restartUrl(request, config);
            QTimer::singleShot(200, this, [this, config]() {
                restartServer(config.serverInterface, config.serverPort);
            });
            return successResponse({{QStringLiteral("restart_url"), url}},
                                   StatusCode::Accepted);
        });

    m_httpServer.setMissingHandler(
        &m_httpServer,
        [](const QHttpServerRequest &request, QHttpServerResponder &responder) {
            QHttpServerResponse response(StatusCode::NoContent);
            if (request.method() == Method::Options) {
                addCorsHeaders(&response);
            } else {
                response = errorResponse(StatusCode::NotFound,
                                         QStringLiteral("not_found"),
                                         QStringLiteral("The requested route was not found."));
            }
            responder.sendResponse(response);
        });
}

void HttpServer::setState(State state, const QString &detail)
{
    m_state = state;
    emit stateChanged(state, detail);
}

HttpServer::AuthorizationResult HttpServer::authorize(
    const QHttpServerRequest &request, bool adminRequired) const
{
    AuthorizationResult result;
    const QByteArray authorization = request.value("Authorization").trimmed();
    constexpr char Prefix[] = "Bearer ";
    if (!authorization.startsWith(Prefix)) {
        result.code = QStringLiteral("unauthorized");
        result.message = tr("A Bearer token is required.");
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
            ? tr("The token user no longer exists.")
            : databaseErrorMessage(databaseError);
        return result;
    }
    if (adminRequired && user->role != QStringLiteral("admin")) {
        result.code = QStringLiteral("forbidden");
        result.message = tr("Administrator permission is required.");
        return result;
    }

    result.authorized = true;
    result.user = *user;
    return result;
}

QHttpServerResponse HttpServer::staticFrontendResponse() const
{
    QFile file(QStringLiteral(":/prefix4/html/index.html"));
    if (!file.open(QIODevice::ReadOnly)) {
        return errorResponse(StatusCode::InternalServerError,
                             QStringLiteral("frontend_unavailable"),
                             tr("The embedded frontend resource is unavailable."));
    }
    QHttpServerResponse response(QByteArrayLiteral("text/html; charset=utf-8"),
                                 file.readAll(), StatusCode::Ok);
    addCorsHeaders(&response);
    return response;
}

QHttpServerResponse HttpServer::healthResponse() const
{
    const ServerConfig config = m_database.serverConfig();
    const QString address = isRunning()
        ? QStringLiteral("%1:%2").arg(m_bindAddress).arg(m_port)
        : QStringLiteral("%1:%2").arg(config.serverInterface).arg(config.serverPort);
    return successResponse({
        {QStringLiteral("status"), isRunning() ? QStringLiteral("running")
                                               : QStringLiteral("stopped")},
        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("address"), address}
    });
}

QHttpServerResponse HttpServer::successResponse(const QJsonObject &data,
                                                StatusCode status)
{
    QHttpServerResponse response(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("data"), data}
    }, status);
    addCorsHeaders(&response);
    return response;
}

QHttpServerResponse HttpServer::errorResponse(StatusCode status,
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

QHttpServerResponse HttpServer::authorizationError(
    const AuthorizationResult &authorization)
{
    const StatusCode status = authorization.code == QStringLiteral("forbidden")
        ? StatusCode::Forbidden
        : StatusCode::Unauthorized;
    return errorResponse(status, authorization.code, authorization.message);
}

bool HttpServer::parseJsonObject(const QHttpServerRequest &request,
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

void HttpServer::addCorsHeaders(QHttpServerResponse *response)
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

bool HttpServer::validRole(const QString &role)
{
    return role == QStringLiteral("admin")
        || role == QStringLiteral("user")
        || role == QStringLiteral("guest");
}

bool HttpServer::validUsername(const QString &username)
{
    if (username.isEmpty() || username.size() > 64) {
        return false;
    }
    for (const QChar character : username) {
        if (character.isSpace() || character.category() == QChar::Other_Control) {
            return false;
        }
    }
    return true;
}

QString HttpServer::restartUrl(const QHttpServerRequest &request,
                               const ServerConfig &config)
{
    QString host = config.serverInterface;
    if (host.isEmpty() || host == QStringLiteral("0.0.0.0")) {
        host = request.localAddress().toString();
    }
    if (host.isEmpty() || host == QStringLiteral("0.0.0.0")) {
        host = QStringLiteral("127.0.0.1");
    }
    return QStringLiteral("http://%1:%2").arg(host).arg(config.serverPort);
}
