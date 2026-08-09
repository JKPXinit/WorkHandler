#include "notificationdialog.h"

#include "httpserver.h"
#include "mainwindow.h"

#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <utility>

NotificationDialog::NotificationDialog(MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
}

QDialog *NotificationDialog::setupNotificationDialog()
{
    if (m_dialog) {
        refreshNotifications();
        return m_dialog;
    }

    m_dialog = new QDialog(m_mainWindow);
    m_dialog->setObjectName(QStringLiteral("NotificationDialog"));
    m_dialog->setWindowTitle(tr("Notifications"));
    m_dialog->setMinimumSize(420, 320);

    auto *mainLayout = new QVBoxLayout(m_dialog);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Unread notifications"), m_dialog);
    titleLabel->setObjectName(QStringLiteral("notificationTitle"));
    m_countLabel = new QLabel(m_dialog);
    m_countLabel->setObjectName(QStringLiteral("notificationCount"));
    auto *refreshButton = new QPushButton(tr("Refresh"), m_dialog);
    refreshButton->setObjectName(QStringLiteral("refreshNotificationsButton"));
    refreshButton->setToolTip(tr("Refresh unread notifications"));
    connect(refreshButton, &QPushButton::clicked,
            this, &NotificationDialog::refreshNotifications);
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(m_countLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(refreshButton);
    mainLayout->addLayout(headerLayout);

    m_feedbackLabel = new QLabel(m_dialog);
    m_feedbackLabel->setObjectName(QStringLiteral("notificationFeedback"));
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->hide();
    mainLayout->addWidget(m_feedbackLabel);

    auto *scrollArea = new QScrollArea(m_dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    m_listContainer = new QWidget(scrollArea);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(2, 2, 2, 2);
    m_listLayout->setSpacing(8);
    m_emptyLabel = new QLabel(tr("No unread notifications."), m_listContainer);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_listLayout->addWidget(m_emptyLabel);
    m_listLayout->addStretch();
    scrollArea->setWidget(m_listContainer);
    mainLayout->addWidget(scrollArea, 1);

    refreshNotifications();
    return m_dialog;
}

void NotificationDialog::refreshNotifications()
{
    if (m_refreshing || !m_mainWindow || !m_mainWindow->m_httpServer) {
        return;
    }
    m_refreshing = true;
    QList<NotificationRecord> notifications;
    QString errorMessage;
    const bool ok = m_mainWindow->m_httpServer->localAdminUnreadNotifications(
        &notifications, &errorMessage);
    m_refreshing = false;
    if (!ok) {
        setFeedback(errorMessage.isEmpty() ? tr("Failed to load notifications.")
                                           : errorMessage, true);
        return;
    }

    clearBlocks();
    for (const NotificationRecord &notification : notifications) {
        addBlock(notification);
    }
    m_emptyLabel->setVisible(notifications.isEmpty());
    m_countLabel->setText(tr("%1 unread").arg(notifications.size()));
    setFeedback(QString());
    emit unreadCountChanged(notifications.size());
}

void NotificationDialog::clearBlocks()
{
    for (QWidget *block : std::as_const(m_blocks)) {
        m_listLayout->removeWidget(block);
        block->deleteLater();
    }
    m_blocks.clear();
}

void NotificationDialog::addBlock(const NotificationRecord &notification)
{
    auto *block = new QFrame(m_listContainer);
    block->setObjectName(QStringLiteral("notificationBlock"));
    block->setFrameShape(QFrame::StyledPanel);
    block->setFrameShadow(QFrame::Raised);

    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(10, 8, 10, 8);
    auto *titleLayout = new QHBoxLayout();
    auto *title = new QLabel(notification.title, block);
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *time = new QLabel(createdAtText(notification.createdAt), block);
    time->setAlignment(Qt::AlignRight | Qt::AlignTop);
    time->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleLayout->addWidget(title, 1);
    titleLayout->addWidget(time);
    layout->addLayout(titleLayout);

    auto *content = new QLabel(notification.content, block);
    content->setWordWrap(true);
    content->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(content);

    const QString sender = senderText(notification);
    if (!sender.isEmpty()) {
        auto *senderLabel = new QLabel(sender, block);
        senderLabel->setWordWrap(true);
        layout->addWidget(senderLabel);
    }

    auto *actions = new QHBoxLayout();
    if (notification.relatedId > 0) {
        auto *openButton = new QPushButton(tr("Open related issue"), block);
        connect(openButton, &QPushButton::clicked, this,
                [this, issueId = notification.relatedId]() {
                    emit openIssueRequested(issueId);
                });
        actions->addWidget(openButton);
    }
    actions->addStretch();
    auto *readButton = new QPushButton(tr("Mark as read"), block);
    connect(readButton, &QPushButton::clicked, this,
            [this, notificationId = notification.id]() {
                markAsRead(notificationId);
            });
    actions->addWidget(readButton);
    layout->addLayout(actions);

    m_blocks.insert(notification.id, block);
    m_listLayout->insertWidget(m_listLayout->count() - 1, block);
}

void NotificationDialog::markAsRead(qint64 notificationId)
{
    if (!m_mainWindow || !m_mainWindow->m_httpServer) {
        return;
    }
    QString errorMessage;
    if (!m_mainWindow->m_httpServer->markLocalAdminNotificationRead(
            notificationId, &errorMessage)) {
        setFeedback(errorMessage.isEmpty() ? tr("Failed to mark notification as read.")
                                           : errorMessage, true);
        return;
    }
    QWidget *block = m_blocks.take(notificationId);
    if (block) {
        m_listLayout->removeWidget(block);
        block->deleteLater();
    }
    m_emptyLabel->setVisible(m_blocks.isEmpty());
    m_countLabel->setText(tr("%1 unread").arg(m_blocks.size()));
    setFeedback(QString());
    emit unreadCountChanged(m_blocks.size());
}

void NotificationDialog::setFeedback(const QString &message, bool isError)
{
    if (!m_feedbackLabel) {
        return;
    }
    m_feedbackLabel->setText(message);
    m_feedbackLabel->setStyleSheet(isError ? QStringLiteral("color: #b00020;")
                                           : QString());
    m_feedbackLabel->setVisible(!message.isEmpty());
}

QString NotificationDialog::senderText(const NotificationRecord &notification) const
{
    if (!notification.sender) {
        return QString();
    }
    const QString name = notification.sender->displayName.isEmpty()
        ? notification.sender->username : notification.sender->displayName;
    return name.isEmpty() ? QString() : tr("From: %1").arg(name);
}

QString NotificationDialog::createdAtText(const QString &createdAt) const
{
    const QDateTime dateTime = QDateTime::fromString(createdAt, Qt::ISODateWithMs);
    return dateTime.isValid() ? QLocale().toString(dateTime, QLocale::ShortFormat)
                              : createdAt;
}
