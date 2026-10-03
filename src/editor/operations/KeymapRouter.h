/*
 * 模块名: KeymapRouter
 * 功能概述: 区分键盘区域和编辑区域，保护文本/IME/弹窗，统一分发编辑快捷键。
 * 对外接口: KeymapRouter、EditorKeymap、InputArea
 * 依赖关系: Qt Widgets，不依赖 Scene 或 OpenGL
 * 输入输出: 键盘事件与区域上下文到已注册的 QAction；未实现操作不执行。
 * 异常与错误: 失效动作与不适用区域被拒绝，不回落到另一套键位。
 * 维护说明: Legacy 保留焦点语义，Blender 使用鼠标区域；不会在进入区域时抢焦点。
 */
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>

class QAction;
class QKeyEvent;
class QMainWindow;
class QSettings;
class QWidget;

namespace mini3d::editor {
class OperatorRegistry;
enum class EditorKeymap { Legacy, Blender };
enum class InputArea { None, Viewport, Outliner, Properties, Console };

/** @brief 输入适配层；只分发已存在的动作，业务校验和历史仍归 ViewModel。 */
class KeymapRouter final : public QObject {
    Q_OBJECT
  public:
    explicit KeymapRouter(QMainWindow& window, QObject* parent = nullptr);
    /** @brief 接入菜单建立后的动作，同时移除旧视口/树快捷键关联以防重复执行。 */
    void bindActions();
    /** @brief 已注册操作由统一注册表校验；UI 入口（如 F3）仍直接启动弹窗。 */
    void setRegistry(OperatorRegistry* registry);
    void setKeymap(EditorKeymap keymap);
    /** @brief 模式镜像仅用于选择互斥 G/R/S 动作和标签；业务校验仍由 Registry 执行。 */
    void setEditMode(bool enabled);
    [[nodiscard]] EditorKeymap keymap() const;
    /** @brief 用户键位偏好独立于工程；未知值使用 Blender，读取不写回设置。 */
    void restorePreferences(QSettings& settings);
    void savePreferences(QSettings& settings) const;
    /** @brief 仅区域最大化的单键重绑定；空值禁用，冲突或多段输入拒绝并返回原因。 */
    bool setMaximizeShortcut(const QKeySequence& sequence, QString* reason = nullptr);
    [[nodiscard]] QKeySequence maximizeShortcut() const;
    /** @brief 仅在按键同步分发期间有效；菜单直接执行时为 None。 */
    [[nodiscard]] InputArea dispatchArea() const;
    [[nodiscard]] InputArea areaForWidget(const QWidget* widget) const;
    [[nodiscard]] static bool isTextInput(const QWidget* widget);
    /** @brief 解析键位契约；返回动作 objectName，不表示该能力已注册或可执行。 */
    [[nodiscard]] static QString actionForKey(EditorKeymap keymap, int key,
                                              Qt::KeyboardModifiers modifiers);

  signals:
    void keymapChanged(EditorKeymap keymap);
    void actionDispatched(const QString& actionName, InputArea area);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updateShortcutLabels();
    [[nodiscard]] InputArea inputArea() const;
    QMainWindow& window_;
    QPointer<OperatorRegistry> registry_;
    QHash<QString, QPointer<QAction>> actions_;
    QPointer<QWidget> pointerWidget_;
    QPointer<QWidget> preeditOwner_;
    EditorKeymap keymap_ = EditorKeymap::Blender;
    bool editMode_ = false;
    QKeySequence maximizeShortcut_{QKeyCombination(Qt::ControlModifier, Qt::Key_Space)};
    InputArea dispatchArea_ = InputArea::None;
};
} // namespace mini3d::editor
