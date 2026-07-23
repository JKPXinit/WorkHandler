#include "mainwindow.h"

#include "public.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(AppIcons::App));

    // 应用元数据由 CMake 项目定义统一提供。
    QCoreApplication::setApplicationName(QStringLiteral(WORKHANDLER_APPLICATION_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(WORKHANDLER_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral(WORKHANDLER_APPLICATION_NAME));
    QCoreApplication::setOrganizationName("YourOrganization");

    qputenv("QT_LOGGING_RULES", "qt.*=true");
    qputenv("QT_DEBUG_PLUGINS", "1");

    MainWindow w;
    w.show();
    return a.exec();
}
