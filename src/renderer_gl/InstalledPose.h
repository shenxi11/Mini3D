/*
 * 模块名: InstalledPose
 * 功能概述: 持有已安装显示姿态及不可变几何范围，统一渲染与 CPU 查询的数值来源。
 * 对外接口: InstalledPose、makePoseGeometry、validatePoseGeometry、ScenePoseView。
 * 依赖关系: Core、CPU Assets、EditorCamera、Qt Core；不使用 OpenGL。
 * 输入输出: 完整数值姿态与准备期几何快照到范围、刚性设备方向和观察矩阵。
 * 异常与错误: 不完整或不可消费的候选返回空值/false；分配异常传播。
 * 维护说明: 几何仅在准备阶段扫描；身份由调用者核对，预览缺节点不回退基础姿态。
 */
#pragma once

#include "EditorCamera.h"
#include "assets/AssetManager.h"
#include "core/EvaluatedPose.h"
#include "core/EvaluatedSceneState.h"
#include "core/Scene.h"
#include "core/ViewportVisibility.h"

#include <QString>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

namespace mini3d::renderer_gl {

/** @brief 显示状态属于会话，不写入基础对象或场景动画定义。 */
enum class AnimationMode { Base, PreviewPaused, Playing, PoseDraft };

/** @brief 完整显示身份；相等判断不只比较当前帧，版本复核由安装入口完成。 */
struct PoseIdentity {
    QString instanceId;
    QString documentId;
    std::uint64_t sourceRevision = 0;
    std::uint64_t evaluationId = 0;
    core::FrameTime frame = 1;
    AnimationMode mode = AnimationMode::Base;
    bool operator==(const PoseIdentity&) const = default;
};

/** @brief 冻结局部范围、可见性与设备；content/revision 保持真实可编辑 GPU 缓存身份。 */
struct PoseGeometryEntry {
    core::EntityId entity = core::kInvalidEntity;
    core::EntityId parent = core::kInvalidEntity;
    core::Aabb localBounds;
    bool visible = false;
    bool hasGeometry = false;
    std::optional<core::CameraComponent> camera;
    std::optional<core::LightComponent> light;
    std::shared_ptr<const core::EditableMeshContent> content;
    std::uint64_t evaluationRevision = 0;
    core::PrimitiveKind primitive = core::PrimitiveKind::Empty;
    core::MeshId editableMesh = 0;
    core::SurfaceStyle legacySurface;
    std::optional<core::SunComponent> sun;
    std::vector<core::MaterialSlotBinding> materialSlots;
    std::shared_ptr<const core::MeshData> cpuMesh;
    core::Aabb fullLocalBounds;
    std::shared_ptr<const assets::MaterialAsset> legacyMaterial;
    std::shared_ptr<const assets::TextureAsset> legacyTexture;
    bool hasLegacyExternalResources = false;
};

/** @brief 随姿态共享的几何与完整隐藏掩码；消费期间资源和可见性都不读取可变会话值。 */
struct PoseGeometry {
    std::unordered_map<core::EntityId, PoseGeometryEntry> entries;
    std::shared_ptr<const assets::AssetManager> assets;
    core::ViewportVisibility visibility;
    core::DocumentUuid documentUuid;
    std::shared_ptr<const core::SceneMaterialData> materials;
    core::Lighting legacyLighting;
    std::shared_ptr<const core::SceneAuthoringSnapshot> authors;
    std::shared_ptr<const core::SceneGeometrySnapshot> sceneGeometry;
    bool hasCompleteResourceIdentity = false;
};
/** @brief 准备期冻结实际 CPU 拓扑/旧解码资源；不生成稳定外部资产 ID 或 hash。 */
[[nodiscard]] bool freezePoseGeometryEntry(const core::SceneNode& node,
                                          const assets::AssetManager& assets,
                                          PoseGeometryEntry& entry);
/** @brief 完整作者与几何内容一次准备；tick 只复用这些不可变拥有者。 */
[[nodiscard]] bool freezePoseSceneData(PoseGeometry& geometry);

enum class PoseStateCoverage { LegacyTrsV1, CompleteSceneJ1 };
/** @brief 完整性不改变旧数值语义；只有实际 typed 作者进入 Joint 消费域。 */
enum class PoseConsumerDomain { LegacyTrsV1, JointJ1 };

/** @brief 一个不可分割的显示结果：身份、完整数值与冻结几何必须一起安装。 */
struct InstalledPose {
    PoseIdentity identity;
    std::shared_ptr<const core::EvaluatedPose> numerics;
    std::shared_ptr<const PoseGeometry> geometry;
    PoseStateCoverage coverage = PoseStateCoverage::LegacyTrsV1;
    std::optional<core::EvaluatedSceneState> sceneState;
    PoseConsumerDomain consumerDomain = PoseConsumerDomain::LegacyTrsV1;
    std::shared_ptr<const core::EvaluatedPose> sceneNumerics;
    /** @brief 仅候选阶段预分配，安装前绑定实际几何 revision 并清空可写拥有者。 */
    void prepareSceneGeometryBinding();
    void bindPreparedSceneGeometry(const core::Scene& scene);

  private:
    std::shared_ptr<core::EvaluatedSceneContent> sceneContentToBind_;
    std::shared_ptr<core::SceneGeometrySnapshot> sceneGeometryToBind_;
};

/**
 * @brief 准备期扫描一次几何；按 inputs 的父先子后顺序传播集合、对象隐藏与隔离。
 * @return 完整不可变快照；缺节点、资源拥有者或非法输入顺序返回 nullptr。
 * 不修改场景、资源与可见性；播放 tick 必须复用结果。
 */
[[nodiscard]] std::shared_ptr<const PoseGeometry>
makePoseGeometry(const core::Scene& scene, std::shared_ptr<const assets::AssetManager> assets,
                 const core::ViewportVisibility& visibility,
                 std::span<const core::AnimationPoseInput> inputs);

/**
 * @brief 核对完整对应关系和实际 float 八角、全部设备 VP、编辑相机与灯方向。
 * @return 可消费时 true；失败写入 error，不修改或扫描几何顶点。
 */
[[nodiscard]] bool validatePoseGeometry(const core::EvaluatedPose& numerics,
                                         const PoseGeometry& geometry,
                                         const EditorCamera& editorCamera,
                                         core::EntityId previewCamera, QString& error);

/** @brief 刚性设备观察结果；缩放与镜像只影响位置继承，不改变设备方向。 */
struct PoseCameraView {
    glm::vec3 position{0};
    glm::vec3 target{0};
    glm::vec3 forward{0, 0, -1};
    glm::vec3 up{0, 1, 0};
    glm::mat4 viewMatrix{1};
    glm::mat4 projectionMatrix{1};
};

/** @brief 任意合法帧的完整 state 相机消费；复用 checked 几何/设备门，不制造 Frozen InstalledPose。 */
[[nodiscard]] std::optional<PoseCameraView>
evaluatedStateCameraView(const core::EvaluatedSceneState& state, const PoseGeometry& geometry,
                         const EditorCamera& editorCamera, core::EntityId camera, float aspect,
                         QString& error);

/** @brief 只读姿态消费者；无 pose 时用基础场景，有 pose 时缺数值返回空值。 */
class ScenePoseView final {
  public:
    /** @brief 不拥有 scene/pose；调用者须保证整个消费期间对象有效并已验证身份。 */
    explicit ScenePoseView(const core::Scene& scene, const InstalledPose* pose = nullptr);
    /** @brief 整体来源/checked 数值/stage 对应关系通过后才开放任一 Joint 查询。 */
    [[nodiscard]] bool isComplete() const;
    [[nodiscard]] const core::EvaluatedPose* numerics() const;
    [[nodiscard]] std::optional<core::Aabb> localBounds(core::EntityId entity) const;
    /** @brief 查询当前显示来源中的有效可见性；不存在返回 false。 */
    [[nodiscard]] bool isVisible(core::EntityId entity) const;
    /** @brief 查询 world/worldInverse/刚性旋转；缺节点返回空值，不改变输入。 */
    [[nodiscard]] std::optional<glm::mat4> worldMatrix(core::EntityId entity) const;
    [[nodiscard]] std::optional<glm::mat4> worldInverse(core::EntityId entity) const;
    [[nodiscard]] std::optional<glm::quat> worldRotation(core::EntityId entity) const;
    /** @brief 最小 ID 的可见灯生效；不完整姿态返回空值，不修改基础光照。 */
    [[nodiscard]] std::optional<core::Lighting> lighting() const;
    /** @brief 查询可见实体相机的刚性观察矩阵；不存在或实际 float 不可用返回空值。 */
    [[nodiscard]] std::optional<PoseCameraView> cameraView(core::EntityId entity,
                                                          float aspect) const;

  private:
    [[nodiscard]] const PoseGeometryEntry* geometryEntry(core::EntityId entity) const;
    [[nodiscard]] const core::EvaluatedPoseNode* poseNode(core::EntityId entity) const;

    const core::Scene& scene_;
    const InstalledPose* pose_;
    bool complete_ = true;
};

} // namespace mini3d::renderer_gl
