/*
 * 模块名: ImageOutputServiceTests
 * 功能概述: 用共享正式作者命令和实际 Viewport GL 验证请求帧独立输出及最后守卫。
 * 对外接口: Catch2 [image-output-source]/[image-output-service]；不替代013/015 GUI作者验收。
 * 依赖关系: SceneViewModel、EditorApiService、ImageOutputService、Qt Test、真实 GL context。
 * 输入输出: 正式作品到冻结有效状态、PNG/EXR文件、母版及无错误发布断言。
 * 异常与错误: 没有显式测试根/GL或任一数值/来源/文件断言不符直接失败。
 * 维护说明: 三个服务同应用线程；每例独占 E 盘测试目录，无后台或跨重启作业。
 */
#include "editor/SceneViewModel.h"
#include "editor/api/EditorApiService.h"
#include "editor/automation/FilePathPolicy.h"
#include "editor/output/ImageOutputService.h"
#include "renderer_gl/ImageFrameOutput.h"
#include "renderer_gl/ViewportWidget.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

using namespace mini3d;
namespace {
namespace api = editor::api;
QString scratchTemplate() {
    const auto root = qEnvironmentVariable("MINI3D_J1_TEST_ROOT");
    REQUIRE_FALSE(root.isEmpty());
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath("image-service-XXXXXX");
}
QStringList files(const QString& path) {
    return QDir(path).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
struct Authors {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    core::EntityId cube = 0, sun = 0, camera = 0;
    core::MaterialId material;
    template <typename T> T mutation() const {
        T request;
        request.document = service.documentState().document;
        request.expectedDocumentRevision = service.documentState().documentRevision;
        return request;
    }
    Authors() {
        model.newScene();
        auto create = mutation<api::EntityCreateRequest>();
        create.name = "Output cube";
        create.primitive = core::PrimitiveKind::Cube;
        const auto created = service.createEntity(create);
        REQUIRE(created.hasValue());
        cube = created.value->createdEntityIds.front();
        auto createMaterial = mutation<api::MaterialCreateRequest>();
        createMaterial.parameters.baseColor = {.6, .4, .2};
        createMaterial.parameters.roughness = .2;
        const auto madeMaterial = service.createMaterial(createMaterial);
        REQUIRE(madeMaterial.hasValue());
        material = madeMaterial.value->materialId;
        auto assign = mutation<api::EntityAssignMaterialRequest>();
        assign.entityId = cube;
        assign.materialId = material;
        REQUIRE(service.assignMaterial(assign).hasValue());
        auto makeSun = mutation<api::SunCreateRequest>();
        makeSun.name = "Output Sun";
        const auto madeSun = service.createSun(makeSun);
        REQUIRE(madeSun.hasValue());
        sun = madeSun.value->createdEntityIds.front();
        auto makeCamera = mutation<api::CameraCreateRequest>();
        makeCamera.name = "Output camera";
        makeCamera.transform.position = {0, 0, 8};
        const auto madeCamera = service.createCamera(makeCamera);
        REQUIRE(madeCamera.hasValue());
        camera = madeCamera.value->createdEntityIds.front();
        const core::PropertyAddress position{model.scene()->documentUuid(), core::EntityOwner{cube},
                                             core::PropertyId::Position, core::PropertyComponent::X};
        const core::PropertyAddress roughness{model.scene()->documentUuid(), core::MaterialOwner{material},
                                             core::PropertyId::PbrRoughness, core::PropertyComponent::Scalar};
        const core::PropertyAddress irradiance{model.scene()->documentUuid(), core::EntityOwner{sun},
                                               core::PropertyId::SunIrradiance, core::PropertyComponent::Scalar};
        key(position, 1, 0); key(position, 24, 2);
        key(roughness, 1, .2); key(roughness, 24, .8);
        key(irradiance, 1, 1); key(irradiance, 24, 2);
    }
    void key(const core::PropertyAddress& address, double time, double value) {
        auto request = mutation<api::AnimationUpsertPropertyKeyframeRequest>();
        request.address = address;
        request.time = time;
        request.value = value;
        REQUIRE(service.upsertAnimationPropertyKeyframe(request).hasValue());
    }
};
struct Fixture : Authors {
    QTemporaryDir directory{scratchTemplate()};
    renderer_gl::ViewportWidget viewport;
    editor::ImageOutputService output{model, service, viewport};
    Fixture() {
        REQUIRE(directory.isValid());
        QString error;
        const auto policy = editor::automation::FilePathPolicy::create({}, {directory.path()}, error);
        REQUIRE(policy);
        service.setFileAccessPolicies({}, [policy](const QString& path, QString& reason) {
            return policy->authorizeNewFile(path, reason);
        });
        viewport.setScene(model.scene());
        viewport.setAssets(model.assets());
        viewport.setOverlayVisible(false);
        viewport.resize(1280, 720);
        model.setAnimationViewProvider([&] {
            const auto camera = viewport.editorCameraSnapshot();
            REQUIRE(camera);
            return *camera;
        });
        viewport.setFrameDisplayStateProvider([&]() -> std::optional<renderer_gl::FrameDisplayState> {
            std::shared_ptr<const renderer_gl::InstalledPose> display;
            QString diagnostic;
            if (!model.prepareDisplayPose(display, diagnostic)) return std::nullopt;
            const auto& document = service.documentState();
            renderer_gl::FrameDisplayState state;
            state.document = {document.document.instanceId, document.document.documentId,
                               document.documentRevision, document.historyRevision};
            state.identity = {document.document.instanceId, document.document.documentId,
                document.documentRevision, model.animationEvaluationId(), model.animationFrame(), model.animationMode()};
            state.sessionRevision = model.animationSessionRevision();
            if (model.animationMode() == editor::AnimationMode::Base) state.baseDisplay = display;
            else state.pose = display;
            return state;
        });
        QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &viewport,
                         &renderer_gl::ViewportWidget::notifySceneVisualChange);
        QObject::connect(&model, &editor::SceneViewModel::animationPoseChanged, &viewport,
                         &renderer_gl::ViewportWidget::notifyFrameDisplayStateChanged);
        QObject::connect(&model, &editor::SceneViewModel::previewCameraChanged, &viewport,
                         &renderer_gl::ViewportWidget::setPreviewCamera);
        viewport.setAttribute(Qt::WA_ShowWithoutActivating);
        viewport.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        viewport.show();
        REQUIRE(QTest::qWaitForWindowExposed(&viewport));
        viewport.repaint();
        QApplication::processEvents();
        REQUIRE(viewport.isObservationAvailable());
        REQUIRE(viewport.lastRenderedFrame());
        REQUIRE(viewport.lastRenderedFrame()->resources.ready);
        REQUIRE(viewport.lastRenderedFrame()->resources.imageFrame);
    }
    editor::ImageOutputRequest request(const QString& name, renderer_gl::ImageFileFormat format) const {
        auto result = mutation<editor::ImageOutputRequest>();
        result.path = directory.filePath(name);
        result.frame = 12.5;
        result.camera = camera;
        result.format = format;
        result.timeoutMs = int(api::limits::mutationTimeoutMaximumMs);
        return result;
    }
    void checkNoFile(const editor::ImageOutputRequest& request) const {
        CHECK_FALSE(output.isPreparing());
        CHECK_FALSE(QFileInfo::exists(request.path));
        CHECK(files(directory.path()).isEmpty());
    }
};
} // namespace

TEST_CASE("Independent output freezes requested full animation without moving the Base cursor", "[image-output-source]") {
    Authors fixture;
    REQUIRE(fixture.model.animationMode() == editor::AnimationMode::Base);
    const auto cursor = fixture.model.animationFrame();
    const auto revision = fixture.service.documentState().documentRevision;
    const auto count = fixture.model.undoStack()->count();
    std::uint64_t previous = 0;
    for (const auto frame : {24.0, 1.0, 12.5, 1.0}) {
        const auto result = fixture.model.freezeImageOutput(frame, fixture.camera);
        INFO((result.error ? result.error->message.toStdString() : ""));
        REQUIRE(result.hasValue());
        const auto& frozen = **result.value;
        REQUIRE(renderer_gl::isAllowedImageOutputDerivation(frozen.originSource(), frozen.state().source));
        CHECK(frozen.originSource().algorithmBuild.find("/base-only-1") == std::string::npos);
        CHECK(frozen.originSource().frameTime == frame);
        CHECK(frozen.state().source.evaluationId > previous);
        previous = frozen.state().source.evaluationId;
        REQUIRE_FALSE(fixture.model.validateFrozenImageOutput(frozen));
        const auto* node = frozen.state().find(fixture.cube);
        REQUIRE(node);
        const auto expected = frame == 24 ? 2.0 : frame == 12.5 ? 1.0 : 0.0;
        CHECK(std::abs(node->localPosition.x - expected) <= 1e-12);
        CHECK(std::abs(frozen.state().content->materials.at(fixture.material).effective.roughness - (.2 + expected * .3)) <= 1e-12);
        REQUIRE(frozen.state().content->visibleSuns.size() == 1);
        CHECK(std::abs(frozen.state().content->visibleSuns.front().effective.irradiance - (1 + expected * .5)) <= 1e-12);
        CHECK(fixture.model.animationFrame() == cursor);
        CHECK(fixture.model.animationMode() == editor::AnimationMode::Base);
        CHECK(fixture.model.undoStack()->count() == count);
        CHECK(fixture.service.documentState().documentRevision == revision);
    }
    CHECK_FALSE(fixture.model.freezeImageOutput(0, fixture.camera).hasValue());
    CHECK_FALSE(fixture.model.freezeImageOutput(12.5, fixture.cube).hasValue());
}

TEST_CASE("Real same-context fixed output owns the requested mother after viewport resize", "[image-output-service][gpu]") {
    Fixture fixture;
    fixture.viewport.resize(431, 287);
    fixture.viewport.repaint();
    QApplication::processEvents();
    const auto cursor = fixture.model.animationFrame();
    const auto revision = fixture.service.documentState().documentRevision;
    QSignalSpy preparing(&fixture.output, &editor::ImageOutputService::preparingChanged);
    const auto pngRequest = fixture.request("frame.png", renderer_gl::ImageFileFormat::Png);
    const auto png = fixture.output.write(pngRequest);
    INFO((png.error ? png.error->message.toStdString() : ""));
    REQUIRE(png.hasValue());
    REQUIRE(png.value->frame);
    CHECK(png.value->frame->pixels().width == 1280);
    CHECK(png.value->frame->pixels().height == 720);
    CHECK(png.value->frame->pixels().source.frameTime == 12.5);
    CHECK(png.value->frame->stamp().devicePixelRatio == 1);
    CHECK(png.value->frame->stamp().cameraEntity == fixture.camera);
    CHECK(fixture.viewport.size() == QSize(431, 287));
    CHECK(fixture.model.animationFrame() == cursor);
    CHECK(fixture.service.documentState().documentRevision == revision);
    CHECK_FALSE(fixture.output.isPreparing());
    REQUIRE(preparing.size() == 2);
    CHECK(preparing[0][0].toBool());
    CHECK_FALSE(preparing[1][0].toBool());
    QString diagnostic;
    const auto expectedPng = renderer_gl::ImageFrameOutput::encode(*png.value->frame,
        renderer_gl::ImageFileFormat::Png, *fixture.viewport.imageOutputColor(), diagnostic);
    REQUIRE(expectedPng);
    REQUIRE(renderer_gl::ImageFrameOutput::validateFile(pngRequest.path, *png.value->frame, *expectedPng, diagnostic));
    const auto exrRequest = fixture.request("frame.exr", renderer_gl::ImageFileFormat::Exr);
    const auto exr = fixture.output.write(exrRequest);
    INFO((exr.error ? exr.error->message.toStdString() : ""));
    REQUIRE(exr.hasValue());
    CHECK(exr.value->frame->stamp().frameId > png.value->frame->stamp().frameId);
    CHECK(exr.value->frame->pixels().source.evaluationId > png.value->frame->pixels().source.evaluationId);
    CHECK(std::get<core::FrozenSourceHandle>(exr.value->frame->pixels().source.sourceHandle) !=
          std::get<core::FrozenSourceHandle>(png.value->frame->pixels().source.sourceHandle));
    CHECK(renderer_gl::isAllowedImageOutputDerivation(exr.value->frame->originSource(), exr.value->frame->pixels().source));
    const auto expectedExr = renderer_gl::ImageFrameOutput::encode(*exr.value->frame,
        renderer_gl::ImageFileFormat::Exr, *fixture.viewport.imageOutputColor(), diagnostic);
    REQUIRE(expectedExr);
    REQUIRE(renderer_gl::ImageFrameOutput::validateFile(exrRequest.path, *exr.value->frame, *expectedExr, diagnostic));
    const auto existing = fixture.output.write(pngRequest);
    REQUIRE_FALSE(existing.hasValue());
    CHECK(QFileInfo::exists(pngRequest.path));
    CHECK(files(fixture.directory.path()) == QStringList{"frame.exr", "frame.png"});
}

TEST_CASE("Fresh preview and independently evaluated output retain the same mother at exact frames", "[image-output-service][gpu]") {
    Fixture fixture;
    REQUIRE(fixture.model.setPreviewCamera(fixture.camera));
    auto previewRequest = fixture.mutation<api::AnimationSetPreviewRequest>();
    previewRequest.enabled = true;
    previewRequest.expectedSessionRevision = fixture.model.animationSessionRevision();
    REQUIRE(fixture.service.setAnimationPreview(previewRequest).hasValue());
    const auto revision = fixture.service.documentState().documentRevision;
    const auto historyCount = fixture.model.undoStack()->count();
    std::uint64_t lastPaint = fixture.viewport.lastRenderedFrame()->frameId;
    int sequence = 0;
    for (const auto time : {24.0, 1.0, 12.5, 1.0}) {
        INFO(time);
        auto seek = fixture.mutation<api::AnimationSetFrameRequest>();
        seek.frame = time;
        seek.expectedSessionRevision = fixture.model.animationSessionRevision();
        REQUIRE(fixture.service.setAnimationFrame(seek).hasValue());
        fixture.viewport.repaint();
        QApplication::processEvents();
        const auto& painted = fixture.viewport.lastRenderedFrame();
        REQUIRE(painted);
        REQUIRE(painted->resources.ready);
        REQUIRE(painted->resources.imageFrame);
        REQUIRE(painted->frameId > lastPaint);
        lastPaint = painted->frameId;
        const auto preview = painted->resources.imageFrame;
        REQUIRE(preview->pixels().width == 1280);
        REQUIRE(preview->pixels().height == 720);
        REQUIRE(preview->stamp().devicePixelRatio == 1);
        REQUIRE(preview->stamp().cameraEntity == fixture.camera);
        REQUIRE(preview->pixels().source.frameTime == time);
        auto request = fixture.request(QString("same-frame-%1.png").arg(++sequence), renderer_gl::ImageFileFormat::Png);
        request.frame = time;
        const auto output = fixture.output.write(request);
        INFO((output.error ? output.error->message.toStdString() : ""));
        REQUIRE(output.hasValue());
        const auto& frame = *output.value->frame;
        REQUIRE(core::isSameWorkSource(preview->pixels().source, frame.originSource()));
        REQUIRE(renderer_gl::isAllowedImageOutputDerivation(frame.originSource(), frame.pixels().source));
        CHECK_FALSE(core::isSameWorkSource(preview->pixels().source, frame.pixels().source));
        CHECK(preview->pixels().source.evaluationSessionId != frame.pixels().source.evaluationSessionId);
        CHECK(preview->stamp().sessionId != frame.stamp().sessionId);
        CHECK(preview->pixels().camera == frame.pixels().camera);
        CHECK(preview->pixels().backendBuild == frame.pixels().backendBuild);
        CHECK(preview->pixels().color == frame.pixels().color);
        CHECK(preview->pixels().geometryStages == frame.pixels().geometryStages);
        const auto previewMetadata = QJsonDocument::fromJson(preview->provenanceJson(renderer_gl::ImageFileFormat::Png)).object();
        const auto outputMetadata = QJsonDocument::fromJson(frame.provenanceJson(renderer_gl::ImageFileFormat::Png)).object();
        for (const auto* field : {"camera", "settings", "backend", "color", "geometryStages", "master"})
            CHECK(previewMetadata[field] == outputMetadata[field]);
        REQUIRE(preview->pixels().beauty.size() == frame.pixels().beauty.size());
        std::size_t rgbMismatches = 0, alphaMismatches = 0, coverageMismatches = 0;
        for (std::size_t index = 0; index < frame.pixels().beauty.size(); ++index) {
            for (int channel = 0; channel < 3; ++channel) {
                const double reference = preview->pixels().beauty[index][channel];
                const double actual = frame.pixels().beauty[index][channel];
                if (std::abs(actual - reference) > 2e-5 + 2e-6 * std::abs(reference))
                    ++rgbMismatches;
            }
            if (std::bit_cast<std::uint32_t>(preview->pixels().beauty[index].a) !=
                std::bit_cast<std::uint32_t>(frame.pixels().beauty[index].a))
                ++alphaMismatches;
            if (std::bit_cast<std::uint32_t>(preview->pixels().coverage[index]) !=
                std::bit_cast<std::uint32_t>(frame.pixels().coverage[index]))
                ++coverageMismatches;
        }
        CHECK(rgbMismatches == 0);
        CHECK(alphaMismatches == 0);
        CHECK(coverageMismatches == 0);
        const auto color = fixture.viewport.imageOutputColor();
        REQUIRE(color);
        // 此 J1 OPAQUE 样例只含 0/1 alpha；预览线性黑背景与 straight PNG 可直接比较。
        REQUIRE(std::all_of(preview->pixels().beauty.begin(), preview->pixels().beauty.end(),
            [](const glm::vec4& pixel) {
                return pixel.a == 1 || (pixel.a == 0 && pixel.r == 0 && pixel.g == 0 && pixel.b == 0);
            }));
        std::vector<glm::vec4> display(preview->pixels().beauty.size());
        QString diagnostic;
        REQUIRE(color->applyCpuRgba(preview->pixels().beauty, display, diagnostic));
        const auto decoded = QImage(request.path).convertToFormat(QImage::Format_RGBA8888);
        REQUIRE(decoded.size() == QSize(1280, 720));
        std::size_t byteMismatches = 0;
        for (std::size_t index = 0; index < display.size(); ++index) {
            const auto* pixel = decoded.constScanLine(int(index / 1280)) + (index % 1280) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                const int expected = int(std::lround(std::clamp(display[index][channel], 0.0F, 1.0F) * 255.0F));
                if (std::abs(int(pixel[channel]) - expected) > 1) ++byteMismatches;
            }
        }
        CHECK(byteMismatches == 0);
        CHECK(fixture.model.animationFrame() == time);
        CHECK(fixture.service.documentState().documentRevision == revision);
        CHECK(fixture.model.undoStack()->count() == historyCount);
    }
}

TEST_CASE("Preparing reentry and cancellation create no output or owned leftovers", "[image-output-service][gpu]") {
    Fixture fixture;
    std::atomic_bool cancelled{false};
    auto request = fixture.request("cancelled.png", renderer_gl::ImageFileFormat::Png);
    request.cancelled = &cancelled;
    bool observed = false;
    QObject::connect(&fixture.output, &editor::ImageOutputService::preparingChanged, &fixture.output,
        [&](bool preparing) {
            if (!preparing) return;
            observed = true;
            CHECK(fixture.output.isPreparing());
            const auto reentry = fixture.output.write(request);
            REQUIRE(reentry.error);
            CHECK(reentry.error->code == api::ErrorCode::Busy);
            cancelled.store(true);
        });
    const auto result = fixture.output.write(request);
    REQUIRE(result.error);
    CHECK(result.error->code == api::ErrorCode::Cancelled);
    CHECK(observed);
    fixture.checkNoFile(request);
}

TEST_CASE("Final cancellation permission revocation and stale CAS never publish the validated image", "[image-output-service][gpu]") {
    Fixture fixture;
    std::atomic_bool cancelled{false};
    auto request = fixture.request("refused.exr", renderer_gl::ImageFileFormat::Exr);
    request.cancelled = &cancelled;
    int guards = 0;
    enum class Refusal { Cancel, Revoke, Stale, Busy, View } refusal = Refusal::Cancel;
    SECTION("Current cancellation after closed-file validation") { refusal = Refusal::Cancel; }
    SECTION("Current External roots revoked after validation") { refusal = Refusal::Revoke; }
    SECTION("A reentrant formal edit makes the frozen CAS stale") { refusal = Refusal::Stale; }
    SECTION("A reentrant draft or interaction is busy") { refusal = Refusal::Busy; }
    SECTION("Actual viewport identity changes after validation") { refusal = Refusal::View; }
    auto previous = fixture.service.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        if (++guards != 2) return std::nullopt;
        const auto temporary = QDir(fixture.directory.path()).entryList({".mini3d-image-*"}, QDir::Files | QDir::Hidden);
        REQUIRE(temporary.size() == 1);
        if (refusal == Refusal::Cancel) cancelled.store(true);
        else if (refusal == Refusal::Revoke) fixture.service.setFileAccessPolicies({}, {});
        else if (refusal == Refusal::Stale) REQUIRE(fixture.model.renameEntity(fixture.cube, "Changed during final guard"));
        else if (refusal == Refusal::Busy) fixture.model.setExternalBusy("test-final-busy", true);
        else fixture.viewport.setOverlayVisible(true);
        return std::nullopt;
    });
    const auto result = fixture.output.write(request);
    fixture.service.exchangeBeforeCommitGuard(std::move(previous));
    INFO((result.error ? result.error->message.toStdString() : ""));
    REQUIRE(result.error);
    CHECK(guards >= 2);
    if (refusal == Refusal::Cancel) CHECK(result.error->code == api::ErrorCode::Cancelled);
    else if (refusal == Refusal::Revoke) CHECK(result.error->code == api::ErrorCode::PermissionDenied);
    else if (refusal == Refusal::Stale) CHECK(result.error->code == api::ErrorCode::RevisionConflict);
    else if (refusal == Refusal::Busy) CHECK((result.error->code == api::ErrorCode::Busy || result.error->code == api::ErrorCode::RevisionConflict));
    else CHECK(result.error->code == api::ErrorCode::ViewChanged);
    fixture.checkNoFile(request);
}

TEST_CASE("Failed output cannot publish an earlier successful mother and unauthorized roots stay closed", "[image-output-service][gpu]") {
    Fixture fixture;
    const auto previousRequest = fixture.request("previous.png", renderer_gl::ImageFileFormat::Png);
    const auto previous = fixture.output.write(previousRequest);
    INFO((previous.error ? previous.error->message.toStdString() : ""));
    REQUIRE(previous.hasValue());
    const auto previousBytes = readBytes(previousRequest.path);
    auto request = fixture.request("invalid.png", renderer_gl::ImageFileFormat::Png);
    SECTION("Invalid request frame") { request.frame = 0; }
    SECTION("Invisible or absent device") { request.camera = fixture.cube; }
    SECTION("Stale initial CAS") { ++request.expectedDocumentRevision; }
    SECTION("Deadline") { request.timeoutMs = 1; }
    SECTION("No External authorization") { fixture.service.setFileAccessPolicies({}, {}); }
    SECTION("Outside the approved root") {
        const QFileInfo directory(fixture.directory.path());
        request.path = directory.absoluteDir().filePath(directory.fileName() + "-outside-approved.png");
    }
    const auto result = fixture.output.write(request);
    REQUIRE(result.error);
    CHECK_FALSE(QFileInfo::exists(request.path));
    CHECK_FALSE(fixture.output.isPreparing());
    CHECK(readBytes(previousRequest.path) == previousBytes);
    CHECK(files(fixture.directory.path()) == QStringList{"previous.png"});
}

TEST_CASE("Self-replacing Busy provider retains its owner and preserves subsequent Busy and CAS gates", "[image-output-source]") {
    Authors fixture;
    const auto state = fixture.service.documentState();
    auto owner = std::make_shared<int>(7);
    const std::weak_ptr<int> weakOwner = owner;
    int originalCalls = 0, replacementCalls = 0;
    fixture.service.setBusyProvider([&, owner = std::move(owner)] {
        ++originalCalls;
        fixture.service.setBusyProvider([&] {
            ++replacementCalls;
            return QStringList{"replacement-busy"};
        });
        REQUIRE_FALSE(weakOwner.expired());
        CHECK(weakOwner.use_count() == 1);
        CHECK(*owner == 7);
        return QStringList{};
    });
    REQUIRE_FALSE(fixture.service.validateContext(state.document, false, true, state.documentRevision));
    CHECK(originalCalls == 1);
    CHECK(weakOwner.expired());
    const auto busy = fixture.service.validateContext(state.document, false, true, state.documentRevision);
    REQUIRE(busy);
    CHECK(busy->code == api::ErrorCode::Busy);
    CHECK(replacementCalls == 1);
    const auto stale = fixture.service.validateContext(state.document, false, true, state.documentRevision + 1);
    REQUIRE(stale);
    CHECK(stale->code == api::ErrorCode::RevisionConflict);
    CHECK(replacementCalls == 1);
    fixture.service.setBusyProvider({});
    CHECK_FALSE(fixture.service.validateContext(state.document, false, true, state.documentRevision));
    CHECK(fixture.service.documentState().documentRevision == state.documentRevision);
    CHECK(fixture.service.documentState().historyRevision == state.historyRevision);
}

TEST_CASE("Existing-target replacement callback retains its owner without preserving revoked roots", "[image-output-source]") {
    Authors fixture;
    QTemporaryDir directory{scratchTemplate()}, otherDirectory{scratchTemplate()};
    REQUIRE(directory.isValid());
    REQUIRE(otherDirectory.isValid());
    const auto path = directory.filePath("existing.png");
    const QByteArray originalBytes{"existing product must not change"};
    QFile existing(path);
    REQUIRE(existing.open(QIODevice::WriteOnly));
    REQUIRE(existing.write(originalBytes) == originalBytes.size());
    existing.close();
    QString diagnostic;
    const auto policy = editor::automation::FilePathPolicy::create({}, {directory.path()}, diagnostic);
    const auto movedPolicy = editor::automation::FilePathPolicy::create({}, {otherDirectory.path()}, diagnostic);
    REQUIRE(policy);
    REQUIRE(movedPolicy);
    bool revoke = true;
    SECTION("Callback revokes all External policies") { revoke = true; }
    SECTION("Callback replaces External policies with a different root") { revoke = false; }
    auto owner = std::make_shared<int>(11);
    const std::weak_ptr<int> weakOwner = owner;
    int replacementCalls = 0;
    fixture.service.setFileAccessPolicies({}, [policy](const QString& target, QString& reason) {
        return policy->authorizeNewFile(target, reason);
    }, [&, owner = std::move(owner), policy](const QString& target, QString& reason) {
        ++replacementCalls;
        if (revoke) fixture.service.setFileAccessPolicies({}, {});
        else fixture.service.setFileAccessPolicies({}, [movedPolicy](const QString& target, QString& reason) {
            return movedPolicy->authorizeNewFile(target, reason);
        }, [movedPolicy](const QString& target, QString& reason) {
            return movedPolicy->authorizeWrite(target, reason);
        });
        REQUIRE_FALSE(weakOwner.expired());
        CHECK(weakOwner.use_count() == 1);
        CHECK(*owner == 11);
        return policy->authorizeWrite(target, reason);
    });
    const auto state = fixture.service.documentState();
    const auto newPath = directory.filePath("new.png");
    const auto access = fixture.service.prepareImageOutputAccess(newPath);
    REQUIRE(access.hasValue());
    REQUIRE_FALSE(fixture.service.confirmImageOutputAccess(*access.value, state.document, state.documentRevision));
    const auto result = fixture.service.prepareImageOutputAccess(path);
    REQUIRE(result.error);
    CHECK(result.error->code == api::ErrorCode::OverwriteDenied);
    CHECK(replacementCalls == 1);
    CHECK(weakOwner.expired());
    const auto oldAccess = fixture.service.confirmImageOutputAccess(*access.value, state.document, state.documentRevision);
    REQUIRE(oldAccess);
    CHECK(oldAccess->code == api::ErrorCode::PermissionDenied);
    const auto oldRoot = fixture.service.prepareImageOutputAccess(newPath);
    REQUIRE(oldRoot.error);
    CHECK(oldRoot.error->code == (revoke ? api::ErrorCode::PermissionDenied : api::ErrorCode::PathDenied));
    CHECK(readBytes(path) == originalBytes);
    CHECK(files(directory.path()) == QStringList{"existing.png"});
    CHECK(files(otherDirectory.path()).isEmpty());
}

TEST_CASE("Third view-provider read rejects recursive output freeze without reusing evaluation IDs", "[image-output-source]") {
    Authors fixture;
    const auto state = fixture.service.documentState();
    const auto animation = fixture.model.scene()->animation();
    const auto materials = fixture.model.scene()->materialDataOwner();
    const auto position = fixture.model.scene()->find(fixture.cube)->transform.position;
    const auto cursor = fixture.model.animationFrame();
    const auto evaluation = fixture.model.animationEvaluationId();
    const auto session = fixture.model.animationSessionRevision();
    const auto pose = fixture.model.installedAnimationPose();
    const auto historyCount = fixture.model.undoStack()->count();
    const auto historyIndex = fixture.model.undoStack()->index();
    renderer_gl::EditorCamera view;
    view.setState(fixture.model.editorCamera());
    int viewCalls = 0, nestedCalls = 0;
    fixture.model.setAnimationViewProvider([&] {
        if (++viewCalls == 3) {
            ++nestedCalls;
            const auto nested = fixture.model.freezeImageOutput(12.5, fixture.camera);
            REQUIRE_FALSE(nested.hasValue());
            REQUIRE(nested.error);
            CHECK(nested.error->code == api::ErrorCode::Busy);
        }
        return view;
    });
    const auto invalid = fixture.model.freezeImageOutput(0, fixture.camera);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code == api::ErrorCode::InvalidArgument);
    CHECK(viewCalls == 0);
    std::uint64_t previous = 0;
    for (const auto frame : {24.0, 1.0, 12.5, 1.0}) {
        const auto result = fixture.model.freezeImageOutput(frame, fixture.camera);
        INFO((result.error ? result.error->message.toStdString() : ""));
        REQUIRE(result.hasValue());
        CHECK((*result.value)->state().source.evaluationId > previous);
        previous = (*result.value)->state().source.evaluationId;
        CHECK(fixture.model.scene()->animation() == animation);
        CHECK(fixture.model.scene()->materialDataOwner() == materials);
        CHECK(fixture.model.scene()->find(fixture.cube)->transform.position == position);
        CHECK(fixture.model.animationFrame() == cursor);
        CHECK(fixture.model.animationMode() == editor::AnimationMode::Base);
        CHECK(fixture.model.animationEvaluationId() == evaluation);
        CHECK(fixture.model.animationSessionRevision() == session);
        CHECK(fixture.model.installedAnimationPose() == pose);
        CHECK(fixture.model.undoStack()->count() == historyCount);
        CHECK(fixture.model.undoStack()->index() == historyIndex);
        CHECK(fixture.service.documentState().document == state.document);
        CHECK(fixture.service.documentState().documentRevision == state.documentRevision);
        CHECK(fixture.service.documentState().historyRevision == state.historyRevision);
    }
    CHECK(viewCalls == 12);
    CHECK(nestedCalls == 1);
}
