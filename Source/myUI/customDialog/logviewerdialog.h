#ifndef LOGVIEWERDIALOG_H
#define LOGVIEWERDIALOG_H

#include <QObject>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QFile>
#include <QRegularExpression>
#include <QButtonGroup>

#include "myLogger.h"

class MainWindow;  // 前向声明，避免循环依赖

class logViewerDialog : public QObject
{
    Q_OBJECT

public:
    logViewerDialog(MainWindow *mainWindow);
    ~logViewerDialog();

    QDialog* setupLogViewerDialog();

private slots:
    void onNewLogMessage(LogLevel level, const QString &message);
    void onFilterChanged();
    void onSearchTextChanged(const QString &text);
    void onClearClicked();
    void onSortChanged(int column);

private:
    MainWindow *m_mainWindow;
    QDialog *m_dialog = nullptr;
    QTableWidget *m_logTable = nullptr;
    QLineEdit *m_searchBox = nullptr;
    QComboBox *m_sortComboBox = nullptr;


    // 过滤按钮组
    QCheckBox *m_filterDebug;
    QCheckBox *m_filterInfo;
    QCheckBox *m_filterWarning;
    QCheckBox *m_filterError;
    QCheckBox *m_filterFatal;

    // 统计标签
    QLabel *m_statTotal;
    QLabel *m_statDebug;
    QLabel *m_statInfo;
    QLabel *m_statWarning;
    QLabel *m_statError;
    QLabel *m_statFatal;

    // 日志数据存储
    struct LogEntry {
        LogLevel level;
        QString timestamp;
        QString file;
        int line;
        QString function;
        QString message;
        QString fullText;  // 完整的格式化文本
    };
    QList<LogEntry> m_logEntries;

    // 内部方法
    void setupUI();
    void createFilterBar(QVBoxLayout *mainLayout);
    void createStatisticsBar(QVBoxLayout *mainLayout);
    void createTableView(QVBoxLayout *mainLayout);
    void createControlBar(QVBoxLayout *mainLayout);
    void loadExistingLogs();
    void addLogEntry(const LogEntry &entry);
    void updateStatistics();
    void applyFilters();
    bool matchesFilter(const LogEntry &entry) const;
    bool matchesSearch(const LogEntry &entry, const QString &searchText) const;
    LogEntry parseLogLine(const QString &line);
    QString getLevelString(LogLevel level) const;
    QColor getLevelColor(LogLevel level) const;
};

#endif // LOGVIEWERDIALOG_H
