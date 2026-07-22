#include "httpservermanagerdialog.h"

#include "mainwindow.h"
#include "httpserver.h"
#include "myLogger.h"
#include "softwareconfig.h"

#include <QDesktopServices>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

HttpServerManagerDialog::HttpServerManagerDialog(MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
}

QDialog *HttpServerManagerDialog::setupHttpServerManagerDialog()
{
    if (m_dialog) {
        return m_dialog;
    }

    m_dialog = new QDialog(m_mainWindow);
    m_dialog->setObjectName("HttpServerManagerDialog");
    m_dialog->setMinimumSize(430, 520);

    setupUi();
    refreshConfiguration();
    setServerState(ServerState::Stopped);
    setReachabilityState(ReachabilityState::Unknown);

    return m_dialog;
}

void HttpServerManagerDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(m_dialog);

    auto *endpointGroup = new QGroupBox(tr("Service Endpoints"), m_dialog);
    auto *endpointLayout = new QFormLayout(endpointGroup);
    auto *refreshRow = new QWidget(endpointGroup);
    auto *refreshLayout = new QHBoxLayout(refreshRow);
    refreshLayout->setContentsMargins(0, 0, 0, 0);
    m_localUrlEdit = new QLineEdit(refreshRow);
    m_localUrlEdit->setReadOnly(true);
    m_openLocalButton = new QToolButton(refreshRow);
    m_openLocalButton->setIcon(
        m_dialog->style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_openLocalButton->setToolTip(tr("Open local URL"));
    m_refreshButton = new QToolButton(refreshRow);
    m_refreshButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh service endpoints"));
    refreshLayout->addWidget(m_localUrlEdit, 1);
    refreshLayout->addWidget(m_openLocalButton);
    refreshLayout->addWidget(m_refreshButton);

    auto createUrlRow = [this, endpointGroup](QLineEdit **urlEdit,
                                               QToolButton **openButton,
                                               const QString &toolTip) {
        auto *row = new QWidget(endpointGroup);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        *urlEdit = new QLineEdit(row);
        (*urlEdit)->setReadOnly(true);
        (*urlEdit)->setClearButtonEnabled(false);
        *openButton = new QToolButton(row);
        (*openButton)->setIcon(m_dialog->style()->standardIcon(QStyle::SP_DialogOpenButton));
        (*openButton)->setToolTip(toolTip);
        layout->addWidget(*urlEdit, 1);
        layout->addWidget(*openButton);
        return row;
    };

    endpointLayout->addRow(tr("Local:"), refreshRow);
    endpointLayout->addRow(
        tr("LAN:"), createUrlRow(&m_lanUrlEdit, &m_openLanButton, tr("Open LAN URL")));
    auto *testRow = new QWidget(endpointGroup);
    auto *testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    m_reachabilityLabel = new QLabel(testRow);
    m_testButton = new QPushButton(tr("Test"), testRow);
    m_testButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_DialogApplyButton));
    testLayout->addWidget(m_reachabilityLabel, 1);
    testLayout->addWidget(m_testButton);
    endpointLayout->addRow(tr("Reachability:"), testRow);
    mainLayout->addWidget(endpointGroup);

    auto *controlGroup = new QGroupBox(tr("Service Control"), m_dialog);
    auto *controlLayout = new QVBoxLayout(controlGroup);
    auto *stateLayout = new QFormLayout();
    m_serverStateLabel = new QLabel(controlGroup);
    m_serverDetailLabel = new QLabel(controlGroup);
    m_serverDetailLabel->setWordWrap(true);
    m_serverDetailLabel->hide();
    stateLayout->addRow(tr("Status:"), m_serverStateLabel);
    stateLayout->addRow(tr("Detail:"), m_serverDetailLabel);
    controlLayout->addLayout(stateLayout);

    auto *buttonLayout = new QHBoxLayout();
    m_startButton = new QPushButton(tr("Start"), controlGroup);
    m_startButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_MediaPlay));
    m_stopButton = new QPushButton(tr("Stop"), controlGroup);
    m_stopButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_MediaStop));
    m_restartButton = new QPushButton(tr("Restart"), controlGroup);
    m_restartButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_BrowserReload));
    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addWidget(m_restartButton);
    controlLayout->addLayout(buttonLayout);
    mainLayout->addWidget(controlGroup);

    m_feedbackLabel = new QLabel(m_dialog);
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->hide();
    mainLayout->addWidget(m_feedbackLabel);

    mainLayout->addStretch();

    connect(m_refreshButton, &QToolButton::clicked,
            this, &HttpServerManagerDialog::refreshConfiguration);
    connect(m_startButton, &QPushButton::clicked,
            this, &HttpServerManagerDialog::requestStart);
    connect(m_stopButton, &QPushButton::clicked,
            this, &HttpServerManagerDialog::requestStop);
    connect(m_restartButton, &QPushButton::clicked,
            this, &HttpServerManagerDialog::requestRestart);
    connect(m_testButton, &QPushButton::clicked,
            this, &HttpServerManagerDialog::requestReachabilityTest);
    connect(m_openLocalButton, &QToolButton::clicked,
            this, &HttpServerManagerDialog::openLocalUrl);
    connect(m_openLanButton, &QToolButton::clicked,
            this, &HttpServerManagerDialog::openLanUrl);
}

void HttpServerManagerDialog::refreshConfiguration()
{
    const ServerConfig config = currentConfiguration();
    m_localUrlEdit->setText(
        QStringLiteral("http://127.0.0.1:%1").arg(config.serverPort));
    updateUrls();
}

void HttpServerManagerDialog::updateUrls()
{
    const QUrl currentLocalUrl = localUrl();
    const QUrl currentLanUrl = lanUrl();
    const bool hasLocalUrl = currentLocalUrl.isValid() && !currentLocalUrl.isEmpty();
    const bool hasLanUrl = currentLanUrl.isValid() && !currentLanUrl.isEmpty();
    m_localUrlEdit->setText(currentLocalUrl.toString());
    m_lanUrlEdit->setText(hasLanUrl ? currentLanUrl.toString() : QString());
    m_openLocalButton->setEnabled(hasLocalUrl);
    m_openLanButton->setEnabled(hasLanUrl);
    m_testButton->setEnabled(m_reachabilityState != ReachabilityState::Testing
                             && (hasLanUrl || hasLocalUrl));
}

void HttpServerManagerDialog::requestStart()
{
    showFeedback(tr("Start request sent. Waiting for the server response."));
    const ServerConfig config = currentConfiguration();
    emit startServerRequested(config.serverInterface, config.serverPort);
}

void HttpServerManagerDialog::requestStop()
{
    showFeedback(tr("Stop request sent. Waiting for the server response."));
    emit stopServerRequested();
}

void HttpServerManagerDialog::requestRestart()
{
    showFeedback(tr("Restart request sent. Waiting for the server response."));
    const ServerConfig config = currentConfiguration();
    emit restartServerRequested(config.serverInterface, config.serverPort);
}

void HttpServerManagerDialog::requestReachabilityTest()
{
    const QUrl currentLanUrl = lanUrl();
    const QUrl targetUrl = currentLanUrl.isValid() && !currentLanUrl.isEmpty()
        ? currentLanUrl
        : localUrl();
    if (!targetUrl.isValid() || targetUrl.isEmpty()) {
        showFeedback(tr("No valid URL is available for testing."), true);
        return;
    }

    setReachabilityState(ReachabilityState::Testing);
    emit reachabilityTestRequested(targetUrl.resolved(QUrl(QStringLiteral("/api/health"))));
}

void HttpServerManagerDialog::openLocalUrl()
{
    QDesktopServices::openUrl(localUrl());
}

void HttpServerManagerDialog::openLanUrl()
{
    const QUrl url = lanUrl();
    if (url.isValid() && !url.isEmpty()) {
        QDesktopServices::openUrl(url);
    }
}

void HttpServerManagerDialog::setServerState(ServerState state, const QString &detail)
{
    m_serverState = state;

    switch (state) {
    case ServerState::Stopped:
        m_serverStateLabel->setText(tr("Stopped"));
        break;
    case ServerState::Starting:
        m_serverStateLabel->setText(tr("Starting"));
        break;
    case ServerState::Running:
        m_serverStateLabel->setText(tr("Running"));
        break;
    case ServerState::Stopping:
        m_serverStateLabel->setText(tr("Stopping"));
        break;
    case ServerState::Error:
        m_serverStateLabel->setText(tr("Error"));
        break;
    }

    m_serverDetailLabel->setText(detail);
    m_serverDetailLabel->setVisible(!detail.isEmpty());
    updateControlState();
}

void HttpServerManagerDialog::setReachabilityState(ReachabilityState state,
                                                    const QString &detail)
{
    m_reachabilityState = state;
    QString text;
    switch (state) {
    case ReachabilityState::Unknown:
        text = tr("Not tested");
        break;
    case ReachabilityState::Testing:
        text = tr("Testing...");
        break;
    case ReachabilityState::Reachable:
        text = tr("Reachable");
        break;
    case ReachabilityState::Unreachable:
        text = tr("Unreachable");
        break;
    }

    if (!detail.isEmpty()) {
        text += QStringLiteral(" - ") + detail;
    }
    m_reachabilityLabel->setText(text);
    m_testButton->setEnabled(state != ReachabilityState::Testing);
}

void HttpServerManagerDialog::showBootstrapCredentials(const QString &username,
                                                       const QString &password)
{
    showFeedback(tr("Initial administrator: %1 / %2. Store this password now; it will not be shown again.")
                     .arg(username, password));
}

void HttpServerManagerDialog::updateControlState()
{
    switch (m_serverState) {
    case ServerState::Stopped:
    case ServerState::Error:
        m_startButton->setEnabled(true);
        m_stopButton->setEnabled(false);
        m_restartButton->setEnabled(false);
        break;
    case ServerState::Starting:
        m_startButton->setEnabled(false);
        m_stopButton->setEnabled(true);
        m_restartButton->setEnabled(false);
        break;
    case ServerState::Running:
        m_startButton->setEnabled(false);
        m_stopButton->setEnabled(true);
        m_restartButton->setEnabled(true);
        break;
    case ServerState::Stopping:
        m_startButton->setEnabled(false);
        m_stopButton->setEnabled(false);
        m_restartButton->setEnabled(false);
        break;
    }
}

void HttpServerManagerDialog::showFeedback(const QString &message, bool isError)
{
    m_feedbackLabel->setText(message);
    m_feedbackLabel->setVisible(!message.isEmpty());
    m_feedbackLabel->setProperty("error", isError);
    m_feedbackLabel->style()->unpolish(m_feedbackLabel);
    m_feedbackLabel->style()->polish(m_feedbackLabel);
}

ServerConfig HttpServerManagerDialog::currentConfiguration() const
{
    ServerConfig config;
    const SoftwareConfig *settings = m_mainWindow->m_softwareconfig;
    config.serverInterface = settings->httpServerBindAllInterfaces()
        ? QStringLiteral("0.0.0.0")
        : settings->httpServerSelectedAddress();
    if (config.serverInterface.isEmpty()) {
        config.serverInterface = QStringLiteral("127.0.0.1");
    }
    config.serverPort = settings->httpServerPort();
    config.autoStart = settings->httpServerAutoStart();
    config.keepOriginal = settings->httpServerKeepOriginal();
    config.maxImageWidth = settings->httpServerMaxImageWidth();
    return config;
}

QUrl HttpServerManagerDialog::localUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1")
                   .arg(currentConfiguration().serverPort));
}

QUrl HttpServerManagerDialog::lanUrl() const
{
    const QString address = m_mainWindow->m_softwareconfig
        ->httpServerSelectedAddress().trimmed();
    if (address == QStringLiteral("0.0.0.0") || address == QStringLiteral("127.0.0.1")) {
        return QUrl();
    }
    if (address.isEmpty()) {
        return QUrl();
    }
    return QUrl(QStringLiteral("http://%1:%2")
                   .arg(address).arg(currentConfiguration().serverPort));
}
