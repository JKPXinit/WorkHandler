#include "api/multipartparser.h"

#include <QFileInfo>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace {
QByteArray boundaryFromContentType(const QByteArray &contentType)
{
    const QList<QByteArray> parameters = contentType.split(';');
    if (parameters.isEmpty()
        || parameters.first().trimmed().toLower()
            != QByteArrayLiteral("multipart/form-data")) {
        return {};
    }
    for (qsizetype index = 1; index < parameters.size(); ++index) {
        const QByteArray parameter = parameters.at(index).trimmed();
        const qsizetype equals = parameter.indexOf('=');
        if (equals < 0
            || parameter.left(equals).trimmed().toLower()
                != QByteArrayLiteral("boundary")) {
            continue;
        }
        QByteArray boundary = parameter.mid(equals + 1).trimmed();
        if (boundary.size() >= 2 && boundary.front() == '"'
            && boundary.back() == '"') {
            boundary = boundary.mid(1, boundary.size() - 2);
        }
        return boundary;
    }
    return {};
}

QString dispositionValue(const QString &disposition, const QString &name)
{
    const QRegularExpression expression(
        QStringLiteral("(?:^|;)\\s*%1=\\\"([^\\\"]*)\\\"")
            .arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(disposition);
    return match.hasMatch() ? match.captured(1) : QString();
}

void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}
}

bool MultipartParser::parseSingleFile(const QHttpServerRequest &request,
                                      const QByteArray &fieldName,
                                      qsizetype maximumBodySize,
                                      MultipartFile *file,
                                      QString *errorMessage)
{
    if (file) {
        *file = {};
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    const QByteArray body = request.body();
    if (body.isEmpty() || body.size() > maximumBodySize) {
        setError(errorMessage,
                 body.isEmpty()
                     ? QStringLiteral("Multipart request body is empty.")
                     : QStringLiteral("Uploaded file exceeds the request size limit."));
        return false;
    }

    const QByteArray boundary = boundaryFromContentType(
        request.value("Content-Type"));
    if (boundary.isEmpty() || boundary.size() > 200
        || boundary.contains('\r') || boundary.contains('\n')) {
        setError(errorMessage, QStringLiteral("Multipart boundary is invalid."));
        return false;
    }

    const QByteArray delimiter = QByteArrayLiteral("--") + boundary;
    qsizetype position = 0;
    int fileCount = 0;
    MultipartFile parsedFile;
    while (true) {
        const qsizetype boundaryStart = body.indexOf(delimiter, position);
        if (boundaryStart < 0) {
            break;
        }
        qsizetype partStart = boundaryStart + delimiter.size();
        if (body.mid(partStart, 2) == QByteArrayLiteral("--")) {
            break;
        }
        if (body.mid(partStart, 2) != QByteArrayLiteral("\r\n")) {
            setError(errorMessage, QStringLiteral("Multipart body is malformed."));
            return false;
        }
        partStart += 2;

        const qsizetype headerEnd = body.indexOf(QByteArrayLiteral("\r\n\r\n"),
                                                 partStart);
        if (headerEnd < 0) {
            setError(errorMessage, QStringLiteral("Multipart headers are malformed."));
            return false;
        }
        const qsizetype nextBoundary = body.indexOf(
            QByteArrayLiteral("\r\n") + delimiter, headerEnd + 4);
        if (nextBoundary < 0) {
            setError(errorMessage, QStringLiteral("Multipart body is incomplete."));
            return false;
        }

        QString disposition;
        QByteArray contentType;
        const QList<QByteArray> headerLines = body.mid(
            partStart, headerEnd - partStart).split('\n');
        for (QByteArray line : headerLines) {
            line = line.trimmed();
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0) {
                continue;
            }
            const QByteArray headerName = line.left(colon).trimmed().toLower();
            const QByteArray headerValue = line.mid(colon + 1).trimmed();
            if (headerName == QByteArrayLiteral("content-disposition")) {
                disposition = QString::fromUtf8(headerValue);
            } else if (headerName == QByteArrayLiteral("content-type")) {
                contentType = headerValue.toLower();
            }
        }

        if (dispositionValue(disposition, QStringLiteral("name"))
            == QString::fromLatin1(fieldName)) {
            ++fileCount;
            parsedFile.filename = QFileInfo(
                dispositionValue(disposition, QStringLiteral("filename"))).fileName();
            parsedFile.contentType = contentType;
            parsedFile.data = body.mid(headerEnd + 4,
                                       nextBoundary - (headerEnd + 4));
        }
        position = nextBoundary + 2;
    }

    if (fileCount != 1 || parsedFile.filename.isEmpty()
        || parsedFile.data.isEmpty()) {
        setError(errorMessage,
                 QStringLiteral("Exactly one nonempty file field is required."));
        return false;
    }
    if (parsedFile.filename.size() > 255
        || parsedFile.filename.contains(QChar::Null)) {
        setError(errorMessage, QStringLiteral("Uploaded filename is invalid."));
        return false;
    }
    if (file) {
        *file = std::move(parsedFile);
    }
    return true;
}

bool MultipartParser::parseComment(const QHttpServerRequest &request,
                                   qsizetype maximumBodySize,
                                   int maximumFiles,
                                   QString *content,
                                   QList<MultipartFile> *files,
                                   QString *errorMessage)
{
    if (content) {
        content->clear();
    }
    if (files) {
        files->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    const QByteArray body = request.body();
    if (body.isEmpty() || body.size() > maximumBodySize) {
        setError(errorMessage,
                 body.isEmpty()
                     ? QStringLiteral("Multipart request body is empty.")
                     : QStringLiteral("Comment attachments exceed the request size limit."));
        return false;
    }
    const QByteArray boundary = boundaryFromContentType(
        request.value("Content-Type"));
    if (boundary.isEmpty() || boundary.size() > 200
        || boundary.contains('\r') || boundary.contains('\n')) {
        setError(errorMessage, QStringLiteral("Multipart boundary is invalid."));
        return false;
    }

    const QByteArray delimiter = QByteArrayLiteral("--") + boundary;
    qsizetype position = 0;
    bool foundContent = false;
    QList<MultipartFile> parsedFiles;
    QString parsedContent;
    while (true) {
        const qsizetype boundaryStart = body.indexOf(delimiter, position);
        if (boundaryStart < 0) {
            break;
        }
        qsizetype partStart = boundaryStart + delimiter.size();
        if (body.mid(partStart, 2) == QByteArrayLiteral("--")) {
            break;
        }
        if (body.mid(partStart, 2) != QByteArrayLiteral("\r\n")) {
            setError(errorMessage, QStringLiteral("Multipart body is malformed."));
            return false;
        }
        partStart += 2;
        const qsizetype headerEnd = body.indexOf(
            QByteArrayLiteral("\r\n\r\n"), partStart);
        const qsizetype nextBoundary = headerEnd < 0 ? -1 : body.indexOf(
            QByteArrayLiteral("\r\n") + delimiter, headerEnd + 4);
        if (headerEnd < 0 || nextBoundary < 0) {
            setError(errorMessage, QStringLiteral("Multipart body is incomplete."));
            return false;
        }

        QString disposition;
        QByteArray partContentType;
        const QList<QByteArray> headerLines = body.mid(
            partStart, headerEnd - partStart).split('\n');
        for (QByteArray line : headerLines) {
            line = line.trimmed();
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0) {
                continue;
            }
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name == QByteArrayLiteral("content-disposition")) {
                disposition = QString::fromUtf8(value);
            } else if (name == QByteArrayLiteral("content-type")) {
                partContentType = value.toLower();
            }
        }
        const QString fieldName = dispositionValue(
            disposition, QStringLiteral("name"));
        const QByteArray partData = body.mid(headerEnd + 4,
                                              nextBoundary - (headerEnd + 4));
        if (fieldName == QStringLiteral("content")) {
            if (foundContent) {
                setError(errorMessage,
                         QStringLiteral("Exactly one comment content field is required."));
                return false;
            }
            foundContent = true;
            parsedContent = QString::fromUtf8(partData);
        } else if (fieldName == QStringLiteral("files")
                   || fieldName == QStringLiteral("images")) {
            MultipartFile file;
            file.filename = QFileInfo(dispositionValue(
                disposition, QStringLiteral("filename"))).fileName();
            file.contentType = partContentType;
            file.data = partData;
            if (file.filename.isEmpty() || file.data.isEmpty()
                || file.filename.size() > 255
                || file.filename.contains(QChar::Null)) {
                setError(errorMessage, QStringLiteral("Comment attachment is invalid."));
                return false;
            }
            parsedFiles.append(std::move(file));
            if (parsedFiles.size() > maximumFiles) {
                setError(errorMessage,
                         QStringLiteral("A comment can contain at most 9 attachments."));
                return false;
            }
        } else if (!fieldName.isEmpty()) {
            setError(errorMessage, QStringLiteral("Multipart field is not supported."));
            return false;
        }
        position = nextBoundary + 2;
    }

    if (!foundContent || parsedFiles.isEmpty()) {
        setError(errorMessage,
                 QStringLiteral("Comment content and at least one attachment are required."));
        return false;
    }
    if (content) {
        *content = parsedContent;
    }
    if (files) {
        *files = std::move(parsedFiles);
    }
    return true;
}

bool MultipartParser::parseIssue(const QHttpServerRequest &request,
                                 qsizetype maximumBodySize,
                                 int maximumFiles,
                                 QJsonObject *values,
                                 QList<MultipartFile> *files,
                                 QList<qint64> *removeAttachmentIds,
                                 QString *errorMessage)
{
    if (values) {
        *values = {};
    }
    if (files) {
        files->clear();
    }
    if (removeAttachmentIds) {
        removeAttachmentIds->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    const QByteArray body = request.body();
    if (body.isEmpty() || body.size() > maximumBodySize) {
        setError(errorMessage,
                 body.isEmpty()
                     ? QStringLiteral("Multipart request body is empty.")
                     : QStringLiteral("Issue attachments exceed the request size limit."));
        return false;
    }
    const QByteArray boundary = boundaryFromContentType(request.value("Content-Type"));
    if (boundary.isEmpty() || boundary.size() > 200
        || boundary.contains('\r') || boundary.contains('\n')) {
        setError(errorMessage, QStringLiteral("Multipart boundary is invalid."));
        return false;
    }

    const QByteArray delimiter = QByteArrayLiteral("--") + boundary;
    qsizetype position = 0;
    bool foundIssue = false;
    QJsonObject parsedValues;
    QList<MultipartFile> parsedFiles;
    while (true) {
        const qsizetype boundaryStart = body.indexOf(delimiter, position);
        if (boundaryStart < 0) {
            break;
        }
        qsizetype partStart = boundaryStart + delimiter.size();
        if (body.mid(partStart, 2) == QByteArrayLiteral("--")) {
            break;
        }
        if (body.mid(partStart, 2) != QByteArrayLiteral("\r\n")) {
            setError(errorMessage, QStringLiteral("Multipart body is malformed."));
            return false;
        }
        partStart += 2;
        const qsizetype headerEnd = body.indexOf(QByteArrayLiteral("\r\n\r\n"), partStart);
        const qsizetype nextBoundary = headerEnd < 0 ? -1 : body.indexOf(
            QByteArrayLiteral("\r\n") + delimiter, headerEnd + 4);
        if (headerEnd < 0 || nextBoundary < 0) {
            setError(errorMessage, QStringLiteral("Multipart body is incomplete."));
            return false;
        }

        QString disposition;
        QByteArray partContentType;
        const QList<QByteArray> headerLines = body.mid(
            partStart, headerEnd - partStart).split('\n');
        for (QByteArray line : headerLines) {
            line = line.trimmed();
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0) {
                continue;
            }
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name == QByteArrayLiteral("content-disposition")) {
                disposition = QString::fromUtf8(value);
            } else if (name == QByteArrayLiteral("content-type")) {
                partContentType = value.toLower();
            }
        }

        const QString fieldName = dispositionValue(disposition, QStringLiteral("name"));
        const QByteArray partData = body.mid(headerEnd + 4, nextBoundary - (headerEnd + 4));
        if (fieldName == QStringLiteral("issue")) {
            if (foundIssue) {
                setError(errorMessage, QStringLiteral("Exactly one issue field is required."));
                return false;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(partData, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                setError(errorMessage, QStringLiteral("Issue field must contain a JSON object."));
                return false;
            }
            foundIssue = true;
            parsedValues = document.object();
        } else if (fieldName == QStringLiteral("files")) {
            MultipartFile file;
            file.filename = QFileInfo(dispositionValue(
                disposition, QStringLiteral("filename"))).fileName();
            file.contentType = partContentType;
            file.data = partData;
            if (file.filename.isEmpty() || file.data.isEmpty()
                || file.filename.size() > 255 || file.filename.contains(QChar::Null)) {
                setError(errorMessage, QStringLiteral("Issue attachment is invalid."));
                return false;
            }
            parsedFiles.append(std::move(file));
            if (parsedFiles.size() > maximumFiles) {
                setError(errorMessage, QStringLiteral("An issue can contain at most 9 attachments."));
                return false;
            }
        } else if (!fieldName.isEmpty()) {
            setError(errorMessage, QStringLiteral("Multipart field is not supported."));
            return false;
        }
        position = nextBoundary + 2;
    }

    if (!foundIssue) {
        setError(errorMessage, QStringLiteral("Exactly one issue field is required."));
        return false;
    }
    QList<qint64> parsedRemoveIds;
    if (parsedValues.contains(QStringLiteral("remove_attachment_ids"))) {
        const QJsonValue removeValue = parsedValues.take(QStringLiteral("remove_attachment_ids"));
        if (!removeValue.isArray()) {
            setError(errorMessage, QStringLiteral("Attachment removal ids must be an array."));
            return false;
        }
        QSet<qint64> knownIds;
        for (const QJsonValue &value : removeValue.toArray()) {
            const qint64 id = value.toInteger();
            if (!value.isDouble() || id <= 0 || double(id) != value.toDouble()
                || knownIds.contains(id)) {
                setError(errorMessage, QStringLiteral("Attachment removal ids are invalid."));
                return false;
            }
            knownIds.insert(id);
            parsedRemoveIds.append(id);
        }
    }
    if (parsedFiles.isEmpty() && parsedRemoveIds.isEmpty()) {
        setError(errorMessage, QStringLiteral("At least one attachment change is required."));
        return false;
    }
    if (values) {
        *values = parsedValues;
    }
    if (files) {
        *files = std::move(parsedFiles);
    }
    if (removeAttachmentIds) {
        *removeAttachmentIds = std::move(parsedRemoveIds);
    }
    return true;
}
