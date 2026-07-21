#include "thememanager.h"

#include <QFile>
#include <QApplication>
#include <QMessageBox>
#include <QComboBox>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{

}

void ThemeManager::loadTheme(int index)
{
    switch (index) {
    case theme_default:
        qApp->setStyleSheet("");
        return;
    case theme_Ubuntu:      // Ubuntu 主题
        m_themeFilePath = ":/prefix3/theme/Ubuntu.qss";
        break;
    case theme_Aqua:        // Aqua 主题
        m_themeFilePath = ":/prefix3/theme/Aqua.qss";
        break;
    case theme_ElegantDark: // ElegantDark 主题
        m_themeFilePath = ":/prefix3/theme/ElegantDark.qss";
        break;
    case theme_MacOS:   // MacOS 主题
        m_themeFilePath = ":/prefix3/theme/MacOS.qss";
        break;
    case theme_ManjaroMix:      // ManjaroMix 主题
        m_themeFilePath = ":/prefix3/theme/ManjaroMix.qss";
        break;
    case theme_NeonButtons:     // NeonButtons 主题
        m_themeFilePath = ":/prefix3/theme/NeonButtons.qss";
        break;
    case theme_ProfessionalDark:    // ProfessionalDark 主题
        m_themeFilePath = ":/prefix3/theme/ProfessionalDark.qss";
        break;
    case theme_NVIDIA:              // NVIDIA 主题
        m_themeFilePath = ":/prefix3/theme/NVIDIA.qss";
        break;
    default:
        break;
    }

    QFile qssFile(m_themeFilePath);
    if (!qssFile.exists())
    {
        QMessageBox::warning(nullptr, tr("Warning"), tr("Theme file does not exist: %1").arg(m_themeFilePath));
        return;
    }

    if (!qssFile.open(QFile::ReadOnly))
    {
        QMessageBox::critical(nullptr, tr("Error"), tr("Failed to open theme file: %1").arg(m_themeFilePath));
        return;
    }

    QString qssContent = qssFile.readAll();
    qssFile.close();

    // 应用样式表到整个应用程序
    qApp->setStyleSheet(qssContent);
}
