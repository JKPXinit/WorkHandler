#include "trayiconrenderer.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>

QString TrayIconRenderer::badgeText(qint64 unreadCount)
{
    if (unreadCount <= 0) {
        return {};
    }
    return unreadCount > 99 ? QStringLiteral("99+")
                            : QString::number(unreadCount);
}

QPixmap TrayIconRenderer::render(const QIcon &baseIcon,
                                 TrayServerState state,
                                 qint64 unreadCount,
                                 int logicalSize,
                                 qreal devicePixelRatio)
{
    const qreal dpr = qMax<qreal>(1.0, devicePixelRatio);
    const int size = qMax(16, qRound(logicalSize * dpr));
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPixmap source = baseIcon.pixmap(QSize(size, size));
    if (!source.isNull()) {
        source = source.scaled(size, size, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
        QPainter basePainter(&pixmap);
        basePainter.drawPixmap((size - source.width()) / 2,
                               (size - source.height()) / 2,
                               source);
    }

    if (state == TrayServerState::Stopped
        || state == TrayServerState::Starting
        || state == TrayServerState::Stopping) {
        QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QColor source = QColor::fromRgba(line[x]);
                const int gray = qGray(source.rgb());
                line[x] = qRgba(gray, gray, gray, source.alpha());
            }
        }
        pixmap = QPixmap::fromImage(image);
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (state == TrayServerState::Error) {
        const qreal markerSize = size * 0.28;
        painter.setPen(QPen(Qt::white, qMax<qreal>(1.0, dpr)));
        painter.setBrush(QColor(QStringLiteral("#d7263d")));
        painter.drawEllipse(QRectF(size * 0.04, size - markerSize - size * 0.04,
                                   markerSize, markerSize));
    }

    const QString badge = badgeText(unreadCount);
    if (!badge.isEmpty()) {
        const qreal height = size * 0.43;
        const qreal width = badge == QStringLiteral("99+")
            ? size * 0.62 : qMax(height, size * 0.34 + badge.size() * size * 0.08);
        const QRectF badgeRect(size - width, 0, width, height);
        painter.setPen(QPen(Qt::white, qMax<qreal>(1.0, dpr)));
        painter.setBrush(QColor(QStringLiteral("#d7263d")));
        painter.drawRoundedRect(badgeRect, height / 2, height / 2);
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(qMax(7, qRound(size * 0.24)));
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter, badge);
    }
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

TrayActionState TrayIconRenderer::actionState(TrayServerState state,
                                              qint64 unreadCount)
{
    TrayActionState result;
    result.markAllReadEnabled = unreadCount > 0;
    switch (state) {
    case TrayServerState::Running:
        result.openWebEnabled = true;
        result.serverAction = TrayServerAction::Stop;
        break;
    case TrayServerState::Stopped:
        result.serverAction = TrayServerAction::Start;
        break;
    case TrayServerState::Starting:
        result.serverAction = TrayServerAction::Starting;
        result.serverActionEnabled = false;
        break;
    case TrayServerState::Stopping:
        result.serverAction = TrayServerAction::Stopping;
        result.serverActionEnabled = false;
        break;
    case TrayServerState::Error:
        result.serverAction = TrayServerAction::Retry;
        break;
    }
    return result;
}

QUrl TrayIconRenderer::rootUrl(quint16 port)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port));
}

QUrl TrayIconRenderer::issueUrl(quint16 port, qint64 issueId)
{
    if (issueId <= 0) {
        return rootUrl(port);
    }
    return QUrl(QStringLiteral("http://127.0.0.1:%1/#/issues/%2")
                    .arg(port).arg(issueId));
}
