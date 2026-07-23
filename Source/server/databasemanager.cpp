#include "databasemanager.h"

#include "passwordhasher.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QStringList>

#include <utility>

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
            "CREATE INDEX IF NOT EXISTS idx_comments_issue_id "
            "ON comments(issue_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS attachments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "issue_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,"
            "comment_id INTEGER NOT NULL REFERENCES comments(id) ON DELETE CASCADE,"
            "uploader_id INTEGER NOT NULL, filename TEXT NOT NULL,"
            "storage_path TEXT NOT NULL, thumb_path TEXT, original_path TEXT,"
            "file_size INTEGER,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_attachments_issue_id "
            "ON attachments(issue_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS notifications ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "type TEXT NOT NULL CHECK(type IN ('issue_created','issue_assigned','comment_added','status_changed')),"
            "title TEXT NOT NULL, content TEXT,"
            "related_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,"
            "sender_id INTEGER REFERENCES users(id) ON DELETE SET NULL,"
            "recipient_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
            "is_read INTEGER DEFAULT 0, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_notifications_unread "
            "ON notifications(recipient_id, is_read, created_at DESC, id DESC)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS security_state ("
            "key TEXT PRIMARY KEY, value TEXT NOT NULL)")
    };

    for (const QString &statement : schema) {
        if (!execute(statement, errorMessage)) {
            return false;
        }
    }

    if (!migrateAttachmentsToComments(errorMessage)
        || !execute(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_attachments_comment_id "
                "ON attachments(comment_id)"), errorMessage)
        || !migrateAttachmentOriginalPath(errorMessage)
        || !migrateNotifications(errorMessage)) {
        return false;
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

bool DatabaseManager::migrateAttachmentsToComments(QString *errorMessage)
{
    bool hasCommentId = false;
    QSqlQuery columnsQuery(m_database);
    if (!columnsQuery.exec(QStringLiteral("PRAGMA table_info(attachments)"))) {
        setError(errorMessage, columnsQuery.lastError().text());
        return false;
    }
    while (columnsQuery.next()) {
        if (columnsQuery.value(1).toString() == QStringLiteral("comment_id")) {
            hasCommentId = true;
            break;
        }
    }
    columnsQuery.finish();
    if (hasCommentId) {
        return true;
    }

    struct LegacyAttachment {
        qint64 id {0};
        qint64 issueId {0};
        qint64 uploaderId {0};
        QString filename;
        QString storagePath;
        QString thumbnailPath;
        QVariant fileSize;
        QString createdAt;
        QString groupKey;
    };
    struct CommentGroup {
        QString key;
        qint64 issueId {0};
        qint64 userId {0};
        QString createdAt;
    };

    QSet<qint64> validUserIds;
    qint64 fallbackAdminId = 0;
    QSqlQuery usersQuery(m_database);
    if (!usersQuery.exec(QStringLiteral("SELECT id, username FROM users"))) {
        setError(errorMessage, usersQuery.lastError().text());
        return false;
    }
    while (usersQuery.next()) {
        const qint64 userId = usersQuery.value(0).toLongLong();
        validUserIds.insert(userId);
        if (usersQuery.value(1).toString() == QStringLiteral("admin")) {
            fallbackAdminId = userId;
        }
    }
    usersQuery.finish();

    QList<LegacyAttachment> attachments;
    QList<CommentGroup> groups;
    QSet<QString> knownGroups;
    QSqlQuery attachmentQuery(m_database);
    if (!attachmentQuery.exec(QStringLiteral(
            "SELECT id, issue_id, uploader_id, filename, storage_path, "
            "COALESCE(thumb_path, ''), file_size, created_at "
            "FROM attachments ORDER BY created_at ASC, id ASC"))) {
        setError(errorMessage, attachmentQuery.lastError().text());
        return false;
    }
    while (attachmentQuery.next()) {
        LegacyAttachment attachment;
        attachment.id = attachmentQuery.value(0).toLongLong();
        attachment.issueId = attachmentQuery.value(1).toLongLong();
        attachment.uploaderId = attachmentQuery.value(2).toLongLong();
        attachment.filename = attachmentQuery.value(3).toString();
        attachment.storagePath = attachmentQuery.value(4).toString();
        attachment.thumbnailPath = attachmentQuery.value(5).toString();
        attachment.fileSize = attachmentQuery.value(6);
        attachment.createdAt = attachmentQuery.value(7).toString();
        const qint64 commentUserId = validUserIds.contains(attachment.uploaderId)
            ? attachment.uploaderId : fallbackAdminId;
        if (commentUserId <= 0) {
            setError(errorMessage,
                     QStringLiteral("Legacy attachments require the fixed admin account."));
            return false;
        }
        attachment.groupKey = QStringLiteral("%1:%2")
                                  .arg(attachment.issueId)
                                  .arg(commentUserId);
        if (!knownGroups.contains(attachment.groupKey)) {
            knownGroups.insert(attachment.groupKey);
            groups.append({attachment.groupKey, attachment.issueId,
                           commentUserId, attachment.createdAt});
        }
        attachments.append(attachment);
    }
    attachmentQuery.finish();

    if (!m_database.transaction()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }
    const auto rollback = [this, errorMessage](const QString &message) {
        m_database.rollback();
        setError(errorMessage, message);
        return false;
    };
    QSqlQuery schemaQuery(m_database);
    if (!schemaQuery.exec(QStringLiteral("DROP INDEX IF EXISTS idx_attachments_issue_id"))
        || !schemaQuery.exec(QStringLiteral("ALTER TABLE attachments RENAME TO attachments_legacy"))
        || !schemaQuery.exec(QStringLiteral(
            "CREATE TABLE attachments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "issue_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,"
            "comment_id INTEGER NOT NULL REFERENCES comments(id) ON DELETE CASCADE,"
            "uploader_id INTEGER NOT NULL,"
            "filename TEXT NOT NULL, storage_path TEXT NOT NULL,"
            "thumb_path TEXT, original_path TEXT, file_size INTEGER,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"))) {
        return rollback(schemaQuery.lastError().text());
    }

    QHash<QString, qint64> commentIds;
    QHash<QString, QStringList> commentLines;
    QSqlQuery commentInsert(m_database);
    commentInsert.prepare(QStringLiteral(
        "INSERT INTO comments(issue_id, user_id, content, created_at) "
        "VALUES(?, ?, '', ?)"));
    for (const CommentGroup &group : std::as_const(groups)) {
        commentInsert.clear();
        commentInsert.prepare(QStringLiteral(
            "INSERT INTO comments(issue_id, user_id, content, created_at) "
            "VALUES(?, ?, '', ?)"));
        commentInsert.addBindValue(group.issueId);
        commentInsert.addBindValue(group.userId);
        commentInsert.addBindValue(group.createdAt);
        if (!commentInsert.exec()) {
            return rollback(commentInsert.lastError().text());
        }
        commentIds.insert(group.key, commentInsert.lastInsertId().toLongLong());
        commentInsert.finish();
    }

    QSqlQuery attachmentInsert(m_database);
    attachmentInsert.prepare(QStringLiteral(
        "INSERT INTO attachments(id, issue_id, comment_id, uploader_id, filename, "
        "storage_path, thumb_path, file_size, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const LegacyAttachment &attachment : std::as_const(attachments)) {
        attachmentInsert.clear();
        attachmentInsert.prepare(QStringLiteral(
            "INSERT INTO attachments(id, issue_id, comment_id, uploader_id, filename, "
            "storage_path, thumb_path, file_size, created_at) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        attachmentInsert.addBindValue(attachment.id);
        attachmentInsert.addBindValue(attachment.issueId);
        attachmentInsert.addBindValue(commentIds.value(attachment.groupKey));
        attachmentInsert.addBindValue(validUserIds.contains(attachment.uploaderId)
                                          ? attachment.uploaderId
                                          : fallbackAdminId);
        attachmentInsert.addBindValue(attachment.filename);
        attachmentInsert.addBindValue(attachment.storagePath);
        attachmentInsert.addBindValue(attachment.thumbnailPath);
        attachmentInsert.addBindValue(attachment.fileSize);
        attachmentInsert.addBindValue(attachment.createdAt);
        if (!attachmentInsert.exec()) {
            return rollback(attachmentInsert.lastError().text());
        }
        commentLines[attachment.groupKey].append(
            QStringLiteral("![Image](attachment:%1)").arg(attachment.id));
        attachmentInsert.finish();
    }
    QSqlQuery commentUpdate(m_database);
    commentUpdate.prepare(QStringLiteral(
        "UPDATE comments SET content = ? WHERE id = ?"));
    for (auto iterator = commentLines.cbegin(); iterator != commentLines.cend();
         ++iterator) {
        commentUpdate.clear();
        commentUpdate.prepare(QStringLiteral(
            "UPDATE comments SET content = ? WHERE id = ?"));
        commentUpdate.addBindValue(iterator.value().join(QLatin1Char('\n')));
        commentUpdate.addBindValue(commentIds.value(iterator.key()));
        if (!commentUpdate.exec()) {
            return rollback(commentUpdate.lastError().text());
        }
        commentUpdate.finish();
    }
    if (!schemaQuery.exec(QStringLiteral("DROP TABLE attachments_legacy"))
        || !schemaQuery.exec(QStringLiteral(
            "CREATE INDEX idx_attachments_issue_id ON attachments(issue_id)"))
        || !schemaQuery.exec(QStringLiteral(
            "CREATE INDEX idx_attachments_comment_id ON attachments(comment_id)"))) {
        return rollback(schemaQuery.lastError().text());
    }
    if (!m_database.commit()) {
        const QString detail = m_database.lastError().text();
        m_database.rollback();
        setError(errorMessage, detail);
        return false;
    }
    return true;
}

bool DatabaseManager::migrateAttachmentOriginalPath(QString *errorMessage)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(attachments)"))) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == QStringLiteral("original_path")) {
            return true;
        }
    }
    query.finish();
    return execute(QStringLiteral(
        "ALTER TABLE attachments ADD COLUMN original_path TEXT"),
                   errorMessage);
}

bool DatabaseManager::migrateNotifications(QString *errorMessage)
{
    bool relatedIdNotNull = false;
    QSqlQuery columnsQuery(m_database);
    if (!columnsQuery.exec(QStringLiteral("PRAGMA table_info(notifications)"))) {
        setError(errorMessage, columnsQuery.lastError().text());
        return false;
    }
    while (columnsQuery.next()) {
        if (columnsQuery.value(1).toString() == QStringLiteral("related_id")) {
            relatedIdNotNull = columnsQuery.value(3).toInt() != 0;
            break;
        }
    }
    columnsQuery.finish();

    QHash<QString, QString> deleteActions;
    QSqlQuery foreignKeyQuery(m_database);
    if (!foreignKeyQuery.exec(QStringLiteral(
            "PRAGMA foreign_key_list(notifications)"))) {
        setError(errorMessage, foreignKeyQuery.lastError().text());
        return false;
    }
    while (foreignKeyQuery.next()) {
        deleteActions.insert(foreignKeyQuery.value(3).toString(),
                             foreignKeyQuery.value(6).toString().toUpper());
    }
    foreignKeyQuery.finish();

    const bool targetSchema = relatedIdNotNull
        && deleteActions.value(QStringLiteral("related_id"))
               == QStringLiteral("CASCADE")
        && deleteActions.value(QStringLiteral("sender_id"))
               == QStringLiteral("SET NULL")
        && deleteActions.value(QStringLiteral("recipient_id"))
               == QStringLiteral("CASCADE");
    QStringList indexColumns;
    QSqlQuery indexQuery(m_database);
    if (!indexQuery.exec(QStringLiteral(
            "PRAGMA index_info(idx_notifications_unread)"))) {
        setError(errorMessage, indexQuery.lastError().text());
        return false;
    }
    while (indexQuery.next()) {
        indexColumns.append(indexQuery.value(2).toString());
    }
    indexQuery.finish();
    const QStringList targetIndexColumns = {
        QStringLiteral("recipient_id"),
        QStringLiteral("is_read"),
        QStringLiteral("created_at"),
        QStringLiteral("id")
    };
    if (targetSchema) {
        if (indexColumns == targetIndexColumns) {
            return true;
        }
        return execute(QStringLiteral(
                   "DROP INDEX IF EXISTS idx_notifications_unread"),
                       errorMessage)
            && execute(QStringLiteral(
                   "CREATE INDEX idx_notifications_unread ON notifications("
                   "recipient_id, is_read, created_at DESC, id DESC)"),
                       errorMessage);
    }

    if (!m_database.transaction()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }
    const auto rollback = [this, errorMessage](const QString &message) {
        m_database.rollback();
        setError(errorMessage, message);
        return false;
    };

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "DROP INDEX IF EXISTS idx_notifications_unread"))
        || !query.exec(QStringLiteral(
            "ALTER TABLE notifications RENAME TO notifications_legacy"))
        || !query.exec(QStringLiteral(
            "CREATE TABLE notifications ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "type TEXT NOT NULL CHECK(type IN "
            "('issue_created','issue_assigned','comment_added','status_changed')),"
            "title TEXT NOT NULL, content TEXT,"
            "related_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,"
            "sender_id INTEGER REFERENCES users(id) ON DELETE SET NULL,"
            "recipient_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
            "is_read INTEGER DEFAULT 0,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"))
        || !query.exec(QStringLiteral(
            "INSERT INTO notifications("
            "id, type, title, content, related_id, sender_id, recipient_id, "
            "is_read, created_at) "
            "SELECT n.id, n.type, n.title, n.content, n.related_id, "
            "CASE WHEN sender.id IS NULL THEN NULL ELSE n.sender_id END, "
            "n.recipient_id, COALESCE(n.is_read, 0), n.created_at "
            "FROM notifications_legacy n "
            "JOIN users recipient ON recipient.id = n.recipient_id "
            "JOIN issues issue ON issue.id = n.related_id "
            "LEFT JOIN users sender ON sender.id = n.sender_id"))
        || !query.exec(QStringLiteral("DROP TABLE notifications_legacy"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX idx_notifications_unread ON notifications("
            "recipient_id, is_read, created_at DESC, id DESC)"))) {
        return rollback(query.lastError().text());
    }

    if (!m_database.commit()) {
        const QString detail = m_database.lastError().text();
        m_database.rollback();
        setError(errorMessage, detail);
        return false;
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

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
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
