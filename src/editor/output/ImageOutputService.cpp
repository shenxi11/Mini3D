/*
 * 模块名: ImageOutputService
 * 功能概述: 以一次同步作用域持有真实母版，在关闭文件验证后执行最后原子发布门。
 * 对外接口: ImageOutputService.h。
 * 依赖关系: 现有应用线程/CAS/文件策略、实际 Viewport GL、固定 OCIO 和独立解码。
 * 输入输出: 请求精确帧到新文件及母版；失败提供当前文档状态和可定位错误。
 * 异常与错误: 任一门拒绝只释放本次私有数据/暂存，不修改作者或安装画面。
 * 维护说明: 最终 guard 的所有可能重入回调先运行，随后只做当前状态纯检查。
 */
#include "ImageOutputService.h"

#include "assets/ImageDocument.h"
#include "editor/SceneViewModel.h"
#include "editor/api/EditorApiService.h"
#include "editor/api/MeshApiSupport.h"
#include "renderer_gl/ImageFrameOutput.h"
#include "renderer_gl/ViewportWidget.h"

#include <QDeadlineTimer>
#include <QFileInfo>
#include <QScopeGuard>
#include <QStorageInfo>
#include <QThread>
#include <limits>
#include <new>

namespace mini3d::editor {
ImageOutputService::ImageOutputService(SceneViewModel& model, api::EditorApiService& api,
                                       renderer_gl::ViewportWidget& viewport, QObject* parent)
    : QObject(parent), model_(model), api_(api), viewport_(viewport) {}
bool ImageOutputService::isPreparing() const { return preparing_; }

api::ApiResult<ImageOutputResult> ImageOutputService::write(const ImageOutputRequest& request) {
    using Result = api::ApiResult<ImageOutputResult>;
    const auto failure = [&](api::ErrorCode code, const QString& message) {
        return api::ApiError{code, message, {}, api::Recovery::None, api_.documentState()};
    };
    if (QThread::currentThread() != model_.thread() || model_.thread() != viewport_.thread() ||
        QThread::currentThread() != thread())
        return Result::failure(failure(api::ErrorCode::Internal, QStringLiteral("图像输出必须在所属应用线程执行。")));
    if (preparing_)
        return Result::failure(failure(api::ErrorCode::Busy, QStringLiteral("当前同步图像输出正在准备。")));
    if (const auto invalid = api::checkMeshEnvelope(request, api_.documentState()))
        return Result::failure(*invalid);
    if (request.format != renderer_gl::ImageFileFormat::Png && request.format != renderer_gl::ImageFileFormat::Exr)
        return Result::failure(failure(api::ErrorCode::InvalidArgument, QStringLiteral("输出只支持 PNG 或 FLOAT32 EXR。")));
    if (QFileInfo(request.path).suffix().toLower() !=
        (request.format == renderer_gl::ImageFileFormat::Png ? QStringLiteral("png") : QStringLiteral("exr")))
        return Result::failure(failure(api::ErrorCode::InvalidArgument, QStringLiteral("输出扩展名须与显式图像格式一致。")));
    if (frameId_ == std::numeric_limits<std::uint64_t>::max())
        return Result::failure(failure(api::ErrorCode::LimitExceeded, QStringLiteral("输出母版帧 ID 已耗尽，不能复用。")));
    QDeadlineTimer deadline(request.timeoutMs);
    const auto interruption = [&]() -> std::optional<api::ApiError> {
        if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
            return failure(api::ErrorCode::Cancelled, QStringLiteral("本次同步图像输出已取消。"));
        if (deadline.hasExpired())
            return failure(api::ErrorCode::DeadlineExceeded, QStringLiteral("本次图像输出已经到期。"));
        return std::nullopt;
    };
    if (const auto denied = interruption()) return Result::failure(*denied);
    try {
        preparing_ = true;
        auto finish = qScopeGuard([&] {
            preparing_ = false;
            emit preparingChanged(false);
        });
        emit preparingChanged(true);
        if (const auto denied = interruption()) return Result::failure(*denied);
        if (const auto denied = api_.validateContext(request.document, false, true,
                                                     request.expectedDocumentRevision))
            return Result::failure(*denied);
        const auto initialAccess = api_.prepareImageOutputAccess(request.path);
        if (!initialAccess.hasValue()) return Result::failure(*initialAccess.error);
        if (const auto denied = api_.confirmImageOutputAccess(*initialAccess.value, request.document,
                                                              request.expectedDocumentRevision))
            return Result::failure(*denied);
        const QStorageInfo storage(QFileInfo(request.path).absolutePath());
        if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable() < qint64(2) * 1024 * 1024 * 1024)
            return Result::failure(failure(api::ErrorCode::LimitExceeded,
                QStringLiteral("输出卷需至少 2 GiB 可用空间，未创建暂存。")));
        auto frozen = model_.freezeImageOutput(request.frame, request.camera);
        if (!frozen.hasValue()) return Result::failure(*frozen.error);
        const auto view = viewport_.imageOutputViewStamp();
        if (!view) return Result::failure(failure(api::ErrorCode::ViewportUnavailable,
            QStringLiteral("所属 GL 视口不可消费本次输出。")));
        if (const auto denied = interruption()) return Result::failure(*denied);
        if (const auto denied = model_.validateFrozenImageOutput(**frozen.value)) return Result::failure(*denied);
        QString error;
        const auto frame = viewport_.renderImageOutput((*frozen.value)->state(), (*frozen.value)->originSource(),
            (*frozen.value)->camera(), (*frozen.value)->cameraEntity(), frameSession_, ++frameId_, *view, error);
        if (!frame) return Result::failure(failure(api::ErrorCode::RenderFailed, error));
        if (const auto denied = interruption()) return Result::failure(*denied);
        const auto color = viewport_.imageOutputColor();
        if (!color) return Result::failure(failure(api::ErrorCode::RenderFailed,
            QStringLiteral("本次输出缺少实际固定 OCIO processor。")));
        const auto encoded = renderer_gl::ImageFrameOutput::encode(*frame, request.format, *color, error);
        if (!encoded) return Result::failure(failure(api::ErrorCode::RenderFailed, error));
        if (const auto denied = interruption()) return Result::failure(*denied);
        std::optional<api::ApiError> finalFailure;
        const auto finalGuard = [&] {
            // 先运行可能重入的 provider/guard/当前路径策略；后段不执行 callback 或事件。
            if (const auto denied = model_.validateFrozenImageOutput(**frozen.value)) {
                finalFailure = denied;
                return false;
            }
            const auto access = api_.prepareImageOutputAccess(request.path);
            if (!access.hasValue()) {
                finalFailure = access.error;
                return false;
            }
            if (const auto denied = interruption()) finalFailure = denied;
            else if (const auto denied = api_.confirmImageOutputAccess(*access.value, request.document,
                                                                        request.expectedDocumentRevision))
                finalFailure = denied;
            else if (const auto denied = model_.validateFrozenImageOutput(**frozen.value, false))
                finalFailure = denied;
            else if (!preparing_ || !viewport_.matchesImageOutputViewStamp(*view))
                finalFailure = failure(api::ErrorCode::ViewChanged,
                    QStringLiteral("最后提交门的实际视图/context 或同步准备凭据已变化。"));
            return !finalFailure;
        };
        bool overwriteDenied = false;
        const bool saved = assets::ImageDocument::write(request.path, encoded->bytes, error,
            [&](const QString& temporaryPath, QString& diagnostic) {
                return renderer_gl::ImageFrameOutput::validateFile(temporaryPath, *frame, *encoded, diagnostic);
            }, finalGuard, &overwriteDenied);
        if (finalFailure) return Result::failure(*finalFailure);
        if (!saved) return Result::failure(failure(overwriteDenied ? api::ErrorCode::OverwriteDenied
                                                                 : api::ErrorCode::IoError, error));
        return Result::success({request.path, frame});
    } catch (const std::bad_alloc&) {
        return Result::failure(failure(api::ErrorCode::LimitExceeded,
            QStringLiteral("图像输出内存不足，未发布本次结果。")));
    }
}
} // namespace mini3d::editor
