#include "blocks/blockdao.h"

#include "databasemanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
BlockDaoResult success()
{
    return {};
}

BlockDaoResult failure(const QSqlError &error, bool allowConflict)
{
    const QString nativeCode = error.nativeErrorCode();
    const QString detail = error.text();
    const bool conflict = allowConflict
        && (nativeCode == QStringLiteral("19")
            || detail.contains(QStringLiteral("constraint"),
                               Qt::CaseInsensitive));
    return {
        conflict ? BlockDaoError::Conflict : BlockDaoError::Database,
        detail.isEmpty() ? QStringLiteral("Database operation failed.") : detail
    };
}

QString blockProjection()
{
    return QStringLiteral(
        "SELECT b.id, b.title, COALESCE(b.description, ''), "
        "COALESCE(b.color, '#3b82f6'), COALESCE(b.sort_order, 0), "
        "COUNT(i.id) "
        "FROM blocks b LEFT JOIN issues i ON i.block_id = b.id ");
}
}

QJsonObject BlockRecord::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), description},
        {QStringLiteral("color"), color},
        {QStringLiteral("sort_order"), sortOrder},
        {QStringLiteral("issue_count"), issueCount}
    };
}

bool BlockDaoResult::ok() const
{
    return error == BlockDaoError::None;
}

BlockDao::BlockDao(DatabaseManager &database)
    : m_database(database)
{
}

BlockDaoResult BlockDao::blocks(QList<BlockRecord> *blocks) const
{
    if (blocks) {
        blocks->clear();
    }

    QSqlQuery query(m_database.connection());
    if (!query.exec(blockProjection()
                    + QStringLiteral(
                        "GROUP BY b.id, b.title, b.description, b.color, b.sort_order "
                        "ORDER BY b.sort_order ASC, b.id ASC"))) {
        return failure(query.lastError(), false);
    }

    if (blocks) {
        while (query.next()) {
            blocks->append(readBlock(query));
        }
    }
    return success();
}

BlockDaoResult BlockDao::blockById(
    qint64 id, std::optional<BlockRecord> *block) const
{
    if (block) {
        block->reset();
    }

    QSqlQuery query(m_database.connection());
    query.prepare(blockProjection()
                  + QStringLiteral(
                      "WHERE b.id = ? "
                      "GROUP BY b.id, b.title, b.description, b.color, b.sort_order"));
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), false);
    }
    if (query.next() && block) {
        *block = readBlock(query);
    }
    return success();
}

BlockDaoResult BlockDao::maximumSortOrder(int *sortOrder) const
{
    if (sortOrder) {
        *sortOrder = -1;
    }

    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral(
            "SELECT COALESCE(MAX(sort_order), -1) FROM blocks"))
        || !query.next()) {
        return failure(query.lastError(), false);
    }
    if (sortOrder) {
        *sortOrder = query.value(0).toInt();
    }
    return success();
}

BlockDaoResult BlockDao::create(const BlockRecord &values,
                                BlockRecord *createdBlock) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO blocks(title, description, color, sort_order) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(values.title);
    query.addBindValue(values.description);
    query.addBindValue(values.color);
    query.addBindValue(values.sortOrder);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }

    std::optional<BlockRecord> created;
    const BlockDaoResult readResult = blockById(
        query.lastInsertId().toLongLong(), &created);
    if (!readResult.ok()) {
        return readResult;
    }
    if (!created) {
        return {BlockDaoError::Database,
                QStringLiteral("The created block could not be read back.")};
    }
    if (createdBlock) {
        *createdBlock = *created;
    }
    return success();
}

BlockDaoResult BlockDao::update(const BlockRecord &values,
                                BlockRecord *updatedBlock) const
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "UPDATE blocks SET title = ?, description = ?, color = ?, sort_order = ? "
        "WHERE id = ?"));
    query.addBindValue(values.title);
    query.addBindValue(values.description);
    query.addBindValue(values.color);
    query.addBindValue(values.sortOrder);
    query.addBindValue(values.id);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }

    std::optional<BlockRecord> updated;
    const BlockDaoResult readResult = blockById(values.id, &updated);
    if (!readResult.ok()) {
        return readResult;
    }
    if (!updated) {
        return {BlockDaoError::Database,
                QStringLiteral("The updated block could not be read back.")};
    }
    if (updatedBlock) {
        *updatedBlock = *updated;
    }
    return success();
}

BlockDaoResult BlockDao::remove(qint64 id, bool *removed) const
{
    if (removed) {
        *removed = false;
    }

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("DELETE FROM blocks WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return failure(query.lastError(), true);
    }
    if (removed) {
        *removed = query.numRowsAffected() > 0;
    }
    return success();
}

BlockRecord BlockDao::readBlock(const QSqlQuery &query)
{
    BlockRecord block;
    block.id = query.value(0).toLongLong();
    block.title = query.value(1).toString();
    block.description = query.value(2).toString();
    block.color = query.value(3).toString();
    block.sortOrder = query.value(4).toInt();
    block.issueCount = query.value(5).toLongLong();
    return block;
}
