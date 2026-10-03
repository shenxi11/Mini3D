/*
 * 模块名: KeymapRouter
 * 功能概述: 执行区域输入优先级和互斥键位分发，保留文件快捷键的应用入口。
 * 对外接口: KeymapRouter
 * 依赖关系: Qt Widgets/Gui
 * 输入输出: Qt 按键与预编辑事件到单次 QAction 触发及其冻结区域。
 * 异常与错误: 未实现或禁用的动作不执行；不是另一键位的别名。
 * 维护说明: 不对文本输入发建模命令，不请求 focus，不持有可变场景。
 */
#include "KeymapRouter.h"

#include "OperatorRegistry.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QEnterEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QSettings>
#include <QTextEdit>

namespace mini3d::editor {
namespace {
constexpr int kLegacy = 1;
constexpr int kBlender = 2;
constexpr int kBoth = kLegacy | kBlender;
struct Binding {
    int profiles;
    int key;
    Qt::KeyboardModifiers modifiers;
    const char* action;
    bool outliner;
    const char* componentAction = nullptr;
};
const Binding kBindings[] = {
    {kBlender, Qt::Key_Z, Qt::NoModifier, "ShadingPie", false},
    {kBlender, Qt::Key_QuoteLeft, Qt::NoModifier, "ViewPie", false},
    {kBlender, Qt::Key_R, Qt::ShiftModifier, "RepeatLastOperation", false},
    {kBlender, Qt::Key_H, Qt::NoModifier, "HideSelection", false},
    {kBlender, Qt::Key_H, Qt::AltModifier, "RevealHidden", false},
    {kBlender, Qt::Key_Slash, Qt::KeypadModifier, "ToggleLocalView", false},
    {kBoth, Qt::Key_Z, Qt::ControlModifier, "Undo", true},
    {kBoth, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier, "Redo", true},
    {kBoth, Qt::Key_Y, Qt::ControlModifier, "Redo", true},
    {kBoth, Qt::Key_D, Qt::ControlModifier, "Duplicate", true},
    {kBoth, Qt::Key_Delete, Qt::NoModifier, "Delete", true, "DeleteComponentsMenu"},
    {kBlender, Qt::Key_X, Qt::NoModifier, "DeleteComponentsMenu", false},
    {kBoth, Qt::Key_F3, Qt::NoModifier, "SearchOperators", true},
    {kLegacy, Qt::Key_W, Qt::NoModifier, "MoveTool", true},
    {kLegacy, Qt::Key_E, Qt::NoModifier, "RotateTool", true},
    {kLegacy, Qt::Key_R, Qt::NoModifier, "ScaleTool", true},
    {kLegacy, Qt::Key_1, Qt::NoModifier, "FrontView", true},
    {kLegacy, Qt::Key_3, Qt::NoModifier, "RightView", true},
    {kLegacy, Qt::Key_7, Qt::NoModifier, "TopView", true},
    {kLegacy, Qt::Key_0, Qt::NoModifier, "OrbitView", true},
    {kLegacy, Qt::Key_5, Qt::NoModifier, "OrthographicView", true},
    {kLegacy, Qt::Key_F, Qt::NoModifier, "FocusSelection", true},
    {kBlender, Qt::Key_1, Qt::KeypadModifier, "FrontView", false},
    {kBlender, Qt::Key_3, Qt::KeypadModifier, "RightView", false},
    {kBlender, Qt::Key_7, Qt::KeypadModifier, "TopView", false},
    {kBlender, Qt::Key_5, Qt::KeypadModifier, "OrthographicView", false},
    {kBlender, Qt::Key_0, Qt::KeypadModifier, "ToggleCameraPreview", false},
    {kBlender, Qt::Key_Period, Qt::KeypadModifier, "FocusSelection", false},
    {kBlender, Qt::Key_Home, Qt::NoModifier, "FocusAll", false},
    {kBlender, Qt::Key_T, Qt::NoModifier, "ToggleViewportToolbar", false},
    {kBlender, Qt::Key_N, Qt::NoModifier, "ToggleViewportSidebar", false},
    {kBoth, Qt::Key_Q, Qt::NoModifier, "QuickFavorites", true},
    {kBlender, Qt::Key_F9, Qt::NoModifier, "AdjustLastOperation", false},
    {kBlender, Qt::Key_Space, Qt::ControlModifier, "ToggleAreaMaximized", true},
    {kBlender, Qt::Key_G, Qt::NoModifier, "TransformMove", false, "TransformComponentsMove"},
    {kBlender, Qt::Key_E, Qt::NoModifier, "ExtrudeRegion", false},
    {kBlender, Qt::Key_I, Qt::NoModifier, "InsetFace", false},
    {kBlender, Qt::Key_B, Qt::ControlModifier, "BevelEdge", false},
    {kBlender, Qt::Key_R, Qt::ControlModifier, "LoopCut", false},
    {kBlender, Qt::Key_W, Qt::NoModifier, "SelectTool", false},
    {kBlender, Qt::Key_R, Qt::NoModifier, "TransformRotate", false, "TransformComponentsRotate"},
    {kBlender, Qt::Key_S, Qt::NoModifier, "TransformScale", false, "TransformComponentsScale"},
    {kBlender, Qt::Key_Tab, Qt::NoModifier, "ToggleEditMode", false},
    {kBlender, Qt::Key_1, Qt::NoModifier, "SelectVertices", false},
    {kBlender, Qt::Key_2, Qt::NoModifier, "SelectEdges", false},
    {kBlender, Qt::Key_3, Qt::NoModifier, "SelectFaces", false},
    {kBlender, Qt::Key_A, Qt::NoModifier, "SelectAllComponents", false},
    {kBlender, Qt::Key_A, Qt::AltModifier, "ClearComponentSelection", false},
    {kBlender, Qt::Key_B, Qt::NoModifier, "BoxSelectComponents", false},
    {kBlender, Qt::Key_L, Qt::NoModifier, "SelectLinkedComponents", false},
    {kBlender, Qt::Key_O, Qt::NoModifier, "ToggleProportionalEditing", false},
    {kBlender, Qt::Key_Z, Qt::AltModifier, "ToggleXRay", false},
    {kBlender, Qt::Key_Z, Qt::AltModifier | Qt::ShiftModifier, "ToggleOverlays", false},
    {kBlender, Qt::Key_F, Qt::NoModifier, "FillFaces", false},
};

const Binding* bindingFor(EditorKeymap keymap, int key, Qt::KeyboardModifiers modifiers) {
    const int profile = keymap == EditorKeymap::Legacy ? kLegacy : kBlender;
    for (const auto& binding : kBindings) {
        if ((binding.profiles & profile) && binding.key == key && binding.modifiers == modifiers) {
            return &binding;
        }
    }
    return nullptr;
}
} // namespace

KeymapRouter::KeymapRouter(QMainWindow& window, QObject* parent)
    : QObject(parent), window_(window) {
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focused) {
        if (preeditOwner_ && focused != preeditOwner_) {
            preeditOwner_.clear();
        }
    });
}

void KeymapRouter::bindActions() {
    for (const auto& binding : kBindings) {
        for (const auto* actionName : {binding.action, binding.componentAction}) {
            if (!actionName) {
                continue;
            }
            const auto name = QString::fromLatin1(actionName);
            auto* action = window_.findChild<QAction*>(name);
            if (!action) {
                continue; // 键位契约可先定义，未实现的算子没有可执行入口。
            }
            actions_.insert(name, action);
            for (const auto* widgetName : {"ViewportWidget", "SceneTree"}) {
                if (auto* widget = window_.findChild<QWidget*>(QString::fromLatin1(widgetName))) {
                    widget->removeAction(action);
                }
            }
            action->setShortcutContext(Qt::WidgetShortcut);
        }
    }
    updateShortcutLabels();
}

EditorKeymap KeymapRouter::keymap() const {
    return keymap_;
}

void KeymapRouter::restorePreferences(QSettings& settings) {
    const auto name = settings.value(QStringLiteral("workbench/v2/keymap")).toString();
    setKeymap(name == QStringLiteral("legacy") ? EditorKeymap::Legacy : EditorKeymap::Blender);
    const auto stored = settings.value(QStringLiteral("workbench/v2/maximizeShortcut"),
                                       QStringLiteral("Ctrl+Space"));
    if (!setMaximizeShortcut(
            QKeySequence::fromString(stored.toString(), QKeySequence::PortableText))) {
        setMaximizeShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space)));
    }
}

void KeymapRouter::savePreferences(QSettings& settings) const {
    settings.setValue(QStringLiteral("workbench/v2/keymap"), keymap_ == EditorKeymap::Legacy
                                                                 ? QStringLiteral("legacy")
                                                                 : QStringLiteral("blender"));
    settings.setValue(QStringLiteral("workbench/v2/maximizeShortcut"),
                      maximizeShortcut_.toString(QKeySequence::PortableText));
}

QKeySequence KeymapRouter::maximizeShortcut() const {
    return maximizeShortcut_;
}

bool KeymapRouter::setMaximizeShortcut(const QKeySequence& sequence, QString* reason) {
    const auto reject = [reason](const QString& text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };
    if (sequence.count() > 1) {
        return reject(QStringLiteral("区域最大化仅支持一个按键组合，不支持多段序列。"));
    }
    if (!sequence.isEmpty()) {
        const auto key = sequence[0].key();
        if (key == Qt::Key_unknown || key == 0 || key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_Escape ||
            key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab ||
            key == Qt::Key_Backspace || key == Qt::Key_F1) {
            return reject(QStringLiteral("请选择有效组合；确认、取消、输入与帮助键保留给原功能。"));
        }
        for (const auto& binding : kBindings) {
            if (QLatin1String(binding.action) != QLatin1String("ToggleAreaMaximized") &&
                sequence == QKeySequence(QKeyCombination(binding.modifiers,
                                                         static_cast<Qt::Key>(binding.key)))) {
                return reject(QStringLiteral("该按键已被编辑器操作使用，请选择其他组合。"));
            }
        }
        for (auto* action : window_.findChildren<QAction*>()) {
            if (action->objectName() != QStringLiteral("ToggleAreaMaximized") &&
                action->shortcuts().contains(sequence)) {
                return reject(QStringLiteral("该按键已被“%1”使用。").arg(action->text()));
            }
        }
    }
    maximizeShortcut_ = sequence;
    updateShortcutLabels();
    return true;
}

void KeymapRouter::setRegistry(OperatorRegistry* registry) {
    registry_ = registry;
}

void KeymapRouter::setKeymap(EditorKeymap keymap) {
    if (keymap_ != keymap) {
        keymap_ = keymap;
        updateShortcutLabels();
        emit keymapChanged(keymap);
    }
}
void KeymapRouter::setEditMode(bool enabled) {
    editMode_ = enabled;
    updateShortcutLabels();
}

void KeymapRouter::updateShortcutLabels() {
    const int profile = keymap_ == EditorKeymap::Legacy ? kLegacy : kBlender;
    for (auto action = actions_.begin(); action != actions_.end(); ++action) {
        QList<QKeySequence> sequences;
        if (action.key() == QStringLiteral("ToggleAreaMaximized")) {
            if (keymap_ == EditorKeymap::Blender && !maximizeShortcut_.isEmpty()) {
                sequences.append(maximizeShortcut_);
            }
            if (action.value()) {
                action.value()->setShortcuts(sequences);
            }
            continue;
        }
        for (const auto& binding : kBindings) {
            const auto* name =
                editMode_ && binding.componentAction ? binding.componentAction : binding.action;
            if ((binding.profiles & profile) && action.key() == QLatin1String(name)) {
                sequences.append(QKeySequence(
                    QKeyCombination(binding.modifiers, static_cast<Qt::Key>(binding.key))));
            }
        }
        if (action.value()) {
            action.value()->setShortcuts(sequences);
        }
    }
}

QString KeymapRouter::actionForKey(EditorKeymap keymap, int key, Qt::KeyboardModifiers modifiers) {
    const auto* binding = bindingFor(keymap, key, modifiers);
    return binding ? QString::fromLatin1(binding->action) : QString();
}

InputArea KeymapRouter::dispatchArea() const {
    return dispatchArea_;
}

bool KeymapRouter::isTextInput(const QWidget* widget) {
    for (auto* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<const QLineEdit*>(current) ||
            qobject_cast<const QAbstractSpinBox*>(current) ||
            qobject_cast<const QTextEdit*>(current) ||
            qobject_cast<const QPlainTextEdit*>(current)) {
            return true;
        }
        const auto* combo = qobject_cast<const QComboBox*>(current);
        if (combo && combo->isEditable()) {
            return true;
        }
    }
    return false;
}

InputArea KeymapRouter::areaForWidget(const QWidget* widget) const {
    if (!widget || (widget != &window_ && !window_.isAncestorOf(widget))) {
        return InputArea::None;
    }
    for (auto* current = widget; current && current != &window_;
         current = current->parentWidget()) {
        const auto name = current->objectName();
        if (name == QStringLiteral("LastOperationPanel")) {
            return InputArea::None; // 参数区域自己处理输入，不穿透到其下的视口。
        }
        if (name == QStringLiteral("WorkbenchShell")) {
            return InputArea::Viewport;
        }
        if (name == QStringLiteral("SceneDock")) {
            return InputArea::Outliner;
        }
        if (name == QStringLiteral("InspectorDock")) {
            return InputArea::Properties;
        }
        if (name == QStringLiteral("ConsoleDock")) {
            return InputArea::Console;
        }
    }
    return InputArea::None;
}

InputArea KeymapRouter::inputArea() const {
    if (keymap_ == EditorKeymap::Legacy) {
        const auto* focused = QApplication::focusWidget();
        if (focused && (focused->objectName() == QStringLiteral("ViewportWidget") ||
                        focused->objectName() == QStringLiteral("SceneTree"))) {
            return areaForWidget(focused);
        }
        return InputArea::None;
    }
    return areaForWidget(pointerWidget_);
}

bool KeymapRouter::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget && (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)) {
        // 以 Qt 实际收到的屏幕坐标命中区域，不轮询可能不可用的系统光标接口。
        const auto position = event->type() == QEvent::Enter
                                  ? static_cast<QEnterEvent*>(event)->globalPosition()
                                  : static_cast<QMouseEvent*>(event)->globalPosition();
        // Qt 已决定接收此事件的顶层窗口；只在该窗口布局内命中，兼容浮动 Dock。
        auto* receiverWindow = widget->window();
        auto* hit = receiverWindow->childAt(receiverWindow->mapFromGlobal(position.toPoint()));
        pointerWidget_ = areaForWidget(hit) == InputArea::None ? nullptr : hit;
    }
    if (!widget || (widget != &window_ && !window_.isAncestorOf(widget))) {
        return false;
    }
    if (event->type() == QEvent::Leave && pointerWidget_ &&
        (widget == pointerWidget_ || widget->isAncestorOf(pointerWidget_))) {
        pointerWidget_.clear();
    }
    if (event->type() == QEvent::InputMethod) {
        const auto* input = static_cast<QInputMethodEvent*>(event);
        preeditOwner_ = input->preeditString().isEmpty() ? nullptr : widget;
        return false;
    }
    if (event->type() == QEvent::WindowDeactivate) {
        preeditOwner_.clear();
        pointerWidget_.clear();
        return false;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) {
        return false;
    }
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget() || preeditOwner_ ||
        isTextInput(QApplication::focusWidget()) || isTextInput(widget)) {
        return false;
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    auto* binding = bindingFor(keymap_, key->key(), key->modifiers());
    const Binding maximizeBinding{kBlender, key->key(), key->modifiers(), "ToggleAreaMaximized",
                                  true};
    const bool maximize = keymap_ == EditorKeymap::Blender && !maximizeShortcut_.isEmpty() &&
                          maximizeShortcut_ == QKeySequence(key->keyCombination());
    if (maximize) {
        binding = &maximizeBinding;
    } else if (binding && QLatin1String(binding->action) == QLatin1String("ToggleAreaMaximized")) {
        binding = nullptr;
    }
    const auto area = inputArea();
    if (!binding ||
        (area != InputArea::Viewport && !(binding->outliner && area == InputArea::Outliner) &&
         !(maximize && area != InputArea::None))) {
        return false;
    }
    const auto name = QString::fromLatin1(
        editMode_ && binding->componentAction ? binding->componentAction : binding->action);
    const auto action = actions_.value(name);
    if (!action || !action->isEnabled()) {
        return false;
    }
    event->accept();
    if ((maximize || name == QStringLiteral("ToggleEditMode") ||
         name == QStringLiteral("ToggleXRay") || name == QStringLiteral("ToggleOverlays") ||
         name == QStringLiteral("BoxSelectComponents") ||
         name == QStringLiteral("ToggleProportionalEditing") ||
         name == QStringLiteral("DeleteComponentsMenu") || name == QStringLiteral("FillFaces") ||
         name == QStringLiteral("AdjustLastOperation") || name == QStringLiteral("ShadingPie") ||
         name == QStringLiteral("ViewPie") || name == QStringLiteral("RepeatLastOperation")) &&
        key->isAutoRepeat()) {
        return true; // 长按切换键不反复改变布局或编辑模式。
    }
    if (event->type() == QEvent::KeyPress) {
        const QScopedValueRollback<InputArea> context(dispatchArea_, area);
        const auto id = action->property("operatorId").toString();
        if (registry_ && !id.isEmpty()) {
            if (registry_->execute(id, registry_->captureContext(area, keymap_))) {
                emit actionDispatched(name, area);
            }
        } else {
            action->trigger();
            emit actionDispatched(name, area);
        }
    }
    return true;
}
} // namespace mini3d::editor
