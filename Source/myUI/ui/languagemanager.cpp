#include "languagemanager.h"
#include "mainwindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QComboBox>

LanguageManager::LanguageManager(MainWindow *parent)
    : translator(new QTranslator(qApp))
{
    m_mainWindow = parent;

}

void LanguageManager::loadLanguage(int index)
{
    QString languageCode;

    switch (index) {
    case English_UK:
        languageCode = "en_UK";
        m_trFilePath = ":/prefix2/language/" + languageCode + ".qm";
        break;
    case Chinese_simpled:
        languageCode = "zh_CN";
        m_trFilePath = ":/prefix2/language/" + languageCode + ".qm";
        break;
    default:
        languageCode = "en_UK";
        break;
    }

    if (translator->load(m_trFilePath)) {
        qApp->installTranslator(translator);
    } else {
        QMessageBox::warning(m_mainWindow, tr("Warning"), tr("Failed to load language file."));
    }
}
