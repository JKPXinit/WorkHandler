#ifndef PASSWORDHASHER_H
#define PASSWORDHASHER_H

#include <QByteArray>
#include <QString>

class PasswordHasher
{
public:
    static QString hashPassword(const QString &password);
    static bool verifyPassword(const QString &password, const QString &encodedHash);
    static QString generatePassword(int length = 16);

private:
    static QByteArray pbkdf2(const QByteArray &password,
                             const QByteArray &salt,
                             int iterations,
                             int outputLength);
    static QByteArray randomBytes(int length);
    static bool constantTimeEquals(const QByteArray &left, const QByteArray &right);
};

#endif // PASSWORDHASHER_H
