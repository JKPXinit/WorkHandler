#include "blocks/blockservice.h"

#include "attachments/attachmentservice.h"

#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace {
BlockServiceResult invalid(const QString &code, const QString &message)
{
    return {BlockServiceError::InvalidInput, code, message};
}

BlockServiceResult notFound()
{
    return {BlockServiceError::NotFound,
            QStringLiteral("block_not_found"),
            QStringLiteral("Block was not found.")};
}

bool readSortOrder(const QJsonValue &value, int *sortOrder)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0
        || std::floor(number) != number
        || number > double(std::numeric_limits<int>::max())) {
        return false;
    }
    if (sortOrder) {
        *sortOrder = int(number);
    }
    return true;
}

bool readTitle(const QJsonValue &value, QString *title)
{
    if (!value.isString()) {
        return false;
    }
    const QString normalized = value.toString().trimmed();
    if (normalized.isEmpty() || normalized.size() > 100) {
        return false;
    }
    if (title) {
        *title = normalized;
    }
    return true;
}

bool readDescription(const QJsonValue &value, QString *description)
{
    if (!value.isString() || value.toString().size() > 1000) {
        return false;
    }
    if (description) {
        *description = value.toString().trimmed();
    }
    return true;
}

bool readColor(const QJsonValue &value, QString *color)
{
    static const QRegularExpression Pattern(
        QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!value.isString()) {
        return false;
    }
    const QString normalized = value.toString().trimmed();
    if (!Pattern.match(normalized).hasMatch()) {
        return false;
    }
    if (color) {
        *color = normalized;
    }
    return true;
}
}

bool BlockServiceResult::ok() const
{
    return error == BlockServiceError::None;
}

BlockService::BlockService(BlockDao &dao,
                           AttachmentService &attachmentService)
    : m_dao(dao)
    , m_attachmentService(attachmentService)
{
}

BlockServiceResult BlockService::list() const
{
    BlockServiceResult result;
    const BlockDaoResult daoResult = m_dao.blocks(&result.blocks);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

BlockServiceResult BlockService::get(qint64 id) const
{
    std::optional<BlockRecord> block;
    const BlockDaoResult daoResult = m_dao.blockById(id, &block);
    if (!daoResult.ok()) {
        return daoFailure(daoResult);
    }
    if (!block) {
        return notFound();
    }
    BlockServiceResult result;
    result.block = *block;
    return result;
}

BlockServiceResult BlockService::create(const QJsonObject &values) const
{
    BlockRecord block;
    if (!values.contains(QStringLiteral("title"))
        || !readTitle(values.value(QStringLiteral("title")), &block.title)) {
        return invalid(QStringLiteral("invalid_block_title"),
                       QStringLiteral("Block title must contain 1 to 100 characters."));
    }

    block.description = QString();
    if (values.contains(QStringLiteral("description"))
        && !readDescription(values.value(QStringLiteral("description")),
                            &block.description)) {
        return invalid(QStringLiteral("invalid_block_description"),
                       QStringLiteral("Block description must not exceed 1000 characters."));
    }

    block.color = QStringLiteral("#0066cc");
    if (values.contains(QStringLiteral("color"))
        && !readColor(values.value(QStringLiteral("color")), &block.color)) {
        return invalid(QStringLiteral("invalid_block_color"),
                       QStringLiteral("Block color must use #RRGGBB format."));
    }

    if (values.contains(QStringLiteral("sort_order"))) {
        if (!readSortOrder(values.value(QStringLiteral("sort_order")),
                           &block.sortOrder)) {
            return invalid(QStringLiteral("invalid_block_sort_order"),
                           QStringLiteral("Block sort order must be a nonnegative integer."));
        }
    } else {
        int maximum = -1;
        const BlockDaoResult maximumResult = m_dao.maximumSortOrder(&maximum);
        if (!maximumResult.ok()) {
            return daoFailure(maximumResult);
        }
        if (maximum == std::numeric_limits<int>::max()) {
            return {BlockServiceError::Conflict,
                    QStringLiteral("block_sort_order_exhausted"),
                    QStringLiteral("No additional block sort order is available.")};
        }
        block.sortOrder = maximum + 1;
    }

    BlockServiceResult result;
    const BlockDaoResult daoResult = m_dao.create(block, &result.block);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

BlockServiceResult BlockService::update(qint64 id,
                                        const QJsonObject &values) const
{
    BlockServiceResult existingResult = get(id);
    if (!existingResult.ok()) {
        return existingResult;
    }

    BlockRecord block = existingResult.block;
    bool hasUpdate = false;
    if (values.contains(QStringLiteral("title"))) {
        hasUpdate = true;
        if (!readTitle(values.value(QStringLiteral("title")), &block.title)) {
            return invalid(QStringLiteral("invalid_block_title"),
                           QStringLiteral("Block title must contain 1 to 100 characters."));
        }
    }
    if (values.contains(QStringLiteral("description"))) {
        hasUpdate = true;
        if (!readDescription(values.value(QStringLiteral("description")),
                             &block.description)) {
            return invalid(QStringLiteral("invalid_block_description"),
                           QStringLiteral("Block description must not exceed 1000 characters."));
        }
    }
    if (values.contains(QStringLiteral("color"))) {
        hasUpdate = true;
        if (!readColor(values.value(QStringLiteral("color")), &block.color)) {
            return invalid(QStringLiteral("invalid_block_color"),
                           QStringLiteral("Block color must use #RRGGBB format."));
        }
    }
    if (values.contains(QStringLiteral("sort_order"))) {
        hasUpdate = true;
        if (!readSortOrder(values.value(QStringLiteral("sort_order")),
                           &block.sortOrder)) {
            return invalid(QStringLiteral("invalid_block_sort_order"),
                           QStringLiteral("Block sort order must be a nonnegative integer."));
        }
    }
    if (!hasUpdate) {
        return invalid(QStringLiteral("missing_block_fields"),
                       QStringLiteral("At least one block field is required."));
    }

    BlockServiceResult result;
    const BlockDaoResult daoResult = m_dao.update(block, &result.block);
    return daoResult.ok() ? result : daoFailure(daoResult);
}

BlockServiceResult BlockService::remove(qint64 id) const
{
    const BlockServiceResult existing = get(id);
    if (!existing.ok()) {
        return existing;
    }

    StagedFileRemoval staged;
    const AttachmentServiceResult stageResult =
        m_attachmentService.stageBlockRemoval(id, &staged);
    if (!stageResult.ok()) {
        return {BlockServiceError::Database,
                stageResult.code,
                stageResult.message};
    }

    bool removed = false;
    const BlockDaoResult daoResult = m_dao.remove(id, &removed);
    if (!daoResult.ok()) {
        m_attachmentService.rollbackRemoval(&staged);
        return daoFailure(daoResult);
    }
    if (!removed) {
        m_attachmentService.rollbackRemoval(&staged);
        return notFound();
    }
    m_attachmentService.commitRemoval(&staged);

    BlockServiceResult result;
    result.deletedId = id;
    return result;
}

BlockServiceResult BlockService::daoFailure(const BlockDaoResult &result)
{
    if (result.error == BlockDaoError::Conflict) {
        return {BlockServiceError::Conflict,
                QStringLiteral("block_conflict"),
                result.message};
    }
    return {BlockServiceError::Database,
            QStringLiteral("database_error"),
            result.message.isEmpty()
                ? QStringLiteral("Database operation failed.")
                : result.message};
}
