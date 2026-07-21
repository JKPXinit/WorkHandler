#ifndef EMPTYDIALOG_H
#define EMPTYDIALOG_H

#include <QObject>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>

class MainWindow;

class emptyDialog:public QObject
{
    Q_OBJECT

public:
    emptyDialog(MainWindow *mainWindow);

public:
    QDialog * setupEmptyViewDialog();

private:
    MainWindow *m_mainWindow;  // 指向 MainWindow 的指针


};

#endif // EMPTYDIALOG_H
