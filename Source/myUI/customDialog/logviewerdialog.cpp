#include "logviewerdialog.h"
#include "mainwindow.h"
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

logViewerDialog::logViewerDialog(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

logViewerDialog::~logViewerDialog()
{
}

QDialog* logViewerDialog::setupLogViewerDialog()
{
    m_dialog = new QDialog(m_mainWindow);
    m_dialog->setWindowTitle(tr("Log Viewer"));
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dialog->resize(1200, 700);

    setupUI();

    // 连接日志信号
    connect(myLogger::instance(), &myLogger::logMessageEmitted,
            this, &logViewerDialog::onNewLogMessage);

    // 加载已有日志
    loadExistingLogs();

    LOG_INFO("Log viewer opened");

    return m_dialog;
}

void logViewerDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(m_dialog);

    // 统计栏
    createStatisticsBar(mainLayout);

    // 过滤栏
    createFilterBar(mainLayout);

    // 表格视图
    createTableView(mainLayout);

    // 控制栏
    createControlBar(mainLayout);

    m_dialog->setLayout(mainLayout);
}

void logViewerDialog::createStatisticsBar(QVBoxLayout *mainLayout)
{
    QGroupBox *statsGroup = new QGroupBox(tr("Statistics"));
    QHBoxLayout *statsLayout = new QHBoxLayout();

    m_statTotal = new QLabel("Total: 0");
    m_statDebug = new QLabel("Debug: 0");
    m_statInfo = new QLabel("Info: 0");
    m_statWarning = new QLabel("Warning: 0");
    m_statError = new QLabel("Error: 0");
    m_statFatal = new QLabel("Fatal: 0");

    // 设置颜色标识
    m_statDebug->setStyleSheet("color: #666; font-weight: bold;");
    m_statInfo->setStyleSheet("color: #0066cc; font-weight: bold;");
    m_statWarning->setStyleSheet("color: #ff8800; font-weight: bold;");
    m_statError->setStyleSheet("color: #cc0033; font-weight: bold;");
    m_statFatal->setStyleSheet("color: #990000; font-weight: bold;");

    statsLayout->addWidget(m_statTotal);
    statsLayout->addSpacing(20);
    statsLayout->addWidget(m_statDebug);
    statsLayout->addWidget(m_statInfo);
    statsLayout->addWidget(m_statWarning);
    statsLayout->addWidget(m_statError);
    statsLayout->addWidget(m_statFatal);
    statsLayout->addStretch();

    statsGroup->setLayout(statsLayout);
    mainLayout->addWidget(statsGroup);
}

void logViewerDialog::createFilterBar(QVBoxLayout *mainLayout)
{
    QGroupBox *filterGroup = new QGroupBox(tr("Filter by Level"));
    QHBoxLayout *filterLayout = new QHBoxLayout();

    m_filterDebug = new QCheckBox("Debug");
    m_filterInfo = new QCheckBox("Info");
    m_filterWarning = new QCheckBox("Warning");
    m_filterError = new QCheckBox("Error");
    m_filterFatal = new QCheckBox("Fatal");

    // 默认全选
    m_filterDebug->setChecked(true);
    m_filterInfo->setChecked(true);
    m_filterWarning->setChecked(true);
    m_filterError->setChecked(true);
    m_filterFatal->setChecked(true);

    // 连接信号
    connect(m_filterDebug, &QCheckBox::toggled, this, &logViewerDialog::onFilterChanged);
    connect(m_filterInfo, &QCheckBox::toggled, this, &logViewerDialog::onFilterChanged);
    connect(m_filterWarning, &QCheckBox::toggled, this, &logViewerDialog::onFilterChanged);
    connect(m_filterError, &QCheckBox::toggled, this, &logViewerDialog::onFilterChanged);
    connect(m_filterFatal, &QCheckBox::toggled, this, &logViewerDialog::onFilterChanged);

    // 添加全选/全不选按钮
    QPushButton *selectAllBtn = new QPushButton(tr("Select All"));
    QPushButton *deselectAllBtn = new QPushButton(tr("Deselect All"));

    connect(selectAllBtn, &QPushButton::clicked, this, [this]() {
        m_filterDebug->setChecked(true);
        m_filterInfo->setChecked(true);
        m_filterWarning->setChecked(true);
        m_filterError->setChecked(true);
        m_filterFatal->setChecked(true);
    });

    connect(deselectAllBtn, &QPushButton::clicked, this, [this]() {
        m_filterDebug->setChecked(false);
        m_filterInfo->setChecked(false);
        m_filterWarning->setChecked(false);
        m_filterError->setChecked(false);
        m_filterFatal->setChecked(false);
    });

    filterLayout->addWidget(m_filterDebug);
    filterLayout->addWidget(m_filterInfo);
    filterLayout->addWidget(m_filterWarning);
    filterLayout->addWidget(m_filterError);
    filterLayout->addWidget(m_filterFatal);
    filterLayout->addSpacing(20);
    filterLayout->addWidget(selectAllBtn);
    filterLayout->addWidget(deselectAllBtn);
    filterLayout->addStretch();

    filterGroup->setLayout(filterLayout);
    mainLayout->addWidget(filterGroup);
}

void logViewerDialog::createTableView(QVBoxLayout *mainLayout)
{
    // 搜索栏
    QHBoxLayout *searchLayout = new QHBoxLayout();
    QLabel *searchLabel = new QLabel(tr("Search:"));
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText(tr("Search in log messages..."));
    connect(m_searchBox, &QLineEdit::textChanged, this, &logViewerDialog::onSearchTextChanged);

    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchBox);
    mainLayout->addLayout(searchLayout);

    // 表格
    m_logTable = new QTableWidget();
    m_logTable->setColumnCount(6);
    m_logTable->setHorizontalHeaderLabels({tr("Time"), tr("Level"), tr("File"), tr("Line"), tr("Function"), tr("Message")});

    // 设置表格属性
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->verticalHeader()->setVisible(false);

    // 设置列宽
    m_logTable->setColumnWidth(0, 160);  // Time
    m_logTable->setColumnWidth(1, 80);   // Level
    m_logTable->setColumnWidth(2, 150);  // File
    m_logTable->setColumnWidth(3, 50);   // Line
    m_logTable->setColumnWidth(4, 200);  // Function

    // 启用排序
    m_logTable->setSortingEnabled(true);
    connect(m_logTable->horizontalHeader(), &QHeaderView::sectionClicked,
            this, &logViewerDialog::onSortChanged);

    mainLayout->addWidget(m_logTable);
}

void logViewerDialog::createControlBar(QVBoxLayout *mainLayout)
{
    QHBoxLayout *controlLayout = new QHBoxLayout();

    QPushButton *clearBtn = new QPushButton(tr("Clear Display"));

    connect(clearBtn, &QPushButton::clicked, this, &logViewerDialog::onClearClicked);

    controlLayout->addWidget(clearBtn);
    controlLayout->addStretch();

    mainLayout->addLayout(controlLayout);
}

void logViewerDialog::loadExistingLogs()
{
    LogConfig config = myLogger::instance()->getConfig();
    QString logFilePath = config.logFilePath.isEmpty()
                          ? myLogger::getDefaultLogFilePath()
                          : config.logFilePath;

    QFile logFile(logFilePath);
    if (!logFile.exists()) {
        LOG_WARNING("Log file does not exist, starting with empty view");
        return;
    }

    if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open log file: %1").arg(logFilePath));
        return;
    }

    QTextStream in(&logFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec("UTF-8");
#endif

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty() || line.startsWith("=")) {
            continue;  // 跳过空行和分隔线
        }

        LogEntry entry = parseLogLine(line);
        if (!entry.fullText.isEmpty()) {
            m_logEntries.append(entry);
        }
    }

    logFile.close();

    // 更新显示
    applyFilters();
    updateStatistics();

    LOG_INFO(QString("Loaded %1 log entries from file").arg(m_logEntries.size()));
}

logViewerDialog::LogEntry logViewerDialog::parseLogLine(const QString &line)
{
    LogEntry entry;
    entry.fullText = line;

    // 日志格式: [2025-03-16 14:30:52.123] [INFO   ] [file.cpp:  42] [function()] message
    QRegularExpression regex(R"(\[([\d\-: .]+)\]\s+\[(\w+)\s*\]\s+\[([\w.]+):\s*(\d+)\]\s+\[(.*?)\]\s+(.*))");
    QRegularExpressionMatch match = regex.match(line);

    if (match.hasMatch()) {
        entry.timestamp = match.captured(1).trimmed();
        QString levelStr = match.captured(2).trimmed();
        entry.file = match.captured(3).trimmed();
        entry.line = match.captured(4).toInt();
        entry.function = match.captured(5).trimmed();
        entry.message = match.captured(6).trimmed();

        // 解析日志级别
        if (levelStr == "DEBUG") entry.level = LogLevel_Debug;
        else if (levelStr == "INFO") entry.level = LogLevel_Info;
        else if (levelStr == "WARNING") entry.level = LogLevel_Warning;
        else if (levelStr == "ERROR") entry.level = LogLevel_Error;
        else if (levelStr == "FATAL") entry.level = LogLevel_Fatal;
        else entry.level = LogLevel_Info;  // 默认
    }

    return entry;
}

void logViewerDialog::onNewLogMessage(LogLevel level, const QString &message)
{
    // 解析新日志消息
    LogEntry entry = parseLogLine(message);
    if (!entry.fullText.isEmpty()) {
        m_logEntries.append(entry);
        addLogEntry(entry);
        updateStatistics();
    }
}

void logViewerDialog::addLogEntry(const LogEntry &entry)
{
    // 检查是否符合过滤条件
    if (!matchesFilter(entry)) {
        return;
    }

    // 检查搜索条件
    QString searchText = m_searchBox ? m_searchBox->text() : "";
    if (!searchText.isEmpty() && !matchesSearch(entry, searchText)) {
        return;
    }

    int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    // 设置单元格内容
    QTableWidgetItem *timeItem = new QTableWidgetItem(entry.timestamp);
    QTableWidgetItem *levelItem = new QTableWidgetItem(getLevelString(entry.level));
    QTableWidgetItem *fileItem = new QTableWidgetItem(entry.file);
    QTableWidgetItem *lineItem = new QTableWidgetItem(QString::number(entry.line));
    QTableWidgetItem *funcItem = new QTableWidgetItem(entry.function);
    QTableWidgetItem *msgItem = new QTableWidgetItem(entry.message);

    // 设置日志级别颜色
    QColor levelColor = getLevelColor(entry.level);
    levelItem->setForeground(QBrush(levelColor));
    levelItem->setData(Qt::UserRole, entry.level);  // 存储级别用于排序

    m_logTable->setItem(row, 0, timeItem);
    m_logTable->setItem(row, 1, levelItem);
    m_logTable->setItem(row, 2, fileItem);
    m_logTable->setItem(row, 3, lineItem);
    m_logTable->setItem(row, 4, funcItem);
    m_logTable->setItem(row, 5, msgItem);

    // 自动滚动到最新
    m_logTable->scrollToBottom();
}

bool logViewerDialog::matchesFilter(const LogEntry &entry) const
{
    if (!m_filterDebug || !m_filterInfo || !m_filterWarning || !m_filterError || !m_filterFatal) {
        return true;  // 如果控件还未初始化，显示所有
    }

    switch (entry.level) {
    case LogLevel_Debug:   return m_filterDebug->isChecked();
    case LogLevel_Info:    return m_filterInfo->isChecked();
    case LogLevel_Warning: return m_filterWarning->isChecked();
    case LogLevel_Error:   return m_filterError->isChecked();
    case LogLevel_Fatal:   return m_filterFatal->isChecked();
    default:               return true;
    }
}

bool logViewerDialog::matchesSearch(const LogEntry &entry, const QString &searchText) const
{
    if (searchText.isEmpty()) {
        return true;
    }

    QString lowerSearch = searchText.toLower();
    return entry.message.toLower().contains(lowerSearch) ||
           entry.file.toLower().contains(lowerSearch) ||
           entry.function.toLower().contains(lowerSearch);
}

void logViewerDialog::updateStatistics()
{
    int total = m_logEntries.size();
    int debugCount = 0, infoCount = 0, warningCount = 0, errorCount = 0, fatalCount = 0;

    for (const LogEntry &entry : m_logEntries) {
        switch (entry.level) {
        case LogLevel_Debug:   debugCount++;   break;
        case LogLevel_Info:    infoCount++;    break;
        case LogLevel_Warning: warningCount++; break;
        case LogLevel_Error:   errorCount++;   break;
        case LogLevel_Fatal:   fatalCount++;   break;
        }
    }

    if (m_statTotal) m_statTotal->setText(QString("Total: %1").arg(total));
    if (m_statDebug) m_statDebug->setText(QString("Debug: %1").arg(debugCount));
    if (m_statInfo) m_statInfo->setText(QString("Info: %1").arg(infoCount));
    if (m_statWarning) m_statWarning->setText(QString("Warning: %1").arg(warningCount));
    if (m_statError) m_statError->setText(QString("Error: %1").arg(errorCount));
    if (m_statFatal) m_statFatal->setText(QString("Fatal: %1").arg(fatalCount));
}

void logViewerDialog::applyFilters()
{
    if (!m_logTable) return;

    m_logTable->setSortingEnabled(false);
    m_logTable->setRowCount(0);

    QString searchText = m_searchBox ? m_searchBox->text() : "";

    for (const LogEntry &entry : m_logEntries) {
        if (matchesFilter(entry) && matchesSearch(entry, searchText)) {
            addLogEntry(entry);
        }
    }

    m_logTable->setSortingEnabled(true);
}

void logViewerDialog::onFilterChanged()
{
    applyFilters();
}

void logViewerDialog::onSearchTextChanged(const QString &text)
{
    Q_UNUSED(text);
    applyFilters();
}

void logViewerDialog::onClearClicked()
{
    m_logTable->setRowCount(0);
    LOG_INFO("Log display cleared (data preserved)");
}

void logViewerDialog::onSortChanged(int column)
{
    Q_UNUSED(column);
    // 排序由 QTableWidget 自动处理
}

QString logViewerDialog::getLevelString(LogLevel level) const
{
    switch (level) {
    case LogLevel_Debug:   return "DEBUG";
    case LogLevel_Info:    return "INFO";
    case LogLevel_Warning: return "WARNING";
    case LogLevel_Error:   return "ERROR";
    case LogLevel_Fatal:   return "FATAL";
    default:               return "UNKNOWN";
    }
}

QColor logViewerDialog::getLevelColor(LogLevel level) const
{
    switch (level) {
    case LogLevel_Debug:   return QColor("#666666");
    case LogLevel_Info:    return QColor("#0066cc");
    case LogLevel_Warning: return QColor("#ff8800");
    case LogLevel_Error:   return QColor("#cc0033");
    case LogLevel_Fatal:   return QColor("#990000");
    default:               return QColor("#000000");
    }
}
