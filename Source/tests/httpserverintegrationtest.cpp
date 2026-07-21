#include "httpserver.h"
#include "passwordhasher.h"
#include "tokenhelper.h"

#include <QEventLoop>
#include <QHttpHeaders>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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
    void healthAndCors();
    void authentication();
    void userCrudAndLastAdminProtection();
    void configurationAndRestart();
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
    QVERIFY(m_server->startServer(QStringLiteral("127.0.0.1"), m_port));
}

void HttpServerIntegrationTest::passwordAndTokenHelpers()
{
    const QString encoded = PasswordHasher::hashPassword(QStringLiteral("correct-password"));
    QVERIFY(PasswordHasher::verifyPassword(QStringLiteral("correct-password"), encoded));
    QVERIFY(!PasswordHasher::verifyPassword(QStringLiteral("wrong-password"), encoded));

    TokenHelper helper(QByteArrayLiteral("integration-test-secret"));
    const QString token = helper.issue(42, QStringLiteral("admin"), 60);
    const TokenHelper::Claims claims = helper.validate(token);
    QVERIFY(claims.valid);
    QCOMPARE(claims.userId, qint64(42));
    QCOMPARE(claims.role, QStringLiteral("admin"));
    QVERIFY(!helper.validate(helper.issue(42, QStringLiteral("admin"), -1)).valid);
    QVERIFY(!helper.validate(token + QStringLiteral("x")).valid);
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
    const Reply created = request(
        "POST", QStringLiteral("/api/users"),
        {{QStringLiteral("username"), QStringLiteral("operator")},
         {QStringLiteral("password"), QStringLiteral("operator-password")},
         {QStringLiteral("role"), QStringLiteral("user")},
         {QStringLiteral("display_name"), QStringLiteral("Operator")}},
        m_token);
    QCOMPARE(created.status, 201);
    const qint64 userId = created.json().value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("user")).toObject()
                              .value(QStringLiteral("id")).toInteger();
    QVERIFY(userId > 0);

    const Reply userLogin = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), QStringLiteral("operator")},
         {QStringLiteral("password"), QStringLiteral("operator-password")}});
    QCOMPARE(userLogin.status, 200);
    const QString userToken = userLogin.json().value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("token")).toString();
    QVERIFY(!userToken.isEmpty());
    QCOMPARE(request("GET", QStringLiteral("/api/users"), {}, userToken).status,
             403);

    QCOMPARE(request("POST", QStringLiteral("/api/users"),
                     {{QStringLiteral("username"), QStringLiteral("operator")},
                      {QStringLiteral("password"), QStringLiteral("another-password")},
                      {QStringLiteral("role"), QStringLiteral("user")}},
                     m_token).status,
             409);

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

    QCOMPARE(request("DELETE", QStringLiteral("/api/users/1"), {}, m_token).status,
             409);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(userId),
                     {}, m_token).status,
             200);
}

void HttpServerIntegrationTest::configurationAndRestart()
{
    const QJsonObject config = {
        {QStringLiteral("server_interface"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("server_port"), int(m_port)},
        {QStringLiteral("auto_start"), true},
        {QStringLiteral("keep_original"), false},
        {QStringLiteral("max_image_width"), 1600}
    };
    QCOMPARE(request("PUT", QStringLiteral("/api/server/config"),
                     config, m_token).status,
             200);

    const Reply read = request("GET", QStringLiteral("/api/server/config"), {}, m_token);
    QCOMPARE(read.status, 200);
    QCOMPARE(read.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("config")).toObject()
                 .value(QStringLiteral("max_image_width")).toInt(),
             1600);

    QSignalSpy runningSpy(m_server.get(), &HttpServer::stateChanged);
    QCOMPARE(request("POST", QStringLiteral("/api/server/restart"), {}, m_token).status,
             202);
    QTRY_VERIFY_WITH_TIMEOUT(runningSpy.count() >= 3, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(m_server->isRunning(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(request("GET", QStringLiteral("/api/health")).status,
                              200, 3000);
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
