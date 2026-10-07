/*
 * 模块名: ObservationService
 * 功能概述: 协调共享业务前置检查、明确视图控制和实际帧的异步捕获。
 * 对外接口: getState、setView、focus、capture、cancelCapture、invoke。
 * 依赖关系: EditorApiService、ViewportWidget、Qt 回调与单调计时器。
 * 输入输出: 显式 DTO 或严格 JSON 到视图状态、真实 PNG 或统一错误。
 * 异常与错误: 版本/视图冲突、不可用 Context、GPU 失败、超时及取消明确完成。
 * 维护说明: 单应用线程、一个有界 pending；不取消用户交互，不运行嵌套事件循环。
 */
#pragma once

#include "ObservationTypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>

namespace mini3d::editor::api {
class EditorApiService;
}
namespace mini3d::editor::observation {
/** @brief 非拥有业务与视口的协调器；两者必须在同一应用线程装配。 */
class ObservationService final : public QObject {
    Q_OBJECT
  public:
    using CaptureCallback = std::function<void(api::ApiResult<CaptureResult>)>;
    using JsonCallback = std::function<void(QJsonObject)>;
    explicit ObservationService(api::EditorApiService& service,
                                renderer_gl::ViewportWidget& viewport, QObject* parent = nullptr);
    ~ObservationService() override;
    [[nodiscard]] api::ApiResult<ViewState> getState(const ViewRequest& request);
    api::ApiResult<ViewCommandResult> setView(const SetViewRequest& request,
                                              api::BeforeCommitGuard beforeCommit = {});
    api::ApiResult<ViewCommandResult> focus(const FocusRequest& request,
                                            api::BeforeCommitGuard beforeCommit = {});
    /** @brief 成功排队返回 captureId；立即失败调用 callback 并返回空 ID。 */
    QString capture(const CaptureRequest& request, CaptureCallback callback);
    /** @brief 取消仅本服务当前等待中的 ID，立即回调 CANCELLED 并释放所有 pending。 */
    bool cancelCapture(const QString& captureId);
    [[nodiscard]] bool isCapturing() const;
    /** @brief 适配四个观察方法，异步 capture 返回供通信层绑定取消的 captureId。 */
    QString invoke(const QString& method, const QJsonValue& params, JsonCallback callback,
                   api::BeforeCommitGuard beforeCommit = {});

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    struct PendingCapture {
        QString captureId;
        CaptureRequest request;
        CaptureCallback callback;
        std::uint64_t afterFrameId = 0, contextGeneration = 0;
        renderer_gl::PoseIdentity identity;
        std::uint64_t sessionRevision = 0;
        QElapsedTimer elapsed;
    };
    api::ApiError error(api::ErrorCode code, const QString& message, const QString& field = {},
                        api::Recovery recovery = api::Recovery::None) const;
    api::ApiResult<ViewState> checkedView(const api::DocumentHandle& document, bool mutation,
                                          std::optional<std::uint64_t> documentRevision = {},
                                          std::optional<std::uint64_t> viewportRevision = {});
    std::optional<api::ApiError> pendingFailure(const QString& captureId);
    [[nodiscard]] bool isPendingCapture(const QString& captureId) const;
    void tryCapture();
    void finishCapture(const QString& captureId, api::ApiResult<CaptureResult> result);
    api::EditorApiService& service_;
    QPointer<renderer_gl::ViewportWidget> viewport_;
    std::unique_ptr<PendingCapture> pending_;
    QTimer timeout_;
};
} // namespace mini3d::editor::observation
