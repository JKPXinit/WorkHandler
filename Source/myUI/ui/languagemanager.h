#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QTranslator>
#include <QStringList>
#include <QMap>

class MainWindow;

enum languageType {
    English_UK = 0,
    Chinese_simpled,
    languageType_MAX
};

const QStringList languageList = {
    "English",      // 英语
    "简体中文",      // 简体中文
};

const QMap<QString, languageType> languageMap = {
    {"English", English_UK},
    {"简体中文", Chinese_simpled},
};

class LanguageManager : public QObject // 继承自 QObject
{
    Q_OBJECT // 宏，用于支持信号和槽机制

public:
    LanguageManager(MainWindow *parent);

    QString m_trFilePath = {};

public slots:
    void loadLanguage(int index);

private:
    MainWindow *m_mainWindow;
    QTranslator *translator;
    QStringList languageList;
    QMap<QString, int> languageMap;
};

#endif // LANGUAGE_MANAGER_H
