/**
 * @file keyboard_shortcuts.cpp
 * @brief Implementation of the central keyboard shortcut manager.
 */

#include "keyboard_shortcuts.hpp"

#include <QKeyCombination>
#include <QSettings>

namespace labelmaster::util {
namespace {

QKeySequence keySequence(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return QKeySequence(QKeyCombination(modifiers, key));
}

QKeyCombination normalizedCombination(QKeyCombination combination) {
    Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    Qt::Key key                     = combination.key();
    // Treat '+' and the physical Shift+'=' representation as the same input.
    if (modifiers.testFlag(Qt::ShiftModifier) && (key == Qt::Key_Plus || key == Qt::Key_Equal)) {
        modifiers.setFlag(Qt::ShiftModifier, false);
        key = Qt::Key_Plus;
    }
    return QKeyCombination(modifiers, key);
}

const QList<KeyboardAction>& orderedActions() {
    static const QList<KeyboardAction> actions{
        KeyboardAction::OpenFolder,
        KeyboardAction::Save,
        KeyboardAction::Previous,
        KeyboardAction::Next,
        KeyboardAction::SmartAnnotate,
        KeyboardAction::HistogramEq,
        KeyboardAction::Delete,
        KeyboardAction::Settings,
        KeyboardAction::Statistics,
        KeyboardAction::Filter,
        KeyboardAction::Help,
        KeyboardAction::About,
        KeyboardAction::Undo,
        KeyboardAction::Redo,
        KeyboardAction::SelectUp,
        KeyboardAction::SelectLeft,
        KeyboardAction::SelectDown,
        KeyboardAction::SelectRight,
        KeyboardAction::EditSelected,
        KeyboardAction::ColorRed,
        KeyboardAction::ColorGray,
        KeyboardAction::ColorBlue,
        KeyboardAction::ColorPurple,
        KeyboardAction::ClassSentry,
        KeyboardAction::Class1,
        KeyboardAction::Class2,
        KeyboardAction::Class3,
        KeyboardAction::Class4,
        KeyboardAction::Class5,
        KeyboardAction::ClassOutpost,
        KeyboardAction::ClassBase,
        KeyboardAction::SizeBig,
        KeyboardAction::SizeSmall,
        KeyboardAction::VisibilityTopLeft,
        KeyboardAction::VisibilityTopRight,
        KeyboardAction::VisibilityBottomLeft,
        KeyboardAction::VisibilityBottomRight,
        KeyboardAction::CancelCanvas,
    };
    return actions;
}

} // namespace

KeyboardManager& KeyboardManager::instance() {
    static KeyboardManager instance;
    return instance;
}

KeyboardManager::KeyboardManager() {
    initDefaults();
    load();
}

void KeyboardManager::initDefaults() {
    defaults_.clear();

    defaults_[KeyboardAction::OpenFolder]    = keySequence(Qt::Key_O, Qt::ControlModifier);
    defaults_[KeyboardAction::Save]          = keySequence(Qt::Key_S, Qt::ControlModifier);
    defaults_[KeyboardAction::Previous]      = keySequence(Qt::Key_Q);
    defaults_[KeyboardAction::Next]          = keySequence(Qt::Key_E);
    defaults_[KeyboardAction::SmartAnnotate] = keySequence(Qt::Key_Space);
    defaults_[KeyboardAction::HistogramEq]   = keySequence(Qt::Key_H);
    defaults_[KeyboardAction::Delete]        = keySequence(Qt::Key_Delete);
    defaults_[KeyboardAction::Settings]      = {};
    defaults_[KeyboardAction::Statistics]    = keySequence(Qt::Key_F1);
    defaults_[KeyboardAction::Filter]        = {};
    defaults_[KeyboardAction::Help]          = {};
    defaults_[KeyboardAction::About]         = {};
    defaults_[KeyboardAction::Undo]          = keySequence(Qt::Key_U);
    defaults_[KeyboardAction::Redo]          = keySequence(Qt::Key_R, Qt::ControlModifier);

    defaults_[KeyboardAction::SelectUp]     = keySequence(Qt::Key_W);
    defaults_[KeyboardAction::SelectLeft]   = keySequence(Qt::Key_A);
    defaults_[KeyboardAction::SelectDown]   = keySequence(Qt::Key_S);
    defaults_[KeyboardAction::SelectRight]  = keySequence(Qt::Key_D);
    defaults_[KeyboardAction::EditSelected] = keySequence(Qt::Key_F2);

    defaults_[KeyboardAction::ColorRed] = keySequence(Qt::Key_R);
    // G is now the top-right visibility command; use the last letter of Gray
    // as its conflict-free mnemonic.
    defaults_[KeyboardAction::ColorGray]    = keySequence(Qt::Key_Y);
    defaults_[KeyboardAction::ColorBlue]    = keySequence(Qt::Key_B);
    defaults_[KeyboardAction::ColorPurple]  = keySequence(Qt::Key_P);
    defaults_[KeyboardAction::ClassSentry]  = keySequence(Qt::Key_0);
    defaults_[KeyboardAction::Class1]       = keySequence(Qt::Key_1);
    defaults_[KeyboardAction::Class2]       = keySequence(Qt::Key_2);
    defaults_[KeyboardAction::Class3]       = keySequence(Qt::Key_3);
    defaults_[KeyboardAction::Class4]       = keySequence(Qt::Key_4);
    defaults_[KeyboardAction::Class5]       = keySequence(Qt::Key_5);
    defaults_[KeyboardAction::ClassOutpost] = keySequence(Qt::Key_O);
    defaults_[KeyboardAction::ClassBase]    = keySequence(Qt::Key_L);
    defaults_[KeyboardAction::SizeBig]      = keySequence(Qt::Key_Plus);
    defaults_[KeyboardAction::SizeSmall]    = keySequence(Qt::Key_Minus);

    // Physical layout:
    //   F G       top-left, top-right
    //   C V       bottom-left, bottom-right
    defaults_[KeyboardAction::VisibilityTopLeft]     = keySequence(Qt::Key_F);
    defaults_[KeyboardAction::VisibilityTopRight]    = keySequence(Qt::Key_G);
    defaults_[KeyboardAction::VisibilityBottomLeft]  = keySequence(Qt::Key_C);
    defaults_[KeyboardAction::VisibilityBottomRight] = keySequence(Qt::Key_V);
    defaults_[KeyboardAction::CancelCanvas]          = keySequence(Qt::Key_Escape);

    shortcuts_ = defaults_;
}

QKeySequence KeyboardManager::shortcut(KeyboardAction action) const {
    return shortcuts_.value(action);
}

QKeySequence KeyboardManager::defaultShortcut(KeyboardAction action) const {
    return defaults_.value(action);
}

void KeyboardManager::setShortcut(KeyboardAction action, const QKeySequence& sequence) {
    if (shortcuts_.value(action) == sequence)
        return;
    shortcuts_[action] = sequence;
    emit shortcutChanged(action, sequence);
}

bool KeyboardManager::setShortcuts(
    const QHash<KeyboardAction, QKeySequence>& shortcuts, QString* error) {
    QHash<KeyboardAction, QKeySequence> complete = shortcuts_;
    for (KeyboardAction action : orderedActions())
        if (shortcuts.contains(action))
            complete[action] = shortcuts.value(action);

    if (!validate(complete, error))
        return false;

    for (KeyboardAction action : orderedActions())
        setShortcut(action, complete.value(action));
    return true;
}

void KeyboardManager::resetToDefaults() {
    setShortcuts(defaults_);
    save();
}

void KeyboardManager::applyToAction(QAction* action, KeyboardAction keyAction) const {
    if (action)
        action->setShortcut(shortcut(keyAction));
}

bool KeyboardManager::matches(
    KeyboardAction action, int key, Qt::KeyboardModifiers modifiers) const {
    const QKeySequence sequence = shortcut(action);
    if (sequence.isEmpty() || sequence.count() != 1)
        return false;

    constexpr auto relevantModifiers = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier
                                     | Qt::MetaModifier | Qt::KeypadModifier
                                     | Qt::GroupSwitchModifier;
    const Qt::KeyboardModifiers normalized = modifiers & relevantModifiers;
    const QKeyCombination configured       = normalizedCombination(sequence[0]);
    const QKeyCombination pressed =
        normalizedCombination(QKeyCombination(normalized, static_cast<Qt::Key>(key)));
    return configured == pressed;
}

QString KeyboardManager::shortcutIdentity(const QKeySequence& sequence) {
    if (sequence.isEmpty() || sequence.count() != 1)
        return sequence.toString(QKeySequence::PortableText);
    return QKeySequence(normalizedCombination(sequence[0])).toString(QKeySequence::PortableText);
}

QString KeyboardManager::actionName(KeyboardAction action) const {
    switch (action) {
    case KeyboardAction::OpenFolder: return tr("打开文件夹");
    case KeyboardAction::Save: return tr("保存标签");
    case KeyboardAction::Previous: return tr("上一张图片");
    case KeyboardAction::Next: return tr("下一张图片");
    case KeyboardAction::SmartAnnotate: return tr("智能标注");
    case KeyboardAction::HistogramEq: return tr("直方图均衡化");
    case KeyboardAction::Delete: return tr("删除当前图片");
    case KeyboardAction::Settings: return tr("打开设置");
    case KeyboardAction::Statistics: return tr("数据统计");
    case KeyboardAction::Filter: return tr("筛选模式");
    case KeyboardAction::Help: return tr("帮助");
    case KeyboardAction::About: return tr("关于");
    case KeyboardAction::Undo: return tr("撤销标注编辑");
    case KeyboardAction::Redo: return tr("重做标注编辑");
    case KeyboardAction::SelectUp: return tr("向上选择 Detector");
    case KeyboardAction::SelectLeft: return tr("向左选择 Detector");
    case KeyboardAction::SelectDown: return tr("向下选择 Detector");
    case KeyboardAction::SelectRight: return tr("向右选择 Detector");
    case KeyboardAction::EditSelected: return tr("编辑选中 Detector");
    case KeyboardAction::ColorRed: return tr("颜色设为 Red");
    case KeyboardAction::ColorGray: return tr("颜色设为 Gray");
    case KeyboardAction::ColorBlue: return tr("颜色设为 Blue");
    case KeyboardAction::ColorPurple: return tr("颜色设为 Purple");
    case KeyboardAction::ClassSentry: return tr("Class 设为 G");
    case KeyboardAction::Class1: return tr("Class 设为 1");
    case KeyboardAction::Class2: return tr("Class 设为 2");
    case KeyboardAction::Class3: return tr("Class 设为 3");
    case KeyboardAction::Class4: return tr("Class 设为 4");
    case KeyboardAction::Class5: return tr("Class 设为 5");
    case KeyboardAction::ClassOutpost: return tr("Class 设为 O（Outpost）");
    case KeyboardAction::ClassBase: return tr("Class 设为 B（Base）");
    case KeyboardAction::SizeBig: return tr("大小设为 Big");
    case KeyboardAction::SizeSmall: return tr("大小设为 Small");
    case KeyboardAction::VisibilityTopLeft: return tr("左上角可见性");
    case KeyboardAction::VisibilityTopRight: return tr("右上角可见性");
    case KeyboardAction::VisibilityBottomLeft: return tr("左下角可见性");
    case KeyboardAction::VisibilityBottomRight: return tr("右下角可见性");
    case KeyboardAction::CancelCanvas: return tr("取消当前画布操作");
    }
    return {};
}

QString KeyboardManager::scopeName(KeyboardAction action) const {
    switch (action) {
    case KeyboardAction::OpenFolder:
    case KeyboardAction::Save:
    case KeyboardAction::Previous:
    case KeyboardAction::Next:
    case KeyboardAction::SmartAnnotate:
    case KeyboardAction::HistogramEq:
    case KeyboardAction::Delete:
    case KeyboardAction::Settings:
    case KeyboardAction::Statistics:
    case KeyboardAction::Filter:
    case KeyboardAction::Help:
    case KeyboardAction::About:
    case KeyboardAction::Undo:
    case KeyboardAction::Redo: return tr("全局");

    case KeyboardAction::SelectUp:
    case KeyboardAction::SelectLeft:
    case KeyboardAction::SelectDown:
    case KeyboardAction::SelectRight: return tr("画布 / 选择");

    case KeyboardAction::VisibilityTopLeft:
    case KeyboardAction::VisibilityTopRight:
    case KeyboardAction::VisibilityBottomLeft:
    case KeyboardAction::VisibilityBottomRight: return tr("画布 / 可见性");

    case KeyboardAction::EditSelected:
    case KeyboardAction::ColorRed:
    case KeyboardAction::ColorGray:
    case KeyboardAction::ColorBlue:
    case KeyboardAction::ColorPurple:
    case KeyboardAction::ClassSentry:
    case KeyboardAction::Class1:
    case KeyboardAction::Class2:
    case KeyboardAction::Class3:
    case KeyboardAction::Class4:
    case KeyboardAction::Class5:
    case KeyboardAction::ClassOutpost:
    case KeyboardAction::ClassBase:
    case KeyboardAction::SizeBig:
    case KeyboardAction::SizeSmall: return tr("画布 / 标注");

    case KeyboardAction::CancelCanvas: return tr("画布");
    }
    return {};
}

QList<KeyboardAction> KeyboardManager::allActions() const { return orderedActions(); }

QString KeyboardManager::actionId(KeyboardAction action) {
    switch (action) {
    case KeyboardAction::OpenFolder: return QStringLiteral("open_folder");
    case KeyboardAction::Save: return QStringLiteral("save");
    case KeyboardAction::Previous: return QStringLiteral("previous");
    case KeyboardAction::Next: return QStringLiteral("next");
    case KeyboardAction::SmartAnnotate: return QStringLiteral("smart_annotate");
    case KeyboardAction::HistogramEq: return QStringLiteral("histogram_equalize");
    case KeyboardAction::Delete: return QStringLiteral("delete_image");
    case KeyboardAction::Settings: return QStringLiteral("settings");
    case KeyboardAction::Statistics: return QStringLiteral("statistics");
    case KeyboardAction::Filter: return QStringLiteral("filter");
    case KeyboardAction::Help: return QStringLiteral("help");
    case KeyboardAction::About: return QStringLiteral("about");
    case KeyboardAction::Undo: return QStringLiteral("undo");
    case KeyboardAction::Redo: return QStringLiteral("redo");
    case KeyboardAction::SelectUp: return QStringLiteral("select_up");
    case KeyboardAction::SelectLeft: return QStringLiteral("select_left");
    case KeyboardAction::SelectDown: return QStringLiteral("select_down");
    case KeyboardAction::SelectRight: return QStringLiteral("select_right");
    case KeyboardAction::EditSelected: return QStringLiteral("edit_selected");
    case KeyboardAction::ColorRed: return QStringLiteral("color_red");
    case KeyboardAction::ColorGray: return QStringLiteral("color_gray");
    case KeyboardAction::ColorBlue: return QStringLiteral("color_blue");
    case KeyboardAction::ColorPurple: return QStringLiteral("color_purple");
    case KeyboardAction::ClassSentry: return QStringLiteral("class_sentry");
    case KeyboardAction::Class1: return QStringLiteral("class_1");
    case KeyboardAction::Class2: return QStringLiteral("class_2");
    case KeyboardAction::Class3: return QStringLiteral("class_3");
    case KeyboardAction::Class4: return QStringLiteral("class_4");
    case KeyboardAction::Class5: return QStringLiteral("class_5");
    case KeyboardAction::ClassOutpost: return QStringLiteral("class_outpost");
    case KeyboardAction::ClassBase: return QStringLiteral("class_base");
    case KeyboardAction::SizeBig: return QStringLiteral("size_big");
    case KeyboardAction::SizeSmall: return QStringLiteral("size_small");
    case KeyboardAction::VisibilityTopLeft: return QStringLiteral("visibility_top_left");
    case KeyboardAction::VisibilityTopRight: return QStringLiteral("visibility_top_right");
    case KeyboardAction::VisibilityBottomLeft: return QStringLiteral("visibility_bottom_left");
    case KeyboardAction::VisibilityBottomRight: return QStringLiteral("visibility_bottom_right");
    case KeyboardAction::CancelCanvas: return QStringLiteral("cancel_canvas");
    }
    return {};
}

bool KeyboardManager::validate(
    const QHash<KeyboardAction, QKeySequence>& shortcuts, QString* error) {
    QHash<QString, KeyboardAction> owners;
    for (KeyboardAction action : orderedActions()) {
        const QKeySequence sequence = shortcuts.value(action);
        if (sequence.isEmpty())
            continue;
        if (sequence.count() != 1) {
            if (error)
                *error = tr("每个操作只能设置一个按键或组合键");
            return false;
        }
        const QString portable = shortcutIdentity(sequence);
        if (owners.contains(portable)) {
            if (error) {
                *error = tr("快捷键 %1 同时分配给了“%2”和“%3”")
                             .arg(
                                 sequence.toString(QKeySequence::NativeText),
                                 actionName(owners.value(portable)), actionName(action));
            }
            return false;
        }
        owners.insert(portable, action);
    }
    return true;
}

void KeyboardManager::save() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("keyboard_shortcuts"));
    settings.remove(QString());
    settings.setValue(QStringLiteral("schema_version"), 2);
    for (KeyboardAction action : orderedActions()) {
        settings.setValue(actionId(action), shortcut(action).toString(QKeySequence::PortableText));
    }
    settings.endGroup();
    settings.sync();
}

void KeyboardManager::load() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("keyboard_shortcuts"));
    if (settings.value(QStringLiteral("schema_version")).toInt() != 2) {
        settings.endGroup();
        return;
    }

    QHash<KeyboardAction, QKeySequence> loaded = defaults_;
    for (KeyboardAction action : orderedActions()) {
        const QString id = actionId(action);
        if (settings.contains(id)) {
            loaded[action] =
                QKeySequence::fromString(settings.value(id).toString(), QKeySequence::PortableText);
        }
    }
    settings.endGroup();

    QString error;
    if (validate(loaded, &error))
        shortcuts_ = loaded;
}

} // namespace labelmaster::util
