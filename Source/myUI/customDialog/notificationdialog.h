#ifndef NOTIFICATIONDIALOG_H
#define NOTIFICATIONDIALOG_H

#include <QObject>
#include <QHash>
#include <QList>

#include "notifications/notificationdao.h"

class MainWindow;
class QDialog;
class QLabel;
class QVBoxLayout;
class QWidget;

class NotificationDialog : public QObject
{
    Q_OBJECT

public:
    explicit NotificationDialog(MainWindow *mainWindow);

    QDialog *setupNotificationDialog();

signals:
    void openIssueRequested(qint64 issueId);
    void unreadCountChanged(qint64 count);

public slots:
    void refreshNotifications();

private:
    void clearBlocks();
    void addBlock(const NotificationRecord &notification);
    void markAsRead(qint64 notificationId);
    void setFeedback(const QString &message, bool isError = false);
    QString senderText(const NotificationRecord &notification) const;
    QString createdAtText(const QString &createdAt) const;

    MainWindow *m_mainWindow {nullptr};
    QDialog *m_dialog {nullptr};
    QWidget *m_listContainer {nullptr};
    QVBoxLayout *m_listLayout {nullptr};
    QLabel *m_countLabel {nullptr};
    QLabel *m_emptyLabel {nullptr};
    QLabel *m_feedbackLabel {nullptr};
    QHash<qint64, QWidget *> m_blocks;
    bool m_refreshing {false};
};

#endif // NOTIFICATIONDIALOG_H
