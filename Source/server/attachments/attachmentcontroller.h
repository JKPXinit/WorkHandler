#ifndef ATTACHMENTCONTROLLER_H
#define ATTACHMENTCONTROLLER_H

#include <QHttpServerResponse>

class ApiContext;
class AttachmentService;
struct AttachmentServiceResult;
class QHttpServer;

class AttachmentController
{
public:
    AttachmentController(ApiContext &apiContext,
                         AttachmentService &service);

    void registerRoutes(QHttpServer &server);

private:
    static QHttpServerResponse errorResponse(
        const AttachmentServiceResult &result);

    ApiContext &m_apiContext;
    AttachmentService &m_service;
};

#endif // ATTACHMENTCONTROLLER_H
