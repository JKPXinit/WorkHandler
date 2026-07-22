#ifndef BLOCKCONTROLLER_H
#define BLOCKCONTROLLER_H

#include <QHttpServerResponse>

class ApiContext;
class BlockService;
struct BlockServiceResult;
class QHttpServer;

class BlockController
{
public:
    BlockController(ApiContext &apiContext, BlockService &service);

    void registerRoutes(QHttpServer &server);

private:
    static QHttpServerResponse errorResponse(const BlockServiceResult &result);

    ApiContext &m_apiContext;
    BlockService &m_service;
};

#endif // BLOCKCONTROLLER_H
