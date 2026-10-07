/*
 * 模块名: ObservationService
 * 功能概述: 复用共享编辑前置校验，控制实际观察并异步捕获匹配的新帧。
 * 对外接口: ObservationService.h。
 * 依赖关系: EditorApiService、ViewportWidget、Qt 定时器与 PNG 编码。
 * 输入输出: 显式文档和版本到实际视图、抓帧元数据或明确失败。
 * 异常与错误: 文档/视图冲突、Context 不可用、渲染失败、超时或取消均结束等待。
 * 维护说明: 单应用线程，一个 pending；不占用全局 Busy，不运行嵌套事件循环。
 */
#include "ObservationService.h"

#include "ObservationJsonCodec.h"
#include "editor/api/EditorApiService.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QEvent>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <set>

namespace mini3d::editor::observation {
namespace {
std::optional<api::ApiError> mutationError(const api::MutationRequest& request,
                                           const api::DocumentState& state) {
    QString field;
    if (request.timeoutMs < 1 ||
        std::size_t(request.timeoutMs) > api::limits::mutationTimeoutMaximumMs)
        field = QStringLiteral("timeoutMs");
    else if (request.mutationSequence && *request.mutationSequence == 0)
        field = QStringLiteral("mutationSequence");
    else if (request.clientSessionId) {
        static const QRegularExpression uuid(QStringLiteral(
            "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
        if (!uuid.match(*request.clientSessionId).hasMatch())
            field = QStringLiteral("clientSessionId");
    }
    if (!field.isEmpty())
        return api::ApiError{api::ErrorCode::InvalidArgument,
                             QStringLiteral("命令信封参数无效。"),
                             field,
                             api::Recovery::CorrectInput,
                             state,
                             -32602};
    return std::nullopt;
}
bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;
    return true;
}
renderer_gl::FrameDocumentStamp frameStamp(const api::DocumentState& state) {
    return {state.document.instanceId, state.document.documentId, state.documentRevision,
            state.historyRevision};
}
} // namespace

ObservationService::ObservationService(api::EditorApiService& service,
                                       renderer_gl::ViewportWidget& viewport, QObject* parent)
    : QObject(parent), service_(service), viewport_(&viewport) {
    timeout_.setSingleShot(true);
    timeout_.setTimerType(Qt::PreciseTimer);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!pending_)
            return;
        const auto captureId = pending_->captureId;
        const auto failure = pendingFailure(captureId);
        finishCapture(captureId, api::ApiResult<CaptureResult>::failure(failure.value_or(
            error(api::ErrorCode::CaptureTimeout, QStringLiteral("到期未取得匹配的实际帧。"), {},
                  api::Recovery::Wait))));
    });
    // paint 信号只排队；抓 FBO 必须在 paintGL 返回后进行。
    connect(&viewport, &renderer_gl::ViewportWidget::framePainted, this,
            &ObservationService::tryCapture, Qt::QueuedConnection);
    connect(&viewport, &renderer_gl::ViewportWidget::viewportChanged, this,
            &ObservationService::tryCapture, Qt::QueuedConnection);
    connect(&viewport, &renderer_gl::ViewportWidget::frameDisplayStateChanged, this, [this] {
        if (!pending_)
            return;
        const auto captureId = pending_->captureId;
        if (const auto failure = pendingFailure(captureId))
            finishCapture(captureId, api::ApiResult<CaptureResult>::failure(*failure));
    });
    const auto unavailable = [this] {
        if (pending_)
            finishCapture(pending_->captureId, api::ApiResult<CaptureResult>::failure(
                error(api::ErrorCode::ViewportUnavailable, QStringLiteral("视口已不可捕获。"), {},
                      api::Recovery::Wait)));
    };
    connect(&viewport, &renderer_gl::ViewportWidget::observationUnavailable, this, unavailable);
    connect(&viewport, &QObject::destroyed, this, unavailable);
    viewport.installEventFilter(this);
    if (viewport.window() != &viewport)
        viewport.window()->installEventFilter(this);
    service_.setObservationAvailable(true);
}
ObservationService::~ObservationService() {
    service_.setObservationAvailable(false);
    if (pending_)
        finishCapture(pending_->captureId, api::ApiResult<CaptureResult>::failure(
            error(api::ErrorCode::Cancelled, QStringLiteral("观察服务关闭，捕获已取消。"))));
}
api::ApiError ObservationService::error(api::ErrorCode code, const QString& message,
                                        const QString& field, api::Recovery recovery) const {
    return {code,
            message,
            field,
            recovery,
            service_.documentState(),
            code == api::ErrorCode::InvalidArgument ? -32602 : -32010};
}
api::ApiResult<ViewState>
ObservationService::checkedView(const api::DocumentHandle& document, bool mutation,
                                std::optional<std::uint64_t> documentRevision,
                                std::optional<std::uint64_t> viewportRevision) {
    using Result = api::ApiResult<ViewState>;
    // 版本冲突先于 Busy，使等待期间的真实变化具有明确的重查结果。
    if (const auto failure = service_.validateContext(document, false, false, documentRevision))
        return Result::failure(*failure);
    if (!viewport_ || QThread::currentThread() != viewport_->thread() ||
        !viewport_->isObservationAvailable())
        return Result::failure(error(api::ErrorCode::ViewportUnavailable,
                                     QStringLiteral("视口或 OpenGL Context 当前不可用。"), {},
                                     api::Recovery::Wait));
    const auto state = viewport_->observationState();
    if (!state || state->document != frameStamp(service_.documentState()))
        return Result::failure(error(api::ErrorCode::ViewportUnavailable,
                                     QStringLiteral("实际绘制文档版本尚未正确装配。"), {},
                                     api::Recovery::Wait));
    if (viewportRevision && state->viewportRevision != *viewportRevision)
        return Result::failure(
            error(api::ErrorCode::ViewChanged, QStringLiteral("实际视图已变化，请重新查询。"),
                  QStringLiteral("expectedViewportRevision"), api::Recovery::Refetch));
    if (const auto failure = service_.validateContext(document, mutation, true, documentRevision))
        return Result::failure(*failure);
    if (viewport_->isNavigationActive())
        return Result::failure(error(api::ErrorCode::Busy, QStringLiteral("请先结束视图导航。"), {},
                                     api::Recovery::Wait));
    if (mutation && viewport_->isPreviewingCamera())
        return Result::failure(error(api::ErrorCode::Busy, QStringLiteral("请先返回编辑视图。"), {},
                                     api::Recovery::Wait));
    return Result::success({*state});
}
api::ApiResult<ViewState> ObservationService::getState(const ViewRequest& request) {
    return checkedView(request.document, false);
}
api::ApiResult<ViewCommandResult> ObservationService::setView(const SetViewRequest& request,
                                                              api::BeforeCommitGuard beforeCommit) {
    using Result = api::ApiResult<ViewCommandResult>;
    const auto before = checkedView(request.document, true, request.expectedDocumentRevision,
                                    request.expectedViewportRevision);
    if (before.error)
        return Result::failure(*before.error);
    if (const auto failure = mutationError(request, service_.documentState()))
        return Result::failure(*failure);
    const auto& changes = request.changes;
    if (!changes.camera && !changes.preset && !changes.orthographic && !changes.shading &&
        !changes.overlays && !changes.xRay)
        return Result::failure(
            error(api::ErrorCode::InvalidArgument, QStringLiteral("至少提供一个显示更新字段。")));
    if ((changes.preset && (int(*changes.preset) < int(renderer_gl::EditorView::Orbit) ||
                            int(*changes.preset) > int(renderer_gl::EditorView::Bottom))) ||
        (changes.camera && changes.preset && *changes.preset != renderer_gl::EditorView::Orbit))
        return Result::failure(error(api::ErrorCode::InvalidArgument,
                                     QStringLiteral("观察方向无效或与自定义相机冲突。"), "preset"));
    if (changes.shading && (int(*changes.shading) < int(renderer_gl::ViewportShading::Material) ||
                            int(*changes.shading) > int(renderer_gl::ViewportShading::Wireframe)))
        return Result::failure(
            error(api::ErrorCode::InvalidArgument, QStringLiteral("着色模式无效。"), "shading"));
    auto candidate = *viewport_->editorCameraSnapshot();
    if (changes.camera &&
        (changes.camera->maximumDistance < .6F || !candidate.setState(*changes.camera)))
        return Result::failure(error(api::ErrorCode::InvalidArgument,
                                     QStringLiteral("相机状态不满足现有观察约束。"), "camera"));
    if (changes.preset)
        candidate.setView(*changes.preset);
    candidate.setOrthographic(
        changes.orthographic.value_or(before.value->viewport.view.orthographic));
    if (!finiteMatrix(candidate.viewMatrix()) || !finiteMatrix(candidate.projectionMatrix()))
        return Result::failure(error(api::ErrorCode::InvalidArgument,
                                     QStringLiteral("相机无法生成有限的观察矩阵。"), "camera"));
    std::optional<api::ApiError> rejection;
    const bool changed = viewport_->applyViewUpdate(changes, [&] {
        if (beforeCommit)
            rejection = beforeCommit();
        return !rejection;
    });
    if (rejection)
        return Result::failure(*rejection);
    const auto after = checkedView(request.document, false);
    if (after.error)
        return Result::failure(*after.error);
    return Result::success(
        {changed ? api::ResultStatus::Committed : api::ResultStatus::NoChange, *after.value});
}
api::ApiResult<ViewCommandResult> ObservationService::focus(const FocusRequest& request,
                                                            api::BeforeCommitGuard beforeCommit) {
    using Result = api::ApiResult<ViewCommandResult>;
    const auto before = checkedView(request.document, true, request.expectedDocumentRevision,
                                    request.expectedViewportRevision);
    if (before.error)
        return Result::failure(*before.error);
    if (const auto failure = mutationError(request, service_.documentState()))
        return Result::failure(*failure);
    if (request.entityIds.empty())
        return Result::failure(error(api::ErrorCode::InvalidArgument,
                                     QStringLiteral("须明确提供非空实体数组。"), "entityIds"));
    if (request.entityIds.size() > api::limits::batchItems)
        return Result::failure(error(api::ErrorCode::LimitExceeded,
                                     QStringLiteral("框景实体数量超出限额。"), "entityIds"));
    std::set<core::EntityId> seen;
    for (const auto id : request.entityIds) {
        if (id == 0 || !seen.insert(id).second)
            return Result::failure(error(api::ErrorCode::InvalidArgument,
                                         QStringLiteral("实体 ID 须非零且不重复。"), "entityIds"));
        if (!viewport_->hasEntity(id))
            return Result::failure(error(api::ErrorCode::NotFound,
                                         QStringLiteral("框景实体不存在，未改变观察。"),
                                         "entityIds", api::Recovery::Refetch));
    }
    std::optional<api::ApiError> rejection;
    const bool focused = viewport_->focusEntities(request.entityIds, [&] {
        if (beforeCommit)
            rejection = beforeCommit();
        return !rejection;
    });
    if (rejection)
        return Result::failure(*rejection);
    if (!focused)
        return Result::failure(error(api::ErrorCode::UnsupportedOperation,
                                     QStringLiteral("目标无法生成有效有限的框景，请检查可见几何和坐标范围。"),
                                     "entityIds"));
    const auto after = checkedView(request.document, false);
    if (after.error)
        return Result::failure(*after.error);
    const bool changed =
        after.value->viewport.viewportRevision != before.value->viewport.viewportRevision;
    return Result::success(
        {changed ? api::ResultStatus::Committed : api::ResultStatus::NoChange, *after.value});
}

QString ObservationService::capture(const CaptureRequest& request, CaptureCallback callback) {
    const auto reject = [&](api::ApiError failure) {
        callback(api::ApiResult<CaptureResult>::failure(std::move(failure)));
        return QString{};
    };
    const auto view = checkedView(request.document, false, request.expectedDocumentRevision,
                                  request.expectedViewportRevision);
    if (view.error)
        return reject(*view.error);
    const auto& state = view.value->viewport;
    if (state.animationMode == renderer_gl::AnimationMode::Playing ||
        state.animationMode == renderer_gl::AnimationMode::PoseDraft)
        return reject(error(api::ErrorCode::Busy,
                            QStringLiteral("播放或姿态草稿期间不能捕获指定帧。"), {},
                            api::Recovery::Wait));
    if (!state.animation || state.animation->evaluationId != request.expectedEvaluationId)
        return reject(error(api::ErrorCode::StaleEvaluation,
                            QStringLiteral("显示求值身份已变化，请重新查询。"),
                            "expectedEvaluationId", api::Recovery::Refetch));
    if (request.longestEdge < 1 ||
        std::size_t(request.longestEdge) > api::limits::captureLongestEdge)
        return reject(error(api::ErrorCode::InvalidArgument,
                            QStringLiteral("输出最长边超出允许范围。"), "longestEdge"));
    if (request.timeoutMs < 1 ||
        std::size_t(request.timeoutMs) > api::limits::captureTimeoutMaximumMs)
        return reject(error(api::ErrorCode::InvalidArgument,
                            QStringLiteral("捕获超时参数超出允许范围。"), "timeoutMs"));
    if (pending_)
        return reject(error(api::ErrorCode::Busy, QStringLiteral("已有一个捕获正在等待。"), {},
                            api::Recovery::Wait));
    auto pending = std::make_unique<PendingCapture>();
    pending->captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending->request = request;
    pending->callback = std::move(callback);
    pending->identity = *state.animation;
    pending->sessionRevision = state.sessionRevision;
    if (viewport_->lastRenderedFrame())
        pending->afterFrameId = viewport_->lastRenderedFrame()->frameId;
    pending->contextGeneration = viewport_->contextGeneration();
    pending->elapsed.start();
    const auto captureId = pending->captureId;
    pending_ = std::move(pending);
    timeout_.start(request.timeoutMs);
    viewport_->update();
    return captureId;
}
bool ObservationService::cancelCapture(const QString& captureId) {
    if (QThread::currentThread() != thread() || !pending_ || pending_->captureId != captureId)
        return false;
    finishCapture(captureId, api::ApiResult<CaptureResult>::failure(
        error(api::ErrorCode::Cancelled, QStringLiteral("捕获已取消。"))));
    return true;
}
bool ObservationService::isCapturing() const {
    return pending_ != nullptr;
}
bool ObservationService::isPendingCapture(const QString& captureId) const {
    return pending_ && pending_->captureId == captureId;
}
std::optional<api::ApiError> ObservationService::pendingFailure(const QString& captureId) {
    if (!isPendingCapture(captureId))
        return std::nullopt;
    // 查询可同步通知并完成旧请求；只保留这次请求的值，不跨重入持有 pending 引用。
    const auto request = pending_->request;
    const auto identity = pending_->identity;
    const auto sessionRevision = pending_->sessionRevision;
    const auto contextGeneration = pending_->contextGeneration;
    const auto view = checkedView(request.document, false, request.expectedDocumentRevision,
                                  request.expectedViewportRevision);
    if (view.error)
        return view.error;
    if (view.value->viewport.animation != identity ||
        view.value->viewport.sessionRevision != sessionRevision)
        return error(api::ErrorCode::StaleEvaluation,
                     QStringLiteral("显示姿态或动画会话已变化，原捕获已失效。"),
                     "expectedEvaluationId", api::Recovery::Refetch);
    if (viewport_->contextGeneration() != contextGeneration)
        return error(api::ErrorCode::ViewportUnavailable,
                     QStringLiteral("OpenGL Context 已重建，原捕获已失效。"), {},
                     api::Recovery::Wait);
    if (!isPendingCapture(captureId))
        return std::nullopt;
    if (pending_->elapsed.elapsed() >= request.timeoutMs)
        return error(api::ErrorCode::CaptureTimeout, QStringLiteral("捕获已超过独立超时。"), {},
                     api::Recovery::Wait);
    return std::nullopt;
}
void ObservationService::tryCapture() {
    using Result = api::ApiResult<CaptureResult>;
    if (!pending_)
        return;
    const auto captureId = pending_->captureId;
    const auto finish = [this, &captureId](Result result) {
        finishCapture(captureId, std::move(result));
    };
    if (const auto failure = pendingFailure(captureId)) {
        finish(Result::failure(*failure));
        return;
    }
    if (!isPendingCapture(captureId))
        return;
    const auto& previous = viewport_->lastRenderedFrame();
    if (!previous || previous->frameId <= pending_->afterFrameId)
        return;
    auto captured = viewport_->grabStampedFramebuffer();
    if (!isPendingCapture(captureId))
        return;
    if (!captured) {
        finish(
            Result::failure(error(api::ErrorCode::ViewportUnavailable,
                                  QStringLiteral("实际帧读取不可用。"), {}, api::Recovery::Wait)));
        return;
    }
    // grabFramebuffer 可再绘制；此处只使用抓取之后的真实输入和资源状态。
    if (const auto failure = pendingFailure(captureId)) {
        finish(Result::failure(*failure));
        return;
    }
    if (!isPendingCapture(captureId))
        return;
    const auto& frame = captured->frame;
    if (frame.contextGeneration != pending_->contextGeneration) {
        finish(Result::failure(error(api::ErrorCode::ViewportUnavailable,
                                            QStringLiteral("抓取帧的 OpenGL Context 已失效。"), {},
                                            api::Recovery::Wait)));
        return;
    }
    if (frame.frameId <= pending_->afterFrameId ||
        frame.state.document != frameStamp(service_.documentState())) {
        finish(Result::failure(error(api::ErrorCode::RevisionConflict,
                                            QStringLiteral("抓取实际帧的文档身份或版本不匹配。"),
                                            {}, api::Recovery::Refetch)));
        return;
    }
    if (frame.state.viewportRevision != pending_->request.expectedViewportRevision) {
        finish(Result::failure(error(api::ErrorCode::ViewChanged,
                                            QStringLiteral("抓取实际帧的观察版本不匹配。"), {},
                                            api::Recovery::Refetch)));
        return;
    }
    if (frame.state.animation != pending_->identity ||
        frame.state.animationMode != pending_->identity.mode ||
        frame.state.sessionRevision != pending_->sessionRevision) {
        finish(Result::failure(error(api::ErrorCode::StaleEvaluation,
                                     QStringLiteral("抓取实际帧的求值或会话身份不匹配。"),
                                     "expectedEvaluationId", api::Recovery::Refetch)));
        return;
    }
    if (!frame.resources.ready) {
        finish(Result::failure(error(api::ErrorCode::RenderFailed, frame.resources.error)));
        return;
    }
    if (captured->image.isNull() || captured->image.size() != frame.state.pixelSize) {
        finish(Result::failure(error(api::ErrorCode::RenderFailed,
                                            QStringLiteral("抓取图像与实际绘制像素尺寸不一致。"))));
        return;
    }
    CaptureResult result;
    result.captureId = pending_->captureId;
    result.frameId = frame.frameId;
    result.contextGeneration = frame.contextGeneration;
    result.view = {frame.state};
    result.originalPixelSize = captured->image.size();
    auto image = std::move(captured->image);
    const auto edge = pending_->request.longestEdge;
    if (std::max(image.width(), image.height()) > edge)
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    result.outputPixelSize = image.size();
    if (std::uint64_t(image.width()) * std::uint64_t(image.height()) > api::limits::capturePixels) {
        finish(Result::failure(
            error(api::ErrorCode::LimitExceeded, QStringLiteral("输出图像像素数量超出限额。"))));
        return;
    }
    QBuffer buffer(&result.png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        finish(Result::failure(
            error(api::ErrorCode::RenderFailed, QStringLiteral("实际图像 PNG 编码失败。"))));
        return;
    }
    if (std::size_t(result.png.size()) > api::limits::capturePngBytes) {
        finish(Result::failure(
            error(api::ErrorCode::LimitExceeded, QStringLiteral("PNG 字节数量超出限额。"))));
        return;
    }
    result.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(result.png, QCryptographicHash::Sha256).toHex());
    result.overlayIncluded = frame.state.overlays && frame.state.view.previewCamera == 0;
    if (const auto failure = pendingFailure(captureId)) {
        finish(Result::failure(*failure));
        return;
    }
    finish(Result::success(std::move(result)));
}
void ObservationService::finishCapture(const QString& captureId, api::ApiResult<CaptureResult> result) {
    if (!isPendingCapture(captureId))
        return;
    timeout_.stop();
    auto completed = std::move(pending_);
    completed->callback(std::move(result));
}
bool ObservationService::eventFilter(QObject* object, QEvent* event) {
    if (pending_ && (event->type() == QEvent::Hide || event->type() == QEvent::Close ||
                     (event->type() == QEvent::WindowStateChange && viewport_ &&
                      viewport_->window()->isMinimized())))
        finishCapture(pending_->captureId, api::ApiResult<CaptureResult>::failure(
            error(api::ErrorCode::ViewportUnavailable, QStringLiteral("窗口当前不可捕获。"), {},
                  api::Recovery::Wait)));
    return QObject::eventFilter(object, event);
}
QString ObservationService::invoke(const QString& method, const QJsonValue& params,
                                   JsonCallback callback, api::BeforeCommitGuard beforeCommit) {
    const auto decoded = ObservationJsonCodec::decodeRequest(method, params);
    if (decoded.error) {
        auto failure = *decoded.error;
        failure.state = service_.documentState();
        callback(QJsonObject{{"error", api::ApiJsonCodec::encodeError(failure)}});
        return {};
    }
    const auto& request = *decoded.value;
    if (method == "viewport.capture")
        return capture(std::get<CaptureRequest>(request),
                       [callback = std::move(callback)](auto result) {
                           callback(ObservationJsonCodec::response(result));
                       });
    if (method == "viewport.getState")
        callback(ObservationJsonCodec::response(getState(std::get<ViewRequest>(request))));
    else if (method == "viewport.setView")
        callback(ObservationJsonCodec::response(
            setView(std::get<SetViewRequest>(request), std::move(beforeCommit))));
    else
        callback(ObservationJsonCodec::response(
            focus(std::get<FocusRequest>(request), std::move(beforeCommit))));
    return {};
}
} // namespace mini3d::editor::observation
