/*
 * 模块名: OperatorRegistry
 * 功能概述: 登记当前真实操作，按中英文/快捷键排序并校验冻结上下文。
 * 对外接口: OperatorRegistry
 * 依赖关系: Qt Widgets、SceneViewModel
 * 输入输出: 查询和稳定 ID 到菜单已有 QAction；历史由业务入口维护。
 * 异常与错误: 失败不改目标，不把不可用或尚未实现功能当可执行结果。
 * 维护说明: 新建/打开沿用 Scene 地址，必须使用 documentReset 代际而非指针判文档。
 */
#include "OperatorRegistry.h"

#include "editor/SceneViewModel.h"
#include "renderer_gl/RayCaster.h"

#include <QAction>
#include <QMainWindow>
#include <QScopedValueRollback>
#include <algorithm>

namespace mini3d::editor {
namespace {
struct Definition {
    const char* id;
    const char* actionName;
    const char* chineseName;
    const char* englishName;
    const char* category;
    bool selection;
    bool viewportOnly;
    bool undoable;
};
const Definition kDefinitions[] = {
    {"view.shading_pie", "ShadingPie", "着色饼菜单", "Shading Pie", "视图", false, true, false},
    {"view.view_pie", "ViewPie", "视图饼菜单", "View Pie", "视图", false, true, false},
    {"view.shading_material", "ShadingMaterial", "材质（现有预览）", "Material", "着色", false,
     true, false},
    {"view.shading_solid", "ShadingSolid", "实体 Solid", "Solid", "着色", false, true, false},
    {"view.shading_wireframe", "ShadingWireframe", "线框 Wireframe", "Wireframe", "着色", false,
     true, false},
    {"history.repeat_last", "RepeatLastOperation", "重复上一步（新操作）", "Repeat Last", "历史",
     true, true, true},
    {"mesh.proportional_toggle", "ToggleProportionalEditing", "比例编辑 Smooth",
     "Proportional Editing", "变换", true, true, false},
    {"mesh.proportional_connected", "ProportionalConnected", "比例编辑仅连通",
     "Connected Proportional", "变换", true, true, false},
    {"mesh.select_loop", "SelectEdgeLoop", "选择边循环 Loop", "Select Edge Loop", "选择", true,
     true, false},
    {"mesh.select_ring", "SelectEdgeRing", "选择边环 Ring", "Select Edge Ring", "选择", true, true,
     false},
    {"mesh.select_linked", "SelectLinkedComponents", "选择连通片", "Select Linked", "选择", true,
     true, false},
    {"modifier.mirror_add", "MirrorAdd", "添加 Mirror", "Add Mirror", "修改器", true, false, true},
    {"modifier.mirror_apply", "MirrorApply", "应用 Mirror", "Apply Mirror", "修改器", true, false,
     true},
    {"modifier.mirror_remove", "MirrorRemove", "删除 Mirror", "Remove Mirror", "修改器", true,
     false, true},
    {"view.hide_selection", "HideSelection", "隐藏所选（仅视口）", "Hide Selected", "可见性", true,
     true, false},
    {"view.reveal_hidden", "RevealHidden", "恢复隐藏项（仅视口）", "Reveal Hidden", "可见性", false,
     true, false},
    {"view.local_view", "ToggleLocalView", "局部视图 / 返回全部", "Toggle Local View", "可见性",
     false, true, false},
    {"transform.snap_toggle", "SnapTransform", "启用/关闭吸附", "Toggle Snapping", "吸附", false,
     true, false},
    {"transform.snap_increment", "SnapIncrement", "吸附类型：步进", "Snap Increment", "吸附", false,
     true, false},
    {"transform.snap_vertex", "SnapVertex", "吸附类型：顶点", "Snap Vertex", "吸附", false, true,
     false},
    {"transform.pivot_median", "PivotMedian", "枢轴：选择质心", "Pivot Median Point", "枢轴", false,
     true, false},
    {"transform.pivot_active", "PivotActive", "枢轴：活动元素", "Pivot Active Element", "枢轴",
     false, true, false},
    {"transform.pivot_cursor", "PivotCursor", "枢轴：3D 游标", "Pivot 3D Cursor", "枢轴", false,
     true, false},
    {"selection.to_cursor", "SelectionToCursor", "选择到游标（保持偏移）",
     "Selection to Cursor Keep Offset", "游标", true, false, true},
    {"cursor.place", "PlaceCursor", "点击放置 3D 游标", "Place 3D Cursor", "游标", false, true,
     false},
    {"cursor.to_origin", "CursorToOrigin", "游标到世界原点", "Cursor to World Origin", "游标",
     false, false, false},
    {"cursor.to_selection", "CursorToSelection", "游标到选择", "Cursor to Selection", "游标", true,
     false, false},
    {"cursor.toggle_visibility", "ToggleCursorVisibility", "显示/隐藏 3D 游标",
     "Toggle 3D Cursor Visibility", "游标", false, false, false},
    {"mesh.toggle_edit_mode", "ToggleEditMode", "切换对象 / 编辑模式", "Toggle Edit Mode", "网格",
     true, true, false},
    {"mesh.select_vertices", "SelectVertices", "点选择模式", "Vertex Select", "网格", true, true,
     false},
    {"mesh.select_edges", "SelectEdges", "边选择模式", "Edge Select", "网格", true, true, false},
    {"mesh.select_faces", "SelectFaces", "面选择模式", "Face Select", "网格", true, true, false},
    {"mesh.select_all", "SelectAllComponents", "全选组件", "Select All Components", "网格", true,
     true, false},
    {"mesh.deselect_all", "ClearComponentSelection", "取消组件选择", "Deselect All Components",
     "网格", true, true, false},
    {"mesh.box_select", "BoxSelectComponents", "框选组件", "Box Select", "网格", true, true, false},
    {"mesh.toggle_xray", "ToggleXRay", "切换穿透选择", "Toggle X-Ray", "网格", true, true, false},
    {"mesh.extrude_region", "ExtrudeRegion", "区域挤出", "Extrude Region", "网格", true, true,
     true},
    {"mesh.inset_face", "InsetFace", "面内插", "Inset Face", "网格", true, true, true},
    {"mesh.bevel_edge", "BevelEdge", "单段边倒角", "Bevel Edge", "网格", true, true, true},
    {"modifier.subdivision_add", "SubdivisionAdd", "添加细分", "Add Subdivision", "修改器", true,
     false, true},
    {"modifier.subdivision_apply", "SubdivisionApply", "应用细分及前置Mirror", "Apply Subdivision",
     "修改器", true, false, true},
    {"modifier.subdivision_remove", "SubdivisionRemove", "删除细分", "Remove Subdivision", "修改器",
     true, false, true},
    {"collection.create", "CreateCollection", "新建集合", "Create Collection", "集合", false, false,
     true},
    {"mesh.loop_cut", "LoopCut", "环切并滑移", "Loop Cut and Slide", "网格", true, true, true},
    {"mesh.fill_face", "FillFaces", "补面", "Fill Face", "网格", true, true, true},
    {"mesh.delete_menu", "DeleteComponentsMenu", "删除组件菜单", "Delete Components Menu", "网格",
     true, true, false},
    {"mesh.delete_vertices", "DeleteVertices", "删除点及关联面", "Delete Vertices", "网格", true,
     true, true},
    {"mesh.delete_edges", "DeleteEdges", "删除边及关联面", "Delete Edges", "网格", true, true,
     true},
    {"mesh.delete_faces", "DeleteFaces", "删除面", "Delete Faces", "网格", true, true, true},
    {"mesh.translate", "TransformComponentsMove", "移动组件", "Translate Components", "网格", true,
     true, true},
    {"mesh.rotate", "TransformComponentsRotate", "旋转组件", "Rotate Components", "网格", true,
     true, true},
    {"mesh.scale", "TransformComponentsScale", "缩放组件", "Scale Components", "网格", true, true,
     true},
    {"view.toggle_overlays", "ToggleOverlays", "显示/隐藏覆盖层", "Toggle Overlays", "视口", false,
     true, false},
    {"object.add_cube", "CreateCube", "添加立方体", "Add Cube", "对象", false, false, true},
    {"object.add_sphere", "CreateSphere", "添加球体", "Add Sphere", "对象", false, false, true},
    {"object.add_plane", "CreatePlane", "添加平面", "Add Plane", "对象", false, false, true},
    {"object.add_empty", "CreateEmpty", "添加空对象", "Add Empty", "对象", false, false, true},
    {"object.add_camera", "CreateCamera", "添加相机", "Add Camera", "对象", false, false, true},
    {"object.add_light", "CreateDirectionalLight", "添加方向光", "Add Directional Light", "对象",
     false, false, true},
    {"object.duplicate", "Duplicate", "复制对象", "Duplicate Object", "对象", true, false, true},
    {"object.delete", "Delete", "删除对象", "Delete Object", "对象", true, false, true},
    {"object.translate", "TransformMove", "移动对象（模态）", "Translate Object", "对象", true,
     true, true},
    {"object.rotate", "TransformRotate", "旋转对象（模态）", "Rotate Object", "对象", true, true,
     true},
    {"object.scale", "TransformScale", "缩放对象（模态）", "Scale Object", "对象", true, true,
     true},
    {"history.undo", "Undo", "撤销", "Undo", "历史", false, false, false},
    {"history.redo", "Redo", "重做", "Redo", "历史", false, false, false},
    {"history.adjust_last", "AdjustLastOperation", "调整上一步", "Adjust Last Operation", "历史",
     true, true, false},
    {"view.front", "FrontView", "前视图", "Front View", "视口", false, true, false},
    {"view.right", "RightView", "右视图", "Right View", "视口", false, true, false},
    {"view.top", "TopView", "顶视图", "Top View", "视口", false, true, false},
    {"view.orbit", "OrbitView", "自由观察", "Orbit View", "视口", false, true, false},
    {"view.orthographic", "OrthographicView", "切换正交投影", "Toggle Orthographic", "视口", false,
     true, false},
    {"view.focus_selected", "FocusSelection", "聚焦所选对象", "Frame Selected", "视口", true, true,
     false},
    {"view.focus_all", "FocusAll", "聚焦全部可见对象", "Frame All", "视口", false, true, false},
    {"view.camera_preview", "ToggleCameraPreview", "相机预览 / 返回编辑视图", "Toggle Camera View",
     "视口", false, true, false},
    {"tool.select", "SelectTool", "选择工具", "Select Tool", "工具", false, true, false},
    {"tool.move", "MoveTool", "移动手柄", "Move Gizmo", "工具", false, true, false},
    {"tool.rotate", "RotateTool", "旋转手柄", "Rotate Gizmo", "工具", false, true, false},
    {"tool.scale", "ScaleTool", "缩放手柄", "Scale Gizmo", "工具", false, true, false},
    {"transform.space_world", "WorldTransformSpace", "世界坐标系", "Global Space", "工具", false,
     true, false},
    {"transform.space_local", "LocalTransformSpace", "局部坐标系", "Local Space", "工具", false,
     true, false},
    {"transform.toggle_snap", "SnapTransform", "切换步进吸附", "Toggle Increment Snap", "工具",
     false, true, false},
    {"workbench.toggle_toolbar", "ToggleViewportToolbar", "显示/隐藏工具条", "Toggle Toolbar",
     "工作台", false, true, false},
    {"workbench.toggle_sidebar", "ToggleViewportSidebar", "显示/隐藏侧栏", "Toggle Sidebar",
     "工作台", false, true, false},
    {"workbench.toggle_area_maximized", "ToggleAreaMaximized", "最大化当前区域 / 还原",
     "Toggle Area Maximized", "工作台", false, false, false},
};

int matchRank(const OperatorDescriptor& item, const QString& query) {
    if (query.isEmpty() || item.chineseName.compare(query, Qt::CaseInsensitive) == 0) {
        return 0;
    }
    if (item.chineseName.startsWith(query, Qt::CaseInsensitive)) {
        return 1;
    }
    if (item.chineseName.contains(query, Qt::CaseInsensitive)) {
        return 2;
    }
    if (item.englishName.compare(query, Qt::CaseInsensitive) == 0) {
        return 3;
    }
    if (item.englishName.startsWith(query, Qt::CaseInsensitive)) {
        return 4;
    }
    if (item.englishName.contains(query, Qt::CaseInsensitive)) {
        return 5;
    }
    if (item.action) {
        for (const auto& shortcut : item.action->shortcuts()) {
            if (shortcut.toString(QKeySequence::PortableText)
                    .contains(query, Qt::CaseInsensitive)) {
                return 6;
            }
        }
    }
    return item.id.contains(query, Qt::CaseInsensitive) ? 7 : -1;
}
} // namespace

OperatorRegistry::OperatorRegistry(QMainWindow& window, SceneViewModel& model, QObject* parent)
    : QObject(parent), model_(model) {
    connect(&model_, &SceneViewModel::documentReset, this, [this] {
        ++documentGeneration_;
    });
    for (const auto& definition : kDefinitions) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(definition.actionName));
        if (!action) {
            continue;
        }
        OperatorDescriptor descriptor;
        descriptor.id = QString::fromLatin1(definition.id);
        descriptor.chineseName = QString::fromUtf8(definition.chineseName);
        descriptor.englishName = QString::fromLatin1(definition.englishName);
        descriptor.category = QString::fromUtf8(definition.category);
        descriptor.iconKey = action->objectName();
        descriptor.defaultShortcut = action->shortcut();
        descriptor.action = action;
        descriptor.undoable = definition.undoable;
        if (descriptor.id == QStringLiteral("mesh.extrude_region")) {
            descriptor.reopenable = true;
            descriptor.parameterSchema.insert(
                QStringLiteral("worldOffset"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("vector3")},
                            {QStringLiteral("space"), QStringLiteral("world")},
                            {QStringLiteral("label"), QStringLiteral("全局位移")}});
        }
        if (descriptor.id == QStringLiteral("mesh.inset_face")) {
            descriptor.reopenable = true;
            descriptor.parameterSchema.insert(
                QStringLiteral("thickness"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("number")},
                            {QStringLiteral("space"), QStringLiteral("local")},
                            {QStringLiteral("label"), QStringLiteral("内插厚度")}});
        }
        if (descriptor.id == QStringLiteral("mesh.bevel_edge")) {
            descriptor.reopenable = true;
            descriptor.parameterSchema.insert(
                QStringLiteral("width"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("number")},
                            {QStringLiteral("space"), QStringLiteral("local")},
                            {QStringLiteral("label"), QStringLiteral("倒角宽度")}});
        }
        descriptor.repeatable = descriptor.reopenable;
        descriptor.poll = [this, selection = definition.selection,
                           viewportOnly = definition.viewportOnly](const OperatorContext& context) {
            if (viewportOnly && context.area != InputArea::Viewport &&
                !(context.keymap == EditorKeymap::Legacy && context.area == InputArea::Outliner)) {
                return QStringLiteral("仅适用于 3D 视口；请从视口调用。");
            }
            if (selection && (!model_.scene()->find(context.selectedEntity) ||
                              context.selectedEntity == core::kInvalidEntity)) {
                return QStringLiteral("需要先选择一个对象。");
            }
            if (selection && context.selectedEntity != model_.selection()->selectedEntity()) {
                return QStringLiteral("调用时的选择已变化，请重新打开操作入口。");
            }
            return QString();
        };
        action->setProperty("operatorId", descriptor.id);
        descriptors_.append(std::move(descriptor));
    }
}

OperatorContext OperatorRegistry::captureContext(InputArea area, EditorKeymap keymap) const {
    OperatorContext context{&model_, documentGeneration_, area, keymap,
                            model_.selection()->selectedEntity()};
    context.editMode = model_.isEditMode();
    context.selectionRevision = model_.componentSelectionRevision();
    if (context.editMode) {
        context.editableMesh = model_.scene()->find(model_.editedEntity())->editableMesh;
        context.meshRevision =
            model_.scene()->editableMesh(context.editableMesh)->evaluationRevision;
    }
    return context;
}

const OperatorDescriptor* OperatorRegistry::descriptor(const QString& id) const {
    for (const auto& descriptor : descriptors_) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

QString OperatorRegistry::disabledReason(const QString& id, const OperatorContext& context) const {
    const auto* item = descriptor(id);
    if (!item || !item->action) {
        return QStringLiteral("该操作尚未实现或已不可用。");
    }
    if (context.model != &model_ || context.documentGeneration != documentGeneration_) {
        return QStringLiteral("调用时的文档已切换，请重新打开操作入口。");
    }
    if (context.editMode != model_.isEditMode()) {
        return QStringLiteral("调用时的对象 / 编辑模式已变化，请重新打开操作入口。");
    }
    if (context.selectionRevision != model_.componentSelectionRevision()) {
        return QStringLiteral("调用后发生过模式或组件选择变化，请重新打开操作入口。");
    }
    if (context.editMode) {
        const auto current = captureContext(context.area, context.keymap);
        if (context.editableMesh != current.editableMesh ||
            context.meshRevision != current.meshRevision ||
            context.selectionRevision != current.selectionRevision) {
            return QStringLiteral("调用时的网格或组件选择已变化，请重新打开操作入口。");
        }
        if (id.startsWith(QStringLiteral("object.")) || id == QStringLiteral("tool.move") ||
            id == QStringLiteral("tool.rotate") || id == QStringLiteral("tool.scale")) {
            return QStringLiteral("当前是编辑模式；此入口只处理整对象，请先返回对象模式。");
        }
    }
    if (id == QStringLiteral("mesh.toggle_edit_mode")) {
        const auto reason = model_.editModeDisabledReason();
        if (!reason.isEmpty()) {
            return reason;
        }
    } else if (id.startsWith(QStringLiteral("mesh.")) && !context.editMode) {
        return QStringLiteral("需要先进入编辑模式。");
    }
    if (id.startsWith(QStringLiteral("modifier.mirror_"))) {
        const auto* node = model_.scene()->find(context.selectedEntity);
        if (!node || node->editableMesh == 0)
            return QStringLiteral("请先将对象转换为可编辑网格。");
        const bool hasMirror = model_.mirrorOptions(context.selectedEntity).has_value();
        if (id == QStringLiteral("modifier.mirror_add") && hasMirror)
            return QStringLiteral("当前对象已有 Mirror。");
        if (id != QStringLiteral("modifier.mirror_add") && !hasMirror)
            return QStringLiteral("当前对象没有 Mirror。");
    }
    if (id.startsWith(QStringLiteral("modifier.subdivision_"))) {
        const auto* node = model_.scene()->find(context.selectedEntity);
        if (!node || node->editableMesh == 0)
            return QStringLiteral("请先将对象转换为可编辑网格。");
        const bool hasSubdivision = model_.subdivisionOptions(context.selectedEntity).has_value();
        if (id == QStringLiteral("modifier.subdivision_add") && hasSubdivision)
            return QStringLiteral("当前对象已有细分。");
        if (id != QStringLiteral("modifier.subdivision_add") && !hasSubdivision)
            return QStringLiteral("当前对象没有细分。");
    }
    if (id == QStringLiteral("mesh.extrude_region")) {
        const auto reason = model_.extrudeRegionDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    if (id == QStringLiteral("history.adjust_last")) {
        const auto reason = model_.lastOperationDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    if (id == QStringLiteral("history.repeat_last")) {
        const auto reason = model_.repeatLastOperationDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    if (id == QStringLiteral("mesh.inset_face")) {
        const auto reason = model_.insetFaceDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    if (id == QStringLiteral("mesh.bevel_edge")) {
        const auto reason = model_.bevelEdgeDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    if (id == QStringLiteral("mesh.fill_face")) {
        const auto reason = model_.fillFaceDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    for (const auto& [operation, domain] :
         {std::pair{"mesh.delete_vertices", SelectionDomain::Vertex},
          {"mesh.delete_edges", SelectionDomain::Edge},
          {"mesh.delete_faces", SelectionDomain::Face}}) {
        if (id == QLatin1String(operation)) {
            const auto reason = model_.deleteComponentsDisabledReason(domain);
            if (!reason.isEmpty())
                return reason;
        }
    }
    if ((id == QStringLiteral("mesh.select_loop") || id == QStringLiteral("mesh.select_ring")) &&
        (model_.componentSelection().domain() != SelectionDomain::Edge ||
         !model_.componentSelection().activeId()))
        return QStringLiteral("请在边选择模式选中一个活动源边。");
    const bool areaOperation =
        id == QStringLiteral("workbench.toggle_area_maximized") && context.area != InputArea::None;
    if (id.startsWith(QStringLiteral("cursor.")) &&
        id != QStringLiteral("cursor.toggle_visibility") && model_.previewCamera() != 0) {
        return QStringLiteral("相机预览中不能定位游标，请先返回编辑视图。");
    }
    if (id == QStringLiteral("cursor.to_selection") && !model_.cursorSelectionCenter()) {
        return QStringLiteral("请先选择对象或网格组件。");
    }
    if (id == QStringLiteral("selection.to_cursor")) {
        const auto reason = model_.selectionToCursorDisabledReason();
        if (!reason.isEmpty())
            return reason;
    }
    if (!areaOperation && context.area != InputArea::Viewport &&
        context.area != InputArea::Outliner) {
        return QStringLiteral("请从 3D 视口或场景树调用此操作。");
    }
    if (id.startsWith(QStringLiteral("view.")) && id != QStringLiteral("view.camera_preview") &&
        model_.previewCamera() != core::kInvalidEntity) {
        return QStringLiteral("相机预览中不能改变编辑视角，请先返回编辑视图。");
    }
    const auto reason = item->poll(context);
    if (!reason.isEmpty()) {
        return reason;
    }
    if ((id == QStringLiteral("view.hide_selection") ||
         (id == QStringLiteral("view.local_view") && !model_.viewportVisibility().localRoot)) &&
        (!model_.viewportVisibility().isVisible(*model_.scene(), context.selectedEntity) ||
         (id == QStringLiteral("view.hide_selection") && model_.isEditMode() &&
          model_.componentSelection().selectedIds().empty()))) {
        return QStringLiteral("请选择可见的对象或组件。");
    }
    if (id == QStringLiteral("view.focus_all") &&
        !renderer_gl::RayCaster::sceneBounds(*model_.scene(), *model_.assets(),
                                             model_.viewportVisibility())
             .isValid()) {
        return QStringLiteral("没有可见对象可聚焦。");
    }
    if (id == QStringLiteral("view.camera_preview") && model_.previewCamera() == 0 &&
        model_.previewCameraCandidate() == 0) {
        return QStringLiteral("没有可见相机：请创建相机或在场景树显示已有相机。");
    }
    if (id == QStringLiteral("object.translate") || id == QStringLiteral("object.rotate") ||
        id == QStringLiteral("object.scale")) {
        if (model_.previewCamera() != 0 ||
            !model_.viewportVisibility().isVisible(*model_.scene(), context.selectedEntity)) {
            return QStringLiteral("请在编辑视图选择一个可见对象，再启动变换。");
        }
    }
    if (id == QStringLiteral("view.focus_selected") &&
        !renderer_gl::RayCaster::worldBounds(*model_.scene(), *model_.assets(),
                                             context.selectedEntity, model_.viewportVisibility())
             .isValid()) {
        return QStringLiteral("所选对象及其后代没有可见几何，无法聚焦。");
    }
    if ((id == QStringLiteral("mesh.translate") || id == QStringLiteral("mesh.rotate") ||
         id == QStringLiteral("mesh.scale")) &&
        model_.componentSelection().selectedIds().empty()) {
        return QStringLiteral("请先选择需要变换的点、边或面。");
    }
    if (!item->action->isEnabled()) {
        return id == QStringLiteral("history.undo")   ? QStringLiteral("没有可撤销的操作。")
               : id == QStringLiteral("history.redo") ? QStringLiteral("没有可重做的操作。")
                                                      : QStringLiteral("当前状态不支持此操作。");
    }
    return {};
}

QVector<OperatorMatch> OperatorRegistry::search(const QString& query,
                                                const OperatorContext& context) const {
    QVector<OperatorMatch> matches;
    for (const auto& item : descriptors_) {
        const int rank = matchRank(item, query.trimmed());
        if (rank >= 0) {
            matches.append({item.id, disabledReason(item.id, context), rank});
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return left.rank < right.rank;
    });
    return matches;
}

InputArea OperatorRegistry::executionArea() const {
    return executionArea_;
}

bool OperatorRegistry::execute(const QString& id, const OperatorContext& context) {
    auto error = disabledReason(id, context);
    if (!error.isEmpty()) {
        emit executionFailed(error);
        return false;
    }
    // 复用菜单同一个意图入口；同步失败仍通过既有 ViewModel 消息通道显示。
    const auto failure =
        connect(&model_, &SceneViewModel::operationFailed, this, [&error](const QString& reason) {
            error = reason;
        });
    {
        const QScopedValueRollback<InputArea> activeArea(executionArea_, context.area);
        descriptor(id)->action->trigger();
    }
    disconnect(failure);
    if (!error.isEmpty()) {
        emit executionFailed(error);
        return false;
    }
    emit executed(id);
    return true;
}

QString OperatorRegistry::areaName(InputArea area) {
    switch (area) {
        case InputArea::Viewport:
            return QStringLiteral("3D 视口");
        case InputArea::Outliner:
            return QStringLiteral("场景树");
        case InputArea::Properties:
            return QStringLiteral("属性编辑器");
        case InputArea::Console:
            return QStringLiteral("控制台");
        case InputArea::None:
            return QStringLiteral("无编辑区域");
    }
    return {};
}
} // namespace mini3d::editor
