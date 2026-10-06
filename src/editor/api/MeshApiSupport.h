/*
 * 模块名: MeshApiSupport
 * 功能概述: 复用网格目标、源版本与候选预算检查，不发布场景内容。
 * 对外接口: meshTarget、meshIdentity、checkMeshRevisions、checkMeshCandidateBudget。
 * 依赖关系: ApiTypes、Core 不可变源/派生/求值内容。
 * 输入输出: 显式身份或独立候选到只读内容及结构化拒绝。
 * 异常与错误: 参数、版本及预算失败在提交前返回；不解析内核中文提示。
 * 维护说明: 数量与字节限额只取生成的 ApiLimits；无历史或 GUI 偏好。
 */
#pragma once
#include "ApiTypes.h"

namespace mini3d::editor::api {
/** @brief 参数错误与 Schema 共用 -32602，保留当前文档状态。 */
ApiError meshArgumentError(const DocumentState& state, const QString& field,
                           const QString& message);
std::optional<ApiError> checkMeshEnvelope(const MutationRequest& request,
                                         const DocumentState& state);
/** @brief 返回指定对象实际绑定的只读源；不接受其它网格的组件身份。 */
ApiResult<const core::EditableMeshRecord*>
meshTarget(const core::Scene& scene, const DocumentState& state, core::EntityId entity,
           core::MeshId mesh);
/** @brief 复制当前网格身份与单调 revision，不暴露记录地址。 */
MeshIdentity meshIdentity(core::EntityId entity, core::MeshId mesh,
                          const core::EditableMeshRecord& record);
ModifierState modifierState(const core::EditableMeshContent& content);
std::optional<ApiError> checkMeshRevisions(const MeshMutationRequest& request,
                                          const core::EditableMeshRecord& record,
                                          const DocumentState& state);
/** @brief 准备之前限制源及修改器展开上界，避免先分配无界求值候选。 */
std::optional<ApiError>
checkMeshCandidateBudget(const core::modeling::EditableMesh& source,
                         const core::EditableMeshContent* modifiers,
                         const DocumentState& state);
/** @brief 参数变更在求值前检查拟链，不复制已保存的求值网格或映射。 */
std::optional<ApiError>
checkMeshCandidateBudget(const core::modeling::EditableMesh& source,
                         const std::optional<core::modeling::MirrorOptions>& mirror,
                         const std::optional<core::modeling::SubdivisionOptions>& subdivision,
                         const DocumentState& state);
/** @brief 准备后核算真实容量，包含各级源、派生/映射及调用方仍持有的源候选。 */
std::optional<ApiError> checkMeshCandidateBudget(const core::EditableMeshContent& content,
                                                const DocumentState& state,
                                                const core::modeling::EditableMesh* retainedSource = nullptr);
std::size_t meshCornerCount(const core::modeling::EditableMesh& source);
bool isFiniteFloat(double value);
} // namespace mini3d::editor::api
