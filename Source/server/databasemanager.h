#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

struct UserRecord
{
    qint64 id {0};
    QString username;
    QString passwordHash;
    QString role;
    QString displayName;
    QString createdAt;

    QJsonObject toJson() const;
};

struct ServerConfig
{
    QString serverInterface {QStringLiteral("0.0.0.0")};
    quint16 serverPort {8080};
    bool autoStart {true};
    bool keepOriginal {false};
    int maxImageWidth {1920};

    QJsonObject toJson() const;
};

class DatabaseManager
{
public:
    explicit DatabaseManager(const QString &databasePath);
    ~DatabaseManager();

    bool initialize(QString *errorMessage = nullptr,
                    QString *bootstrapAdminPassword = nullptr);
    bool isOpen() const;
    bool wasCreated() const;

    QList<UserRecord> users(QString *errorMessage = nullptr) const;
    std::optional<UserRecord> userById(qint64 id, QString *errorMessage = nullptr) const;
    std::optional<UserRecord> userByUsername(const QString &username,
                                             QString *errorMessage = nullptr) const;
    bool usernameExists(const QString &username, qint64 exceptId = 0) const;
    bool createUser(const QString &username,
                    const QString &passwordHash,
                    const QString &role,
                    const QString &displayName,
                    UserRecord *createdUser,
                    QString *errorMessage = nullptr);
    bool updateUser(qint64 id,
                    const QString &username,
                    const QString &role,
                    const QString &displayName,
                    const QString &passwordHash,
                    UserRecord *updatedUser,
                    QString *errorMessage = nullptr);
    bool deleteUser(qint64 id, QString *errorMessage = nullptr);
    int adminCount(QString *errorMessage = nullptr) const;

    ServerConfig serverConfig(QString *errorMessage = nullptr) const;
    bool setServerConfig(const ServerConfig &config, QString *errorMessage = nullptr);
    QByteArray tokenSecret(QString *errorMessage = nullptr) const;

private:
    bool execute(const QString &sql, QString *errorMessage = nullptr) const;
    bool setConfigValue(const QString &key,
                        const QString &value,
                        QString *errorMessage = nullptr);
    QString configValue(const QString &key,
                        const QString &fallback = QString(),
                        QString *errorMessage = nullptr) const;
    static UserRecord readUser(const class QSqlQuery &query);
    static void setError(QString *target, const QString &message);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
    bool m_wasCreated {false};
};

#endif // DATABASEMANAGER_H
