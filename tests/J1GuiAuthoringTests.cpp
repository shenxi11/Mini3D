/*
 * 模块名: J1GuiAuthoringTests
 * 功能概述: 用真实控件验证 J1 标量创作，并用独立 MainWindow 完成空白到保存重开的作品。
 * 对外接口: Catch2 [j1-gui-widget]、[j1-gui-ui] 与 [j1-joint-ui]。
 * 依赖关系: AppearanceInspector、AnimationTimeline、MainWindow、真实 ViewModel 与 Qt Test。
 * 输入输出: 菜单、树选区和控件输入到作者值、稳定身份、唯一历史及新绘制证据。
 * 异常与错误: 非法输入不得录制旧值；对话框超时只取消本测试的对话框。
 * 维护说明: 小控件用例无 GL；整窗用例须真实显示，产物仅写明确的 E 盘测试根。
 */
#include "core/SceneSerializer.h"
#include "editor/AnimationTimeline.h"
#include "editor/AppearanceInspector.h"
#include "editor/MainWindow.h"
#include "editor/SceneTreeModel.h"
#include "editor/SceneViewModel.h"
#include "editor/api/EditorApiService.h"
#include "editor/automation/FilePathPolicy.h"
#include "editor/output/ImageOutputService.h"
#include "editor/workbench/CommitSpinBox.h"
#include "renderer_gl/ImageFrameOutput.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QOpenGLContext>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

using namespace mini3d;
namespace {
namespace api = editor::api;

template <typename Request> Request mutation(const editor::SceneViewModel& model) {
    Request request;
    request.document = model.apiDocumentState().document;
    request.expectedDocumentRevision = model.apiDocumentState().documentRevision;
    return request;
}
template <typename Widget> Widget* control(QWidget& root, const char* name) {
    auto* widget = root.findChild<Widget*>(QString::fromLatin1(name));
    REQUIRE(widget);
    return widget;
}
void enterNumber(QWidget& root, const char* name, double value) {
    auto* field = control<editor::CommitSpinBox>(root, name);
    REQUIRE(field->isEnabled());
    auto* input = field->findChild<QLineEdit*>();
    REQUIRE(input);
    input->selectAll();
    QTest::keyClicks(input, QString::number(value, 'g', 17));
    QTest::keyClick(input, Qt::Key_Return);
}
void enterScalarText(QWidget& root, const char* name, const QString& text) {
    auto* field = control<QLineEdit>(root, name);
    REQUIRE(field->isEnabled());
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}
void enterScalar(QWidget& root, const char* name, double value) {
    auto* field = control<QLineEdit>(root, name);
    enterScalarText(root, name, field->locale().toString(value, 'g', 17));
}
double scalarValue(QWidget& root, const char* name) {
    auto* field = control<QLineEdit>(root, name);
    bool valid = false;
    const auto value = field->locale().toDouble(field->text(), &valid);
    REQUIRE(valid);
    return value;
}
void enterInteger(QWidget& root, const char* name, int value) {
    auto* field = control<QSpinBox>(root, name);
    auto* input = field->findChild<QLineEdit*>();
    REQUIRE(input);
    input->selectAll();
    QTest::keyClicks(input, QString::number(value));
    QTest::keyClick(input, Qt::Key_Return);
}
void click(QWidget& root, const char* name) {
    auto* button = control<QPushButton>(root, name);
    REQUIRE(button->isEnabled());
    QTest::mouseClick(button, Qt::LeftButton);
}
std::string encodedScene(const editor::SceneViewModel& model) {
    core::SceneDocumentData data(*model.scene());
    data.camera = model.editorCamera();
    data.cursor = model.cursor3D();
    return core::SceneSerializer::encode(data);
}
const core::ScalarCurve& curveFor(const editor::SceneViewModel& model,
                                  const core::OwnerRef& owner, core::PropertyId property,
                                  core::PropertyComponent component) {
    const auto& animation = model.scene()->animation().properties;
    for (const auto& binding : animation.bindings) {
        if (binding.owner != owner)
            continue;
        for (const auto& action : animation.actions) {
            if (action.id != binding.action)
                continue;
            for (const auto& slot : action.actionSlots) {
                if (slot.id != binding.slot)
                    continue;
                for (const auto& curve : slot.curves)
                    if (curve.property == property && curve.component == component)
                        return curve;
            }
        }
    }
    FAIL("GUI did not produce the requested typed scalar curve");
    std::terminate();
}

/** @brief 无 GL 的真实控件夹具；Explicit 仅准备对象，被测材料与键编辑走控件。 */
struct WidgetAuthoring {
    editor::SceneViewModel model;
    QWidget host;
    editor::AppearanceInspector* inspector = nullptr;
    core::EntityId cube = 0;
    WidgetAuthoring() {
        model.newScene();
        auto request = mutation<api::EntityCreateRequest>(model);
        request.name = QStringLiteral("控件立方体");
        request.primitive = core::PrimitiveKind::Cube;
        const auto created = model.createEntityExplicit(request);
        REQUIRE(created.hasValue());
        cube = created.value->createdEntityIds.front();
        model.selection()->setSelectedEntity(cube);
        auto* layout = new QHBoxLayout(&host);
        auto* scroll = new QScrollArea(&host);
        scroll->setWidgetResizable(true);
        inspector = new editor::AppearanceInspector(model, scroll);
        scroll->setWidget(inspector);
        layout->addWidget(scroll);
        layout->addWidget(new editor::AnimationTimeline(model, &host), 2);
        host.setAttribute(Qt::WA_ShowWithoutActivating);
        host.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        host.resize(1200, 760);
        host.show();
        REQUIRE(QTest::qWaitForWindowExposed(&host));
    }
    core::MaterialId createAndAssignMaterial() {
        click(host, "CreatePbrMaterial");
        click(host, "AssignPbrMaterial");
        const auto& materialSlots = model.scene()->find(cube)->materialSlots;
        REQUIRE(materialSlots.size() == 1);
        return materialSlots.front().material;
    }
    void record(int property, double value) {
        control<QComboBox>(host, "AnimationProperty")->setCurrentIndex(property);
        enterScalar(host, "AnimationPropertyValue", value);
        click(host, "AnimationRecordProperty");
    }
};

/** @brief 仅阻止本测试窗口的子对话框激活；不改变其他应用或其他窗口。 */
class InactiveTestDialogs final : public QObject {
  public:
    explicit InactiveTestDialogs(QWidget& window) : window_(window) {
        qApp->installEventFilter(this);
    }
    ~InactiveTestDialogs() override { qApp->removeEventFilter(this); }
  protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Polish) {
            auto* dialog = qobject_cast<QDialog*>(object);
            if (dialog && window_.isAncestorOf(dialog)) {
                dialog->setAttribute(Qt::WA_ShowWithoutActivating);
                dialog->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            }
        }
        return QObject::eventFilter(object, event);
    }
  private:
    QWidget& window_;
};

void answerFileAction(editor::MainWindow& window, const char* actionName, const QString& path) {
    bool answered = false;
    QTimer response, deadline;
    response.setSingleShot(true);
    deadline.setSingleShot(true);
    QObject::connect(&response, &QTimer::timeout, &window, [&] {
        auto* dialog = window.findChild<QFileDialog*>();
        if (!dialog)
            return;
        dialog->setDirectory(QFileInfo(path).absolutePath());
        QTest::qWait(100);
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!input || !buttons || !dialog->testOption(QFileDialog::DontUseNativeDialog)) {
            dialog->reject();
            return;
        }
        input->selectAll();
        QTest::keyClicks(input, QFileInfo(path).fileName());
        answered = true;
        QTest::mouseClick(buttons->button(dialog->acceptMode() == QFileDialog::AcceptSave
            ? QDialogButtonBox::Save : QDialogButtonBox::Open), Qt::LeftButton);
    });
    QObject::connect(&deadline, &QTimer::timeout, &window, [&] {
        if (auto* dialog = window.findChild<QFileDialog*>())
            dialog->reject();
    });
    response.start(25);
    deadline.start(5000);
    control<QAction>(window, actionName)->trigger();
    response.stop();
    deadline.stop();
    REQUIRE(answered);
}

/** @brief 仅重定向本测试进程的设置存储；窗口析构后恢复默认格式，不改用户全局身份。 */
class IsolatedGuiSettings final {
  public:
    explicit IsolatedGuiSettings(const QString& evidenceDirectory)
        : previousFormat_(QSettings::defaultFormat()),
          userPath_(QDir(evidenceDirectory).filePath(QStringLiteral("settings/user"))),
          systemPath_(QDir(evidenceDirectory).filePath(QStringLiteral("settings/system"))) {
        QSettings user(QSettings::IniFormat, QSettings::UserScope, probeOrganization(), probeApplication());
        QSettings system(QSettings::IniFormat, QSettings::SystemScope, probeOrganization(), probeApplication());
        previousUserFile_ = user.fileName();
        previousSystemFile_ = system.fileName();
        previousUserRoot_ = settingsRoot(previousUserFile_);
        previousSystemRoot_ = settingsRoot(previousSystemFile_);
        REQUIRE(QDir().mkpath(userPath_));
        REQUIRE(QDir().mkpath(systemPath_));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, userPath_);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, systemPath_);
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }
    ~IsolatedGuiSettings() {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, previousUserRoot_);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, previousSystemRoot_);
        QSettings::setDefaultFormat(previousFormat_);
        QSettings user(QSettings::IniFormat, QSettings::UserScope, probeOrganization(), probeApplication());
        QSettings system(QSettings::IniFormat, QSettings::SystemScope, probeOrganization(), probeApplication());
        CHECK(user.fileName() == previousUserFile_);
        CHECK(system.fileName() == previousSystemFile_);
        CHECK(QSettings::defaultFormat() == previousFormat_);
    }
    void verify() const {
        REQUIRE(QSettings::defaultFormat() == QSettings::IniFormat);
        QSettings user(QSettings::IniFormat, QSettings::UserScope, probeOrganization(), probeApplication());
        QSettings system(QSettings::IniFormat, QSettings::SystemScope, probeOrganization(), probeApplication());
        REQUIRE(QDir::cleanPath(user.fileName()).startsWith(userPath_ + '/', Qt::CaseInsensitive));
        REQUIRE(QDir::cleanPath(system.fileName()).startsWith(systemPath_ + '/', Qt::CaseInsensitive));
    }
  private:
    static QString probeOrganization() { return QStringLiteral("Mini3DJ1GuiProbe7208b9e6"); }
    static QString probeApplication() { return QStringLiteral("PathProbe"); }
    static QString settingsRoot(const QString& fileName) {
        const auto normalized = QDir::cleanPath(fileName);
        const auto suffix = QStringLiteral("/%1/%2.ini").arg(probeOrganization(), probeApplication());
        REQUIRE(normalized.endsWith(suffix));
        return normalized.left(normalized.size() - suffix.size());
    }
    QSettings::Format previousFormat_;
    QString previousUserFile_, previousSystemFile_, previousUserRoot_, previousSystemRoot_;
    QString userPath_;
    QString systemPath_;
};

/** @brief 真正显示的隔离产品窗口；选择走树控件，文件输入走本窗口非原生对话框。 */
struct GuiAuthoring {
    IsolatedGuiSettings isolatedSettings;
    editor::MainWindow window;
    InactiveTestDialogs inactiveDialogs{window};
    editor::SceneViewModel* model = nullptr;
    renderer_gl::ViewportWidget* viewport = nullptr;
    explicit GuiAuthoring(const QString& evidenceDirectory) : isolatedSettings(evidenceDirectory) {
        isolatedSettings.verify();
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        window.resize(1440, 900);
        window.show();
        REQUIRE(QTest::qWaitForWindowExposed(&window));
        model = window.findChild<editor::SceneViewModel*>();
        viewport = window.findChild<renderer_gl::ViewportWidget*>();
        REQUIRE(model);
        REQUIRE(viewport);
        auto* animationDock = control<QDockWidget>(window, "AnimationDock");
        animationDock->show();
        animationDock->raise();
        QApplication::processEvents();
        REQUIRE_FALSE(animationDock->widget()->visibleRegion().isEmpty());
        control<QAction>(window, "NewScene")->trigger();
        REQUIRE(model->scene()->nodes().empty());
    }
    ~GuiAuthoring() { window.hide(); }
    core::EntityId create(const char* actionName) {
        const auto count = model->scene()->nodes().size();
        control<QAction>(window, actionName)->trigger();
        REQUIRE(model->scene()->nodes().size() == count + 1);
        const auto id = model->selection()->selectedEntity();
        REQUIRE(id != 0);
        return id;
    }
    void select(core::EntityId entity) {
        auto* tree = control<QTreeView>(window, "SceneTree");
        auto* treeModel = qobject_cast<editor::SceneTreeModel*>(tree->model());
        REQUIRE(treeModel);
        const auto index = treeModel->indexForEntity(entity);
        REQUIRE(index.isValid());
        tree->scrollTo(index);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualRect(index).center());
        REQUIRE(model->selection()->selectedEntity() == entity);
    }
    void showAppearance(const char* name) {
        auto* pages = control<QTabWidget>(window, "PropertyPages");
        auto* scroll = control<QScrollArea>(window, "DataPropertiesScroll");
        pages->setCurrentWidget(scroll);
        scroll->ensureWidgetVisible(control<QWidget>(window, name));
    }
    void record(core::EntityId entity, int property, double value) {
        select(entity);
        control<QComboBox>(window, "AnimationProperty")->setCurrentIndex(property);
        REQUIRE_FALSE(control<QWidget>(window, "AnimationPropertyValue")->visibleRegion().isEmpty());
        REQUIRE_FALSE(control<QWidget>(window, "AnimationRecordProperty")->visibleRegion().isEmpty());
        enterScalar(window, "AnimationPropertyValue", value);
        click(window, "AnimationRecordProperty");
    }
    void seek(double frame) {
        REQUIRE_FALSE(control<QWidget>(window, "AnimationFrame")->visibleRegion().isEmpty());
        enterNumber(window, "AnimationFrame", frame);
    }
    std::uint64_t paint() {
        const auto before = viewport->lastRenderedFrame() ? viewport->lastRenderedFrame()->frameId : 0;
        viewport->update();
        for (int attempt = 0; attempt < 100; ++attempt) {
            QTest::qWait(10);
            if (viewport->lastRenderedFrame() && viewport->lastRenderedFrame()->frameId > before) {
                INFO(viewport->lastRenderedFrame()->resources.error.toStdString());
                REQUIRE(viewport->lastRenderedFrame()->resources.ready);
                REQUIRE(viewport->lastRenderedFrame()->state.animation == model->installedAnimationPose()->identity);
                return viewport->lastRenderedFrame()->frameId;
            }
        }
        FAIL("J1 GUI did not produce a fresh real paint");
        return 0;
    }
};

using JointFrame = std::shared_ptr<const renderer_gl::ImageFrame>;
constexpr std::array<const char*, 3> jointPropertyRoles{"positionX", "roughness", "irradiance"};

/** @brief 真实提交发行的角色映射；跨文档只比较语义，不借用作者身份。 */
struct JointRoles {
    core::EntityId cube = 0, sun = 0, camera = 0;
    core::MaterialId material;
    core::MaterialSlotId materialSlot;
    std::array<core::PropertyAddress, 3> addresses(const core::DocumentUuid& document) const {
        return {{{document, core::EntityOwner{cube}, core::PropertyId::Position,
                   core::PropertyComponent::X},
                 {document, core::MaterialOwner{material}, core::PropertyId::PbrRoughness,
                   core::PropertyComponent::Scalar},
                 {document, core::EntityOwner{sun}, core::PropertyId::SunIrradiance,
                   core::PropertyComponent::Scalar}}};
    }
};

/** @brief 只经既有相机预览 QAction 切换 author/capture 阶段，保持选区不变。 */
void setJointCameraPreview(GuiAuthoring& fixture, const JointRoles& roles, bool enabled) {
    const auto selected = fixture.model->selection()->selectedEntity();
    const auto current = fixture.model->previewCamera();
    const auto target = enabled ? roles.camera : core::kInvalidEntity;
    auto* action = control<QAction>(fixture.window, "ToggleCameraPreview");
    INFO("joint camera preview current=" << current << " target=" << target
        << " selected=" << selected << " actionEnabled=" << action->isEnabled());
    REQUIRE((current == 0 || current == roles.camera));
    REQUIRE(action->isEnabled());
    if (current != target) {
        action->trigger();
        QCoreApplication::processEvents();
    }
    REQUIRE(fixture.model->previewCamera() == target);
    REQUIRE(fixture.model->selection()->selectedEntity() == selected);
}

void writeJointEvidence(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    REQUIRE(file.flush());
}
QByteArray readJointBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
void writeJointJson(const QString& path, const QJsonObject& value) {
    writeJointEvidence(path, QJsonDocument(value).toJson());
}
void saveJointWindow(GuiAuthoring& fixture, const QString& path) {
    REQUIRE(fixture.window.isVisible());
    QByteArray bytes;
    QBuffer buffer(&bytes);
    REQUIRE(buffer.open(QIODevice::WriteOnly));
    REQUIRE(fixture.window.grab().save(&buffer, "PNG"));
    writeJointEvidence(path, bytes);
}
void showJointControl(GuiAuthoring& fixture, const char* name, bool transform = false) {
    if (transform) {
        auto* scroll = control<QScrollArea>(fixture.window, "ObjectPropertiesScroll");
        control<QTabWidget>(fixture.window, "PropertyPages")->setCurrentWidget(scroll);
        scroll->ensureWidgetVisible(control<QWidget>(fixture.window, name));
    } else {
        fixture.showAppearance(name);
    }
    QApplication::processEvents();
    REQUIRE_FALSE(control<QWidget>(fixture.window, name)->visibleRegion().isEmpty());
}
void jointNumber(GuiAuthoring& fixture, const char* name, double value, bool transform = false) {
    showJointControl(fixture, name, transform);
    enterNumber(fixture.window, name, value);
}
void jointPreview(GuiAuthoring& fixture, bool enabled) {
    auto* preview = control<QCheckBox>(fixture.window, "AnimationPreview");
    REQUIRE_FALSE(preview->visibleRegion().isEmpty());
    if (preview->isChecked() != enabled)
        QTest::mouseClick(preview, Qt::LeftButton);
    REQUIRE(preview->isChecked() == enabled);
    REQUIRE(fixture.model->animationMode() ==
        (enabled ? editor::AnimationMode::PreviewPaused : editor::AnimationMode::Base));
}

/** @brief 作者操作期间恢复自己的 dock；拍摄时固定实际 viewport，不改变用户窗口。 */
struct JointCaptureLayout {
    GuiAuthoring& fixture;
    QSize minimum, maximum, windowSize;
    std::vector<std::pair<QDockWidget*, bool>> docks;
    explicit JointCaptureLayout(GuiAuthoring& author)
        : fixture(author), minimum(author.viewport->minimumSize()),
          maximum(author.viewport->maximumSize()), windowSize(author.window.size()) {
        for (auto* dock : author.window.findChildren<QDockWidget*>())
            docks.emplace_back(dock, dock->isVisible());
    }
    void authoring() {
        fixture.viewport->setMinimumSize(minimum);
        fixture.viewport->setMaximumSize(maximum);
        for (const auto& [dock, visible] : docks)
            dock->setVisible(visible);
        fixture.window.resize(windowSize);
        auto* animationDock = control<QDockWidget>(fixture.window, "AnimationDock");
        animationDock->raise();
        QApplication::processEvents();
        REQUIRE_FALSE(animationDock->widget()->visibleRegion().isEmpty());
    }
    void capture() {
        for (const auto& [dock, visible] : docks) {
            (void)visible;
            dock->hide();
        }
        fixture.viewport->setFixedSize(1280, 720);
        fixture.window.resize(1360, 840);
        QApplication::processEvents();
        REQUIRE(fixture.viewport->size() == QSize(1280, 720));
        REQUIRE(fixture.viewport->devicePixelRatioF() == 1);
        REQUIRE(fixture.viewport->isVisible());
        REQUIRE(fixture.window.testAttribute(Qt::WA_ShowWithoutActivating));
        REQUIRE(fixture.window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        REQUIRE(fixture.viewport->context());
        REQUIRE(std::max(0, fixture.viewport->context()->format().samples()) == 0);
    }
};

QJsonArray jointVector(const auto& vector) {
    return {double(vector.x), double(vector.y), double(vector.z)};
}
QJsonObject jointRoleMap(const editor::SceneViewModel& model, const JointRoles& roles) {
    const auto& state = model.apiDocumentState();
    const auto& scene = *model.scene();
    const auto watermarks = scene.idHighWatermarks();
    QJsonObject properties;
    const auto addresses = roles.addresses(scene.documentUuid());
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        const auto& address = addresses[index];
        const auto& bindings = scene.animation().properties.bindings;
        const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto& value) {
            return value.owner == address.owner;
        });
        REQUIRE(binding != bindings.end());
        REQUIRE(binding->documentUuid == scene.documentUuid());
        const auto& curve = curveFor(model, address.owner, address.property, address.component);
        QJsonArray keys;
        for (const auto& key : curve.keys)
            keys.append(QJsonObject{{"keyId", QString::number(key.id.value)}, {"time", key.time},
                {"value", key.value}, {"outgoing", int(key.outgoing)}});
        properties.insert(QString::fromLatin1(jointPropertyRoles[index]), QJsonObject{
            {"ownerKind", index == 1 ? "Material" : "Entity"},
            {"ownerId", QString::number(index == 1 ? roles.material.value :
                index == 0 ? roles.cube : roles.sun)},
            {"property", int(address.property)}, {"component", int(address.component)},
            {"actionId", QString::number(binding->action.value)},
            {"actionSlotId", QString::number(binding->slot.value)},
            {"curveId", QString::number(curve.id.value)}, {"keys", keys}});
    }
    return {{"documentUuid", QString::fromStdString(core::documentUuidString(scene.documentUuid()))},
        {"instanceId", state.document.instanceId}, {"documentId", state.document.documentId},
        {"documentRevision", QString::number(state.documentRevision)},
        {"historyRevision", QString::number(state.historyRevision)},
        {"cube", QString::number(roles.cube)}, {"sun", QString::number(roles.sun)},
        {"camera", QString::number(roles.camera)}, {"material", QString::number(roles.material.value)},
        {"materialSlot", QString::number(roles.materialSlot.value)}, {"properties", properties},
        {"watermarks", QJsonObject{{"asset", QString::number(watermarks.asset)},
            {"materialSlot", QString::number(watermarks.materialSlot)},
            {"action", QString::number(watermarks.action)},
            {"actionSlot", QString::number(watermarks.actionSlot)},
            {"curve", QString::number(watermarks.curve)}, {"key", QString::number(watermarks.key)}}}};
}

/** @brief 角色化作者语义保留全部当前配方参数，身份另记，不把不同作品说成同源。 */
QJsonObject jointAuthorSemantics(const editor::SceneViewModel& model, const JointRoles& roles) {
    const auto& scene = *model.scene();
    REQUIRE(scene.nodes().size() == 3);
    QJsonObject nodes;
    for (const auto& [name, id] : std::array<std::pair<const char*, core::EntityId>, 3>{
             {{"cube", roles.cube}, {"sun", roles.sun}, {"camera", roles.camera}}}) {
        const auto* node = scene.find(id);
        REQUIRE(node);
        REQUIRE(node->parent == 0);
        REQUIRE(node->children.empty());
        REQUIRE_FALSE(node->meshRenderer);
        REQUIRE_FALSE(node->light);
        REQUIRE(node->editableMesh == 0);
        nodes.insert(QString::fromLatin1(name), QJsonObject{
            {"name", QString::fromStdString(node->name)}, {"primitive", int(node->primitive)},
            {"visible", node->visible}, {"position", jointVector(node->transform.position)},
            {"scale", jointVector(node->transform.scale)},
            {"rotationWXYZ", QJsonArray{double(node->transform.rotation.w),
                double(node->transform.rotation.x), double(node->transform.rotation.y),
                double(node->transform.rotation.z)}},
            {"legacySurface", QJsonObject{{"tint", jointVector(node->surface.tint)},
                {"useTexture", node->surface.useTexture}, {"useVertexColor", node->surface.useVertexColor}}}});
    }
    const auto* cube = scene.find(roles.cube);
    REQUIRE(cube->materialSlots.size() == 1);
    REQUIRE(cube->materialSlots.front().id == roles.materialSlot);
    REQUIRE(cube->materialSlots.front().material == roles.material);
    REQUIRE(scene.find(roles.sun)->materialSlots.empty());
    REQUIRE(scene.find(roles.camera)->materialSlots.empty());
    REQUIRE(scene.materialData().assets.size() == 1);
    const auto* material = scene.materialData().find(roles.material);
    REQUIRE(material);
    REQUIRE(material->textures.empty());
    REQUIRE(scene.find(roles.sun)->sun);
    REQUIRE(scene.find(roles.camera)->camera);
    const auto& sun = *scene.find(roles.sun)->sun;
    const auto& camera = *scene.find(roles.camera)->camera;
    const auto& animation = scene.animation();
    REQUIRE(animation.tracks.empty());
    REQUIRE(animation.properties.actions.size() == 3);
    REQUIRE(animation.properties.bindings.size() == 3);
    QJsonObject properties;
    const auto addresses = roles.addresses(scene.documentUuid());
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        const auto& address = addresses[index];
        const auto& curve = curveFor(model, address.owner, address.property, address.component);
        REQUIRE(curve.keys.size() == 2);
        QJsonArray keys;
        for (const auto& key : curve.keys)
            keys.append(QJsonObject{{"time", key.time}, {"value", key.value},
                                    {"outgoing", int(key.outgoing)}});
        properties.insert(QString::fromLatin1(jointPropertyRoles[index]), keys);
    }
    const auto fps = animation.properties.rationalFps.value_or(core::RationalFps{animation.settings.fps, 1});
    return {{"nodes", nodes}, {"material", QJsonObject{{"model", int(material->model)},
        {"definitionVersion", QString::number(material->definitionVersion)},
        {"baseColor", jointVector(material->parameters.baseColor)},
        {"roughness", material->parameters.roughness}, {"metallic", material->parameters.metallic},
        {"coverage", material->parameters.coverage}, {"assignedRole", "cube"}}},
        {"sun", QJsonObject{{"irradiance", sun.irradiance}, {"color", jointVector(sun.color)}}},
        {"camera", QJsonObject{{"fov", double(camera.fieldOfView)},
            {"near", double(camera.nearPlane)}, {"far", double(camera.farPlane)}}},
        {"animation", QJsonObject{{"fps", QJsonArray{int(fps.numerator), int(fps.denominator)}},
            {"start", int(animation.settings.startFrame)}, {"end", int(animation.settings.endFrame)},
            {"rationalOverride", animation.properties.rationalFps.has_value()},
            {"loopPolicy", int(animation.properties.loopPolicy)}, {"properties", properties}}}};
}

std::array<double, 3> jointValues(const editor::SceneViewModel& model, const JointRoles& roles) {
    const auto pose = model.installedAnimationPose();
    REQUIRE(pose);
    REQUIRE(pose->sceneState);
    const auto* cube = pose->sceneState->find(roles.cube);
    REQUIRE(cube);
    const auto& content = *pose->sceneState->content;
    const auto sun = std::find_if(content.visibleSuns.begin(), content.visibleSuns.end(),
        [&](const auto& value) { return value.entity == roles.sun; });
    REQUIRE(sun != content.visibleSuns.end());
    REQUIRE(sun->directionToLight == glm::dvec3(0, 0, 1));
    return {cube->localPosition.x, content.materials.at(roles.material).effective.roughness,
            sun->effective.irradiance};
}
void requireJointValues(const std::array<double, 3>& actual, const std::array<double, 3>& expected) {
    for (std::size_t index = 0; index < actual.size(); ++index) {
        INFO(jointPropertyRoles[index] << " actual=" << actual[index] << " expected=" << expected[index]);
        REQUIRE(std::abs(actual[index] - expected[index]) <= 1e-12);
    }
}

/** @brief 输出前后核全部作者、游标和历史；输出私有求值不安装到 GUI。 */
struct JointUnchanged {
    const editor::SceneViewModel& model;
    std::shared_ptr<const core::Scene> scene;
    std::string encoded;
    api::DocumentState document;
    core::FrameTime frame;
    editor::AnimationMode mode;
    std::uint64_t sessionRevision, evaluationId;
    std::shared_ptr<const renderer_gl::InstalledPose> pose;
    int historyCount, historyIndex;
    bool modified;
    explicit JointUnchanged(const editor::SceneViewModel& value)
        : model(value), scene(value.scene()), encoded(encodedScene(value)),
          document(value.apiDocumentState()), frame(value.animationFrame()), mode(value.animationMode()),
          sessionRevision(value.animationSessionRevision()), evaluationId(value.animationEvaluationId()),
          pose(value.installedAnimationPose()), historyCount(value.undoStack()->count()),
          historyIndex(value.undoStack()->index()), modified(value.isModified()) {}
    void verify() const {
        REQUIRE(model.scene() == scene);
        REQUIRE(encodedScene(model) == encoded);
        REQUIRE(model.apiDocumentState().document == document.document);
        REQUIRE(model.apiDocumentState().documentRevision == document.documentRevision);
        REQUIRE(model.apiDocumentState().historyRevision == document.historyRevision);
        REQUIRE(model.animationFrame() == frame);
        REQUIRE(model.animationMode() == mode);
        REQUIRE(model.animationSessionRevision() == sessionRevision);
        REQUIRE(model.animationEvaluationId() == evaluationId);
        REQUIRE(model.installedAnimationPose() == pose);
        REQUIRE(model.undoStack()->count() == historyCount);
        REQUIRE(model.undoStack()->index() == historyIndex);
        REQUIRE(model.isModified() == modified);
    }
};

JointFrame jointPaint(GuiAuthoring& fixture, const QString& prefix) {
    const auto frameId = fixture.paint();
    const auto rendered = fixture.viewport->lastRenderedFrame();
    REQUIRE(rendered);
    REQUIRE(rendered->resources.imageFrame);
    const auto frame = rendered->resources.imageFrame;
    writeJointEvidence(prefix + "-preview-source.json",
        frame->provenanceJson(renderer_gl::ImageFileFormat::Png));
    REQUIRE(frame->stamp().frameId == frameId);
    REQUIRE(frame->pixels().source == fixture.model->installedAnimationPose()->sceneState->source);
    REQUIRE(frame->pixels().source.frameTime == fixture.model->animationFrame());
    return frame;
}

void compareJointMother(const renderer_gl::ImageFrame& preview,
                        const renderer_gl::ImageFrame& output, const QString& evidencePath) {
    REQUIRE(core::isSameWorkSource(preview.pixels().source, output.originSource()));
    REQUIRE(renderer_gl::isAllowedImageOutputDerivation(output.originSource(), output.pixels().source));
    REQUIRE_FALSE(core::isSameWorkSource(preview.pixels().source, output.pixels().source));
    REQUIRE(preview.pixels().source.evaluationSessionId != output.pixels().source.evaluationSessionId);
    REQUIRE(preview.stamp().sessionId != output.stamp().sessionId);
    REQUIRE(preview.pixels().source.frameTime == output.pixels().source.frameTime);
    REQUIRE(preview.pixels().source.rationalFps == output.pixels().source.rationalFps);
    REQUIRE(preview.pixels().source.authorContextIdentity == output.pixels().source.authorContextIdentity);
    REQUIRE(preview.pixels().camera == output.pixels().camera);
    REQUIRE(preview.pixels().backendBuild == output.pixels().backendBuild);
    REQUIRE(preview.pixels().color == output.pixels().color);
    REQUIRE(preview.pixels().geometryStages == output.pixels().geometryStages);
    REQUIRE(preview.pixels().width == 1280);
    REQUIRE(preview.pixels().height == 720);
    REQUIRE(output.pixels().width == 1280);
    REQUIRE(output.pixels().height == 720);
    REQUIRE(preview.stamp().devicePixelRatio == 1);
    REQUIRE(output.stamp().devicePixelRatio == 1);
    const auto previewMetadata = QJsonDocument::fromJson(
        preview.provenanceJson(renderer_gl::ImageFileFormat::Png)).object();
    const auto outputMetadata = QJsonDocument::fromJson(
        output.provenanceJson(renderer_gl::ImageFileFormat::Png)).object();
    for (const auto* field : {"camera", "settings", "backend", "color", "geometryStages", "master"})
        REQUIRE(previewMetadata[field] == outputMetadata[field]);
    REQUIRE(previewMetadata["evaluatedTimeBinary64"] == outputMetadata["evaluatedTimeBinary64"]);
    REQUIRE(previewMetadata["secondsBinary64"] == outputMetadata["secondsBinary64"]);
    REQUIRE(preview.pixels().beauty.size() == output.pixels().beauty.size());
    REQUIRE(preview.pixels().coverage.size() == output.pixels().coverage.size());
    std::size_t rgbMismatches = 0, alphaMismatches = 0, coverageMismatches = 0;
    for (std::size_t index = 0; index < preview.pixels().beauty.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel) {
            const double reference = preview.pixels().beauty[index][channel];
            const double actual = output.pixels().beauty[index][channel];
            if (std::abs(actual - reference) > 2e-5 + 2e-6 * std::abs(reference))
                ++rgbMismatches;
        }
        if (std::bit_cast<std::uint32_t>(preview.pixels().beauty[index].a) !=
            std::bit_cast<std::uint32_t>(output.pixels().beauty[index].a))
            ++alphaMismatches;
        if (std::bit_cast<std::uint32_t>(preview.pixels().coverage[index]) !=
            std::bit_cast<std::uint32_t>(output.pixels().coverage[index]))
            ++coverageMismatches;
    }
    writeJointJson(evidencePath, {{"rgbAbsoluteTolerance", 2e-5}, {"rgbRelativeTolerance", 2e-6},
        {"rgbMismatches", QString::number(rgbMismatches)},
        {"alphaBitMismatches", QString::number(alphaMismatches)},
        {"coverageBitMismatches", QString::number(coverageMismatches)}});
    REQUIRE(rgbMismatches == 0);
    REQUIRE(alphaMismatches == 0);
    REQUIRE(coverageMismatches == 0);
}

void validateJointFile(const QString& path, const renderer_gl::ImageFrame& frame,
                       renderer_gl::ImageFileFormat format,
                       const renderer_gl::OcioDisplayTransform& color) {
    QString diagnostic;
    const auto encoded = renderer_gl::ImageFrameOutput::encode(frame, format, color, diagnostic);
    INFO(diagnostic.toStdString());
    REQUIRE(encoded);
    // 正式解码器读取关闭后的 PNG/EXR，核真实五 FLOAT32 plane/标签/全图位值。
    const auto valid = renderer_gl::ImageFrameOutput::validateFile(path, frame, *encoded, diagnostic);
    INFO(diagnostic.toStdString());
    REQUIRE(valid);
    const auto metadata = QJsonDocument::fromJson(frame.provenanceJson(format)).object();
    const auto file = metadata["file"].toObject();
    REQUIRE(file["alphaOcioApplications"].toInt() == 0);
    if (format == renderer_gl::ImageFileFormat::Exr) {
        REQUIRE((file["planes"].toArray() == QJsonArray{"R", "G", "B", "A", "coverage"}));
        REQUIRE(file["sampleType"].toString() == "FLOAT32");
        REQUIRE(file["independentCoverageSaved"].toBool());
        REQUIRE(file["rgbOcioApplications"].toInt() == 0);
    } else {
        const QImage image(path);
        REQUIRE(image.size() == QSize(1280, 720));
        REQUIRE(QByteArray::fromBase64(image.text("mini3d.ImageFrame").toLatin1()) ==
            frame.provenanceJson(format));
        REQUIRE(file["rgbOcioApplications"].toInt() == 1);
    }
}

void compareJointPng(const renderer_gl::ImageFrame& preview, const QString& path,
                     const renderer_gl::OcioDisplayTransform& color, const QString& evidencePath) {
    REQUIRE(std::all_of(preview.pixels().beauty.begin(), preview.pixels().beauty.end(),
        [](const glm::vec4& pixel) {
            return pixel.a == 1 || (pixel.a == 0 && pixel.r == 0 && pixel.g == 0 && pixel.b == 0);
        }));
    std::vector<glm::vec4> display(preview.pixels().beauty.size());
    QString diagnostic;
    REQUIRE(color.applyCpuRgba(preview.pixels().beauty, display, diagnostic));
    const auto decoded = QImage(path).convertToFormat(QImage::Format_RGBA8888);
    REQUIRE(decoded.size() == QSize(1280, 720));
    std::size_t mismatches = 0;
    int maximumDifference = 0;
    for (std::size_t index = 0; index < display.size(); ++index) {
        const auto* pixel = decoded.constScanLine(int(index / 1280)) + (index % 1280) * 4;
        for (int channel = 0; channel < 4; ++channel) {
            const int expected = int(std::lround(std::clamp(display[index][channel], 0.0F, 1.0F) * 255.0F));
            const int difference = std::abs(int(pixel[channel]) - expected);
            maximumDifference = std::max(maximumDifference, difference);
            if (difference > 1)
                ++mismatches;
        }
    }
    writeJointJson(evidencePath, {{"maximumAllowedChannelByteDifference", 1},
        {"maximumObservedChannelByteDifference", maximumDifference},
        {"channelMismatches", QString::number(mismatches)}});
    REQUIRE(mismatches == 0);
}

/** @brief 从实际 GUI 的同一模型/服务/视口输出两次独立母版，并留存失败前证据。 */
JointFrame jointOutputs(GuiAuthoring& fixture, editor::ImageOutputService& output,
                        const JointRoles& roles, const QString& prefix,
                        std::uint64_t& previousOutputFrame, std::uint64_t& previousOutputEvaluation) {
    const auto preview = jointPaint(fixture, prefix);
    REQUIRE(preview->stamp().cameraEntity == roles.camera);
    REQUIRE(preview->pixels().camera.position == glm::dvec3(1, 0, 6));
    REQUIRE(preview->pixels().backendBuild ==
        "NativeGL41-Lambert-GGX-height-correlated-Smith-Schlick-alpha-floor-0.001-v1");
    writeJointJson(prefix + "-roles.json", jointRoleMap(*fixture.model, roles));
    const auto color = fixture.viewport->imageOutputColor();
    REQUIRE(color);
    const JointUnchanged unchanged(*fixture.model);
    JointFrame png;
    for (const auto format : {renderer_gl::ImageFileFormat::Png, renderer_gl::ImageFileFormat::Exr}) {
        const auto extension = format == renderer_gl::ImageFileFormat::Png ? "png" : "exr";
        auto request = mutation<editor::ImageOutputRequest>(*fixture.model);
        request.path = prefix + "-output." + extension;
        request.frame = fixture.model->animationFrame();
        request.camera = roles.camera;
        request.format = format;
        request.timeoutMs = int(api::limits::mutationTimeoutMaximumMs);
        const auto result = output.write(request);
        INFO((result.error ? result.error->message.toStdString() : ""));
        REQUIRE(result.hasValue());
        REQUIRE(result.value->frame);
        const auto frame = result.value->frame;
        writeJointEvidence(prefix + "-" + extension + "-source.json", frame->provenanceJson(format));
        REQUIRE(result.value->path == request.path);
        REQUIRE(frame->stamp().cameraEntity == roles.camera);
        REQUIRE(frame->stamp().frameId > previousOutputFrame);
        REQUIRE(frame->pixels().source.evaluationId > previousOutputEvaluation);
        previousOutputFrame = frame->stamp().frameId;
        previousOutputEvaluation = frame->pixels().source.evaluationId;
        compareJointMother(*preview, *frame, prefix + "-" + extension + "-mother-comparison.json");
        validateJointFile(request.path, *frame, format, *color);
        unchanged.verify();
        REQUIRE_FALSE(output.isPreparing());
        if (format == renderer_gl::ImageFileFormat::Png) {
            png = frame;
            compareJointPng(*preview, request.path, *color, prefix + "-png-byte-comparison.json");
        } else {
            REQUIRE(png);
            REQUIRE(frame->stamp().sessionId == png->stamp().sessionId);
            REQUIRE(frame->pixels().source.evaluationSessionId == png->pixels().source.evaluationSessionId);
            REQUIRE(std::get<core::FrozenSourceHandle>(frame->pixels().source.sourceHandle) !=
                std::get<core::FrozenSourceHandle>(png->pixels().source.sourceHandle));
        }
    }
    return png;
}

void requireJointRoiChange(const renderer_gl::ImageFrame& before,
                           const renderer_gl::ImageFrame& after, const QJsonArray& roi,
                           const QString& evidencePath) {
    REQUIRE(roi.size() == 4);
    const int left = roi[0].toInt(), top = roi[1].toInt();
    const int right = roi[2].toInt(), bottom = roi[3].toInt();
    REQUIRE(left >= 0);
    REQUIRE(top >= 0);
    REQUIRE(right <= 1280);
    REQUIRE(bottom <= 720);
    REQUIRE(left < right);
    REQUIRE(top < bottom);
    REQUIRE(before.pixels().width == 1280);
    REQUIRE(before.pixels().height == 720);
    REQUIRE(after.pixels().width == 1280);
    REQUIRE(after.pixels().height == 720);
    std::size_t changed = 0;
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            const auto index = std::size_t(row) * 1280 + std::size_t(column);
            bool different = false;
            for (int channel = 0; channel < 3; ++channel)
                different = different || std::abs(double(after.pixels().beauty[index][channel]) -
                    double(before.pixels().beauty[index][channel])) > 1e-4;
            if (different)
                ++changed;
        }
    }
    writeJointJson(evidencePath, {{"roiTopLeftHalfOpen", roi}, {"rgbThreshold", 1e-4},
        {"minimumChangedPixels", 16}, {"observedChangedPixels", QString::number(changed)},
        {"before", QJsonDocument::fromJson(before.provenanceJson(renderer_gl::ImageFileFormat::Png)).object()},
        {"after", QJsonDocument::fromJson(after.provenanceJson(renderer_gl::ImageFileFormat::Png)).object()}});
    INFO("Fixed ROI changed pixels: " << changed);
    REQUIRE(changed >= 16);
}
} // namespace

TEST_CASE("J1 inspector uses complete material and Sun commands and refresh writes nothing",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    const auto material = fixture.createAndAssignMaterial();
    const auto slot = fixture.model.scene()->find(fixture.cube)->materialSlots.front().id;
    auto request = mutation<api::MaterialUpdateRequest>(fixture.model);
    request.materialId = material;
    request.parameters.baseColor = {.12345678912345678, .25, .75};
    request.parameters.metallic = .23456789123456789;
    request.parameters.roughness = .6;
    REQUIRE(fixture.model.updateMaterialExplicit(request).hasValue());
    const auto beforeIndex = fixture.model.undoStack()->index();
    enterScalar(fixture.host, "PbrRoughness", .2);
    auto expected = request.parameters;
    expected.roughness = .2;
    REQUIRE(fixture.model.scene()->materialData().find(material)->parameters == expected);
    REQUIRE(fixture.model.undoStack()->index() == beforeIndex + 1);
    REQUIRE(fixture.model.scene()->find(fixture.cube)->materialSlots.front().id == slot);
    enterScalar(fixture.host, "PbrRoughness", 0);
    REQUIRE(fixture.model.scene()->materialData().find(material)->parameters.roughness == 0);
    const auto source = fixture.model.scene();
    const auto document = fixture.model.apiDocumentState();
    const auto encoded = encodedScene(fixture.model);
    const auto index = fixture.model.undoStack()->index();
    fixture.inspector->refresh();
    fixture.inspector->refresh();
    REQUIRE(fixture.model.scene() == source);
    REQUIRE(encodedScene(fixture.model) == encoded);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(fixture.model.apiDocumentState().documentRevision == document.documentRevision);
    REQUIRE_FALSE(control<QCheckBox>(fixture.host, "UseTexture")->isEnabled());
    click(fixture.host, "CreatePbrMaterial");
    auto* materialSelection = control<QComboBox>(fixture.host, "PbrMaterialSelection");
    const core::MaterialId secondMaterial{materialSelection->currentData().toULongLong()};
    REQUIRE(secondMaterial != material);
    enterScalar(fixture.host, "PbrRoughness", .7);
    REQUIRE(materialSelection->currentData().toULongLong() == secondMaterial.value);
    REQUIRE(fixture.model.scene()->materialData().find(secondMaterial)->parameters.roughness == .7);
    REQUIRE(fixture.model.scene()->materialData().find(material)->parameters.roughness == 0);

    auto sunRequest = mutation<api::SunCreateRequest>(fixture.model);
    sunRequest.name = QStringLiteral("控件 Sun");
    sunRequest.sun.color = {.23456789123456789, .5, 1};
    const auto sun = fixture.model.createSunExplicit(sunRequest);
    REQUIRE(sun.hasValue());
    const auto sunId = sun.value->createdEntityIds.front();
    fixture.model.selection()->setSelectedEntity(sunId);
    enterScalar(fixture.host, "SunIrradiance", 2);
    REQUIRE(fixture.model.scene()->find(sunId)->sun->irradiance == 2);
    REQUIRE(fixture.model.scene()->find(sunId)->sun->color == sunRequest.sun.color);
    REQUIRE_FALSE(control<QDoubleSpinBox>(fixture.host, "LightIntensity")->isEnabled());
    REQUIRE(control<QLabel>(fixture.host, "SunStatus")->text().contains(QStringLiteral("W/m²")));
}

TEST_CASE("J1 typed controls author XYZ and exact subframes without overwriting the base",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    const auto material = fixture.createAndAssignMaterial();
    const std::array<double, 3> values{1.25, -2.5, 3.75};
    for (int axis = 0; axis < 3; ++axis) {
        fixture.record(axis, values[axis]);
        const auto& curve = curveFor(fixture.model, core::EntityOwner{fixture.cube},
            core::PropertyId::Position, static_cast<core::PropertyComponent>(axis));
        REQUIRE(curve.keys.size() == 1);
        REQUIRE(curve.keys.front().time == 1);
        REQUIRE(curve.keys.front().value == values[axis]);
    }
    REQUIRE(fixture.model.scene()->find(fixture.cube)->transform.position == glm::vec3(0));
    QTest::mouseClick(control<QCheckBox>(fixture.host, "AnimationPreview"), Qt::LeftButton);
    enterNumber(fixture.host, "AnimationFrame", 12.5);
    fixture.record(2, 4.25);
    const auto& z = curveFor(fixture.model, core::EntityOwner{fixture.cube},
        core::PropertyId::Position, core::PropertyComponent::Z);
    REQUIRE(z.keys.size() == 2);
    REQUIRE(z.keys.back().time == 12.5);
    REQUIRE(z.keys.back().value == 4.25);
    REQUIRE_FALSE(control<QPushButton>(fixture.host, "AnimationRecordKeyframes")->isEnabled());
    fixture.record(3, 0);
    REQUIRE(fixture.model.scene()->materialData().find(material)->parameters.roughness == 1);
    REQUIRE(curveFor(fixture.model, core::MaterialOwner{material}, core::PropertyId::PbrRoughness,
        core::PropertyComponent::Scalar).keys.front().value == 0);
    enterNumber(fixture.host, "AnimationFrame", 24);
    const auto encoded = encodedScene(fixture.model);
    const auto index = fixture.model.undoStack()->index();
    enterScalar(fixture.host, "AnimationPropertyValue", -.01);
    REQUIRE(control<QLabel>(fixture.host, "AnimationPropertyStatus")->text().contains(QStringLiteral("不合法")));
    click(fixture.host, "AnimationRecordProperty");
    REQUIRE(encodedScene(fixture.model) == encoded);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(control<QTableWidget>(fixture.host, "AnimationPropertyKeyTable")->rowCount() == 1);
    control<QComboBox>(fixture.host, "AnimationProperty")->setCurrentIndex(2);
    auto* table = control<QTableWidget>(fixture.host, "AnimationPropertyKeyTable");
    REQUIRE(table->rowCount() == 2);
    const auto point = table->visualItemRect(table->item(1, 0)).center();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    REQUIRE(fixture.model.animationFrame() == 12.5);
    REQUIRE(scalarValue(fixture.host, "AnimationPropertyValue") == 4.25);
}

TEST_CASE("J1 GUI range changes preserve rational FPS and legacy integer editing",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    SECTION("existing rational override remains exact") {
        auto request = mutation<api::AnimationSetRationalSettingsRequest>(fixture.model);
        request.settings.rationalFps = {30000, 1001};
        request.settings.endFrame = 24;
        REQUIRE(fixture.model.setRationalAnimationSettingsExplicit(request).hasValue());
        fixture.record(0, 2);
        const auto properties = fixture.model.scene()->animation().properties;
        REQUIRE_FALSE(control<QSpinBox>(fixture.host, "AnimationFps")->isEnabled());
        REQUIRE(control<QLabel>(fixture.host, "AnimationEffectiveFps")->text().contains("30000/1001"));
        enterInteger(fixture.host, "AnimationStart", 3);
        enterInteger(fixture.host, "AnimationEnd", 36);
        click(fixture.host, "AnimationApplySettings");
        REQUIRE(fixture.model.scene()->animation().properties == properties);
        REQUIRE(fixture.model.scene()->animation().settings.startFrame == 3);
        REQUIRE(fixture.model.scene()->animation().settings.endFrame == 36);
    }
    SECTION("legacy integer setting remains editable") {
        REQUIRE(control<QSpinBox>(fixture.host, "AnimationFps")->isEnabled());
        enterInteger(fixture.host, "AnimationFps", 30);
        click(fixture.host, "AnimationApplySettings");
        REQUIRE(fixture.model.scene()->animation().settings.fps == 30);
        REQUIRE_FALSE(fixture.model.scene()->animation().properties.rationalFps);
    }
}

TEST_CASE("J1 scalar text fields preserve tiny legal doubles and exact key rerecord is no change",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    const auto material = fixture.createAndAssignMaterial();
    auto sunRequest = mutation<api::SunCreateRequest>(fixture.model);
    sunRequest.name = QStringLiteral("精确标量 Sun");
    const auto createdSun = fixture.model.createSunExplicit(sunRequest);
    REQUIRE(createdSun.hasValue());
    const auto sun = createdSun.value->createdEntityIds.front();
    const std::array<double, 2> values{1e-20, std::numeric_limits<double>::denorm_min()};
    for (const auto value : values) {
        fixture.model.selection()->setSelectedEntity(fixture.cube);
        enterScalar(fixture.host, "PbrRoughness", value);
        REQUIRE(fixture.model.scene()->materialData().find(material)->parameters.roughness == value);
        REQUIRE(core::isPbrMaterialValid(fixture.model.scene()->materialData().find(material)->parameters));
        REQUIRE(scalarValue(fixture.host, "PbrRoughness") == value);
        const auto before = encodedScene(fixture.model);
        const auto history = fixture.model.undoStack()->index();
        fixture.inspector->refresh();
        REQUIRE(scalarValue(fixture.host, "PbrRoughness") == value);
        REQUIRE(encodedScene(fixture.model) == before);
        REQUIRE(fixture.model.undoStack()->index() == history);
        fixture.model.selection()->setSelectedEntity(sun);
        enterScalar(fixture.host, "SunIrradiance", value);
        REQUIRE(fixture.model.scene()->find(sun)->sun->irradiance == value);
        REQUIRE(core::isSunComponentValid(*fixture.model.scene()->find(sun)->sun));
        REQUIRE(scalarValue(fixture.host, "SunIrradiance") == value);
    }
    QTest::mouseClick(control<QCheckBox>(fixture.host, "AnimationPreview"), Qt::LeftButton);
    for (const auto value : values) {
        for (const int property : {0, 3, 4}) {
            const auto entity = property == 4 ? sun : fixture.cube;
            fixture.model.selection()->setSelectedEntity(entity);
            fixture.record(property, value);
            auto* table = control<QTableWidget>(fixture.host, "AnimationPropertyKeyTable");
            REQUIRE(table->rowCount() == 1);
            const auto point = table->visualItemRect(table->item(0, 0)).center();
            QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            REQUIRE(scalarValue(fixture.host, "AnimationPropertyValue") == value);
            const auto owner = property == 3 ? core::OwnerRef(core::MaterialOwner{material})
                                             : core::OwnerRef(core::EntityOwner{entity});
            const auto propertyId = property == 0 ? core::PropertyId::Position
                : property == 3 ? core::PropertyId::PbrRoughness : core::PropertyId::SunIrradiance;
            const auto component = property == 0 ? core::PropertyComponent::X : core::PropertyComponent::Scalar;
            const auto key = curveFor(fixture.model, owner, propertyId, component).keys.front();
            REQUIRE(key.value == value);
            const auto before = encodedScene(fixture.model);
            const auto document = fixture.model.apiDocumentState();
            const auto watermarks = fixture.model.scene()->idHighWatermarks();
            const auto history = fixture.model.undoStack()->index();
            const auto count = fixture.model.undoStack()->count();
            click(fixture.host, "AnimationRecordProperty");
            REQUIRE(encodedScene(fixture.model) == before);
            REQUIRE(fixture.model.apiDocumentState().documentRevision == document.documentRevision);
            REQUIRE(fixture.model.scene()->idHighWatermarks() == watermarks);
            REQUIRE(fixture.model.undoStack()->index() == history);
            REQUIRE(fixture.model.undoStack()->count() == count);
            REQUIRE(curveFor(fixture.model, owner, propertyId, component).keys.front() == key);
        }
    }
}

TEST_CASE("J1 scalar input failures retain text and Escape restores without authoring",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    fixture.createAndAssignMaterial();
    auto sunRequest = mutation<api::SunCreateRequest>(fixture.model);
    sunRequest.name = QStringLiteral("无效输入 Sun");
    const auto createdSun = fixture.model.createSunExplicit(sunRequest);
    REQUIRE(createdSun.hasValue());
    const auto sun = createdSun.value->createdEntityIds.front();
    const auto before = encodedScene(fixture.model);
    const auto history = fixture.model.undoStack()->index();
    for (const auto* name : {"PbrRoughness", "SunIrradiance"}) {
        fixture.model.selection()->setSelectedEntity(QString::fromLatin1(name) == QStringLiteral("PbrRoughness")
            ? fixture.cube : sun);
        auto* field = control<QLineEdit>(fixture.host, name);
        const auto original = field->text();
        field->selectAll();
        QTest::keyClicks(field, QStringLiteral("bad"));
        QFocusEvent focusOut(QEvent::FocusOut);
        QApplication::sendEvent(field, &focusOut);
        REQUIRE(field->text() == QStringLiteral("bad"));
        const auto* status = control<QLabel>(fixture.host,
            QString::fromLatin1(name) == QStringLiteral("PbrRoughness") ? "PbrMaterialStatus" : "SunStatus");
        REQUIRE(status->text().contains(QStringLiteral("有限")));
        enterScalarText(fixture.host, name, QStringLiteral("-0.1"));
        REQUIRE(field->text() == QStringLiteral("-0.1"));
        QTest::keyClick(field, Qt::Key_Escape);
        REQUIRE(field->text() == original);
    }
    fixture.model.selection()->setSelectedEntity(fixture.cube);
    auto* value = control<QLineEdit>(fixture.host, "AnimationPropertyValue");
    const auto propertyOriginal = value->text();
    enterScalarText(fixture.host, "AnimationPropertyValue", QStringLiteral("bad"));
    click(fixture.host, "AnimationRecordProperty");
    REQUIRE(value->text() == QStringLiteral("bad"));
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(value, &focusOut);
    REQUIRE(value->text() == QStringLiteral("bad"));
    QTest::keyClick(value, Qt::Key_Escape);
    REQUIRE(value->text() == propertyOriginal);
    REQUIRE(encodedScene(fixture.model) == before);
    REQUIRE(fixture.model.undoStack()->index() == history);
}

TEST_CASE("J1 scalar recording rejects an invalid frame instead of using the previous frame",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    fixture.record(0, 1);
    QTest::mouseClick(control<QCheckBox>(fixture.host, "AnimationPreview"), Qt::LeftButton);
    auto* frame = control<editor::CommitSpinBox>(fixture.host, "AnimationFrame");
    auto* input = frame->findChild<QLineEdit*>();
    REQUIRE(input);
    const auto before = encodedScene(fixture.model);
    const auto document = fixture.model.apiDocumentState();
    const auto watermarks = fixture.model.scene()->idHighWatermarks();
    const auto history = fixture.model.undoStack()->index();
    for (const auto& text : {QStringLiteral("0"), QStringLiteral("100001"), QStringLiteral("bad")}) {
        input->selectAll();
        QTest::keyClicks(input, text);
        QTest::keyClick(input, Qt::Key_Return);
        REQUIRE_FALSE(frame->validationMessage().isEmpty());
        enterScalar(fixture.host, "AnimationPropertyValue", 2);
        click(fixture.host, "AnimationRecordProperty");
        REQUIRE(input->text() == text);
        REQUIRE(fixture.model.animationFrame() == 1);
        REQUIRE(encodedScene(fixture.model) == before);
        REQUIRE(fixture.model.apiDocumentState().documentRevision == document.documentRevision);
        REQUIRE(fixture.model.scene()->idHighWatermarks() == watermarks);
        REQUIRE(fixture.model.undoStack()->index() == history);
    }
}

TEST_CASE("J1 scalar key table reads exact 256 key pages through the GUI",
          "[j1-gui-widget]") {
    WidgetAuthoring fixture;
    for (int index = 0; index < 257; ++index) {
        auto request = mutation<api::AnimationUpsertPropertyKeyframeRequest>(fixture.model);
        request.address = {fixture.model.scene()->documentUuid(), core::EntityOwner{fixture.cube},
            core::PropertyId::Position, core::PropertyComponent::X};
        request.time = index + 1;
        request.value = index;
        REQUIRE(fixture.model.upsertAnimationPropertyKeyframeExplicit(request).hasValue());
    }
    auto* table = control<QTableWidget>(fixture.host, "AnimationPropertyKeyTable");
    click(fixture.host, "AnimationPropertyFirstPage");
    REQUIRE(table->rowCount() == 256);
    REQUIRE(table->item(0, 0)->data(Qt::UserRole).toDouble() == 1);
    REQUIRE(table->item(255, 0)->data(Qt::UserRole).toDouble() == 256);
    const auto lastFirstPageId = table->item(255, 3)->text();
    const auto before = encodedScene(fixture.model);
    const auto document = fixture.model.apiDocumentState();
    const auto history = fixture.model.undoStack()->index();
    click(fixture.host, "AnimationPropertyNextPage");
    REQUIRE(table->rowCount() == 1);
    REQUIRE(table->item(0, 0)->data(Qt::UserRole).toDouble() == 257);
    REQUIRE(table->item(0, 1)->text().toDouble() == 256);
    REQUIRE(table->item(0, 3)->text() != lastFirstPageId);
    REQUIRE_FALSE(control<QPushButton>(fixture.host, "AnimationPropertyNextPage")->isEnabled());
    click(fixture.host, "AnimationPropertyFirstPage");
    REQUIRE(table->rowCount() == 256);
    REQUIRE(table->item(255, 3)->text() == lastFirstPageId);
    REQUIRE(encodedScene(fixture.model) == before);
    REQUIRE(fixture.model.apiDocumentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.model.undoStack()->index() == history);
}

TEST_CASE("J1 GUI creates records seeks undoes and saves the first real work",
          "[j1-gui-ui]") {
    const auto evidenceRoot = qEnvironmentVariable("MINI3D_J1_GUI_EVIDENCE",
        qEnvironmentVariable("MINI3D_J1_TEST_ROOT"));
    REQUIRE(QDir::isAbsolutePath(evidenceRoot));
    REQUIRE(QDir(evidenceRoot).absolutePath().startsWith(QStringLiteral("E:/"), Qt::CaseInsensitive));
    REQUIRE(QFileInfo(evidenceRoot).isDir());
    QTemporaryDir directory(QDir(evidenceRoot).filePath(QStringLiteral("j1-gui-author-XXXXXX")));
    REQUIRE(directory.isValid());
    directory.setAutoRemove(false);
    INFO("J1 GUI evidence: " << directory.path().toStdString());
    std::shared_ptr<const core::Scene> retainedScene;
    std::string retainedEncoded;
    const auto previousSettingsFormat = QSettings::defaultFormat();
    {
        GuiAuthoring fixture(directory.path());
        const auto cube = fixture.create("CreateCube");
        const auto camera = fixture.create("CreateCamera");
        const auto sun = fixture.create("CreateSun");
        REQUIRE(fixture.model->scene()->find(camera)->camera);
        REQUIRE(fixture.model->scene()->find(sun)->sun);
        enterInteger(fixture.window, "AnimationFps", 24);
        enterInteger(fixture.window, "AnimationStart", 1);
        enterInteger(fixture.window, "AnimationEnd", 24);
        click(fixture.window, "AnimationApplySettings");
        REQUIRE(fixture.model->scene()->animation().settings.fps == 24);
        REQUIRE(fixture.model->scene()->animation().properties.loopPolicy == core::AnimationLoopPolicy::InclusiveFrameStep);
        fixture.select(cube);
        fixture.showAppearance("CreatePbrMaterial");
        control<QAction>(fixture.window, "CreatePbrMaterial")->trigger();
        click(fixture.window, "AssignPbrMaterial");
        const auto materialSlot = fixture.model->scene()->find(cube)->materialSlots.front();
        const auto material = materialSlot.material;
        fixture.showAppearance("PbrRoughness");
        enterScalar(fixture.window, "PbrRoughness", .2);
        fixture.record(cube, 0, 0);
        fixture.record(cube, 3, .2);
        fixture.record(sun, 4, 1);
        QTest::mouseClick(control<QCheckBox>(fixture.window, "AnimationPreview"), Qt::LeftButton);
        fixture.seek(24);
        fixture.record(cube, 0, 2);
        fixture.record(cube, 3, .8);
        fixture.record(sun, 4, 2);
        QJsonArray stamps;
        const std::array<double, 4> frames{24, 1, 12.5, 1};
        const std::array<std::array<double, 3>, 4> expected{{{2, .8, 2}, {0, .2, 1},
            {1, .5, 1.5}, {0, .2, 1}}};
        std::uint64_t lastFrameId = 0;
        for (std::size_t index = 0; index < frames.size(); ++index) {
            fixture.seek(frames[index]);
            const auto pose = fixture.model->installedAnimationPose();
            REQUIRE(pose);
            REQUIRE(pose->sceneState);
            REQUIRE(pose->sceneState->source.frameTime == frames[index]);
            const auto& content = *pose->sceneState->content;
            const auto x = pose->sceneState->find(cube)->localPosition.x;
            const auto roughness = content.materials.at(material).effective.roughness;
            const auto light = std::find_if(content.visibleSuns.begin(), content.visibleSuns.end(),
                [sun](const auto& value) { return value.entity == sun; });
            REQUIRE(light != content.visibleSuns.end());
            REQUIRE(std::abs(x - expected[index][0]) <= 1e-12);
            REQUIRE(std::abs(roughness - expected[index][1]) <= 1e-12);
            REQUIRE(std::abs(light->effective.irradiance - expected[index][2]) <= 1e-12);
            const auto frameId = fixture.paint();
            REQUIRE(frameId > lastFrameId);
            lastFrameId = frameId;
            stamps.append(QJsonObject{{"frame", frames[index]}, {"x", x}, {"roughness", roughness},
                {"irradiance", light->effective.irradiance}, {"frameId", QString::number(frameId)},
                {"evaluationId", QString::number(pose->identity.evaluationId)},
                {"contentRevision", QString::number(pose->sceneState->source.contentRevision)}});
            if (frames[index] == 12.5) {
                fixture.select(cube);
                fixture.showAppearance("PbrRoughness");
                control<QComboBox>(fixture.window, "AnimationProperty")->setCurrentIndex(3);
                REQUIRE(scalarValue(fixture.window, "PbrRoughness") == .2);
                REQUIRE(scalarValue(fixture.window, "AnimationPropertyValue") == .5);
                REQUIRE(fixture.window.isVisible());
                REQUIRE(fixture.window.grab().save(directory.filePath("visible-midpoint-window.png")));
            }
        }
        fixture.select(cube);
        fixture.showAppearance("PbrRoughness");
        enterScalar(fixture.window, "PbrRoughness", .35);
        REQUIRE(fixture.model->scene()->materialData().find(material)->parameters.roughness == .35);
        control<QAction>(fixture.window, "Undo")->trigger();
        REQUIRE(fixture.model->scene()->materialData().find(material)->parameters.roughness == .2);
        control<QAction>(fixture.window, "Redo")->trigger();
        REQUIRE(fixture.model->scene()->materialData().find(material)->parameters.roughness == .35);
        fixture.seek(24);
        const auto keyId = curveFor(*fixture.model, core::EntityOwner{cube}, core::PropertyId::Position,
            core::PropertyComponent::X).keys.back().id;
        fixture.record(cube, 0, 3);
        REQUIRE(curveFor(*fixture.model, core::EntityOwner{cube}, core::PropertyId::Position,
            core::PropertyComponent::X).keys.back().id == keyId);
        control<QAction>(fixture.window, "Undo")->trigger();
        REQUIRE(curveFor(*fixture.model, core::EntityOwner{cube}, core::PropertyId::Position,
            core::PropertyComponent::X).keys.back().value == 2);
        control<QAction>(fixture.window, "Redo")->trigger();
        REQUIRE(curveFor(*fixture.model, core::EntityOwner{cube}, core::PropertyId::Position,
            core::PropertyComponent::X).keys.back().value == 3);
        control<QAction>(fixture.window, "Undo")->trigger();
        const auto before = encodedScene(*fixture.model);
        const auto uuid = fixture.model->scene()->documentUuid();
        const auto watermarks = fixture.model->scene()->idHighWatermarks();
        const auto path = directory.filePath("gui-authored-v5.m3dscene");
        answerFileAction(fixture.window, "SaveSceneAs", path);
        REQUIRE(QFileInfo::exists(path));
        REQUIRE_FALSE(fixture.model->isModified());
        control<QAction>(fixture.window, "NewScene")->trigger();
        REQUIRE(fixture.model->scene()->nodes().empty());
        answerFileAction(fixture.window, "OpenScene", path);
        REQUIRE(encodedScene(*fixture.model) == before);
        REQUIRE(fixture.model->scene()->documentUuid() == uuid);
        REQUIRE(fixture.model->scene()->idHighWatermarks() == watermarks);
        REQUIRE(fixture.model->scene()->find(cube)->materialSlots.front() == materialSlot);
        REQUIRE(fixture.model->scene()->find(camera)->camera);
        REQUIRE(fixture.model->scene()->find(sun)->sun->irradiance == 1);
        REQUIRE_FALSE(fixture.model->isModified());
        const auto reopenedHandle = fixture.model->apiDocumentState().document;
        QTest::mouseClick(control<QCheckBox>(fixture.window, "AnimationPreview"), Qt::LeftButton);
        fixture.seek(12.5);
        const auto reopened = fixture.model->installedAnimationPose();
        REQUIRE(reopened);
        REQUIRE(reopened->sceneState);
        REQUIRE(reopened->identity.documentId == reopenedHandle.documentId);
        REQUIRE(std::abs(reopened->sceneState->find(cube)->localPosition.x - 1) <= 1e-12);
        REQUIRE(std::abs(reopened->sceneState->content->materials.at(material).effective.roughness - .5) <= 1e-12);
        REQUIRE(fixture.paint() > lastFrameId);
        QFile evidence(directory.filePath("gui-authoring-paints.json"));
        REQUIRE(evidence.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        const auto bytes = QJsonDocument(stamps).toJson();
        REQUIRE(evidence.write(bytes) == bytes.size());
        evidence.close();
        retainedScene = fixture.model->scene();
        retainedEncoded = core::SceneSerializer::encode(core::SceneDocumentData(*retainedScene));
        REQUIRE(fixture.window.close());
        REQUIRE(encodedScene(*fixture.model) == before);
    }
    REQUIRE(QSettings::defaultFormat() == previousSettingsFormat);
    REQUIRE(core::SceneSerializer::encode(core::SceneDocumentData(*retainedScene)) == retainedEncoded);
}

TEST_CASE("J1 real GUI work survives API equivalence independent output and isolated pixel changes",
          "[j1-joint-ui][gpu]") {
    const auto evidenceRoot = qEnvironmentVariable("MINI3D_J1_GUI_EVIDENCE",
        qEnvironmentVariable("MINI3D_J1_TEST_ROOT"));
    REQUIRE(QDir::isAbsolutePath(evidenceRoot));
    REQUIRE(QDir(evidenceRoot).absolutePath().startsWith(QStringLiteral("E:/"), Qt::CaseInsensitive));
    REQUIRE(QFileInfo(evidenceRoot).isDir());
    QTemporaryDir directory(QDir(evidenceRoot).filePath(QStringLiteral("j1-joint-author-output-XXXXXX")));
    REQUIRE(directory.isValid());
    directory.setAutoRemove(false);
    INFO("J1 joint evidence: " << directory.path().toStdString());
    const auto manifestPath = QFileInfo(QString::fromUtf8(MINI3D_J1_RENDER_MANIFEST)).absoluteDir()
        .filePath(QStringLiteral("j1-joint-author-output-manifest.json"));
    const auto manifestBytes = readJointBytes(manifestPath);
    const auto manifestSha = QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256).toHex().toUpper();
    REQUIRE(manifestSha == "3CAC19735108C6C1344729ADB94C4FDA7FEE48924FB17F389DF71BA76C0A76E2");
    const auto manifest = QJsonDocument::fromJson(manifestBytes).object();
    REQUIRE(manifest["task"].toString() == "EXEC-015");
    writeJointEvidence(directory.filePath("frozen-input-manifest.json"), manifestBytes);
    writeJointJson(directory.filePath("author-path.json"), {
        {"manifestSha256", QString::fromLatin1(manifestSha)},
        {"guiPath", "MainWindow menu/visible controls: entities/material/assignment/camera/keys/seek/base roughness/history/V5"},
        {"fixedNonAnimatedBaseColorPath", "accepted window.apiService().updateMaterial command after GUI material allocation"},
        {"baseColorGuiControlAvailable", false}, {"directSceneMutation", false},
        {"crossDocumentIdentityPolicy", "independent real allocation; compare semantics by role only"}});

    GuiAuthoring fixture(directory.path());
    JointCaptureLayout layout(fixture);
    JointRoles roles;
    roles.cube = fixture.create("CreateCube");
    roles.sun = fixture.create("CreateSun");
    control<QAction>(fixture.window, "FrontView")->trigger();
    roles.camera = fixture.create("CreateCamera");
    fixture.select(roles.cube);
    showJointControl(fixture, "CreatePbrMaterial");
    control<QAction>(fixture.window, "CreatePbrMaterial")->trigger();
    showJointControl(fixture, "AssignPbrMaterial");
    click(fixture.window, "AssignPbrMaterial");
    REQUIRE(fixture.model->scene()->find(roles.cube)->materialSlots.size() == 1);
    const auto materialSlot = fixture.model->scene()->find(roles.cube)->materialSlots.front();
    roles.material = materialSlot.material;
    roles.materialSlot = materialSlot.id;
    REQUIRE(roles.material.isValid());
    REQUIRE(roles.materialSlot.isValid());
    auto colorRequest = mutation<api::MaterialUpdateRequest>(*fixture.model);
    colorRequest.materialId = roles.material;
    colorRequest.parameters = fixture.model->scene()->materialData().find(roles.material)->parameters;
    colorRequest.parameters.baseColor = {.8, .2, .1};
    const auto colorHistory = fixture.model->undoStack()->index();
    REQUIRE(fixture.window.apiService().updateMaterial(colorRequest).hasValue());
    REQUIRE(fixture.model->scene()->materialData().find(roles.material)->parameters == colorRequest.parameters);
    REQUIRE(fixture.model->undoStack()->index() == colorHistory + 1);
    REQUIRE(fixture.model->scene()->find(roles.cube)->materialSlots.front() == materialSlot);
    showJointControl(fixture, "PbrRoughness");
    enterScalar(fixture.window, "PbrRoughness", .2);
    REQUIRE(fixture.model->scene()->materialData().find(roles.material)->parameters.roughness == .2);

    fixture.select(roles.camera);
    jointNumber(fixture, "PositionX", 1, true);
    jointNumber(fixture, "PositionY", 0, true);
    jointNumber(fixture, "PositionZ", 6, true);
    for (const auto* axis : {"RotationX", "RotationY", "RotationZ"})
        jointNumber(fixture, axis, 0, true);
    jointNumber(fixture, "CameraFov", 45);
    jointNumber(fixture, "CameraNear", .1);
    jointNumber(fixture, "CameraFar", 100);
    const auto* cameraNode = fixture.model->scene()->find(roles.camera);
    REQUIRE(cameraNode->transform.position == glm::vec3(1, 0, 6));
    REQUIRE(cameraNode->transform.rotation == glm::quat(1, 0, 0, 0));
    REQUIRE(cameraNode->transform.scale == glm::vec3(1));
    REQUIRE((cameraNode->camera == core::CameraComponent{45, .1F, 100}));
    showJointControl(fixture, "PreviewCamera");
    click(fixture.window, "PreviewCamera");
    REQUIRE(fixture.model->previewCamera() == roles.camera);
    setJointCameraPreview(fixture, roles, false);
    fixture.select(roles.sun);
    showJointControl(fixture, "SunIrradiance");
    enterScalar(fixture.window, "SunIrradiance", 1);
    REQUIRE((fixture.model->scene()->find(roles.sun)->sun == core::SunComponent{1, {1, 1, 1}}));
    REQUIRE(fixture.model->scene()->find(roles.sun)->transform.rotation == glm::quat(1, 0, 0, 0));
    for (const auto* name : {"AnimationFps", "AnimationStart", "AnimationEnd", "AnimationApplySettings"})
        REQUIRE_FALSE(control<QWidget>(fixture.window, name)->visibleRegion().isEmpty());
    enterInteger(fixture.window, "AnimationFps", 24);
    enterInteger(fixture.window, "AnimationStart", 1);
    enterInteger(fixture.window, "AnimationEnd", 24);
    click(fixture.window, "AnimationApplySettings");
    REQUIRE(fixture.model->scene()->animation().settings.fps == 24);
    REQUIRE(fixture.model->scene()->animation().settings.startFrame == 1);
    REQUIRE(fixture.model->scene()->animation().settings.endFrame == 24);
    REQUIRE(fixture.model->scene()->animation().properties.loopPolicy == core::AnimationLoopPolicy::InclusiveFrameStep);
    fixture.record(roles.cube, 0, 0);
    fixture.record(roles.cube, 3, .2);
    fixture.record(roles.sun, 4, 1);
    jointPreview(fixture, true);
    fixture.seek(24);
    fixture.record(roles.cube, 0, 2);
    fixture.record(roles.cube, 3, .8);
    fixture.record(roles.sun, 4, 2);
    setJointCameraPreview(fixture, roles, true);
    const auto addresses = roles.addresses(fixture.model->scene()->documentUuid());
    const std::array<core::ScalarCurve, 3> originalCurves{
        curveFor(*fixture.model, addresses[0].owner, addresses[0].property, addresses[0].component),
        curveFor(*fixture.model, addresses[1].owner, addresses[1].property, addresses[1].component),
        curveFor(*fixture.model, addresses[2].owner, addresses[2].property, addresses[2].component)};
    const auto originalBindings = fixture.model->scene()->animation().properties.bindings;
    const std::array<double, 4> times{24, 1, 12.5, 1};
    const std::array<std::array<double, 3>, 4> expected{{{2, .8, 2}, {0, .2, 1},
        {1, .5, 1.5}, {0, .2, 1}}};
    std::uint64_t previousPaint = 0;
    for (std::size_t index = 0; index < times.size(); ++index) {
        fixture.seek(times[index]);
        const auto prefix = directory.filePath(QString("gui-seek-%1").arg(index + 1));
        const auto painted = jointPaint(fixture, prefix);
        REQUIRE(painted->stamp().frameId > previousPaint);
        previousPaint = painted->stamp().frameId;
        REQUIRE(painted->pixels().source.frameTime == times[index]);
        REQUIRE((painted->pixels().source.rationalFps == core::RationalFps{24, 1}));
        const auto values = jointValues(*fixture.model, roles);
        writeJointJson(prefix + "-values.json", {{"frame", times[index]},
            {"actual", QJsonArray{values[0], values[1], values[2]}},
            {"expected", QJsonArray{expected[index][0], expected[index][1], expected[index][2]}}});
        writeJointJson(prefix + "-roles.json", jointRoleMap(*fixture.model, roles));
        requireJointValues(values, expected[index]);
        if (times[index] == 12.5) {
            fixture.select(roles.cube);
            showJointControl(fixture, "PbrRoughness");
            control<QComboBox>(fixture.window, "AnimationProperty")->setCurrentIndex(3);
            REQUIRE(scalarValue(fixture.window, "PbrRoughness") == .2);
            REQUIRE(scalarValue(fixture.window, "AnimationPropertyValue") == .5);
        }
        saveJointWindow(fixture, prefix + "-visible-window.png");
    }

    setJointCameraPreview(fixture, roles, false);
    fixture.select(roles.cube);
    showJointControl(fixture, "PbrRoughness");
    const auto originalMaterial = *fixture.model->scene()->materialData().find(roles.material);
    const auto beforeBase = encodedScene(*fixture.model);
    const auto beforeBaseAnimation = fixture.model->scene()->animation();
    const auto stableWatermarks = fixture.model->scene()->idHighWatermarks();
    const auto baseHistory = fixture.model->undoStack()->index();
    enterScalar(fixture.window, "PbrRoughness", .35);
    auto editedMaterial = originalMaterial;
    editedMaterial.parameters.roughness = .35;
    const auto afterBase = encodedScene(*fixture.model);
    REQUIRE(*fixture.model->scene()->materialData().find(roles.material) == editedMaterial);
    REQUIRE(fixture.model->scene()->animation() == beforeBaseAnimation);
    REQUIRE(fixture.model->undoStack()->index() == baseHistory + 1);
    control<QAction>(fixture.window, "Undo")->trigger();
    REQUIRE(encodedScene(*fixture.model) == beforeBase);
    REQUIRE(*fixture.model->scene()->materialData().find(roles.material) == originalMaterial);
    REQUIRE(fixture.model->undoStack()->index() == baseHistory);
    control<QAction>(fixture.window, "Redo")->trigger();
    REQUIRE(encodedScene(*fixture.model) == afterBase);
    REQUIRE(*fixture.model->scene()->materialData().find(roles.material) == editedMaterial);
    REQUIRE(fixture.model->undoStack()->index() == baseHistory + 1);
    REQUIRE(fixture.model->scene()->idHighWatermarks() == stableWatermarks);
    fixture.seek(24);
    const auto beforeKey = encodedScene(*fixture.model);
    const auto keyHistory = fixture.model->undoStack()->index();
    fixture.record(roles.cube, 0, 3);
    auto editedCurve = originalCurves[0];
    editedCurve.keys.back().value = 3;
    const auto afterKey = encodedScene(*fixture.model);
    REQUIRE(curveFor(*fixture.model, addresses[0].owner, addresses[0].property, addresses[0].component) == editedCurve);
    REQUIRE(fixture.model->undoStack()->index() == keyHistory + 1);
    control<QAction>(fixture.window, "Undo")->trigger();
    REQUIRE(encodedScene(*fixture.model) == beforeKey);
    REQUIRE(fixture.model->undoStack()->index() == keyHistory);
    control<QAction>(fixture.window, "Redo")->trigger();
    REQUIRE(encodedScene(*fixture.model) == afterKey);
    REQUIRE(fixture.model->undoStack()->index() == keyHistory + 1);
    control<QAction>(fixture.window, "Undo")->trigger();
    REQUIRE(encodedScene(*fixture.model) == beforeKey);
    REQUIRE(fixture.model->undoStack()->index() == keyHistory);
    REQUIRE(fixture.model->scene()->idHighWatermarks() == stableWatermarks);
    REQUIRE(fixture.model->scene()->find(roles.cube)->materialSlots.front() == materialSlot);
    REQUIRE(fixture.model->scene()->animation().properties.bindings == originalBindings);
    for (std::size_t index = 0; index < addresses.size(); ++index)
        REQUIRE(curveFor(*fixture.model, addresses[index].owner,
            addresses[index].property, addresses[index].component) == originalCurves[index]);
    writeJointJson(directory.filePath("gui-before-save-roles.json"), jointRoleMap(*fixture.model, roles));
    const auto beforeSave = encodedScene(*fixture.model);
    const auto beforeSaveUuid = fixture.model->scene()->documentUuid();
    const auto beforeSaveHandle = fixture.model->apiDocumentState().document;
    const auto path = directory.filePath("joint-gui-authored-v5.m3dscene");
    REQUIRE_FALSE(QFileInfo::exists(path));
    answerFileAction(fixture.window, "SaveSceneAs", path);
    REQUIRE(QJsonDocument::fromJson(readJointBytes(path)).object()["version"].toInt() == 5);
    REQUIRE_FALSE(fixture.model->isModified());
    control<QAction>(fixture.window, "NewScene")->trigger();
    REQUIRE(fixture.model->scene()->nodes().empty());
    answerFileAction(fixture.window, "OpenScene", path);
    REQUIRE(encodedScene(*fixture.model) == beforeSave);
    REQUIRE(fixture.model->scene()->documentUuid() == beforeSaveUuid);
    REQUIRE(fixture.model->apiDocumentState().document != beforeSaveHandle);
    REQUIRE(fixture.model->scene()->idHighWatermarks() == stableWatermarks);
    REQUIRE(fixture.model->scene()->find(roles.cube)->materialSlots.front() == materialSlot);
    REQUIRE(*fixture.model->scene()->materialData().find(roles.material) == editedMaterial);
    REQUIRE(fixture.model->scene()->animation().properties.bindings == originalBindings);
    for (std::size_t index = 0; index < addresses.size(); ++index)
        REQUIRE(curveFor(*fixture.model, addresses[index].owner,
            addresses[index].property, addresses[index].component) == originalCurves[index]);
    REQUIRE_FALSE(fixture.model->isModified());
    fixture.select(roles.camera);
    showJointControl(fixture, "PreviewCamera");
    click(fixture.window, "PreviewCamera");
    jointPreview(fixture, true);
    fixture.seek(12.5);
    const auto reopened = jointPaint(fixture, directory.filePath("gui-reopened"));
    REQUIRE(reopened->stamp().frameId > previousPaint);
    previousPaint = reopened->stamp().frameId;
    REQUIRE(fixture.model->installedAnimationPose()->identity.documentId ==
        fixture.model->apiDocumentState().document.documentId);
    requireJointValues(jointValues(*fixture.model, roles), {1, .5, 1.5});
    saveJointWindow(fixture, directory.filePath("gui-reopened-visible-window.png"));
    const auto guiSemantics = jointAuthorSemantics(*fixture.model, roles);
    writeJointJson(directory.filePath("gui-reopened-roles.json"), jointRoleMap(*fixture.model, roles));
    writeJointJson(directory.filePath("gui-author-semantics.json"), guiSemantics);

    editor::SceneViewModel apiModel;
    api::EditorApiService apiService(apiModel);
    auto newRequest = mutation<api::DocumentNewRequest>(apiModel);
    newRequest.ifDirty = api::IfDirty::Discard;
    REQUIRE(apiService.newDocument(newRequest).hasValue());
    REQUIRE(apiModel.scene()->nodes().empty());
    JointRoles apiRoles;
    auto makeCube = mutation<api::EntityCreateRequest>(apiModel);
    makeCube.name = QStringLiteral("立方体");
    makeCube.primitive = core::PrimitiveKind::Cube;
    const auto madeCube = apiService.createEntity(makeCube);
    REQUIRE(madeCube.hasValue());
    apiRoles.cube = madeCube.value->createdEntityIds.front();
    auto makeSun = mutation<api::SunCreateRequest>(apiModel);
    makeSun.name = QStringLiteral("Sun");
    const auto madeSun = apiService.createSun(makeSun);
    REQUIRE(madeSun.hasValue());
    apiRoles.sun = madeSun.value->createdEntityIds.front();
    auto makeCamera = mutation<api::CameraCreateRequest>(apiModel);
    makeCamera.name = QStringLiteral("相机");
    makeCamera.transform.position = {1, 0, 6};
    makeCamera.camera = {45, .1F, 100};
    const auto madeCamera = apiService.createCamera(makeCamera);
    REQUIRE(madeCamera.hasValue());
    apiRoles.camera = madeCamera.value->createdEntityIds.front();
    auto makeMaterial = mutation<api::MaterialCreateRequest>(apiModel);
    makeMaterial.parameters.baseColor = {.8, .2, .1};
    makeMaterial.parameters.roughness = .2;
    const auto madeMaterial = apiService.createMaterial(makeMaterial);
    REQUIRE(madeMaterial.hasValue());
    apiRoles.material = madeMaterial.value->materialId;
    auto assign = mutation<api::EntityAssignMaterialRequest>(apiModel);
    assign.entityId = apiRoles.cube;
    assign.materialId = apiRoles.material;
    REQUIRE(apiService.assignMaterial(assign).hasValue());
    apiRoles.materialSlot = apiModel.scene()->find(apiRoles.cube)->materialSlots.front().id;
    auto settings = mutation<api::AnimationSetSettingsRequest>(apiModel);
    settings.settings = {24, 1, 24};
    REQUIRE(apiService.setAnimationSettings(settings).hasValue());
    const auto apiAddresses = apiRoles.addresses(apiModel.scene()->documentUuid());
    for (const auto time : {1.0, 24.0}) {
        const auto values = time == 1 ? std::array<double, 3>{0, .2, 1}
                                      : std::array<double, 3>{2, .8, 2};
        for (std::size_t index = 0; index < apiAddresses.size(); ++index) {
            auto key = mutation<api::AnimationUpsertPropertyKeyframeRequest>(apiModel);
            key.address = apiAddresses[index];
            key.time = time;
            key.value = values[index];
            const auto result = apiService.upsertAnimationPropertyKeyframe(key);
            REQUIRE(result.hasValue());
            REQUIRE(result.value->keyIdentity);
            REQUIRE(result.value->keyIdentity->keyId.isValid());
        }
    }
    auto apiBase = mutation<api::MaterialUpdateRequest>(apiModel);
    apiBase.materialId = apiRoles.material;
    apiBase.parameters = apiModel.scene()->materialData().find(apiRoles.material)->parameters;
    apiBase.parameters.roughness = .35;
    REQUIRE(apiService.updateMaterial(apiBase).hasValue());
    writeJointJson(directory.filePath("api-author-roles.json"), jointRoleMap(apiModel, apiRoles));
    const auto apiSemantics = jointAuthorSemantics(apiModel, apiRoles);
    writeJointJson(directory.filePath("api-author-semantics.json"), apiSemantics);
    REQUIRE(apiModel.scene()->documentUuid() != fixture.model->scene()->documentUuid());
    REQUIRE(apiModel.apiDocumentState().document != fixture.model->apiDocumentState().document);
    REQUIRE(apiSemantics == guiSemantics);
    const JointUnchanged apiUnchanged(apiModel);
    QJsonArray apiSamples;
    for (std::size_t sequence = 0; sequence < times.size(); ++sequence) {
        api::AnimationSamplePropertiesRequest request;
        request.document = apiService.documentState().document;
        request.expectedDocumentRevision = apiService.documentState().documentRevision;
        request.frame = times[sequence];
        request.addresses.assign(apiAddresses.begin(), apiAddresses.end());
        const auto result = apiService.sampleAnimationProperties(request);
        REQUIRE(result.hasValue());
        REQUIRE(result.value->samples.size() == 3);
        REQUIRE(result.value->frame == times[sequence]);
        REQUIRE(result.value->source.documentUuid == apiModel.scene()->documentUuid());
        REQUIRE(result.value->source.frameTime == times[sequence]);
        REQUIRE_FALSE(core::isSameWorkSource(reopened->pixels().source, result.value->source));
        std::array<double, 3> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            REQUIRE(result.value->samples[index].address == apiAddresses[index]);
            values[index] = result.value->samples[index].value;
        }
        requireJointValues(values, expected[sequence]);
        apiSamples.append(QJsonObject{{"frame", times[sequence]},
            {"values", QJsonArray{values[0], values[1], values[2]}},
            {"documentUuid", QString::fromStdString(core::documentUuidString(result.value->source.documentUuid))},
            {"revisionDomain", QJsonObject{
                {"instanceId", QString::fromStdString(result.value->source.authorContextIdentity.revisionDomain.instanceId)},
                {"documentId", QString::fromStdString(result.value->source.authorContextIdentity.revisionDomain.documentId)}}},
            {"contentRevision", QString::number(result.value->source.contentRevision)},
            {"evaluationSessionId", QString::fromStdString(core::documentUuidString(
                core::DocumentUuid{result.value->source.evaluationSessionId.bytes}))},
            {"evaluationId", QString::number(result.value->source.evaluationId)}});
        apiUnchanged.verify();
    }
    writeJointEvidence(directory.filePath("api-real-samples.json"), QJsonDocument(apiSamples).toJson());

    QString diagnostic;
    const auto policy = editor::automation::FilePathPolicy::create({}, {directory.path()}, diagnostic);
    INFO(diagnostic.toStdString());
    REQUIRE(policy);
    fixture.window.apiService().setFileAccessPolicies({}, [policy](const QString& target, QString& reason) {
        return policy->authorizeNewFile(target, reason);
    });
    editor::ImageOutputService output(*fixture.model, fixture.window.apiService(), *fixture.viewport);
    fixture.viewport->setOverlayVisible(false);
    std::uint64_t previousOutputFrame = 0, previousOutputEvaluation = 0;
    for (std::size_t index = 0; index < times.size(); ++index) {
        layout.authoring();
        fixture.seek(times[index]);
        layout.capture();
        const auto prefix = directory.filePath(QString("same-frame-%1").arg(index + 1));
        const auto frame = jointOutputs(fixture, output, roles, prefix,
            previousOutputFrame, previousOutputEvaluation);
        requireJointValues(jointValues(*fixture.model, roles), expected[index]);
        REQUIRE(frame->pixels().source.frameTime == times[index]);
        REQUIRE(fixture.viewport->lastRenderedFrame()->frameId > previousPaint);
        previousPaint = fixture.viewport->lastRenderedFrame()->frameId;
        if (index == 0)
            saveJointWindow(fixture, directory.filePath("fixed-preview-visible-window.png"));
    }

    const auto successfulPath = directory.filePath("same-frame-4-output.png");
    const auto successfulBytes = readJointBytes(successfulPath);
    const JointUnchanged outputUnchanged(*fixture.model);
    const auto requestFor = [&](const QString& name) {
        auto request = mutation<editor::ImageOutputRequest>(*fixture.model);
        request.path = directory.filePath(name);
        request.frame = 12.5;
        request.camera = roles.camera;
        request.timeoutMs = int(api::limits::mutationTimeoutMaximumMs);
        return request;
    };
    const auto outputFiles = [&] {
        return QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    };
    {
        std::atomic_bool cancelled{false};
        auto request = requestFor("cancelled-after-validation.png");
        request.cancelled = &cancelled;
        int guards = 0;
        const auto beforeFiles = outputFiles();
        auto previous = fixture.window.apiService().exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            if (++guards == 2) {
                REQUIRE(QDir(directory.path()).entryList({".mini3d-image-*"}, QDir::Files | QDir::Hidden).size() == 1);
                cancelled.store(true);
            }
            return std::nullopt;
        });
        const auto restore = qScopeGuard([&] {
            fixture.window.apiService().exchangeBeforeCommitGuard(std::move(previous));
        });
        const auto result = output.write(request);
        REQUIRE(result.error);
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error->code == api::ErrorCode::Cancelled);
        REQUIRE(guards >= 2);
        REQUIRE_FALSE(QFileInfo::exists(request.path));
        REQUIRE(outputFiles() == beforeFiles);
        REQUIRE(readJointBytes(successfulPath) == successfulBytes);
        REQUIRE_FALSE(output.isPreparing());
        outputUnchanged.verify();
        writeJointJson(directory.filePath("cancelled-output-receipt.json"), {
            {"errorCode", int(result.error->code)}, {"guardCount", guards},
            {"newResultAbsent", true}, {"newFileAbsent", true}, {"previousFileUnchanged", true}});
    }
    for (int variant = 0; variant < 4; ++variant) {
        auto request = requestFor(QString("invalid-request-%1.png").arg(variant));
        if (variant == 0)
            request.frame = 0;
        else if (variant == 1)
            request.camera = roles.cube;
        else if (variant == 2)
            ++request.expectedDocumentRevision;
        else
            request.path = successfulPath;
        const auto beforeFiles = outputFiles();
        const auto result = output.write(request);
        REQUIRE(result.error);
        REQUIRE_FALSE(result.hasValue());
        if (variant != 3)
            REQUIRE_FALSE(QFileInfo::exists(request.path));
        REQUIRE(outputFiles() == beforeFiles);
        REQUIRE(readJointBytes(successfulPath) == successfulBytes);
        REQUIRE_FALSE(output.isPreparing());
        outputUnchanged.verify();
        writeJointJson(directory.filePath(QString("invalid-output-receipt-%1.json").arg(variant)), {
            {"variant", variant}, {"errorCode", int(result.error->code)},
            {"newResultAbsent", true}, {"previousFileUnchanged", true}});
    }

    const auto constantKeys = [&](double x, double roughness, double irradiance) {
        layout.authoring();
        setJointCameraPreview(fixture, roles, false);
        for (const auto time : {1.0, 24.0}) {
            fixture.seek(time);
            fixture.record(roles.cube, 0, x);
            fixture.record(roles.cube, 3, roughness);
            fixture.record(roles.sun, 4, irradiance);
        }
        for (std::size_t index = 0; index < addresses.size(); ++index) {
            const auto& curve = curveFor(*fixture.model, addresses[index].owner,
                addresses[index].property, addresses[index].component);
            REQUIRE(curve.id == originalCurves[index].id);
            REQUIRE(curve.keys.size() == 2);
            REQUIRE(curve.keys.front().id == originalCurves[index].keys.front().id);
            REQUIRE(curve.keys.back().id == originalCurves[index].keys.back().id);
            REQUIRE(curve.keys.front().time == 1);
            REQUIRE(curve.keys.back().time == 24);
        }
        REQUIRE(fixture.model->scene()->animation().properties.bindings == originalBindings);
        REQUIRE(fixture.model->scene()->idHighWatermarks() == stableWatermarks);
        fixture.seek(12.5);
        requireJointValues(jointValues(*fixture.model, roles), {x, roughness, irradiance});
        setJointCameraPreview(fixture, roles, true);
        layout.capture();
    };
    const auto isolated = manifest["isolatedChanges"].toObject();
    constantKeys(0, .5, 1.5);
    const auto positionBefore = jointOutputs(fixture, output, roles, directory.filePath("isolated-x-0"),
        previousOutputFrame, previousOutputEvaluation);
    constantKeys(2, .5, 1.5);
    const auto positionAfter = jointOutputs(fixture, output, roles, directory.filePath("isolated-x-2"),
        previousOutputFrame, previousOutputEvaluation);
    requireJointRoiChange(*positionBefore, *positionAfter, isolated["position"].toObject()["roi"].toArray(),
        directory.filePath("isolated-position-roi-count.json"));
    constantKeys(1, .5, 1);
    const auto irradianceBefore = jointOutputs(fixture, output, roles, directory.filePath("isolated-sun-1"),
        previousOutputFrame, previousOutputEvaluation);
    constantKeys(1, .5, 2);
    const auto irradianceAfter = jointOutputs(fixture, output, roles, directory.filePath("isolated-sun-2"),
        previousOutputFrame, previousOutputEvaluation);
    requireJointRoiChange(*irradianceBefore, *irradianceAfter, isolated["irradiance"].toObject()["roi"].toArray(),
        directory.filePath("isolated-irradiance-roi-count.json"));
    layout.authoring();
    setJointCameraPreview(fixture, roles, false);
    jointPreview(fixture, false);
    fixture.select(roles.cube);
    jointNumber(fixture, "RotationY", 15, true);
    REQUIRE(fixture.model->scene()->find(roles.cube)->transform.rotation ==
        glm::quat(glm::vec3(0, glm::radians(15.0F), 0)));
    jointPreview(fixture, true);
    constantKeys(1, .2, 1.5);
    const auto roughnessBefore = jointOutputs(fixture, output, roles, directory.filePath("isolated-roughness-02"),
        previousOutputFrame, previousOutputEvaluation);
    constantKeys(1, .8, 1.5);
    const auto roughnessAfter = jointOutputs(fixture, output, roles, directory.filePath("isolated-roughness-08"),
        previousOutputFrame, previousOutputEvaluation);
    requireJointRoiChange(*roughnessBefore, *roughnessAfter, isolated["roughness"].toObject()["roi"].toArray(),
        directory.filePath("isolated-roughness-roi-count.json"));
    writeJointJson(directory.filePath("joint-chain-completed.json"), {
        {"manifestSha256", QString::fromLatin1(manifestSha)}, {"guiAuthorToV5ToOutput", true},
        {"apiRoleSemanticsEqual", true}, {"sameFramePngExrPairs", 4},
        {"independentFixedRoiChanges", 3}, {"invalidAndCancelledRequestsPublishNothing", true}});
}
