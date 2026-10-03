/*
 * 模块名: ComponentInteraction
 * 功能概述: 连接真实点击、稳定选择和深度覆盖层，保持 ViewModel 为状态权威。
 * 对外接口: ComponentInteraction；依赖关系: ComponentPicker、Qt Widgets。
 * 输入输出: Qt 逻辑坐标到选区，世界源面/边/点到只读覆盖层。
 * 异常与错误: 模式/相机不适用时拒绝输入；维护说明: 导航不重建覆盖层缓冲。
 */
#include "ComponentInteraction.h"

#include "ComponentPicker.h"
#include "KeymapRouter.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QEnterEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QRubberBand>
#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

namespace mini3d::editor {
struct ComponentInteraction::BoxState {
    renderer_gl::EditorCamera camera;
    QSize size;
    core::EntityId entity;
    core::MeshId mesh;
    std::uint64_t revision;
    ComponentSelection before;
    quint64 selectionRevision;
    bool xRay;
    QCursor cursor;
    bool hadCursor;
    std::optional<QPointF> origin;
    SelectionOperation operation = SelectionOperation::Replace;
};
struct ComponentInteraction::OverlayTopology {
    static constexpr auto missingFace = std::numeric_limits<std::size_t>::max();
    struct Edge {
        ComponentId id;
        std::size_t first;
        std::size_t second;
        std::array<std::size_t, 2> faces;
    };
    std::shared_ptr<const core::EditableMeshContent> reference;
    std::vector<Edge> edges;
    std::vector<std::size_t> corners;
    std::vector<std::size_t> faceOffsets;
    std::vector<bool> attached;
    std::unordered_map<core::modeling::FaceId, std::size_t> faceIndices;

    explicit OverlayTopology(std::shared_ptr<const core::EditableMeshContent> content)
        : reference(std::move(content)) {
        const auto& source = reference->source;
        std::unordered_map<core::modeling::VertexId, std::size_t> indices;
        indices.reserve(source.vertices.size());
        attached.resize(source.vertices.size());
        for (std::size_t i = 0; i < source.vertices.size(); ++i)
            indices.emplace(source.vertices[i].id, i);
        std::size_t cornerCount = 0;
        for (const auto& face : source.faces)
            cornerCount += face.corners.size();
        corners.reserve(cornerCount);
        edges.reserve(cornerCount);
        faceOffsets.reserve(source.faces.size() + 1);
        faceIndices.reserve(source.faces.size());
        for (std::size_t i = 0; i < source.faces.size(); ++i) {
            const auto& face = source.faces[i];
            faceIndices.emplace(face.id, i);
            faceOffsets.push_back(corners.size());
            for (std::size_t j = 0; j < face.corners.size(); ++j) {
                const auto a = face.corners[j].vertex;
                const auto b = face.corners[(j + 1) % face.corners.size()].vertex;
                const auto id = ComponentId::edge({a, b});
                corners.push_back(indices.at(a));
                attached[indices.at(a)] = true;
                edges.push_back(
                    {id, indices.at(id.first), indices.at(id.second), {i, missingFace}});
            }
        }
        faceOffsets.push_back(corners.size());
        std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
            return a.id < b.id;
        });
        std::size_t count = 0;
        for (std::size_t i = 0; i < edges.size(); ++i) {
            if (count != 0 && edges[count - 1].id == edges[i].id)
                edges[count - 1].faces[1] = edges[i].faces[0];
            else
                edges[count++] = edges[i];
        }
        edges.resize(count);
    }
    bool matches(const std::shared_ptr<const core::EditableMeshContent>& content) const {
        if (reference == content)
            return true;
        const auto& before = reference->source;
        const auto& after = content->source;
        if (before.vertices.size() != after.vertices.size() ||
            before.faces.size() != after.faces.size())
            return false;
        for (std::size_t i = 0; i < before.vertices.size(); ++i)
            if (before.vertices[i].id != after.vertices[i].id)
                return false;
        for (std::size_t i = 0; i < before.faces.size(); ++i) {
            const auto& first = before.faces[i];
            const auto& second = after.faces[i];
            if (first.id != second.id || first.corners.size() != second.corners.size())
                return false;
            for (std::size_t j = 0; j < first.corners.size(); ++j)
                if (first.corners[j].id != second.corners[j].id ||
                    first.corners[j].vertex != second.corners[j].vertex)
                    return false;
        }
        return true;
    }
};
ComponentInteraction::ComponentInteraction(QMainWindow& window, SceneViewModel& model,
                                           renderer_gl::ViewportWidget& viewport, QObject* parent)
    : QObject(parent), window_(window), model_(model), viewport_(viewport) {
    band_ = new QRubberBand(QRubberBand::Rectangle, &viewport_);
    band_->setObjectName(QStringLiteral("ComponentBoxBand"));
    band_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud_ = new QLabel(&viewport_);
    hud_->setObjectName(QStringLiteral("ComponentBoxHud"));
    hud_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud_->setWordWrap(true);
    hud_->setStyleSheet(QStringLiteral("background: #242424; color: #eeeeee; padding: 6px;"));
    hud_->hide();
    qApp->installEventFilter(this);
    connect(&viewport_, &renderer_gl::ViewportWidget::componentPickRequested, this,
            [this](QPointF point, bool extend) {
                const auto camera = viewport_.editorCameraSnapshot();
                if (!model_.isEditMode() || model_.previewCamera() != 0 || !camera) {
                    return;
                }
                const auto hit =
                    pickComponent(*model_.scene(), *model_.assets(), model_.editedEntity(),
                                  model_.componentSelection().domain(), *camera,
                                  {static_cast<float>(point.x()), static_cast<float>(point.y())},
                                  {viewport_.width(), viewport_.height()},
                                  viewport_.isXRayEnabled(), model_.viewportVisibility());
                if (hit) {
                    model_.selectComponent(*hit, extend ? SelectionOperation::Toggle
                                                        : SelectionOperation::Replace);
                } else if (!extend) {
                    model_.clearComponentSelection();
                }
            });
    connect(&model_, &SceneViewModel::componentSelectionChanged, this,
            &ComponentInteraction::refreshOverlay);
    connect(&model_, &SceneViewModel::sceneChanged, this, &ComponentInteraction::refreshOverlay);
    connect(&model_, &SceneViewModel::viewportVisibilityChanged, this,
            &ComponentInteraction::refreshOverlay);
    connect(&model_, &SceneViewModel::viewportVisibilityChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&model_, &SceneViewModel::componentPreviewChanged, this,
            &ComponentInteraction::refreshOverlay);
    connect(&model_, &SceneViewModel::componentPreviewChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&model_, &SceneViewModel::componentSelectionChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&model_, &SceneViewModel::sceneChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&model_, &SceneViewModel::documentReset, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&model_, &SceneViewModel::previewCameraChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&viewport_, &renderer_gl::ViewportWidget::cameraChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&viewport_, &renderer_gl::ViewportWidget::viewModeChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    connect(&viewport_, &renderer_gl::ViewportWidget::xRayChanged, this,
            &ComponentInteraction::cancelBoxSelection);
    refreshOverlay();
}
ComponentInteraction::~ComponentInteraction() = default;
bool ComponentInteraction::isBoxSelecting() const {
    return bool(box_);
}
bool ComponentInteraction::startBoxSelection() {
    if (box_) {
        return true;
    }
    const auto camera = viewport_.editorCameraSnapshot();
    if (!model_.isEditMode() || model_.previewCamera() != 0 || !camera || !viewport_.isVisible()) {
        return false;
    }
    viewport_.resetMoveInteraction();
    viewport_.setFocus(Qt::OtherFocusReason);
    model_.cancelTransformEdit();
    const auto entity = model_.editedEntity();
    const auto mesh = model_.scene()->find(entity)->editableMesh;
    box_ =
        std::make_unique<BoxState>(BoxState{*camera,
                                            viewport_.size(),
                                            entity,
                                            mesh,
                                            model_.scene()->editableMesh(mesh)->evaluationRevision,
                                            model_.componentSelection(),
                                            model_.componentSelectionRevision(),
                                            viewport_.isXRayEnabled(),
                                            viewport_.cursor(),
                                            viewport_.testAttribute(Qt::WA_SetCursor),
                                            {},
                                            SelectionOperation::Replace});
    viewport_.setCursor(Qt::CrossCursor);
    hud_->setText(QStringLiteral("框选 · %1\n左键拖动：替换　Shift：追加　Ctrl：移除\nEsc / "
                                 "右键：取消（点投影 / 边触碰 / 面中心）")
                      .arg(box_->xRay ? QStringLiteral("穿透选择") : QStringLiteral("仅可见部分")));
    hud_->setFixedWidth(std::max(80, std::min(420, viewport_.width() - 20)));
    hud_->move(10, 10);
    hud_->adjustSize();
    hud_->show();
    hud_->raise();
    return true;
}
void ComponentInteraction::cancelBoxSelection() {
    if (!box_) {
        return;
    }
    const auto cursor = box_->cursor;
    const auto hadCursor = box_->hadCursor;
    box_.reset();
    band_->hide();
    hud_->hide();
    if (hadCursor)
        viewport_.setCursor(cursor);
    else
        viewport_.unsetCursor();
    viewport_.resetMoveInteraction();
}
void ComponentInteraction::finishBoxSelection(const QPointF& point) {
    const auto camera = viewport_.editorCameraSnapshot();
    const auto* node = model_.scene()->find(box_->entity);
    const bool valid =
        box_->origin && viewport_.rect().contains(point.toPoint()) && model_.isEditMode() &&
        model_.editedEntity() == box_->entity && node && node->editableMesh == box_->mesh &&
        model_.scene()->editableMesh(box_->mesh)->evaluationRevision == box_->revision &&
        model_.componentSelectionRevision() == box_->selectionRevision &&
        model_.componentSelection() == box_->before && viewport_.size() == box_->size && camera &&
        camera->viewProjectionMatrix() == box_->camera.viewProjectionMatrix();
    if (!valid) {
        cancelBoxSelection();
        return;
    }
    const auto ids = boxSelectComponents(
        *model_.scene(), *model_.assets(), box_->entity, box_->before.domain(), box_->camera,
        {static_cast<float>(box_->origin->x()), static_cast<float>(box_->origin->y())},
        {static_cast<float>(point.x()), static_cast<float>(point.y())},
        {box_->size.width(), box_->size.height()}, box_->xRay, model_.viewportVisibility());
    const auto operation = box_->operation;
    cancelBoxSelection();
    model_.selectComponents(ids, operation);
}
bool ComponentInteraction::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (watched == &viewport_ && event->type() == QEvent::MouseMove)
        pointer_ = static_cast<QMouseEvent*>(event)->position();
    if (watched == &viewport_ && event->type() == QEvent::Enter)
        pointer_ = static_cast<QEnterEvent*>(event)->position();
    if (watched == &viewport_ && event->type() == QEvent::Leave)
        pointer_.reset();
    if (!box_ && watched == &viewport_ && event->type() == QEvent::MouseButtonPress &&
        model_.isEditMode()) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto* router = window_.findChild<KeymapRouter*>();
        const auto modifiers = mouse->modifiers();
        const auto camera = viewport_.editorCameraSnapshot();
        if (router && router->keymap() == EditorKeymap::Blender && camera &&
            model_.previewCamera() == 0 && mouse->button() == Qt::LeftButton &&
            modifiers.testFlag(Qt::AltModifier) && !modifiers.testFlag(Qt::MetaModifier) &&
            model_.componentSelection().domain() == SelectionDomain::Edge) {
            const auto hit = pickComponent(
                *model_.scene(), *model_.assets(), model_.editedEntity(), SelectionDomain::Edge,
                *camera, {static_cast<float>(mouse->position().x()),
                          static_cast<float>(mouse->position().y())},
                {viewport_.width(), viewport_.height()}, viewport_.isXRayEnabled(),
                model_.viewportVisibility());
            viewport_.resetMoveInteraction();
            if (hit)
                model_.selectEdgePath({hit->first, hit->second},
                                      modifiers.testFlag(Qt::ControlModifier),
                                      modifiers.testFlag(Qt::ShiftModifier)
                                          ? SelectionOperation::Add
                                          : SelectionOperation::Replace);
            event->accept();
            return true;
        }
    }
    if (!box_ || !widget || (widget != &window_ && !window_.isAncestorOf(widget))) {
        return false;
    }
    if (event->type() == QEvent::WindowDeactivate ||
        (watched == &viewport_ &&
         (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
          event->type() == QEvent::Resize || event->type() == QEvent::UngrabMouse))) {
        cancelBoxSelection();
        return false;
    }
    if (!widget->isVisible()) {
        return false;
    }
    if (event->type() == QEvent::InputMethod &&
        !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty()) {
        cancelBoxSelection();
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (watched != &viewport_) {
            cancelBoxSelection();
            return false;
        }
        if (mouse->button() == Qt::RightButton) {
            cancelBoxSelection();
        } else if (mouse->button() == Qt::LeftButton &&
                   viewport_.rect().contains(mouse->position().toPoint())) {
            box_->origin = mouse->position();
            box_->operation =
                mouse->modifiers().testFlag(Qt::ControlModifier) ? SelectionOperation::Remove
                : mouse->modifiers().testFlag(Qt::ShiftModifier) ? SelectionOperation::Add
                                                                 : SelectionOperation::Replace;
            band_->setGeometry(QRect(box_->origin->toPoint(), QSize()));
            band_->show();
            viewport_.grabMouse();
        }
        return true;
    }
    if (watched == &viewport_ && event->type() == QEvent::MouseMove) {
        if (box_->origin) {
            band_->setGeometry(QRect(box_->origin->toPoint(),
                                     static_cast<QMouseEvent*>(event)->position().toPoint())
                                   .normalized()
                                   .intersected(viewport_.rect()));
        }
        return true;
    }
    if (watched == &viewport_ && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && box_->origin) {
            finishBoxSelection(mouse->position());
        }
        return true;
    }
    if (watched == &viewport_ && event->type() == QEvent::Wheel) {
        return true;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
        event->type() != QEvent::ShortcutOverride) {
        return false;
    }
    if (KeymapRouter::isTextInput(widget) ||
        KeymapRouter::isTextInput(QApplication::focusWidget())) {
        cancelBoxSelection();
        return false;
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    if (event->type() == QEvent::KeyRelease) {
        return true;
    }
    if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_B || key->key() == Qt::Key_Shift ||
        key->key() == Qt::Key_Control) {
        event->accept();
        if (event->type() == QEvent::KeyPress && key->key() == Qt::Key_Escape) {
            cancelBoxSelection();
        }
        return true;
    }
    // 文件、帮助、搜索及模式等新意图先结束框选，再交还统一 Router。
    cancelBoxSelection();
    return false;
}
void ComponentInteraction::selectLinkedUnderPointer() {
    const auto camera = viewport_.editorCameraSnapshot();
    if (!model_.isEditMode() || model_.previewCamera() != 0 || !camera)
        return;
    std::optional<ComponentId> seed;
    if (pointer_ && viewport_.rect().contains(pointer_->toPoint())) {
        seed = pickComponent(*model_.scene(), *model_.assets(), model_.editedEntity(),
                             model_.componentSelection().domain(), *camera,
                             {static_cast<float>(pointer_->x()), static_cast<float>(pointer_->y())},
                             {viewport_.width(), viewport_.height()}, viewport_.isXRayEnabled(),
                             model_.viewportVisibility());
        if (!seed)
            return; // L 在空白处不跨到旧活动元素；菜单无悬停时使用活动项。
    }
    model_.selectConnected(seed);
}
void ComponentInteraction::refreshOverlay() {
    const auto& visibility = model_.viewportVisibility();
    auto preview = model_.componentPreview();
    if (model_.isEditMode() && visibility.hasHiddenElements()) {
        // 只过滤派生三角；source 和文档资源仍是完整网格，隐藏不是删除。
        const auto original = model_.displayedEditableMesh(model_.editedEntity());
        auto display = std::make_shared<core::EditableMeshContent>(*original);
        display->derived.mesh.indices.clear();
        display->derived.triangleSources.clear();
        for (std::size_t i = 0; i < original->derived.triangleSources.size(); ++i) {
            if (!visibility.isFaceVisible(
                    original->source, original->derived.triangleSources[i].face))
                continue;
            display->derived.triangleSources.push_back(original->derived.triangleSources[i]);
            for (std::size_t corner = 0; corner < 3; ++corner)
                display->derived.mesh.indices.push_back(
                    original->derived.mesh.indices[i * 3 + corner]);
        }
        preview = std::move(display);
    }
    viewport_.setEditablePreview(model_.editedEntity(), std::move(preview));
    renderer_gl::ComponentOverlay overlay;
    if (!model_.isEditMode()) {
        overlayTopology_.reset();
        viewport_.setComponentOverlay(std::move(overlay));
        return;
    }
    const auto& scene = *model_.scene();
    const auto entity = model_.editedEntity();
    const auto displayed = model_.displayedEditableMesh(entity);
    const auto& content = *displayed;
    const auto& source = content.source;
    const auto world = scene.worldMatrix(entity);
    const auto& selected = model_.displayedComponentSelection();
    const glm::vec4 normal{0.12F, 0.12F, 0.12F, 1}, chosen{1.0F, 0.5F, 0.05F, 1},
        active{1, 0.9F, 0.65F, 1};
    const auto color = [&](ComponentId id) {
        return selected.activeId() == id             ? active
               : selected.selectedIds().contains(id) ? chosen
                                                     : normal;
    };
    // 仅复用布局/身份完全相同的拓扑；坐标、选区、世界矩阵与隐藏状态每次更新。
    if (!overlayTopology_ || !overlayTopology_->matches(displayed))
        overlayTopology_ = std::make_unique<OverlayTopology>(displayed);
    overlayTopology_->reference = displayed;
    const auto& topology = *overlayTopology_;
    std::vector<glm::vec3> positions;
    positions.reserve(source.vertices.size());
    std::vector<bool> selectedVertices;
    selectedVertices.reserve(source.vertices.size());
    for (const auto& vertex : source.vertices) {
        positions.push_back(glm::vec3(world * glm::vec4(vertex.position, 1)));
        selectedVertices.push_back(selected.selectedIds().contains({vertex.id}));
    }
    const bool hidden = visibility.hasHiddenElements();
    std::vector<bool> visibleFaces(source.faces.size(), true);
    std::vector<bool> selectedFaces(source.faces.size(), false);
    std::vector<bool> visibleVertices(source.vertices.size(), !hidden);
    for (std::size_t i = 0; i < source.faces.size(); ++i) {
        visibleFaces[i] = !hidden || visibility.isFaceVisible(source.faces[i]);
        selectedFaces[i] = selected.domain() == SelectionDomain::Face &&
                           selected.selectedIds().contains({source.faces[i].id});
        if (hidden && visibleFaces[i])
            for (auto corner = topology.faceOffsets[i]; corner < topology.faceOffsets[i + 1];
                 ++corner)
                visibleVertices[topology.corners[corner]] = true;
    }
    overlay.lines.reserve(topology.edges.size() * 2);
    for (const auto& edge : topology.edges) {
        const auto firstFace = edge.faces[0];
        const auto secondFace = edge.faces[1];
        if (!visibleFaces[firstFace] &&
            (secondFace == OverlayTopology::missingFace || !visibleFaces[secondFace]))
            continue;
        const bool projectedSelected =
            selected.domain() == SelectionDomain::Face
                ? selectedFaces[firstFace] ||
                      (secondFace != OverlayTopology::missingFace && selectedFaces[secondFace])
                : selectedVertices[edge.first] && selectedVertices[edge.second];
        const auto edgeColor = selected.domain() == SelectionDomain::Edge ? color(edge.id)
                               : projectedSelected                        ? chosen
                                                                          : normal;
        overlay.lines.push_back({positions[edge.first], edgeColor});
        overlay.lines.push_back({positions[edge.second], edgeColor});
    }
    if (selected.domain() == SelectionDomain::Vertex) {
        overlay.points.reserve(source.vertices.size());
        for (std::size_t i = 0; i < source.vertices.size(); ++i) {
            const auto id = source.vertices[i].id;
            if (visibility.vertices.contains(id) || (!visibleVertices[i] && topology.attached[i]))
                continue;
            const auto pointColor = selected.activeId() == ComponentId{id} ? active
                                    : selectedVertices[i]                  ? chosen
                                                                           : normal;
            overlay.points.push_back({positions[i], pointColor});
        }
    } else if (selected.domain() == SelectionDomain::Face) {
        overlay.points.reserve(source.faces.size());
        for (std::size_t i = 0; i < source.faces.size(); ++i) {
            const auto& face = source.faces[i];
            if (!visibleFaces[i])
                continue;
            glm::vec3 center{0};
            for (auto corner = topology.faceOffsets[i]; corner < topology.faceOffsets[i + 1];
                 ++corner)
                center += positions[topology.corners[corner]];
            overlay.points.push_back(
                {center / static_cast<float>(face.corners.size()), color({face.id})});
        }
        const auto& derived = content.derived;
        for (std::size_t triangle = 0; triangle < derived.triangleSources.size(); ++triangle) {
            const ComponentId face{derived.triangleSources[triangle].face};
            if (!selected.selectedIds().contains(face) ||
                !visibleFaces[topology.faceIndices.at(face.first)])
                continue;
            auto faceColor = color(face);
            faceColor.a = 0.22F;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto vertex = derived.mesh.indices[triangle * 3 + corner];
                overlay.triangles.push_back(
                    {glm::vec3(world * glm::vec4(derived.mesh.vertices[vertex].position, 1)),
                     faceColor});
            }
        }
    }
    viewport_.setComponentOverlay(std::move(overlay));
}
} // namespace mini3d::editor
