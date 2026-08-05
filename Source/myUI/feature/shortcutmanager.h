#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QMap>
#include <QKeySequence>
#include <QAction>
#include <QShortcut>
#include <QMainWindow>
#include <QStyledItemDelegate>
#include <QLineEdit>

class MainWindow;

// 快捷键 ID 枚举
enum ShortcutId {
    Shortcut_Exit = 1,          // 退出程序，保留持久化配置使用的 ID
    Shortcut_FullScreen,        // 全屏显示
    Shortcut_LockLayout,        // 锁定布局
    Shortcut_SaveLayout,        // 保存布局
    Shortcut_Settings,          // 设置
    Shortcut_Help,              // 帮助
    Shortcut_MAX                // 快捷键数量上限
};

// 快捷键信息结构
struct ShortcutInfo {
    ShortcutId id;
    QString name;               // 快捷键名称（用于显示）
    QKeySequence defaultKey;    // 默认快捷键
    QKeySequence currentKey;    // 当前快捷键
    QString description;        // 快捷键描述
};

class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit ShortcutManager(QObject *parent = nullptr);
    ~ShortcutManager();

    // 初始化快捷键
    void initializeShortcuts();

    // 从配置文件加载快捷键（构造时自动调用）
    void loadFromConfig();

    // 将当前快捷键写回配置文件
    void saveToConfig();

    // 注册快捷键到 QAction
    void registerShortcutToAction(ShortcutId id, QAction *action);

    // 注册快捷键到 QShortcut（用于没有 QAction 的情况）
    QShortcut* registerShortcut(ShortcutId id, QWidget *parent, const QObject *receiver, const char *slot);

    // 获取快捷键序列
    QKeySequence getShortcut(ShortcutId id) const;

    // 设置快捷键
    bool setShortcut(ShortcutId id, const QKeySequence &keySequence);

    // 重置快捷键为默认值
    void resetShortcut(ShortcutId id);

    // 重置所有快捷键为默认值
    void resetAllShortcuts();

    // 获取快捷键信息
    ShortcutInfo getShortcutInfo(ShortcutId id) const;

    // 获取所有快捷键信息
    QList<ShortcutInfo> getAllShortcutInfo() const;

    // 检查快捷键冲突
    bool hasConflict(const QKeySequence &keySequence, ShortcutId excludeId = Shortcut_MAX) const;

    // 获取快捷键名称
    QString getShortcutName(ShortcutId id) const;

signals:
    // 快捷键改变信号
    void shortcutChanged(ShortcutId id, const QKeySequence &newKey);
    // 快捷键数据已写入内存，请求外部落盘
    void shortcutConfigSaved();

private:
    MainWindow *m_mainWindow;

    // 快捷键信息映射表
    QMap<ShortcutId, ShortcutInfo> m_shortcuts;

    // 注册的 QAction 映射表（用于更新快捷键）
    QMap<ShortcutId, QAction*> m_actions;

    // 注册的 QShortcut 映射表
    QMap<ShortcutId, QShortcut*> m_shortcutObjects;

    // 初始化默认快捷键
    void initDefaultShortcuts();
};

// ─── KeyCaptureEdit ──────────────────────────────────────────────────────────
// 单行编辑框，进入焦点后捕获第一次有效按键组合并立即提交

class KeyCaptureEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit KeyCaptureEdit(QWidget *parent = nullptr);

signals:
    void keySequenceCaptured(const QKeySequence &seq);

protected:
    void keyPressEvent(QKeyEvent *e) override;
};

// ─── KeyCaptureDelegate ──────────────────────────────────────────────────────
// 表格列委托，将单元格编辑器替换为 KeyCaptureEdit

class KeyCaptureDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit KeyCaptureDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override;

    void setModelData(QWidget *editor,
                      QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;
};

#endif // SHORTCUTMANAGER_H
