#ifndef EXITMODEDIALOG_H
#define EXITMODEDIALOG_H

#include <QDialog>
#include <QButtonGroup>

enum softwareExitMode {
    exitForce = 0,  // 直接退出
    systray         // 托盘
};

class MainWindow;

class ExitModeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExitModeDialog(QWidget *parent = nullptr);

    softwareExitMode selectedMode() const { return m_mode; }
    int showWithAnimation(MainWindow *mainWindow);

private:
    softwareExitMode m_mode = exitForce;
    QButtonGroup *m_group;
};


#endif // EXITMODEDIALOG_H
