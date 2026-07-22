#ifndef ISSUECONTROLLER_H
#define ISSUECONTROLLER_H

#include <QHttpServerResponse>

class ApiContext;
class IssueService;
struct IssueServiceResult;
class QHttpServer;

class IssueController
{
public:
    IssueController(ApiContext &apiContext, IssueService &service);

    void registerRoutes(QHttpServer &server);

private:
    static QHttpServerResponse errorResponse(const IssueServiceResult &result);

    ApiContext &m_apiContext;
    IssueService &m_service;
};

#endif // ISSUECONTROLLER_H
