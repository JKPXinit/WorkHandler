#ifndef TOKENHELPER_H
#define TOKENHELPER_H

#include <QByteArray>
#include <QString>

class TokenHelper
{
public:
    struct Claims {
        bool valid {false};
        qint64 userId {0};
        QString role;
        qint64 expiresAt {0};
        QString error;
    };

    explicit TokenHelper(const QByteArray &secret = QByteArray());

    void setSecret(const QByteArray &secret);
    QString issue(qint64 userId, const QString &role, qint64 lifetimeSeconds = 8 * 60 * 60) const;
    Claims validate(const QString &token) const;

private:
    QByteArray signature(const QByteArray &payload) const;
    static bool constantTimeEquals(const QByteArray &left, const QByteArray &right);

    QByteArray m_secret;
};

#endif // TOKENHELPER_H
