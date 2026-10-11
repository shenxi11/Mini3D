/*
 * 模块名: AppearanceInspector
 * 功能概述: 绑定材质/光照编辑与只读刷新。
 * 对外接口: AppearanceInspector
 * 依赖关系: Qt Widgets、SceneViewModel
 * 输入输出: 数值与开关到可撤销意图。
 * 异常与错误: 控件限制输入范围，零方向由业务层拒绝。
 * 维护说明: 刷新使用信号阻断，不产生新历史。
 */
#include "AppearanceInspector.h"

#include "workbench/CommitSpinBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace mini3d::editor {
AppearanceInspector::AppearanceInspector(SceneViewModel& model, QWidget* parent)
    : QWidget(parent), model_(model) {
    auto* form = new QFormLayout(this);
    const auto createNumber = [this] {
        auto* field = new CommitSpinBox(this);
        connect(&model_, &SceneViewModel::operationFailed, field, &CommitSpinBox::rejectSubmission);
        return field;
    };
    form->addRow(new QLabel(QStringLiteral("无纹理 PBR（线性颜色，OPAQUE）"), this));
    materials_ = new QComboBox(this);
    materials_->setObjectName(QStringLiteral("PbrMaterialSelection"));
    form->addRow(QStringLiteral("材料"), materials_);
    createMaterial_ = new QPushButton(QStringLiteral("创建无纹理 PBR"), this);
    createMaterial_->setObjectName(QStringLiteral("CreatePbrMaterial"));
    assignMaterial_ = new QPushButton(QStringLiteral("分配给所选几何"), this);
    assignMaterial_->setObjectName(QStringLiteral("AssignPbrMaterial"));
    form->addRow(createMaterial_);
    form->addRow(assignMaterial_);
    roughness_ = new QLineEdit(this);
    roughness_->setObjectName(QStringLiteral("PbrRoughness"));
    roughness_->installEventFilter(this);
    form->addRow(QStringLiteral("基础 roughness"), roughness_);
    materialStatus_ = new QLabel(this);
    materialStatus_->setObjectName(QStringLiteral("PbrMaterialStatus"));
    materialStatus_->setWordWrap(true);
    form->addRow(materialStatus_);
    connect(createMaterial_, &QPushButton::clicked, this, &AppearanceInspector::createMaterial);
    connect(assignMaterial_, &QPushButton::clicked, this, &AppearanceInspector::assignMaterial);
    connect(materials_, &QComboBox::currentIndexChanged, this,
            &AppearanceInspector::refreshJ1Values);
    connect(roughness_, &QLineEdit::editingFinished, this, &AppearanceInspector::applyRoughness);
    form->addRow(new QLabel(QStringLiteral("Sun（物理方向光，局部 −Z）"), this));
    irradiance_ = new QLineEdit(this);
    irradiance_->setObjectName(QStringLiteral("SunIrradiance"));
    irradiance_->installEventFilter(this);
    irradiance_->setMaximumWidth(260);
    form->addRow(QStringLiteral("基础辐照度 W/m²"), irradiance_);
    sunStatus_ = new QLabel(this);
    sunStatus_->setObjectName(QStringLiteral("SunStatus"));
    sunStatus_->setWordWrap(true);
    form->addRow(sunStatus_);
    connect(irradiance_, &QLineEdit::editingFinished, this, &AppearanceInspector::applySun);
    const auto vectorRow = [this, form,
                            createNumber](const QString& name, const QString& label, double minimum,
                                          std::array<QDoubleSpinBox*, 3>& values, bool surface) {
        auto* row = new QVBoxLayout;
        for (int axis = 0; axis < 3; ++axis) {
            auto* spin = createNumber();
            spin->setObjectName(name + QString::number(axis));
            spin->setRange(minimum, minimum < 0 ? 10 : 1);
            spin->setDecimals(3);
            spin->setSingleStep(0.1);
            spin->setKeyboardTracking(false);
            spin->setPrefix(
                QString(surface || name == QStringLiteral("LightColor") ? "RGB" : "XYZ")[axis] +
                QStringLiteral(" "));
            values[axis] = spin;
            row->addWidget(spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, surface] {
                if (surface) {
                    applySurface();
                } else {
                    applyLighting();
                }
            });
        }
        form->addRow(label, row);
    };
    form->addRow(new QLabel(QStringLiteral("表面材质（所选几何）"), this));
    vectorRow(QStringLiteral("Tint"), QStringLiteral("叠加色"), 0, tint_, true);
    texture_ = new QCheckBox(QStringLiteral("使用纹理"), this);
    texture_->setObjectName(QStringLiteral("UseTexture"));
    vertexColor_ = new QCheckBox(QStringLiteral("使用顶点色"), this);
    vertexColor_->setObjectName(QStringLiteral("UseVertexColor"));
    form->addRow(texture_);
    form->addRow(vertexColor_);
    connect(texture_, &QCheckBox::toggled, this, &AppearanceInspector::applySurface);
    connect(vertexColor_, &QCheckBox::toggled, this, &AppearanceInspector::applySurface);
    lightingLabel_ = new QLabel(this);
    lightingLabel_->setWordWrap(true);
    form->addRow(lightingLabel_);
    vectorRow(QStringLiteral("LightDirection"), QStringLiteral("光源方向"), -10, direction_, false);
    vectorRow(QStringLiteral("LightColor"), QStringLiteral("光源颜色"), 0, color_, false);
    intensity_ = createNumber();
    ambient_ = createNumber();
    intensity_->setObjectName(QStringLiteral("LightIntensity"));
    ambient_->setObjectName(QStringLiteral("Ambient"));
    intensity_->setRange(0, 10);
    ambient_->setRange(0, 1);
    for (auto* spin : {intensity_, ambient_}) {
        spin->setSingleStep(0.1);
        spin->setDecimals(3);
        spin->setKeyboardTracking(false);
    }
    connect(intensity_, &QDoubleSpinBox::valueChanged, this, &AppearanceInspector::applyLighting);
    connect(ambient_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        auto lighting = model_.scene()->lighting();
        lighting.ambient = static_cast<float>(value);
        model_.setLighting(lighting);
    });
    form->addRow(QStringLiteral("强度"), intensity_);
    form->addRow(QStringLiteral("环境光"), ambient_);
    form->addRow(new QLabel(QStringLiteral("相机（所选对象）"), this));
    const QString names[] = {QStringLiteral("CameraFov"), QStringLiteral("CameraNear"),
                             QStringLiteral("CameraFar")};
    const QString labels[] = {QStringLiteral("垂直视角（度）"), QStringLiteral("近裁剪"),
                              QStringLiteral("远裁剪")};
    for (int i = 0; i < 3; ++i) {
        auto* spin = createNumber();
        spin->setObjectName(names[i]);
        spin->setDecimals(3);
        spin->setRange(i == 0 ? 1 : 0.001, i == 0 ? 179 : 1000000);
        spin->setKeyboardTracking(false);
        camera_[i] = spin;
        form->addRow(labels[i], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &AppearanceInspector::applyCamera);
    }
    preview_ = new QPushButton(QStringLiteral("预览所选相机"), this);
    preview_->setObjectName(QStringLiteral("PreviewCamera"));
    exitPreview_ = new QPushButton(QStringLiteral("返回编辑视图"), this);
    exitPreview_->setObjectName(QStringLiteral("ExitCameraPreview"));
    form->addRow(preview_);
    form->addRow(exitPreview_);
    previewLabel_ = new QLabel(this);
    previewLabel_->setObjectName(QStringLiteral("CameraPreviewStatus"));
    previewLabel_->setWordWrap(true);
    form->addRow(previewLabel_);
    connect(preview_, &QPushButton::clicked, this, [this] {
        model_.setPreviewCamera(model_.selection()->selectedEntity());
    });
    connect(exitPreview_, &QPushButton::clicked, this, [this] {
        model_.setPreviewCamera(0);
    });
    connect(&model_, &SceneViewModel::previewCameraChanged, this, &AppearanceInspector::refresh);
    connect(&model_, &SceneViewModel::sceneChanged, this, &AppearanceInspector::refresh);
    connect(&model_, &SceneViewModel::animationPoseChanged, this,
            &AppearanceInspector::refreshJ1Values);
    connect(&model_, &SceneViewModel::animationSessionChanged, this,
            &AppearanceInspector::refreshJ1Values);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this, [this] {
        refresh();
        const auto* node = model_.scene()->find(model_.selection()->selectedEntity());
        if (node && node->materialSlots.size() == 1)
            materials_->setCurrentIndex(materials_->findData(
                QVariant::fromValue<qulonglong>(node->materialSlots.front().material.value)));
    });
    connect(&model_, &SceneViewModel::operationFailed, this, &AppearanceInspector::refresh);
    refresh();
}
core::MaterialId AppearanceInspector::selectedMaterial() const {
    return core::MaterialId{materials_->currentData().toULongLong()};
}
bool AppearanceInspector::eventFilter(QObject* object, QEvent* event) {
    if ((object == roughness_ || object == irradiance_) && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        auto* field = object == roughness_ ? roughness_ : irradiance_;
        const auto value = object == roughness_ ? roughnessOriginal_ : irradianceOriginal_;
        field->setText(field->locale().toString(value, 'g', 17));
        return true;
    }
    return QWidget::eventFilter(object, event);
}
void AppearanceInspector::createMaterial() {
    api::MaterialCreateRequest request;
    request.document = model_.apiDocumentState().document;
    request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
    const auto result = model_.createMaterialExplicit(request);
    if (!result.hasValue()) {
        materialStatus_->setText(result.error->message);
        return;
    }
    materials_->setCurrentIndex(materials_->findData(
        QVariant::fromValue<qulonglong>(result.value->materialId.value)));
    refreshJ1Values();
}
void AppearanceInspector::assignMaterial() {
    api::EntityAssignMaterialRequest request;
    request.document = model_.apiDocumentState().document;
    request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
    request.entityId = model_.selection()->selectedEntity();
    request.materialId = selectedMaterial();
    const auto result = model_.assignMaterialExplicit(request);
    if (!result.hasValue())
        materialStatus_->setText(result.error->message);
}
void AppearanceInspector::applyRoughness() {
    bool valid = false;
    const auto input = roughness_->text();
    const auto value = roughness_->locale().toDouble(input, &valid);
    if (!valid || !std::isfinite(value)) {
        materialStatus_->setText(QStringLiteral("请输入有效的有限 roughness 数值；输入未提交。"));
        return;
    }
    api::MaterialGetRequest read;
    read.document = model_.apiDocumentState().document;
    read.materialId = selectedMaterial();
    const auto material = model_.getMaterialExplicit(read);
    if (!material.hasValue()) {
        roughness_->setText(input);
        materialStatus_->setText(material.error->message);
        return;
    }
    api::MaterialUpdateRequest request;
    request.document = model_.apiDocumentState().document;
    request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
    request.materialId = read.materialId;
    // 只改 roughness；其余完整作者参数直接沿用，避免控件显示舍入污染作者值。
    request.parameters = material.value->material.parameters;
    request.parameters.roughness = value;
    const auto result = model_.updateMaterialExplicit(request);
    if (!result.hasValue()) {
        roughness_->setText(input);
        materialStatus_->setText(result.error->message);
    }
}
void AppearanceInspector::applySun() {
    bool valid = false;
    const auto input = irradiance_->text();
    const auto value = irradiance_->locale().toDouble(input, &valid);
    if (!valid || !std::isfinite(value)) {
        sunStatus_->setText(QStringLiteral("请输入有效的有限辐照度数值；输入未提交。"));
        return;
    }
    const auto* node = model_.scene()->find(model_.selection()->selectedEntity());
    if (!node || !node->sun)
        return;
    api::SunUpdateRequest request;
    request.document = model_.apiDocumentState().document;
    request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
    request.entityId = node->id;
    request.sun = *node->sun;
    request.sun.irradiance = value;
    const auto result = model_.updateSunExplicit(request);
    if (!result.hasValue()) {
        irradiance_->setText(input);
        sunStatus_->setText(result.error->message);
    }
}
void AppearanceInspector::refreshJ1Values() {
    const auto* node = model_.scene()->find(model_.selection()->selectedEntity());
    const auto materialId = selectedMaterial();
    const auto* material = model_.scene()->materialData().find(materialId);
    const bool editable = model_.apiBusyReasons().isEmpty();
    const bool geometry = node && (node->meshRenderer || node->primitive != core::PrimitiveKind::Empty);
    createMaterial_->setEnabled(editable);
    assignMaterial_->setEnabled(editable && geometry && node->materialSlots.size() <= 1 && material);
    const QSignalBlocker roughnessBlock(roughness_), sunBlock(irradiance_);
    roughness_->setEnabled(editable && material && material->textures.empty());
    roughnessOriginal_ = material ? material->parameters.roughness : 1;
    roughness_->setText(roughness_->locale().toString(roughnessOriginal_, 'g', 17));
    irradiance_->setEnabled(editable && node && node->sun);
    irradianceOriginal_ = node && node->sun ? node->sun->irradiance : 1;
    irradiance_->setText(irradiance_->locale().toString(irradianceOriginal_, 'g', 17));
    const auto pose = model_.installedAnimationPose();
    const auto* content = pose && pose->sceneState ? pose->sceneState->content.get() : nullptr;
    const auto displayed = [&pose](double value) {
        return QStringLiteral("当前求值 %1（帧 %2，求值 ID %3）")
            .arg(value, 0, 'g', 17).arg(pose->sceneState->source.frameTime, 0, 'g', 17)
            .arg(pose->identity.evaluationId);
    };
    if (!material) {
        materialStatus_->setText(QStringLiteral("先创建材料，再分配给几何；共享材料的 roughness 同步变化。"));
    } else {
        QString current = model_.animationMode() == AnimationMode::Base
            ? QStringLiteral("基础模式显示作者值") : QStringLiteral("当前求值不可用");
        if (content) {
            const auto found = content->materials.find(materialId);
            if (found != content->materials.end())
                current = displayed(found->second.effective.roughness);
        }
        const bool assigned = node && node->materialSlots.size() == 1 &&
            node->materialSlots.front().material == materialId;
        materialStatus_->setText(QStringLiteral("材料 %1，%2；作者 roughness %3；%4。")
            .arg(materialId.value).arg(assigned ? QStringLiteral("已分配") : QStringLiteral("未分配给所选对象"))
            .arg(material->parameters.roughness, 0, 'g', 17).arg(current));
    }
    if (!node || !node->sun) {
        sunStatus_->setText(QStringLiteral("请选择 Sun；旧方向光的无量纲强度不转换为 W/m²。"));
    } else {
        QString current = model_.animationMode() == AnimationMode::Base
            ? QStringLiteral("基础模式显示作者值") : QStringLiteral("当前求值不可用");
        if (content) {
            const auto found = std::find_if(content->visibleSuns.begin(), content->visibleSuns.end(),
                [node](const auto& value) { return value.entity == node->id; });
            if (found != content->visibleSuns.end())
                current = displayed(found->effective.irradiance);
        }
        sunStatus_->setText(QStringLiteral("作者辐照度 %1 W/m²；%2。")
            .arg(node->sun->irradiance, 0, 'g', 17).arg(current));
    }
}
void AppearanceInspector::applySurface() {
    core::SurfaceStyle value;
    for (int i = 0; i < 3; ++i) {
        value.tint[i] = static_cast<float>(tint_[i]->value());
    }
    value.useTexture = texture_->isChecked();
    value.useVertexColor = vertexColor_->isChecked();
    model_.setSurface(model_.selection()->selectedEntity(), value);
}
void AppearanceInspector::applyLighting() {
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    if (node && node->light) {
        core::LightComponent light;
        for (int i = 0; i < 3; ++i) {
            light.color[i] = static_cast<float>(color_[i]->value());
        }
        light.intensity = static_cast<float>(intensity_->value());
        model_.setLight(id, light);
        return;
    }
    core::Lighting value;
    for (int i = 0; i < 3; ++i) {
        value.direction[i] = static_cast<float>(direction_[i]->value());
        value.color[i] = static_cast<float>(color_[i]->value());
    }
    value.intensity = static_cast<float>(intensity_->value());
    value.ambient = static_cast<float>(ambient_->value());
    model_.setLighting(value);
}
void AppearanceInspector::applyCamera() {
    model_.setCamera(model_.selection()->selectedEntity(),
                     {static_cast<float>(camera_[0]->value()),
                      static_cast<float>(camera_[1]->value()),
                      static_cast<float>(camera_[2]->value())});
}
void AppearanceInspector::refresh() {
    const auto* node = model_.scene()->find(model_.selection()->selectedEntity());
    const bool geometry =
        node && (node->meshRenderer || node->primitive != core::PrimitiveKind::Empty);
    const auto surface = node ? node->surface : core::SurfaceStyle{};
    auto light = model_.scene()->lighting();
    const bool selectedLight = node && node->light.has_value();
    const bool selectedSun = node && node->sun.has_value();
    const bool hasPbr = node && !node->materialSlots.empty();
    auto selected = selectedMaterial();
    if (!selected.isValid() && node && node->materialSlots.size() == 1)
        selected = node->materialSlots.front().material;
    {
        const QSignalBlocker blocker(materials_);
        materials_->clear();
        for (const auto& [id, asset] : model_.scene()->materialData().assets)
            if (asset.material)
                materials_->addItem(QStringLiteral("PBR [%1]").arg(id.value),
                    QVariant::fromValue<qulonglong>(id.value));
        const auto index = materials_->findData(QVariant::fromValue<qulonglong>(selected.value));
        if (index >= 0)
            materials_->setCurrentIndex(index);
    }
    bool hasLight = false;
    for (const auto& entity : model_.scene()->nodes()) {
        hasLight = hasLight || entity.light.has_value();
    }
    if (selectedLight) {
        light.color = node->light->color;
        light.intensity = node->light->intensity;
        light.direction = model_.scene()->worldRotation(node->id) * glm::vec3(0, 0, 1);
    }
    lightingLabel_->setText(
        selectedSun ? QStringLiteral("所选 Sun 的物理参数在上方编辑；下方为兼容光照。")
        : selectedLight ? QStringLiteral("当前方向光：通过旋转调整照射方向。可见灯中编号最小的生效。")
        : hasLight    ? QStringLiteral("请选择方向光进行编辑。环境光为全局设置。")
                      : QStringLiteral("兼容场景光（无灯实体）。方向指向光源。"));
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker a(tint_[i]), b(direction_[i]), c(color_[i]);
        tint_[i]->setEnabled(geometry && !hasPbr);
        tint_[i]->setValue(surface.tint[i]);
        direction_[i]->setValue(light.direction[i]);
        direction_[i]->setEnabled(!hasLight && !selectedSun);
        color_[i]->setValue(light.color[i]);
        color_[i]->setEnabled(selectedLight || (!hasLight && !selectedSun));
    }
    const QSignalBlocker a(texture_), b(vertexColor_), c(intensity_), d(ambient_);
    texture_->setEnabled(geometry && !hasPbr);
    vertexColor_->setEnabled(geometry && !hasPbr);
    texture_->setChecked(surface.useTexture);
    vertexColor_->setChecked(surface.useVertexColor);
    intensity_->setValue(light.intensity);
    intensity_->setEnabled(selectedLight || (!hasLight && !selectedSun));
    ambient_->setValue(light.ambient);
    ambient_->setEnabled(!selectedSun);
    const auto camera = node && node->camera ? *node->camera : core::CameraComponent{};
    const float values[] = {camera.fieldOfView, camera.nearPlane, camera.farPlane};
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker blocker(camera_[i]);
        camera_[i]->setEnabled(node && node->camera);
        camera_[i]->setValue(values[i]);
    }
    preview_->setEnabled(node && node->camera && model_.scene()->isVisible(node->id));
    exitPreview_->setEnabled(model_.previewCamera() != 0);
    const auto* previewNode = model_.scene()->find(model_.previewCamera());
    previewLabel_->setText(
        previewNode
            ? QStringLiteral("只读预览：%1。已暂停视口导航、拾取和移动；在视口按 Esc 退出。")
                  .arg(QString::fromStdString(previewNode->name))
            : QStringLiteral("编辑视图。请从场景树选择相机或灯光对象。"));
    refreshJ1Values();
}
} // namespace mini3d::editor
