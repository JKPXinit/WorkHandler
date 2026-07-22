#ifndef USEROPTIONSCONTROLLER_H
#define USEROPTIONSCONTROLLER_H

class ApiContext;
class QHttpServer;
class UserOptionsService;

class UserOptionsController
{
public:
    UserOptionsController(ApiContext &apiContext,
                          UserOptionsService &service);

    void registerRoutes(QHttpServer &server);

private:
    ApiContext &m_apiContext;
    UserOptionsService &m_service;
};

#endif // USEROPTIONSCONTROLLER_H
