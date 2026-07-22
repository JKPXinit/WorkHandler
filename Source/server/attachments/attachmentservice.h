#ifndef ATTACHMENTSERVICE_H
#define ATTACHMENTSERVICE_H

#include "attachments/attachmentdao.h"
#include "attachments/imageprocessor.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

struct UserRecord;

enum class AttachmentServiceError {
    None,
    InvalidInput,
    NotFound,
    Forbidden,
    Conflict,
    Database,
    Storage
};

struct AttachmentServiceResult
{
    AttachmentServiceError error {AttachmentServiceError::None};
    QString code;
    QString message;
    QList<AttachmentRecord> attachments;
    QByteArray fileData;

    bool ok() const;
};

class AttachmentService
{
public:
    using OptionsProvider = std::function<ImageProcessingOptions()>;

    AttachmentService(AttachmentDao &dao,
                      ImageProcessor &imageProcessor,
                      OptionsProvider optionsProvider);

    AttachmentServiceResult read(qint64 id,
                                 bool thumbnail) const;
    AttachmentServiceResult processCommentImages(
        qint64 issueId,
        qint64 uploaderId,
        const QList<MultipartFile> &files) const;
    void removeFiles(const QList<AttachmentRecord> &attachments) const;

    AttachmentServiceResult stageIssueRemoval(
        qint64 issueId, StagedFileRemoval *staged) const;
    AttachmentServiceResult stageBlockRemoval(
        qint64 blockId, StagedFileRemoval *staged) const;
    void rollbackRemoval(StagedFileRemoval *staged) const;
    void commitRemoval(StagedFileRemoval *staged) const;

private:
    AttachmentServiceResult stageAttachments(
        const QList<AttachmentRecord> &attachments,
        StagedFileRemoval *staged) const;
    static AttachmentServiceResult daoFailure(const AttachmentDaoResult &result);

    AttachmentDao &m_dao;
    ImageProcessor &m_imageProcessor;
    OptionsProvider m_optionsProvider;
};

#endif // ATTACHMENTSERVICE_H
