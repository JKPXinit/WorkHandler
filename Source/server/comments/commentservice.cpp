#include "comments/commentservice.h"

#include "attachments/attachmentservice.h"
#include "databasemanager.h"
#include "notifications/notificationmanager.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace {
constexpr qsizetype MaximumAttachmentCount = 9;
constexpr qint64 MaximumAttachmentTotalSize = 30 * 1024 * 1024;

CommentServiceResult invalid(const QString &code, const QString &message)
{
    return {CommentServiceError::InvalidInput, code, message};
}

CommentServiceResult notFound()
{
    return {CommentServiceError::NotFound,
            QStringLiteral("issue_not_found"),
            QStringLiteral("Issue was not found.")};
}

CommentServiceResult forbidden()
{
    return {CommentServiceError::Forbidden,
            QStringLiteral("comment_write_forbidden"),
            QStringLiteral("Comment creation requires write permission.")};
}

CommentServiceResult forbidden(const QString &code, const QString &message)
{
    return {CommentServiceError::Forbidden, code, message};
}

CommentServiceResult commentNotFound()
{
    return {CommentServiceError::NotFound,
            QStringLiteral("comment_not_found"),
            QStringLiteral("Comment was not found.")};
}

CommentServiceResult attachmentFailure(const AttachmentServiceResult &result)
{
    return {result.error == AttachmentServiceError::InvalidInput
                ? CommentServiceError::InvalidInput
                : CommentServiceError::Database,
            result.code, result.message};
}

bool canModify(const UserRecord &user, const CommentRecord &comment)
{
    return user.role == QStringLiteral("admin")
        || (user.role == QStringLiteral("user") && user.id == comment.userId);
}

bool readRemoveIds(const QJsonObject &values,
                   QList<qint64> *removeIds,
                   QString *errorMessage)
{
    if (removeIds) {
        removeIds->clear();
    }
    if (!values.contains(QStringLiteral("remove_attachment_ids"))) {
        return true;
    }
    const QJsonValue value = values.value(QStringLiteral("remove_attachment_ids"));
    if (!value.isArray()) {
        if (errorMessage) *errorMessage = QStringLiteral("Attachment removal ids must be an array.");
        return false;
    }
    QSet<qint64> knownIds;
    for (const QJsonValue &idValue : value.toArray()) {
        const qint64 id = idValue.toInteger();
        if (!idValue.isDouble() || id <= 0 || double(id) != idValue.toDouble()
            || knownIds.contains(id)) {
            if (errorMessage) *errorMessage = QStringLiteral("Attachment removal ids are invalid.");
            return false;
        }
        knownIds.insert(id);
        if (removeIds) removeIds->append(id);
    }
    return true;
}
}

bool CommentServiceResult::ok() const
{
    return error == CommentServiceError::None;
}

CommentService::CommentService(CommentDao &dao,
                               AttachmentService &attachmentService,
                               NotificationManager &notificationManager)
    : m_dao(dao)
    , m_attachmentService(attachmentService)
    , m_notificationManager(notificationManager)
{
}

CommentServiceResult CommentService::list(qint64 issueId) const
{
    const CommentServiceResult issueResult = validateIssue(issueId);
    if (!issueResult.ok()) {
        return issueResult;
    }

    CommentServiceResult result;
    const CommentDaoResult daoResult = m_dao.comments(issueId, &result.comments);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

CommentServiceResult CommentService::createWithAttachments(
    qint64 issueId,
    const QString &content,
    const QList<MultipartFile> &files,
    const UserRecord &currentUser)
{
    if (currentUser.role != QStringLiteral("admin")
        && currentUser.role != QStringLiteral("user")) {
        return forbidden();
    }
    if (content.size() > 4000 || files.isEmpty() || files.size() > 9
        || content.contains(QRegularExpression(
            QStringLiteral("attachment:[0-9]+")))) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment attachment content is invalid."));
    }

    const QRegularExpression marker(
        QStringLiteral("(?:!?\\[[^\\]\\r\\n]*\\])?\\(upload:([0-9]+)\\)"));
    QSet<int> indexes;
    QRegularExpressionMatchIterator matches = marker.globalMatch(content);
    while (matches.hasNext()) {
        const int index = matches.next().captured(1).toInt();
        if (index < 0 || index >= files.size() || indexes.contains(index)) {
            return invalid(QStringLiteral("invalid_comment_attachment_reference"),
                           QStringLiteral("Each comment attachment must be referenced exactly once."));
        }
        indexes.insert(index);
    }
    QString withoutMarkers = content;
    withoutMarkers.remove(marker);
    if (indexes.size() != files.size()
        || withoutMarkers.contains(QStringLiteral("upload:"))) {
        return invalid(QStringLiteral("invalid_comment_attachment_reference"),
                       QStringLiteral("Each comment attachment must be referenced exactly once."));
    }

    const CommentServiceResult issueResult = validateIssue(issueId);
    if (!issueResult.ok()) {
        return issueResult;
    }
    const AttachmentServiceResult imageResult =
        m_attachmentService.processCommentAttachments(
            issueId, currentUser.id, files);
    if (!imageResult.ok()) {
        return {imageResult.error == AttachmentServiceError::InvalidInput
                    ? CommentServiceError::InvalidInput
                    : CommentServiceError::Database,
                imageResult.code, imageResult.message};
    }

    CommentServiceResult result;
    const CommentDaoResult daoResult = m_dao.createWithAttachments(
        issueId, currentUser.id, content, imageResult.attachments,
        &result.comment);
    if (!daoResult.ok()) {
        m_attachmentService.removeFiles(imageResult.attachments);
        return daoFailure(daoResult);
    }
    m_notificationManager.commentAdded(issueId, result.comment, currentUser);
    return result;
}

CommentServiceResult CommentService::create(
    qint64 issueId,
    const QJsonObject &values,
    const UserRecord &currentUser)
{
    if (currentUser.role != QStringLiteral("admin")
        && currentUser.role != QStringLiteral("user")) {
        return forbidden();
    }
    if (values.size() != 1 || !values.contains(QStringLiteral("content"))
        || !values.value(QStringLiteral("content")).isString()) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment content is required."));
    }
    const QString content = values.value(QStringLiteral("content"))
                                .toString().trimmed();
    if (content.isEmpty() || content.size() > 4000) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment content must contain 1 to 4000 characters."));
    }

    const CommentServiceResult issueResult = validateIssue(issueId);
    if (!issueResult.ok()) {
        return issueResult;
    }

    CommentServiceResult result;
    const CommentDaoResult daoResult = m_dao.create(
        issueId, currentUser.id, content, &result.comment);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    m_notificationManager.commentAdded(issueId, result.comment, currentUser);
    return result;
}

CommentServiceResult CommentService::update(
    qint64 id,
    const QJsonObject &values,
    const UserRecord &currentUser)
{
    QList<qint64> removeAttachmentIds;
    QString errorMessage;
    if (!readRemoveIds(values, &removeAttachmentIds, &errorMessage)) {
        return invalid(QStringLiteral("invalid_attachment_removal"), errorMessage);
    }
    QJsonObject editableValues = values;
    editableValues.remove(QStringLiteral("remove_attachment_ids"));
    return updateWithAttachments(
        id, editableValues, {}, removeAttachmentIds, currentUser);
}

CommentServiceResult CommentService::updateWithAttachments(
    qint64 id,
    const QJsonObject &values,
    const QList<MultipartFile> &files,
    const QList<qint64> &removeAttachmentIds,
    const UserRecord &currentUser)
{
    CommentRecord existing;
    bool found = false;
    const CommentDaoResult readResult = m_dao.commentById(id, &existing, &found);
    if (!readResult.ok()) {
        return daoFailure(readResult);
    }
    if (!found) {
        return commentNotFound();
    }
    if (!canModify(currentUser, existing)) {
        return forbidden(QStringLiteral("comment_edit_forbidden"),
                         QStringLiteral("Only the author or an administrator can edit this comment."));
    }
    if (!existing.deletedAt.isEmpty()) {
        return {CommentServiceError::Conflict,
                QStringLiteral("comment_deleted"),
                QStringLiteral("Deleted comments cannot be edited.")};
    }
    if (values.size() != 1 || !values.value(QStringLiteral("content")).isString()) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment content is required."));
    }
    const QString content = values.value(QStringLiteral("content")).toString();
    if (content.trimmed().isEmpty() || content.size() > 4000) {
        return invalid(QStringLiteral("invalid_comment_content"),
                       QStringLiteral("Comment content must contain 1 to 4000 characters."));
    }

    QSet<qint64> removeIds;
    QList<AttachmentRecord> removedAttachments;
    for (qint64 removeId : removeAttachmentIds) {
        if (removeId <= 0 || removeIds.contains(removeId)) {
            return invalid(QStringLiteral("invalid_attachment_removal"),
                           QStringLiteral("Attachment removal ids are invalid."));
        }
        bool attachmentFound = false;
        for (const AttachmentRecord &attachment : existing.attachments) {
            if (attachment.id == removeId) {
                removedAttachments.append(attachment);
                attachmentFound = true;
                break;
            }
        }
        if (!attachmentFound) {
            return invalid(QStringLiteral("invalid_attachment_removal"),
                           QStringLiteral("Attachment does not belong to this comment."));
        }
        removeIds.insert(removeId);
    }

    QSet<qint64> retainedIds;
    qint64 retainedSize = 0;
    for (const AttachmentRecord &attachment : existing.attachments) {
        if (!removeIds.contains(attachment.id)) {
            retainedIds.insert(attachment.id);
            retainedSize += attachment.fileSize;
        }
    }
    const QRegularExpression attachmentMarker(
        QStringLiteral("!?\\[[^\\]\\r\\n]*\\]\\(attachment:([0-9]+)\\)"));
    QSet<qint64> referencedIds;
    QRegularExpressionMatchIterator attachmentMatches = attachmentMarker.globalMatch(content);
    while (attachmentMatches.hasNext()) {
        const qint64 attachmentId = attachmentMatches.next().captured(1).toLongLong();
        if (!retainedIds.contains(attachmentId) || referencedIds.contains(attachmentId)) {
            return invalid(QStringLiteral("invalid_comment_attachment_reference"),
                           QStringLiteral("Each retained attachment must be referenced exactly once."));
        }
        referencedIds.insert(attachmentId);
    }
    QString contentWithoutAttachmentMarkers = content;
    contentWithoutAttachmentMarkers.remove(attachmentMarker);
    if (referencedIds != retainedIds
        || contentWithoutAttachmentMarkers.contains(QStringLiteral("attachment:"))) {
        return invalid(QStringLiteral("invalid_comment_attachment_reference"),
                       QStringLiteral("Each retained attachment must be referenced exactly once."));
    }

    const QRegularExpression uploadMarker(
        QStringLiteral("!?\\[[^\\]\\r\\n]*\\]\\(upload:([0-9]+)\\)"));
    QSet<int> uploadIndexes;
    QRegularExpressionMatchIterator uploadMatches = uploadMarker.globalMatch(content);
    while (uploadMatches.hasNext()) {
        const int index = uploadMatches.next().captured(1).toInt();
        if (index < 0 || index >= files.size() || uploadIndexes.contains(index)) {
            return invalid(QStringLiteral("invalid_comment_attachment_reference"),
                           QStringLiteral("Each new attachment must be referenced exactly once."));
        }
        uploadIndexes.insert(index);
    }
    QString contentWithoutUploadMarkers = content;
    contentWithoutUploadMarkers.remove(uploadMarker);
    if (uploadIndexes.size() != files.size()
        || contentWithoutUploadMarkers.contains(QStringLiteral("upload:"))) {
        return invalid(QStringLiteral("invalid_comment_attachment_reference"),
                       QStringLiteral("Each new attachment must be referenced exactly once."));
    }
    if (retainedIds.size() + files.size() > MaximumAttachmentCount) {
        return invalid(QStringLiteral("invalid_attachment_count"),
                       QStringLiteral("A comment can contain at most 9 attachments."));
    }
    qint64 newSize = 0;
    for (const MultipartFile &file : files) newSize += file.data.size();
    if (retainedSize + newSize > MaximumAttachmentTotalSize) {
        return invalid(QStringLiteral("invalid_attachment_total_size"),
                       QStringLiteral("Comment attachments must not exceed 30 MiB in total."));
    }

    AttachmentServiceResult newAttachments;
    if (!files.isEmpty()) {
        newAttachments = m_attachmentService.processCommentAttachments(
            existing.issueId, currentUser.id, files);
        if (!newAttachments.ok()) {
            return attachmentFailure(newAttachments);
        }
    }
    StagedFileRemoval stagedRemoval;
    if (!removedAttachments.isEmpty()) {
        const AttachmentServiceResult stageResult =
            m_attachmentService.stageRemoval(removedAttachments, &stagedRemoval);
        if (!stageResult.ok()) {
            m_attachmentService.removeFiles(newAttachments.attachments);
            return attachmentFailure(stageResult);
        }
    }
    CommentServiceResult result;
    const CommentDaoResult updateResult = m_dao.updateWithAttachments(
        id, content, newAttachments.attachments, removeAttachmentIds, &result.comment);
    if (!updateResult.ok()) {
        m_attachmentService.removeFiles(newAttachments.attachments);
        m_attachmentService.rollbackRemoval(&stagedRemoval);
        return daoFailure(updateResult);
    }
    m_attachmentService.commitRemoval(&stagedRemoval);
    return result;
}

CommentServiceResult CommentService::remove(
    qint64 id, const UserRecord &currentUser)
{
    CommentRecord existing;
    bool found = false;
    const CommentDaoResult readResult = m_dao.commentById(id, &existing, &found);
    if (!readResult.ok()) {
        return daoFailure(readResult);
    }
    if (!found) {
        return commentNotFound();
    }
    if (!canModify(currentUser, existing)) {
        return forbidden(QStringLiteral("comment_delete_forbidden"),
                         QStringLiteral("Only the author or an administrator can delete this comment."));
    }
    if (!existing.deletedAt.isEmpty()) {
        return {CommentServiceError::Conflict,
                QStringLiteral("comment_deleted"),
                QStringLiteral("Comment has already been deleted.")};
    }
    const QString deletedByName = currentUser.displayName.trimmed().isEmpty()
        ? currentUser.username : currentUser.displayName.trimmed();
    CommentServiceResult result;
    const CommentDaoResult deleteResult = m_dao.softDelete(
        id, currentUser.id, deletedByName, &result.comment);
    return deleteResult.ok() ? result : daoFailure(deleteResult);
}

CommentServiceResult CommentService::validateIssue(qint64 issueId) const
{
    bool exists = false;
    const CommentDaoResult daoResult = m_dao.issueExists(issueId, &exists);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    return exists ? CommentServiceResult() : notFound();
}

CommentServiceResult CommentService::daoFailure(
    const CommentDaoResult &result)
{
    if (result.error == CommentDaoError::Conflict) {
        return {CommentServiceError::Conflict,
                QStringLiteral("comment_conflict"),
                result.message};
    }
    return {CommentServiceError::Database,
            QStringLiteral("database_error"),
            result.message.isEmpty()
                ? QStringLiteral("Database operation failed.")
                : result.message};
}
