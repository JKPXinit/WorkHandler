#include "emptydialog.h"
#include "mainwindow.h"
#include "myLogger.h"

emptyDialog::emptyDialog(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

QDialog * emptyDialog::setupEmptyViewDialog()
{
    QDialog *emptyViewDialog = new QDialog(m_mainWindow);
    emptyViewDialog->setAttribute(Qt::WA_DeleteOnClose);          // 关闭即销毁

    QVBoxLayout *mainLayout = new QVBoxLayout(emptyViewDialog);

    QLabel *onlyLabel = new QLabel();
    onlyLabel->setText(tr("This dialog just for show"));
    onlyLabel->setAlignment(Qt::AlignCenter);          // 内容居中
    mainLayout->addWidget(onlyLabel);

    return emptyViewDialog;
}
