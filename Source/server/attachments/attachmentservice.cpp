#include "attachments/attachmentservice.h"

#include "databasemanager.h"

#include <QFile>
#include <QFileInfo>

#include <utility>

namespace {
constexpr qsizetype MaximumFileSize = 10 * 1024 * 1024;
constexpr qsizetype MaximumCommentFileSize = 30 * 1024 * 1024;

AttachmentServiceResult invalid(const QString &code, const QString &message)
{
    return {AttachmentServiceError::InvalidInput, code, message};
}

AttachmentServiceResult notFound(const QString &code, const QString &message)
{
    return {AttachmentServiceError::NotFound, code, message};
}

QString storageMessage(const QString &detail)
{
    return detail.isEmpty() ? QStringLiteral("Attachment storage operation failed.")
                            : detail;
}

bool isSupportedFile(const MultipartFile &file)
{
    static const QSet<QString> extensions {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("txt"),
        QStringLiteral("log"),
        QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("csv"),
        QStringLiteral("md"), QStringLiteral("pdf"), QStringLiteral("zip"),
        QStringLiteral("bin"),
        QStringLiteral("doc"), QStringLiteral("docx"),
        QStringLiteral("docm"), QStringLiteral("dot"),
        QStringLiteral("dotx"), QStringLiteral("dotm"),
        QStringLiteral("xls"), QStringLiteral("xlsx"),
        QStringLiteral("xlsm"), QStringLiteral("xlt"),
        QStringLiteral("xltx"), QStringLiteral("xltm"),
        QStringLiteral("ppt"), QStringLiteral("pptx"),
        QStringLiteral("pptm"), QStringLiteral("pot"),
        QStringLiteral("potx"), QStringLiteral("potm")
    };
    return extensions.contains(QFileInfo(file.filename).suffix().toLower());
}

bool isImageFile(const MultipartFile &file)
{
    const QString suffix = QFileInfo(file.filename).suffix().toLower();
    return suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg") || suffix == QStringLiteral("webp")
        || suffix == QStringLiteral("bmp") || suffix == QStringLiteral("gif")
        || suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff");
}

QString contentTypeFor(const MultipartFile &file)
{
    const QString suffix = QFileInfo(file.filename).suffix().toLower();
    if (suffix == QStringLiteral("json")) {
        return QStringLiteral("application/json");
    }
    if (suffix == QStringLiteral("xml")) {
        return QStringLiteral("application/xml");
    }
    if (suffix == QStringLiteral("csv")) {
        return QStringLiteral("text/csv");
    }
    if (suffix == QStringLiteral("md")) {
        return QStringLiteral("text/markdown");
    }
    if (suffix == QStringLiteral("txt") || suffix == QStringLiteral("log")) {
        return QStringLiteral("text/plain");
    }
    if (suffix == QStringLiteral("pdf")) {
        return QStringLiteral("application/pdf");
    }
    if (suffix == QStringLiteral("zip")) {
        return QStringLiteral("application/zip");
    }
    if (suffix == QStringLiteral("bin")) {
        return QStringLiteral("application/octet-stream");
    }
    if (suffix == QStringLiteral("doc") || suffix == QStringLiteral("dot")) {
        return QStringLiteral("application/msword");
    }
    if (suffix == QStringLiteral("docx")) {
        return QStringLiteral(
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    }
    if (suffix == QStringLiteral("docm")) {
        return QStringLiteral("application/vnd.ms-word.document.macroEnabled.12");
    }
    if (suffix == QStringLiteral("dotx")) {
        return QStringLiteral(
            "application/vnd.openxmlformats-officedocument.wordprocessingml.template");
    }
    if (suffix == QStringLiteral("dotm")) {
        return QStringLiteral("application/vnd.ms-word.template.macroEnabled.12");
    }
    if (suffix == QStringLiteral("xls") || suffix == QStringLiteral("xlt")) {
        return QStringLiteral("application/vnd.ms-excel");
    }
    if (suffix == QStringLiteral("xlsx")) {
        return QStringLiteral(
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    }
    if (suffix == QStringLiteral("xlsm")) {
        return QStringLiteral("application/vnd.ms-excel.sheet.macroEnabled.12");
    }
    if (suffix == QStringLiteral("xltx")) {
        return QStringLiteral(
            "application/vnd.openxmlformats-officedocument.spreadsheetml.template");
    }
    if (suffix == QStringLiteral("xltm")) {
        return QStringLiteral("application/vnd.ms-excel.template.macroEnabled.12");
    }
    if (suffix == QStringLiteral("ppt") || suffix == QStringLiteral("pot")) {
        return QStringLiteral("application/vnd.ms-powerpoint");
    }
    if (suffix == QStringLiteral("pptx")) {
        return QStringLiteral(
            "application/vnd.openxmlformats-officedocument.presentationml.presentation");
    }
    if (suffix == QStringLiteral("pptm")) {
        return QStringLiteral("application/vnd.ms-powerpoint.presentation.macroEnabled.12");
    }
    if (suffix == QStringLiteral("potx")) {
        return QStringLiteral(
            "application/vnd.openxmlformats-officedocument.presentationml.template");
    }
    if (suffix == QStringLiteral("potm")) {
        return QStringLiteral("application/vnd.ms-powerpoint.template.macroEnabled.12");
    }
    return QString::fromLatin1(file.contentType).section(QLatin1Char(';'), 0, 0).trimmed();
}
}

bool AttachmentServiceResult::ok() const
{
    return error == AttachmentServiceError::None;
}

AttachmentService::AttachmentService(AttachmentDao &dao,
                                     ImageProcessor &imageProcessor,
                                     OptionsProvider optionsProvider)
    : m_dao(dao)
    , m_imageProcessor(imageProcessor)
    , m_optionsProvider(std::move(optionsProvider))
{
}

AttachmentServiceResult AttachmentService::processCommentImages(
    qint64 issueId,
    qint64 uploaderId,
    const QList<MultipartFile> &files) const
{
    return processCommentAttachments(issueId, uploaderId, files);
}

AttachmentServiceResult AttachmentService::processCommentAttachments(
    qint64 issueId,
    qint64 uploaderId,
    const QList<MultipartFile> &files) const
{
    if (files.isEmpty() || files.size() > 9) {
        return invalid(QStringLiteral("invalid_comment_attachment_count"),
                       QStringLiteral("A comment must contain 1 to 9 attachments."));
    }
    qsizetype totalSize = 0;
    for (const MultipartFile &file : files) {
        if (file.data.isEmpty() || file.data.size() > MaximumFileSize) {
            return invalid(QStringLiteral("invalid_attachment_size"),
                           QStringLiteral("Each attachment must not exceed 10 MiB."));
        }
        if (!isSupportedFile(file)) {
            return invalid(QStringLiteral("invalid_attachment_type"),
                           QStringLiteral("This attachment type is not supported."));
        }
        totalSize += file.data.size();
        if (totalSize > MaximumCommentFileSize) {
            return invalid(QStringLiteral("invalid_comment_attachment_size"),
                           QStringLiteral("Comment attachments must not exceed 30 MiB in total."));
        }
    }

    AttachmentServiceResult result;
    const ImageProcessingOptions options = m_optionsProvider
        ? m_optionsProvider() : ImageProcessingOptions();
    for (const MultipartFile &file : files) {
        const bool imageFile = isImageFile(file);
        ProcessedImage image;
        ProcessedFile storedFile;
        QString imageError;
        bool storageError = false;
        const bool processed = imageFile
            ? m_imageProcessor.process(file, options, &image, &imageError, &storageError)
            : m_imageProcessor.storeFile(file, &storedFile, &imageError);
        if (!processed) {
            removeFiles(result.attachments);
            m_imageProcessor.remove(image.storagePath);
            m_imageProcessor.remove(image.thumbnailPath);
            m_imageProcessor.remove(image.originalPath);
            return (storageError || !imageFile)
                ? AttachmentServiceResult{
                      AttachmentServiceError::Storage,
                      QStringLiteral("attachment_storage_error"),
                      storageMessage(imageError)}
                : invalid(QStringLiteral("invalid_attachment_image"),
                          storageMessage(imageError));
        }
        AttachmentRecord attachment;
        attachment.issueId = issueId;
        attachment.uploaderId = uploaderId;
        attachment.filename = file.filename;
        attachment.contentType = imageFile
            ? QStringLiteral("image/webp") : contentTypeFor(file);
        attachment.image = imageFile;
        attachment.storagePath = imageFile ? image.storagePath : storedFile.storagePath;
        attachment.thumbnailPath = image.thumbnailPath;
        attachment.originalPath = image.originalPath;
        attachment.fileSize = imageFile ? image.fileSize : storedFile.fileSize;
        result.attachments.append(attachment);
    }
    return result;
}

void AttachmentService::removeFiles(
    const QList<AttachmentRecord> &attachments) const
{
    for (const AttachmentRecord &attachment : attachments) {
        for (const QString &path : attachment.filePaths()) {
            m_imageProcessor.remove(path);
        }
    }
}

AttachmentServiceResult AttachmentService::read(qint64 id,
                                                 bool thumbnail) const
{
    std::optional<AttachmentRecord> attachment;
    const AttachmentDaoResult daoResult = m_dao.attachmentById(id, &attachment);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    if (!attachment) {
        return notFound(QStringLiteral("attachment_not_found"),
                        QStringLiteral("Attachment was not found."));
    }
    const QString relativePath = thumbnail
        ? attachment->thumbnailPath : attachment->storagePath;
    const QString absolutePath = m_imageProcessor.absolutePath(relativePath);
    QFile file(absolutePath);
    if (absolutePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return notFound(QStringLiteral("attachment_file_not_found"),
                        QStringLiteral("Attachment file was not found."));
    }

    AttachmentServiceResult result;
    result.attachment = *attachment;
    result.fileData = file.readAll();
    if (result.fileData.isEmpty()) {
        return {AttachmentServiceError::Storage,
                QStringLiteral("attachment_read_failed"),
                QStringLiteral("Attachment file could not be read.")};
    }
    return result;
}

AttachmentServiceResult AttachmentService::stageIssueRemoval(
    qint64 issueId, StagedFileRemoval *staged) const
{
    QList<AttachmentRecord> attachments;
    const AttachmentDaoResult result = m_dao.attachments(issueId, &attachments);
    return result.ok() ? stageAttachments(attachments, staged)
                       : daoFailure(result);
}

AttachmentServiceResult AttachmentService::stageBlockRemoval(
    qint64 blockId, StagedFileRemoval *staged) const
{
    QList<AttachmentRecord> attachments;
    const AttachmentDaoResult result = m_dao.attachmentsForBlock(
        blockId, &attachments);
    return result.ok() ? stageAttachments(attachments, staged)
                       : daoFailure(result);
}

void AttachmentService::rollbackRemoval(StagedFileRemoval *staged) const
{
    m_imageProcessor.rollbackRemoval(staged);
}

void AttachmentService::commitRemoval(StagedFileRemoval *staged) const
{
    m_imageProcessor.commitRemoval(staged);
}

AttachmentServiceResult AttachmentService::stageAttachments(
    const QList<AttachmentRecord> &attachments,
    StagedFileRemoval *staged) const
{
    QStringList paths;
    for (const AttachmentRecord &attachment : attachments) {
        paths.append(attachment.filePaths());
    }
    QString storageError;
    if (!m_imageProcessor.stageRemoval(paths, staged, &storageError)) {
        return {AttachmentServiceError::Storage,
                QStringLiteral("attachment_storage_error"),
                storageMessage(storageError)};
    }
    return {};
}

AttachmentServiceResult AttachmentService::daoFailure(
    const AttachmentDaoResult &result)
{
    if (result.error == AttachmentDaoError::Conflict) {
        return {AttachmentServiceError::Conflict,
                QStringLiteral("attachment_conflict"),
                result.message};
    }
    return {AttachmentServiceError::Database,
            QStringLiteral("database_error"),
            result.message.isEmpty()
                ? QStringLiteral("Database operation failed.")
                : result.message};
}
