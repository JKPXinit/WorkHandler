#ifndef COMMENTCONTROLLER_H
#define COMMENTCONTROLLER_H

#include <QHttpServerResponse>

class ApiContext;
class CommentService;
struct CommentServiceResult;
class QHttpServer;

class CommentController
{
public:
    CommentController(ApiContext &apiContext, CommentService &service);

    void registerRoutes(QHttpServer &server);

private:
    static QHttpServerResponse errorResponse(
        const CommentServiceResult &result);

    ApiContext &m_apiContext;
    CommentService &m_service;
};

#endif // COMMENTCONTROLLER_H
