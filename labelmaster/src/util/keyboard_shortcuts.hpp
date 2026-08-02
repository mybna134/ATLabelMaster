/**
 * @file keyboard_shortcuts.hpp
 * @brief Central keyboard shortcut customization.
 */

#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

namespace labelmaster::util {

// Keep every keyboard-driven command in this enum.  The settings dialog is
// generated from allActions(), so adding a command here without exposing it in
// the key-binding tab is deliberately difficult.
enum class KeyboardAction {
    OpenFolder,
    Save,
    Previous,
    Next,
    SmartAnnotate,
    HistogramEq,
    Delete,
    Settings,
    Statistics,
    Filter,
    Help,
    About,
    Undo,
    Redo,

    SelectUp,
    SelectLeft,
    SelectDown,
    SelectRight,
    EditSelected,

    ColorRed,
    ColorGray,
    ColorBlue,
    ColorPurple,
    ClassSentry,
    Class1,
    Class2,
    Class3,
    Class4,
    Class5,
    ClassOutpost,
    ClassBase,
    SizeBig,
    SizeSmall,

    VisibilityTopLeft,
    VisibilityTopRight,
    VisibilityBottomLeft,
    VisibilityBottomRight,
    CancelCanvas,
};

class KeyboardManager final : public QObject {
    Q_OBJECT

public:
    static KeyboardManager& instance();

    QKeySequence shortcut(KeyboardAction action) const;
    QKeySequence defaultShortcut(KeyboardAction action) const;
    QHash<KeyboardAction, QKeySequence> shortcuts() const { return shortcuts_; }

    void setShortcut(KeyboardAction action, const QKeySequence& sequence);
    bool setShortcuts(
        const QHash<KeyboardAction, QKeySequence>& shortcuts, QString* error = nullptr);
    void resetToDefaults();

    void applyToAction(QAction* action, KeyboardAction keyAction) const;
    bool matches(
        KeyboardAction action, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const;
    static QString shortcutIdentity(const QKeySequence& sequence);

    QString actionName(KeyboardAction action) const;
    QString scopeName(KeyboardAction action) const;
    QList<KeyboardAction> allActions() const;

    void save() const;
    void load();

signals:
    void shortcutChanged(KeyboardAction action, const QKeySequence& sequence);

private:
    KeyboardManager();
    ~KeyboardManager() override = default;

    void initDefaults();
    static QString actionId(KeyboardAction action);
    bool validate(const QHash<KeyboardAction, QKeySequence>& shortcuts, QString* error = nullptr);

    QHash<KeyboardAction, QKeySequence> defaults_;
    QHash<KeyboardAction, QKeySequence> shortcuts_;
};

inline QString shortcutString(KeyboardAction action) {
    return KeyboardManager::instance().shortcut(action).toString(QKeySequence::NativeText);
}

} // namespace labelmaster::util
