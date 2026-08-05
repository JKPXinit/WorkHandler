#include "shortcutmanager.h"
#include "mainwindow.h"
#include "softwareconfig.h"

#include <QKeyEvent>

#include "myLogger.h"

ShortcutManager::ShortcutManager(QObject *parent)
    : QObject(parent)
{
    m_mainWindow = qobject_cast<MainWindow*>(parent);
    initDefaultShortcuts();
    loadFromConfig();
}

ShortcutManager::~ShortcutManager()
{
    // 清理所有 QShortcut 对象
    qDeleteAll(m_shortcutObjects);
    m_shortcutObjects.clear();
}

void ShortcutManager::initDefaultShortcuts()
{
    // 定义所有默认快捷键
    m_shortcuts[Shortcut_Exit] = {
        Shortcut_Exit,
        tr("Exit"),
        QKeySequence(Qt::CTRL | Qt::Key_Q),
        QKeySequence(Qt::CTRL | Qt::Key_Q),
        tr("Exit the application")
    };

    m_shortcuts[Shortcut_FullScreen] = {
        Shortcut_FullScreen,
        tr("Full Screen"),
        QKeySequence(Qt::SHIFT | Qt::Key_F11),
        QKeySequence(Qt::SHIFT | Qt::Key_F11),
        tr("Toggle full screen mode")
    };

    m_shortcuts[Shortcut_LockLayout] = {
        Shortcut_LockLayout,
        tr("Lock Layout"),
        QKeySequence(Qt::ALT | Qt::Key_L),
        QKeySequence(Qt::ALT | Qt::Key_L),
        tr("Lock or unlock the layout")
    };

    m_shortcuts[Shortcut_SaveLayout] = {
        Shortcut_SaveLayout,
        tr("Save Layout"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        tr("Save the current layout")
    };

    m_shortcuts[Shortcut_Settings] = {
        Shortcut_Settings,
        tr("Settings"),
        QKeySequence(Qt::CTRL | Qt::Key_Comma),
        QKeySequence(Qt::CTRL | Qt::Key_Comma),
        tr("Open settings dialog")
    };

    m_shortcuts[Shortcut_Help] = {
        Shortcut_Help,
        tr("Help"),
        QKeySequence(Qt::Key_F1),
        QKeySequence(Qt::Key_F1),
        tr("Show help information")
    };
}

void ShortcutManager::initializeShortcuts()
{
    LOG_DEBUG("ShortcutManager: Shortcuts initialized");
}

void ShortcutManager::loadFromConfig()
{
    if (!m_mainWindow || !m_mainWindow->m_softwareconfig) return;

    const auto &savedKeys = m_mainWindow->m_softwareconfig->shortcutKeys();
    for (auto it = savedKeys.constBegin(); it != savedKeys.constEnd(); ++it) {
        ShortcutId id = static_cast<ShortcutId>(it.key());
        if (!m_shortcuts.contains(id)) continue;

        QKeySequence seq(it.value());
        if (!seq.isEmpty() && !hasConflict(seq, id)) {
            m_shortcuts[id].currentKey = seq;
            LOG_DEBUG(QString("ShortcutManager: Loaded from config: id=%1, key=%2")
                      .arg(id).arg(it.value()));
        }
    }
}

void ShortcutManager::saveToConfig()
{
    if (!m_mainWindow || !m_mainWindow->m_softwareconfig) return;

    QMap<int, QString> newKeys;
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        newKeys[it.key()] = it.value().currentKey.toString();
    }
    m_mainWindow->m_softwareconfig->setShortcutKeys(newKeys);
    emit shortcutConfigSaved();
}

void ShortcutManager::registerShortcutToAction(ShortcutId id, QAction *action)
{
    if (!action) {
        LOG_WARNING("ShortcutManager: Cannot register null action");
        return;
    }

    if (!m_shortcuts.contains(id)) {
        LOG_WARNING(QString("ShortcutManager: Invalid shortcut ID %1").arg(id));
        return;
    }

    // 设置快捷键到 QAction
    action->setShortcut(m_shortcuts[id].currentKey);

    // 保存引用
    m_actions[id] = action;

    LOG_INFO(QString("ShortcutManager: Registered shortcut %1 to action with key %2")
             .arg(m_shortcuts[id].name)
             .arg(m_shortcuts[id].currentKey.toString()));
}

QShortcut* ShortcutManager::registerShortcut(ShortcutId id, QWidget *parent,
                                             const QObject *receiver, const char *slot)
{
    if (!parent || !receiver || !slot) {
        LOG_WARNING("ShortcutManager: Invalid parameters for registerShortcut");
        return nullptr;
    }

    if (!m_shortcuts.contains(id)) {
        LOG_WARNING(QString("ShortcutManager: Invalid shortcut ID %1").arg(id));
        return nullptr;
    }

    // 创建 QShortcut 对象
    QShortcut *shortcut = new QShortcut(m_shortcuts[id].currentKey, parent);
    connect(shortcut, SIGNAL(activated()), receiver, slot);

    // 保存引用
    m_shortcutObjects[id] = shortcut;

    LOG_DEBUG(QString("ShortcutManager: Registered shortcut %1 with key %2")
             .arg(m_shortcuts[id].name)
             .arg(m_shortcuts[id].currentKey.toString()));

    return shortcut;
}

QKeySequence ShortcutManager::getShortcut(ShortcutId id) const
{
    if (m_shortcuts.contains(id)) {
        return m_shortcuts[id].currentKey;
    }
    return QKeySequence();
}

bool ShortcutManager::setShortcut(ShortcutId id, const QKeySequence &keySequence)
{
    if (!m_shortcuts.contains(id)) {
        LOG_WARNING(QString("ShortcutManager: Invalid shortcut ID %1").arg(id));
        return false;
    }

    // 检查冲突
    if (hasConflict(keySequence, id)) {
        LOG_WARNING(QString("ShortcutManager: Shortcut conflict detected for %1").arg(keySequence.toString()));
        return false;
    }

    // 更新快捷键
    m_shortcuts[id].currentKey = keySequence;

    // 更新已注册的 QAction
    if (m_actions.contains(id) && m_actions[id]) {
        m_actions[id]->setShortcut(keySequence);
    }

    // 更新已注册的 QShortcut
    if (m_shortcutObjects.contains(id) && m_shortcutObjects[id]) {
        m_shortcutObjects[id]->setKey(keySequence);
    }

    emit shortcutChanged(id, keySequence);

    LOG_DEBUG(QString("ShortcutManager: Shortcut %1 changed to %2")
             .arg(m_shortcuts[id].name)
             .arg(keySequence.toString()));

    saveToConfig();

    return true;
}

void ShortcutManager::resetShortcut(ShortcutId id)
{
    if (!m_shortcuts.contains(id)) {
        LOG_WARNING(QString("ShortcutManager: Invalid shortcut ID %1").arg(id));
        return;
    }

    // 恢复默认快捷键
    setShortcut(id, m_shortcuts[id].defaultKey);

    LOG_DEBUG(QString("ShortcutManager: Shortcut %1 reset to default").arg(m_shortcuts[id].name));
}

void ShortcutManager::resetAllShortcuts()
{
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        setShortcut(it.key(), it.value().defaultKey);
    }

    LOG_DEBUG("ShortcutManager: All shortcuts reset to default");
}

ShortcutInfo ShortcutManager::getShortcutInfo(ShortcutId id) const
{
    if (m_shortcuts.contains(id)) {
        return m_shortcuts[id];
    }
    return ShortcutInfo();
}

QList<ShortcutInfo> ShortcutManager::getAllShortcutInfo() const
{
    return m_shortcuts.values();
}

bool ShortcutManager::hasConflict(const QKeySequence &keySequence, ShortcutId excludeId) const
{
    // 空快捷键不算冲突
    if (keySequence.isEmpty()) {
        return false;
    }

    // 检查是否与其他快捷键冲突
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (it.key() == excludeId) {
            continue; // 跳过自己
        }

        if (it.value().currentKey == keySequence) {
            LOG_DEBUG(QString("ShortcutManager: Conflict found with %1").arg(it.value().name));
            return true;
        }
    }

    return false;
}

QString ShortcutManager::getShortcutName(ShortcutId id) const
{
    if (m_shortcuts.contains(id)) {
        return m_shortcuts[id].name;
    }
    return QString();
}

// ─── KeyCaptureEdit ──────────────────────────────────────────────────────────

KeyCaptureEdit::KeyCaptureEdit(QWidget *parent) : QLineEdit(parent)
{
    setReadOnly(true);
    setAlignment(Qt::AlignCenter);
    setPlaceholderText(tr("Press a key combination..."));
    setText("...");
}

void KeyCaptureEdit::keyPressEvent(QKeyEvent *e)
{
    int key = e->key();

    // 忽略纯修饰键
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt    || key == Qt::Key_Meta  ||
        key == Qt::Key_unknown) {
        return;
    }

    // Esc → 取消，发空序列通知 delegate 放弃
    if (key == Qt::Key_Escape) {
        emit keySequenceCaptured(QKeySequence());
        return;
    }

    Qt::KeyboardModifiers mods = e->modifiers();
    emit keySequenceCaptured(QKeySequence(key | static_cast<int>(mods)));
}

// ─── KeyCaptureDelegate ──────────────────────────────────────────────────────

KeyCaptureDelegate::KeyCaptureDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

QWidget *KeyCaptureDelegate::createEditor(QWidget *parent,
                                          const QStyleOptionViewItem &,
                                          const QModelIndex &) const
{
    KeyCaptureEdit *editor = new KeyCaptureEdit(parent);
    connect(editor, &KeyCaptureEdit::keySequenceCaptured,
            this, [this, editor](const QKeySequence &seq) {
                editor->setProperty("capturedSeq", seq.toString());
                emit const_cast<KeyCaptureDelegate*>(this)->commitData(editor);
                emit const_cast<KeyCaptureDelegate*>(this)->closeEditor(
                    editor, QAbstractItemDelegate::NoHint);
            });
    return editor;
}

void KeyCaptureDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    Q_UNUSED(index)
    static_cast<KeyCaptureEdit*>(editor)->setText("...");
}

void KeyCaptureDelegate::setModelData(QWidget *editor,
                                      QAbstractItemModel *model,
                                      const QModelIndex &index) const
{
    QString captured = editor->property("capturedSeq").toString();
    if (!captured.isNull())
        model->setData(index, captured, Qt::EditRole);
}

void KeyCaptureDelegate::updateEditorGeometry(QWidget *editor,
                                              const QStyleOptionViewItem &option,
                                              const QModelIndex &) const
{
    editor->setGeometry(option.rect);
}
