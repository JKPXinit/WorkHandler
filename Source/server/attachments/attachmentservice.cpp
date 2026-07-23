#include "attachments/attachmentservice.h"

#include "databasemanager.h"

#include <QFile>

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
    if (files.isEmpty() || files.size() > 9) {
        return invalid(QStringLiteral("invalid_comment_image_count"),
                       QStringLiteral("A comment must contain 1 to 9 images."));
    }
    qsizetype totalSize = 0;
    for (const MultipartFile &file : files) {
        if (file.data.isEmpty() || file.data.size() > MaximumFileSize) {
            return invalid(QStringLiteral("invalid_attachment_size"),
                           QStringLiteral("Each image must not exceed 10 MiB."));
        }
        totalSize += file.data.size();
        if (totalSize > MaximumCommentFileSize) {
            return invalid(QStringLiteral("invalid_comment_image_size"),
                           QStringLiteral("Comment images must not exceed 30 MiB in total."));
        }
    }

    AttachmentServiceResult result;
    const ImageProcessingOptions options = m_optionsProvider
        ? m_optionsProvider() : ImageProcessingOptions();
    for (const MultipartFile &file : files) {
        ProcessedImage image;
        QString imageError;
        bool storageError = false;
        if (!m_imageProcessor.process(
                file, options, &image, &imageError, &storageError)) {
            removeFiles(result.attachments);
            m_imageProcessor.remove(image.storagePath);
            m_imageProcessor.remove(image.thumbnailPath);
            m_imageProcessor.remove(image.originalPath);
            return storageError
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
        attachment.storagePath = image.storagePath;
        attachment.thumbnailPath = image.thumbnailPath;
        attachment.originalPath = image.originalPath;
        attachment.fileSize = image.fileSize;
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
                        QStringLiteral("Attachment image file was not found."));
    }

    AttachmentServiceResult result;
    result.fileData = file.readAll();
    if (result.fileData.isEmpty()) {
        return {AttachmentServiceError::Storage,
                QStringLiteral("attachment_read_failed"),
                QStringLiteral("Attachment image file could not be read.")};
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
