/*
 * 模块名: ApiDocumentState
 * 功能概述: 在实际提交边界维护应用/文档身份及两个单调版本。
 * 对外接口: ApiDocumentState::state、recordCommit、resetDocument。
 * 依赖关系: ApiTypes、Qt UUID；不监听预览或通用场景刷新信号。
 * 输入输出: 内容/历史提交类别到当前文档状态。
 * 异常与错误: 不处理业务失败，只有业务确认成功才调用版本入口。
 * 维护说明: 一进程一个 instanceId；新文档从版本 1 开始。
 */
#pragma once

#include "ApiTypes.h"

namespace mini3d::editor::api {
/** @brief SceneViewModel 独占的运行期状态，不维护第二份场景或历史。 */
class ApiDocumentState final {
  public:
    ApiDocumentState();
    [[nodiscard]] const DocumentState& state() const;
    /** @brief 成功提交后推进指定版本；查询、失败和预览不调用。 */
    void recordCommit(bool contentChanged, bool historyChanged);
    /** @brief 成功新建/打开后更新文档身份，应用身份不变。 */
    void resetDocument();

  private:
    DocumentState state_;
};
} // namespace mini3d::editor::api
