#include "users/useroptionsservice.h"

#include "databasemanager.h"

QJsonObject UserOption::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("username"), username},
        {QStringLiteral("display_name"), displayName}
    };
}

UserOptionsService::UserOptionsService(DatabaseManager &database)
    : m_database(database)
{
}

bool UserOptionsService::options(QList<UserOption> *options,
                                 QString *errorMessage) const
{
    if (options) {
        options->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    QString databaseError;
    const QList<UserRecord> users = m_database.users(&databaseError);
    if (!databaseError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = databaseError;
        }
        return false;
    }

    if (options) {
        options->reserve(users.size());
        for (const UserRecord &user : users) {
            options->append(UserOption{
                user.id, user.username, user.displayName
            });
        }
    }
    return true;
}
