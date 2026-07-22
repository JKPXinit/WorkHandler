#include "uiAdminoptions.h"
#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAbstractSocket>
#include <QRadioButton>
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QStackedWidget>
#include <QDesktopServices>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QMessageBox>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableWidget>
#include <QHeaderView>
#include <QSpinBox>

#include "softwareconfig.h"
#include "languagemanager.h"
#include "exitmodedialog.h"
#include "myLogger.h"
#include "uiManager.h"
#include "thememanager.h"
#include "shortcutmanager.h"
#include "httpserver.h"

namespace {

class ResetButtonDelegate : public QStyledItemDelegate
{
public:
    explicit ResetButtonDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);

        const QWidget *widget = itemOption.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &itemOption, painter, widget);

        QRect buttonRect(0, 0, 72, 30);
        buttonRect.moveCenter(option.rect.center());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor("#76b900"), 2));
        painter->setBrush(QColor("#ffffff"));
        painter->drawRect(buttonRect.adjusted(1, 1, -2, -2));
        painter->setPen(QColor("#000000"));
        QFont font = option.font;
        font.setBold(true);
        painter->setFont(font);
        painter->drawText(buttonRect.adjusted(2, 0, -2, 0), Qt::AlignCenter, index.data().toString());
        painter->restore();
    }
};

} // namespace

ui_AdminOptions::ui_AdminOptions(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

QWidget * ui_AdminOptions::createGeneralPage() {

    // 创建堆叠窗口的General页面
    QWidget *generalPage = new QWidget();
    QVBoxLayout *generalLayout = new QVBoxLayout(generalPage);
    QLabel *generallabel = new QLabel(tr("General Preferences"), generalPage);
    generallabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    generalLayout->addWidget(generallabel);
    generalPage->setLayout(generalLayout);

    return generalPage;
}

QWidget * ui_AdminOptions::createBasicPage() {

    // 创建堆叠窗口的 Basic 页面
    QWidget *basicPage = new QWidget();
    QVBoxLayout *basicLayout = new QVBoxLayout(basicPage);
    QLabel *basiclabel = new QLabel(tr("General basic Preferences"), basicPage);
    basiclabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    basicLayout->addWidget(basiclabel);
    basicPage->setLayout(basicLayout);

    return basicPage;
}

QWidget * ui_AdminOptions::createLanguagePage() {

    // 创建堆叠窗口的 Language 页面
    QWidget *languagePage = new QWidget();
    QVBoxLayout *languagePagelayout = new QVBoxLayout();

    QComboBox *languageAdminCombox = new QComboBox();
    for (int i = 0 ;i < ENUM_MAX(languageType) ;i++) {
        languageAdminCombox->addItem(languageList[i]);
    }
    languageAdminCombox->setCurrentIndex(m_mainWindow->m_softwareconfig->language());

    QGroupBox *languageAdminGroupbox = new QGroupBox(tr("Select language"));
    QVBoxLayout *languageSelectlayout = new QVBoxLayout();
    languageSelectlayout->addWidget(languageAdminCombox);
    languageAdminGroupbox->setLayout(languageSelectlayout);
    languagePagelayout->addWidget(languageAdminGroupbox);

    QSpacerItem *spacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    languagePagelayout->addItem(spacer);

    languagePage->setLayout(languagePagelayout);

    connect(languageAdminCombox, QOverload<int>::of(&QComboBox::currentIndexChanged), m_mainWindow->m_UI, &uiManager::languageComBox_currenIndexChanged);

    return languagePage;
}

QWidget * ui_AdminOptions::createThemePage() {

    // 创建堆叠窗口的 Theme 页面
    QWidget *themePage = new QWidget();
    QVBoxLayout *themePagelayout = new QVBoxLayout();

    QComboBox *themeAdminCombox = new QComboBox();
    for (int i = 0 ;i < ENUM_MAX(themeType) ;i++) {
        themeAdminCombox->addItem(themeList[i]);
    }
    themeAdminCombox->setCurrentIndex(m_mainWindow->m_softwareconfig->theme());

    QGroupBox *themeAdminGroupbox = new QGroupBox(tr("Select theme"));
    QVBoxLayout *themeSelectlayout = new QVBoxLayout();
    themeSelectlayout->addWidget(themeAdminCombox);
    themeAdminGroupbox->setLayout(themeSelectlayout);
    themePagelayout->addWidget(themeAdminGroupbox);

    QSpacerItem *spacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    themePagelayout->addItem(spacer);

    themePage->setLayout(themePagelayout);

    connect(themeAdminCombox, QOverload<int>::of(&QComboBox::currentIndexChanged), m_mainWindow->m_UI, &uiManager::themeComboBox_currenIndexChanged);


    return themePage;
}

QWidget *ui_AdminOptions::createAccountPage()
{
    QWidget *accountPage = new QWidget();
    QVBoxLayout *accountLayout = new QVBoxLayout(accountPage);

    QGroupBox *registrationGroup = new QGroupBox(tr("Add user"), accountPage);
    QHBoxLayout *registrationLayout = new QHBoxLayout(registrationGroup);
    QLabel *usernameLabel = new QLabel(tr("Username:"), registrationGroup);
    QLineEdit *usernameEdit = new QLineEdit(registrationGroup);
    usernameEdit->setObjectName(QStringLiteral("newAccountUsername"));
    usernameEdit->setMaxLength(64);
    usernameEdit->setClearButtonEnabled(true);
    QPushButton *addUserButton = new QPushButton(tr("Add User"), registrationGroup);
    addUserButton->setIcon(m_mainWindow->m_UI->AccountIcon);
    registrationLayout->addWidget(usernameLabel);
    registrationLayout->addWidget(usernameEdit, 1);
    registrationLayout->addWidget(addUserButton);
    accountLayout->addWidget(registrationGroup);

    QHBoxLayout *tableHeaderLayout = new QHBoxLayout();
    QLabel *tableLabel = new QLabel(tr("Registered accounts"), accountPage);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), accountPage);
    refreshButton->setIcon(
        accountPage->style()->standardIcon(QStyle::SP_BrowserReload));
    tableHeaderLayout->addWidget(tableLabel);
    tableHeaderLayout->addStretch();
    tableHeaderLayout->addWidget(refreshButton);
    accountLayout->addLayout(tableHeaderLayout);

    QTableWidget *table = new QTableWidget(accountPage);
    table->setObjectName(QStringLiteral("accountsTable"));
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({
        tr("ID"),
        tr("Username"),
        tr("Display name"),
        tr("Role"),
        tr("Password"),
        tr("Registered at")
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    accountLayout->addWidget(table, 1);

    QLabel *statusLabel = new QLabel(accountPage);
    statusLabel->setWordWrap(true);
    accountLayout->addWidget(statusLabel);

    QGroupBox *passwordGroup = new QGroupBox(tr("Admin password"), accountPage);
    QVBoxLayout *passwordLayout = new QVBoxLayout(passwordGroup);
    QFormLayout *passwordForm = new QFormLayout();
    QLineEdit *currentPasswordEdit = new QLineEdit(passwordGroup);
    QLineEdit *newPasswordEdit = new QLineEdit(passwordGroup);
    QLineEdit *confirmPasswordEdit = new QLineEdit(passwordGroup);
    currentPasswordEdit->setObjectName(QStringLiteral("adminCurrentPassword"));
    newPasswordEdit->setObjectName(QStringLiteral("adminNewPassword"));
    confirmPasswordEdit->setObjectName(QStringLiteral("adminConfirmPassword"));
    currentPasswordEdit->setEchoMode(QLineEdit::Password);
    newPasswordEdit->setEchoMode(QLineEdit::Password);
    confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    currentPasswordEdit->setMaxLength(256);
    newPasswordEdit->setMaxLength(256);
    confirmPasswordEdit->setMaxLength(256);
    passwordForm->addRow(tr("Current password:"), currentPasswordEdit);
    passwordForm->addRow(tr("New password:"), newPasswordEdit);
    passwordForm->addRow(tr("Confirm password:"), confirmPasswordEdit);
    passwordLayout->addLayout(passwordForm);

    QHBoxLayout *passwordButtonLayout = new QHBoxLayout();
    QPushButton *changePasswordButton = new QPushButton(
        tr("Change Password"), passwordGroup);
    passwordButtonLayout->addStretch();
    passwordButtonLayout->addWidget(changePasswordButton);
    passwordLayout->addLayout(passwordButtonLayout);
    passwordGroup->setEnabled(false);
    accountLayout->addWidget(passwordGroup);

    const auto reloadAccounts = [this, table, statusLabel, passwordGroup]() {
        table->setRowCount(0);
        passwordGroup->setEnabled(false);

        if (!m_mainWindow->m_httpServer) {
            statusLabel->setText(tr("The account database is unavailable."));
            return;
        }

        QString errorMessage;
        const QList<UserSummary> accounts =
            m_mainWindow->m_httpServer->accountSummaries(&errorMessage);
        if (!errorMessage.isEmpty()) {
            statusLabel->setText(errorMessage);
            return;
        }

        bool adminFound = false;
        table->setRowCount(accounts.size());
        for (int row = 0; row < accounts.size(); ++row) {
            const UserSummary &account = accounts.at(row);
            QTableWidgetItem *idItem = new QTableWidgetItem();
            idItem->setData(Qt::DisplayRole, account.id);
            table->setItem(row, 0, idItem);
            table->setItem(row, 1, new QTableWidgetItem(account.username));
            table->setItem(row, 2, new QTableWidgetItem(account.displayName));
            table->setItem(row, 3, new QTableWidgetItem(account.role));
            const QString passwordStatus =
                account.username == QStringLiteral("admin")
                    ? tr("Protected")
                    : (account.usesDefaultPassword
                           ? tr("123456 (default)")
                           : tr("Changed"));
            table->setItem(row, 4, new QTableWidgetItem(passwordStatus));
            table->setItem(row, 5, new QTableWidgetItem(account.createdAt));
            adminFound = adminFound
                || account.username == QStringLiteral("admin");
        }

        passwordGroup->setEnabled(adminFound);
        statusLabel->setText(adminFound
            ? tr("%1 account(s)").arg(accounts.size())
            : tr("%1 account(s). The admin account was not found.")
                  .arg(accounts.size()));
    };

    connect(refreshButton, &QPushButton::clicked,
            accountPage, reloadAccounts);
    connect(addUserButton, &QPushButton::clicked,
            accountPage, [this, accountPage, usernameEdit]() {
        const QString username = usernameEdit->text().trimmed();
        if (username.isEmpty()) {
            QMessageBox::warning(accountPage,
                                 tr("Add user"),
                                 tr("Enter a username."));
            return;
        }

        QString errorMessage;
        UserSummary createdUser;
        if (!m_mainWindow->m_httpServer
            || !m_mainWindow->m_httpServer->createManagedUser(
                username, &createdUser, &errorMessage)) {
            QMessageBox::warning(
                accountPage,
                tr("Add user"),
                errorMessage.isEmpty()
                    ? tr("The account database is unavailable.")
                    : errorMessage);
            return;
        }

        usernameEdit->clear();
        QMessageBox::information(
            accountPage,
            tr("Add user"),
            tr("User %1 was created with the initial password 123456.")
                .arg(createdUser.username));
    });
    connect(usernameEdit, &QLineEdit::returnPressed,
            addUserButton, &QPushButton::click);
    if (m_mainWindow->m_httpServer) {
        connect(m_mainWindow->m_httpServer, &HttpServer::accountsChanged,
                accountPage, reloadAccounts);
    }
    connect(changePasswordButton, &QPushButton::clicked,
            accountPage,
            [this, accountPage, currentPasswordEdit, newPasswordEdit,
             confirmPasswordEdit]() {
        const QString currentPassword = currentPasswordEdit->text();
        const QString newPassword = newPasswordEdit->text();
        const QString confirmPassword = confirmPasswordEdit->text();
        if (currentPassword.isEmpty() || newPassword.isEmpty()
            || confirmPassword.isEmpty()) {
            QMessageBox::warning(accountPage,
                                 tr("Admin password"),
                                 tr("Complete all password fields."));
            return;
        }
        if (newPassword != confirmPassword) {
            QMessageBox::warning(accountPage,
                                 tr("Admin password"),
                                 tr("The new passwords do not match."));
            return;
        }
        if (newPassword.size() < 8 || newPassword.size() > 256) {
            QMessageBox::warning(
                accountPage,
                tr("Admin password"),
                tr("The new password must contain 8 to 256 characters."));
            return;
        }

        QString errorMessage;
        if (!m_mainWindow->m_httpServer
            || !m_mainWindow->m_httpServer->changeAdminPassword(
                currentPassword, newPassword, &errorMessage)) {
            QMessageBox::warning(
                accountPage,
                tr("Admin password"),
                errorMessage.isEmpty()
                    ? tr("The account database is unavailable.")
                    : errorMessage);
            return;
        }

        currentPasswordEdit->clear();
        newPasswordEdit->clear();
        confirmPasswordEdit->clear();
        QMessageBox::information(
            accountPage,
            tr("Admin password"),
            tr("The admin password was updated. All accounts must sign in again."));
    });

    reloadAccounts();
    return accountPage;
}

QWidget *ui_AdminOptions::createHttpPage(
    std::function<bool()> *applyConfiguration)
{
    QWidget *httpPage = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(httpPage);

    QGroupBox *networkGroup = new QGroupBox(tr("Network configuration"), httpPage);
    QFormLayout *networkForm = new QFormLayout(networkGroup);
    QComboBox *interfaceCombo = new QComboBox(networkGroup);
    interfaceCombo->setObjectName(QStringLiteral("httpInterfaceCombo"));
    QComboBox *addressCombo = new QComboBox(networkGroup);
    addressCombo->setObjectName(QStringLiteral("httpAddressCombo"));
    QSpinBox *portSpin = new QSpinBox(networkGroup);
    portSpin->setRange(1, 65535);
    QCheckBox *bindAllCheck = new QCheckBox(
        tr("Bind all interfaces (0.0.0.0)"), networkGroup);
    networkForm->addRow(tr("NIC:"), interfaceCombo);
    networkForm->addRow(tr("IP address:"), addressCombo);
    networkForm->addRow(tr("Port:"), portSpin);
    networkForm->addRow(QString(), bindAllCheck);
    layout->addWidget(networkGroup);

    QGroupBox *runtimeGroup = new QGroupBox(tr("Runtime configuration"), httpPage);
    QFormLayout *runtimeForm = new QFormLayout(runtimeGroup);
    QCheckBox *autoStartCheck = new QCheckBox(
        tr("Start with the application"), runtimeGroup);
    QCheckBox *keepOriginalCheck = new QCheckBox(
        tr("Keep original uploaded images"), runtimeGroup);
    QSpinBox *maxWidthSpin = new QSpinBox(runtimeGroup);
    maxWidthSpin->setRange(320, 16384);
    runtimeForm->addRow(QString(), autoStartCheck);
    runtimeForm->addRow(QString(), keepOriginalCheck);
    runtimeForm->addRow(tr("Maximum image width:"), maxWidthSpin);
    layout->addWidget(runtimeGroup);

    QLabel *statusLabel = new QLabel(httpPage);
    statusLabel->setWordWrap(true);
    statusLabel->hide();
    layout->addWidget(statusLabel);

    layout->addStretch();

    const auto populateAddresses = [interfaceCombo, addressCombo](
        const QString &preferredAddress) {
        QSignalBlocker blocker(addressCombo);
        addressCombo->clear();
        const QString interfaceName = interfaceCombo->currentData().toString();
        const QNetworkInterface networkInterface =
            QNetworkInterface::interfaceFromName(interfaceName);
        for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                && !entry.ip().isNull()) {
                addressCombo->addItem(entry.ip().toString());
            }
        }
        const int preferredIndex = addressCombo->findText(preferredAddress);
        if (preferredIndex >= 0) {
            addressCombo->setCurrentIndex(preferredIndex);
        } else if (addressCombo->count() > 0) {
            addressCombo->setCurrentIndex(0);
        }
    };

    const auto reloadInterfaces = [this, interfaceCombo, addressCombo,
                                   portSpin, bindAllCheck, autoStartCheck,
                                   keepOriginalCheck, maxWidthSpin,
                                   populateAddresses]() {
        const SoftwareConfig *settings = m_mainWindow->m_softwareconfig;
        const QString preferredInterface = settings->httpServerInterfaceName();
        const QString preferredAddress = settings->httpServerSelectedAddress();
        interfaceCombo->clear();
        for (const QNetworkInterface &networkInterface :
             QNetworkInterface::allInterfaces()) {
            const auto flags = networkInterface.flags();
            if (!(flags & QNetworkInterface::IsUp)
                || !(flags & QNetworkInterface::IsRunning)) {
                continue;
            }
            bool hasIpv4 = false;
            for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                    && !entry.ip().isNull()) {
                    hasIpv4 = true;
                    break;
                }
            }
            if (!hasIpv4) {
                continue;
            }
            const QString name = networkInterface.humanReadableName().isEmpty()
                ? networkInterface.name()
                : networkInterface.humanReadableName();
            interfaceCombo->addItem(
                QStringLiteral("%1 (%2)").arg(name, networkInterface.name()),
                networkInterface.name());
        }
        int interfaceIndex = interfaceCombo->findData(preferredInterface);
        if (interfaceIndex < 0 && interfaceCombo->count() > 0) {
            interfaceIndex = 0;
        }
        interfaceCombo->setCurrentIndex(interfaceIndex);
        populateAddresses(preferredAddress);
        portSpin->setValue(settings->httpServerPort());
        bindAllCheck->setChecked(settings->httpServerBindAllInterfaces());
        autoStartCheck->setChecked(settings->httpServerAutoStart());
        keepOriginalCheck->setChecked(settings->httpServerKeepOriginal());
        maxWidthSpin->setValue(settings->httpServerMaxImageWidth());
    };

    connect(interfaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            httpPage, [populateAddresses](int) { populateAddresses(QString()); });
    connect(bindAllCheck, &QCheckBox::toggled, httpPage,
            [addressCombo](bool enabled) { addressCombo->setEnabled(!enabled); });

    if (applyConfiguration) {
        *applyConfiguration =
            [this, interfaceCombo, addressCombo, portSpin, bindAllCheck,
             autoStartCheck, keepOriginalCheck, maxWidthSpin,
             statusLabel]() -> bool {
                const QString serverInterface = bindAllCheck->isChecked()
                    ? QStringLiteral("0.0.0.0")
                    : addressCombo->currentText().trimmed();
                QHostAddress address;
                if (!address.setAddress(serverInterface)
                    || address.protocol() != QAbstractSocket::IPv4Protocol) {
                    statusLabel->setText(tr("Select a valid IPv4 address."));
                    statusLabel->show();
                    return false;
                }

                SoftwareConfig *settings = m_mainWindow->m_softwareconfig;
                const QString interfaceName =
                    interfaceCombo->currentData().toString();
                const QString selectedAddress =
                    addressCombo->currentText().trimmed();
                const quint16 port = static_cast<quint16>(portSpin->value());
                const bool bindAll = bindAllCheck->isChecked();
                const bool autoStart = autoStartCheck->isChecked();
                const bool keepOriginal = keepOriginalCheck->isChecked();
                const int maxImageWidth = maxWidthSpin->value();

                const bool changed =
                    interfaceName != settings->httpServerInterfaceName()
                    || selectedAddress != settings->httpServerSelectedAddress()
                    || port != settings->httpServerPort()
                    || bindAll != settings->httpServerBindAllInterfaces()
                    || autoStart != settings->httpServerAutoStart()
                    || keepOriginal != settings->httpServerKeepOriginal()
                    || maxImageWidth != settings->httpServerMaxImageWidth();
                if (!changed) {
                    return true;
                }
                if (!m_mainWindow->m_httpServer) {
                    statusLabel->setText(tr("HTTP server is unavailable."));
                    statusLabel->show();
                    return false;
                }

                const QString oldInterfaceName =
                    settings->httpServerInterfaceName();
                const QString oldSelectedAddress =
                    settings->httpServerSelectedAddress();
                const quint16 oldPort = settings->httpServerPort();
                const bool oldBindAll = settings->httpServerBindAllInterfaces();
                const bool oldAutoStart = settings->httpServerAutoStart();
                const bool oldKeepOriginal = settings->httpServerKeepOriginal();
                const int oldMaxImageWidth = settings->httpServerMaxImageWidth();

                settings->setHttpServerInterfaceName(interfaceName);
                settings->setHttpServerSelectedAddress(selectedAddress);
                settings->setHttpServerPort(port);
                settings->setHttpServerBindAllInterfaces(bindAll);
                settings->setHttpServerAutoStart(autoStart);
                settings->setHttpServerKeepOriginal(keepOriginal);
                settings->setHttpServerMaxImageWidth(maxImageWidth);

                const ServerConfig config = {
                    serverInterface,
                    port,
                    autoStart,
                    keepOriginal,
                    maxImageWidth
                };
                QString errorMessage;
                if (!m_mainWindow->m_httpServer->updateConfiguration(
                        config, &errorMessage)) {
                    settings->setHttpServerInterfaceName(oldInterfaceName);
                    settings->setHttpServerSelectedAddress(oldSelectedAddress);
                    settings->setHttpServerPort(oldPort);
                    settings->setHttpServerBindAllInterfaces(oldBindAll);
                    settings->setHttpServerAutoStart(oldAutoStart);
                    settings->setHttpServerKeepOriginal(oldKeepOriginal);
                    settings->setHttpServerMaxImageWidth(oldMaxImageWidth);
                    statusLabel->setText(errorMessage);
                    statusLabel->show();
                    return false;
                }
                m_mainWindow->m_httpServer->restartServer(
                    config.serverInterface, config.serverPort);
                return true;
            };
    }

    reloadInterfaces();
    addressCombo->setEnabled(!bindAllCheck->isChecked());
    return httpPage;
}

QDialog *ui_AdminOptions::setupOptionsDialog() {
    QDialog *optionsDialog = new QDialog(m_mainWindow);
    optionsDialog->setWindowTitle(tr("Preferences"));
    optionsDialog->setWindowIcon(m_mainWindow->m_UI->OptionsIcon);
    optionsDialog->resize(800, 600); // 调整对话框大小以适应新的布局

    // 创建一个垂直布局，用于包含上下两个 layout
    QVBoxLayout *mainDialogLayout = new QVBoxLayout();

    // 创建一个水平布局，用于包含左侧的树形结构和右侧的堆叠窗口
    QHBoxLayout *upLayout = new QHBoxLayout();

    // 创建左侧的垂直布局，用于包含搜索框和树形结构
    QVBoxLayout *leftDialogLayout = new QVBoxLayout();
    // 创建右侧的垂直布局
    QVBoxLayout *rightDialogLayout = new QVBoxLayout();
    upLayout->addLayout(leftDialogLayout);
    upLayout->addLayout(rightDialogLayout);

    // 创建搜索框
    QLineEdit *searchLineEdit = new QLineEdit(optionsDialog);
    searchLineEdit->setPlaceholderText(tr("Search..."));
    searchLineEdit->setMinimumWidth(150);
    searchLineEdit->setMaximumWidth(200);

    // 创建左侧的树形结构
    QTreeWidget *treeWidget = new QTreeWidget(optionsDialog);
    treeWidget->setHeaderHidden(true); // 隐藏表头
    treeWidget->setMinimumWidth(200);
    treeWidget->setMaximumWidth(200);
    treeWidget->setMinimumHeight(500);

    // 添加分组和配置项
    QTreeWidgetItem *generalGroup = new QTreeWidgetItem(treeWidget);
    generalGroup->setText(0, tr("General"));
    QTreeWidgetItem *basicGroup = new QTreeWidgetItem(generalGroup);
    basicGroup->setText(0, tr("Basic"));
    basicGroup->setIcon(0, m_mainWindow->m_UI->BasicIcon);
    QTreeWidgetItem *languageItem = new QTreeWidgetItem(basicGroup);
    languageItem->setText(0, tr("Language"));
    languageItem->setIcon(0, m_mainWindow->m_UI->LanguageIcon);
    QTreeWidgetItem *themeItem = new QTreeWidgetItem(basicGroup);
    themeItem->setText(0, tr("Theme"));
    themeItem->setIcon(0, m_mainWindow->m_UI->ThemeIcon);

    // System 节点
    QTreeWidgetItem *systemItem = new QTreeWidgetItem(generalGroup);
    systemItem->setText(0, tr("System"));
    systemItem->setIcon(0, m_mainWindow->m_UI->SystemIcon);

    QTreeWidgetItem *shortcutsItem = new QTreeWidgetItem(generalGroup);
    shortcutsItem->setText(0, tr("Shortcuts"));
    shortcutsItem->setIcon(0, m_mainWindow->m_UI->ShortcutIcon);

    QTreeWidgetItem *httpItem = new QTreeWidgetItem(treeWidget);
    httpItem->setText(0, tr("Http"));

    QTreeWidgetItem *accountItem = new QTreeWidgetItem(treeWidget);
    accountItem->setText(0, tr("Account"));
    accountItem->setIcon(0, m_mainWindow->m_UI->AccountIcon);

    // 将搜索框和树形结构添加到左侧布局中
    leftDialogLayout->addWidget(searchLineEdit);
    leftDialogLayout->addWidget(treeWidget);

    // 添加右侧的分支标签
    QLabel *proLabel = new QLabel(tr("General"), optionsDialog); // 初始值为 "General"
    proLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    proLabel->setStyleSheet("font-weight: bold; font-size: 16px;");
    rightDialogLayout->addWidget(proLabel);

    // 添加右侧的分割线
    QFrame *line = new QFrame(optionsDialog);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    rightDialogLayout->addWidget(line);

    // 创建右侧的堆叠窗口
    QStackedWidget *stackedWidget = new QStackedWidget(optionsDialog);
    rightDialogLayout->addWidget(stackedWidget);

    // 创建堆叠窗口
    QWidget *generalPage = createGeneralPage();     // General 页面
    QWidget *languagePage = createLanguagePage();   // Language 页面
    QWidget *themePage = createThemePage();         // Theme 页面
    QWidget *basicPage = createBasicPage();         // Basic 页面
    QWidget *systemPage    = createSystemPage();       // System 页面
    QWidget *shortcutsPage = createShortcutsPage();    // Shortcuts 页面
    QWidget *accountPage = createAccountPage();        // Account 页面
    std::function<bool()> applyHttpConfiguration;
    QWidget *httpPage = createHttpPage(&applyHttpConfiguration); // Http 页面


    // 将页面添加到堆叠窗口
    generalGroup->setData(0, Qt::UserRole,
                          stackedWidget->addWidget(generalPage));
    basicGroup->setData(0, Qt::UserRole,
                        stackedWidget->addWidget(basicPage));
    languageItem->setData(0, Qt::UserRole,
                          stackedWidget->addWidget(languagePage));
    themeItem->setData(0, Qt::UserRole,
                       stackedWidget->addWidget(themePage));
    systemItem->setData(0, Qt::UserRole,
                        stackedWidget->addWidget(systemPage));
    shortcutsItem->setData(0, Qt::UserRole,
                           stackedWidget->addWidget(shortcutsPage));
    accountItem->setData(0, Qt::UserRole,
                         stackedWidget->addWidget(accountPage));
    httpItem->setData(0, Qt::UserRole,
                      stackedWidget->addWidget(httpPage));


    // 连接树形结构的信号到堆叠窗口的槽
    connect(treeWidget, &QTreeWidget::itemSelectionChanged, this, [stackedWidget, treeWidget, proLabel]() {
        QTreeWidgetItem *currentItem = treeWidget->currentItem();
        if (currentItem) {
            bool indexOk = false;
            const int pageIndex = currentItem->data(0, Qt::UserRole).toInt(&indexOk);
            if (indexOk) {
                stackedWidget->setCurrentIndex(pageIndex);
            }

            proLabel->setText(currentItem->text(0));
        }
    });
    treeWidget->setCurrentItem(generalGroup);

    // 连接搜索框的 textChanged 信号到搜索函数
    connect(searchLineEdit, &QLineEdit::textChanged, this, [this, treeWidget](const QString &searchText) {
        this->searchTreeWidget(treeWidget, searchText);
    });

    // 创建一个水平布局，用于包含最下层的控件
    QHBoxLayout *downLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("Ok"));
    QPushButton *cannelButton = new QPushButton(tr("Cannel"));

    connect(okButton, &QPushButton::clicked, optionsDialog,
            [optionsDialog, treeWidget, httpItem,
             applyHttpConfiguration]() {
        if (applyHttpConfiguration && !applyHttpConfiguration()) {
            treeWidget->setCurrentItem(httpItem);
            return;
        }
        optionsDialog->accept();
    });

    connect(cannelButton, &QPushButton::clicked, optionsDialog, [optionsDialog] {
        optionsDialog->reject();
    });

    // 添加弹性空间，将按钮推到右侧
    downLayout->addStretch();
    downLayout->addWidget(okButton);
    downLayout->addWidget(cannelButton);

    // 设置对话框的主布局
    mainDialogLayout->addLayout(upLayout);
    mainDialogLayout->addStretch(); // 添加弹性空间，将按钮推到下方
    mainDialogLayout->addLayout(downLayout);

    optionsDialog->setLayout(mainDialogLayout);

    return optionsDialog;
}

void ui_AdminOptions::searchTreeWidget(QTreeWidget *treeWidget, const QString &searchText) {
    if (searchText.isEmpty()) {
        // 如果搜索框为空，清除所有项目的字体加粗并关闭展开
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = treeWidget->topLevelItem(i);
            clearBoldAndCollapse(item);
        }
        return;
    }

    // 遍历所有顶级项目
    for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = treeWidget->topLevelItem(i);
        searchInItem(item, searchText);
    }
}

void ui_AdminOptions::clearBoldAndCollapse(QTreeWidgetItem *item) {
    item->setHidden(false); // 确保项目可见
    item->setExpanded(false); // 关闭展开
    QFont itemFont = item->font(0); // 获取当前项目的字体
    itemFont.setBold(false); // 设置字体为非加粗
    item->setFont(0, itemFont);

    // 递归处理所有子项目
    for (int i = 0; i < item->childCount(); ++i) {
        clearBoldAndCollapse(item->child(i));
    }
}

void ui_AdminOptions::expandAllChildren(QTreeWidgetItem *item) {
    item->setHidden(false); // 显示当前项目
    item->setExpanded(true); // 展开当前项目
    for (int i = 0; i < item->childCount(); ++i) {
        expandAllChildren(item->child(i));
    }
}

bool ui_AdminOptions::searchInItem(QTreeWidgetItem *item, const QString &searchText) {
    QFont itemFont = item->font(0); // 获取当前项目的字体

    if (item->text(0).contains(searchText, Qt::CaseInsensitive)) {
        // 如果当前项目匹配搜索内容，展开其父级并显示
        QTreeWidgetItem *parent = item->parent();
        while (parent) {
            parent->setExpanded(true);
            parent->setHidden(false); // 确保父级可见
            parent = parent->parent();
        }
        item->setHidden(false); // 显示当前匹配的项目
        item->setExpanded(true); // 展开当前匹配的项目

        // 设置字体为粗体
        itemFont.setBold(true);
        item->setFont(0, itemFont);

        return true; // 返回匹配成功
    } else {
        // 如果当前项目不匹配，递归检查子项目
        bool hasVisibleChild = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (searchInItem(item->child(i), searchText)) {
                hasVisibleChild = true;
            }
        }
        // 如果当前项目没有匹配的子项目，则隐藏当前项目
        if (!hasVisibleChild) {
            item->setHidden(true);

            // 重置字体为默认
            itemFont.setBold(false);
            item->setFont(0, itemFont);
        }
        return hasVisibleChild; // 返回是否有可见的子项目
    }
}

QWidget * ui_AdminOptions::createSystemPage()
{
    QWidget *systemPage = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(systemPage);

    // 退出方式 Group
    QGroupBox *grp = new QGroupBox(tr("Exit mode"));
    QVBoxLayout *gLay = new QVBoxLayout(grp);

    QRadioButton *radioExit = new QRadioButton(tr("Exit directly"));
    QRadioButton *radioTray = new QRadioButton(tr("Minimize to system tray"));

    // 读取当前配置
    bool curTray = (m_mainWindow->m_softwareconfig->exitMode() == systray);
    (curTray ? radioTray : radioExit)->setChecked(true);

    gLay->addWidget(radioExit);
    gLay->addWidget(radioTray);

    // 连接信号：立即保存
    auto saveMode = [=]() {
        softwareExitMode mode = radioExit->isChecked() ? exitForce : systray;
        m_mainWindow->m_softwareconfig->setExitMode(mode);
        m_mainWindow->m_softwareconfig->Write_config();
    };
    connect(radioExit, &QRadioButton::clicked, this, saveMode);
    connect(radioTray, &QRadioButton::clicked, this, saveMode);

    lay->addWidget(grp);

    // 日志设置 Group
    QGroupBox *logGroup = new QGroupBox(tr("Logging settings"));
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

    // 日志输出模式 RadioButton
    QRadioButton *logConsoleRadio = new QRadioButton(tr("Output to console"));
    QRadioButton *logFileRadio = new QRadioButton(tr("Output to file"));

    // 读取当前配置
    bool isFileMode = (m_mainWindow->m_softwareconfig->logOutputMode() == LogOutputFile);
    (isFileMode ? logFileRadio : logConsoleRadio)->setChecked(true);

    logLayout->addWidget(logConsoleRadio);
    logLayout->addWidget(logFileRadio);

    // 日志文件路径设置
    QLabel *logPathLabel = new QLabel(tr("Log file path (empty for default):"));
    QLineEdit *logPathLineEdit = new QLineEdit();
    logPathLineEdit->setText(m_mainWindow->m_softwareconfig->logFilePath());
    logPathLineEdit->setPlaceholderText(myLogger::getDefaultLogFilePath());
    logPathLineEdit->setEnabled(isFileMode);  // 只有文件模式时启用

    QPushButton *browseButton = new QPushButton(tr("Browse..."));
    browseButton->setEnabled(isFileMode);

    QPushButton *openLogFolderButton = new QPushButton(tr("Open log folder"));
    openLogFolderButton->setEnabled(isFileMode);  // 只有文件模式时启用

    // 日志路径布局
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(logPathLineEdit);
    pathLayout->addWidget(browseButton);

    // 添加到日志布局
    logLayout->addWidget(logPathLabel);
    logLayout->addLayout(pathLayout);
    logLayout->addWidget(openLogFolderButton);

    // 控制台模式切换
    connect(logConsoleRadio, &QRadioButton::toggled, this, [=](bool checked) {
        if (checked) {
            // 切换到控制台模式
            logPathLineEdit->setEnabled(false);
            browseButton->setEnabled(false);
            openLogFolderButton->setEnabled(false);

            // 保存配置并更新日志系统
            m_mainWindow->m_softwareconfig->setLogOutputMode(LogOutputConsole);
            m_mainWindow->m_softwareconfig->Write_config();

            LogConfig config = myLogger::instance()->getConfig();
            config.enableFileLogging = false;
            config.enableConsoleLogging = true;
            myLogger::instance()->setConfig(config);

            LOG_INFO("Log output mode changed to Console");
        }
    });

    // 文件模式切换
    connect(logFileRadio, &QRadioButton::toggled, this, [=](bool checked) {
        if (checked) {
            // 切换到文件模式
            logPathLineEdit->setEnabled(true);
            browseButton->setEnabled(true);
            openLogFolderButton->setEnabled(true);

            // 保存配置并更新日志系统
            m_mainWindow->m_softwareconfig->setLogOutputMode(LogOutputFile);
            m_mainWindow->m_softwareconfig->Write_config();

            LogConfig config = myLogger::instance()->getConfig();
            config.enableFileLogging = true;
            config.enableConsoleLogging = false;
            myLogger::instance()->setConfig(config);

            LOG_INFO("Log output mode changed to File");
        }
    });

    // 浏览按钮点击事件
    connect(browseButton, &QPushButton::clicked, this, [=]() {
        QString defaultPath = m_mainWindow->m_softwareconfig->logFilePath();
        if (defaultPath.isEmpty()) {
            defaultPath = myLogger::getDefaultLogFilePath();
        }

        QString filePath = QFileDialog::getSaveFileName(
            m_mainWindow,
            tr("Select log file path"),
            defaultPath,
            tr("Log Files (*.log);;All Files (*.*)")
        );

        if (!filePath.isEmpty()) {
            logPathLineEdit->setText(filePath);

            // 保存配置并更新日志系统
            m_mainWindow->m_softwareconfig->setLogFilePath(filePath);
            m_mainWindow->m_softwareconfig->Write_config();

            LogConfig config = myLogger::instance()->getConfig();
            config.logFilePath = filePath;
            myLogger::instance()->setConfig(config);

            LOG_INFO(QString("Log file path changed to: %1").arg(filePath));
        }
    });

    // 日志路径输入框变化时保存
    connect(logPathLineEdit, &QLineEdit::editingFinished, this, [=]() {
        QString newPath = logPathLineEdit->text().trimmed();

        // 保存配置
        m_mainWindow->m_softwareconfig->setLogFilePath(newPath);
        m_mainWindow->m_softwareconfig->Write_config();

        // 更新日志系统
        LogConfig config = myLogger::instance()->getConfig();
        config.logFilePath = newPath;
        myLogger::instance()->setConfig(config);

        LOG_INFO(QString("Log file path changed to: %1").arg(newPath.isEmpty() ? "default" : newPath));
    });

    // 打开日志文件夹按钮
    connect(openLogFolderButton, &QPushButton::clicked, this, [=]() {
        QString logPath = m_mainWindow->m_softwareconfig->logFilePath();
        if (logPath.isEmpty()) {
            logPath = myLogger::getDefaultLogFilePath();
        }

        QFileInfo fileInfo(logPath);
        QString logDir = fileInfo.absolutePath();

        // 使用系统默认文件管理器打开
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    });

    // 日志轮转设置
    QFrame *rotationSep = new QFrame(logGroup);
    rotationSep->setFrameShape(QFrame::HLine);
    rotationSep->setFrameShadow(QFrame::Sunken);
    logLayout->addWidget(rotationSep);

    QCheckBox *rotationCheckBox = new QCheckBox(tr("Enable log rotation"), logGroup);
    bool rotationEnabled = m_mainWindow->m_softwareconfig->logRotationEnabled();
    rotationCheckBox->setChecked(rotationEnabled);
    rotationCheckBox->setEnabled(isFileMode);
    logLayout->addWidget(rotationCheckBox);

    // 按大小轮转行
    QHBoxLayout *sizeLayout = new QHBoxLayout();
    QRadioButton *rotBySizeRadio = new QRadioButton(tr("By file size"), logGroup);
    QSpinBox *maxSizeSpinBox = new QSpinBox(logGroup);
    maxSizeSpinBox->setRange(1, 500);
    maxSizeSpinBox->setSuffix(" MB");
    maxSizeSpinBox->setValue(m_mainWindow->m_softwareconfig->logRotationMaxSizeMB());
    sizeLayout->addWidget(rotBySizeRadio);
    sizeLayout->addWidget(maxSizeSpinBox);
    sizeLayout->addStretch();

    // 按时间轮转行
    QHBoxLayout *timeLayout = new QHBoxLayout();
    QRadioButton *rotByTimeRadio = new QRadioButton(tr("By time"), logGroup);
    QSpinBox *intervalSpinBox = new QSpinBox(logGroup);
    intervalSpinBox->setRange(1, 365);
    intervalSpinBox->setSuffix(tr(" days"));
    intervalSpinBox->setValue(m_mainWindow->m_softwareconfig->logRotationIntervalDays());
    timeLayout->addWidget(rotByTimeRadio);
    timeLayout->addWidget(intervalSpinBox);
    timeLayout->addStretch();

    // 最大备份数行
    QHBoxLayout *backupLayout = new QHBoxLayout();
    QLabel *backupLabel = new QLabel(tr("Max backup files:"), logGroup);
    QSpinBox *maxBackupSpinBox = new QSpinBox(logGroup);
    maxBackupSpinBox->setRange(1, 50);
    maxBackupSpinBox->setValue(m_mainWindow->m_softwareconfig->logRotationMaxBackups());
    backupLayout->addWidget(backupLabel);
    backupLayout->addWidget(maxBackupSpinBox);
    backupLayout->addStretch();

    logLayout->addLayout(sizeLayout);
    logLayout->addLayout(timeLayout);
    logLayout->addLayout(backupLayout);

    // 设置轮转模式单选按钮初始状态
    int rotMode = m_mainWindow->m_softwareconfig->logRotationMode();
    (rotMode == 0 ? rotBySizeRadio : rotByTimeRadio)->setChecked(true);

    // 轮转子控件的整体 enabled 状态
    auto setRotationSubEnabled = [=](bool en) {
        rotBySizeRadio->setEnabled(en);
        maxSizeSpinBox->setEnabled(en && rotBySizeRadio->isChecked());
        rotByTimeRadio->setEnabled(en);
        intervalSpinBox->setEnabled(en && rotByTimeRadio->isChecked());
        backupLabel->setEnabled(en);
        maxBackupSpinBox->setEnabled(en);
    };
    setRotationSubEnabled(isFileMode && rotationEnabled);

    // 勾选框切换
    connect(rotationCheckBox, &QCheckBox::toggled, this, [=](bool checked) {
        setRotationSubEnabled(checked);
        m_mainWindow->m_softwareconfig->setLogRotationEnabled(checked);
        m_mainWindow->m_softwareconfig->Write_config();
        LogConfig cfg = myLogger::instance()->getConfig();
        cfg.enableRotation = checked;
        myLogger::instance()->setConfig(cfg);
    });

    // 按大小单选
    connect(rotBySizeRadio, &QRadioButton::toggled, this, [=](bool checked) {
        maxSizeSpinBox->setEnabled(checked);
        intervalSpinBox->setEnabled(!checked);
        if (checked) {
            m_mainWindow->m_softwareconfig->setLogRotationMode(0);
            m_mainWindow->m_softwareconfig->Write_config();
            LogConfig cfg = myLogger::instance()->getConfig();
            cfg.rotationMode = 0;
            myLogger::instance()->setConfig(cfg);
        }
    });

    // 按时间单选
    connect(rotByTimeRadio, &QRadioButton::toggled, this, [=](bool checked) {
        intervalSpinBox->setEnabled(checked);
        maxSizeSpinBox->setEnabled(!checked);
        if (checked) {
            m_mainWindow->m_softwareconfig->setLogRotationMode(1);
            m_mainWindow->m_softwareconfig->Write_config();
            LogConfig cfg = myLogger::instance()->getConfig();
            cfg.rotationMode = 1;
            myLogger::instance()->setConfig(cfg);
        }
    });

    // 大小阈值变化
    connect(maxSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int val) {
        m_mainWindow->m_softwareconfig->setLogRotationMaxSizeMB(val);
        m_mainWindow->m_softwareconfig->Write_config();
        LogConfig cfg = myLogger::instance()->getConfig();
        cfg.maxFileSizeMB = val;
        myLogger::instance()->setConfig(cfg);
    });

    // 时间间隔变化
    connect(intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int val) {
        m_mainWindow->m_softwareconfig->setLogRotationIntervalDays(val);
        m_mainWindow->m_softwareconfig->Write_config();
        LogConfig cfg = myLogger::instance()->getConfig();
        cfg.rotationIntervalDays = val;
        myLogger::instance()->setConfig(cfg);
    });

    // 最大备份数变化
    connect(maxBackupSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int val) {
        m_mainWindow->m_softwareconfig->setLogRotationMaxBackups(val);
        m_mainWindow->m_softwareconfig->Write_config();
        LogConfig cfg = myLogger::instance()->getConfig();
        cfg.maxBackupCount = val;
        myLogger::instance()->setConfig(cfg);
    });

    // 控制台/文件模式切换时同步轮转控件 enabled
    connect(logConsoleRadio, &QRadioButton::toggled, this, [=](bool checked) {
        if (checked) rotationCheckBox->setEnabled(false);
        setRotationSubEnabled(false);
    });
    connect(logFileRadio, &QRadioButton::toggled, this, [=](bool checked) {
        if (checked) {
            rotationCheckBox->setEnabled(true);
            setRotationSubEnabled(rotationCheckBox->isChecked());
        }
    });

    lay->addWidget(logGroup);

    lay->addStretch();
    systemPage->setLayout(lay);
    return systemPage;
}

QWidget * ui_AdminOptions::createShortcutsPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(page);

    QList<ShortcutInfo> infos = m_mainWindow->m_shortcutManager->getAllShortcutInfo();

    // 按 id 排序，保证顺序稳定
    std::sort(infos.begin(), infos.end(), [](const ShortcutInfo &a, const ShortcutInfo &b){
        return a.id < b.id;
    });

    // 顶部搜索栏（右对齐）
    QHBoxLayout *topBar = new QHBoxLayout();
    topBar->addStretch();
    QLabel *searchLabel = new QLabel(tr("Search:"), page);
    QLineEdit *searchEdit = new QLineEdit(page);
    searchEdit->setPlaceholderText(tr("Filter shortcuts..."));
    searchEdit->setMaximumWidth(200);
    searchEdit->setClearButtonEnabled(true);
    topBar->addWidget(searchLabel);
    topBar->addWidget(searchEdit);
    lay->addLayout(topBar);

    QTableWidget *table = new QTableWidget(infos.size(), 3, page);
    table->setObjectName("shortcutsTable");
    table->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut"), tr("Reset")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->horizontalHeader()->resizeSection(2, 96);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(48);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked);
    table->setItemDelegateForColumn(1, new KeyCaptureDelegate(table));
    table->setItemDelegateForColumn(2, new ResetButtonDelegate(table));

    // 填充行
    for (int row = 0; row < infos.size(); ++row) {
        const ShortcutInfo &info = infos[row];

        // 列 0：名称（只读）
        QTableWidgetItem *nameItem = new QTableWidgetItem(info.name);
        nameItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 0, nameItem);

        // 列 1：当前快捷键（可编辑）
        QTableWidgetItem *keyItem = new QTableWidgetItem(
            info.currentKey.toString(QKeySequence::NativeText));
        keyItem->setTextAlignment(Qt::AlignCenter);
        keyItem->setData(Qt::UserRole, info.id);   // 存 id 供 itemChanged 使用
        table->setItem(row, 1, keyItem);

        // 列 2：Reset 操作，使用 item delegate 绘制，避免 setCellWidget 和选中区域错位
        QTableWidgetItem *resetItem = new QTableWidgetItem(tr("Reset"));
        resetItem->setTextAlignment(Qt::AlignCenter);
        resetItem->setData(Qt::UserRole, info.id);
        resetItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table->setItem(row, 2, resetItem);
    }

    // 搜索过滤 + 高亮
    connect(searchEdit, &QLineEdit::textChanged, this, [table](const QString &text) {
        const QBrush highlightBrush(QColor(255, 200, 0, 120));
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem *nameItem = table->item(row, 0);
            QTableWidgetItem *keyItem  = table->item(row, 1);
            if (!nameItem || !keyItem) continue;

            if (text.isEmpty()) {
                table->setRowHidden(row, false);
                nameItem->setBackground(QBrush());
            } else {
                bool nameMatch = nameItem->text().contains(text, Qt::CaseInsensitive);
                table->setRowHidden(row, !nameMatch);
                nameItem->setBackground(nameMatch ? highlightBrush : QBrush());
            }
        }
    });

    // itemChanged：验证并写入 ShortcutManager
    connect(table, &QTableWidget::itemChanged, this, [this, table](QTableWidgetItem *item) {
        if (item->column() != 1) return;

        ShortcutId id = static_cast<ShortcutId>(item->data(Qt::UserRole).toInt());
        QString text  = item->text().trimmed();

        // Esc 取消时 delegate 不写回，但以防万一处理空串
        if (text.isEmpty()) {
            QKeySequence cur = m_mainWindow->m_shortcutManager->getShortcut(id);
            table->blockSignals(true);
            item->setText(cur.toString(QKeySequence::NativeText));
            table->blockSignals(false);
            return;
        }

        QKeySequence newSeq(text);
        if (!m_mainWindow->m_shortcutManager->setShortcut(id, newSeq)) {
            // 冲突或无效，回滚
            QKeySequence cur = m_mainWindow->m_shortcutManager->getShortcut(id);
            table->blockSignals(true);
            item->setText(cur.toString(QKeySequence::NativeText));
            table->blockSignals(false);
            QMessageBox::warning(m_mainWindow, tr("Conflict"),
                tr("The key \"%1\" is already in use.").arg(text));
        } else {
            LOG_INFO(QString("Shortcut changed: id=%1, key=%2").arg(id).arg(text));
            m_mainWindow->statusBar()->showMessage(tr("Shortcut updated: %1").arg(text), 3000);
        }
    });

    auto resetShortcutAtRow = [this, table](int row) {
        QTableWidgetItem *resetItem = table->item(row, 2);
        QTableWidgetItem *keyItem = table->item(row, 1);
        if (!resetItem || !keyItem) {
            return;
        }

        ShortcutId id = static_cast<ShortcutId>(resetItem->data(Qt::UserRole).toInt());
        m_mainWindow->m_shortcutManager->resetShortcut(id);
        QKeySequence newKey = m_mainWindow->m_shortcutManager->getShortcut(id);

        table->blockSignals(true);
        keyItem->setText(newKey.toString(QKeySequence::NativeText));
        table->blockSignals(false);

        LOG_DEBUG(QString("Shortcut reset: id=%1").arg(id));
    };

    connect(table, &QTableWidget::cellClicked, this, [resetShortcutAtRow](int row, int column) {
        if (column == 2) {
            resetShortcutAtRow(row);
        }
    });

    connect(table, &QTableWidget::cellActivated, this, [resetShortcutAtRow](int row, int column) {
        if (column == 2) {
            resetShortcutAtRow(row);
        }
    });

    lay->addWidget(table);

    // Reset All 按钮
    QHBoxLayout *btnLay = new QHBoxLayout();
    btnLay->addStretch();
    QPushButton *resetAllBtn = new QPushButton(tr("Reset All"), page);
    connect(resetAllBtn, &QPushButton::clicked, this, [this, table, infos]() {
        m_mainWindow->m_shortcutManager->resetAllShortcuts();
        table->blockSignals(true);
        for (int row = 0; row < infos.size(); ++row) {
            QKeySequence key = m_mainWindow->m_shortcutManager->getShortcut(infos[row].id);
            table->item(row, 1)->setText(key.toString(QKeySequence::NativeText));
        }
        table->blockSignals(false);
        LOG_INFO("All shortcuts reset to default");
    });
    btnLay->addWidget(resetAllBtn);
    lay->addLayout(btnLay);

    page->setLayout(lay);
    return page;
}

