#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>

// 扩展主题类型
enum themeType {
    theme_default = 0,  // 默认主题
    theme_Ubuntu,
    theme_Aqua,
    theme_ElegantDark,
    theme_MacOS,
    theme_ManjaroMix,
    theme_NeonButtons,
    theme_ProfessionalDark,
    theme_NVIDIA,
    themeType_MAX
};

// 扩展主题列表
const QStringList themeList = {
    "默认主题",    // 默认主题
    "Ubuntu",
    "Aqua",
    "ElegantDark",
    "MacOS",
    "ManjaroMix",
    "NeonButtons",
    "ProfessionalDark",
    "NVIDIA"
};

const QMap<QString, themeType> themeMap = {
    {"default", theme_default},
    {"Ubuntu", theme_Ubuntu},
    {"Aqua", theme_Aqua},
    {"ElegantDark", theme_ElegantDark},
    {"MacOS", theme_MacOS},
    {"ManjaroMix", theme_ManjaroMix},
    {"NeonButtons", theme_NeonButtons},
    {"ProfessionalDark", theme_ProfessionalDark},
    {"NVIDIA", theme_NVIDIA}
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);

public:
    QString m_themeFilePath = {};

public slots:
    void loadTheme(int index);

signals:

};

#endif // THEME_MANAGER_H
