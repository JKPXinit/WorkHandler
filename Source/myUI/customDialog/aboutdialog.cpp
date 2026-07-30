#include "aboutdialog.h"
#include "mainwindow.h"
#include "myLogger.h"
#include "public.h"

#include <QCoreApplication>

aboutDialog::aboutDialog(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

aboutDialog::~aboutDialog()
{
}

QDialog* aboutDialog::setupAboutDialog()
{
    m_dialog = new QDialog(m_mainWindow);
    m_dialog->setWindowTitle(
        QStringLiteral("%1 - %2").arg(QCoreApplication::applicationName(), tr("About")));
    m_dialog->setFixedSize(600, 500);
    m_dialog->setModal(true);

    setupUI();

    LOG_INFO("About dialog opened");

    return m_dialog;
}

void aboutDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(m_dialog);

    // 创建标签页
    QTabWidget *tabWidget = new QTabWidget();
    tabWidget->addTab(createAboutTab(), tr("About"));
    tabWidget->addTab(createSystemInfoTab(), tr("System Info"));
    tabWidget->addTab(createLicenseTab(), tr("License"));

    mainLayout->addWidget(tabWidget);

    // 底部按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("OK"));
    okButton->setDefault(true);
    okButton->setFixedWidth(100);
    connect(okButton, &QPushButton::clicked, m_dialog, &QDialog::accept);

    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);

    m_dialog->setLayout(mainLayout);
}

QWidget* aboutDialog::createAboutTab()
{
    QWidget *widget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    // 顶部：应用程序图标和名称
    QHBoxLayout *headerLayout = new QHBoxLayout();

    // 图标
    QLabel *iconLabel = new QLabel();
    QPixmap pixmap(AppIcons::App);
    if (!pixmap.isNull()) {
        iconLabel->setPixmap(pixmap.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    iconLabel->setFixedSize(80, 80);

    // 应用名称和版本
    QVBoxLayout *titleLayout = new QVBoxLayout();
    QLabel *appNameLabel = new QLabel(
        QStringLiteral("<h1>%1</h1>").arg(QCoreApplication::applicationName().toHtmlEscaped()));
    QLabel *versionLabel = new QLabel(QString("<b>Version:</b> %1").arg(getAppVersion()));
    versionLabel->setStyleSheet("font-size: 12pt;");

    titleLayout->addWidget(appNameLabel);
    titleLayout->addWidget(versionLabel);
    titleLayout->addStretch();

    headerLayout->addWidget(iconLabel);
    headerLayout->addSpacing(20);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    layout->addLayout(headerLayout);

    // 添加分隔线
    QFrame *line1 = new QFrame();
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line1);

    // 基本信息组
    QGroupBox *infoGroup = new QGroupBox(tr("Application Information"));
    QGridLayout *infoLayout = new QGridLayout();

    QLabel *authorLabel = new QLabel(tr("<b>Author:</b>"));
    QLabel *authorValue = new QLabel("JKPX");

    QLabel *dateLabel = new QLabel(tr("<b>Build Date:</b>"));
    QLabel *dateValue = new QLabel(getAppBuildDate());

    QLabel *qtLabel = new QLabel(tr("<b>Qt Version:</b>"));
    QLabel *qtValue = new QLabel(getQtVersion());

    QLabel *descLabel = new QLabel(tr("<b>Description:</b>"));
    QLabel *descValue = new QLabel(tr("A Qt C++ desktop application template demonstrating\n"
                                      "advanced UI features including docking windows,\n"
                                      "theme management, and multi-language support."));
    descValue->setWordWrap(true);

    infoLayout->addWidget(authorLabel, 0, 0, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(authorValue, 0, 1, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(dateLabel, 1, 0, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(dateValue, 1, 1, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(qtLabel, 2, 0, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(qtValue, 2, 1, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(descLabel, 3, 0, Qt::AlignTop | Qt::AlignLeft);
    infoLayout->addWidget(descValue, 3, 1, Qt::AlignTop | Qt::AlignLeft);

    infoLayout->setColumnStretch(1, 1);
    infoGroup->setLayout(infoLayout);

    layout->addWidget(infoGroup);

    // 版权信息
    QLabel *copyrightLabel = new QLabel(tr("Copyright © 2025 JKPX. All rights reserved."));
    copyrightLabel->setStyleSheet("color: #666; font-size: 9pt;");
    copyrightLabel->setAlignment(Qt::AlignCenter);

    layout->addStretch();
    layout->addWidget(copyrightLabel);

    widget->setLayout(layout);
    return widget;
}

QWidget* aboutDialog::createSystemInfoTab()
{
    QWidget *widget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(20, 20, 20, 20);

    QGroupBox *sysGroup = new QGroupBox(tr("System Information"));
    QGridLayout *sysLayout = new QGridLayout();

    // 操作系统信息
    QLabel *osLabel = new QLabel(tr("<b>Operating System:</b>"));
    QLabel *osValue = new QLabel(QSysInfo::prettyProductName());

    QLabel *kernelLabel = new QLabel(tr("<b>Kernel Version:</b>"));
    QLabel *kernelValue = new QLabel(QSysInfo::kernelVersion());

    QLabel *cpuLabel = new QLabel(tr("<b>CPU Architecture:</b>"));
    QLabel *cpuValue = new QLabel(QSysInfo::currentCpuArchitecture());

    QLabel *buildAbiLabel = new QLabel(tr("<b>Build ABI:</b>"));
    QLabel *buildAbiValue = new QLabel(QSysInfo::buildAbi());

    QLabel *hostLabel = new QLabel(tr("<b>Host Name:</b>"));
    QLabel *hostValue = new QLabel(QSysInfo::machineHostName());

    QLabel *qtRuntimeLabel = new QLabel(tr("<b>Qt Runtime Version:</b>"));
    QLabel *qtRuntimeValue = new QLabel(qVersion());

    QLabel *qtCompileLabel = new QLabel(tr("<b>Qt Compile Version:</b>"));
    QLabel *qtCompileValue = new QLabel(QT_VERSION_STR);

    sysLayout->addWidget(osLabel, 0, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(osValue, 0, 1, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(kernelLabel, 1, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(kernelValue, 1, 1, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(cpuLabel, 2, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(cpuValue, 2, 1, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(buildAbiLabel, 3, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(buildAbiValue, 3, 1, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(hostLabel, 4, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(hostValue, 4, 1, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(qtRuntimeLabel, 5, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(qtRuntimeValue, 5, 1, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(qtCompileLabel, 6, 0, Qt::AlignTop | Qt::AlignLeft);
    sysLayout->addWidget(qtCompileValue, 6, 1, Qt::AlignTop | Qt::AlignLeft);

    sysLayout->setColumnStretch(1, 1);
    sysGroup->setLayout(sysLayout);

    layout->addWidget(sysGroup);
    layout->addStretch();

    widget->setLayout(layout);
    return widget;
}

QWidget* aboutDialog::createLicenseTab()
{
    QWidget *widget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(20, 20, 20, 20);

    QTextEdit *licenseText = new QTextEdit();
    licenseText->setReadOnly(true);
    licenseText->setPlainText(getLicenseText());
    licenseText->setStyleSheet("QTextEdit { background-color: #f5f5f5; border: 1px solid #ccc; }");

    layout->addWidget(licenseText);

    widget->setLayout(layout);
    return widget;
}

QString aboutDialog::getAppVersion() const
{
    return QCoreApplication::applicationVersion();
}

QString aboutDialog::getAppBuildDate() const
{
    return "2026-07-28";
}

QString aboutDialog::getQtVersion() const
{
    return QString("%1 (runtime: %2)").arg(QT_VERSION_STR).arg(qVersion());
}

QString aboutDialog::getSystemInfo() const
{
    QString info;
    info += tr("Operating System: ") + QSysInfo::prettyProductName() + "\n";
    info += tr("Kernel: ") + QSysInfo::kernelVersion() + "\n";
    info += tr("Architecture: ") + QSysInfo::currentCpuArchitecture() + "\n";
    return info;
}

QString aboutDialog::getLicenseText() const
{
    return tr(
        "MIT License\n\n"

        "Copyright (c) 2025 JKPX\n\n"

        "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
        "of this software and associated documentation files (the \"Software\"), to deal\n"
        "in the Software without restriction, including without limitation the rights\n"
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
        "copies of the Software, and to permit persons to whom the Software is\n"
        "furnished to do so, subject to the following conditions:\n\n"

        "The above copyright notice and this permission notice shall be included in all\n"
        "copies or substantial portions of the Software.\n\n"

        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
        "SOFTWARE.\n\n"

        "---\n\n"

        "This application uses Qt Framework, which is licensed under LGPL v3.\n"
        "For more information, visit: https://www.qt.io/licensing/\n\n"

        "Qt Advanced Docking System is licensed under LGPL v2.1.\n"
        "For more information, visit: https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System"
    );
}
