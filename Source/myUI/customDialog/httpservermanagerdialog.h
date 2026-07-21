#ifndef HTTPSERVERMANAGERDIALOG_H
#define HTTPSERVERMANAGERDIALOG_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

class MainWindow;
class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

class HttpServerManagerDialog : public QObject
{
    Q_OBJECT

public:
    enum class ServerState {
        Stopped,
        Starting,
        Running,
        Stopping,
        Error
    };
    Q_ENUM(ServerState)

    enum class ReachabilityState {
        Unknown,
        Testing,
        Reachable,
        Unreachable
    };
    Q_ENUM(ReachabilityState)

    explicit HttpServerManagerDialog(MainWindow *mainWindow);

    QDialog *setupHttpServerManagerDialog();

public slots:
    void setServerState(ServerState state, const QString &detail = QString());
    void setReachabilityState(ReachabilityState state, const QString &detail = QString());

signals:
    void startServerRequested(const QString &bindAddress, quint16 port);
    void stopServerRequested();
    void restartServerRequested(const QString &bindAddress, quint16 port);
    void reachabilityTestRequested(const QUrl &url);

private slots:
    void refreshNetworkInterfaces();
    void onInterfaceChanged();
    void updateUrls();
    void saveConfiguration();
    void requestStart();
    void requestStop();
    void requestRestart();
    void requestReachabilityTest();
    void openLocalUrl();
    void openLanUrl();

private:
    void setupUi();
    void populateAddresses(const QString &preferredAddress = QString());
    void updateControlState();
    void showFeedback(const QString &message, bool isError = false);
    QString selectedAddress() const;
    QString effectiveBindAddress() const;
    QUrl localUrl() const;
    QUrl lanUrl() const;
    bool validateConfiguration();

    MainWindow *m_mainWindow {nullptr};
    QDialog *m_dialog {nullptr};
    QComboBox *m_interfaceCombo {nullptr};
    QComboBox *m_addressCombo {nullptr};
    QSpinBox *m_portSpinBox {nullptr};
    QCheckBox *m_bindAllCheckBox {nullptr};
    QLineEdit *m_localUrlEdit {nullptr};
    QLineEdit *m_lanUrlEdit {nullptr};
    QLabel *m_serverStateLabel {nullptr};
    QLabel *m_serverDetailLabel {nullptr};
    QLabel *m_reachabilityLabel {nullptr};
    QLabel *m_feedbackLabel {nullptr};
    QPushButton *m_startButton {nullptr};
    QPushButton *m_stopButton {nullptr};
    QPushButton *m_restartButton {nullptr};
    QPushButton *m_testButton {nullptr};
    QToolButton *m_refreshButton {nullptr};
    QToolButton *m_openLocalButton {nullptr};
    QToolButton *m_openLanButton {nullptr};
    ServerState m_serverState {ServerState::Stopped};
    ReachabilityState m_reachabilityState {ReachabilityState::Unknown};
};

#endif // HTTPSERVERMANAGERDIALOG_H
