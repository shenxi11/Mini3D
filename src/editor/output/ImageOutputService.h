/*
 * 模块名: ImageOutputService
 * 功能概述: 同应用线程编排正式来源冻结、固定 GL 母版、编码验证和新文件提交。
 * 对外接口: ImageOutputService::write、isPreparing、preparingChanged。
 * 依赖关系: SceneViewModel、EditorApiService、ViewportWidget、ImageDocument。
 * 输入输出: 内部显式请求到本次 ImageFrame/新目标或结构化失败。
 * 异常与错误: 取消、期限、CAS、撤权或渲染失败不发布旧帧/部分文件。
 * 维护说明: 无后台线程、持久作业或新增 wire 方法；取消指针须覆盖本次同步调用。
 */
#pragma once

#include "editor/api/ApiTypes.h"
#include "renderer_gl/ImageFrame.h"

#include <QObject>
#include <atomic>

namespace mini3d::renderer_gl { class ViewportWidget; }
namespace mini3d::editor {
class SceneViewModel;
namespace api { class EditorApiService; }

/** @brief 仅内部 C++ 请求；继承当前文档 CAS/期限，任意合法子帧独立输出。 */
struct ImageOutputRequest : api::MutationRequest {
    QString path;
    core::FrameTime frame = 1;
    core::EntityId camera = 0;
    renderer_gl::ImageFileFormat format = renderer_gl::ImageFileFormat::Png;
    const std::atomic_bool* cancelled = nullptr;
};
struct ImageOutputResult {
    QString path;
    std::shared_ptr<const renderer_gl::ImageFrame> frame;
};
/** @brief 非拥有业务编排；三个服务对象及取消标记须在整个同步调用中存活。 */
class ImageOutputService final : public QObject {
    Q_OBJECT
  public:
    ImageOutputService(SceneViewModel& model, api::EditorApiService& api,
                        renderer_gl::ViewportWidget& viewport, QObject* parent = nullptr);
    [[nodiscard]] bool isPreparing() const;
    /** @brief 真实执行固定输出；NewOnly、当前 External 根授权和最后 CAS 合取。 */
    [[nodiscard]] api::ApiResult<ImageOutputResult> write(const ImageOutputRequest& request);
  signals:
    /** @brief 当前同步准备作用域；不表示跨重启 Ready，信号可重入且随后必须重查来源。 */
    void preparingChanged(bool preparing);
  private:
    SceneViewModel& model_;
    api::EditorApiService& api_;
    renderer_gl::ViewportWidget& viewport_;
    core::EvaluationSessionId frameSession_ = core::createEvaluationSessionId();
    std::uint64_t frameId_ = 0;
    bool preparing_ = false;
};
} // namespace mini3d::editor
