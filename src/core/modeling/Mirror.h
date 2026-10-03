/*
 * 模块名: Mirror
 * 功能概述: 单局部轴镜像离线求值与独立的中心边界夹持，不改变源网格或文档。
 * 对外接口: MirrorOptions、MirrorEvaluation、evaluateMirror、MirrorClipSession。
 * 依赖关系: MeshDerivation、VertexTransform、标准容器，无 Qt/GL。
 * 输入输出: 完整源快照/局部参数到合法求值网格、来源映射或受约束的变换候选。
 * 异常与错误: 不合法参数/拓扑/结果返回中文原因；分配异常传播，不发布部分结果。
 * 维护说明: 不提供 Bisect、多轴、任意点焊接或持久修改器栈；应用与界面在上层组合。
 */
#pragma once

#include "MeshDerivation.h"
#include "VertexTransform.h"

#include <map>

namespace mini3d::core::modeling {
enum class MirrorAxis { X, Y, Z };
/** @brief threshold 为局部坐标到原点平面的距离；Merge 与 Clipping 独立开关。 */
struct MirrorOptions {
    MirrorAxis axis = MirrorAxis::X;
    bool enabled = true;
    bool merge = true;
    bool clipping = true;
    double threshold = 0.001;
    [[nodiscard]] bool isValid() const;
    bool operator==(const MirrorOptions&) const = default;
};
/** @brief 求值身份映射回源 ID；镜像面/角不可当作可写源选择，中心共用点除外。 */
struct MirrorElementSource {
    std::uint64_t source = 0;
    bool mirrored = false;
    bool operator==(const MirrorElementSource&) const = default;
};
struct MirrorEvaluation {
    EditableMesh mesh;
    DerivedMesh derived;
    std::map<VertexId, MirrorElementSource> vertices;
    std::map<FaceId, MirrorElementSource> faces;
    std::map<CornerId, MirrorElementSource> corners;
};
struct MirrorResult {
    std::optional<MirrorEvaluation> evaluation;
    std::string error;
};
/** @brief 仅对应的中心边界端点共享身份，近邻部件不焊；结果经完整校验和三角化。
 * 原身份不变，新增身份按源 ID 排序分配。关闭时返回源的恒等求值；输入始终不修改。
 */
[[nodiscard]] MirrorResult evaluateMirror(const EditableMesh& source, const MirrorOptions& options);

/** @brief 一次组件变换的夹持状态；固定 before，捕获仅在合法候选被接受后生效。 */
class MirrorClipSession {
  public:
    /** @brief 冻结局部参数/选区/边界；仅所选边界点可捕获，初始阈值内锁定。
     * 非法源/选择/参数返回空并写 error；关闭镜像或 Clipping 时保留无约束会话。
     */
    [[nodiscard]] static std::optional<MirrorClipSession> begin(const EditableMesh& before,
                                                                const std::set<VertexId>& selected,
                                                                const MirrorOptions& options,
                                                                std::string& error);
    /** @brief 输入由同一 before 得到的原始候选，不能以上一帧结果累积变换。
     * 捕获阈值内或跨过平面的边界点，沿平面可移动；失败不改变已锁定集合。
     * 只接受位置/法线编辑，不改变拓扑；夹持造成形变的面重新计算法线。
     */
    [[nodiscard]] VertexTransformResult constrain(const EditableMesh& candidate);
    /** @brief 比例编辑用当前受影响点集夹持；半径变化仍保留本会话已经捕获的身份。 */
    [[nodiscard]] VertexTransformResult constrain(const EditableMesh& candidate,
                                                  const std::set<VertexId>& affected);
    [[nodiscard]] const std::set<VertexId>& constrainedVertices() const;

  private:
    EditableMesh before_;
    MirrorOptions options_;
    std::set<VertexId> selected_, boundary_, constrained_;
};
} // namespace mini3d::core::modeling
