#ifndef BLOCKSERVICE_H
#define BLOCKSERVICE_H

#include "blocks/blockdao.h"

#include <QJsonObject>
#include <QList>
#include <QString>

enum class BlockServiceError {
    None,
    InvalidInput,
    NotFound,
    Conflict,
    Database
};

struct BlockServiceResult
{
    BlockServiceError error {BlockServiceError::None};
    QString code;
    QString message;
    QList<BlockRecord> blocks;
    BlockRecord block;
    qint64 deletedId {0};

    bool ok() const;
};

class BlockService
{
public:
    explicit BlockService(BlockDao &dao);

    BlockServiceResult list() const;
    BlockServiceResult get(qint64 id) const;
    BlockServiceResult create(const QJsonObject &values) const;
    BlockServiceResult update(qint64 id, const QJsonObject &values) const;
    BlockServiceResult remove(qint64 id) const;

private:
    static BlockServiceResult daoFailure(const BlockDaoResult &result);
    BlockDao &m_dao;
};

#endif // BLOCKSERVICE_H
