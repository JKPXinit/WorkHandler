#include "uiAdminoptions.h"
#include "mainwindow.h"

#include <QRadioButton>
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QStackedWidget>
#include <QDesktopServices>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QMessageBox>
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
#include "passwordverfy.h"
#include "thememanager.h"
#include "shortcutmanager.h"

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

QWidget * ui_AdminOptions::createAccountPage() {

    // 创建堆叠窗口的 Account 页面
    QWidget *accountPage = new QWidget();
    QVBoxLayout *accountLayout = new QVBoxLayout(accountPage);
    QLabel *accountlabel = new QLabel(tr("Account Preferences"), accountPage);
    accountlabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    accountLayout->addWidget(accountlabel);
    accountPage->setLayout(accountLayout);

    return accountPage;
}

QWidget * ui_AdminOptions::createPasswordPage() {
    // 创建堆叠窗口的 Password 页面
    QWidget *passwordPage = new QWidget();

    // 创建密码输入框
    QLineEdit *modifiedLineEdit = new QLineEdit(passwordPage);
    modifiedLineEdit->setEchoMode(QLineEdit::Password); // 设置密码模式
    QLineEdit *confirmLineEdit = new QLineEdit(passwordPage);
    confirmLineEdit->setEchoMode(QLineEdit::Password); // 设置密码模式

    // 创建密码标签
    QLabel *newpwLabel = new QLabel(tr("New Password :"));
    QLabel *confirmpwLabel = new QLabel(tr("Confirm New Password :"));

    // 使用 QFormLayout 来对齐标签和输入框
    QFormLayout *formLayout = new QFormLayout();
    QHBoxLayout *modifiedLayout = new QHBoxLayout();
    QHBoxLayout *confirmLayout = new QHBoxLayout();

    // 创建清除按钮
    QPushButton *clearModifiedButton = new QPushButton(passwordPage);
    clearModifiedButton->setIcon(m_mainWindow->m_UI->DeleteIcon);
    QPushButton *clearConfirmButton = new QPushButton(passwordPage);
    clearConfirmButton->setIcon(m_mainWindow->m_UI->DeleteIcon);

    // 将输入框和清除按钮添加到水平布局中
    modifiedLayout->addWidget(modifiedLineEdit);
    modifiedLayout->addWidget(clearModifiedButton);
    confirmLayout->addWidget(confirmLineEdit);
    confirmLayout->addWidget(clearConfirmButton);

    // 将水平布局添加到表单布局中
    formLayout->addRow(newpwLabel, modifiedLayout);
    formLayout->addRow(confirmpwLabel, confirmLayout);

    // 创建分组框
    QGroupBox *modifiedPw = new QGroupBox(tr("Modified Password"));

    // 创建 Apply 和 Restore 按钮
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *restoreButton = new QPushButton(tr("Restore"));

    // 创建按钮的水平布局
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(); // 添加弹性空间，将按钮推到右侧
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(restoreButton);

    // 创建一个垂直布局，用于分组框的内部布局
    QVBoxLayout *groupBoxLayout = new QVBoxLayout();
    groupBoxLayout->addLayout(formLayout); // 添加表单布局
    groupBoxLayout->addLayout(buttonLayout); // 添加按钮布局

    // 设置分组框的布局
    modifiedPw->setLayout(groupBoxLayout);

    // 创建主布局，将分组框添加到主布局中
    QVBoxLayout *passwordLayout = new QVBoxLayout(passwordPage);
    passwordLayout->addWidget(modifiedPw);

    // 添加一个可伸缩的空白区域（spacer），放在分组框下方
    QSpacerItem *spacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    passwordLayout->addItem(spacer);

    // 将主布局设置为Password页面的布局
    passwordPage->setLayout(passwordLayout);

    // 连接清除按钮的点击信号到槽函数
    connect(clearModifiedButton, &QPushButton::clicked, modifiedLineEdit, &QLineEdit::clear);
    connect(clearConfirmButton, &QPushButton::clicked, confirmLineEdit, &QLineEdit::clear);

    // 连接 Apply 和 Restore 按钮的点击信号到槽函数
    connect(applyButton, &QPushButton::clicked, this, [this ,modifiedLineEdit ,confirmLineEdit]() {

        if (modifiedLineEdit->text() == confirmLineEdit->text()) {  // 两次输入相同
            if (this->m_mainWindow->m_passwordverfy->modifiedPassword(confirmLineEdit->text())) {  // 调用修改密码成功
                QMessageBox::information(m_mainWindow, tr("Information"), tr("Password change successed ！"));
            } else { // 失败
                QMessageBox::warning(m_mainWindow, tr("Warning"), tr("Password change failed ！"));
                modifiedLineEdit->clear();
                confirmLineEdit->clear();
            }
        } else { // 两次输入不同
            QMessageBox::warning(m_mainWindow, tr("Warning"), tr("The password entered twice is not the same !"));
            modifiedLineEdit->clear();
            confirmLineEdit->clear();
        }


    });
    connect(restoreButton, &QPushButton::clicked, this, [this]() {
        // 处理 Restore 按钮的点击事件
        LOG_DEBUG("Restore button clicked");
        m_mainWindow->m_passwordverfy->defaultStoredHash();
    });

    return passwordPage;
}


QDialog * ui_AdminOptions::setupadminDialog() {
    QDialog *adminDialog = new QDialog(m_mainWindow);
    adminDialog->setWindowTitle(tr("Administrator Preferences"));
    adminDialog->setWindowIcon(m_mainWindow->m_UI->AdministratorIcon);
    adminDialog->resize(800, 600); // 调整对话框大小以适应新的布局

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
    QLineEdit *searchLineEdit = new QLineEdit(adminDialog);
    searchLineEdit->setPlaceholderText(tr("Search..."));
    searchLineEdit->setMinimumWidth(150);
    searchLineEdit->setMaximumWidth(200);

    // 创建左侧的树形结构
    QTreeWidget *treeWidget = new QTreeWidget(adminDialog);
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

    QTreeWidgetItem *accountGroup = new QTreeWidgetItem(treeWidget);
    accountGroup->setText(0, tr("Account"));
    QTreeWidgetItem *passwordItem = new QTreeWidgetItem(accountGroup);
    passwordItem->setText(0, tr("Password"));
    passwordItem->setIcon(0, m_mainWindow->m_UI->PasswordIcon);

    // 将搜索框和树形结构添加到左侧布局中
    leftDialogLayout->addWidget(searchLineEdit);
    leftDialogLayout->addWidget(treeWidget);

    // 添加右侧的分支标签
    QLabel *proLabel = new QLabel(tr("General"), adminDialog); // 初始值为 "General"
    proLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    proLabel->setStyleSheet("font-weight: bold; font-size: 16px;");
    rightDialogLayout->addWidget(proLabel);

    // 添加右侧的分割线
    QFrame *line = new QFrame(adminDialog);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    rightDialogLayout->addWidget(line);

    // 创建右侧的堆叠窗口
    QStackedWidget *stackedWidget = new QStackedWidget(adminDialog);
    rightDialogLayout->addWidget(stackedWidget);

    // 创建堆叠窗口
    QWidget *generalPage = createGeneralPage();     // General 页面
    QWidget *languagePage = createLanguagePage();   // Language 页面
    QWidget *themePage = createThemePage();         // Theme 页面
    QWidget *basicPage = createBasicPage();         // Basic 页面
    QWidget *accountPage = createAccountPage();     // Account 页面
    QWidget *passwordPage = createPasswordPage();   // Password 页面
    QWidget *systemPage    = createSystemPage();       // System 页面
    QWidget *shortcutsPage = createShortcutsPage();    // Shortcuts 页面


    // 将页面添加到堆叠窗口
    stackedWidget->addWidget(generalPage);
    stackedWidget->addWidget(basicPage);
    stackedWidget->addWidget(languagePage);
    stackedWidget->addWidget(themePage);
    stackedWidget->addWidget(accountPage);
    stackedWidget->addWidget(passwordPage);
    stackedWidget->addWidget(systemPage);
    stackedWidget->addWidget(shortcutsPage);


    // 连接树形结构的信号到堆叠窗口的槽
    connect(treeWidget, &QTreeWidget::itemSelectionChanged, this, [this, stackedWidget, treeWidget, proLabel]() {
        QTreeWidgetItem *currentItem = treeWidget->currentItem();
        if (currentItem) {
            QString text = currentItem->text(0);
            if (text == tr("General")) {
                stackedWidget->setCurrentIndex(0);
            } else if (text == tr("Basic")) {
                stackedWidget->setCurrentIndex(1);
            } else if (text == tr("Language")) {
                stackedWidget->setCurrentIndex(2);
            } else if (text == tr("Theme")) {
                stackedWidget->setCurrentIndex(3);
            } else if (text == tr("Account")) {
                stackedWidget->setCurrentIndex(4);
            } else if (text == tr("Password")) {
                stackedWidget->setCurrentIndex(5);
            } else if (text == tr("System")) {
                stackedWidget->setCurrentIndex(6);
            } else if (text == tr("Shortcuts")) {
                stackedWidget->setCurrentIndex(7);
            }

            proLabel->setText(text); // 更新 proLabel 的内容为当前选中的分支
        }
    });

    // 连接搜索框的 textChanged 信号到搜索函数
    connect(searchLineEdit, &QLineEdit::textChanged, this, [this, treeWidget](const QString &searchText) {
        this->searchTreeWidget(treeWidget, searchText);
    });

    // 创建一个水平布局，用于包含最下层的控件
    QHBoxLayout *downLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("Ok"));
    QPushButton *cannelButton = new QPushButton(tr("Cannel"));

    connect(okButton ,&QPushButton::clicked ,this ,[this]{
        this->m_mainWindow->m_UI->adminOptsDialog->close();
    });

    connect(cannelButton ,&QPushButton::clicked ,this ,[this]{
        this->m_mainWindow->m_UI->adminOptsDialog->close();
    });

    // 添加弹性空间，将按钮推到右侧
    downLayout->addStretch();
    downLayout->addWidget(okButton);
    downLayout->addWidget(cannelButton);

    // 设置对话框的主布局
    mainDialogLayout->addLayout(upLayout);
    mainDialogLayout->addStretch(); // 添加弹性空间，将按钮推到下方
    mainDialogLayout->addLayout(downLayout);

    adminDialog->setLayout(mainDialogLayout);

    return adminDialog;
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

