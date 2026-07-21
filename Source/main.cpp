#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置应用程序名称（用于日志文件名）
    QCoreApplication::setApplicationName("Page_demo");
    QCoreApplication::setApplicationVersion("0.1.2");
    QCoreApplication::setOrganizationName("YourOrganization");

    qputenv("QT_LOGGING_RULES", "qt.*=true");
    qputenv("QT_DEBUG_PLUGINS", "1");

    MainWindow w;
    w.show();
    return a.exec();
}
