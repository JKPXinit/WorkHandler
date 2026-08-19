#include "softwareconfig.h"
#include "mainwindow.h"

#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QMessageBox>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

#include "languagemanager.h"
#include "thememanager.h"
#include "exitmodedialog.h"

SoftwareConfig::SoftwareConfig(QObject *parent) : QObject(parent)
{
    m_mainWindow = static_cast<class MainWindow*>(parent);

    InitializeXmlConfig();      // 初始化软件配置文件
}

void SoftwareConfig::pathError (const QString &path) {

    // 配置文件创建失败，弹出消息框并退出程序
    QMessageBox::critical(nullptr, QObject::tr("Error"),
                          QObject::tr("Failed to create file: %1").arg(path),
                          QMessageBox::Ok);
    std::exit(1); // 退出程序，返回值为1表示异常退出

    return ;
}

/*
* 初始 xml 配置文件
*
*/
void SoftwareConfig::InitializeXmlConfig() {
    // 获取应用程序的运行目录
    QString configFolderPath = Practical_Function::ResourcePath("/Config/");
    configFilePath = configFolderPath + "/SoftwareConfig.xml";

    QDir dir;
    if (!dir.mkpath(configFolderPath)) {
        pathError (configFolderPath);
    } else {
        QFile frq(configFilePath);
        if (frq.exists()) {  // 文件存在
            Read_config();  // 直接读取配置文件
        } else {  // 文件不存在
            if (frq.open(QIODevice::ReadWrite)) { // 尝试新建配置文件
                Default_Config();  // 使用默认配置
                frq.close();
            } else {
                pathError (configFilePath);
            }
        }
    }

}

/*
* 读取 xml 配置文件
*
*/
void SoftwareConfig::Read_config()
{
    QFile *m_xmlFile;
    m_xmlFile = new QFile(configFilePath);
    m_xmlFile->open(QIODevice::ReadOnly|QIODevice::Text);

    QString strxmlw = QString(m_xmlFile->readAll());
    m_Config = strXml_to_Options(strxmlw);     //指针

    m_xmlFile->close();  //关闭文件流

}

/*
* 解析 xml 配置文件
*
*/
m_Options_t *SoftwareConfig::strXml_to_Options(QString strxml) {
    m_Options_t *OptionsTmp = new m_Options_t;

    // 轮转字段默认值（旧配置文件中可能不存在这些字段）
    OptionsTmp->m_systemConfig.logRotationEnabled      = true;
    OptionsTmp->m_systemConfig.logRotationMode         = 1;
    OptionsTmp->m_systemConfig.logRotationMaxSizeMB    = 10;
    OptionsTmp->m_systemConfig.logRotationIntervalDays = 1;
    OptionsTmp->m_systemConfig.logRotationMaxBackups   = 30;
    OptionsTmp->m_systemConfig.logRetentionDays        = 30;
    OptionsTmp->m_httpServerConfig.interfaceName.clear();
    OptionsTmp->m_httpServerConfig.selectedAddress.clear();
    OptionsTmp->m_httpServerConfig.port = 8080;
    OptionsTmp->m_httpServerConfig.bindAllInterfaces = false;
    OptionsTmp->m_httpServerConfig.autoStart = true;
    OptionsTmp->m_httpServerConfig.keepOriginal = false;
    OptionsTmp->m_httpServerConfig.maxImageWidth = 1920;
    OptionsTmp->m_httpServerConfig.seniorAnimation = false;

    QXmlStreamReader xmlReader(strxml);     // 读取 XML 迭代器

    while (!xmlReader.atEnd() && !xmlReader.hasError()) { // 是否读到 XML 结尾
        xmlReader.readNextStartElement();       // 找到根节点

        if (xmlReader.name() == "myOptions" && xmlReader.isStartElement()) {   // 找到根节点
            while (!(xmlReader.name() == "myOptions" && xmlReader.isEndElement())) {
                xmlReader.readNextStartElement();       // 下一个节点

                if (xmlReader.name() == "UI" && xmlReader.isStartElement()) {
                    while (!(xmlReader.name() == "UI" && xmlReader.isEndElement())) {
                        xmlReader.readNextStartElement();       // 下一个节点

                        if (xmlReader.name() == "language") {
                            QString languageValue = xmlReader.readElementText();
                            OptionsTmp->m_uiConfig.language = languageMap.value(languageValue, English_UK);
                        }

                        if (xmlReader.name() == "theme") {
                            QString themeValue = xmlReader.readElementText();
                            OptionsTmp->m_uiConfig.theme = themeMap.value(themeValue, theme_default);
                        }
                    }
                } // UI

                if (xmlReader.name() == "System" && xmlReader.isStartElement()) {
                    while (!(xmlReader.name() == "System" && xmlReader.isEndElement())) {
                        xmlReader.readNextStartElement();

                        if (xmlReader.name() == "first") {
                            QString firstValue = xmlReader.readElementText();
                            OptionsTmp->m_systemConfig.first = (firstValue == "true");
                        }

                        if (xmlReader.name() == "exitMode") {
                            QString exitValue = xmlReader.readElementText();
                            if (exitValue == "0") {
                                OptionsTmp->m_systemConfig.exitMode = exitForce;
                            } else if (exitValue == "1") {
                                OptionsTmp->m_systemConfig.exitMode = systray;
                            } else {
                                QMessageBox::warning (m_mainWindow ,tr("Warning")
                                                      ,tr("Error type of software exir mode %1").arg(exitValue));
                            }

                        }

                        if (xmlReader.name() == "logOutputMode") {
                            QString logModeValue = xmlReader.readElementText();
                            OptionsTmp->m_systemConfig.logOutputMode = logModeValue.toInt();
                            // 确保值有效
                            if (OptionsTmp->m_systemConfig.logOutputMode < LogOutputConsole ||
                                OptionsTmp->m_systemConfig.logOutputMode > LogOutputFile) {
                                OptionsTmp->m_systemConfig.logOutputMode = LogOutputFile; // 默认文件输出
                            }
                        }

                        if (xmlReader.name() == "logFilePath") {
                            OptionsTmp->m_systemConfig.logFilePath = xmlReader.readElementText();
                        }

                        if (xmlReader.name() == "logRotationEnabled") {
                            OptionsTmp->m_systemConfig.logRotationEnabled = (xmlReader.readElementText() == "true");
                        }

                        if (xmlReader.name() == "logRotationMode") {
                            OptionsTmp->m_systemConfig.logRotationMode = xmlReader.readElementText().toInt();
                        }

                        if (xmlReader.name() == "logRotationMaxSizeMB") {
                            OptionsTmp->m_systemConfig.logRotationMaxSizeMB = xmlReader.readElementText().toInt();
                        }

                        if (xmlReader.name() == "logRotationIntervalDays") {
                            OptionsTmp->m_systemConfig.logRotationIntervalDays = xmlReader.readElementText().toInt();
                        }

                        if (xmlReader.name() == "logRotationMaxBackups") {
                            OptionsTmp->m_systemConfig.logRotationMaxBackups = xmlReader.readElementText().toInt();
                        }

                        if (xmlReader.name() == "logRetentionDays") {
                            bool ok = false;
                            const int days = xmlReader.readElementText().toInt(&ok);
                            if (ok && days >= 1 && days <= 3650) {
                                OptionsTmp->m_systemConfig.logRetentionDays = days;
                            }
                        }
                    }
                } // System

                if (xmlReader.name() == "HttpServer" && xmlReader.isStartElement()) {
                    while (!(xmlReader.name() == "HttpServer" && xmlReader.isEndElement())) {
                        xmlReader.readNextStartElement();

                        if (xmlReader.name() == "interfaceName") {
                            OptionsTmp->m_httpServerConfig.interfaceName = xmlReader.readElementText();
                        }

                        if (xmlReader.name() == "selectedAddress") {
                            OptionsTmp->m_httpServerConfig.selectedAddress = xmlReader.readElementText();
                        }

                        if (xmlReader.name() == "port") {
                            bool ok = false;
                            const int port = xmlReader.readElementText().toInt(&ok);
                            if (ok && port >= 1 && port <= 65535) {
                                OptionsTmp->m_httpServerConfig.port = static_cast<quint16>(port);
                            }
                        }

                        if (xmlReader.name() == "bindAllInterfaces") {
                            OptionsTmp->m_httpServerConfig.bindAllInterfaces =
                                (xmlReader.readElementText() == "true");
                        }

                        if (xmlReader.name() == "autoStart") {
                            OptionsTmp->m_httpServerConfig.autoStart =
                                (xmlReader.readElementText() == "true");
                        }

                        if (xmlReader.name() == "keepOriginal") {
                            OptionsTmp->m_httpServerConfig.keepOriginal =
                                (xmlReader.readElementText() == "true");
                        }

                        if (xmlReader.name() == "maxImageWidth") {
                            bool ok = false;
                            const int width = xmlReader.readElementText().toInt(&ok);
                            if (ok && width >= 320 && width <= 16383) {
                                OptionsTmp->m_httpServerConfig.maxImageWidth = width;
                            }
                        }

                        if (xmlReader.name() == "seniorAnimation") {
                            OptionsTmp->m_httpServerConfig.seniorAnimation =
                                (xmlReader.readElementText() == "true");
                        }
                    }
                } // HttpServer

                if (xmlReader.name() == "Shortcuts" && xmlReader.isStartElement()) {
                    while (!(xmlReader.name() == "Shortcuts" && xmlReader.isEndElement())) {
                        xmlReader.readNextStartElement();

                        if (xmlReader.name() == "key" && xmlReader.isStartElement()) {
                            int id = xmlReader.attributes().value("id").toInt();
                            QString seq = xmlReader.readElementText();
                            OptionsTmp->m_shortcutConfig.keys[id] = seq;
                        }
                    }
                } // Shortcuts
            }
        }
    } // While xml！

    return OptionsTmp;
}



/*
* 使用默认 xml 配置文件
*
*/
void SoftwareConfig::Default_Config() {

    m_Config = new m_Options_t();  //软件配置
    m_Config->m_uiConfig.language = 0;
    m_Config->m_uiConfig.theme = 0;
    m_Config->m_systemConfig.first = false;
    m_Config->m_systemConfig.exitMode = 0;
    m_Config->m_systemConfig.logOutputMode = LogOutputFile;  // 默认文件输出
    m_Config->m_systemConfig.logFilePath = "";               // 默认使用自动路径
    m_Config->m_systemConfig.logRotationEnabled      = true;
    m_Config->m_systemConfig.logRotationMode         = 1;
    m_Config->m_systemConfig.logRotationMaxSizeMB    = 10;
    m_Config->m_systemConfig.logRotationIntervalDays = 1;
    m_Config->m_systemConfig.logRotationMaxBackups   = 30;
    m_Config->m_systemConfig.logRetentionDays        = 30;
    m_Config->m_httpServerConfig.interfaceName.clear();
    m_Config->m_httpServerConfig.selectedAddress.clear();
    m_Config->m_httpServerConfig.port = 8080;
    m_Config->m_httpServerConfig.bindAllInterfaces = false;
    m_Config->m_httpServerConfig.autoStart = true;
    m_Config->m_httpServerConfig.keepOriginal = false;
    m_Config->m_httpServerConfig.maxImageWidth = 1920;
    m_Config->m_httpServerConfig.seniorAnimation = false;
    Write_config(); // 写入配置文件
}


/*
* 写入 xml 配置文件
*
*/
void SoftwareConfig::Write_config() {

    QFile *m_xmlFile = new QFile(configFilePath);
    if(!m_xmlFile->isOpen()) {
        m_xmlFile->open(QIODevice::WriteOnly|QIODevice::Text|QFile::Truncate);  //清空重新写
    }
    QString xmlstr = Options_to_strXml(m_Config);
    m_xmlFile->write(xmlstr.toUtf8());
    m_xmlFile->close(); //写完关闭

}

/*
* 序列化结构体
*
*/
QString SoftwareConfig::Options_to_strXml(m_Options_t *OptionsTmp) {

    QString strWriter;
    QXmlStreamWriter writer(&strWriter);
    writer.setAutoFormatting(true);
    writer.writeStartDocument("1.0", true);

    writer.writeStartElement("myOptions");

    writer.writeStartElement("UI");
    writer.writeTextElement("language", languageList[OptionsTmp->m_uiConfig.language]);
    writer.writeTextElement("theme", themeList[OptionsTmp->m_uiConfig.theme]);
    writer.writeEndElement();               // UI 设置

    writer.writeStartElement("System");
    writer.writeTextElement("first", OptionsTmp->m_systemConfig.first ? "true" : "false");
    writer.writeTextElement("exitMode", OptionsTmp->m_systemConfig.exitMode == exitForce ? "0" : "1");
    writer.writeTextElement("logOutputMode", QString::number(OptionsTmp->m_systemConfig.logOutputMode));
    writer.writeTextElement("logFilePath", OptionsTmp->m_systemConfig.logFilePath);
    writer.writeTextElement("logRotationEnabled",      OptionsTmp->m_systemConfig.logRotationEnabled ? "true" : "false");
    writer.writeTextElement("logRotationMode",         QString::number(OptionsTmp->m_systemConfig.logRotationMode));
    writer.writeTextElement("logRotationMaxSizeMB",    QString::number(OptionsTmp->m_systemConfig.logRotationMaxSizeMB));
    writer.writeTextElement("logRotationIntervalDays", QString::number(OptionsTmp->m_systemConfig.logRotationIntervalDays));
    writer.writeTextElement("logRotationMaxBackups",   QString::number(OptionsTmp->m_systemConfig.logRotationMaxBackups));
    writer.writeTextElement("logRetentionDays",        QString::number(OptionsTmp->m_systemConfig.logRetentionDays));
    writer.writeEndElement();               // 软件设置

    writer.writeStartElement("HttpServer");
    writer.writeTextElement("interfaceName", OptionsTmp->m_httpServerConfig.interfaceName);
    writer.writeTextElement("selectedAddress", OptionsTmp->m_httpServerConfig.selectedAddress);
    writer.writeTextElement("port", QString::number(OptionsTmp->m_httpServerConfig.port));
    writer.writeTextElement("bindAllInterfaces",
                            OptionsTmp->m_httpServerConfig.bindAllInterfaces ? "true" : "false");
    writer.writeTextElement("autoStart",
                            OptionsTmp->m_httpServerConfig.autoStart ? "true" : "false");
    writer.writeTextElement("keepOriginal",
                            OptionsTmp->m_httpServerConfig.keepOriginal ? "true" : "false");
    writer.writeTextElement("maxImageWidth",
                            QString::number(OptionsTmp->m_httpServerConfig.maxImageWidth));
    writer.writeTextElement("seniorAnimation",
                            OptionsTmp->m_httpServerConfig.seniorAnimation ? "true" : "false");
    writer.writeEndElement();               // HTTP 服务设置

    writer.writeStartElement("Shortcuts");
    for (auto it = OptionsTmp->m_shortcutConfig.keys.constBegin();
         it != OptionsTmp->m_shortcutConfig.keys.constEnd(); ++it) {
        writer.writeStartElement("key");
        writer.writeAttribute("id", QString::number(it.key()));
        writer.writeCharacters(it.value());
        writer.writeEndElement();
    }
    writer.writeEndElement();               // Shortcuts

    writer.writeEndElement();               // myOptions
    writer.writeEndDocument();

    return strWriter;
}

void SoftwareConfig::saveLayoutConfig(){

    QSettings Settings(Practical_Function::ResourcePath("/Config/") + "/Windowscf.ini", QSettings::IniFormat);
    Settings.setValue("mainWindow/Geometry", m_mainWindow->saveGeometry());
    Settings.setValue("mainWindow/State", m_mainWindow->saveState());
    Settings.setValue("mainWindow/DockingState", m_mainWindow->m_DockManager->saveState());
    QMessageBox::information(m_mainWindow ,tr("Information") ,tr("The UI layout is saved successfully"));

    return ;
}

void SoftwareConfig::restoreLayoutConfig() {

    QSettings Settings(Practical_Function::ResourcePath("/Config/") + "/Windowscf.ini", QSettings::IniFormat);
    m_mainWindow->restoreGeometry(Settings.value("mainWindow/Geometry").toByteArray());
    m_mainWindow->restoreState(Settings.value("mainWindow/State").toByteArray());
    if (!m_mainWindow->m_DockManager->restoreState(Settings.value("mainWindow/DockingState").toByteArray())) {
        QMessageBox::warning(m_mainWindow ,tr("Warning") ,tr("Restore UI layout failed !"));
    }
}
