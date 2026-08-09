#include "httpserver.h"
#include "issues/issueidentifier.h"
#include "passwordhasher.h"
#include "tokenhelper.h"

#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QDateTime>
#include <QFileInfo>
#include <QHttpHeaders>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageAuthenticationCode>
#include <QPair>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

class HttpServerIntegrationTest : public QObject
{
    Q_OBJECT

private:
    struct Reply {
        int status {0};
        QByteArray body;
        QByteArray allowOrigin;
        QByteArray contentType;
        QNetworkReply::NetworkError error {QNetworkReply::NoError};

        QJsonObject json() const
        {
            return QJsonDocument::fromJson(body).object();
        }
    };

private slots:
    void initTestCase();
    void taskIdentifiers();
    void passwordAndTokenHelpers();
    void legacyDatabaseMigration();
    void targetNotificationSchemaPurgesReadRows();
    void healthAndCors();
    void authentication();
    void userOptionsAndBlockCrud();
    void issueCrudFilteringAndPermissions();
    void commentReadAndCreatePermissions();
    void notificationApiAndBusinessEvents();
    void commentImagesAndCascadeCleanup();
    void attachmentUploadReadAndDeletePermissions();
    void legacyAttachmentTestRemoved();
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
                     const QString &token = QString(),
                     const QByteArray &contentType = QByteArrayLiteral("application/json"));
    Reply requestMultipart(const QString &path,
                           const QString &filename,
                           const QByteArray &contentType,
                           const QByteArray &fileData,
                           const QString &token = QString());
    Reply requestCommentMultipart(
        const QString &path,
        const QString &content,
        const QList<QPair<QString, QByteArray>> &files,
        const QString &token = QString());

    QTemporaryDir m_temporaryDirectory;
    std::unique_ptr<HttpServer> m_server;
    QNetworkAccessManager m_networkManager;
    QString m_adminPassword;
    QString m_token;
    quint16 m_port {0};
};

void HttpServerIntegrationTest::taskIdentifiers()
{
    QCOMPARE(IssueIdentifier::format(42), QStringLiteral("T42"));
    QCOMPARE(IssueIdentifier::format(0), QString());

    qint64 issueId = 0;
    QVERIFY(IssueIdentifier::parse(QStringLiteral("T42"), &issueId));
    QCOMPARE(issueId, qint64(42));
    QVERIFY(IssueIdentifier::parse(QStringLiteral("t42"), &issueId));
    QCOMPARE(issueId, qint64(42));
    QVERIFY(IssueIdentifier::parse(QStringLiteral("42"), &issueId));
    QCOMPARE(issueId, qint64(42));
    QVERIFY(!IssueIdentifier::parse(QStringLiteral("T042"), &issueId));
    QVERIFY(!IssueIdentifier::parse(QStringLiteral("T0"), &issueId));
    QVERIFY(!IssueIdentifier::parse(QStringLiteral("T-1"), &issueId));
    QVERIFY(!IssueIdentifier::parse(QStringLiteral("task42"), &issueId));
}

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
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE blocks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL, "
            "description TEXT, color TEXT DEFAULT '#3b82f6', "
            "sort_order INTEGER DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE issues ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, block_id INTEGER NOT NULL, "
            "title TEXT NOT NULL, description TEXT, status TEXT DEFAULT 'open', "
            "priority TEXT DEFAULT 'medium', reporter_id INTEGER NOT NULL, "
            "assignee_id INTEGER, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO blocks(id, title) VALUES(1, 'Legacy Block')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO issues(id, block_id, title, reporter_id) "
            "VALUES(1, 1, 'Legacy Issue', 1)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE notifications ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT NOT NULL, "
            "title TEXT NOT NULL, content TEXT, related_id INTEGER, "
            "sender_id INTEGER REFERENCES users(id), recipient_id INTEGER NOT NULL, "
            "is_read INTEGER DEFAULT 0, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE INDEX idx_notifications_unread "
            "ON notifications(recipient_id, is_read)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, related_id, sender_id, recipient_id) "
            "VALUES(1, 'issue_created', 'Legacy valid', 1, 1, 1)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, related_id, sender_id, recipient_id) "
            "VALUES(2, 'comment_added', 'Legacy sender removed', 1, 999, 1)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, related_id, sender_id, recipient_id) "
            "VALUES(3, 'status_changed', 'Legacy orphan', 999, 1, 1)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, related_id, sender_id, recipient_id, is_read) "
            "VALUES(4, 'status_changed', 'Legacy read', 1, 1, 1, 1)")));
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
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT id, sender_id FROM notifications ORDER BY id")));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(0).toLongLong(), qint64(1));
    QCOMPARE(migratedQuery.value(1).toLongLong(), qint64(1));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(0).toLongLong(), qint64(2));
    QVERIFY(migratedQuery.value(1).isNull());
    QVERIFY(!migratedQuery.next());

    bool relatedCascade = false;
    bool senderSetNull = false;
    bool recipientCascade = false;
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "PRAGMA foreign_key_list(notifications)")));
    while (migratedQuery.next()) {
        const QString column = migratedQuery.value(3).toString();
        const QString action = migratedQuery.value(6).toString().toUpper();
        relatedCascade = relatedCascade
            || (column == QStringLiteral("related_id")
                && action == QStringLiteral("CASCADE"));
        senderSetNull = senderSetNull
            || (column == QStringLiteral("sender_id")
                && action == QStringLiteral("SET NULL"));
        recipientCascade = recipientCascade
            || (column == QStringLiteral("recipient_id")
                && action == QStringLiteral("CASCADE"));
    }
    QVERIFY(relatedCascade);
    QVERIFY(senderSetNull);
    QVERIFY(recipientCascade);

    QStringList notificationIndexColumns;
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "PRAGMA index_info(idx_notifications_unread)")));
    while (migratedQuery.next()) {
        notificationIndexColumns.append(migratedQuery.value(2).toString());
    }
    QCOMPARE(notificationIndexColumns,
             QStringList({QStringLiteral("recipient_id"),
                          QStringLiteral("is_read"),
                          QStringLiteral("created_at"),
                          QStringLiteral("id")}));
    migratedDatabase.close();
    QSqlDatabase::removeDatabase(QStringLiteral("legacy_migration_check"));
}

void HttpServerIntegrationTest::targetNotificationSchemaPurgesReadRows()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString databasePath = temporary.filePath(QStringLiteral("target.db"));

    {
        DatabaseManager database(databasePath);
        QString errorMessage;
        QVERIFY2(database.initialize(&errorMessage), qPrintable(errorMessage));
        QSqlQuery query(database.connection());
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO blocks(id, title) VALUES(1, 'Target Block')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO issues(id, block_id, title, reporter_id) "
            "VALUES(1, 1, 'Target Issue', 1)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, related_id, sender_id, recipient_id, is_read) "
            "VALUES(1, 'issue_created', 'Unread', 1, 1, 1, 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, related_id, sender_id, recipient_id, is_read) "
            "VALUES(2, 'status_changed', 'Read', 1, 1, 1, 1)")));
    }

    DatabaseManager database(databasePath);
    QString errorMessage;
    QVERIFY2(database.initialize(&errorMessage), qPrintable(errorMessage));
    QSqlQuery query(database.connection());
    QVERIFY(query.exec(QStringLiteral(
        "SELECT id, is_read FROM notifications ORDER BY id")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toLongLong(), qint64(1));
    QCOMPARE(query.value(1).toInt(), 0);
    QVERIFY(!query.next());
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

void HttpServerIntegrationTest::userOptionsAndBlockCrud()
{
    QString errorMessage;
    UserSummary member;
    UserSummary guest;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("block_member"), &member, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("block_guest"), &guest, &errorMessage),
             qPrintable(errorMessage));

    const Reply guestUpdate = request(
        "PUT", QStringLiteral("/api/users/%1").arg(guest.id),
        {{QStringLiteral("username"), guest.username},
         {QStringLiteral("role"), QStringLiteral("guest")},
         {QStringLiteral("display_name"), QStringLiteral("Block Guest")}},
        m_token);
    QCOMPARE(guestUpdate.status, 200);

    const auto login = [this](const QString &username) {
        return request(
            "POST", QStringLiteral("/api/auth/login"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("password"), QStringLiteral("123456")}});
    };
    const Reply memberLogin = login(member.username);
    const Reply guestLogin = login(guest.username);
    QCOMPARE(memberLogin.status, 200);
    QCOMPARE(guestLogin.status, 200);
    const QString memberToken =
        memberLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    const QString guestToken =
        guestLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!memberToken.isEmpty());
    QVERIFY(!guestToken.isEmpty());

    QCOMPARE(request("GET", QStringLiteral("/api/users/options")).status, 401);
    const Reply options = request(
        "GET", QStringLiteral("/api/users/options"), {}, m_token);
    QCOMPARE(options.status, 200);
    const QJsonArray users = options.json().value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("users")).toArray();
    QCOMPARE(users.size(), 3);
    qint64 adminId = 0;
    for (const QJsonValue &value : users) {
        const QJsonObject user = value.toObject();
        QCOMPARE(user.size(), 3);
        QVERIFY(user.contains(QStringLiteral("display_name")));
        QVERIFY(user.contains(QStringLiteral("id")));
        QVERIFY(user.contains(QStringLiteral("username")));
        if (user.value(QStringLiteral("username")).toString()
            == QStringLiteral("admin")) {
            adminId = user.value(QStringLiteral("id")).toInteger();
        }
    }
    QVERIFY(adminId > 0);
    QCOMPARE(request("GET", QStringLiteral("/api/users/options"), {},
                     memberToken).status,
             200);
    QCOMPARE(request("GET", QStringLiteral("/api/users/options"), {},
                     guestToken).status,
             200);

    QCOMPARE(request("GET", QStringLiteral("/api/blocks")).status, 401);
    QCOMPARE(request("GET", QStringLiteral("/api/blocks"), {},
                     memberToken).status,
             200);
    QCOMPARE(request("GET", QStringLiteral("/api/blocks"), {},
                     guestToken).status,
             200);
    QCOMPARE(request("POST", QStringLiteral("/api/blocks"), {},
                     m_token).status,
             400);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Invalid Color")},
         {QStringLiteral("color"), QStringLiteral("blue")}},
        m_token).status,
        400);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Invalid Order")},
         {QStringLiteral("sort_order"), 1.5}},
        m_token).status,
        400);

    const Reply firstCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("  First Block  ")},
         {QStringLiteral("description"), QStringLiteral("First description")},
         {QStringLiteral("color"), QStringLiteral("#0066cc")}},
        m_token);
    QCOMPARE(firstCreated.status, 201);
    const QJsonObject first = firstCreated.json()
                                  .value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("block")).toObject();
    const qint64 firstId = first.value(QStringLiteral("id")).toInteger();
    QVERIFY(firstId > 0);
    QCOMPARE(first.value(QStringLiteral("title")).toString(),
             QStringLiteral("First Block"));
    QCOMPARE(first.value(QStringLiteral("sort_order")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("issue_count")).toInteger(), qint64(0));

    const Reply secondCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Second Block")}},
        m_token);
    QCOMPARE(secondCreated.status, 201);
    const QJsonObject second = secondCreated.json()
                                   .value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("block")).toObject();
    const qint64 secondId = second.value(QStringLiteral("id")).toInteger();
    QVERIFY(secondId > firstId);
    QCOMPARE(second.value(QStringLiteral("sort_order")).toInt(), 1);

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/blocks/%1").arg(secondId),
        {{QStringLiteral("sort_order"), 0}}, m_token).status,
        200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/blocks/%1").arg(firstId),
        {{QStringLiteral("sort_order"), 1}}, m_token).status,
        200);
    const Reply sorted = request(
        "GET", QStringLiteral("/api/blocks"), {}, m_token);
    QCOMPARE(sorted.status, 200);
    const QJsonArray blocks = sorted.json().value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("blocks")).toArray();
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks.at(0).toObject().value(QStringLiteral("id")).toInteger(),
             secondId);
    QCOMPARE(blocks.at(1).toObject().value(QStringLiteral("id")).toInteger(),
             firstId);

    QCOMPARE(request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Denied")}},
        memberToken).status,
        403);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/blocks/%1").arg(firstId),
        {{QStringLiteral("title"), QStringLiteral("Denied")}},
        guestToken).status,
        403);
    QCOMPARE(request(
        "DELETE", QStringLiteral("/api/blocks/%1").arg(firstId), {},
        memberToken).status,
        403);
    QCOMPARE(request("GET", QStringLiteral("/api/blocks/999999"), {},
                     m_token).status,
             404);
    QCOMPARE(request("PUT", QStringLiteral("/api/blocks/999999"),
                     {{QStringLiteral("sort_order"), 0}}, m_token).status,
             404);
    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/999999"), {},
                     m_token).status,
             404);

    const QString databasePath =
        m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db"));
    const QString setupConnection = QStringLiteral("batch_one_cascade_setup");
    qint64 issueId = 0;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), setupConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
        query.prepare(QStringLiteral(
            "INSERT INTO issues(block_id, title, reporter_id) VALUES(?, ?, ?)"));
        query.addBindValue(firstId);
        query.addBindValue(QStringLiteral("Cascade issue"));
        query.addBindValue(adminId);
        QVERIFY(query.exec());
        issueId = query.lastInsertId().toLongLong();
        QVERIFY(issueId > 0);
        query.prepare(QStringLiteral(
            "INSERT INTO comments(issue_id, user_id, content) VALUES(?, ?, ?)"));
        query.addBindValue(issueId);
        query.addBindValue(adminId);
        query.addBindValue(QStringLiteral("Cascade comment"));
        QVERIFY(query.exec());
        const qint64 commentId = query.lastInsertId().toLongLong();
        query.prepare(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, storage_path) "
            "VALUES(?, ?, ?, ?, ?)"));
        query.addBindValue(issueId);
        query.addBindValue(commentId);
        query.addBindValue(adminId);
        query.addBindValue(QStringLiteral("cascade.txt"));
        query.addBindValue(QStringLiteral("cascade/cascade.txt"));
        QVERIFY(query.exec());
        database.close();
    }
    QSqlDatabase::removeDatabase(setupConnection);

    const Reply withIssue = request(
        "GET", QStringLiteral("/api/blocks/%1").arg(firstId), {}, m_token);
    QCOMPARE(withIssue.status, 200);
    QCOMPARE(withIssue.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("block")).toObject()
                 .value(QStringLiteral("issue_count")).toInteger(),
             qint64(1));
    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(firstId),
                     {}, m_token).status,
             200);

    const QString checkConnection = QStringLiteral("batch_one_cascade_check");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), checkConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        const QStringList cascadeChecks = {
            QStringLiteral("SELECT COUNT(*) FROM issues WHERE id = ?"),
            QStringLiteral("SELECT COUNT(*) FROM comments WHERE issue_id = ?"),
            QStringLiteral("SELECT COUNT(*) FROM attachments WHERE issue_id = ?")
        };
        for (const QString &statement : cascadeChecks) {
            query.prepare(statement);
            query.addBindValue(issueId);
            QVERIFY(query.exec());
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 0);
            query.finish();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(checkConnection);

    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(secondId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(member.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(guest.id),
                     {}, m_token).status,
             200);
}

void HttpServerIntegrationTest::issueCrudFilteringAndPermissions()
{
    QString errorMessage;
    UserSummary reporter;
    UserSummary other;
    UserSummary guest;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("issue_reporter"), &reporter, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("issue_other"), &other, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("issue_guest"), &guest, &errorMessage),
             qPrintable(errorMessage));

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(guest.id),
        {{QStringLiteral("username"), guest.username},
         {QStringLiteral("role"), QStringLiteral("guest")},
         {QStringLiteral("display_name"), QStringLiteral("Issue Guest")}},
        m_token).status,
        200);

    const auto login = [this](const QString &username) {
        return request(
            "POST", QStringLiteral("/api/auth/login"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("password"), QStringLiteral("123456")}});
    };
    const Reply reporterLogin = login(reporter.username);
    const Reply otherLogin = login(other.username);
    const Reply guestLogin = login(guest.username);
    QCOMPARE(reporterLogin.status, 200);
    QCOMPARE(otherLogin.status, 200);
    QCOMPARE(guestLogin.status, 200);
    const QString reporterToken =
        reporterLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    const QString otherToken =
        otherLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    const QString guestToken =
        guestLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!reporterToken.isEmpty());
    QVERIFY(!otherToken.isEmpty());
    QVERIFY(!guestToken.isEmpty());

    const Reply firstBlockCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Issue Block One")}},
        m_token);
    const Reply secondBlockCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Issue Block Two")}},
        m_token);
    QCOMPARE(firstBlockCreated.status, 201);
    QCOMPARE(secondBlockCreated.status, 201);
    const qint64 firstBlockId = firstBlockCreated.json()
                                    .value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("block")).toObject()
                                    .value(QStringLiteral("id")).toInteger();
    const qint64 secondBlockId = secondBlockCreated.json()
                                     .value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("block")).toObject()
                                     .value(QStringLiteral("id")).toInteger();
    QVERIFY(firstBlockId > 0);
    QVERIFY(secondBlockId > firstBlockId);

    QCOMPARE(request("GET", QStringLiteral("/api/issues")).status, 401);
    QCOMPARE(request("GET", QStringLiteral("/api/issues"), {},
                     guestToken).status,
             200);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), firstBlockId},
         {QStringLiteral("title"), QStringLiteral("Denied guest issue")}},
        guestToken).status,
        403);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), firstBlockId}}, reporterToken).status,
        400);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), firstBlockId},
         {QStringLiteral("title"), QStringLiteral("Invalid priority")},
         {QStringLiteral("priority"), QStringLiteral("urgent")}},
        reporterToken).status,
        400);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), qint64(999999)},
         {QStringLiteral("title"), QStringLiteral("Missing block")}},
        reporterToken).status,
        404);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), firstBlockId},
         {QStringLiteral("title"), QStringLiteral("Missing assignee")},
         {QStringLiteral("assignee_id"), qint64(999999)}},
        reporterToken).status,
        404);

    const Reply firstCreated = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), firstBlockId},
         {QStringLiteral("title"), QStringLiteral("  Alpha issue  ")},
         {QStringLiteral("description"), QStringLiteral("Network contract")},
         {QStringLiteral("priority"), QStringLiteral("high")},
         {QStringLiteral("assignee_id"), other.id}},
        reporterToken);
    QCOMPARE(firstCreated.status, 201);
    const QJsonObject firstIssue = firstCreated.json()
                                       .value(QStringLiteral("data")).toObject()
                                       .value(QStringLiteral("issue")).toObject();
    const qint64 firstIssueId = firstIssue.value(QStringLiteral("id")).toInteger();
    QVERIFY(firstIssueId > 0);
    const QString firstTaskId = QStringLiteral("T%1").arg(firstIssueId);
    QCOMPARE(firstIssue.value(QStringLiteral("task_id")).toString(),
             firstTaskId);
    QCOMPARE(firstIssue.value(QStringLiteral("title")).toString(),
             QStringLiteral("Alpha issue"));
    QCOMPARE(firstIssue.value(QStringLiteral("status")).toString(),
             QStringLiteral("open"));
    QCOMPARE(firstIssue.value(QStringLiteral("reporter_id")).toInteger(),
             reporter.id);
    QCOMPARE(firstIssue.value(QStringLiteral("reporter")).toObject()
                 .value(QStringLiteral("username")).toString(),
             reporter.username);
    QCOMPARE(firstIssue.value(QStringLiteral("assignee")).toObject()
                 .value(QStringLiteral("id")).toInteger(),
             other.id);
    QCOMPARE(firstIssue.value(QStringLiteral("comment_count")).toInteger(),
             qint64(0));
    QCOMPARE(firstIssue.value(QStringLiteral("attachment_count")).toInteger(),
             qint64(0));

    const Reply assigneeInProgress = request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("in_progress")}},
        otherToken);
    QCOMPARE(assigneeInProgress.status, 200);
    QCOMPARE(assigneeInProgress.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issue")).toObject()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("in_progress"));
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("resolved")}},
        otherToken).status,
        200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("closed")}},
        reporterToken).status,
        200);
    const Reply assigneeReopened = request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("open")}},
        otherToken);
    QCOMPARE(assigneeReopened.status, 200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("closed")}},
        otherToken).status,
        403);

    const Reply secondCreated = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), secondBlockId},
         {QStringLiteral("title"), QStringLiteral("Beta issue")},
         {QStringLiteral("priority"), QStringLiteral("medium")}},
        otherToken);
    const Reply thirdCreated = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), firstBlockId},
         {QStringLiteral("title"), QStringLiteral("Gamma issue")},
         {QStringLiteral("description"), QStringLiteral("Unique needle text")},
         {QStringLiteral("priority"), QStringLiteral("low")},
         {QStringLiteral("assignee_id"), QJsonValue(QJsonValue::Null)}},
        reporterToken);
    QCOMPARE(secondCreated.status, 201);
    QCOMPARE(thirdCreated.status, 201);
    const qint64 secondIssueId = secondCreated.json()
                                     .value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("issue")).toObject()
                                     .value(QStringLiteral("id")).toInteger();
    const qint64 thirdIssueId = thirdCreated.json()
                                    .value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("issue")).toObject()
                                    .value(QStringLiteral("id")).toInteger();
    QVERIFY(secondIssueId > firstIssueId);
    QVERIFY(thirdIssueId > secondIssueId);

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1").arg(thirdIssueId),
        {{QStringLiteral("assignee_id"), guest.id}}, reporterToken).status,
        200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(thirdIssueId),
        {{QStringLiteral("status"), QStringLiteral("in_progress")}},
        guestToken).status,
        403);

    const Reply blockFiltered = request(
        "GET", QStringLiteral("/api/issues?block_id=%1&sort=created_desc")
                   .arg(firstBlockId), {}, guestToken);
    QCOMPARE(blockFiltered.status, 200);
    QCOMPARE(blockFiltered.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issues")).toArray().size(),
             2);
    const Reply blockRoute = request(
        "GET", QStringLiteral("/api/blocks/%1/issues").arg(firstBlockId),
        {}, guestToken);
    QCOMPARE(blockRoute.status, 200);
    QCOMPARE(blockRoute.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issues")).toArray().size(),
             2);
    QCOMPARE(request("GET", QStringLiteral("/api/blocks/999999/issues"), {},
                     guestToken).status,
             404);
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues?priority=high"), {},
        guestToken).json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("issues")).toArray().size(),
        1);
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues?assignee_id=%1").arg(other.id), {},
        guestToken).json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("issues")).toArray().size(),
        1);
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues?q=needle"), {},
        guestToken).json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("issues")).toArray().size(),
        1);
    const QJsonArray taskSearch = request(
        "GET", QStringLiteral("/api/issues?q=%1").arg(firstTaskId), {},
        guestToken).json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("issues")).toArray();
    QCOMPARE(taskSearch.size(), 1);
    QCOMPARE(taskSearch.first().toObject()
                 .value(QStringLiteral("task_id")).toString(),
             firstTaskId);

    const Reply prioritySorted = request(
        "GET", QStringLiteral("/api/issues?sort=priority_desc"), {},
        guestToken);
    QCOMPARE(prioritySorted.status, 200);
    const QJsonArray sortedIssues = prioritySorted.json()
                                        .value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("issues")).toArray();
    QCOMPARE(sortedIssues.size(), 3);
    QCOMPARE(sortedIssues.at(0).toObject()
                 .value(QStringLiteral("priority")).toString(),
             QStringLiteral("high"));
    QCOMPARE(sortedIssues.at(2).toObject()
                 .value(QStringLiteral("priority")).toString(),
             QStringLiteral("low"));
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues?status=invalid"), {},
        guestToken).status,
        400);
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues?sort=random"), {},
        guestToken).status,
        400);

    const Reply detail = request(
        "GET", QStringLiteral("/api/issues/%1").arg(firstIssueId), {},
        guestToken);
    QCOMPARE(detail.status, 200);
    QCOMPARE(detail.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issue")).toObject()
                 .value(QStringLiteral("assignee_id")).toInteger(),
             other.id);
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues/%1").arg(firstTaskId), {},
        guestToken).status,
        200);
    QCOMPARE(request(
        "GET", QStringLiteral("/api/issues/t%1").arg(firstIssueId), {},
        guestToken).status,
        200);
    for (const QString &invalidIdentifier : {
             QStringLiteral("T0"), QStringLiteral("T001"),
             QStringLiteral("TX")}) {
        const Reply invalidReply = request(
            "GET", QStringLiteral("/api/issues/%1").arg(invalidIdentifier),
            {}, guestToken);
        QCOMPARE(invalidReply.status, 400);
        QCOMPARE(invalidReply.json().value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(),
                 QStringLiteral("invalid_task_id"));
    }

    const QJsonObject editPayload = {
        {QStringLiteral("title"), QStringLiteral("Alpha issue updated")},
        {QStringLiteral("block_id"), secondBlockId},
        {QStringLiteral("priority"), QStringLiteral("low")},
        {QStringLiteral("assignee_id"), QJsonValue(QJsonValue::Null)}
    };
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1").arg(firstIssueId),
        editPayload, otherToken).status,
        403);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1").arg(firstIssueId),
        editPayload, guestToken).status,
        403);
    const Reply updated = request(
        "PUT", QStringLiteral("/api/issues/%1").arg(firstTaskId),
        editPayload, reporterToken);
    QCOMPARE(updated.status, 200);
    const QJsonObject updatedIssue = updated.json()
                                         .value(QStringLiteral("data")).toObject()
                                         .value(QStringLiteral("issue")).toObject();
    QCOMPARE(updatedIssue.value(QStringLiteral("block_id")).toInteger(),
             secondBlockId);
    QVERIFY(updatedIssue.value(QStringLiteral("assignee_id")).isNull());
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("closed")}},
        reporterToken).status,
        400);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/999999"),
        {{QStringLiteral("title"), QStringLiteral("Missing")}},
        reporterToken).status,
        404);

    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("resolved")}},
        otherToken).status,
        403);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("resolved")}},
        guestToken).status,
        403);
    const Reply statusChanged = request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstTaskId),
        {{QStringLiteral("status"), QStringLiteral("resolved")}},
        reporterToken);
    QCOMPARE(statusChanged.status, 200);
    QCOMPARE(statusChanged.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issue")).toObject()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("resolved"));
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(firstIssueId),
        {{QStringLiteral("status"), QStringLiteral("invalid")}},
        reporterToken).status,
        400);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1").arg(secondIssueId),
        {{QStringLiteral("priority"), QStringLiteral("high")}},
        m_token).status,
        200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(secondIssueId),
        {{QStringLiteral("status"), QStringLiteral("closed")}},
        m_token).status,
        200);

    QCOMPARE(request(
        "DELETE", QStringLiteral("/api/issues/%1").arg(firstIssueId), {},
        reporterToken).status,
        403);
    QCOMPARE(request(
        "DELETE", QStringLiteral("/api/issues/%1").arg(secondIssueId), {},
        guestToken).status,
        403);

    const QString databasePath =
        m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db"));
    const QString setupConnection = QStringLiteral("issue_cascade_setup");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), setupConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
        query.prepare(QStringLiteral(
            "INSERT INTO comments(issue_id, user_id, content) VALUES(?, ?, ?)"));
        query.addBindValue(firstIssueId);
        query.addBindValue(reporter.id);
        query.addBindValue(QStringLiteral("Cascade comment"));
        QVERIFY(query.exec());
        const qint64 commentId = query.lastInsertId().toLongLong();
        query.prepare(QStringLiteral(
            "INSERT INTO attachments(issue_id, comment_id, uploader_id, filename, storage_path) "
            "VALUES(?, ?, ?, ?, ?)"));
        query.addBindValue(firstIssueId);
        query.addBindValue(commentId);
        query.addBindValue(reporter.id);
        query.addBindValue(QStringLiteral("issue-cascade.png"));
        query.addBindValue(QStringLiteral("issue/issue-cascade.png"));
        QVERIFY(query.exec());
        database.close();
    }
    QSqlDatabase::removeDatabase(setupConnection);

    const Reply countedDetail = request(
        "GET", QStringLiteral("/api/issues/%1").arg(firstIssueId), {},
        m_token);
    QCOMPARE(countedDetail.status, 200);
    const QJsonObject countedIssue = countedDetail.json()
                                         .value(QStringLiteral("data")).toObject()
                                         .value(QStringLiteral("issue")).toObject();
    QCOMPARE(countedIssue.value(QStringLiteral("comment_count")).toInteger(),
             qint64(1));
    QCOMPARE(countedIssue.value(QStringLiteral("attachment_count")).toInteger(),
             qint64(1));
    QCOMPARE(request(
        "DELETE", QStringLiteral("/api/issues/%1").arg(firstIssueId), {},
        m_token).status,
        200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/999999"), {},
                     m_token).status,
             404);

    const QString checkConnection = QStringLiteral("issue_cascade_check");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), checkConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        const QStringList cascadeChecks = {
            QStringLiteral("SELECT COUNT(*) FROM comments WHERE issue_id = ?"),
            QStringLiteral("SELECT COUNT(*) FROM attachments WHERE issue_id = ?")
        };
        for (const QString &statement : cascadeChecks) {
            query.prepare(statement);
            query.addBindValue(firstIssueId);
            QVERIFY(query.exec());
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 0);
            query.finish();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(checkConnection);

    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/%1").arg(secondIssueId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/T%1").arg(thirdIssueId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(firstBlockId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(secondBlockId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(reporter.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(other.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(guest.id),
                     {}, m_token).status,
             200);
}

void HttpServerIntegrationTest::commentReadAndCreatePermissions()
{
    QString errorMessage;
    UserSummary member;
    UserSummary guest;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("comment_member"), &member, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("comment_guest"), &guest, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(member.id),
        {{QStringLiteral("username"), member.username},
         {QStringLiteral("role"), QStringLiteral("user")},
         {QStringLiteral("display_name"), QStringLiteral("Comment Member")}},
        m_token).status,
        200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(guest.id),
        {{QStringLiteral("username"), guest.username},
         {QStringLiteral("role"), QStringLiteral("guest")},
         {QStringLiteral("display_name"), QStringLiteral("Comment Guest")}},
        m_token).status,
        200);

    const auto login = [this](const QString &username) {
        return request(
            "POST", QStringLiteral("/api/auth/login"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("password"), QStringLiteral("123456")}});
    };
    const Reply memberLogin = login(member.username);
    const Reply guestLogin = login(guest.username);
    QCOMPARE(memberLogin.status, 200);
    QCOMPARE(guestLogin.status, 200);
    const QString memberToken =
        memberLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    const QString guestToken =
        guestLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!memberToken.isEmpty());
    QVERIFY(!guestToken.isEmpty());

    const Reply blockCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Comment Block")}},
        m_token);
    QCOMPARE(blockCreated.status, 201);
    const qint64 blockId = blockCreated.json()
                               .value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("block")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    QVERIFY(blockId > 0);

    const Reply issueCreated = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), blockId},
         {QStringLiteral("title"), QStringLiteral("Comment Issue")}},
        memberToken);
    QCOMPARE(issueCreated.status, 201);
    const qint64 issueId = issueCreated.json()
                               .value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("issue")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    QVERIFY(issueId > 0);

    const QString commentsPath =
        QStringLiteral("/api/issues/T%1/comments").arg(issueId);
    QCOMPARE(request("GET", QStringLiteral("/api/issues/T001/comments"), {},
                     guestToken).status,
             400);
    QCOMPARE(request("GET", commentsPath).status, 401);
    QCOMPARE(request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QStringLiteral("Unauthorized comment")}}).status,
        401);
    QCOMPARE(request("GET", commentsPath, {}, memberToken).status, 200);
    QCOMPARE(request("GET", commentsPath, {}, guestToken).status, 200);
    QCOMPARE(request("GET", QStringLiteral("/api/issues/999999/comments"), {},
                     guestToken).status,
             404);

    QCOMPARE(request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QStringLiteral("Guest comment")}},
        guestToken).status,
        403);
    QCOMPARE(request("POST", commentsPath, {}, memberToken).status, 400);
    QCOMPARE(request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QStringLiteral("   ")}},
        memberToken).status,
        400);
    QCOMPARE(request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QString(4001, QLatin1Char('x'))}},
        memberToken).status,
        400);
    QCOMPARE(request(
        "POST", QStringLiteral("/api/issues/999999/comments"),
        {{QStringLiteral("content"), QStringLiteral("Missing issue")}},
        memberToken).status,
        404);

    const Reply memberCreated = request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QStringLiteral("  First comment  ")}},
        memberToken);
    QCOMPARE(memberCreated.status, 201);
    const QJsonObject memberComment = memberCreated.json()
                                          .value(QStringLiteral("data")).toObject()
                                          .value(QStringLiteral("comment")).toObject();
    const qint64 memberCommentId =
        memberComment.value(QStringLiteral("id")).toInteger();
    QVERIFY(memberCommentId > 0);
    QCOMPARE(memberComment.value(QStringLiteral("issue_id")).toInteger(),
             issueId);
    QCOMPARE(memberComment.value(QStringLiteral("user_id")).toInteger(),
             member.id);
    QCOMPARE(memberComment.value(QStringLiteral("content")).toString(),
             QStringLiteral("First comment"));
    QCOMPARE(memberComment.value(QStringLiteral("user")).toObject()
                 .value(QStringLiteral("display_name")).toString(),
             QStringLiteral("Comment Member"));
    QVERIFY(!memberComment.value(QStringLiteral("created_at")).toString().isEmpty());

    const Reply adminCreated = request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QStringLiteral("Second comment")}},
        m_token);
    QCOMPARE(adminCreated.status, 201);
    const qint64 adminCommentId = adminCreated.json()
                                      .value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("comment")).toObject()
                                      .value(QStringLiteral("id")).toInteger();
    QVERIFY(adminCommentId > memberCommentId);

    const Reply comments = request("GET", commentsPath, {}, guestToken);
    QCOMPARE(comments.status, 200);
    const QJsonArray rows = comments.json()
                                .value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("comments")).toArray();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("id")).toInteger(),
             memberCommentId);
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("id")).toInteger(),
             adminCommentId);
    for (const QJsonValue &value : rows) {
        const QJsonObject comment = value.toObject();
        QCOMPARE(comment.value(QStringLiteral("user")).toObject().size(), 3);
        QVERIFY(comment.value(QStringLiteral("user")).toObject()
                    .contains(QStringLiteral("id")));
        QVERIFY(comment.value(QStringLiteral("user")).toObject()
                    .contains(QStringLiteral("username")));
        QVERIFY(comment.value(QStringLiteral("user")).toObject()
                    .contains(QStringLiteral("display_name")));
    }

    const Reply issueWithCount = request(
        "GET", QStringLiteral("/api/issues/%1").arg(issueId), {}, m_token);
    QCOMPARE(issueWithCount.status, 200);
    QCOMPARE(issueWithCount.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issue")).toObject()
                 .value(QStringLiteral("comment_count")).toInteger(),
             qint64(2));

    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/%1").arg(issueId),
                     {}, m_token).status,
             200);
    const QString databasePath =
        m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db"));
    const QString checkConnection = QStringLiteral("comment_cascade_check");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), checkConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM comments WHERE issue_id = ?"));
        query.addBindValue(issueId);
        QVERIFY(query.exec());
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        database.close();
    }
    QSqlDatabase::removeDatabase(checkConnection);

    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(blockId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(member.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(guest.id),
                     {}, m_token).status,
             200);
}

void HttpServerIntegrationTest::notificationApiAndBusinessEvents()
{
    QCOMPARE(request("GET", QStringLiteral("/api/notifications")).status, 401);
    QCOMPARE(request("GET", QStringLiteral("/api/notifications/unread-count")).status,
             401);
    QCOMPARE(request("PUT", QStringLiteral("/api/notifications/read-all")).status,
             401);
    QCOMPARE(request("PUT", QStringLiteral("/api/notifications/1/read")).status,
             401);

    QCOMPARE(request("PUT", QStringLiteral("/api/notifications/read-all"), {},
                     m_token).status,
             200);

    QString errorMessage;
    UserSummary reporter;
    UserSummary assignee;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("notification_reporter"), &reporter,
                 &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("notification_assignee"), &assignee,
                 &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(reporter.id),
        {{QStringLiteral("username"), reporter.username},
         {QStringLiteral("role"), QStringLiteral("user")},
         {QStringLiteral("display_name"), QStringLiteral("Notification Reporter")}},
        m_token).status,
        200);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(assignee.id),
        {{QStringLiteral("username"), assignee.username},
         {QStringLiteral("role"), QStringLiteral("user")},
         {QStringLiteral("display_name"), QStringLiteral("Notification Assignee")}},
        m_token).status,
        200);

    const auto login = [this](const QString &username) {
        return request(
            "POST", QStringLiteral("/api/auth/login"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("password"), QStringLiteral("123456")}});
    };
    const Reply reporterLogin = login(reporter.username);
    const Reply assigneeLogin = login(assignee.username);
    QCOMPARE(reporterLogin.status, 200);
    QCOMPARE(assigneeLogin.status, 200);
    const QString reporterToken =
        reporterLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    const QString assigneeToken =
        assigneeLogin.json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("token")).toString();
    QVERIFY(!reporterToken.isEmpty());
    QVERIFY(!assigneeToken.isEmpty());

    QSignalSpy createdSpy(m_server.get(), &HttpServer::notificationCreated);
    QSignalSpy countChangedSpy(m_server.get(),
                               &HttpServer::notificationCountChanged);

    const Reply blockCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Notification Block")}},
        m_token);
    QCOMPARE(blockCreated.status, 201);
    const qint64 blockId = blockCreated.json()
                               .value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("block")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    QVERIFY(blockId > 0);

    const Reply issueCreated = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), blockId},
         {QStringLiteral("title"), QStringLiteral("Notification Issue")},
         {QStringLiteral("assignee_id"), assignee.id}},
        reporterToken);
    QCOMPARE(issueCreated.status, 201);
    const qint64 issueId = issueCreated.json()
                               .value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("issue")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    QVERIFY(issueId > 0);
    QCOMPARE(createdSpy.count(), 2);
    QCOMPARE(countChangedSpy.count(), 2);

    const auto unreadCount = [this](const QString &token) {
        return request("GET", QStringLiteral("/api/notifications/unread-count"),
                       {}, token)
            .json().value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("unread_count")).toInteger();
    };
    QCOMPARE(unreadCount(m_token), qint64(1));
    QCOMPARE(unreadCount(reporterToken), qint64(0));
    QCOMPARE(unreadCount(assigneeToken), qint64(1));

    const Reply adminList = request(
        "GET", QStringLiteral("/api/notifications"), {}, m_token);
    QCOMPARE(adminList.status, 200);
    const QJsonArray adminNotifications = adminList.json()
                                               .value(QStringLiteral("data")).toObject()
                                               .value(QStringLiteral("notifications")).toArray();
    QCOMPARE(adminNotifications.size(), 1);
    const QJsonObject adminNotification = adminNotifications.first().toObject();
    QCOMPARE(adminNotification.value(QStringLiteral("type")).toString(),
             QStringLiteral("issue_created"));
    QCOMPARE(adminNotification.value(QStringLiteral("related_id")).toInteger(),
             issueId);
    QCOMPARE(adminNotification.value(
                 QStringLiteral("related_task_id")).toString(),
             QStringLiteral("T%1").arg(issueId));
    QVERIFY(!adminNotification.contains(QStringLiteral("recipient_id")));
    QCOMPARE(adminNotification.value(QStringLiteral("sender")).toObject()
                 .value(QStringLiteral("id")).toInteger(),
             reporter.id);
    const qint64 adminNotificationId =
        adminNotification.value(QStringLiteral("id")).toInteger();
    QVERIFY(adminNotificationId > 0);

    QCOMPARE(request(
        "PUT",
        QStringLiteral("/api/notifications/%1/read").arg(adminNotificationId),
        {}, assigneeToken).status,
        404);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/notifications/not-a-number/read"), {},
        m_token).status,
        400);
    const Reply readOne = request(
        "PUT",
        QStringLiteral("/api/notifications/%1/read").arg(adminNotificationId),
        {}, m_token);
    QCOMPARE(readOne.status, 200);
    QCOMPARE(readOne.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("deleted_id")).toInteger(),
             adminNotificationId);
    QCOMPARE(request(
        "PUT",
        QStringLiteral("/api/notifications/%1/read").arg(adminNotificationId),
        {}, m_token).status,
        404);

    const int signalsAfterRead = createdSpy.count();
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1").arg(issueId),
        {{QStringLiteral("assignee_id"), assignee.id}}, reporterToken).status,
        200);
    QCOMPARE(createdSpy.count(), signalsAfterRead);

    const QString commentsPath =
        QStringLiteral("/api/issues/%1/comments").arg(issueId);
    QCOMPARE(request(
        "POST", commentsPath,
        {{QStringLiteral("content"), QStringLiteral("First line\nSecond line")}},
        assigneeToken).status,
        201);
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(issueId),
        {{QStringLiteral("status"), QStringLiteral("in_progress")}},
        reporterToken).status,
        200);
    const int signalsAfterStatus = createdSpy.count();
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/issues/%1/status").arg(issueId),
        {{QStringLiteral("status"), QStringLiteral("in_progress")}},
        reporterToken).status,
        200);
    QCOMPARE(createdSpy.count(), signalsAfterStatus);

    QByteArray pngData;
    QBuffer pngBuffer(&pngData);
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(pngBuffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&pngBuffer, "PNG"));
    QCOMPARE(requestCommentMultipart(
        commentsPath, QStringLiteral("![Image](upload:0)"),
        {{QStringLiteral("notification.png"), pngData}}, assigneeToken).status,
        201);
    const QJsonArray commentNotifications = request(
        "GET", QStringLiteral("/api/notifications"), {}, m_token)
        .json().value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("notifications")).toArray();
    bool foundAttachmentSummary = false;
    for (const QJsonValue &value : commentNotifications) {
        const QJsonObject notification = value.toObject();
        if (notification.value(QStringLiteral("type")).toString()
                == QStringLiteral("comment_added")) {
            foundAttachmentSummary = notification.value(QStringLiteral("content"))
                .toString().contains(QStringLiteral("1 attachment"));
        }
    }
    QVERIFY(foundAttachmentSummary);

    QCOMPARE(createdSpy.count(), 8);
    QCOMPARE(unreadCount(m_token), qint64(3));
    QCOMPARE(unreadCount(reporterToken), qint64(2));
    QCOMPARE(unreadCount(assigneeToken), qint64(2));

    qint64 localAdminCount = 0;
    QVERIFY2(m_server->localAdminUnreadCount(&localAdminCount, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(localAdminCount, qint64(3));
    QList<NotificationRecord> localNotifications;
    QVERIFY2(m_server->localAdminUnreadNotifications(
                 &localNotifications, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(localNotifications.size(), 3);
    QVERIFY(localNotifications.first().id > 0);
    const qint64 locallyReadId = localNotifications.first().id;
    QVERIFY2(m_server->markLocalAdminNotificationRead(
                 locallyReadId, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->localAdminUnreadNotifications(
                 &localNotifications, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(localNotifications.size(), 2);
    QVERIFY2(m_server->issueExists(issueId, &errorMessage),
             qPrintable(errorMessage));

    const Reply readAll = request(
        "PUT", QStringLiteral("/api/notifications/read-all"), {}, m_token);
    QCOMPARE(readAll.status, 200);
    QCOMPARE(readAll.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("deleted_count")).toInteger(),
             qint64(2));
    QCOMPARE(request("PUT", QStringLiteral("/api/notifications/read-all"), {},
                     m_token).json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("deleted_count")).toInteger(),
             qint64(0));
    qint64 locallyDeleted = -1;
    QVERIFY2(m_server->markAllLocalAdminNotificationsRead(
                 &locallyDeleted, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(locallyDeleted, qint64(0));

    const QString databasePath =
        m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db"));
    const QString failureConnection =
        QStringLiteral("notification_failure_injection");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), failureConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "ALTER TABLE notifications RENAME TO notifications_unavailable")));
        database.close();
    }
    QSqlDatabase::removeDatabase(failureConnection);

    const Reply issueWithoutNotification = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), blockId},
         {QStringLiteral("title"), QStringLiteral("Notification Failure Issue")}},
        reporterToken);

    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), failureConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "ALTER TABLE notifications_unavailable RENAME TO notifications")));
        database.close();
    }
    QSqlDatabase::removeDatabase(failureConnection);
    QCOMPARE(issueWithoutNotification.status, 201);
    const qint64 issueWithoutNotificationId = issueWithoutNotification.json()
        .value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("issue")).toObject()
        .value(QStringLiteral("id")).toInteger();
    QVERIFY(issueWithoutNotificationId > 0);
    QCOMPARE(unreadCount(m_token), qint64(0));
    QCOMPARE(request(
        "DELETE",
        QStringLiteral("/api/issues/%1").arg(issueWithoutNotificationId), {},
        m_token).status,
        200);

    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/%1").arg(issueId),
                     {}, m_token).status,
             200);
    QCOMPARE(unreadCount(reporterToken), qint64(0));
    QCOMPARE(unreadCount(assigneeToken), qint64(0));
    errorMessage.clear();
    QVERIFY(!m_server->issueExists(issueId, &errorMessage));
    QVERIFY(errorMessage.isEmpty());

    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(blockId),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(reporter.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(assignee.id),
                     {}, m_token).status,
             200);
}

void HttpServerIntegrationTest::attachmentUploadReadAndDeletePermissions()
{
    QSKIP("Legacy Issue-level attachment test is replaced by comment image coverage.");
}

void HttpServerIntegrationTest::commentImagesAndCascadeCleanup()
{
    QString errorMessage;
    UserSummary member;
    UserSummary guest;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("comment_image_member"), &member, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("comment_image_guest"), &guest, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(guest.id),
        {{QStringLiteral("username"), guest.username},
         {QStringLiteral("role"), QStringLiteral("guest")}},
        m_token).status,
        200);
    const Reply memberLogin = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), member.username},
         {QStringLiteral("password"), QStringLiteral("123456")} });
    const Reply guestLogin = request(
        "POST", QStringLiteral("/api/auth/login"),
        {{QStringLiteral("username"), guest.username},
         {QStringLiteral("password"), QStringLiteral("123456")} });
    const QString memberToken = memberLogin.json().value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("token")).toString();
    const QString guestToken = guestLogin.json().value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("token")).toString();
    const Reply block = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Comment Images")}}, m_token);
    const qint64 blockId = block.json().value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("block")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    const Reply issue = request(
        "POST", QStringLiteral("/api/issues"),
        {{QStringLiteral("block_id"), blockId},
         {QStringLiteral("title"), QStringLiteral("Inline images")}}, memberToken);
    const qint64 issueId = issue.json().value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("issue")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    QByteArray firstImage;
    QImage first(800, 500, QImage::Format_ARGB32);
    first.fill(qRgb(42, 118, 201));
    QBuffer firstBuffer(&firstImage);
    QVERIFY(firstBuffer.open(QIODevice::WriteOnly));
    QVERIFY(first.save(&firstBuffer, "PNG"));
    QByteArray secondImage;
    QImage second(640, 420, QImage::Format_ARGB32);
    second.fill(qRgb(201, 118, 42));
    QBuffer secondBuffer(&secondImage);
    QVERIFY(secondBuffer.open(QIODevice::WriteOnly));
    QVERIFY(second.save(&secondBuffer, "PNG"));
    QByteArray largeImage;
    QImage large(8000, 6000, QImage::Format_Mono);
    large.setColorCount(2);
    large.setColor(0, qRgb(0, 0, 0));
    large.setColor(1, qRgb(255, 255, 255));
    large.fill(1);
    QBuffer largeBuffer(&largeImage);
    QVERIFY(largeBuffer.open(QIODevice::WriteOnly));
    QVERIFY(large.save(&largeBuffer, "PNG"));
    QByteArray tallImage;
    QImage tall(1000, 20000, QImage::Format_Mono);
    tall.setColorCount(2);
    tall.setColor(0, qRgb(0, 0, 0));
    tall.setColor(1, qRgb(255, 255, 255));
    tall.fill(0);
    QBuffer tallBuffer(&tallImage);
    QVERIFY(tallBuffer.open(QIODevice::WriteOnly));
    QVERIFY(tall.save(&tallBuffer, "PNG"));

    const QString commentsPath = QStringLiteral("/api/issues/%1/comments").arg(issueId);
    QByteArray bitmapImage;
    QBuffer bitmapBuffer(&bitmapImage);
    QVERIFY(bitmapBuffer.open(QIODevice::WriteOnly));
    QVERIFY(first.save(&bitmapBuffer, "BMP"));
    const Reply unsupported = requestCommentMultipart(
        commentsPath, QStringLiteral("![位图](upload:0)"),
        {{QStringLiteral("unsupported.bmp"), bitmapImage}}, memberToken);
    QCOMPARE(unsupported.status, 400);
    QVERIFY(unsupported.json().value(QStringLiteral("error")).toObject()
                .value(QStringLiteral("message")).toString()
                .contains(QStringLiteral("PNG, JPEG, and WebP")));
    const Reply corrupt = requestCommentMultipart(
        commentsPath, QStringLiteral("![损坏图片](upload:0)"),
        {{QStringLiteral("corrupt.png"),
          QByteArray::fromHex("89504e470d0a1a0a") + QByteArrayLiteral("broken")}},
        memberToken);
    QCOMPARE(corrupt.status, 400);
    QVERIFY(corrupt.json().value(QStringLiteral("error")).toObject()
                .value(QStringLiteral("message")).toString()
                .contains(QStringLiteral("could not be decoded")));
    const Reply unsupportedFile = requestCommentMultipart(
        commentsPath, QStringLiteral("[Executable](upload:0)"),
        {{QStringLiteral("diagnostic.exe"), QByteArrayLiteral("not executable")}},
        memberToken);
    QCOMPARE(unsupportedFile.status, 400);
    QCOMPARE(unsupportedFile.json().value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("invalid_attachment_type"));
    QCOMPARE(requestCommentMultipart(
        commentsPath,
        QStringLiteral("[First](upload:0)\n[Duplicate](upload:0)"),
        {{QStringLiteral("duplicate.log"), QByteArrayLiteral("duplicate")}},
        memberToken).status,
        400);
    QList<QPair<QString, QByteArray>> tooManyFiles;
    QStringList tooManyMarkers;
    for (int index = 0; index < 10; ++index) {
        tooManyFiles.append({QStringLiteral("log-%1.txt").arg(index),
                             QByteArrayLiteral("data")});
        tooManyMarkers.append(QStringLiteral("[Log](upload:%1)").arg(index));
    }
    QCOMPARE(requestCommentMultipart(
        commentsPath, tooManyMarkers.join(QLatin1Char('\n')),
        tooManyFiles, memberToken).status,
        400);
    const QByteArray oversizedLog(10 * 1024 * 1024 + 1, 'x');
    const Reply oversized = requestCommentMultipart(
        commentsPath, QStringLiteral("[Large log](upload:0)"),
        {{QStringLiteral("oversized.log"), oversizedLog}}, memberToken);
    QCOMPARE(oversized.status, 400);
    QCOMPARE(oversized.json().value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("invalid_attachment_size"));
    QCOMPARE(requestCommentMultipart(
        commentsPath,
        QStringLiteral("前置\n![第一张](upload:0)\n后置\n![第二张](upload:1)\n![大图](upload:2)\n![长图](upload:3)"),
        {{QStringLiteral("first.png"), firstImage},
         {QStringLiteral("second.png"), secondImage},
         {QStringLiteral("large.png"), largeImage},
         {QStringLiteral("tall.png"), tallImage}},
        guestToken).status,
        403);
    m_server->setConfigurationProvider([this](QString *providerError) {
        if (providerError) {
            providerError->clear();
        }
        ServerConfig config;
        config.serverInterface = QStringLiteral("127.0.0.1");
        config.serverPort = m_port;
        config.keepOriginal = true;
        config.maxImageWidth = 1600;
        return config;
    });
    const Reply created = requestCommentMultipart(
        commentsPath,
        QStringLiteral("前置\n![第一张](upload:0)\n后置\n![第二张](upload:1)\n![大图](upload:2)\n![长图](upload:3)"),
        {{QStringLiteral("first.png"), firstImage},
         {QStringLiteral("second.png"), secondImage},
         {QStringLiteral("large.png"), largeImage},
         {QStringLiteral("tall.png"), tallImage}},
        memberToken);
    m_server->setConfigurationProvider([this](QString *providerError) {
        if (providerError) {
            providerError->clear();
        }
        ServerConfig config;
        config.serverInterface = QStringLiteral("127.0.0.1");
        config.serverPort = m_port;
        config.maxImageWidth = 1600;
        return config;
    });
    QCOMPARE(created.status, 201);
    const QJsonObject comment = created.json().value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("comment")).toObject();
    const QJsonArray attachments = comment.value(
        QStringLiteral("attachments")).toArray();
    QCOMPARE(attachments.size(), 4);
    const QString storedContent = comment.value(QStringLiteral("content")).toString();
    QVERIFY(storedContent.contains(QStringLiteral("attachment:")));
    QVERIFY(!storedContent.contains(QStringLiteral("upload:")));
    const qint64 attachmentId = attachments.at(0).toObject()
                                    .value(QStringLiteral("id")).toInteger();
    QVERIFY(!attachments.at(0).toObject().contains(
        QStringLiteral("original_path")));
    QStringList originalFiles;
    const QString originalConnection = QStringLiteral(
        "comment_original_path_check");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), originalConnection);
        database.setDatabaseName(
            m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db")));
        QVERIFY(database.open());
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "SELECT storage_path, thumb_path, original_path "
            "FROM attachments WHERE id = ?"));
        query.addBindValue(attachmentId);
        QVERIFY(query.exec());
        QVERIFY(query.next());
        for (int column = 0; column < 3; ++column) {
            const QString relativePath = query.value(column).toString();
            QVERIFY(!relativePath.isEmpty());
            originalFiles.append(QDir(
                m_temporaryDirectory.filePath(QStringLiteral("uploads")))
                                     .filePath(relativePath));
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(originalConnection);
    QCOMPARE(originalFiles.size(), 3);
    for (const QString &path : std::as_const(originalFiles)) {
        QVERIFY(QFileInfo::exists(path));
    }
    QCOMPARE(request("GET", QStringLiteral("/api/attachments/%1").arg(attachmentId), {}, guestToken).status, 200);
    const QByteArray logData = QByteArrayLiteral("startup\nerror: failed to open config\n");
    const Reply logComment = requestCommentMultipart(
        commentsPath, QStringLiteral("[运行日志](upload:0)"),
        {{QStringLiteral("runtime.log"), logData}}, memberToken);
    QCOMPARE(logComment.status, 201);
    const QJsonObject logAttachment = logComment.json()
        .value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("comment")).toObject()
        .value(QStringLiteral("attachments")).toArray().at(0).toObject();
    QCOMPARE(logAttachment.value(QStringLiteral("content_type")).toString(),
             QStringLiteral("text/plain"));
    QVERIFY(!logAttachment.value(QStringLiteral("is_image")).toBool());
    const qint64 logAttachmentId = logAttachment.value(QStringLiteral("id")).toInteger();
    const Reply downloadedLog = request(
        "GET", QStringLiteral("/api/attachments/%1").arg(logAttachmentId), {}, guestToken);
    QCOMPARE(downloadedLog.status, 200);
    QCOMPARE(downloadedLog.body, logData);
    QCOMPARE(request("DELETE", QStringLiteral("/api/attachments/%1").arg(attachmentId), {}, m_token).status, 405);
    const qint64 largeAttachmentId = attachments.at(2).toObject()
                                         .value(QStringLiteral("id")).toInteger();
    const Reply scaledLarge = request(
        "GET", QStringLiteral("/api/attachments/%1").arg(largeAttachmentId),
        {}, guestToken);
    QCOMPARE(scaledLarge.status, 200);
    const QImage decodedLarge = QImage::fromData(scaledLarge.body, "WEBP");
    QVERIFY(!decodedLarge.isNull());
    QCOMPARE(decodedLarge.width(), 1600);
    const qint64 tallAttachmentId = attachments.at(3).toObject()
                                        .value(QStringLiteral("id")).toInteger();
    const Reply scaledTall = request(
        "GET", QStringLiteral("/api/attachments/%1").arg(tallAttachmentId),
        {}, guestToken);
    QCOMPARE(scaledTall.status, 200);
    const QImage decodedTall = QImage::fromData(scaledTall.body, "WEBP");
    QVERIFY(!decodedTall.isNull());
    QVERIFY(decodedTall.width() <= 1600);
    QVERIFY(decodedTall.height() <= 16383);
    QVERIFY(decodedTall.height() > decodedTall.width());
    const Reply comments = request("GET", commentsPath, {}, guestToken);
    QCOMPARE(comments.status, 200);
    QCOMPARE(comments.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("comments")).toArray().at(0).toObject()
                 .value(QStringLiteral("attachments")).toArray().size(), 4);
    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/%1").arg(issueId), {}, m_token).status, 200);
    for (const QString &path : std::as_const(originalFiles)) {
        QVERIFY(!QFileInfo::exists(path));
    }
    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(blockId), {}, m_token).status, 200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(member.id), {}, m_token).status, 200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(guest.id), {}, m_token).status, 200);
}

void HttpServerIntegrationTest::legacyAttachmentTestRemoved()
{
    QSKIP("Legacy Issue-level attachment test is replaced by comment image coverage.");
    QString errorMessage;
    UserSummary uploader;
    UserSummary other;
    UserSummary guest;
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("attachment_uploader"), &uploader, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("attachment_other"), &other, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(m_server->createManagedUser(
                 QStringLiteral("attachment_guest"), &guest, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(request(
        "PUT", QStringLiteral("/api/users/%1").arg(guest.id),
        {{QStringLiteral("username"), guest.username},
         {QStringLiteral("role"), QStringLiteral("guest")},
         {QStringLiteral("display_name"), QStringLiteral("Attachment Guest")}},
        m_token).status,
        200);

    const auto login = [this](const QString &username) {
        return request(
            "POST", QStringLiteral("/api/auth/login"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("password"), QStringLiteral("123456")}});
    };
    const Reply uploaderLogin = login(uploader.username);
    const Reply otherLogin = login(other.username);
    const Reply guestLogin = login(guest.username);
    QCOMPARE(uploaderLogin.status, 200);
    QCOMPARE(otherLogin.status, 200);
    QCOMPARE(guestLogin.status, 200);
    const QString uploaderToken = uploaderLogin.json()
                                      .value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("token")).toString();
    const QString otherToken = otherLogin.json()
                                   .value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("token")).toString();
    const QString guestToken = guestLogin.json()
                                   .value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("token")).toString();
    QVERIFY(!uploaderToken.isEmpty());
    QVERIFY(!otherToken.isEmpty());
    QVERIFY(!guestToken.isEmpty());

    const Reply blockCreated = request(
        "POST", QStringLiteral("/api/blocks"),
        {{QStringLiteral("title"), QStringLiteral("Attachment Block")}},
        m_token);
    QCOMPARE(blockCreated.status, 201);
    const qint64 blockId = blockCreated.json()
                               .value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("block")).toObject()
                               .value(QStringLiteral("id")).toInteger();
    QVERIFY(blockId > 0);

    const auto createIssue = [this, blockId, &uploaderToken](const QString &title) {
        return request(
            "POST", QStringLiteral("/api/issues"),
            {{QStringLiteral("block_id"), blockId},
             {QStringLiteral("title"), title}},
            uploaderToken);
    };
    const Reply firstIssueCreated = createIssue(QStringLiteral("Attachment Issue"));
    const Reply secondIssueCreated = createIssue(
        QStringLiteral("Block Attachment Issue"));
    QCOMPARE(firstIssueCreated.status, 201);
    QCOMPARE(secondIssueCreated.status, 201);
    const qint64 firstIssueId = firstIssueCreated.json()
                                    .value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("issue")).toObject()
                                    .value(QStringLiteral("id")).toInteger();
    const qint64 secondIssueId = secondIssueCreated.json()
                                     .value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("issue")).toObject()
                                     .value(QStringLiteral("id")).toInteger();
    QVERIFY(firstIssueId > 0);
    QVERIFY(secondIssueId > firstIssueId);

    QImage sourceImage(2000, 1200, QImage::Format_ARGB32);
    sourceImage.fill(qRgb(42, 118, 201));
    QByteArray pngData;
    QBuffer pngBuffer(&pngData);
    QVERIFY(pngBuffer.open(QIODevice::WriteOnly));
    QVERIFY(sourceImage.save(&pngBuffer, "PNG"));
    QVERIFY(!pngData.isEmpty());

    const auto upload = [this, &pngData, &uploaderToken](qint64 issueId,
                                                         const QString &name) {
        return requestMultipart(
            QStringLiteral("/api/issues/%1/attachments").arg(issueId),
            name, QByteArrayLiteral("image/png"), pngData, uploaderToken);
    };

    const QString firstListPath =
        QStringLiteral("/api/issues/%1/attachments").arg(firstIssueId);
    QCOMPARE(request("GET", firstListPath).status, 401);
    QCOMPARE(requestMultipart(
        firstListPath, QStringLiteral("guest.png"), QByteArrayLiteral("image/png"),
        pngData, guestToken).status,
        403);
    QCOMPARE(requestMultipart(
        firstListPath, QStringLiteral("invalid.txt"), QByteArrayLiteral("text/plain"),
        QByteArrayLiteral("not an image"), uploaderToken).status,
        400);
    QCOMPARE(requestMultipart(
        QStringLiteral("/api/issues/999999/attachments"),
        QStringLiteral("missing.png"), QByteArrayLiteral("image/png"), pngData,
        uploaderToken).status,
        404);
    QCOMPARE(requestRaw(
        "POST", firstListPath, QByteArrayLiteral("invalid multipart"),
        uploaderToken,
        QByteArrayLiteral("multipart/form-data; boundary=bad-boundary")).status,
        400);

    const Reply firstUpload = upload(firstIssueId, QStringLiteral("photo.png"));
    QCOMPARE(firstUpload.status, 201);
    const QJsonObject firstAttachment = firstUpload.json()
                                              .value(QStringLiteral("data")).toObject()
                                              .value(QStringLiteral("attachment")).toObject();
    const qint64 firstAttachmentId =
        firstAttachment.value(QStringLiteral("id")).toInteger();
    QVERIFY(firstAttachmentId > 0);
    QCOMPARE(firstAttachment.value(QStringLiteral("issue_id")).toInteger(),
             firstIssueId);
    QCOMPARE(firstAttachment.value(QStringLiteral("uploader_id")).toInteger(),
             uploader.id);
    QCOMPARE(firstAttachment.value(QStringLiteral("filename")).toString(),
             QStringLiteral("photo.png"));
    QVERIFY(firstAttachment.value(QStringLiteral("file_size")).toInteger() > 0);
    QVERIFY(!firstAttachment.contains(QStringLiteral("storage_path")));

    const Reply list = request("GET", firstListPath, {}, guestToken);
    QCOMPARE(list.status, 200);
    QCOMPARE(list.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("attachments")).toArray().size(),
             1);
    const Reply issueWithAttachment = request(
        "GET", QStringLiteral("/api/issues/%1").arg(firstIssueId), {},
        guestToken);
    QCOMPARE(issueWithAttachment.status, 200);
    QCOMPARE(issueWithAttachment.json().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("issue")).toObject()
                 .value(QStringLiteral("attachment_count")).toInteger(),
             qint64(1));
    QCOMPARE(request("GET", QStringLiteral("/api/issues/999999/attachments"), {},
                     guestToken).status,
             404);

    const QString firstImagePath =
        QStringLiteral("/api/attachments/%1").arg(firstAttachmentId);
    QCOMPARE(request("GET", firstImagePath).status, 401);
    QCOMPARE(request("GET", firstImagePath + QStringLiteral("?size=large"), {},
                     guestToken).status,
             400);
    const Reply fullImage = request("GET", firstImagePath, {}, guestToken);
    const Reply thumbnail = request(
        "GET", firstImagePath + QStringLiteral("?size=thumb"), {}, guestToken);
    QCOMPARE(fullImage.status, 200);
    QCOMPARE(thumbnail.status, 200);
    QVERIFY(fullImage.contentType.startsWith(QByteArrayLiteral("image/webp")));
    QVERIFY(thumbnail.contentType.startsWith(QByteArrayLiteral("image/webp")));
    const QImage decodedFull = QImage::fromData(fullImage.body, "WEBP");
    const QImage decodedThumbnail = QImage::fromData(thumbnail.body, "WEBP");
    QVERIFY(!decodedFull.isNull());
    QVERIFY(!decodedThumbnail.isNull());
    QCOMPARE(decodedFull.width(), 1600);
    QCOMPARE(decodedThumbnail.width(), 480);

    const QString databasePath =
        m_temporaryDirectory.filePath(QStringLiteral("issue_panel.db"));
    const QDir uploadRoot(m_temporaryDirectory.filePath(QStringLiteral("uploads")));
    const auto storedFiles = [databasePath, uploadRoot](qint64 id) {
        QStringList files;
        const QString connectionName = QStringLiteral("attachment_paths_%1").arg(id);
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(databasePath);
            if (database.open()) {
                QSqlQuery query(database);
                query.prepare(QStringLiteral(
                    "SELECT storage_path, thumb_path FROM attachments WHERE id = ?"));
                query.addBindValue(id);
                if (query.exec() && query.next()) {
                    files.append(uploadRoot.filePath(query.value(0).toString()));
                    files.append(uploadRoot.filePath(query.value(1).toString()));
                }
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        return files;
    };
    const QStringList firstFiles = storedFiles(firstAttachmentId);
    QCOMPARE(firstFiles.size(), 2);
    for (const QString &path : firstFiles) {
        QVERIFY(QFileInfo::exists(path));
    }
    QCOMPARE(request("DELETE", firstImagePath, {}, otherToken).status, 403);
    QCOMPARE(request("DELETE", firstImagePath, {}, guestToken).status, 403);
    QCOMPARE(request("DELETE", firstImagePath, {}, uploaderToken).status, 200);
    for (const QString &path : firstFiles) {
        QVERIFY(!QFileInfo::exists(path));
    }
    QCOMPARE(request("GET", firstImagePath, {}, uploaderToken).status, 404);

    const Reply adminDeleteUpload = upload(
        firstIssueId, QStringLiteral("admin-delete.png"));
    QCOMPARE(adminDeleteUpload.status, 201);
    const qint64 adminDeleteAttachmentId = adminDeleteUpload.json()
                                               .value(QStringLiteral("data")).toObject()
                                               .value(QStringLiteral("attachment")).toObject()
                                               .value(QStringLiteral("id")).toInteger();
    QCOMPARE(request(
        "DELETE",
        QStringLiteral("/api/attachments/%1").arg(adminDeleteAttachmentId),
        {}, m_token).status,
        200);

    const Reply issueDeleteUpload = upload(
        firstIssueId, QStringLiteral("issue-delete.png"));
    const Reply blockDeleteUpload = upload(
        secondIssueId, QStringLiteral("block-delete.png"));
    QCOMPARE(issueDeleteUpload.status, 201);
    QCOMPARE(blockDeleteUpload.status, 201);
    const qint64 issueDeleteAttachmentId = issueDeleteUpload.json()
                                               .value(QStringLiteral("data")).toObject()
                                               .value(QStringLiteral("attachment")).toObject()
                                               .value(QStringLiteral("id")).toInteger();
    const qint64 blockDeleteAttachmentId = blockDeleteUpload.json()
                                               .value(QStringLiteral("data")).toObject()
                                               .value(QStringLiteral("attachment")).toObject()
                                               .value(QStringLiteral("id")).toInteger();
    const QStringList issueFiles = storedFiles(issueDeleteAttachmentId);
    const QStringList blockFiles = storedFiles(blockDeleteAttachmentId);
    QCOMPARE(issueFiles.size(), 2);
    QCOMPARE(blockFiles.size(), 2);
    for (const QString &path : issueFiles + blockFiles) {
        QVERIFY(QFileInfo::exists(path));
    }

    QCOMPARE(request("DELETE", QStringLiteral("/api/issues/%1").arg(firstIssueId),
                     {}, m_token).status,
             200);
    for (const QString &path : issueFiles) {
        QVERIFY(!QFileInfo::exists(path));
    }
    for (const QString &path : blockFiles) {
        QVERIFY(QFileInfo::exists(path));
    }

    QCOMPARE(request("DELETE", QStringLiteral("/api/blocks/%1").arg(blockId),
                     {}, m_token).status,
             200);
    for (const QString &path : blockFiles) {
        QVERIFY(!QFileInfo::exists(path));
    }
    QCOMPARE(request("GET", QStringLiteral("/api/issues/%1").arg(secondIssueId),
                     {}, m_token).status,
             404);

    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(uploader.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(other.id),
                     {}, m_token).status,
             200);
    QCOMPARE(request("DELETE", QStringLiteral("/api/users/%1").arg(guest.id),
                     {}, m_token).status,
             200);
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
    const QString &token,
    const QByteArray &contentType)
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
                          QString::fromLatin1(contentType));
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
    reply.contentType = networkReply->rawHeader("Content-Type");
    reply.error = networkReply->error();
    networkReply->deleteLater();
    return reply;
}

HttpServerIntegrationTest::Reply HttpServerIntegrationTest::requestMultipart(
    const QString &path,
    const QString &filename,
    const QByteArray &contentType,
    const QByteArray &fileData,
    const QString &token)
{
    const QByteArray boundary = QByteArrayLiteral(
        "WorkHandlerTestBoundary7MA4YWxkTrZu0gW");
    QByteArray payload;
    payload += "--" + boundary + "\r\n";
    payload += "Content-Disposition: form-data; name=\"file\"; filename=\""
        + filename.toUtf8() + "\"\r\n";
    payload += "Content-Type: " + contentType + "\r\n\r\n";
    payload += fileData;
    payload += "\r\n--" + boundary + "--\r\n";
    return requestRaw(
        QByteArrayLiteral("POST"), path, payload, token,
        QByteArrayLiteral("multipart/form-data; boundary=") + boundary);
}

HttpServerIntegrationTest::Reply
HttpServerIntegrationTest::requestCommentMultipart(
    const QString &path,
    const QString &content,
    const QList<QPair<QString, QByteArray>> &files,
    const QString &token)
{
    const QByteArray boundary = QByteArrayLiteral(
        "WorkHandlerCommentBoundary7MA4YWxkTrZu0gW");
    QByteArray payload;
    payload += "--" + boundary + "\r\n";
    payload += "Content-Disposition: form-data; name=\"content\"\r\n";
    payload += "Content-Type: text/plain; charset=utf-8\r\n\r\n";
    payload += content.toUtf8();
    payload += "\r\n";
    for (const auto &file : files) {
        payload += "--" + boundary + "\r\n";
        payload += "Content-Disposition: form-data; name=\"files\"; filename=\""
            + file.first.toUtf8() + "\"\r\n";
        payload += "Content-Type: image/png\r\n\r\n";
        payload += file.second;
        payload += "\r\n";
    }
    payload += "--" + boundary + "--\r\n";
    return requestRaw(
        QByteArrayLiteral("POST"), path, payload, token,
        QByteArrayLiteral("multipart/form-data; boundary=") + boundary);
}

QTEST_GUILESS_MAIN(HttpServerIntegrationTest)

#include "httpserverintegrationtest.moc"
