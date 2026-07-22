#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "databasemanager.h"
#include "tokenhelper.h"

#include <QHttpServer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QHttpServerRequest;
class QHttpServerResponse;

struct ServerConfig
{
    QString serverInterface {QStringLiteral("0.0.0.0")};
    quint16 serverPort {8080};
    bool autoStart {true};
    bool keepOriginal {false};
    int maxImageWidth {1920};

    QJsonObject toJson() const;
};

class HttpServer : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Stopping,
        Error
    };
    Q_ENUM(State)

    explicit HttpServer(const QString &databasePath, QObject *parent = nullptr);
    ~HttpServer() override;

    bool initialize(QString *errorMessage = nullptr);
    bool databaseWasCreated() const;
    bool isRunning() const;
    ServerConfig configuration(QString *errorMessage = nullptr) const;
    void setConfigurationProvider(std::function<ServerConfig(QString *)> provider);
    QList<UserSummary> accountSummaries(QString *errorMessage = nullptr) const;
    bool createManagedUser(const QString &username,
                           UserSummary *createdUser = nullptr,
                           QString *errorMessage = nullptr);
    bool changeAdminPassword(const QString &currentPassword,
                             const QString &newPassword,
                             QString *errorMessage = nullptr);

public slots:
    bool startServer(const QString &bindAddress, quint16 port);
    void stopServer();
    void restartServer(const QString &bindAddress, quint16 port);
    bool updateConfiguration(const ServerConfig &config, QString *errorMessage = nullptr);
    void testReachability(const QUrl &url);

signals:
    void stateChanged(HttpServer::State state, const QString &detail);
    void reachabilityTested(bool reachable, const QString &detail);
    void bootstrapAdminCreated(const QString &username, const QString &password);
    void accountsChanged();
    void configurationChanged(const QString &serverInterface,
                              quint16 serverPort,
                              bool autoStart,
                              bool keepOriginal,
                              int maxImageWidth);

private:
    struct AuthorizationResult {
        bool authorized {false};
        UserRecord user;
        QString code;
        QString message;
    };

    void registerRoutes();
    void setState(State state, const QString &detail = QString());
    AuthorizationResult authorize(const QHttpServerRequest &request,
                                  bool adminRequired) const;
    QHttpServerResponse staticFrontendResponse() const;
    QHttpServerResponse healthResponse() const;

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
    static bool validRole(const QString &role);
    static bool validUsername(const QString &username);
    DatabaseManager m_database;
    TokenHelper m_tokenHelper;
    QHttpServer m_httpServer;
    QTcpServer *m_tcpServer {nullptr};
    QNetworkAccessManager *m_networkManager {nullptr};
    State m_state {State::Stopped};
    QString m_bindAddress;
    quint16 m_port {0};
    bool m_initialized {false};
    std::function<ServerConfig(QString *)> m_configurationProvider;
};

#endif // HTTPSERVER_H
