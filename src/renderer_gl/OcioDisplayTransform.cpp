/*
 * 模块名: OcioDisplayTransform
 * 功能概述: 核验固定配置字节并调用真实 OCIO 2.5 CPU/GPU processor。
 * 对外接口: OcioDisplayTransform.h 中的不可变颜色服务。
 * 依赖关系: OCIO 2.5、QFile、QCryptographicHash、标准数学。
 * 输入输出: 原始 linear RGBA 到显示 RGBA；只允许 RGB 进入 processor。
 * 异常与错误: OCIO 异常和非法输入返回诊断，不清零负值或 HDR。
 * 维护说明: 静态 Qt 颜色资源显式注册；无全局 SetCurrentConfig 或环境回退。
 */
#include "OcioDisplayTransform.h"

#include <OpenColorIO/OpenColorIO.h>
#include <QCryptographicHash>
#include <QFile>
#include <QResource>
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

// Q_INIT_RESOURCE 必须在全局命名空间中引用静态 QRC 的链接符号。
static void initializeMini3dColorResources() {
    Q_INIT_RESOURCE(renderer_color_config);
}

namespace mini3d::renderer_gl {
namespace OCIO = OCIO_NAMESPACE;
namespace {
constexpr char kConfigSha256[] =
    "580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927";

bool isFinitePixel(const glm::vec4& pixel) {
    return std::isfinite(pixel.x) && std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
           std::isfinite(pixel.w) && pixel.w >= 0 && pixel.w <= 1;
}
} // namespace

struct OcioDisplayTransform::Impl {
    OCIO::ConstConfigRcPtr config;
    OCIO::ConstProcessorRcPtr processor;
    OCIO::ConstCPUProcessorRcPtr cpu;
    OCIO::ConstGPUProcessorRcPtr gpu;
    QByteArray shader;
    OcioDisplayIdentity identity;
};

OcioDisplayTransform::OcioDisplayTransform(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation)) {}
OcioDisplayTransform::~OcioDisplayTransform() = default;

std::shared_ptr<const OcioDisplayTransform>
OcioDisplayTransform::createFixedStandard(const QString& configPath, QString& error) {
    error.clear();
    initializeMini3dColorResources();
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取固定 OCIO 配置：%1").arg(file.errorString());
        return nullptr;
    }
    const auto bytes = file.readAll();
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toUpper();
    if (hash != kConfigSha256) {
        error = QStringLiteral("OCIO 配置字节不属于冻结的 Standard 配置。");
        return nullptr;
    }
    if (QString::fromLatin1(OCIO::GetVersion()) != QStringLiteral("2.5.0")) {
        error = QStringLiteral("当前实际 OCIO 库不是冻结的 2.5.0。");
        return nullptr;
    }
    try {
        auto implementation = std::make_unique<Impl>();
        std::istringstream stream(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        implementation->config = OCIO::Config::CreateFromStream(stream);
        implementation->config->validate();
        const auto transform = OCIO::DisplayViewTransform::Create();
        transform->setSrc("Linear Rec.709");
        transform->setDisplay("sRGB");
        transform->setView("Standard");
        transform->setLooksBypass(true);
        transform->setDataBypass(true);
        implementation->processor = implementation->config->getProcessor(transform);
        implementation->cpu = implementation->processor->getDefaultCPUProcessor();
        implementation->gpu = implementation->processor->getDefaultGPUProcessor();
        auto shader = OCIO::GpuShaderDesc::CreateShaderDesc();
        shader->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
        shader->setFunctionName("mini3dOcioDisplay");
        shader->setResourcePrefix("mini3d_ocio_");
        implementation->gpu->extractGpuShaderInfo(shader);
        if (shader->getNumTextures() != 0 || shader->getNum3DTextures() != 0 ||
            shader->getNumUniforms() != 0) {
            error = QStringLiteral("固定 Standard processor 出现未批准的纹理或动态参数。");
            return nullptr;
        }
        implementation->shader = shader->getShaderText();
        if (implementation->shader.isEmpty()) {
            error = QStringLiteral("OCIO 未生成实际 GPU shader。");
            return nullptr;
        }
        implementation->identity.libraryVersion = QString::fromLatin1(OCIO::GetVersion());
        implementation->identity.configSha256 = hash;
        implementation->identity.processorCacheId = implementation->processor->getCacheID();
        implementation->identity.shaderSha256 =
            QCryptographicHash::hash(implementation->shader, QCryptographicHash::Sha256).toHex().toUpper();
        return std::shared_ptr<const OcioDisplayTransform>(
            new OcioDisplayTransform(std::move(implementation)));
    } catch (const OCIO::Exception& exception) {
        error = QStringLiteral("固定 OCIO processor 创建失败：%1")
                    .arg(QString::fromUtf8(exception.what()));
        return nullptr;
    }
}

bool OcioDisplayTransform::applyCpuRgba(std::span<const glm::vec4> input,
                                       std::span<glm::vec4> output, QString& error) const {
    error.clear();
    if (input.size() != output.size() ||
        !std::all_of(input.begin(), input.end(), isFinitePixel)) {
        error = QStringLiteral("OCIO 输入尺寸或 FLOAT32/alpha 数值非法。");
        return false;
    }
    std::vector<glm::vec4> candidate(input.begin(), input.end());
    try {
        for (auto& pixel : candidate) {
            std::array<float, 3> rgb{pixel.x, pixel.y, pixel.z};
            impl_->cpu->applyRGB(rgb.data());
            pixel.x = rgb[0];
            pixel.y = rgb[1];
            pixel.z = rgb[2];
            if (!isFinitePixel(pixel)) {
                error = QStringLiteral("OCIO 输出出现不可消费的 FLOAT32 数值。");
                return false;
            }
        }
    } catch (const OCIO::Exception& exception) {
        error = QStringLiteral("OCIO CPU 变换失败：%1").arg(QString::fromUtf8(exception.what()));
        return false;
    }
    std::copy(candidate.begin(), candidate.end(), output.begin());
    return true;
}

const QByteArray& OcioDisplayTransform::gpuShaderText() const { return impl_->shader; }
const OcioDisplayIdentity& OcioDisplayTransform::identity() const { return impl_->identity; }

} // namespace mini3d::renderer_gl
