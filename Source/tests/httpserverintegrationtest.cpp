#include "httpserver.h"
#include "passwordhasher.h"
#include "tokenhelper.h"

#include <QEventLoop>
#include <QDateTime>
#include <QHttpHeaders>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageAuthenticationCode>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <memory>

class HttpServerIntegrationTest : public QObject
{
    Q_OBJECT

private:
    struct Reply {
        int status {0};
        QByteArray body;
        QByteArray allowOrigin;
        QNetworkReply::NetworkError error {QNetworkReply::NoError};

        QJsonObject json() const
        {
            return QJsonDocument::fromJson(body).object();
        }
    };

private slots:
    void initTestCase();
    void passwordAndTokenHelpers();
    void legacyDatabaseMigration();
    void healthAndCors();
    void authentication();
    void userCrudAndLastAdminProtection();
    void userPasswordChangeAndTokenVersion();
    void accountSummaryAndAdminPasswordChange();
    void configurationReadOnly();
    void cleanupTestCase();

private:
    Reply request(const QByteArray &method,
                  const QString &path,
                  const QJsonObject &body = QJsonObject(),
                  const QString &token = QString());
    Reply requestRaw(const QByteArray &method,
                     const QString &path,
                     const QByteArray &payload,
                     const QString &token = QString());

    QTemporaryDir m_temporaryDirectory;
    std::unique_ptr<HttpServer> m_server;
    QNetworkAccessManager m_networkManager;
    QString m_adminPassword;
    QString m_token;
    quint16 m_port {0};
};

void HttpServerIntegrationTest::initTestCase()
{
    QVERIFY(m_temporaryDirectory.isValid());
    m_server = std::make_unique<HttpServer>(
        m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db")));
    connect(m_server.get(), &HttpServer::bootstrapAdminCreated,
            this, [this](const QString &username, const QString &password) {
                QCOMPARE(username, QStringLiteral("admin"));
                m_adminPassword = password;
            });

    QString error;
    QVERIFY2(m_server->initialize(&error), qPrintable(error));
    QVERIFY(!m_adminPassword.isEmpty());

    QTcpServer portProbe;
    QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
    m_port = portProbe.serverPort();
    portProbe.close();
    m_server->setConfigurationProvider([this](QString *errorMessage) {
        if (errorMessage) {
            errorMessage->clear();
        }
        ServerConfig config;
        config.serverInterface = QStringLiteral("127.0.0.1");
        config.serverPort = m_port;
        config.maxImageWidth = 1600;
        return config;
    });
    QVERIFY(m_server->startServer(QStringLiteral("127.0.0.1"), m_port));
}

void HttpServerIntegrationTest::passwordAndTokenHelpers()
{
    const QString encoded = PasswordHasher::hashPassword(QStringLiteral("correct-password"));
    QVERIFY(PasswordHasher::verifyPassword(QStringLiteral("correct-password"), encoded));
    QVERIFY(!PasswordHasher::verifyPassword(QStringLiteral("wrong-password"), encoded));

    const QByteArray secret = QByteArrayLiteral("integration-test-secret");
    TokenHelper helper(secret);
    const QString token = helper.issue(42, QStringLiteral("admin"), 7, 60);
    const TokenHelper::Claims claims = helper.validate(token);
    QVERIFY(claims.valid);
    QCOMPARE(claims.userId, qint64(42));
    QCOMPARE(claims.role, QStringLiteral("admin"));
    QCOMPARE(claims.tokenVersion, 7);
    QVERIFY(!helper.validate(
        helper.issue(42, QStringLiteral("admin"), 0, -1)).valid);
    QVERIFY(!helper.validate(token + QStringLiteral("x")).valid);

    const QByteArray legacyClaims = QByteArrayLiteral("42:admin:")
        + QByteArray::number(QDateTime::currentSecsSinceEpoch() + 60);
    const QByteArray legacyPayload = legacyClaims.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray legacySignature = QMessageAuthenticationCode::hash(
        legacyPayload, secret, QCryptographicHash::Sha256).toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const TokenHelper::Claims legacy = helper.validate(
        QString::fromLatin1(legacyPayload + ':' + legacySignature));
    QVERIFY(legacy.valid);
    QCOMPARE(legacy.tokenVersion, 0);
}

void HttpServerIntegrationTest::legacyDatabaseMigration()
{
    QTemporaryDir legacyDirectory;
    QVERIFY(legacyDirectory.isValid());
    const QString databasePath =
        legacyDirectory.filePath(QStringLiteral("legacy.db"));
    const QString connectionName = QStringLiteral("legacy_schema_test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT NOT NULL UNIQUE,"
            "password TEXT NOT NULL,"
            "role TEXT NOT NULL,"
            "display_name TEXT,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE system_config ("
            "key TEXT PRIMARY KEY, value TEXT NOT NULL)")));
        query.prepare(QStringLiteral(
            "INSERT INTO system_config(key, value) VALUES(?, ?)"));
        query.addBindValue(QStringLiteral("server_port"));
        query.addBindValue(QStringLiteral("9999"));
        QVERIFY(query.exec());
        query.prepare(QStringLiteral(
            "INSERT INTO system_config(key, value) VALUES(?, ?)"));
        query.addBindValue(QStringLiteral("token_secret"));
        query.addBindValue(QStringLiteral("legacy-secret"));
        QVERIFY(query.exec());
        query.prepare(QStringLiteral(
            "INSERT INTO users(username, password, role, display_name) "
            "VALUES(?, ?, ?, ?)"));
        query.addBindValue(QStringLiteral("legacy"));
        query.addBindValue(PasswordHasher::hashPassword(QStringLiteral("123456")));
        query.addBindValue(QStringLiteral("user"));
        query.addBindValue(QStringLiteral("Legacy User"));
        QVERIFY(query.exec());
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    HttpServer legacyServer(databasePath);
    QString errorMessage;
    QVERIFY2(legacyServer.initialize(&errorMessage), qPrintable(errorMessage));
    const QList<UserSummary> accounts =
        legacyServer.accountSummaries(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(accounts.size(), 1);
    QCOMPARE(accounts.first().username, QStringLiteral("legacy"));
    QVERIFY(accounts.first().usesDefaultPassword);

    legacyServer.stopServer();
    QSqlDatabase migratedDatabase = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("legacy_migration_check"));
    migratedDatabase.setDatabaseName(databasePath);
    QVERIFY(migratedDatabase.open());
    QSqlQuery migratedQuery(migratedDatabase);
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type = 'table' "
        "AND name = 'system_config'")));
    QVERIFY(!migratedQuery.next());
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT value FROM security_state WHERE key = 'token_secret'")));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(0).toString(), QStringLiteral("legacy-secret"));
    migratedDatabase.close();
    QSqlDatabase::removeDatabase(QStringLiteral("legacy_migration_check"));
}

void HttpServerIntegrationTest::healthAndCors()
{
    const Reply health = request("GET", QStringLiteral("/api/health"));
    QCOMPARE(health.status, 200);
    QCOMPARE(health.allowOrigin, QByteArrayLiteral("*"));
    QCOMPARE(health.json().value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(health.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("running"));

    const Reply options = request("OPTIONS", QStringLiteral("/api/users"));
    QCOMPARE(options.status, 204);
    QCOMPARE(options.allowOrigin, QByteArrayLiteral("*"));

    QCOMPARE(requestRaw("POST", QStringLiteral("/api/auth/login"), "{").status,
             400);
    QCOMPARE(request("GET", QStringLiteral("/api/unknown")).status, 404);
}

void HttpServerIntegrationTest::authentication()
{
    const Reply rejected = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("admin")},
         {QStringLiteral("password"), QStringLiteral("incorrect")}});
    QCOMPARE(rejected.status, 401);

    const Reply login = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("admin")},
         {QStringLiteral("password"), m_adminPassword}});
    QCOMPARE(login.status, 200);
    m_token = login.json().value(QStringLiteral("data")).toObject()
                  .value(QStringLiteral("token")).toString();
    QVERIFY(!m_token.isEmpty());

    QCOMPARE(request("GET", QStringLiteral("/api/auth/me")).status, 401);
    const Reply me = request("GET", QStringLiteral("/api/auth/me"), {}, m_token);
    QCOMPARE(me.status, 200);
    QCOMPARE(me.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("user")).toObject()
                 .value(QStringLiteral("username")).toString(),
             QStringLiteral("admin"));
}

void HttpServerIntegrationTest::userCrudAndLastAdminProtection()
{
    QSignalSpy accountsChangedSpy(m_server.get(), &HttpServer::accountsChanged);
    QString errorMessage;
    UserSummary created;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("operator"), &created, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(accountsChangedSpy.count(), 1);
    const qint64 userId = created.id;
    QVERIFY(userId > 0);
    QCOMPARE(created.role, QStringLiteral("user"));
    QVERIFY(created.displayName.isEmpty());
    QVERIFY(created.usesDefaultPassword);

    UserSummary duplicate;
    QVERIFY(!m_server->createManagedUser(
        QStringLiteral("Operator"), &duplicate, &errorMessage));
    QVERIFY(!m_server->createManagedUser(
        QStringLiteral("Admin"), &duplicate, &errorMessage));
    QCOMPARE(accountsChangedSpy.count(), 1);

    const Reply userLogin = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("operator")},
         {QStringLiteral("password"), QStringLiteral("123456")}});
    QCOMPARE(userLogin.status, 200);
    const QString userToken = userLogin.json().value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("token")).toString();
    QVERIFY(!userToken.isEmpty());
    QCOMPARE(request("GET", QStringLiteral("/api/users"), {}, userToken).status,
             403);

    QCOMPARE(request("POST", QStringLiteral("/api/users"), {}, m_token).status,
             405);

    const Reply list = request("GET", QStringLiteral("/api/users"), {}, m_token);
    QCOMPARE(list.status, 200);
    QCOMPARE(list.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("users")).toArray().size(),
             2);

    const Reply updated = request(
        "PUT", QStringLiteral("/api/users/%1").arg(userId),
        {{QStringLiteral("username"), QStringLiteral("operator")},
         {QStringLiteral("role"), QStringLiteral("guest")},
         {QStringLiteral("display_name"), QStringLiteral("Read Only")}},
        m_token);
    QCOMPARE(updated.status, 200);
    QCOMPARE(accountsChangedSpy.count(), 2);

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(userId),
        {{QStringLiteral("password"), QStringLiteral("another-password")}},
        m_token).status,
        400);
    QCOMPARE(accountsChangedSpy.count(), 2);

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/1"),
        {{QStringLiteral("username"), QStringLiteral("root")},
         {QStringLiteral("role"), QStringLiteral("admin")}},
        m_token).status,
        409);

    QCOMPARE(request("DELETE", QStringLiteral("/api/users/1"), {}, m_token).status,
             409);
    QCOMPARE(accountsChangedSpy.count(), 2);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(userId),
                     {}, m_token).status,
             200);
    QCOMPARE(accountsChangedSpy.count(), 3);
}

void HttpServerIntegrationTest::userPasswordChangeAndTokenVersion()
{
    QString errorMessage;
    UserSummary created;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("operator"), &created, &errorMessage),
             qPrintable(errorMessage));

    const auto loginOperator = [this](const QString &password) {
        return request(
            "POST", QStringLiteral("/api/auth/login"),
            {{QStringLiteral("username"), QStringLiteral("operator")},
             {QStringLiteral("password"), password}});
    };
    const Reply firstLogin = loginOperator(QStringLiteral("123456"));
    const Reply secondLogin = loginOperator(QStringLiteral("123456"));
    QCOMPARE(firstLogin.status, 200);
    QCOMPARE(secondLogin.status, 200);
    const QString firstToken =
        firstLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    const QString secondToken =
        secondLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!firstToken.isEmpty());
    QVERIFY(!secondToken.isEmpty());

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/auth/password"),
        {{QStringLiteral("new_password"), QStringLiteral("short")}},
        firstToken).status,
        400);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, firstToken).status,
             200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/auth/password"),
        {{QStringLiteral("new_password"), QStringLiteral("admin-new-password")}},
        m_token).status,
        403);

    QSignalSpy accountsChangedSpy(m_server.get(), &HttpServer::accountsChanged);
    const QString newPassword = QStringLiteral("operator-new-password");
    const Reply changed = request(
        "PUT", QStringLiteral("/api/auth/password"),
        {{QStringLiteral("new_password"), newPassword}},
        firstToken);
    QCOMPARE(changed.status, 200);
    QCOMPARE(accountsChangedSpy.count(), 1);
    const QString replacementToken =
        changed.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!replacementToken.isEmpty());

    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, firstToken).status,
             401);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, secondToken).status,
             401);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, replacementToken).status,
             200);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, m_token).status,
             200);
    QCOMPARE(loginOperator(QStringLiteral("123456")).status, 401);
    QCOMPARE(loginOperator(newPassword).status, 200);

    const QList<UserSummary> accounts = m_server->accountSummaries(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    const auto operatorIt = std::find_if(
        accounts.cbegin(), accounts.cend(), [](const UserSummary &account) {
            return account.username == QStringLiteral("operator");
        });
    QVERIFY(operatorIt != accounts.cend());
    QVERIFY(!operatorIt->usesDefaultPassword);

    QCOMPARE(request("DELETE",
                     QStringLiteral("/api/users/%1").arg(created.id),
                     {}, m_token).status,
             200);
    QCOMPARE(accountsChangedSpy.count(), 2);
}

void HttpServerIntegrationTest::accountSummaryAndAdminPasswordChange()
{
    QString errorMessage;
    UserSummary created;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("operator"), &created, &errorMessage),
             qPrintable(errorMessage));
    const QString operatorPassword = QStringLiteral("123456");
    const qint64 operatorId = created.id;
    QVERIFY(operatorId > 0);

    const Reply operatorLogin = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("operator")},
         {QStringLiteral("password"), operatorPassword}});
    QCOMPARE(operatorLogin.status, 200);
    const QString operatorToken =
        operatorLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!operatorToken.isEmpty());

    const QList<UserSummary> accounts = m_server->accountSummaries(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(accounts.size(), 2);
    QCOMPARE(accounts.at(0).username, QStringLiteral("admin"));
    QCOMPARE(accounts.at(1).username, QStringLiteral("operator"));
    QVERIFY(accounts.at(1).displayName.isEmpty());
    QVERIFY(accounts.at(1).usesDefaultPassword);

    QSignalSpy accountsChangedSpy(m_server.get(), &HttpServer::accountsChanged);
    QVERIFY(!m_server->changeAdminPassword(
        QStringLiteral("incorrect-current-password"),
        QStringLiteral("replacement-password"),
        &errorMessage));
    QCOMPARE(accountsChangedSpy.count(), 0);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, m_token).status,
             200);

    errorMessage.clear();
    QVERIFY(!m_server->changeAdminPassword(
        m_adminPassword, QStringLiteral("short"), &errorMessage));
    QCOMPARE(accountsChangedSpy.count(), 0);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, operatorToken).status,
             200);

    const QString newAdminPassword = QStringLiteral("replacement-password");
    errorMessage.clear();
    QVERIFY2(m_server->changeAdminPassword(
                 m_adminPassword, newAdminPassword, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(accountsChangedSpy.count(), 1);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, m_token).status,
             401);
    QCOMPARE(request("GET", QStringLiteral("/api/auth/me"), {}, operatorToken).status,
             401);

    QCOMPARE(request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("admin")},
         {QStringLiteral("password"), m_adminPassword}}).status,
        401);
    const Reply newAdminLogin = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("admin")},
         {QStringLiteral("password"), newAdminPassword}});
    QCOMPARE(newAdminLogin.status, 200);
    m_token = newAdminLogin.json().value(QStringLiteral("data")).toObject()
                  .value(QStringLiteral("token")).toString();
    QVERIFY(!m_token.isEmpty());
    m_adminPassword = newAdminPassword;

    QCOMPARE(request("DELETE",
                     QStringLiteral("/api/users/%1").arg(operatorId),
                     {}, m_token).status,
             200);
}

void HttpServerIntegrationTest::configurationReadOnly()
{
    const QJsonObject config = {
        {QStringLiteral("server_interface"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("server_port"), int(m_port)},
        {QStringLiteral("auto_start"), true},
        {QStringLiteral("keep_original"), false},
        {QStringLiteral("max_image_width"), 1600}
    };
    const Reply removedUpdate = request("PUT", QStringLiteral("/api/server/config"),
                                        config, m_token);
    QCOMPARE(removedUpdate.status, 404);

    const Reply read = request("GET", QStringLiteral("/api/server/config"), {}, m_token);
    QCOMPARE(read.status, 200);
    QCOMPARE(read.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("config")).toObject()
                 .value(QStringLiteral("max_image_width")).toInt(),
             1600);

    QCOMPARE(request("POST", QStringLiteral("/api/server/restart"), {}, m_token).status,
             404);
    QVERIFY(m_server->isRunning());
}

void HttpServerIntegrationTest::cleanupTestCase()
{
    if (m_server) {
        m_server->stopServer();
        m_server.reset();
    }
}

HttpServerIntegrationTest::Reply HttpServerIntegrationTest::request(
    const QByteArray &method,
    const QString &path,
    const QJsonObject &body,
    const QString &token)
{
    const QByteArray payload = body.isEmpty()
        ? QByteArray()
        : QJsonDocument(body).toJson(QJsonDocument::Compact);
    return requestRaw(method, path, payload, token);
}

HttpServerIntegrationTest::Reply HttpServerIntegrationTest::requestRaw(
    const QByteArray &method,
    const QString &path,
    const QByteArray &payload,
    const QString &token)
{
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                                     .arg(m_port).arg(path)));
    request.setRawHeader("Accept", "application/json");
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    }

    QNetworkReply *networkReply = nullptr;
    if (!payload.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    }
    if (method == "GET") {
        networkReply = m_networkManager.get(request);
    } else if (method == "POST") {
        networkReply = m_networkManager.post(request, payload);
    } else if (method == "PUT") {
        networkReply = m_networkManager.put(request, payload);
    } else if (method == "DELETE") {
        networkReply = m_networkManager.deleteResource(request);
    } else {
        networkReply = m_networkManager.sendCustomRequest(request, method, payload);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, networkReply, &QNetworkReply::abort);
    connect(networkReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();

    Reply reply;
    reply.status = networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply.body = networkReply->readAll();
    reply.allowOrigin = networkReply->rawHeader("Access-Control-Allow-Origin");
    reply.error = networkReply->error();
    networkReply->deleteLater();
    return reply;
}

QTEST_GUILESS_MAIN(HttpServerIntegrationTest)

#include "httpserverintegrationtest.moc"
