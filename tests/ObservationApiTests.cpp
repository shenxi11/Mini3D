/*
 * 模块名: ObservationApiTests
 * 功能概述: 以真实 OpenGL 窗口验证观察、明确框景及精确帧捕获闭环。
 * 对外接口: Catch2 [api-observation]；无独立 main 或监听器。
 * 依赖关系: ObservationService/Codec、ViewModel、ViewportWidget、Qt Test。
 * 输入输出: 显式版本与真实用户事件到相机、绘制戳、PNG 和失败原子性断言。
 * 异常与错误: 旧帧、渲染失败、Context 失效、超时和取消不能伪装成功。
 * 维护说明: 事件条件等待只用于测试；不写用户配置，不使用固定睡眠。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/api/EditorApiService.h"
#include "editor/observation/ObservationJsonCodec.h"
#include "editor/observation/ObservationService.h"
#include "renderer_gl/GpuMesh.h"
#include "renderer_gl/PrimitiveFactory.h"
#include "renderer_gl/RayCaster.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mini3d;
namespace {
namespace api = editor::api;
namespace observation = editor::observation;
using Error = api::ErrorCode;
using Viewport = renderer_gl::ViewportWidget;

struct CaptureAttempt {
    observation::ObservationService& service;
    std::optional<api::ApiResult<observation::CaptureResult>> result;
    std::optional<renderer_gl::RenderedFrame> completionFrame;
    int calls = 0;
    QString id;
    CaptureAttempt(observation::ObservationService& service, Viewport& viewport,
                   const observation::CaptureRequest& request)
        : service(service) {
        id = service.capture(request, [this, &viewport](auto completed) {
            ++calls;
            completionFrame = viewport.lastRenderedFrame();
            result = std::move(completed);
        });
    }
    ~CaptureAttempt() {
        service.cancelCapture(id);
    }
    void wait() {
        REQUIRE(QTest::qWaitFor(
            [this] {
                return result.has_value();
            },
            6000));
        REQUIRE(calls == 1);
        REQUIRE_FALSE(service.isCapturing());
    }
};
struct ObservationFixture {
    editor::SceneViewModel model;
    api::EditorApiService api{model};
    Viewport viewport;
    observation::ObservationService service{api, viewport};
    ObservationFixture() {
        model.newScene();
        viewport.setScene(model.scene());
        viewport.setAssets(model.assets());
        viewport.setEditorCamera(model.editorCamera());
        viewport.setFrameDocumentProvider([this] {
            const auto& state = api.documentState();
            return renderer_gl::FrameDocumentStamp{state.document.instanceId,
                                                   state.document.documentId,
                                                   state.documentRevision, state.historyRevision};
        });
        QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &viewport, [this] {
            viewport.setScene(model.scene());
        });
        QObject::connect(&model, &editor::SceneViewModel::documentReset, &viewport, [this] {
            viewport.setScene(model.scene());
            viewport.setAssets(model.assets());
            viewport.setEditorCamera(model.editorCamera());
        });
        QObject::connect(&viewport, &Viewport::cameraChanged, &model,
                         &editor::SceneViewModel::setEditorCamera);
        QObject::connect(model.selection(), &editor::SelectionModel::selectedEntityChanged,
                         &viewport, &Viewport::setSelectedEntity);
        QObject::connect(&model, &editor::SceneViewModel::previewCameraChanged, &viewport,
                         &Viewport::setPreviewCamera);
        QObject::connect(&model, &editor::SceneViewModel::editModeChanged, &viewport,
                         &Viewport::setEditMode);
        api.setBusyProvider([this] {
            return viewport.isNavigationActive() ? QStringList{QStringLiteral("navigation")}
                                                 : QStringList{};
        });
        viewport.resize(640, 480);
        viewport.show();
        REQUIRE(QTest::qWaitForWindowExposed(&viewport));
        REQUIRE(QTest::qWaitFor([this] {
            return viewport.lastRenderedFrame().has_value();
        }));
        REQUIRE(viewport.isObservationAvailable());
        REQUIRE(viewport.lastRenderedFrame()->resources.ready);
    }
    ~ObservationFixture() {
        viewport.hide();
    }
    observation::ViewState state() {
        auto result = service.getState({api.documentState().document});
        REQUIRE(result.hasValue());
        return *result.value;
    }
    template <typename Request> Request request() {
        const auto view = state();
        Request result;
        result.document = api.documentState().document;
        result.expectedDocumentRevision = api.documentState().documentRevision;
        result.expectedViewportRevision = view.viewport.viewportRevision;
        return result;
    }
    QJsonObject wireRequest() {
        const auto view = observation::ObservationJsonCodec::encode(state());
        return {{"document", view["document"]},
                {"expectedDocumentRevision", view["documentRevision"]},
                {"expectedViewportRevision", view["viewportRevision"]}};
    }
};
QJsonObject validCamera() {
    return {{"position", QJsonArray{4, 3, 9}}, {"target", QJsonArray{1, 1, 1}}};
}
void requireVector(const glm::vec3& actual, const glm::vec3& expected) {
    REQUIRE(glm::length(actual - expected) < 1.0e-5F);
}
template <typename T> void requireError(const api::ApiResult<T>& result, Error expected) {
    REQUIRE(result.error);
    REQUIRE(result.error->code == expected);
    REQUIRE_FALSE(result.hasValue());
}
} // namespace

TEST_CASE("Observation codec rejects malformed complete requests before changing the view",
          "[api-observation]") {
    ObservationFixture fixture;
    const auto before = fixture.state();
    const auto camera = fixture.model.editorCamera();
    const auto history = fixture.model.undoStack()->count();
    auto valid = fixture.wireRequest();
    valid.insert("camera", validCamera());
    valid.insert("orthographic", true);
    REQUIRE(observation::ObservationJsonCodec::decodeRequest("viewport.setView", valid).hasValue());
    for (int kind = 0; kind < 13; ++kind) {
        CAPTURE(kind);
        auto invalid = valid;
        switch (kind) {
            case 0:
                invalid.insert("extra", true);
                break;
            case 1:
                invalid.insert("expectedViewportRevision", 1);
                break;
            case 2:
                invalid.remove("expectedDocumentRevision");
                break;
            case 3:
                invalid.insert("preset", "top");
                break;
            case 4:
                invalid.insert("orthographic", "true");
                break;
            case 5:
                invalid.insert("shading", "unknown");
                break;
            case 6:
                invalid.insert("mutationSequence", "00");
                break;
            case 7:
                invalid.insert("clientSessionId", "not-a-uuid");
                break;
            case 8:
                invalid.insert("timeoutMs", 30001);
                break;
            case 9: {
                auto nested = validCamera();
                nested.insert("up", QJsonArray{0, 1, 0});
                invalid.insert("camera", nested);
                break;
            }
            case 10:
                invalid.insert("camera", QJsonObject{{"position", QJsonArray{0, 0, 0}},
                                                     {"target", QJsonArray{0, 0, 0}}});
                break;
            case 11:
                invalid.insert("camera", QJsonObject{{"position", QJsonArray{1e100, 0, 3}},
                                                     {"target", QJsonArray{0, 0, 0}}});
                break;
            case 12:
                invalid.insert("camera", QJsonObject{{"position", QJsonArray{true, 0, 3}},
                                                     {"target", QJsonArray{0, 0, 0}}});
                break;
        }
        std::optional<QJsonObject> response;
        fixture.service.invoke("viewport.setView", invalid, [&](auto value) {
            response = std::move(value);
        });
        REQUIRE(response);
        REQUIRE((*response)["error"].toObject()["code"].toInt() == -32602);
        REQUIRE(fixture.model.editorCamera() == camera);
        REQUIRE(fixture.state().viewport.viewportRevision == before.viewport.viewportRevision);
        REQUIRE(fixture.model.undoStack()->count() == history);
    }
    auto focus = fixture.wireRequest();
    for (const auto& ids : {QJsonArray{"1", "1"}, QJsonArray{"0"}, QJsonArray{1}, QJsonArray{}}) {
        focus.insert("entityIds", ids);
        REQUIRE_FALSE(
            observation::ObservationJsonCodec::decodeRequest("viewport.focus", focus).hasValue());
    }
    auto capture = fixture.wireRequest();
    capture.insert("longestEdge", int(api::limits::captureLongestEdge + 1));
    REQUIRE_FALSE(
        observation::ObservationJsonCodec::decodeRequest("viewport.capture", capture).hasValue());
    capture = fixture.wireRequest();
    capture.insert("timeoutMs", 10001);
    REQUIRE_FALSE(
        observation::ObservationJsonCodec::decodeRequest("viewport.capture", capture).hasValue());
    REQUIRE_FALSE(
        observation::ObservationJsonCodec::decodeRequest(
            "viewport.getState", QJsonObject{{"document", valid["document"]}, {"extra", true}})
            .hasValue());
}

TEST_CASE("Actual axis projection is session state and custom camera writes back exactly once",
          "[api-observation]") {
    ObservationFixture fixture;
    const auto selected = fixture.model.selection()->selectedEntity();
    const auto saved = fixture.model.editorCamera();
    const auto document = fixture.api.documentState();
    const auto history = fixture.model.undoStack()->count();
    QSignalSpy writeback(&fixture.viewport, &Viewport::cameraChanged);
    auto request = fixture.request<observation::SetViewRequest>();
    request.changes.preset = renderer_gl::EditorView::Top;
    request.changes.orthographic = true;
    request.changes.shading = renderer_gl::ViewportShading::Solid;
    request.changes.overlays = false;
    request.changes.xRay = true;
    const auto result = fixture.service.setView(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->status == api::ResultStatus::Committed);
    REQUIRE(fixture.model.editorCamera() == saved);
    REQUIRE(fixture.api.documentState().documentRevision == document.documentRevision);
    REQUIRE(writeback.count() == 0);
    requireVector(result.value->view.viewport.view.forward, {0, -1, 0});
    requireVector(result.value->view.viewport.view.up, {0, 0, -1});
    REQUIRE(result.value->view.viewport.view.orthographic);
    request = fixture.request<observation::SetViewRequest>();
    request.changes.preset = renderer_gl::EditorView::Top;
    const auto unchanged = fixture.service.setView(request);
    REQUIRE(unchanged.hasValue());
    REQUIRE(unchanged.value->status == api::ResultStatus::NoChange);
    request = fixture.request<observation::SetViewRequest>();
    request.changes.camera = core::CameraState{{4, 3, 9}, {1, 1, 1}, 0, 50};
    const auto changed = fixture.service.setView(request);
    REQUIRE(changed.hasValue());
    REQUIRE(changed.value->view.viewport.view.preset == renderer_gl::EditorView::Orbit);
    REQUIRE(changed.value->view.viewport.view.orthographic);
    requireVector(changed.value->view.viewport.view.position, {4, 3, 9});
    REQUIRE(writeback.count() == 1);
    REQUIRE(fixture.api.documentState().documentRevision == document.documentRevision + 1);
    REQUIRE(fixture.api.documentState().historyRevision == document.historyRevision);
    REQUIRE(fixture.model.undoStack()->count() == history);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
}

TEST_CASE("Typed observation candidates reject every invalid field with zero partial changes",
          "[api-observation]") {
    ObservationFixture fixture;
    const auto before = fixture.state();
    const auto saved = fixture.model.editorCamera();
    for (int kind = 0; kind < 8; ++kind) {
        CAPTURE(kind);
        auto request = fixture.request<observation::SetViewRequest>();
        request.changes.shading = renderer_gl::ViewportShading::Solid;
        switch (kind) {
            case 0:
                request.changes.preset = renderer_gl::EditorView(99);
                break;
            case 1:
                request.changes.shading = renderer_gl::ViewportShading(-1);
                break;
            case 2:
                request.timeoutMs = 0;
                break;
            case 3:
                request.mutationSequence = 0;
                break;
            case 4:
                request.clientSessionId = QStringLiteral("bad");
                break;
            case 5:
                request.changes.camera = core::CameraState{{0, 0, 0}, {0, 0, 0}, 0, 50};
                break;
            case 6:
                request.changes.camera = saved;
                request.changes.camera->focusRadius = std::numeric_limits<float>::max();
                break;
            case 7:
                request.changes.camera = saved;
                request.changes.preset = renderer_gl::EditorView::Front;
                break;
        }
        const auto result = fixture.service.setView(request);
        REQUIRE(result.error);
        REQUIRE(result.error->code == Error::InvalidArgument);
        REQUIRE(fixture.model.editorCamera() == saved);
        REQUIRE(fixture.state().viewport.viewportRevision == before.viewport.viewportRevision);
    }
    for (int kind = 0; kind < 4; ++kind) {
        auto request = fixture.request<observation::CaptureRequest>();
        if (kind == 0)
            request.longestEdge = 0;
        if (kind == 1)
            request.longestEdge = int(api::limits::captureLongestEdge + 1);
        if (kind == 2)
            request.timeoutMs = 0;
        if (kind == 3)
            request.timeoutMs = int(api::limits::captureTimeoutMaximumMs + 1);
        CaptureAttempt capture(fixture.service, fixture.viewport, request);
        REQUIRE(capture.id.isEmpty());
        REQUIRE(capture.result);
        requireError(*capture.result, Error::InvalidArgument);
        REQUIRE_FALSE(fixture.service.isCapturing());
    }
}

TEST_CASE("Explicit focus uses visible union and never changes selection or cancels a preview",
          "[api-observation]") {
    ObservationFixture fixture;
    const auto cube = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto sphere = fixture.model.createEntity(core::PrimitiveKind::Sphere);
    auto transform = fixture.model.scene()->find(cube)->transform;
    transform.position.x = -3;
    REQUIRE(fixture.model.setTransform(cube, transform));
    transform = fixture.model.scene()->find(sphere)->transform;
    transform.position.x = 5;
    REQUIRE(fixture.model.setTransform(sphere, transform));
    const auto selected = fixture.model.createEntity(core::PrimitiveKind::Empty);
    const auto history = fixture.model.undoStack()->count();
    const auto saved = fixture.model.editorCamera();
    auto request = fixture.request<observation::FocusRequest>();
    request.entityIds = {cube, 999999};
    requireError(fixture.service.focus(request), Error::NotFound);
    REQUIRE(fixture.model.editorCamera() == saved);
    request.entityIds = {cube, sphere};
    const auto focused = fixture.service.focus(request);
    REQUIRE(focused.hasValue());
    const auto a =
        renderer_gl::RayCaster::worldBounds(*fixture.model.scene(), *fixture.model.assets(), cube);
    const auto b = renderer_gl::RayCaster::worldBounds(*fixture.model.scene(),
                                                       *fixture.model.assets(), sphere);
    requireVector(focused.value->view.viewport.view.target,
                  (glm::min(a.minimum, b.minimum) + glm::max(a.maximum, b.maximum)) * .5F);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.undoStack()->count() == history);
    core::ViewportVisibility hidden;
    hidden.hiddenObjects = {cube, sphere};
    fixture.viewport.setViewportVisibility(hidden);
    request = fixture.request<observation::FocusRequest>();
    request.entityIds = {cube, sphere};
    requireError(fixture.service.focus(request), Error::UnsupportedOperation);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    fixture.model.beginTransformEdit(cube);
    REQUIRE(fixture.model.apiBusyReasons().contains(QStringLiteral("object_transform")));
    requireError(fixture.service.focus(request), Error::Busy);
    REQUIRE(fixture.model.apiBusyReasons().contains(QStringLiteral("object_transform")));
    fixture.model.cancelTransformEdit();
}

TEST_CASE(
    "Viewport revision follows visual inputs while ordinary paint and equal setters preserve it",
    "[api-observation]") {
    ObservationFixture fixture;
    const auto cube = fixture.model.createEntity(core::PrimitiveKind::Cube);
    auto state = fixture.state();
    const auto frame = fixture.viewport.lastRenderedFrame()->frameId;
    fixture.viewport.update();
    REQUIRE(QTest::qWaitFor([&] {
        return fixture.viewport.lastRenderedFrame()->frameId > frame;
    }));
    REQUIRE(fixture.state().viewport.viewportRevision == state.viewport.viewportRevision);
    fixture.viewport.setScene(fixture.model.scene());
    REQUIRE(fixture.state().viewport.viewportRevision == state.viewport.viewportRevision);
    const auto changed = [&](auto action) {
        const auto before = fixture.state().viewport.viewportRevision;
        action();
        REQUIRE(fixture.state().viewport.viewportRevision > before);
    };
    changed([&] {
        fixture.viewport.setSelectedEntity(0);
    });
    changed([&] {
        fixture.viewport.setOverlayVisible(false);
    });
    const auto noChange = fixture.state().viewport.viewportRevision;
    fixture.viewport.setOverlayVisible(false);
    REQUIRE(fixture.state().viewport.viewportRevision == noChange);
    changed([&] {
        fixture.viewport.setShadingMode(renderer_gl::ViewportShading::Wireframe);
    });
    changed([&] {
        fixture.viewport.setXRayEnabled(true);
    });
    changed([&] {
        fixture.viewport.setCursor3D({{1, 2, 3}, true});
    });
    changed([&] {
        fixture.viewport.setSnapTarget(glm::vec3(1, 0, 0));
    });
    changed([&] {
        fixture.viewport.setProportionalInfluence({{0, 0, 0}}, 2);
    });
    changed([&] {
        fixture.viewport.setTransformPivot(glm::vec3(1, 0, 0));
    });
    changed([&] {
        fixture.viewport.setTransformTool(renderer_gl::GizmoTool::Move);
    });
    changed([&] {
        fixture.viewport.setTransformSpace(renderer_gl::GizmoSpace::Local);
    });
    renderer_gl::ComponentOverlay overlay;
    overlay.points.push_back({{0, 0, 0}, {1, 0, 0, 1}});
    changed([&] {
        fixture.viewport.setComponentOverlay(overlay);
    });
    const auto sameOverlay = fixture.state().viewport.viewportRevision;
    fixture.viewport.setComponentOverlay(overlay);
    REQUIRE(fixture.state().viewport.viewportRevision == sameOverlay);
    core::ViewportVisibility visibility;
    visibility.hiddenObjects.insert(cube);
    changed([&] {
        fixture.viewport.setViewportVisibility(visibility);
    });
    changed([&] {
        fixture.viewport.resize(800, 500);
    });
    changed([&] {
        fixture.model.newScene();
    });
}

TEST_CASE("Immediate capture returns the matching actual paint PNG and excludes child widgets",
          "[api-observation]") {
    ObservationFixture fixture;
    fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto oldFrame = fixture.viewport.lastRenderedFrame()->frameId;
    auto view = fixture.request<observation::SetViewRequest>();
    view.changes.preset = renderer_gl::EditorView::Front;
    view.changes.orthographic = true;
    REQUIRE(fixture.service.setView(view).hasValue());
    QWidget sentinel(&fixture.viewport);
    sentinel.setGeometry(0, 0, 100, 100);
    sentinel.setStyleSheet(QStringLiteral("background-color: rgb(255, 0, 255)"));
    sentinel.show();
    auto request = fixture.request<observation::CaptureRequest>();
    request.longestEdge = 200;
    CaptureAttempt capture(fixture.service, fixture.viewport, request);
    REQUIRE_FALSE(capture.id.isEmpty());
    capture.wait();
    REQUIRE(capture.result->hasValue());
    const auto& result = *capture.result->value;
    REQUIRE(result.captureId == capture.id);
    REQUIRE(result.frameId > oldFrame);
    REQUIRE(capture.completionFrame);
    REQUIRE(result.frameId == capture.completionFrame->frameId);
    REQUIRE(result.contextGeneration == fixture.viewport.contextGeneration());
    REQUIRE(result.view.viewport.document.documentRevision == request.expectedDocumentRevision);
    REQUIRE(result.view.viewport.viewportRevision == request.expectedViewportRevision);
    REQUIRE(result.view.viewport.view.preset == renderer_gl::EditorView::Front);
    REQUIRE(result.view.viewport.view.orthographic);
    const auto ratio = fixture.viewport.devicePixelRatioF();
    REQUIRE(result.originalPixelSize == QSize(qRound(fixture.viewport.width() * ratio),
                                              qRound(fixture.viewport.height() * ratio)));
    const auto image = QImage::fromData(result.png, "PNG");
    REQUIRE_FALSE(image.isNull());
    REQUIRE(image.size() == result.outputPixelSize);
    REQUIRE(std::max(image.width(), image.height()) == 200);
    REQUIRE(image.pixelColor(5, 5) != QColor(255, 0, 255));
    REQUIRE(result.sha256 ==
            QString::fromLatin1(
                QCryptographicHash::hash(result.png, QCryptographicHash::Sha256).toHex()));
    const auto encoded = observation::ObservationJsonCodec::encode(result);
    REQUIRE(encoded["capturedRegion"] == "gl_viewport");
    REQUIRE(encoded["frameId"].isString());
    REQUIRE(encoded["view"].toObject()["viewMatrix"].toArray().size() == 16);
    REQUIRE(encoded["view"].toObject()["projectionMatrix"].toArray().size() == 16);
    REQUIRE(QByteArray::fromBase64(encoded["pngBase64"].toString().toLatin1()) == result.png);
    REQUIRE(encoded["byteLength"].toInteger() == result.png.size());
}

TEST_CASE("Capture detects intervening visual content and document identity changes without "
          "locking edits",
          "[api-observation]") {
    ObservationFixture fixture;
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        fixture.viewport.setOverlayVisible(false);
        capture.wait();
        requireError(*capture.result, Error::ViewChanged);
    }
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        api::EntityCreateRequest create;
        create.document = fixture.api.documentState().document;
        create.expectedDocumentRevision = fixture.api.documentState().documentRevision;
        create.name = QStringLiteral("捕获等待时的用户内容");
        create.primitive = core::PrimitiveKind::Cube;
        REQUIRE(fixture.api.createEntity(create).hasValue());
        capture.wait();
        requireError(*capture.result, Error::RevisionConflict);
    }
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        fixture.model.newScene();
        capture.wait();
        requireError(*capture.result, Error::StaleDocument);
    }
}

TEST_CASE("Capture timeout cancellation and unavailable window always release pending once",
          "[api-observation]") {
    ObservationFixture fixture;
    fixture.viewport.setUpdatesEnabled(false);
    {
        auto request = fixture.request<observation::CaptureRequest>();
        request.timeoutMs = 20;
        CaptureAttempt capture(fixture.service, fixture.viewport, request);
        capture.wait();
        requireError(*capture.result, Error::CaptureTimeout);
    }
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        REQUIRE_FALSE(fixture.service.cancelCapture(QStringLiteral("wrong-id")));
        REQUIRE(fixture.service.isCapturing());
        REQUIRE(fixture.service.cancelCapture(capture.id));
        REQUIRE_FALSE(fixture.service.cancelCapture(capture.id));
        capture.wait();
        requireError(*capture.result, Error::Cancelled);
    }
    fixture.viewport.setUpdatesEnabled(true);
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        capture.wait();
        REQUIRE(capture.result->hasValue());
    }
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        fixture.viewport.hide();
        capture.wait();
        requireError(*capture.result, Error::ViewportUnavailable);
    }
}

TEST_CASE("Known upload and frame resource failures never become successful nonempty captures",
          "[api-observation]") {
    ObservationFixture fixture;
    fixture.viewport.makeCurrent();
    QOpenGLFunctions_4_1_Core functions;
    REQUIRE(functions.initializeOpenGLFunctions());
    renderer_gl::GpuMesh mesh;
    functions.glEnable(0xffffffffU);
    REQUIRE_FALSE(mesh.upload(functions, renderer_gl::PrimitiveFactory::createCube()));
    REQUIRE_FALSE(mesh.isValid());
    REQUIRE(functions.glGetError() == GL_NO_ERROR);
    fixture.viewport.doneCurrent();
    auto scene = std::make_shared<core::Scene>();
    const auto missing = scene->createEntity("缺失 GPU 上传来源");
    REQUIRE(scene->setMeshRenderer(missing, {999999, 0}));
    fixture.viewport.setScene(scene);
    CaptureAttempt capture(fixture.service, fixture.viewport,
                           fixture.request<observation::CaptureRequest>());
    capture.wait();
    requireError(*capture.result, Error::RenderFailed);
    REQUIRE(capture.completionFrame);
    REQUIRE_FALSE(capture.completionFrame->resources.ready);
    REQUIRE_FALSE(capture.result->hasValue());
}

TEST_CASE("Context rebuild invalidates pending capture and changes the real context generation",
          "[api-observation]") {
    ObservationFixture fixture;
    const auto generation = fixture.viewport.contextGeneration();
    QWidget destination;
    destination.resize(700, 550);
    const auto unparent = qScopeGuard([&] {
        fixture.viewport.setParent(nullptr);
    });
    {
        CaptureAttempt capture(fixture.service, fixture.viewport,
                               fixture.request<observation::CaptureRequest>());
        fixture.viewport.setParent(&destination);
        capture.wait();
        requireError(*capture.result, Error::ViewportUnavailable);
    }
    destination.show();
    fixture.viewport.show();
    REQUIRE(QTest::qWaitForWindowExposed(&destination));
    REQUIRE(QTest::qWaitFor([&] {
        return fixture.viewport.isObservationAvailable() &&
               fixture.viewport.contextGeneration() > generation;
    }));
    CaptureAttempt capture(fixture.service, fixture.viewport,
                           fixture.request<observation::CaptureRequest>());
    capture.wait();
    REQUIRE(capture.result->hasValue());
    REQUIRE(capture.result->value->contextGeneration > generation);
}

TEST_CASE("Window assembly supplies current document and history identity on every actual paint",
          "[api-observation]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* viewport = window.findChild<Viewport*>();
    auto* service = &window.observationService();
    REQUIRE(viewport);
    REQUIRE(service);
    auto& sharedApi = window.apiService();
    auto view = service->getState({sharedApi.documentState().document});
    REQUIRE(view.hasValue());
    api::EntityCreateRequest create;
    create.document = sharedApi.documentState().document;
    create.expectedDocumentRevision = sharedApi.documentState().documentRevision;
    create.name = QStringLiteral("真实装配捕获");
    create.primitive = core::PrimitiveKind::Cube;
    REQUIRE(sharedApi.createEntity(create).hasValue());
    view = service->getState({sharedApi.documentState().document});
    REQUIRE(view.hasValue());
    observation::CaptureRequest request;
    request.document = sharedApi.documentState().document;
    request.expectedDocumentRevision = sharedApi.documentState().documentRevision;
    request.expectedViewportRevision = view.value->viewport.viewportRevision;
    CaptureAttempt capture(*service, *viewport, request);
    capture.wait();
    REQUIRE(capture.result->hasValue());
    const auto& document = capture.result->value->view.viewport.document;
    REQUIRE(document.instanceId == sharedApi.documentState().document.instanceId);
    REQUIRE(document.documentId == sharedApi.documentState().document.documentId);
    REQUIRE(document.documentRevision == sharedApi.documentState().documentRevision);
    REQUIRE(document.historyRevision == sharedApi.documentState().historyRevision);
    window.hide();
}

TEST_CASE("Observation canonical parameters use typed defaults without bridge metadata",
          "[api-observation][observation-canonical]") {
    ObservationFixture fixture;
    auto implicit = fixture.wireRequest();
    implicit.insert("camera", validCamera());
    auto explicitDefaults = implicit;
    explicitDefaults.insert("timeoutMs", int(api::limits::mutationTimeoutMs));
    explicitDefaults.insert("clientSessionId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    explicitDefaults.insert("mutationSequence", "1");
    auto camera = validCamera();
    const core::CameraState defaults;
    camera.insert("focusRadius", defaults.focusRadius);
    camera.insert("maximumDistance", defaults.maximumDistance);
    explicitDefaults.insert("camera", camera);
    const auto a = observation::ObservationJsonCodec::canonicalParams("viewport.setView", implicit);
    const auto b =
        observation::ObservationJsonCodec::canonicalParams("viewport.setView", explicitDefaults);
    REQUIRE(a.hasValue());
    REQUIRE(b.hasValue());
    REQUIRE(*a.value == *b.value);
    REQUIRE_FALSE(a.value->contains("clientSessionId"));
    REQUIRE_FALSE(a.value->contains("mutationSequence"));
    auto capture = fixture.wireRequest();
    const auto captureCanonical =
        observation::ObservationJsonCodec::canonicalParams("viewport.capture", capture);
    REQUIRE(captureCanonical.hasValue());
    REQUIRE((*captureCanonical.value)["longestEdge"].toInt() ==
            int(api::limits::captureLongestEdge));
    REQUIRE((*captureCanonical.value)["timeoutMs"].toInt() == int(api::limits::captureTimeoutMs));
}

TEST_CASE(
    "Observation commit guards reject fully prepared candidates without changing view or history",
    "[api-observation][observation-commit-guard]") {
    ObservationFixture fixture;
    const auto cube = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto before = fixture.state();
    const auto document = fixture.api.documentState();
    const auto camera = fixture.model.editorCamera();
    const auto history = fixture.model.undoStack()->count();
    int calls = 0;
    const api::BeforeCommitGuard reject = [&]() -> std::optional<api::ApiError> {
        ++calls;
        return api::ApiError{Error::DeadlineExceeded,
                             QStringLiteral("test deadline"),
                             {},
                             api::Recovery::QueryResult,
                             fixture.api.documentState()};
    };
    auto view = fixture.request<observation::SetViewRequest>();
    view.changes.camera = core::CameraState{{12, 7, 4}, {1, 2, 3}};
    requireError(fixture.service.setView(view, reject), Error::DeadlineExceeded);
    auto focus = fixture.request<observation::FocusRequest>();
    focus.entityIds = {cube};
    requireError(fixture.service.focus(focus, reject), Error::DeadlineExceeded);
    REQUIRE(calls == 2);
    REQUIRE(fixture.model.editorCamera() == camera);
    REQUIRE(fixture.state().viewport.viewportRevision == before.viewport.viewportRevision);
    REQUIRE(fixture.api.documentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.api.documentState().historyRevision == document.historyRevision);
    REQUIRE(fixture.model.undoStack()->count() == history);
    REQUIRE(fixture.service.setView(view).hasValue());
}

TEST_CASE("Focus rejects geometry whose translation loses finite camera precision",
          "[api-observation][observation-focus-precision]") {
    ObservationFixture fixture;
    const auto cube = fixture.model.createEntity(core::PrimitiveKind::Cube);
    for (int axis = 0; axis < 3; ++axis)
        REQUIRE(fixture.model.setTransformComponent(cube, 0, axis, 100000000.0));
    const auto before = fixture.state();
    const auto camera = fixture.model.editorCamera();
    const auto document = fixture.api.documentState();
    auto request = fixture.request<observation::FocusRequest>();
    request.entityIds = {cube};
    requireError(fixture.service.focus(request), Error::UnsupportedOperation);
    REQUIRE(fixture.model.editorCamera() == camera);
    REQUIRE(fixture.state().viewport.viewportRevision == before.viewport.viewportRevision);
    REQUIRE(fixture.api.documentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.api.documentState().historyRevision == document.historyRevision);
}
