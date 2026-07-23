#ifndef TRAYICONRENDERER_H
#define TRAYICONRENDERER_H

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QUrl>

enum class TrayServerState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error
};

enum class TrayServerAction {
    Start,
    Stop,
    Starting,
    Stopping,
    Retry
};

struct TrayActionState
{
    bool openWebEnabled {false};
    bool serverActionEnabled {true};
    TrayServerAction serverAction {TrayServerAction::Start};
    bool markAllReadEnabled {false};
};

class TrayIconRenderer
{
public:
    static QString badgeText(qint64 unreadCount);
    static QPixmap render(const QIcon &baseIcon,
                          TrayServerState state,
                          qint64 unreadCount,
                          int logicalSize = 32,
                          qreal devicePixelRatio = 1.0);
    static TrayActionState actionState(TrayServerState state,
                                       qint64 unreadCount);
    static QUrl rootUrl(quint16 port);
    static QUrl issueUrl(quint16 port, qint64 issueId);
};

#endif // TRAYICONRENDERER_H
