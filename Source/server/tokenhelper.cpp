#include "tokenhelper.h"

#include <QDateTime>
#include <QList>
#include <QMessageAuthenticationCode>

TokenHelper::TokenHelper(const QByteArray &secret)
    : m_secret(secret)
{
}

void TokenHelper::setSecret(const QByteArray &secret)
{
    m_secret = secret;
}

QString TokenHelper::issue(qint64 userId,
                           const QString &role,
                           int tokenVersion,
                           qint64 lifetimeSeconds) const
{
    const qint64 expiresAt = QDateTime::currentSecsSinceEpoch() + lifetimeSeconds;
    const QByteArray claims = QByteArray::number(userId) + ':'
        + role.toUtf8() + ':' + QByteArray::number(tokenVersion) + ':'
        + QByteArray::number(expiresAt);
    const QByteArray payload = claims.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray digest = signature(payload).toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(payload + ':' + digest);
}

TokenHelper::Claims TokenHelper::validate(const QString &token) const
{
    Claims claims;
    if (m_secret.isEmpty()) {
        claims.error = QStringLiteral("Token secret is unavailable.");
        return claims;
    }

    const QByteArray encoded = token.toLatin1();
    const QList<QByteArray> parts = encoded.split(':');
    if (parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
        claims.error = QStringLiteral("Malformed token.");
        return claims;
    }

    const QByteArray providedSignature = QByteArray::fromBase64(
        parts.at(1), QByteArray::Base64UrlEncoding);
    if (!constantTimeEquals(signature(parts.at(0)), providedSignature)) {
        claims.error = QStringLiteral("Invalid token signature.");
        return claims;
    }

    const QByteArray decoded = QByteArray::fromBase64(
        parts.at(0), QByteArray::Base64UrlEncoding);
    const QList<QByteArray> values = decoded.split(':');
    if (values.size() != 3 && values.size() != 4) {
        claims.error = QStringLiteral("Malformed token claims.");
        return claims;
    }

    bool idOk = false;
    bool versionOk = true;
    bool expiryOk = false;
    claims.userId = values.at(0).toLongLong(&idOk);
    claims.role = QString::fromUtf8(values.at(1));
    if (values.size() == 4) {
        claims.tokenVersion = values.at(2).toInt(&versionOk);
        claims.expiresAt = values.at(3).toLongLong(&expiryOk);
    } else {
        claims.tokenVersion = 0;
        claims.expiresAt = values.at(2).toLongLong(&expiryOk);
    }
    if (!idOk || !versionOk || !expiryOk || claims.userId < 1
        || claims.role.isEmpty() || claims.tokenVersion < 0) {
        claims.error = QStringLiteral("Invalid token claims.");
        return claims;
    }
    if (claims.expiresAt <= QDateTime::currentSecsSinceEpoch()) {
        claims.error = QStringLiteral("Token has expired.");
        return claims;
    }

    claims.valid = true;
    return claims;
}

QByteArray TokenHelper::signature(const QByteArray &payload) const
{
    return QMessageAuthenticationCode::hash(
        payload, m_secret, QCryptographicHash::Sha256);
}

bool TokenHelper::constantTimeEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (qsizetype i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
    }
    return difference == 0;
}
