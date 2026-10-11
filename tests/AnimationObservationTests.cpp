/*
 * 模块名: AnimationObservationTests
 * 功能概述: 用真实 Qt/GL 验证指定动画帧的完整身份、主动失效和捕获重入隔离。
 * 对外接口: Catch2 [animation-observation]；不启动桥、不写用户场景。
 * 依赖关系: MainWindow、SceneViewModel、ObservationService/Codec、Qt Test。
 * 输入输出: 隔离动画、显式捕获请求到真实 PNG、绘制戳和结构化失败。
 * 异常与错误: 假身份、旧会话、活动播放/草稿和晚回调均不能产生成功截图。
 * 维护说明: 应用线程串行，条件等待仅用于测试；不构造伪造 PNG 充当真实帧。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/AnimationDraftGesture.h"
#include "editor/observation/ObservationJsonCodec.h"
#include "editor/observation/ObservationService.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QPointer>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

using namespace mini3d;
namespace {
namespace api = editor::api;
namespace observation = editor::observation;
using Viewport = renderer_gl::ViewportWidget;
using Error = api::ErrorCode;

/** @brief 提供正式应用装配的隔离动画窗口；析构隐藏窗口并停止会话。 */
struct AnimationObservationWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = nullptr;
    Viewport* viewport = nullptr;
    core::EntityId cube = 0;
    explicit AnimationObservationWindow(bool legacy = true) {
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.show();
        REQUIRE(QTest::qWaitForWindowExposed(&window));
        model = window.findChild<editor::SceneViewModel*>();
        viewport = window.findChild<Viewport*>();
        REQUIRE(model);
        REQUIRE(viewport);
        model->newScene();
        if (!legacy)
            return;
        cube = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(cube != 0);
        core::SceneAnimation animation;
        animation.settings = {24, 1, 49};
        animation.tracks[{cube, core::AnimationChannel::Position}] = {
            {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
             {49, {4, 0, 0}, core::AnimationInterpolation::Linear}}};
        animation.tracks[{cube, core::AnimationChannel::RotationEulerXYZDegrees}] = {
            {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
             {49, {0, 720, 0}, core::AnimationInterpolation::Linear}}};
        REQUIRE(model->replaceAnimation(animation));
    }
    ~AnimationObservationWindow() { window.hide(); }
    /** @brief 读取同应用线程的真实显示包，供错误身份的定点注入使用。 */
    renderer_gl::FrameDisplayState displayState() const {
        const auto state = model->apiDocumentState();
        return {{state.document.instanceId, state.document.documentId,
                 state.documentRevision, state.historyRevision},
                {state.document.instanceId, state.document.documentId, state.documentRevision,
                 model->animationEvaluationId(), model->animationFrame(), model->animationMode()},
                model->animationSessionRevision(), model->installedAnimationPose()};
    }
    /** @brief 冻结当前文档、观察版本及求值 ID；要求观察服务可用。 */
    observation::CaptureRequest request() {
        const auto view = window.observationService().getState({model->apiDocumentState().document});
        REQUIRE(view.hasValue());
        REQUIRE(view.value->viewport.animation);
        observation::CaptureRequest request;
        request.document = model->apiDocumentState().document;
        request.expectedDocumentRevision = model->apiDocumentState().documentRevision;
        request.expectedViewportRevision = view.value->viewport.viewportRevision;
        request.expectedEvaluationId = view.value->viewport.animation->evaluationId;
        request.longestEdge = 320;
        return request;
    }
    /** @brief 将正式动画安装为指定暂停帧，不使用 sample 冒充 GUI 显示。 */
    void preview(core::FrameTime frame = 13) {
        REQUIRE(model->setAnimationPreview(true));
        REQUIRE(model->setAnimationFrame(frame));
    }
};

/** @brief 持有一次真实异步请求；退出测试作用域时先取消，避免回调悬空。 */
struct CaptureAttempt {
    observation::ObservationService& service;
    std::optional<api::ApiResult<observation::CaptureResult>> result;
    std::optional<renderer_gl::RenderedFrame> completionFrame;
    QString id;
    int calls = 0;
    CaptureAttempt(AnimationObservationWindow& fixture, const observation::CaptureRequest& request)
        : service(fixture.window.observationService()) {
        id = service.capture(request, [this, viewport = fixture.viewport](auto completed) {
            ++calls;
            completionFrame = viewport->lastRenderedFrame();
            result = std::move(completed);
        });
    }
    ~CaptureAttempt() { service.cancelCapture(id); }
    void wait() {
        REQUIRE(QTest::qWaitFor([this] { return result.has_value(); }, 6000));
        REQUIRE(calls == 1);
        REQUIRE_FALSE(service.isCapturing());
    }
    void requireError(Error error) const {
        REQUIRE(result);
        REQUIRE(result->error);
        REQUIRE(result->error->code == error);
        REQUIRE_FALSE(result->hasValue());
        REQUIRE(calls == 1);
    }
};

glm::dvec3 manifestVector(const QJsonValue& value) {
    const auto vector = value.toArray();
    REQUIRE(vector.size() == 3);
    return {vector[0].toDouble(), vector[1].toDouble(), vector[2].toDouble()};
}

/** @brief 只安装隔离作者夹具；随后经正式动画/相机入口驱动真实 MainWindow。 */
struct JointObservationWindow : AnimationObservationWindow {
    QJsonObject manifest;
    core::SceneAnimation animation;
    core::EntityId sun = 0, camera = 0;
    core::MaterialId material;
    QSize target;

    explicit JointObservationWindow(bool highlight = false, bool missingBinding = false)
        : AnimationObservationWindow(false) {
        const QString path = QString::fromUtf8(MINI3D_J1_RENDER_MANIFEST);
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        REQUIRE(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toUpper() ==
                QByteArray("BE7C46AEEB710E37D1B702FE71ECC17DD82201C2FE6A8DF39F0BE1F53E3B4848"));
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        REQUIRE(parse.error == QJsonParseError::NoError);
        REQUIRE(document.isObject());
        manifest = document.object();
        const auto cubeData = manifest["cube"].toObject();
        const QDir root(QFileInfo(path).dir().absoluteFilePath(QStringLiteral("../..")));
        QFile primitive(root.absoluteFilePath(cubeData["meshSource"].toString()));
        REQUIRE(primitive.open(QIODevice::ReadOnly));
        REQUIRE(QCryptographicHash::hash(primitive.readAll(), QCryptographicHash::Sha256)
                    .toHex().toUpper() == cubeData["meshSourceSha256"].toString().toLatin1());
        const auto size = manifest["target"].toObject();
        target = {size["width"].toInt(), size["height"].toInt()};
        const auto middle = manifest["timeSamples"].toArray()[1].toObject();
        const auto materialData = manifest["material"].toObject();
        material = core::MaterialId{static_cast<std::uint64_t>(materialData["id"].toInt())};
        core::MaterialDefinition definition;
        definition.id = material;
        definition.parameters.baseColor = manifestVector(materialData["baseColor"]);
        definition.parameters.metallic = materialData["metallic"].toDouble();
        definition.parameters.coverage = materialData["coverage"].toDouble();
        definition.parameters.roughness = middle["roughness"].toDouble();
        core::SceneMaterialData materials;
        materials.assets.emplace(core::StableAssetId{material.value}, core::SceneAssetDefinition{
            core::StableAssetId{material.value}, core::SceneAssetKind::Material, definition});
        auto scene = std::const_pointer_cast<core::Scene>(model->scene());
        std::string error;
        auto prepared = scene->prepareMaterialData(materials, error);
        REQUIRE(prepared);
        REQUIRE(scene->installPreparedMaterialData(*prepared));
        core::SceneNode geometry, sunNode, cameraNode;
        cube = geometry.id = static_cast<core::EntityId>(cubeData["entity"].toInt());
        geometry.name = "Manifest geometry";
        geometry.primitive = highlight ? core::PrimitiveKind::Empty : core::PrimitiveKind::Cube;
        geometry.transform.position.x = static_cast<float>(middle["x"].toDouble());
        if (!missingBinding)
            geometry.materialSlots = {{core::MaterialSlotId{
                static_cast<std::uint64_t>(materialData["slotId"].toInt())}, material}};
        const auto sunData = manifest["sun"].toObject();
        sun = sunNode.id = static_cast<core::EntityId>(sunData["entity"].toInt());
        sunNode.name = "Manifest Sun";
        sunNode.sun = core::SunComponent{middle["irradiance"].toDouble(),
                                         manifestVector(sunData["color"])};
        const auto cameraData = manifest["camera"].toObject();
        camera = cameraNode.id = static_cast<core::EntityId>(cameraData["entity"].toInt());
        cameraNode.name = "Manifest camera";
        cameraNode.transform.position = glm::vec3(manifestVector(cameraData["position"]));
        cameraNode.camera = core::CameraComponent{
            static_cast<float>(cameraData["fovDegrees"].toDouble()),
            static_cast<float>(cameraData["near"].toDouble()),
            static_cast<float>(cameraData["far"].toDouble())};
        REQUIRE(scene->replaceNodes({geometry, sunNode, cameraNode}));
        if (highlight) {
            const auto quad = manifest["highlightQuad"].toObject();
            core::modeling::EditableMesh mesh;
            core::modeling::EditableFace face;
            face.id = 1;
            for (const auto& point : quad["positions"].toArray()) {
                const auto vertex = mesh.vertices.size() + 1;
                mesh.vertices.push_back({vertex, glm::vec3(manifestVector(point))});
                face.corners.push_back({vertex, vertex, {0, 0},
                                       glm::vec3(manifestVector(quad["normal"]))});
            }
            mesh.faces.push_back(std::move(face));
            const auto preparedMesh = scene->prepareEditableGeometry(cube, mesh, error);
            REQUIRE(preparedMesh);
            REQUIRE(scene->installGeometry(*preparedMesh));
            core::Transform transform;
            transform.position = glm::vec3(manifestVector(quad["position"]));
            const auto rotation = core::quaternionFromEulerXYZDegrees(
                manifestVector(quad["rotationEulerXYZDegrees"]));
            REQUIRE(rotation);
            transform.rotation = glm::quat(*rotation);
            REQUIRE(scene->setTransform(cube, transform));
        }
        const auto samples = manifest["timeSamples"].toArray();
        const auto first = samples.first().toObject(), last = samples.last().toObject();
        const auto fps = manifest["fps"].toArray();
        animation.settings = {static_cast<std::uint32_t>(fps[0].toInt()),
                              static_cast<std::uint32_t>(first["frame"].toInt()),
                              static_cast<std::uint32_t>(last["frame"].toInt())};
        animation.properties.rationalFps = core::RationalFps{
            static_cast<std::uint32_t>(fps[0].toInt()), static_cast<std::uint32_t>(fps[1].toInt())};
        core::ScalarCurve x{core::CurveId{1}, core::PropertyId::Position,
            core::PropertyComponent::X, {{core::KeyId{1}, first["frame"].toDouble(), first["x"].toDouble()},
                {core::KeyId{2}, last["frame"].toDouble(), last["x"].toDouble()}}};
        core::ScalarCurve roughness{core::CurveId{2}, core::PropertyId::PbrRoughness,
            core::PropertyComponent::Scalar,
            {{core::KeyId{3}, first["frame"].toDouble(), first["roughness"].toDouble()},
             {core::KeyId{4}, last["frame"].toDouble(), last["roughness"].toDouble()}}};
        core::ScalarCurve irradiance{core::CurveId{3}, core::PropertyId::SunIrradiance,
            core::PropertyComponent::Scalar,
            {{core::KeyId{5}, first["frame"].toDouble(), first["irradiance"].toDouble()},
             {core::KeyId{6}, last["frame"].toDouble(), last["irradiance"].toDouble()}}};
        animation.properties.actions = {{core::ActionId{1}, {
            {core::ActionSlotId{1}, core::PropertyOwnerKind::Entity, {x}},
            {core::ActionSlotId{2}, core::PropertyOwnerKind::Material, {roughness}},
            {core::ActionSlotId{3}, core::PropertyOwnerKind::Entity, {irradiance}}}}};
        animation.properties.bindings = {
            {scene->documentUuid(), core::EntityOwner{cube}, core::ActionId{1}, core::ActionSlotId{1}},
            {scene->documentUuid(), core::MaterialOwner{material}, core::ActionId{1}, core::ActionSlotId{2}},
            {scene->documentUuid(), core::EntityOwner{sun}, core::ActionId{1}, core::ActionSlotId{3}}};
        REQUIRE(model->replaceAnimation(animation));
        REQUIRE(model->setPreviewCamera(camera));
        viewport->setOverlayVisible(false);
        viewport->setTransformTool(renderer_gl::GizmoTool::None);
        const auto extra = target - viewport->size();
        viewport->setFixedSize(target);
        window.resize(window.size() + extra);
        REQUIRE(QTest::qWaitFor([this] { return viewport->size() == target; }));
        REQUIRE(viewport->devicePixelRatioF() == size["dpr"].toDouble());
        REQUIRE(viewport->context());
        REQUIRE(std::max(0, viewport->context()->format().samples()) == size["samples"].toInt());
    }

    std::shared_ptr<const renderer_gl::InstalledPose> display() {
        QString error;
        std::shared_ptr<const renderer_gl::InstalledPose> result;
        const bool prepared = model->prepareDisplayPose(result, error);
        INFO(error.toStdString());
        REQUIRE(prepared);
        REQUIRE(result);
        REQUIRE(renderer_gl::ScenePoseView(*model->scene(), result.get()).isComplete());
        return result;
    }

    renderer_gl::StampedFramebuffer capture() {
        const auto pose = display();
        auto wanted = request();
        wanted.longestEdge = std::max(target.width(), target.height());
        CaptureAttempt attempt(*this, wanted);
        attempt.wait();
        INFO((attempt.result->error ? attempt.result->error->message.toStdString() : ""));
        REQUIRE(attempt.result->hasValue());
        const auto& result = *attempt.result->value;
        REQUIRE(result.originalPixelSize == target);
        REQUIRE(result.outputPixelSize == target);
        REQUIRE(attempt.completionFrame);
        const auto& frame = *attempt.completionFrame;
        REQUIRE(result.frameId == frame.frameId);
        REQUIRE(result.view.viewport.animation == frame.state.animation);
        REQUIRE(frame.state.animation == pose->identity);
        REQUIRE(frame.state.document.documentRevision == pose->identity.sourceRevision);
        REQUIRE(frame.state.devicePixelRatio == 1);
        REQUIRE(frame.state.pixelSize == target);
        REQUIRE(frame.state.view.previewCamera == camera);
        REQUIRE_FALSE(result.overlayIncluded);
        REQUIRE(frame.resources.ready);
        REQUIRE(frame.resources.joint);
        const auto& receipt = *frame.resources.joint;
        REQUIRE(receipt.source == pose->sceneState->source);
        REQUIRE(receipt.width == target.width());
        REQUIRE(receipt.height == target.height());
        REQUIRE(receipt.source.evaluationId == wanted.expectedEvaluationId);
        REQUIRE(receipt.source.frameTime == model->animationFrame());
        REQUIRE(receipt.source.evaluationProfile == core::EvaluationProfile::JointPreviewJ1);
        REQUIRE(receipt.camera.position == glm::dvec3(frame.state.view.position));
        REQUIRE(receipt.camera.viewMatrix == glm::dmat4(frame.state.view.viewMatrix));
        REQUIRE(receipt.camera.projectionMatrix == glm::dmat4(frame.state.view.projectionMatrix));
        REQUIRE(receipt.geometryStages.size() == pose->sceneState->content->geometryStages.size());
        for (const auto& stage : receipt.geometryStages)
            REQUIRE(stage == pose->sceneState->content->geometryStages.at(stage.entity).identity);
        const auto color = manifest["color"].toObject();
        REQUIRE(receipt.backendBuild == manifest["backend"].toString());
        REQUIRE(receipt.color.configSha256 == color["configSha256"].toString().toLatin1());
        REQUIRE(receipt.color.libraryVersion == color["ocioVersion"].toString());
        REQUIRE(receipt.color.display == color["display"].toString());
        REQUIRE(receipt.color.view == color["view"].toString());
        REQUIRE(receipt.color.look == color["look"].toString());
        REQUIRE(receipt.color.exposure == color["exposure"].toDouble());
        REQUIRE(receipt.color.gamma == color["gamma"].toDouble());
        REQUIRE_FALSE(receipt.color.processorCacheId.isEmpty());
        REQUIRE(receipt.color.shaderSha256.size() == 64);
        const auto image = QImage::fromData(result.png, "PNG");
        REQUIRE_FALSE(image.isNull());
        REQUIRE(image.size() == target);
        REQUIRE(display() == pose);
        return {image, frame};
    }

    void requireChanged(const QImage& first, const QImage& last, const QString& property) const {
        const auto roi = manifest["isolatedChanges"].toObject()[property].toObject()["roi"].toArray();
        const auto tolerances = manifest["tolerances"].toObject();
        std::size_t changed = 0;
        for (int y = roi[1].toInt(); y < roi[3].toInt(); ++y)
            for (int x = roi[0].toInt(); x < roi[2].toInt(); ++x) {
                const auto left = first.pixelColor(x, y), right = last.pixelColor(x, y);
                const auto threshold = tolerances["changedPixelRgbThreshold"].toDouble();
                if (std::abs(left.redF() - right.redF()) > threshold ||
                    std::abs(left.greenF() - right.greenF()) > threshold ||
                    std::abs(left.blueF() - right.blueF()) > threshold)
                    ++changed;
            }
        CAPTURE(property, changed);
        REQUIRE(changed >= static_cast<std::size_t>(tolerances["minimumChangedPixels"].toInt()));
    }
};

void requireJointRenderFailure(JointObservationWindow& fixture) {
    fixture.preview(1);
    QSignalSpy failures(fixture.model, &editor::SceneViewModel::operationFailed);
    CaptureAttempt attempt(fixture, fixture.request());
    attempt.wait();
    attempt.requireError(Error::RenderFailed);
    REQUIRE(attempt.completionFrame);
    REQUIRE_FALSE(attempt.completionFrame->resources.ready);
    REQUIRE_FALSE(attempt.completionFrame->resources.error.isEmpty());
    REQUIRE(failures.count() > 0);
}

void requireIsolatedJointPixels(const QString& property) {
    JointObservationWindow fixture(property == QStringLiteral("roughness"));
    auto animation = fixture.animation;
    const auto change = fixture.manifest["isolatedChanges"].toObject()[property].toObject();
    for (auto& slot : animation.properties.actions.front().actionSlots)
        for (auto& curve : slot.curves) {
            const bool tested = (property == QStringLiteral("position") &&
                                 curve.property == core::PropertyId::Position) ||
                                (property == QStringLiteral("irradiance") &&
                                 curve.property == core::PropertyId::SunIrradiance) ||
                                (property == QStringLiteral("roughness") &&
                                 curve.property == core::PropertyId::PbrRoughness);
            if (tested)
                continue;
            const auto field = curve.property == core::PropertyId::Position ? "x" :
                               curve.property == core::PropertyId::PbrRoughness ? "roughness" : "irradiance";
            const double value = change.contains(field) ? change[field].toDouble() : 1;
            for (auto& key : curve.keys)
                key.value = value;
        }
    REQUIRE(fixture.model->replaceAnimation(animation));
    const auto document = fixture.model->apiDocumentState();
    fixture.preview(1);
    const auto first = fixture.capture();
    REQUIRE(fixture.model->setAnimationFrame(24));
    const auto last = fixture.capture();
    fixture.requireChanged(first.image, last.image, property);
    REQUIRE(fixture.model->apiDocumentState().document == document.document);
    REQUIRE(fixture.model->apiDocumentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.model->apiDocumentState().historyRevision == document.historyRevision);
}
} // namespace

TEST_CASE("Capture wire requires a canonical evaluation ID and permits real zero",
          "[animation-observation][observation-canonical]") {
    editor::SceneViewModel model;
    const auto state = model.apiDocumentState();
    QJsonObject params{
        {"document", QJsonObject{{"instanceId", state.document.instanceId},
                                  {"documentId", state.document.documentId}}},
        {"expectedDocumentRevision", QString::number(state.documentRevision)},
        {"expectedViewportRevision", "1"}, {"expectedEvaluationId", "0"}};
    const auto decoded = observation::ObservationJsonCodec::decodeRequest("viewport.capture", params);
    REQUIRE(decoded.hasValue());
    REQUIRE(std::get<observation::CaptureRequest>(*decoded.value).expectedEvaluationId == 0);
    const auto canonical = observation::ObservationJsonCodec::canonicalParams("viewport.capture", params);
    REQUIRE(canonical.hasValue());
    REQUIRE((*canonical.value)["expectedEvaluationId"] == "0");
    auto missing = params;
    missing.remove("expectedEvaluationId");
    const auto missingResult = observation::ObservationJsonCodec::decodeRequest("viewport.capture", missing);
    REQUIRE(missingResult.error);
    REQUIRE(missingResult.error->fieldPath == "expectedEvaluationId");
    for (const auto value : {QJsonValue(0), QJsonValue("00"), QJsonValue("-1"),
                            QJsonValue("18446744073709551616"), QJsonValue(true)}) {
        auto invalid = params;
        invalid["expectedEvaluationId"] = value;
        const auto result = observation::ObservationJsonCodec::decodeRequest("viewport.capture", invalid);
        REQUIRE(result.error);
        REQUIRE(result.error->fieldPath == "expectedEvaluationId");
    }
}

TEST_CASE("Initialized viewport never cleans derived resources during its base destructor",
          "[animation-observation][viewport-lifetime]") {
    auto viewport = std::make_unique<Viewport>();
    viewport->resize(640, 480);
    viewport->show();
    REQUIRE(QTest::qWaitForWindowExposed(viewport.get()));
    REQUIRE(QTest::qWaitFor([&] {
        return viewport->context() && viewport->context()->isValid();
    }));
    QObject observer;
    QOpenGLWidget* base = viewport.get();
    int invalidations = 0;
    bool lateCleanup = false;
    QObject::connect(viewport.get(), &Viewport::observationUnavailable, &observer, [&] {
        ++invalidations;
        // 信号发出时基类仍存活；动态类型已退化表示派生成员生命周期已结束。
        lateCleanup |= !base->metaObject()->inherits(&Viewport::staticMetaObject);
    });
    viewport.reset();
    REQUIRE(invalidations > 0);
    REQUIRE_FALSE(lateCleanup);
}

TEST_CASE("Window destroys its animation input filter before the referenced model",
          "[animation-observation][animation-window-lifetime]") {
    QObject observer;
    auto window = std::make_unique<editor::MainWindow>();
    window->show();
    REQUIRE(QTest::qWaitForWindowExposed(window.get()));
    auto* model = window->findChild<editor::SceneViewModel*>();
    QPointer<editor::AnimationDraftGesture> gesture =
        window->findChild<editor::AnimationDraftGesture*>();
    REQUIRE(model);
    REQUIRE(gesture);
    bool modelDestroyed = false;
    bool filterStillAlive = false;
    QObject::connect(model, &QObject::destroyed, &observer, [&] {
        modelDestroyed = true;
        filterStillAlive = !gesture.isNull();
        // 旧实现只记录错误顺序，不故意让后续控件析构事件访问已死模型。
        if (gesture)
            qApp->removeEventFilter(gesture.data());
    });
    window.reset();
    REQUIRE(modelDestroyed);
    REQUIRE_FALSE(filterStillAlive);
}

TEST_CASE("Base capture reports the actual controller identity including initial zero",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    REQUIRE(fixture.model->animationEvaluationId() == 0);
    auto request = fixture.request();
    REQUIRE(request.expectedEvaluationId == 0);
    {
        CaptureAttempt capture(fixture, request);
        capture.wait();
        REQUIRE(capture.result->hasValue());
        const auto& view = capture.result->value->view.viewport;
        REQUIRE(view.animation);
        REQUIRE(view.animation->evaluationId == fixture.model->animationEvaluationId());
        REQUIRE(view.animation->frame == fixture.model->animationFrame());
        REQUIRE(view.animation->mode == renderer_gl::AnimationMode::Base);
        REQUIRE(view.sessionRevision == fixture.model->animationSessionRevision());
        const auto json = observation::ObservationJsonCodec::encode(capture.result->value->view);
        REQUIRE(json["evaluationId"] == "0");
        REQUIRE(json["mode"] == "base");
        REQUIRE(json["frame"].toDouble() == fixture.model->animationFrame());
        REQUIRE(json["sessionRevision"] == QString::number(fixture.model->animationSessionRevision()));
    }
    fixture.preview(25);
    REQUIRE(fixture.model->setAnimationPreview(false));
    request = fixture.request();
    REQUIRE(request.expectedEvaluationId != 0);
    REQUIRE_FALSE(fixture.model->installedAnimationPose());
    {
        auto stale = request;
        stale.expectedEvaluationId = 0;
        CaptureAttempt capture(fixture, stale);
        REQUIRE(capture.id.isEmpty());
        capture.requireError(Error::StaleEvaluation);
    }
    CaptureAttempt current(fixture, request);
    current.wait();
    REQUIRE(current.result->hasValue());
    REQUIRE(current.result->value->view.viewport.animation->evaluationId == request.expectedEvaluationId);
}

TEST_CASE("Unassembled renderer still paints but cannot claim controlled observation identity",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    const auto request = fixture.request();
    fixture.viewport->setFrameDisplayStateProvider({});
    const auto before = fixture.viewport->lastRenderedFrame()
                            ? fixture.viewport->lastRenderedFrame()->frameId : 0;
    fixture.viewport->update();
    REQUIRE(QTest::qWaitFor([&] {
        return fixture.viewport->lastRenderedFrame() &&
               fixture.viewport->lastRenderedFrame()->frameId > before;
    }));
    REQUIRE(fixture.viewport->lastRenderedFrame()->resources.ready);
    REQUIRE_FALSE(fixture.viewport->lastRenderedFrame()->state.animation);
    REQUIRE_FALSE(fixture.viewport->observationState());
    REQUIRE_FALSE(fixture.viewport->grabStampedFramebuffer());
    CaptureAttempt capture(fixture, request);
    REQUIRE(capture.id.isEmpty());
    capture.requireError(Error::ViewportUnavailable);
}

TEST_CASE("Viewport rejects each pose identity field that differs from the frozen controller",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    const auto display = fixture.displayState();
    for (int field = 0; field < 9; ++field) {
        CAPTURE(field);
        auto invalid = display;
        auto pose = std::make_shared<renderer_gl::InstalledPose>(*display.pose);
        switch (field) {
            case 0: pose->identity.instanceId = "other-instance"; break;
            case 1: pose->identity.documentId = "other-document"; break;
            case 2: ++pose->identity.sourceRevision; break;
            case 3: ++pose->identity.evaluationId; break;
            case 4: pose->identity.frame += .5; break;
            case 5: pose->identity.mode = renderer_gl::AnimationMode::Playing; break;
            case 6: pose->numerics.reset(); break;
            case 7: pose->geometry.reset(); break;
            case 8: invalid.identity.frame = std::numeric_limits<double>::quiet_NaN(); break;
        }
        invalid.pose = std::move(pose);
        fixture.viewport->setFrameDisplayStateProvider([invalid] { return invalid; });
        REQUIRE_FALSE(fixture.viewport->observationState());
        REQUIRE_FALSE(fixture.viewport->grabStampedFramebuffer());
    }
    fixture.viewport->setFrameDisplayStateProvider([display] { return display; });
    REQUIRE(fixture.viewport->observationState());
}

TEST_CASE("Paused capture uses a new real paint and the actual stamp after a second grab paint",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    std::set<QString> hashes;
    for (const core::FrameTime target : {13.0, 13.5, 25.0, 37.0, 49.0}) {
        CAPTURE(target);
        REQUIRE(fixture.model->setAnimationFrame(target));
        const auto request = fixture.request();
        const auto document = fixture.model->apiDocumentState();
        const auto session = fixture.model->animationSessionRevision();
        const auto before = fixture.viewport->lastRenderedFrame()
                                ? fixture.viewport->lastRenderedFrame()->frameId : 0;
        QSignalSpy painted(fixture.viewport, &Viewport::framePainted);
        std::uint64_t firstPaint = 0;
        const auto connection = QObject::connect(fixture.viewport, &Viewport::framePainted,
                                                  fixture.viewport, [&] {
            if (firstPaint == 0)
                firstPaint = fixture.viewport->lastRenderedFrame()->frameId;
        });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
        CaptureAttempt capture(fixture, request);
        capture.wait();
        REQUIRE(capture.result->hasValue());
        const auto& result = *capture.result->value;
        REQUIRE(painted.count() >= 2);
        REQUIRE(result.frameId > before);
        REQUIRE(result.frameId > firstPaint);
        REQUIRE(capture.completionFrame);
        REQUIRE(result.frameId == capture.completionFrame->frameId);
        REQUIRE(result.view.viewport.document == capture.completionFrame->state.document);
        REQUIRE(result.view.viewport.viewportRevision ==
                capture.completionFrame->state.viewportRevision);
        REQUIRE(result.view.viewport.animation == capture.completionFrame->state.animation);
        REQUIRE(result.view.viewport.sessionRevision == capture.completionFrame->state.sessionRevision);
        REQUIRE(result.contextGeneration == capture.completionFrame->contextGeneration);
        REQUIRE(result.contextGeneration == fixture.viewport->contextGeneration());
        REQUIRE(result.view.viewport.document.instanceId == document.document.instanceId);
        REQUIRE(result.view.viewport.document.documentId == document.document.documentId);
        REQUIRE(result.view.viewport.document.documentRevision == request.expectedDocumentRevision);
        REQUIRE(result.view.viewport.viewportRevision == request.expectedViewportRevision);
        REQUIRE(result.view.viewport.animation == fixture.model->installedAnimationPose()->identity);
        REQUIRE(result.view.viewport.animation->evaluationId == request.expectedEvaluationId);
        REQUIRE(result.view.viewport.animation->frame == target);
        REQUIRE(result.view.viewport.animation->mode == renderer_gl::AnimationMode::PreviewPaused);
        REQUIRE(result.view.viewport.sessionRevision == session);
        const auto image = QImage::fromData(result.png, "PNG");
        REQUIRE_FALSE(image.isNull());
        REQUIRE(image.size() == result.outputPixelSize);
        REQUIRE(result.sha256 == QString::fromLatin1(
            QCryptographicHash::hash(result.png, QCryptographicHash::Sha256).toHex()));
        REQUIRE(hashes.insert(result.sha256).second);
        const auto evaluation = fixture.model->animationEvaluationId();
        REQUIRE(fixture.model->setAnimationFrame(target));
        REQUIRE(fixture.model->animationEvaluationId() == evaluation);
    }
}

TEST_CASE("Base loop changes actively end pending capture without waiting for paint",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.viewport->setUpdatesEnabled(false);
    const auto request = fixture.request();
    const auto session = fixture.model->animationSessionRevision();
    CaptureAttempt capture(fixture, request);
    REQUIRE_FALSE(capture.id.isEmpty());
    REQUIRE(fixture.model->setAnimationLoop(true));
    REQUIRE(fixture.model->animationEvaluationId() == request.expectedEvaluationId);
    REQUIRE(fixture.model->animationSessionRevision() == session + 1);
    capture.requireError(Error::StaleEvaluation);
    REQUIRE_FALSE(fixture.window.observationService().isCapturing());
    REQUIRE_FALSE(fixture.window.observationService().cancelCapture(capture.id));
    fixture.viewport->setUpdatesEnabled(true);
}

TEST_CASE("Pending paused capture rejects seek loop preview and formal source changes immediately",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    fixture.viewport->setUpdatesEnabled(false);
    CaptureAttempt capture(fixture, fixture.request());
    REQUIRE_FALSE(capture.id.isEmpty());
    SECTION("Seek changes evaluation") {
        REQUIRE(fixture.model->setAnimationFrame(25));
        capture.requireError(Error::StaleEvaluation);
    }
    SECTION("Loop changes session only") {
        REQUIRE(fixture.model->setAnimationLoop(true));
        capture.requireError(Error::StaleEvaluation);
    }
    SECTION("Disable changes mode") {
        REQUIRE(fixture.model->setAnimationPreview(false));
        capture.requireError(Error::StaleEvaluation);
    }
    SECTION("Formal source edit changes document") {
        auto changed = fixture.model->scene()->animation();
        changed.tracks.at({fixture.cube, core::AnimationChannel::Position}).keys.back().value.x = 5;
        REQUIRE(fixture.model->replaceAnimation(changed));
        capture.requireError(Error::RevisionConflict);
    }
    REQUIRE_FALSE(fixture.window.observationService().isCapturing());
    fixture.viewport->setUpdatesEnabled(true);
}

TEST_CASE("Another Base document at the same revision actively invalidates capture",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.model->newScene();
    fixture.viewport->setUpdatesEnabled(false);
    const auto request = fixture.request();
    CaptureAttempt capture(fixture, request);
    REQUIRE_FALSE(capture.id.isEmpty());
    fixture.model->newScene();
    REQUIRE(fixture.model->apiDocumentState().documentRevision == request.expectedDocumentRevision);
    REQUIRE(fixture.model->apiDocumentState().document != request.document);
    capture.requireError(Error::StaleDocument);
    fixture.viewport->setUpdatesEnabled(true);
}

TEST_CASE("Playing Draft and physical Busy reject capture admission",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    SECTION("Playing") { REQUIRE(fixture.model->playAnimation()); }
    SECTION("Draft") {
        REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, false, false}));
    }
    auto request = fixture.request();
    SECTION("Physical Busy") {
        fixture.model->setExternalBusy(QStringLiteral("observation_test_interaction"), true);
    }
    CaptureAttempt capture(fixture, request);
    REQUIRE(capture.id.isEmpty());
    capture.requireError(Error::Busy);
    fixture.model->setExternalBusy(QStringLiteral("observation_test_interaction"), false);
}

TEST_CASE("A request replaced during the grab repaint cannot receive the previous result",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    const auto request = fixture.request();
    auto& service = fixture.window.observationService();
    std::optional<api::ApiResult<observation::CaptureResult>> first, second;
    int firstCalls = 0, secondCalls = 0, paints = 0;
    QString firstId, secondId;
    std::uint64_t replacementAfterFrame = 0;
    firstId = service.capture(request, [&](auto result) {
        ++firstCalls;
        first = std::move(result);
        replacementAfterFrame = fixture.viewport->lastRenderedFrame()->frameId;
        secondId = service.capture(fixture.request(), [&](auto replacement) {
            ++secondCalls;
            second = std::move(replacement);
        });
    });
    const auto connection = QObject::connect(fixture.viewport, &Viewport::framePainted,
                                              fixture.viewport, [&] {
        if (++paints == 2)
            REQUIRE(service.cancelCapture(firstId));
    });
    REQUIRE(QTest::qWaitFor([&] { return second.has_value(); }, 6000));
    QObject::disconnect(connection);
    service.cancelCapture(firstId);
    service.cancelCapture(secondId);
    REQUIRE(first);
    REQUIRE(first->error);
    REQUIRE(first->error->code == Error::Cancelled);
    REQUIRE(firstCalls == 1);
    REQUIRE(secondCalls == 1);
    REQUIRE(second->hasValue());
    REQUIRE(second->value->captureId == secondId);
    REQUIRE(second->value->frameId > replacementAfterFrame);
    REQUIRE(second->value->view.viewport.animation->evaluationId == request.expectedEvaluationId);
    REQUIRE_FALSE(service.isCapturing());
}

TEST_CASE("MainWindow Joint displays the three frozen manifest times with actual consumption stamps",
          "[animation-observation][j1-consumer]") {
    JointObservationWindow fixture;
    const auto document = fixture.model->apiDocumentState();
    fixture.preview(1);
    std::vector<renderer_gl::StampedFramebuffer> frames;
    for (const auto& value : fixture.manifest["timeSamples"].toArray()) {
        const auto sample = value.toObject();
        REQUIRE(fixture.model->setAnimationFrame(sample["frame"].toDouble()));
        const auto pose = fixture.display();
        REQUIRE(pose->numerics->find(fixture.cube)->local.position.x == 1);
        REQUIRE(std::abs(pose->sceneState->find(fixture.cube)->localPosition.x -
                         sample["x"].toDouble()) < 1e-12);
        REQUIRE(std::abs(pose->sceneState->content->materials.at(fixture.material).effective.roughness -
                         sample["roughness"].toDouble()) < 1e-12);
        REQUIRE(pose->sceneState->content->visibleSuns.size() == 1);
        REQUIRE(std::abs(pose->sceneState->content->visibleSuns.front().effective.irradiance -
                         sample["irradiance"].toDouble()) < 1e-12);
        frames.push_back(fixture.capture());
        REQUIRE(frames.back().frame.state.animation->frame == sample["frame"].toDouble());
    }
    REQUIRE(frames.size() == 3);
    fixture.requireChanged(frames[0].image, frames[1].image, QStringLiteral("position"));
    fixture.requireChanged(frames[1].image, frames[2].image, QStringLiteral("position"));
    REQUIRE(fixture.model->apiDocumentState().document == document.document);
    REQUIRE(fixture.model->apiDocumentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.model->apiDocumentState().historyRevision == document.historyRevision);
    REQUIRE(fixture.model->scene()->find(fixture.cube)->transform.position.x == 1);
}

TEST_CASE("MainWindow frozen manifest isolates typed position Sun and highlight roughness pixels",
          "[animation-observation][j1-consumer]") {
    SECTION("Position") { requireIsolatedJointPixels(QStringLiteral("position")); }
    SECTION("Sun irradiance") { requireIsolatedJointPixels(QStringLiteral("irradiance")); }
    SECTION("Highlight roughness") { requireIsolatedJointPixels(QStringLiteral("roughness")); }
}

TEST_CASE("MainWindow Joint Base displays author values without sampling keys or publishing per paint",
          "[animation-observation][j1-consumer]") {
    JointObservationWindow fixture;
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::Base);
    REQUIRE_FALSE(fixture.model->installedAnimationPose());
    const auto base = fixture.display();
    const auto document = fixture.model->apiDocumentState();
    const auto middle = fixture.manifest["timeSamples"].toArray()[1].toObject();
    REQUIRE(base->sceneState->find(fixture.cube)->localPosition.x == middle["x"].toDouble());
    REQUIRE(base->sceneState->content->materials.at(fixture.material).effective.roughness ==
            middle["roughness"].toDouble());
    REQUIRE(base->sceneState->content->visibleSuns.front().effective.irradiance ==
            middle["irradiance"].toDouble());
    const auto first = fixture.capture(), repeated = fixture.capture();
    REQUIRE(first.image == repeated.image);
    REQUIRE(repeated.frame.frameId > first.frame.frameId);
    REQUIRE(fixture.display() == base);
    REQUIRE(fixture.model->animationEvaluationId() == base->identity.evaluationId);
    REQUIRE_FALSE(fixture.model->installedAnimationPose());
    fixture.preview(1);
    const auto animated = fixture.capture();
    fixture.requireChanged(first.image, animated.image, QStringLiteral("position"));
    REQUIRE_FALSE(core::isSameWorkSource(base->sceneState->source,
                                        fixture.model->installedAnimationPose()->sceneState->source));
    REQUIRE(fixture.model->setAnimationPreview(false));
    const auto restored = fixture.capture();
    REQUIRE(restored.image == first.image);
    REQUIRE(restored.frame.state.animation->mode == renderer_gl::AnimationMode::Base);
    REQUIRE_FALSE(fixture.model->installedAnimationPose());
    REQUIRE(fixture.model->apiDocumentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.model->apiDocumentState().historyRevision == document.historyRevision);
}

TEST_CASE("MainWindow Joint render failures cannot complete a successful capture",
          "[animation-observation][j1-consumer]") {
    SECTION("Missing material slot") {
        JointObservationWindow fixture(false, true);
        requireJointRenderFailure(fixture);
    }
    SECTION("Non Material display") {
        JointObservationWindow fixture;
        fixture.viewport->setShadingMode(renderer_gl::ViewportShading::Solid);
        requireJointRenderFailure(fixture);
    }
    SECTION("Temporary editable preview") {
        JointObservationWindow fixture;
        fixture.viewport->setEditablePreview(fixture.cube,
                                             std::make_shared<const core::EditableMeshContent>());
        requireJointRenderFailure(fixture);
    }
}

TEST_CASE("MainWindow rejects missing checked state and stops playing after an actual Joint render failure",
          "[animation-observation][j1-consumer]") {
    JointObservationWindow fixture;
    fixture.preview(1);
    SECTION("Missing checked state") {
        const auto request = fixture.request();
        auto invalid = fixture.displayState();
        auto pose = std::make_shared<renderer_gl::InstalledPose>(*invalid.pose);
        pose->sceneNumerics.reset();
        invalid.pose = std::move(pose);
        fixture.viewport->setFrameDisplayStateProvider([invalid] { return invalid; });
        REQUIRE_FALSE(fixture.viewport->observationState());
        REQUIRE_FALSE(fixture.viewport->grabStampedFramebuffer());
        CaptureAttempt attempt(fixture, request);
        REQUIRE(attempt.id.isEmpty());
        attempt.requireError(Error::ViewportUnavailable);
    }
    SECTION("Actual render failure stops playback") {
        QSignalSpy failures(fixture.model, &editor::SceneViewModel::operationFailed);
        REQUIRE(fixture.model->playAnimation());
        fixture.viewport->setShadingMode(renderer_gl::ViewportShading::Solid);
        REQUIRE(QTest::qWaitFor([&] {
            return fixture.model->animationMode() == renderer_gl::AnimationMode::PreviewPaused &&
                   fixture.viewport->lastRenderedFrame() &&
                   !fixture.viewport->lastRenderedFrame()->resources.ready;
        }, 6000));
        REQUIRE(failures.count() > 0);
    }
}
