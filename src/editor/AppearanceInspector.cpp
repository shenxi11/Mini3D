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

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
namespace mini3d::editor {
AppearanceInspector::AppearanceInspector(SceneViewModel& model, QWidget* parent)
    : QWidget(parent), model_(model) {
    auto* form = new QFormLayout(this);
    const auto vectorRow = [this, form](const QString& name, const QString& label, double minimum,
                                        std::array<QDoubleSpinBox*, 3>& values, bool surface) {
        auto* row = new QHBoxLayout;
        for (int axis = 0; axis < 3; ++axis) {
            auto* spin = new QDoubleSpinBox(this);
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
    intensity_ = new QDoubleSpinBox(this);
    ambient_ = new QDoubleSpinBox(this);
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
        auto* spin = new QDoubleSpinBox(this);
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
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this,
            &AppearanceInspector::refresh);
    connect(&model_, &SceneViewModel::operationFailed, this, &AppearanceInspector::refresh);
    refresh();
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
        selectedLight ? QStringLiteral("当前方向光：通过旋转调整照射方向。可见灯中编号最小的生效。")
        : hasLight    ? QStringLiteral("请选择方向光进行编辑。环境光为全局设置。")
                      : QStringLiteral("兼容场景光（无灯实体）。方向指向光源。"));
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker a(tint_[i]), b(direction_[i]), c(color_[i]);
        tint_[i]->setEnabled(geometry);
        tint_[i]->setValue(surface.tint[i]);
        direction_[i]->setValue(light.direction[i]);
        direction_[i]->setEnabled(!hasLight);
        color_[i]->setValue(light.color[i]);
        color_[i]->setEnabled(selectedLight || !hasLight);
    }
    const QSignalBlocker a(texture_), b(vertexColor_), c(intensity_), d(ambient_);
    texture_->setEnabled(geometry);
    vertexColor_->setEnabled(geometry);
    texture_->setChecked(surface.useTexture);
    vertexColor_->setChecked(surface.useVertexColor);
    intensity_->setValue(light.intensity);
    intensity_->setEnabled(selectedLight || !hasLight);
    ambient_->setValue(light.ambient);
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
}
} // namespace mini3d::editor
