#ifndef BLOCKSERVICE_H
#define BLOCKSERVICE_H

#include "blocks/blockdao.h"

#include <QJsonObject>
#include <QList>
#include <QString>

class AttachmentService;
class NotificationManager;

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
    BlockService(BlockDao &dao,
                 AttachmentService &attachmentService,
                 NotificationManager &notificationManager);

    BlockServiceResult list() const;
    BlockServiceResult get(qint64 id) const;
    BlockServiceResult create(const QJsonObject &values) const;
    BlockServiceResult update(qint64 id, const QJsonObject &values) const;
    BlockServiceResult remove(qint64 id);

private:
    static BlockServiceResult daoFailure(const BlockDaoResult &result);
    BlockDao &m_dao;
    AttachmentService &m_attachmentService;
    NotificationManager &m_notificationManager;
};

#endif // BLOCKSERVICE_H
