#ifndef ISSUETOAST_H
#define ISSUETOAST_H

#include <QFrame>
#include <QString>

class QCloseEvent;
class QMouseEvent;
class QRect;
class QTimer;

class IssueToast final : public QFrame
{
    Q_OBJECT

public:
    explicit IssueToast(qint64 issueId,
                        const QString &title,
                        const QString &content,
                        QWidget *parent = nullptr);

    qint64 issueId() const;
    void showAt(const QRect &availableGeometry);

signals:
    void activated(qint64 issueId);
    void dismissed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    qint64 m_issueId {0};
    QTimer *m_autoCloseTimer {nullptr};
};

#endif // ISSUETOAST_H
