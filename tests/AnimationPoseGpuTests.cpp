/*
 * 模块名: AnimationPoseGpuTests
 * 功能概述: 用真实离屏 GL 像素验证冻结掩码与范围/拾取一致，并观察跨模式网格上传。
 * 对外接口: Catch2 [animation-pose-gpu] 用例；复用 mini3d_gpu_tests 的入口与资源初始化。
 * 依赖关系: Renderer、InstalledPose、RayCaster、Qt Gui/OpenGL、Catch2。
 * 输入输出: 两块独立面和完整掩码到实际 FBO 像素、命中和成功上传计数。
 * 异常与错误: Context、GL 或断言失败直接失败，不跳过、不把模拟计数当 GPU 证据。
 * 维护说明: GPU 对象先于 Context 释放；不保存图片、不改用户文件或窗口。
 */
#include "renderer_gl/InstalledPose.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/Renderer.h"

#include <QColor>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_4_1_Core>
#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;

namespace {
core::modeling::EditableMesh twoPatchMesh() {
    core::modeling::EditableMesh mesh;
    for (const float center : {-1.0F, 1.0F}) {
        core::modeling::EditableFace face;
        face.id = mesh.faces.size() + 1;
        for (const auto& point :
             std::array<glm::vec3, 4>{{{center - 0.4F, -0.4F, 0},
                                       {center + 0.4F, -0.4F, 0},
                                       {center + 0.4F, 0.4F, 0},
                                       {center - 0.4F, 0.4F, 0}}}) {
            const auto vertex = mesh.vertices.size() + 1;
            mesh.vertices.push_back({vertex, point});
            face.corners.push_back({vertex, vertex, {0, 0}, glm::vec3(0, 0, 1)});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

core::EntityId installPatches(core::Scene& scene) {
    const auto entity = scene.createEntity("Two patches");
    std::string error;
    const auto prepared = scene.prepareEditableGeometry(entity, twoPatchMesh(), error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    core::SurfaceStyle surface;
    surface.tint = {1, 0, 0};
    surface.useTexture = false;
    surface.useVertexColor = false;
    REQUIRE(scene.setSurface(entity, surface));
    return entity;
}

renderer_gl::InstalledPose gpuPose(const core::Scene& scene,
                                   std::shared_ptr<const assets::AssetManager> assets,
                                   const core::ViewportVisibility& visibility = {},
                                   const core::SceneAnimation& animation = {},
                                   core::FrameTime frame = 1) {
    std::string error;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    auto result = core::evaluateAnimationPose(*inputs, animation, frame);
    REQUIRE(result.pose);
    renderer_gl::InstalledPose pose;
    pose.identity = {QStringLiteral("gpu-instance"), QStringLiteral("gpu-document"), 1, 1,
                     frame, renderer_gl::AnimationMode::PreviewPaused};
    pose.numerics = std::make_shared<const core::EvaluatedPose>(std::move(*result.pose));
    pose.geometry = renderer_gl::makePoseGeometry(scene, std::move(assets), visibility, *inputs);
    REQUIRE(pose.geometry);
    return pose;
}

struct PoseGpuContext {
    static constexpr int width = 320;
    static constexpr int height = 160;
    QOpenGLContext context;
    QOffscreenSurface surface;
    QOpenGLFunctions_4_1_Core gl;
    std::unique_ptr<QOpenGLFramebufferObject> framebuffer;
    renderer_gl::Renderer renderer;

    PoseGpuContext() {
        QSurfaceFormat format;
        format.setVersion(4, 1);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        REQUIRE(context.create());
        surface.setFormat(context.format());
        surface.create();
        REQUIRE(surface.isValid());
        REQUIRE(context.makeCurrent(&surface));
        REQUIRE(gl.initializeOpenGLFunctions());
        framebuffer = std::make_unique<QOpenGLFramebufferObject>(
            width, height, QOpenGLFramebufferObject::CombinedDepthStencil);
        REQUIRE(framebuffer->isValid());
        REQUIRE(renderer.initialize(gl));
        renderer.resize(width, height);
        REQUIRE(renderer.setCameraState({{0, 0, 4}, {0, 0, 0}}));
        renderer.setCameraView(renderer_gl::EditorView::Front);
        renderer.setOrthographic(true);
    }

    ~PoseGpuContext() {
        renderer.destroy();
        framebuffer.reset();
        context.doneCurrent();
    }

    QImage draw(const core::Scene& scene, const assets::AssetManager& assets,
                const renderer_gl::InstalledPose* pose = nullptr) {
        REQUIRE(framebuffer->bind());
        gl.glViewport(0, 0, width, height);
        const auto status = renderer.render(
            scene, assets, 0, false, -1, 0, renderer_gl::GizmoTool::None,
            renderer_gl::GizmoSpace::World, nullptr, 5, false, false, 0, nullptr,
            std::nullopt, pose);
        INFO(status.error.toStdString());
        REQUIRE(status.ready);
        gl.glFinish();
        const auto image = framebuffer->toImage();
        REQUIRE_FALSE(image.isNull());
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
        return image;
    }
};

QColor patchPixel(const QImage& image, const renderer_gl::Renderer& renderer, float x) {
    const auto clip = renderer.camera().viewProjectionMatrix() * glm::vec4(x, 0, 0, 1);
    REQUIRE(clip.w > 0);
    const auto pixelX = static_cast<int>((clip.x / clip.w * 0.5F + 0.5F) * image.width());
    const auto pixelY = static_cast<int>((0.5F - clip.y / clip.w * 0.5F) * image.height());
    REQUIRE(pixelX >= 0);
    REQUIRE(pixelX < image.width());
    REQUIRE(pixelY >= 0);
    REQUIRE(pixelY < image.height());
    return image.pixelColor(pixelX, pixelY);
}

void requirePatches(const QImage& image, const renderer_gl::Renderer& renderer,
                    bool leftVisible, bool rightVisible) {
    for (const auto& [x, visible] :
         std::array<std::pair<float, bool>, 2>{{{-1, leftVisible}, {1, rightVisible}}}) {
        const auto color = patchPixel(image, renderer, x);
        REQUIRE((color.red() > color.green() + 50) == visible);
        if (!visible)
            REQUIRE(color == QColor(59, 59, 59));
    }
}
} // namespace

TEST_CASE("Frozen pose masks keep actual GL pixels bounds and picking consistent",
          "[gpu][animation][animation-pose-gpu]") {
    PoseGpuContext gpu;
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto entity = installPatches(scene);
    core::ViewportVisibility hideLeft;
    hideLeft.editedEntity = entity;
    hideLeft.faces = {1};
    auto hideRight = hideLeft;
    hideRight.faces = {2};
    const auto poseLeftHidden = gpuPose(scene, assets, hideLeft);
    const auto poseRightHidden = gpuPose(scene, assets, hideRight);
    REQUIRE(poseLeftHidden.geometry->visibility.faces.size() ==
            poseRightHidden.geometry->visibility.faces.size());
    QString error;
    REQUIRE(renderer_gl::validatePoseGeometry(*poseLeftHidden.numerics,
                                              *poseLeftHidden.geometry, gpu.renderer.camera(),
                                              0, error));
    gpu.renderer.setViewportVisibility(hideRight);
    const auto base = gpu.draw(scene, *assets);
    requirePatches(base, gpu.renderer, true, false);
    const auto frozen = gpu.draw(scene, *assets, &poseLeftHidden);
    requirePatches(frozen, gpu.renderer, false, true);
    REQUIRE(frozen != base);
    const auto bounds = renderer_gl::RayCaster::worldBounds(scene, *assets, entity, hideRight,
                                                            &poseLeftHidden);
    REQUIRE(bounds.minimum.x > 0);
    REQUIRE(renderer_gl::RayCaster::pick(scene, *assets, {{-1, 0, 4}, {0, 0, -1}}, hideRight,
                                          &poseLeftHidden) == 0);
    REQUIRE(renderer_gl::RayCaster::pick(scene, *assets, {{1, 0, 4}, {0, 0, -1}}, hideRight,
                                          &poseLeftHidden) == entity);
    // 再读基础视图证明预览没有临时覆盖 Renderer 的会话掩码。
    requirePatches(gpu.draw(scene, *assets), gpu.renderer, true, false);
    requirePatches(gpu.draw(scene, *assets, &poseLeftHidden), gpu.renderer, false, true);
    const auto uploads = gpu.renderer.editableMeshUploadCount();
    gpu.renderer.setViewportVisibility(hideRight);
    REQUIRE(gpu.draw(scene, *assets, &poseLeftHidden) == frozen);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == uploads);

    gpu.renderer.setViewportVisibility(hideLeft);
    requirePatches(gpu.draw(scene, *assets, &poseRightHidden), gpu.renderer, true, false);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == uploads + 1);
    REQUIRE(renderer_gl::RayCaster::worldBounds(scene, *assets, entity, hideLeft,
                                                &poseRightHidden).maximum.x < 0);
    REQUIRE(renderer_gl::RayCaster::pick(scene, *assets, {{-1, 0, 4}, {0, 0, -1}}, hideLeft,
                                          &poseRightHidden) == entity);
    REQUIRE(renderer_gl::RayCaster::pick(scene, *assets, {{1, 0, 4}, {0, 0, -1}}, hideLeft,
                                          &poseRightHidden) == 0);
    requirePatches(gpu.draw(scene, *assets, &poseLeftHidden), gpu.renderer, false, true);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == uploads + 2);
}

TEST_CASE("Base preview and repeated pose ticks share actual editable GPU uploads",
          "[gpu][animation][animation-pose-gpu]") {
    PoseGpuContext gpu;
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto entity = installPatches(scene);
    core::SceneAnimation animation;
    animation.tracks[{entity, core::AnimationChannel::Position}] =
        {{{1, {0, 0, 0}}, {49, {0.3, 0, 0}}}};
    auto pose = gpuPose(scene, assets, {}, animation);
    const auto* record = scene.editableMesh(scene.find(entity)->editableMesh);
    REQUIRE(record);
    REQUIRE(record->evaluationRevision > 0);
    REQUIRE(pose.geometry->entries.at(entity).evaluationRevision == record->evaluationRevision);
    const auto base = gpu.draw(scene, *assets);
    requirePatches(base, gpu.renderer, true, true);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == 1);
    for (int iteration = 0; iteration < 6; ++iteration) {
        REQUIRE(gpu.draw(scene, *assets, &pose) == base);
        REQUIRE(gpu.draw(scene, *assets) == base);
        REQUIRE(gpu.renderer.editableMeshUploadCount() == 1);
    }
    gpu.renderer.setViewportVisibility({});
    REQUIRE(gpu.draw(scene, *assets, &pose) == base);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == 1);
    const auto cached = pose.geometry;
    std::string error;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    auto result = core::evaluateAnimationPose(*inputs, animation, 49);
    REQUIRE(result.pose);
    pose.numerics = std::make_shared<const core::EvaluatedPose>(std::move(*result.pose));
    pose.identity.frame = 49;
    ++pose.identity.evaluationId;
    REQUIRE(pose.geometry == cached);
    REQUIRE(gpu.draw(scene, *assets, &pose) != base);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == 1);
    REQUIRE(gpu.draw(scene, *assets) == base);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == 1);

    // 真正更换不可变内容后必须观察到一次成功上传，证明计数跟随实际 GPU 调用。
    auto changedMesh = twoPatchMesh();
    for (auto& vertex : changedMesh.vertices)
        vertex.position.x *= 1.1F;
    const auto changed = scene.prepareEditableGeometry(entity, changedMesh, error);
    REQUIRE(changed);
    REQUIRE(scene.installGeometry(*changed));
    const auto changedBase = gpu.draw(scene, *assets);
    REQUIRE(changedBase != base);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == 2);
    const auto changedPose = gpuPose(scene, assets);
    REQUIRE(gpu.draw(scene, *assets, &changedPose) == changedBase);
    REQUIRE(gpu.renderer.editableMeshUploadCount() == 2);
}
