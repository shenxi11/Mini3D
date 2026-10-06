/*
 * 模块名: ObservationTypes
 * 功能概述: 定义真实观察与截图的强类型值，不持有场景或 GPU 资源。
 * 对外接口: ViewRequest、SetViewRequest、FocusRequest、CaptureRequest 及结果。
 * 依赖关系: ApiTypes、ViewportWidget 的渲染输入快照。
 * 输入输出: 显式文档/版本与显示参数到实际视图和有界 PNG。
 * 异常与错误: 使用统一 ApiResult；图像只在 JSON 边界转 base64。
 * 维护说明: 保持 api/schema/m3.schema.json 字段语义，矩阵为列主序。
 */
#pragma once

#include "editor/api/ApiTypes.h"
#include "renderer_gl/ViewportWidget.h"

#include <QByteArray>
#include <variant>

namespace mini3d::editor::observation {
using ViewRequest = api::DocumentRequest;
/** @brief 一次显示更新的完整候选，所有字段先验证后安装。 */
struct SetViewRequest : api::MutationRequest {
    std::uint64_t expectedViewportRevision = 0;
    renderer_gl::ViewUpdate changes;
};
/** @brief 明确可见实体/子树的并集框景，不读取当前选区。 */
struct FocusRequest : api::MutationRequest {
    std::uint64_t expectedViewportRevision = 0;
    std::vector<core::EntityId> entityIds;
};
/** @brief 捕获只精确匹配两类版本，具有独立的单调超时。 */
struct CaptureRequest : api::DocumentRequest {
    std::uint64_t expectedDocumentRevision = 0, expectedViewportRevision = 0;
    int longestEdge = int(api::limits::captureLongestEdge);
    int timeoutMs = int(api::limits::captureTimeoutMs);
};
struct ViewState {
    renderer_gl::ViewportState viewport;
};
struct ViewCommandResult {
    api::ResultStatus status = api::ResultStatus::NoChange;
    ViewState view;
};
/** @brief 元数据来自实际抓取之后的 paint，png 是有界原始编码字节。 */
struct CaptureResult {
    QString captureId;
    std::uint64_t frameId = 0, contextGeneration = 0;
    ViewState view;
    QSize originalPixelSize, outputPixelSize;
    QString sha256;
    bool overlayIncluded = false;
    QByteArray png;
};
using ObservationRequest = std::variant<ViewRequest, SetViewRequest, FocusRequest, CaptureRequest>;
} // namespace mini3d::editor::observation
