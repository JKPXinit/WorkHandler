#include "passwordhasher.h"

#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QStringList>

namespace {
constexpr int SaltLength = 16;
constexpr int HashLength = 32;
constexpr int Iterations = 100000;
}

QString PasswordHasher::hashPassword(const QString &password)
{
    const QByteArray salt = randomBytes(SaltLength);
    const QByteArray hash = pbkdf2(password.toUtf8(), salt, Iterations, HashLength);
    return QStringLiteral("pbkdf2_sha256$%1$%2$%3")
        .arg(Iterations)
        .arg(QString::fromLatin1(salt.toBase64(QByteArray::OmitTrailingEquals)))
        .arg(QString::fromLatin1(hash.toBase64(QByteArray::OmitTrailingEquals)));
}

bool PasswordHasher::verifyPassword(const QString &password, const QString &encodedHash)
{
    const QStringList parts = encodedHash.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != QStringLiteral("pbkdf2_sha256")) {
        return false;
    }

    bool iterationsOk = false;
    const int iterations = parts.at(1).toInt(&iterationsOk);
    const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1());
    const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1());
    if (!iterationsOk || iterations < 1 || salt.isEmpty() || expected.isEmpty()) {
        return false;
    }

    return constantTimeEquals(
        pbkdf2(password.toUtf8(), salt, iterations, expected.size()), expected);
}

QString PasswordHasher::generatePassword(int length)
{
    static const QByteArray alphabet(
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%+-_");
    const QByteArray random = randomBytes(qMax(1, length));
    QString password;
    password.reserve(random.size());
    for (const char byte : random) {
        const int index = static_cast<unsigned char>(byte) % alphabet.size();
        password.append(QLatin1Char(alphabet.at(index)));
    }
    return password;
}

QByteArray PasswordHasher::pbkdf2(const QByteArray &password,
                                  const QByteArray &salt,
                                  int iterations,
                                  int outputLength)
{
    constexpr int DigestLength = 32;
    QByteArray output;
    output.reserve(outputLength);

    for (quint32 block = 1; output.size() < outputLength; ++block) {
        QByteArray blockSalt = salt;
        blockSalt.append(char((block >> 24) & 0xff));
        blockSalt.append(char((block >> 16) & 0xff));
        blockSalt.append(char((block >> 8) & 0xff));
        blockSalt.append(char(block & 0xff));

        QByteArray value = QMessageAuthenticationCode::hash(
            blockSalt, password, QCryptographicHash::Sha256);
        QByteArray accumulated = value;
        for (int iteration = 1; iteration < iterations; ++iteration) {
            value = QMessageAuthenticationCode::hash(
                value, password, QCryptographicHash::Sha256);
            for (int i = 0; i < DigestLength; ++i) {
                accumulated[i] = accumulated.at(i) ^ value.at(i);
            }
        }
        output.append(accumulated);
    }

    output.truncate(outputLength);
    return output;
}

QByteArray PasswordHasher::randomBytes(int length)
{
    QByteArray output(length, Qt::Uninitialized);
    QRandomGenerator *generator = QRandomGenerator::system();
    for (int offset = 0; offset < length; offset += int(sizeof(quint32))) {
        const quint32 value = generator->generate();
        const int count = qMin(int(sizeof(value)), length - offset);
        for (int i = 0; i < count; ++i) {
            output[offset + i] = char((value >> (i * 8)) & 0xff);
        }
    }
    return output;
}

bool PasswordHasher::constantTimeEquals(const QByteArray &left, const QByteArray &right)
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
