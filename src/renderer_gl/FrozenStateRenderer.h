/*
 * 模块名: FrozenStateRenderer
 * 功能概述: 只消费完整冻结状态，绘制 J1 OPAQUE PBR 浮点母版并以固定 OCIO 显示。
 * 对外接口: initialize、render、present、destroy；原始像素出口不是正式 ImageFrame。
 * 依赖关系: EvaluatedSceneState、GpuMesh、OCIO 服务、OpenGL 4.1 Core。
 * 输入输出: 完整 IR/显式相机/尺寸到 linear associated Beauty 与独立 coverage。
 * 异常与错误: 不支持的材质/资源、context 或数值错误整帧拒绝，不回退基础值。
 * 维护说明: 所有 GL 对象仅在创建 context 和线程中消费、释放；不含 UI 覆盖层。
 */
#pragma once

#include "OcioDisplayTransform.h"
#include "core/EvaluatedSceneState.h"

#include <QString>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <vector>

class QOpenGLFunctions_4_1_Core;

namespace mini3d::renderer_gl {

/** @brief 实际消费的观察矩阵；来源相机及正式设置身份由联合接线/输出票绑定。 */
struct FrozenRenderCamera {
    glm::dvec3 position{0, 0, 6};
    glm::dmat4 viewMatrix{1};
    glm::dmat4 projectionMatrix{1};
    bool operator==(const FrozenRenderCamera&) const = default;
};

/** @brief 真正 GL 读回的模块母版；TopDown、FLOAT32、associated、instant，无显示编码。 */
struct FrozenRenderPixels {
    core::SourceIdentity source;
    std::vector<core::GeometryStageIdentity> geometryStages;
    FrozenRenderCamera camera;
    OcioDisplayIdentity color;
    QString backendBuild;
    int width = 0;
    int height = 0;
    std::vector<glm::vec4> beauty;
    std::vector<float> coverage;
};

/** @brief 渲染模块明确拒绝未支持生产者，不提供 legacy/白材质替代路径。 */
class FrozenStateRenderer final {
  public:
    FrozenStateRenderer();
    ~FrozenStateRenderer();
    FrozenStateRenderer(const FrozenStateRenderer&) = delete;
    FrozenStateRenderer& operator=(const FrozenStateRenderer&) = delete;

    /** @brief 在当前实际 GL4.1 core context 建立资源并记住所属线程；失败写 error。 */
    [[nodiscard]] bool initialize(QOpenGLFunctions_4_1_Core& gl,
                                  std::shared_ptr<const OcioDisplayTransform> color,
                                  QString& error);
    /** @brief 整体校验并真实绘制/读回完整 IR；失败返回空值，无旧图发布。 */
    [[nodiscard]] std::optional<FrozenRenderPixels>
    render(const core::EvaluatedSceneState& state, const FrozenRenderCamera& camera,
           int width, int height, QString& error);
    /** @brief 在 linear 域预合黑背景后执行一次 OCIO；尺寸取 pixels，恢复所触及 GL 状态。 */
    [[nodiscard]] bool present(const FrozenRenderPixels& pixels,
                               unsigned int destinationFramebuffer, QString& error);
    /** @brief 必须在所属 context/thread 释放所有资源；不在错误 context 调旧 GpuMesh 清理。 */
    void destroy();
    /** @brief 纯诊断：本实例成功完成的 GL 母版帧数，不是 SourceIdentity 或 ImageFrame ID。 */
    [[nodiscard]] std::uint64_t completedRenderCount() const;
    /** @brief 纯诊断：是否仍持有已成功初始化的 GL 资源。 */
    [[nodiscard]] bool isInitialized() const;
    /** @brief 仅同 context 的删除、glIs 查询和真实 fence 均成功才为 true。 */
    [[nodiscard]] bool hasVerifiedRelease() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mini3d::renderer_gl
