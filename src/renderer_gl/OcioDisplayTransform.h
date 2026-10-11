/*
 * 模块名: OcioDisplayTransform
 * 功能概述: 从固定自有配置创建唯一 OCIO 2.5 Standard processor，提供 CPU/GPU 同源变换。
 * 对外接口: createFixedStandard、applyCpuRgba、gpuShaderText、identity。
 * 依赖关系: OCIO 2.5、Qt Core、GLM；本模块不调用 OpenGL。
 * 输入输出: scene-linear Linear Rec.709 RGB 到显示 RGB；alpha 按原位旁路。
 * 异常与错误: 配置字节、版本或数值不符返回失败；分配异常传播。
 * 维护说明: 无 Look/LUT/ICC 或全局配置覆盖；GPU 必须编译实际生成的 GLSL。
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <glm/vec4.hpp>
#include <memory>
#include <span>

namespace mini3d::renderer_gl {

/** @brief 记录实际 processor 与 shader 出处；固定参数不是用户可调配置。 */
struct OcioDisplayIdentity {
    QString libraryVersion;
    QByteArray configSha256;
    QByteArray processorCacheId;
    QByteArray shaderSha256;
    QString display = QStringLiteral("sRGB");
    QString view = QStringLiteral("Standard");
    QString look = QStringLiteral("None");
    double exposure = 0;
    double gamma = 1;
    bool operator==(const OcioDisplayIdentity&) const = default;
};

/** @brief 不可变颜色服务；构造成功后 CPU 与 GPU 共用同一实际 OCIO processor。 */
class OcioDisplayTransform final {
  public:
    ~OcioDisplayTransform();
    OcioDisplayTransform(const OcioDisplayTransform&) = delete;
    OcioDisplayTransform& operator=(const OcioDisplayTransform&) = delete;

    /** @brief 仅接受合同固定 SHA 的文件/Qt 资源；失败返回 nullptr，不修改全局 OCIO。 */
    [[nodiscard]] static std::shared_ptr<const OcioDisplayTransform>
    createFixedStandard(const QString& configPath, QString& error);

    /** @brief 只变换 RGB，逐位保留 alpha；失败不发布部分 output，data 不属于此接口。 */
    [[nodiscard]] bool applyCpuRgba(std::span<const glm::vec4> input,
                                    std::span<glm::vec4> output, QString& error) const;
    /** @brief 返回由实际 GPU processor 生成的 mini3dOcioDisplay GLSL 函数。 */
    [[nodiscard]] const QByteArray& gpuShaderText() const;
    /** @brief 返回固定配置、实际库版本、processor cache ID 和 shader hash。 */
    [[nodiscard]] const OcioDisplayIdentity& identity() const;

  private:
    struct Impl;
    explicit OcioDisplayTransform(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> impl_;
};

} // namespace mini3d::renderer_gl
