#ifndef PUBLIC_H
#define PUBLIC_H

// 公共目录

#include <QString>
#include <QDir>
#include <QApplication>

#define ENUM_MAX(enum_type) ((int)enum_type##_MAX)


namespace Practical_Function {

    // 获取资源相对与软件的路径
    static QString ResourcePath(QString path={}) {
        return QDir::cleanPath(QApplication::applicationDirPath()+path);
    }

}

namespace AppIcons {
    constexpr const char* App           = ":/prefix1/icon/app.png";
    constexpr const char* Options       = ":/prefix1/icon/settings.png";
    constexpr const char* Administrator = ":/prefix1/icon/Administrator.png";
    constexpr const char* Password      = ":/prefix1/icon/password.png";
    constexpr const char* Account       = ":/prefix1/icon/account.png";
    constexpr const char* Basic         = ":/prefix1/icon/basic.png";
    constexpr const char* Theme         = ":/prefix1/icon/theme.png";
    constexpr const char* Language      = ":/prefix1/icon/language.png";
    constexpr const char* Delete        = ":/prefix1/icon/delete.png";
    constexpr const char* System        = ":/prefix1/icon/system.png";
    constexpr const char* Shortcut      = ":/prefix1/icon/shortcut.png";
}


#endif // PUBLIC_H
