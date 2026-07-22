#ifndef USEROPTIONSSERVICE_H
#define USEROPTIONSSERVICE_H

#include <QJsonObject>
#include <QList>
#include <QString>

class DatabaseManager;

struct UserOption
{
    qint64 id {0};
    QString username;
    QString displayName;

    QJsonObject toJson() const;
};

class UserOptionsService
{
public:
    explicit UserOptionsService(DatabaseManager &database);

    bool options(QList<UserOption> *options,
                 QString *errorMessage = nullptr) const;

private:
    DatabaseManager &m_database;
};

#endif // USEROPTIONSSERVICE_H
