#include "attachments/imageprocessor.h"

#include <QBuffer>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QUuid>

#include <cmath>
#include <utility>

namespace {
constexpr qint64 MaximumDecodedPixels = 40000000;
constexpr int MaximumWebPDimension = 16383;

void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

QByteArray detectedFormat(const QByteArray &data)
{
    static const QByteArray pngSignature = QByteArray::fromHex(
        "89504e470d0a1a0a");
    if (data.startsWith(pngSignature)) {
        return QByteArrayLiteral("png");
    }
    if (data.size() >= 3
        && uchar(data.at(0)) == 0xff
        && uchar(data.at(1)) == 0xd8
        && uchar(data.at(2)) == 0xff) {
        return QByteArrayLiteral("jpeg");
    }
    if (data.size() >= 12
        && data.first(4) == QByteArrayLiteral("RIFF")
        && data.sliced(8, 4) == QByteArrayLiteral("WEBP")) {
        return QByteArrayLiteral("webp");
    }
    return {};
}

QSize sizeWithinDecodeLimits(const QSize &sourceSize, int maximumWidth)
{
    if (!sourceSize.isValid()) {
        return {};
    }

    const qint64 sourcePixels = qint64(sourceSize.width())
        * qint64(sourceSize.height());
    const int allowedWidth = qBound(1, maximumWidth,
                                    MaximumWebPDimension);
    double scale = 1.0;
    if (sourceSize.width() > allowedWidth) {
        scale = double(allowedWidth) / double(sourceSize.width());
    }
    if (double(sourceSize.height()) * scale
        > double(MaximumWebPDimension)) {
        scale = qMin(scale,
                     double(MaximumWebPDimension)
                         / double(sourceSize.height()));
    }
    if (double(sourcePixels) * scale * scale
        > double(MaximumDecodedPixels)) {
        scale = qMin(scale,
                     std::sqrt(double(MaximumDecodedPixels)
                               / double(sourcePixels)));
    }

    return QSize(qMax(1, int(double(sourceSize.width()) * scale)),
                 qMax(1, int(double(sourceSize.height()) * scale)));
}
}

ImageProcessor::ImageProcessor(const QString &storageRoot)
    : m_storageRoot(QDir::cleanPath(storageRoot))
{
}

bool ImageProcessor::process(const MultipartFile &file,
                             const ImageProcessingOptions &options,
                             ProcessedImage *processed,
                             QString *errorMessage,
                             bool *storageError) const
{
    if (processed) {
        *processed = {};
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    if (storageError) {
        *storageError = false;
    }
    const QByteArray format = detectedFormat(file.data);
    if (format.isEmpty()) {
        setError(errorMessage,
                 QStringLiteral("Only PNG, JPEG, and WebP images are supported."));
        return false;
    }

    QBuffer inputBuffer;
    inputBuffer.setData(file.data);
    if (!inputBuffer.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Uploaded image could not be read."));
        return false;
    }
    QImageReader reader(&inputBuffer, format);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    const QSize decodeSize = sizeWithinDecodeLimits(
        sourceSize, options.maximumWidth);
    if (decodeSize.isValid() && decodeSize != sourceSize) {
        reader.setScaledSize(decodeSize);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        const QString detail = reader.errorString();
        setError(errorMessage, detail.isEmpty()
                     ? QStringLiteral("Uploaded image could not be decoded.")
                     : QStringLiteral("Uploaded image could not be decoded: %1")
                           .arg(detail));
        return false;
    }
    const QSize outputSize = sizeWithinDecodeLimits(
        image.size(), options.maximumWidth);
    if (!outputSize.isValid()) {
        setError(errorMessage,
                 QStringLiteral("Uploaded image dimensions are invalid."));
        return false;
    }
    if (outputSize != image.size()) {
        image = image.scaled(outputSize, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }
    const QSize thumbnailSize = sizeWithinDecodeLimits(
        image.size(), options.thumbnailWidth);
    const QImage thumbnail = thumbnailSize != image.size()
        ? image.scaled(thumbnailSize, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation)
        : image;

    const QString month = QDate::currentDate().toString(QStringLiteral("yyyy-MM"));
    const QString identifier = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ProcessedImage output;
    output.storagePath = QStringLiteral("%1/%2.webp").arg(month, identifier);
    output.thumbnailPath = QStringLiteral("%1/%2_thumb.webp").arg(month, identifier);
    if (options.keepOriginal) {
        output.originalPath = QStringLiteral("%1/%2_original.bin").arg(month, identifier);
    }

    if (!writeWebP(image, output.storagePath, errorMessage)
        || !writeWebP(thumbnail, output.thumbnailPath, errorMessage)
        || (!output.originalPath.isEmpty()
            && !writeFile(file.data, output.originalPath, errorMessage))) {
        if (storageError) {
            *storageError = true;
        }
        remove(output.storagePath);
        remove(output.thumbnailPath);
        remove(output.originalPath);
        return false;
    }
    output.fileSize = QFileInfo(absolutePath(output.storagePath)).size();
    if (processed) {
        *processed = output;
    }
    return true;
}

QString ImageProcessor::absolutePath(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return {};
    }
    const QString normalized = QDir::cleanPath(relativePath);
    if (QDir::isAbsolutePath(normalized)
        || normalized == QStringLiteral("..")
        || normalized.startsWith(QStringLiteral("../"))) {
        return {};
    }
    const QString absolute = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(m_storageRoot).absoluteFilePath(normalized)));
    const QString rootPrefix = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(m_storageRoot).absolutePath())) + QLatin1Char('/');
    return absolute.startsWith(rootPrefix, Qt::CaseInsensitive)
        ? absolute
        : QString();
}

bool ImageProcessor::remove(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return true;
    }
    const QString path = absolutePath(relativePath);
    return !path.isEmpty() && (!QFile::exists(path) || QFile::remove(path));
}

bool ImageProcessor::stageRemoval(const QStringList &relativePaths,
                                  StagedFileRemoval *staged,
                                  QString *errorMessage) const
{
    if (staged) {
        staged->files.clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    StagedFileRemoval pending;
    for (const QString &relativePath : relativePaths) {
        if (relativePath.isEmpty()) {
            continue;
        }
        const QString source = absolutePath(relativePath);
        if (source.isEmpty()) {
            rollbackRemoval(&pending);
            setError(errorMessage, QStringLiteral("Attachment file path is invalid."));
            return false;
        }
        if (!QFile::exists(source)) {
            continue;
        }
        const QString destination = source + QStringLiteral(".deleting-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QFile::rename(source, destination)) {
            rollbackRemoval(&pending);
            setError(errorMessage,
                     QStringLiteral("Attachment file could not be prepared for deletion."));
            return false;
        }
        pending.files.append({source, destination});
    }
    if (staged) {
        *staged = pending;
    }
    return true;
}

void ImageProcessor::rollbackRemoval(StagedFileRemoval *staged) const
{
    if (!staged) {
        return;
    }
    for (auto iterator = staged->files.crbegin();
         iterator != staged->files.crend(); ++iterator) {
        if (QFile::exists(iterator->stagedPath)) {
            QFile::rename(iterator->stagedPath, iterator->originalPath);
        }
    }
    staged->files.clear();
}

void ImageProcessor::commitRemoval(StagedFileRemoval *staged) const
{
    if (!staged) {
        return;
    }
    for (const StagedFile &file : std::as_const(staged->files)) {
        QFile::remove(file.stagedPath);
    }
    staged->files.clear();
}

bool ImageProcessor::writeWebP(const QImage &image,
                               const QString &relativePath,
                               QString *errorMessage) const
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("Image output buffer could not be opened."));
        return false;
    }
    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(82);
    if (!writer.write(image)) {
        setError(errorMessage,
                 writer.errorString().isEmpty()
                     ? QStringLiteral("WebP image encoding is unavailable.")
                     : writer.errorString());
        return false;
    }
    return writeFile(encoded, relativePath, errorMessage);
}

bool ImageProcessor::writeFile(const QByteArray &data,
                               const QString &relativePath,
                               QString *errorMessage) const
{
    const QString path = absolutePath(relativePath);
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        setError(errorMessage, QStringLiteral("Image storage directory could not be created."));
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(data) != data.size() || !output.commit()) {
        setError(errorMessage,
                 output.errorString().isEmpty()
                     ? QStringLiteral("Image file could not be stored.")
                     : output.errorString());
        return false;
    }
    return true;
}
