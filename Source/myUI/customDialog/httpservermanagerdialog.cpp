#include "httpservermanagerdialog.h"

#include "mainwindow.h"
#include "myLogger.h"
#include "softwareconfig.h"

#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
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
    m_dialog->setMinimumSize(430, 440);

    setupUi();
    refreshNetworkInterfaces();
    setServerState(ServerState::Stopped);
    setReachabilityState(ReachabilityState::Unknown);

    return m_dialog;
}

void HttpServerManagerDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(m_dialog);

    auto *networkGroup = new QGroupBox(tr("Network Configuration"), m_dialog);
    auto *networkLayout = new QFormLayout(networkGroup);

    auto *interfaceRow = new QWidget(networkGroup);
    auto *interfaceLayout = new QHBoxLayout(interfaceRow);
    interfaceLayout->setContentsMargins(0, 0, 0, 0);
    m_interfaceCombo = new QComboBox(interfaceRow);
    m_interfaceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_refreshButton = new QToolButton(interfaceRow);
    m_refreshButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh network interfaces"));
    interfaceLayout->addWidget(m_interfaceCombo, 1);
    interfaceLayout->addWidget(m_refreshButton);

    m_addressCombo = new QComboBox(networkGroup);
    m_portSpinBox = new QSpinBox(networkGroup);
    m_portSpinBox->setRange(1, 65535);
    m_portSpinBox->setValue(m_mainWindow->m_softwareconfig->httpServerPort());
    m_bindAllCheckBox = new QCheckBox(tr("Bind all interfaces (0.0.0.0)"), networkGroup);
    m_bindAllCheckBox->setChecked(
        m_mainWindow->m_softwareconfig->httpServerBindAllInterfaces());

    networkLayout->addRow(tr("NIC:"), interfaceRow);
    networkLayout->addRow(tr("IP Address:"), m_addressCombo);
    networkLayout->addRow(tr("Port:"), m_portSpinBox);
    networkLayout->addRow(QString(), m_bindAllCheckBox);
    mainLayout->addWidget(networkGroup);

    auto *endpointGroup = new QGroupBox(tr("Service Endpoints"), m_dialog);
    auto *endpointLayout = new QFormLayout(endpointGroup);

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

    endpointLayout->addRow(
        tr("Local:"), createUrlRow(&m_localUrlEdit, &m_openLocalButton, tr("Open local URL")));
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

    auto *saveLayout = new QHBoxLayout();
    saveLayout->addStretch();
    auto *saveButton = new QPushButton(tr("Save Config"), m_dialog);
    saveButton->setIcon(m_dialog->style()->standardIcon(QStyle::SP_DialogSaveButton));
    saveLayout->addWidget(saveButton);
    mainLayout->addLayout(saveLayout);
    mainLayout->addStretch();

    connect(m_refreshButton, &QToolButton::clicked,
            this, &HttpServerManagerDialog::refreshNetworkInterfaces);
    connect(m_interfaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HttpServerManagerDialog::onInterfaceChanged);
    connect(m_addressCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HttpServerManagerDialog::updateUrls);
    connect(m_portSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &HttpServerManagerDialog::updateUrls);
    connect(m_bindAllCheckBox, &QCheckBox::toggled,
            this, &HttpServerManagerDialog::updateUrls);
    connect(saveButton, &QPushButton::clicked,
            this, &HttpServerManagerDialog::saveConfiguration);
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

void HttpServerManagerDialog::refreshNetworkInterfaces()
{
    const bool firstLoad = m_interfaceCombo->count() == 0;
    const QString preferredInterface = firstLoad
        ? m_mainWindow->m_softwareconfig->httpServerInterfaceName()
        : m_interfaceCombo->currentData().toString();
    const QString preferredAddress = firstLoad
        ? m_mainWindow->m_softwareconfig->httpServerSelectedAddress()
        : selectedAddress();

    const QSignalBlocker blocker(m_interfaceCombo);
    m_interfaceCombo->clear();

    QList<QNetworkInterface> loopbackInterfaces;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        const auto flags = networkInterface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)) {
            continue;
        }

        bool hasIpv4Address = false;
        for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isNull()) {
                hasIpv4Address = true;
                break;
            }
        }
        if (!hasIpv4Address) {
            continue;
        }

        if (flags & QNetworkInterface::IsLoopBack) {
            loopbackInterfaces.append(networkInterface);
            continue;
        }

        const QString displayName = networkInterface.humanReadableName().isEmpty()
            ? networkInterface.name()
            : networkInterface.humanReadableName();
        m_interfaceCombo->addItem(
            QStringLiteral("%1 (%2)").arg(displayName, networkInterface.name()),
            networkInterface.name());
    }

    for (const QNetworkInterface &networkInterface : loopbackInterfaces) {
        const QString displayName = networkInterface.humanReadableName().isEmpty()
            ? tr("Loopback")
            : networkInterface.humanReadableName();
        m_interfaceCombo->addItem(
            QStringLiteral("%1 (%2)").arg(displayName, networkInterface.name()),
            networkInterface.name());
    }

    if (m_interfaceCombo->count() == 0) {
        m_interfaceCombo->addItem(tr("Loopback (local only)"), QStringLiteral("__loopback__"));
    }

    int interfaceIndex = m_interfaceCombo->findData(preferredInterface);
    if (interfaceIndex < 0 && m_interfaceCombo->count() > 0) {
        interfaceIndex = 0;
    }
    m_interfaceCombo->setCurrentIndex(interfaceIndex);
    populateAddresses(preferredAddress);

    const bool hasInterface = m_interfaceCombo->count() > 0;
    m_interfaceCombo->setEnabled(hasInterface);
    m_addressCombo->setEnabled(hasInterface);
    updateUrls();

    if (!hasInterface) {
        showFeedback(tr("No active IPv4 network interface was found."), true);
    }
}

void HttpServerManagerDialog::onInterfaceChanged()
{
    populateAddresses();
    updateUrls();
}

void HttpServerManagerDialog::populateAddresses(const QString &preferredAddress)
{
    const QSignalBlocker blocker(m_addressCombo);
    m_addressCombo->clear();

    const QString interfaceName = m_interfaceCombo->currentData().toString();
    if (interfaceName == QStringLiteral("__loopback__")) {
        m_addressCombo->addItem(QHostAddress(QHostAddress::LocalHost).toString());
        m_addressCombo->setCurrentIndex(0);
        return;
    }

    const QNetworkInterface networkInterface = QNetworkInterface::interfaceFromName(interfaceName);
    for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
        const QHostAddress address = entry.ip();
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isNull()) {
            continue;
        }
        const QString addressText = address.toString();
        if (m_addressCombo->findText(addressText) < 0) {
            m_addressCombo->addItem(addressText);
        }
    }

    int addressIndex = m_addressCombo->findText(preferredAddress);
    if (addressIndex < 0 && m_addressCombo->count() > 0) {
        addressIndex = 0;
    }
    m_addressCombo->setCurrentIndex(addressIndex);
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

void HttpServerManagerDialog::saveConfiguration()
{
    if (!validateConfiguration()) {
        return;
    }

    SoftwareConfig *config = m_mainWindow->m_softwareconfig;
    config->setHttpServerInterfaceName(m_interfaceCombo->currentData().toString());
    config->setHttpServerSelectedAddress(selectedAddress());
    config->setHttpServerPort(static_cast<quint16>(m_portSpinBox->value()));
    config->setHttpServerBindAllInterfaces(m_bindAllCheckBox->isChecked());
    config->Write_config();

    showFeedback(tr("HTTP server configuration saved."));
    LOG_INFO(QString("HTTP server configuration saved: %1:%2")
                 .arg(effectiveBindAddress())
                 .arg(m_portSpinBox->value()));
}

void HttpServerManagerDialog::requestStart()
{
    if (!validateConfiguration()) {
        return;
    }
    showFeedback(tr("Start request sent. Waiting for the server response."));
    emit startServerRequested(effectiveBindAddress(),
                              static_cast<quint16>(m_portSpinBox->value()));
}

void HttpServerManagerDialog::requestStop()
{
    showFeedback(tr("Stop request sent. Waiting for the server response."));
    emit stopServerRequested();
}

void HttpServerManagerDialog::requestRestart()
{
    if (!validateConfiguration()) {
        return;
    }
    showFeedback(tr("Restart request sent. Waiting for the server response."));
    emit restartServerRequested(effectiveBindAddress(),
                                static_cast<quint16>(m_portSpinBox->value()));
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

QString HttpServerManagerDialog::selectedAddress() const
{
    return m_addressCombo->currentText().trimmed();
}

QString HttpServerManagerDialog::effectiveBindAddress() const
{
    return m_bindAllCheckBox->isChecked()
        ? QHostAddress(QHostAddress::AnyIPv4).toString()
        : selectedAddress();
}

QUrl HttpServerManagerDialog::localUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_portSpinBox->value()));
}

QUrl HttpServerManagerDialog::lanUrl() const
{
    const QString address = selectedAddress();
    if (address.isEmpty()) {
        return QUrl();
    }
    return QUrl(QStringLiteral("http://%1:%2").arg(address).arg(m_portSpinBox->value()));
}

bool HttpServerManagerDialog::validateConfiguration()
{
    if (!m_bindAllCheckBox->isChecked() && selectedAddress().isEmpty()) {
        showFeedback(tr("Select a valid IPv4 address before starting the server."), true);
        return false;
    }
    return true;
}
