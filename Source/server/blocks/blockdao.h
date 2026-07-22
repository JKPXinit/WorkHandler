#ifndef BLOCKDAO_H
#define BLOCKDAO_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

class DatabaseManager;
class QSqlQuery;

struct BlockRecord
{
    qint64 id {0};
    QString title;
    QString description;
    QString color;
    int sortOrder {0};
    qint64 issueCount {0};

    QJsonObject toJson() const;
};

enum class BlockDaoError {
    None,
    Conflict,
    Database
};

struct BlockDaoResult
{
    BlockDaoError error {BlockDaoError::None};
    QString message;

    bool ok() const;
};

class BlockDao
{
public:
    explicit BlockDao(DatabaseManager &database);

    BlockDaoResult blocks(QList<BlockRecord> *blocks) const;
    BlockDaoResult blockById(qint64 id,
                             std::optional<BlockRecord> *block) const;
    BlockDaoResult maximumSortOrder(int *sortOrder) const;
    BlockDaoResult create(const BlockRecord &values,
                          BlockRecord *createdBlock) const;
    BlockDaoResult update(const BlockRecord &values,
                          BlockRecord *updatedBlock) const;
    BlockDaoResult remove(qint64 id, bool *removed) const;

private:
    static BlockRecord readBlock(const QSqlQuery &query);
    DatabaseManager &m_database;
};

#endif // BLOCKDAO_H
