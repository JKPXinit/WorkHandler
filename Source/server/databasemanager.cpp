#include "databasemanager.h"

#include "passwordhasher.h"

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
QByteArray randomSecret(int length)
{
    QByteArray bytes(length, Qt::Uninitialized);
    QRandomGenerator *generator = QRandomGenerator::system();
    for (int offset = 0; offset < length; offset += int(sizeof(quint32))) {
        const quint32 value = generator->generate();
        const int count = qMin(int(sizeof(value)), length - offset);
        for (int i = 0; i < count; ++i) {
            bytes[offset + i] = char((value >> (i * 8)) & 0xff);
        }
    }
    return bytes;
}
}

QJsonObject UserRecord::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("username"), username},
        {QStringLiteral("role"), role},
        {QStringLiteral("display_name"), displayName},
        {QStringLiteral("created_at"), createdAt}
    };
}

UserSummary UserRecord::toSummary() const
{
    UserSummary summary;
    summary.id = id;
    summary.username = username;
    summary.role = role;
    summary.displayName = displayName;
    summary.createdAt = createdAt;
    summary.usesDefaultPassword = PasswordHasher::verifyPassword(
        QStringLiteral("123456"), passwordHash);
    return summary;
}

DatabaseManager::DatabaseManager(const QString &databasePath)
    : m_databasePath(databasePath)
    , m_connectionName(QStringLiteral("workhandler_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16))
{
}

DatabaseManager::~DatabaseManager()
{
    if (m_database.isValid()) {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DatabaseManager::initialize(QString *errorMessage,
                                 QString *bootstrapAdminPassword)
{
    if (bootstrapAdminPassword) {
        bootstrapAdminPassword->clear();
    }

    const QFileInfo databaseInfo(m_databasePath);
    m_wasCreated = !databaseInfo.exists();
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        setError(errorMessage,
                 QStringLiteral("Failed to create data directory: %1")
                     .arg(databaseInfo.absolutePath()));
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }

    const QStringList schema = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT NOT NULL UNIQUE,"
            "password TEXT NOT NULL,"
            "role TEXT NOT NULL CHECK(role IN ('admin','user','guest')),"
            "display_name TEXT,"
            "token_version INTEGER NOT NULL DEFAULT 0,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS blocks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "title TEXT NOT NULL, description TEXT, color TEXT DEFAULT '#3b82f6',"
            "sort_order INTEGER DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS issues ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "block_id INTEGER NOT NULL REFERENCES blocks(id) ON DELETE CASCADE,"
            "title TEXT NOT NULL, description TEXT,"
            "status TEXT DEFAULT 'open' CHECK(status IN ('open','in_progress','resolved','closed')),"
            "priority TEXT DEFAULT 'medium',"
            "reporter_id INTEGER NOT NULL REFERENCES users(id),"
            "assignee_id INTEGER REFERENCES users(id),"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_issues_block_id "
            "ON issues(block_id)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_issues_status "
            "ON issues(status)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_issues_assignee_id "
            "ON issues(assignee_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS comments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "issue_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,"
            "user_id INTEGER NOT NULL REFERENCES users(id),"
            "content TEXT NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS attachments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "issue_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,"
            "uploader_id INTEGER NOT NULL, filename TEXT NOT NULL,"
            "storage_path TEXT NOT NULL, thumb_path TEXT, file_size INTEGER,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS notifications ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "type TEXT NOT NULL CHECK(type IN ('issue_created','issue_assigned','comment_added','status_changed')),"
            "title TEXT NOT NULL, content TEXT, related_id INTEGER,"
            "sender_id INTEGER REFERENCES users(id), recipient_id INTEGER NOT NULL,"
            "is_read INTEGER DEFAULT 0, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_notifications_unread "
            "ON notifications(recipient_id, is_read)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS security_state ("
            "key TEXT PRIMARY KEY, value TEXT NOT NULL)")
    };

    for (const QString &statement : schema) {
        if (!execute(statement, errorMessage)) {
            return false;
        }
    }

    bool hasTokenVersion = false;
    QSqlQuery userColumnsQuery(m_database);
    if (!userColumnsQuery.exec(QStringLiteral("PRAGMA table_info(users)"))) {
        setError(errorMessage, userColumnsQuery.lastError().text());
        return false;
    }
    while (userColumnsQuery.next()) {
        if (userColumnsQuery.value(1).toString()
            == QStringLiteral("token_version")) {
            hasTokenVersion = true;
            break;
        }
    }
    userColumnsQuery.finish();
    if (!hasTokenVersion
        && !execute(QStringLiteral(
            "ALTER TABLE users ADD COLUMN token_version "
            "INTEGER NOT NULL DEFAULT 0"), errorMessage)) {
        return false;
    }

    // Migrate the old configuration table once. It may contain HTTP settings
    // that are no longer database-owned; only the persistent token secret is
    // security state worth retaining.
    QSqlQuery legacyTableQuery(m_database);
    if (!legacyTableQuery.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' "
            "AND name = 'system_config'"))) {
        setError(errorMessage, legacyTableQuery.lastError().text());
        return false;
    }
    const bool hasLegacyTable = legacyTableQuery.next();
    legacyTableQuery.finish();
    if (hasLegacyTable) {
        if (!execute(QStringLiteral(
            "INSERT OR IGNORE INTO security_state(key, value) "
            "SELECT key, value FROM system_config "
            "WHERE key = 'token_secret' AND value <> ''"),
                     errorMessage)) {
            return false;
        }
        if (!execute(QStringLiteral("DROP TABLE system_config"),
                     errorMessage)) {
            return false;
        }
    }

    const QString tokenSecret = QString::fromLatin1(
        randomSecret(32).toBase64(QByteArray::OmitTrailingEquals));
    QSqlQuery tokenSecretQuery(m_database);
    tokenSecretQuery.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO security_state(key, value) VALUES(?, ?)"));
    tokenSecretQuery.addBindValue(QStringLiteral("token_secret"));
    tokenSecretQuery.addBindValue(tokenSecret);
    if (!tokenSecretQuery.exec()) {
        setError(errorMessage, tokenSecretQuery.lastError().text());
        return false;
    }
    tokenSecretQuery.finish();

    QSqlQuery countQuery(m_database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM users"))
        || !countQuery.next()) {
        setError(errorMessage, countQuery.lastError().text());
        return false;
    }
    const bool createBootstrapAdmin = countQuery.value(0).toInt() == 0;
    countQuery.finish();
    if (createBootstrapAdmin) {
        const QString password = PasswordHasher::generatePassword();
        UserRecord admin;
        if (!createUser(QStringLiteral("admin"),
                        PasswordHasher::hashPassword(password),
                        QStringLiteral("admin"),
                        QStringLiteral("Administrator"),
                        &admin,
                        errorMessage)) {
            return false;
        }
        if (bootstrapAdminPassword) {
            *bootstrapAdminPassword = password;
        }
    }

    return true;
}

bool DatabaseManager::isOpen() const
{
    return m_database.isOpen();
}

QSqlDatabase DatabaseManager::connection() const
{
    return m_database;
}

bool DatabaseManager::wasCreated() const
{
    return m_wasCreated;
}

QList<UserRecord> DatabaseManager::users(QString *errorMessage) const
{
    QList<UserRecord> records;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT id, username, password, role, display_name, created_at, "
            "token_version "
            "FROM users ORDER BY id"))) {
        setError(errorMessage, query.lastError().text());
        return records;
    }
    while (query.next()) {
        records.append(readUser(query));
    }
    return records;
}

std::optional<UserRecord> DatabaseManager::userById(qint64 id,
                                                     QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, username, password, role, display_name, created_at, "
        "token_version "
        "FROM users WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return readUser(query);
}

std::optional<UserRecord> DatabaseManager::userByUsername(
    const QString &username, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, username, password, role, display_name, created_at, "
        "token_version "
        "FROM users WHERE username = ?"));
    query.addBindValue(username);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return readUser(query);
}

bool DatabaseManager::usernameExists(const QString &username, qint64 exceptId) const
{
    QSqlQuery query(m_database);
    query.prepare(exceptId > 0
        ? QStringLiteral(
              "SELECT 1 FROM users WHERE username = ? COLLATE NOCASE AND id != ?")
        : QStringLiteral(
              "SELECT 1 FROM users WHERE username = ? COLLATE NOCASE"));
    query.addBindValue(username);
    if (exceptId > 0) {
        query.addBindValue(exceptId);
    }
    return query.exec() && query.next();
}

bool DatabaseManager::createUser(const QString &username,
                                 const QString &passwordHash,
                                 const QString &role,
                                 const QString &displayName,
                                 UserRecord *createdUser,
                                 QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO users(username, password, role, display_name) VALUES(?, ?, ?, ?)"));
    query.addBindValue(username);
    query.addBindValue(passwordHash);
    query.addBindValue(role);
    query.addBindValue(displayName);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }

    const auto record = userById(query.lastInsertId().toLongLong(), errorMessage);
    if (!record) {
        return false;
    }
    if (createdUser) {
        *createdUser = *record;
    }
    return true;
}

bool DatabaseManager::updateUser(qint64 id,
                                 const QString &username,
                                 const QString &role,
                                 const QString &displayName,
                                 const QString &passwordHash,
                                 UserRecord *updatedUser,
                                 QString *errorMessage)
{
    QSqlQuery query(m_database);
    if (passwordHash.isEmpty()) {
        query.prepare(QStringLiteral(
            "UPDATE users SET username = ?, role = ?, display_name = ? WHERE id = ?"));
    } else {
        query.prepare(QStringLiteral(
            "UPDATE users SET username = ?, role = ?, display_name = ?, password = ? "
            "WHERE id = ?"));
    }
    query.addBindValue(username);
    query.addBindValue(role);
    query.addBindValue(displayName);
    if (!passwordHash.isEmpty()) {
        query.addBindValue(passwordHash);
    }
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorMessage, QStringLiteral("User was not found."));
        return false;
    }

    const auto record = userById(id, errorMessage);
    if (!record) {
        return false;
    }
    if (updatedUser) {
        *updatedUser = *record;
    }
    return true;
}

bool DatabaseManager::deleteUser(qint64 id, QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM users WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorMessage, QStringLiteral("User was not found."));
        return false;
    }
    return true;
}

int DatabaseManager::adminCount(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE role = 'admin'"));
    if (!query.exec() || !query.next()) {
        setError(errorMessage, query.lastError().text());
        return -1;
    }
    return query.value(0).toInt();
}

bool DatabaseManager::updatePasswordAndTokenSecret(
    qint64 userId,
    const QString &passwordHash,
    QByteArray *newTokenSecret,
    QString *errorMessage)
{
    if (newTokenSecret) {
        newTokenSecret->clear();
    }

    if (!m_database.transaction()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }

    QSqlQuery passwordQuery(m_database);
    passwordQuery.prepare(QStringLiteral(
        "UPDATE users SET password = ?, token_version = token_version + 1 "
        "WHERE id = ?"));
    passwordQuery.addBindValue(passwordHash);
    passwordQuery.addBindValue(userId);
    if (!passwordQuery.exec() || passwordQuery.numRowsAffected() != 1) {
        const QString detail = passwordQuery.lastError().text();
        m_database.rollback();
        setError(errorMessage, detail.isEmpty()
                                   ? QStringLiteral("User was not found.")
                                   : detail);
        return false;
    }

    const QByteArray secret = randomSecret(32);
    const QString encodedSecret = QString::fromLatin1(
        secret.toBase64(QByteArray::OmitTrailingEquals));
    if (!setSecurityState(QStringLiteral("token_secret"),
                          encodedSecret,
                          errorMessage)) {
        m_database.rollback();
        return false;
    }

    if (!m_database.commit()) {
        setError(errorMessage, m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    if (newTokenSecret) {
        *newTokenSecret = secret;
    }
    return true;
}

bool DatabaseManager::updatePasswordAndIncrementTokenVersion(
    qint64 userId,
    const QString &passwordHash,
    int *newTokenVersion,
    QString *errorMessage)
{
    if (newTokenVersion) {
        *newTokenVersion = -1;
    }
    if (!m_database.transaction()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }

    QSqlQuery updateQuery(m_database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE users SET password = ?, token_version = token_version + 1 "
        "WHERE id = ?"));
    updateQuery.addBindValue(passwordHash);
    updateQuery.addBindValue(userId);
    if (!updateQuery.exec() || updateQuery.numRowsAffected() != 1) {
        const QString detail = updateQuery.lastError().text();
        m_database.rollback();
        setError(errorMessage, detail.isEmpty()
                                   ? QStringLiteral("User was not found.")
                                   : detail);
        return false;
    }

    QSqlQuery versionQuery(m_database);
    versionQuery.prepare(QStringLiteral(
        "SELECT token_version FROM users WHERE id = ?"));
    versionQuery.addBindValue(userId);
    if (!versionQuery.exec() || !versionQuery.next()) {
        const QString detail = versionQuery.lastError().text();
        m_database.rollback();
        setError(errorMessage, detail.isEmpty()
                                   ? QStringLiteral("User was not found.")
                                   : detail);
        return false;
    }
    const int tokenVersion = versionQuery.value(0).toInt();

    if (!m_database.commit()) {
        setError(errorMessage, m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    if (newTokenVersion) {
        *newTokenVersion = tokenVersion;
    }
    return true;
}

QByteArray DatabaseManager::tokenSecret(QString *errorMessage) const
{
    return QByteArray::fromBase64(
        securityState(QStringLiteral("token_secret"), QString(), errorMessage).toLatin1());
}

bool DatabaseManager::execute(const QString &sql, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    return true;
}

bool DatabaseManager::setSecurityState(const QString &key,
                                       const QString &value,
                                       QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO security_state(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    return true;
}

QString DatabaseManager::securityState(const QString &key,
                                       const QString &fallback,
                                       QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT value FROM security_state WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return fallback;
    }
    return query.next() ? query.value(0).toString() : fallback;
}

UserRecord DatabaseManager::readUser(const QSqlQuery &query)
{
    UserRecord user;
    user.id = query.value(0).toLongLong();
    user.username = query.value(1).toString();
    user.passwordHash = query.value(2).toString();
    user.role = query.value(3).toString();
    user.displayName = query.value(4).toString();
    user.createdAt = query.value(5).toString();
    user.tokenVersion = query.value(6).toInt();
    return user;
}

void DatabaseManager::setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}
