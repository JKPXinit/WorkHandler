#include "issuetoast.h"

#include <QCloseEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QRect>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int ToastDurationMilliseconds = 5000;
constexpr int ToastMargin = 16;
constexpr int ToastMaximumWidth = 380;
}

IssueToast::IssueToast(qint64 issueId,
                       const QString &title,
                       const QString &content,
                       QWidget *parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint
                         | Qt::WindowStaysOnTopHint)
    , m_issueId(issueId)
    , m_autoCloseTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("IssueToast"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral(
        "QFrame#IssueToast {"
        "  background: #202124;"
        "  border: 1px solid #4b5058;"
        "  border-radius: 6px;"
        "}"
        "QLabel#IssueToastTitle {"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "}"
        "QLabel#IssueToastContent {"
        "  color: #d8dce3;"
        "}"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(6);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("IssueToastTitle"));
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    auto *contentLabel = new QLabel(content, this);
    contentLabel->setObjectName(QStringLiteral("IssueToastContent"));
    contentLabel->setTextFormat(Qt::PlainText);
    contentLabel->setWordWrap(true);
    contentLabel->setVisible(!content.isEmpty());
    layout->addWidget(contentLabel);

    m_autoCloseTimer->setSingleShot(true);
    m_autoCloseTimer->setInterval(ToastDurationMilliseconds);
    connect(m_autoCloseTimer, &QTimer::timeout, this, &QWidget::close);
}

qint64 IssueToast::issueId() const
{
    return m_issueId;
}

void IssueToast::showAt(const QRect &availableGeometry)
{
    const int availableWidth = qMax(1, availableGeometry.width()
                                           - ToastMargin * 2);
    setFixedWidth(qMin(ToastMaximumWidth, availableWidth));
    adjustSize();

    const int x = availableGeometry.x() + availableGeometry.width()
        - width() - ToastMargin;
    const int y = availableGeometry.y() + availableGeometry.height()
        - height() - ToastMargin;
    move(qMax(availableGeometry.x(), x), qMax(availableGeometry.y(), y));
    show();
    raise();
    m_autoCloseTimer->start();
}

void IssueToast::closeEvent(QCloseEvent *event)
{
    m_autoCloseTimer->stop();
    QFrame::closeEvent(event);
    emit dismissed();
}

void IssueToast::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit activated(m_issueId);
        close();
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}
