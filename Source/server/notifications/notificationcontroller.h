#ifndef NOTIFICATIONCONTROLLER_H
#define NOTIFICATIONCONTROLLER_H

class ApiContext;
class NotificationManager;
class QHttpServer;

class NotificationController
{
public:
    NotificationController(ApiContext &apiContext,
                           NotificationManager &manager);

    void registerRoutes(QHttpServer &server);

private:
    ApiContext &m_apiContext;
    NotificationManager &m_manager;
};

#endif // NOTIFICATIONCONTROLLER_H
