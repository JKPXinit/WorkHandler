#include "httpserver.h"

#include "attachments/attachmentcontroller.h"
#include "attachments/attachmentdao.h"
#include "attachments/attachmentservice.h"
#include "attachments/imageprocessor.h"
#include "blocks/blockcontroller.h"
#include "blocks/blockdao.h"
#include "blocks/blockservice.h"
#include "comments/commentcontroller.h"
#include "comments/commentdao.h"
#include "comments/commentservice.h"
#include "issues/issuecontroller.h"
#include "issues/issuedao.h"
#include "issues/issueservice.h"
#include "maintenance/maintenancedao.h"
#include "maintenance/maintenancemanager.h"
#include "notifications/notificationcontroller.h"
#include "notifications/notificationdao.h"
#include "notifications/notificationmanager.h"
#include "passwordhasher.h"
#include "users/useroptionscontroller.h"
#include "users/useroptionsservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTimer>

#include <utility>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;

QString databaseErrorMessage(const QString &detail)
{
    return detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail;
}
}

QJsonObject ServerConfig::toJson() const
{
    return {
        {QStringLiteral("server_interface"), serverInterface},
        {QStringLiteral("server_port"), int(serverPort)},
        {QStringLiteral("auto_start"), autoStart},
        {QStringLiteral("keep_original"), keepOriginal},
        {QStringLiteral("max_image_width"), maxImageWidth}
    };
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
    shutdown();
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

    const QString uploadRoot = QDir(
        QFileInfo(m_database.databasePath()).absolutePath())
                                   .filePath(QStringLiteral("uploads"));
    m_maintenanceDao = std::make_unique<MaintenanceDao>(m_database);
    m_maintenanceManager = std::make_unique<MaintenanceManager>(
        *m_maintenanceDao,
        uploadRoot,
        [this]() {
            return m_maintenanceConfigurationProvider
                ? m_maintenanceConfigurationProvider()
                : MaintenanceConfig();
        },
        [this]() { return isRunning(); },
        [this](MaintenanceLogLevel level, const QString &message) {
            emit maintenanceLogMessage(int(level), message);
        },
        this);
    m_maintenanceManager->runStartupMaintenance();

    m_apiContext = std::make_unique<ApiContext>(m_database, m_tokenHelper);
    m_userOptionsService = std::make_unique<UserOptionsService>(m_database);
    m_userOptionsController = std::make_unique<UserOptionsController>(
        *m_apiContext, *m_userOptionsService);
    m_imageProcessor = std::make_unique<ImageProcessor>(uploadRoot);
    m_attachmentDao = std::make_unique<AttachmentDao>(m_database);
    m_attachmentService = std::make_unique<AttachmentService>(
        *m_attachmentDao, *m_imageProcessor, [this]() {
            const ServerConfig config = configuration();
            ImageProcessingOptions options;
            options.maximumWidth = config.maxImageWidth;
            options.keepOriginal = config.keepOriginal;
            return options;
        });
    m_attachmentController = std::make_unique<AttachmentController>(
        *m_apiContext, *m_attachmentService);
    m_issueDao = std::make_unique<IssueDao>(m_database);
    m_notificationDao = std::make_unique<NotificationDao>(m_database);
    m_notificationManager = std::make_unique<NotificationManager>(
        m_database, *m_notificationDao, *m_issueDao,
        [this](const QList<NotificationRecord> &created,
               const QList<qint64> &changedRecipients) {
            for (const NotificationRecord &record : created) {
                emit notificationCreated(record.id,
                                         record.recipientId,
                                         record.relatedId,
                                         record.type,
                                         record.title,
                                         record.content);
            }
            for (qint64 recipientId : changedRecipients) {
                emit notificationCountChanged(recipientId);
            }
        });
    m_notificationController = std::make_unique<NotificationController>(
        *m_apiContext, *m_notificationManager);
    m_blockDao = std::make_unique<BlockDao>(m_database);
    m_blockService = std::make_unique<BlockService>(
        *m_blockDao, *m_attachmentService, *m_notificationManager);
    m_blockController = std::make_unique<BlockController>(
        *m_apiContext, *m_blockService);
    m_commentDao = std::make_unique<CommentDao>(m_database);
    m_commentService = std::make_unique<CommentService>(
        *m_commentDao, *m_attachmentService, *m_notificationManager);
    m_commentController = std::make_unique<CommentController>(
        *m_apiContext, *m_commentService);
    m_issueService = std::make_unique<IssueService>(
        *m_issueDao, *m_attachmentService, *m_notificationManager);
    m_issueController = std::make_unique<IssueController>(
        *m_apiContext, *m_issueService);

    registerRoutes();
    m_initialized = true;
    m_maintenanceManager->startDailyTimer();
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
    if (errorMessage) {
        errorMessage->clear();
    }
    return m_configurationProvider ? m_configurationProvider(errorMessage)
                                   : ServerConfig();
}

void HttpServer::setConfigurationProvider(
    std::function<ServerConfig(QString *)> provider)
{
    m_configurationProvider = std::move(provider);
}

void HttpServer::setMaintenanceConfigurationProvider(
    std::function<MaintenanceConfig()> provider)
{
    m_maintenanceConfigurationProvider = std::move(provider);
}

QList<UserSummary> HttpServer::accountSummaries(QString *errorMessage) const
{
    QList<UserSummary> summaries;
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!m_initialized) {
        if (errorMessage) {
            *errorMessage = tr("Account database is not initialized.");
        }
        return summaries;
    }

    const QList<UserRecord> records = m_database.users(errorMessage);
    summaries.reserve(records.size());
    for (const UserRecord &record : records) {
        summaries.append(record.toSummary());
    }
    return summaries;
}

bool HttpServer::createManagedUser(const QString &username,
                                   UserSummary *createdUser,
                                   QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!m_initialized) {
        if (errorMessage) {
            *errorMessage = tr("Account database is not initialized.");
        }
        return false;
    }

    const QString normalizedUsername = username.trimmed();
    if (!validUsername(normalizedUsername)) {
        if (errorMessage) {
            *errorMessage = tr("Provide a username of at most 64 characters without spaces.");
        }
        return false;
    }
    if (normalizedUsername.compare(QStringLiteral("admin"),
                                   Qt::CaseInsensitive) == 0) {
        if (errorMessage) {
            *errorMessage = tr("The admin username is reserved.");
        }
        return false;
    }
    if (m_database.usernameExists(normalizedUsername)) {
        if (errorMessage) {
            *errorMessage = tr("The username already exists.");
        }
        return false;
    }

    UserRecord user;
    QString databaseError;
    if (!m_database.createUser(
            normalizedUsername,
            PasswordHasher::hashPassword(QStringLiteral("123456")),
            QStringLiteral("user"),
            QString(),
            &user,
            &databaseError)) {
        if (errorMessage) {
            *errorMessage = databaseErrorMessage(databaseError);
        }
        return false;
    }

    if (createdUser) {
        *createdUser = user.toSummary();
    }
    emit accountsChanged();
    return true;
}

bool HttpServer::changeAdminPassword(const QString &currentPassword,
                                     const QString &newPassword,
                                     QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!m_initialized) {
        if (errorMessage) {
            *errorMessage = tr("Account database is not initialized.");
        }
        return false;
    }
    if (newPassword.size() < 8 || newPassword.size() > 256) {
        if (errorMessage) {
            *errorMessage = tr("The new password must contain 8 to 256 characters.");
        }
        return false;
    }

    QString databaseError;
    const auto admin = m_database.userByUsername(
        QStringLiteral("admin"), &databaseError);
    if (!databaseError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = databaseErrorMessage(databaseError);
        }
        return false;
    }
    if (!admin) {
        if (errorMessage) {
            *errorMessage = tr("The admin account was not found.");
        }
        return false;
    }
    if (!PasswordHasher::verifyPassword(currentPassword, admin->passwordHash)) {
        if (errorMessage) {
            *errorMessage = tr("The current password is incorrect.");
        }
        return false;
    }

    QByteArray newTokenSecret;
    if (!m_database.updatePasswordAndTokenSecret(
            admin->id,
            PasswordHasher::hashPassword(newPassword),
            &newTokenSecret,
            &databaseError)) {
        if (errorMessage) {
            *errorMessage = databaseErrorMessage(databaseError);
        }
        return false;
    }

    m_tokenHelper.setSecret(newTokenSecret);
    emit accountsChanged();
    return true;
}

bool HttpServer::localAdminUnreadCount(qint64 *count,
                                       QString *errorMessage) const
{
    if (!m_notificationManager) {
        if (errorMessage) {
            *errorMessage = tr("Notification service is not initialized.");
        }
        return false;
    }
    return m_notificationManager->localAdminUnreadCount(count, errorMessage);
}

bool HttpServer::markAllLocalAdminNotificationsRead(
    qint64 *deletedCount, QString *errorMessage)
{
    if (!m_notificationManager) {
        if (errorMessage) {
            *errorMessage = tr("Notification service is not initialized.");
        }
        return false;
    }
    return m_notificationManager->markAllLocalAdminNotificationsRead(
        deletedCount, errorMessage);
}

bool HttpServer::issueExists(qint64 issueId, QString *errorMessage) const
{
    if (!m_notificationManager) {
        if (errorMessage) {
            *errorMessage = tr("Notification service is not initialized.");
        }
        return false;
    }
    return m_notificationManager->issueExists(issueId, errorMessage);
}

bool HttpServer::isLocalAdminRecipient(qint64 userId,
                                       QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const auto admin = m_database.userByUsername(
        QStringLiteral("admin"), errorMessage);
    return admin && admin->id == userId;
}

bool HttpServer::startServer(const QString &bindAddress, quint16 port)
{
    m_shuttingDown = false;
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

    ServerConfig config = configuration();
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

void HttpServer::shutdown()
{
    m_shuttingDown = true;
    if (m_maintenanceManager) {
        m_maintenanceManager->stopDailyTimer();
    }
    if (m_tcpServer) {
        m_tcpServer->close();
    }
    m_bindAddress.clear();
    m_port = 0;
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
    if (config.maxImageWidth < 320 || config.maxImageWidth > 16383) {
        if (errorMessage) {
            *errorMessage = tr("Maximum image width must be between 320 and 16383.");
        }
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

    m_userOptionsController->registerRoutes(m_httpServer);
    m_blockController->registerRoutes(m_httpServer);
    m_issueController->registerRoutes(m_httpServer);
    m_commentController->registerRoutes(m_httpServer);
    m_attachmentController->registerRoutes(m_httpServer);
    m_notificationController->registerRoutes(m_httpServer);

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
                {QStringLiteral("token"),
                 m_tokenHelper.issue(user->id, user->role, user->tokenVersion)},
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
        QStringLiteral("/api/auth/password"), Method::Put,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, false);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            if (authorization.user.username == QStringLiteral("admin")) {
                return errorResponse(StatusCode::Forbidden,
                                     QStringLiteral("admin_password_desktop_only"),
                                     tr("The admin password can only be changed in the desktop application."));
            }

            QJsonObject body;
            QString parseError;
            if (!parseJsonObject(request, &body, &parseError)) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_json"), parseError);
            }
            const QString newPassword =
                body.value(QStringLiteral("new_password")).toString();
            if (newPassword.size() < 8 || newPassword.size() > 256) {
                return errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("invalid_password"),
                    tr("The new password must contain 8 to 256 characters."));
            }

            int newTokenVersion = -1;
            QString databaseError;
            if (!m_database.updatePasswordAndIncrementTokenVersion(
                    authorization.user.id,
                    PasswordHasher::hashPassword(newPassword),
                    &newTokenVersion,
                    &databaseError)) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }

            emit accountsChanged();
            return successResponse({
                {QStringLiteral("token"),
                 m_tokenHelper.issue(authorization.user.id,
                                     authorization.user.role,
                                     newTokenVersion)},
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
        [this](const QHttpServerRequest &) {
            return errorResponse(
                StatusCode::MethodNotAllowed,
                QStringLiteral("registration_managed_by_application"),
                tr("User registration is managed by the desktop application."));
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
            if (body.contains(QStringLiteral("password"))) {
                return errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("password_change_not_allowed"),
                    tr("Use the current-user password endpoint to change a password."));
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
            if (!validUsername(username) || !validRole(role)
                || displayName.size() > 128) {
                return errorResponse(StatusCode::BadRequest,
                                     QStringLiteral("invalid_user"),
                                     tr("The user fields are invalid."));
            }
            if (existing->username == QStringLiteral("admin")
                && (username != QStringLiteral("admin")
                    || role != QStringLiteral("admin"))) {
                return errorResponse(
                    StatusCode::Conflict,
                    QStringLiteral("protected_admin"),
                    tr("The fixed admin account cannot be renamed or demoted."));
            }
            if (existing->username != QStringLiteral("admin")
                && username.compare(QStringLiteral("admin"),
                                    Qt::CaseInsensitive) == 0) {
                return errorResponse(StatusCode::Conflict,
                                     QStringLiteral("reserved_username"),
                                     tr("The admin username is reserved."));
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
            if (!m_database.updateUser(id, username, role, displayName,
                                       QString(), &updated, &databaseError)) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("database_error"),
                                     databaseErrorMessage(databaseError));
            }
            emit accountsChanged();
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
            if (existing->username == QStringLiteral("admin")) {
                return errorResponse(StatusCode::Conflict,
                                     QStringLiteral("protected_admin"),
                                     tr("The fixed admin account cannot be deleted."));
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
            emit accountsChanged();
            return successResponse({{QStringLiteral("deleted_id"), id}});
        });

    m_httpServer.route(
        QStringLiteral("/api/server/config"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const AuthorizationResult authorization = authorize(request, true);
            if (!authorization.authorized) {
                return authorizationError(authorization);
            }
            QString configurationError;
            const ServerConfig config = configuration(&configurationError);
            if (!configurationError.isEmpty()) {
                return errorResponse(StatusCode::InternalServerError,
                                     QStringLiteral("configuration_error"),
                                     configurationError);
            }
            return successResponse({{QStringLiteral("config"), config.toJson()}});
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
    if (state == State::Stopped && m_maintenanceManager && !m_shuttingDown) {
        m_maintenanceManager->onServerStopped();
    }
}

HttpServer::AuthorizationResult HttpServer::authorize(
    const QHttpServerRequest &request, bool adminRequired) const
{
    return m_apiContext->authorize(request, adminRequired);
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
    const ServerConfig config = configuration();
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
    return ApiContext::successResponse(data, status);
}

QHttpServerResponse HttpServer::errorResponse(StatusCode status,
                                              const QString &code,
                                              const QString &message)
{
    return ApiContext::errorResponse(status, code, message);
}

QHttpServerResponse HttpServer::authorizationError(
    const AuthorizationResult &authorization)
{
    return ApiContext::authorizationError(authorization);
}

bool HttpServer::parseJsonObject(const QHttpServerRequest &request,
                                 QJsonObject *object,
                                 QString *errorMessage)
{
    return ApiContext::parseJsonObject(request, object, errorMessage);
}

void HttpServer::addCorsHeaders(QHttpServerResponse *response)
{
    ApiContext::addCorsHeaders(response);
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
