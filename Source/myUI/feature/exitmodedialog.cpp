#include "exitmodedialog.h"
#include "mainwindow.h"

#include <QVBoxLayout>
#include <QRadioButton>
#include <QDialogButtonBox>

ExitModeDialog::ExitModeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Choose Exit Mode"));
    setFixedSize(300, 120);

    auto *vbox = new QVBoxLayout(this);
    m_group = new QButtonGroup(this);

    auto *radioExit = new QRadioButton(tr("Exit directly"));
    auto *radioTray = new QRadioButton(tr("Minimize to system tray"));
    radioExit->setChecked(true);

    m_group->addButton(radioExit, int(exitForce));
    m_group->addButton(radioTray, int(systray));

    vbox->addWidget(radioExit);
    vbox->addWidget(radioTray);

    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    vbox->addWidget(bbox);
}

int ExitModeDialog::showWithAnimation(MainWindow *mainWindow)
{
    Q_UNUSED(mainWindow);

    int result = exec();
    if (result == QDialog::Accepted)
        m_mode = static_cast<softwareExitMode>(m_group->checkedId());

    return result;
}
