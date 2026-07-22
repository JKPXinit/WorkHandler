#ifndef UI_ADMINOPTIONS_H
#define UI_ADMINOPTIONS_H

#include <QWidget>
#include <QDialog>
#include <QTreeWidget>

#include <functional>

class MainWindow;

class ui_AdminOptions : public QObject
{
    Q_OBJECT

public:
    ui_AdminOptions(MainWindow *mainWindow);

public slots:
    QDialog *setupOptionsDialog();

private:
    MainWindow *m_mainWindow;  // 指向 MainWindow 的指针

    QWidget * createGeneralPage();
    QWidget * createBasicPage();
    QWidget * createLanguagePage();
    QWidget * createThemePage();
    QWidget * createHttpPage(std::function<bool()> *applyConfiguration);
    QWidget * createAccountPage();
    QWidget * createSystemPage();
    QWidget * createShortcutsPage();

    void searchTreeWidget(QTreeWidget *treeWidget, const QString &searchText);
    void expandAllChildren(QTreeWidgetItem *item);
    bool searchInItem(QTreeWidgetItem *item, const QString &searchText);
    void clearBoldAndCollapse(QTreeWidgetItem *item);

};

#endif // UI_ADMINOPTIONS_H
