/*
 * 模块名: FrozenStateRenderer
 * 功能概述: 真实 GL4.1 OPAQUE PBR、多 Sun 加和、浮点母版及一次 OCIO 显示。
 * 对外接口: FrozenStateRenderer.h 的模块原始像素出口。
 * 依赖关系: GpuMesh、固定 OCIO 服务、Qt Context/Thread、OpenGL4.1。
 * 输入输出: 同源完整 IR 到 TopDown FLOAT32 Beauty/coverage；不发布文件或 UI 覆盖层。
 * 异常与错误: 完整性、支持范围、设备数值和 GL 失败整帧拒绝；分配异常传播。
 * 维护说明: 每次调用恢复实际触及的 GL 状态；所有 GPU 生命周期在同 context/thread。
 */
#include "FrozenStateRenderer.h"

#include "GpuMesh.h"
#include "core/Scene.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <limits>

namespace mini3d::renderer_gl {
namespace {
constexpr char kBackendBuild[] =
    "NativeGL41-Lambert-GGX-height-correlated-Smith-Schlick-alpha-floor-0.001-v1";
constexpr std::uint64_t kGpuBudget = 128ULL * 1024 * 1024;

const QByteArray kMeshVertex = R"glsl(#version 410 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat3 uNormal;
out vec3 worldPosition;
out vec3 worldNormal;
void main() {
    vec4 world = uModel * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = uNormal * normal;
    gl_Position = uViewProjection * world;
}
)glsl";

const QByteArray kMeshFragment = R"glsl(#version 410 core
in vec3 worldPosition;
in vec3 worldNormal;
layout(location=0) out vec4 beauty;
layout(location=1) out float coverage;
uniform vec3 uEye;
uniform vec3 uBaseColor;
uniform float uRoughness;
uniform float uMetallic;
uniform samplerBuffer uSuns;
uniform int uSunCount;
const float pi = 3.14159265358979323846;
void main() {
    beauty = vec4(0.0, 0.0, 0.0, 1.0);
    coverage = 1.0;
    vec3 n = normalize(worldNormal);
    vec3 eyeVector = uEye - worldPosition;
    if (dot(eyeVector, eyeVector) <= 0.0) return;
    vec3 v = normalize(eyeVector);
    float nv = dot(n, v);
    if (nv <= 0.0) return;
    for (int index = 0; index < uSunCount; ++index) {
        vec4 directionIrradiance = texelFetch(uSuns, index * 2);
        vec3 sunColor = texelFetch(uSuns, index * 2 + 1).rgb;
        vec3 l = normalize(directionIrradiance.xyz);
        float nl = dot(n, l);
        if (nl <= 0.0) continue;
        vec3 halfVector = v + l;
        if (dot(halfVector, halfVector) <= 0.0) continue;
        vec3 h = normalize(halfVector);
        float nh = clamp(dot(n, h), 0.0, 1.0);
        float vh = clamp(dot(v, h), 0.0, 1.0);
        float a = max(uRoughness * uRoughness, 0.001);
        float a2 = a * a;
        // 单位 N/H 的 |N×H|²=1-(N·H)²；避免合法 r=0 近正面高光的 float 相消。
        vec3 normalCrossHalf = cross(n, h);
        float dBase = dot(normalCrossHalf, normalCrossHalf) + a2 * nh * nh;
        float d = a2 / (pi * dBase * dBase);
        float visibility = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2)
                              + nv * sqrt(nl * nl * (1.0 - a2) + a2));
        vec3 f0 = mix(vec3(0.04), uBaseColor, uMetallic);
        vec3 f = f0 + (vec3(1.0) - f0) * pow(1.0 - vh, 5.0);
        vec3 diffuse = (1.0 - uMetallic) * (vec3(1.0) - f) * uBaseColor / pi;
        beauty.rgb += (diffuse + d * visibility * f) * directionIrradiance.w * sunColor * nl;
    }
}
)glsl";

const QByteArray kFullscreenVertex = R"glsl(#version 410 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    uv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

template <class Vector> bool finiteVector(const Vector& value) {
    for (typename Vector::length_type i = 0; i < value.length(); ++i)
        if (!std::isfinite(value[i])) return false;
    return true;
}
template <class Matrix> bool floatMatrix(const Matrix& value) {
    for (typename Matrix::length_type column = 0; column < value.length(); ++column)
        for (typename Matrix::col_type::length_type row = 0; row < value[column].length(); ++row)
            if (!std::isfinite(value[column][row]) ||
                std::abs(value[column][row]) > std::numeric_limits<float>::max()) return false;
    return true;
}
bool finiteRgba(const glm::vec4& value) {
    return finiteVector(value) && value.w >= 0 && value.w <= 1;
}

/** @brief 只恢复本模块确实改动的状态；MRT blend/mask 必须按 attachment 分别保存。 */
class GlStateGuard final {
  public:
    explicit GlStateGuard(QOpenGLFunctions_4_1_Core& gl) : gl_(gl) {
        gl_.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
        gl_.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        gl_.glGetIntegerv(GL_VIEWPORT, viewport_.data());
        gl_.glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        gl_.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
        gl_.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer_);
        gl_.glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer_);
        gl_.glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer_);
        gl_.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer_);
        gl_.glGetIntegerv(GL_TEXTURE_BUFFER_BINDING, &textureBuffer_);
        gl_.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
        gl_.glActiveTexture(GL_TEXTURE0);
        gl_.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_);
        gl_.glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &bufferTexture_);
        gl_.glGetIntegeri_v(GL_SAMPLER_BINDING, 0, &sampler_);
        gl_.glGetIntegerv(GL_DEPTH_FUNC, &depthFunction_);
        gl_.glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
        gl_.glGetIntegerv(GL_POLYGON_MODE, polygonMode_.data());
        gl_.glGetIntegerv(GL_CLAMP_READ_COLOR, &clampRead_);
        for (std::size_t i = 0; i < enables_.size(); ++i)
            enables_[i] = gl_.glIsEnabled(kEnables[i]);
        for (GLuint i = 0; i < 2; ++i) {
            gl_.glGetBooleani_v(GL_COLOR_WRITEMASK, i, masks_[i].data());
            blends_[i].enabled = gl_.glIsEnabledi(GL_BLEND, i);
            for (std::size_t field = 0; field < kBlendFields.size(); ++field)
                gl_.glGetIntegeri_v(kBlendFields[field], i, &blends_[i].values[field]);
        }
        for (std::size_t i = 0; i < kPixelStores.size(); ++i)
            gl_.glGetIntegerv(kPixelStores[i], &pixelStores_[i]);
    }
    ~GlStateGuard() {
        gl_.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer_));
        gl_.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer_));
        gl_.glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        gl_.glUseProgram(static_cast<GLuint>(program_));
        gl_.glBindVertexArray(static_cast<GLuint>(vao_));
        gl_.glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer_));
        gl_.glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer_));
        gl_.glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(packBuffer_));
        gl_.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer_));
        gl_.glBindBuffer(GL_TEXTURE_BUFFER, static_cast<GLuint>(textureBuffer_));
        gl_.glActiveTexture(GL_TEXTURE0);
        gl_.glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_));
        gl_.glBindTexture(GL_TEXTURE_BUFFER, static_cast<GLuint>(bufferTexture_));
        gl_.glBindSampler(0, static_cast<GLuint>(sampler_));
        gl_.glActiveTexture(static_cast<GLenum>(activeTexture_));
        gl_.glDepthFunc(static_cast<GLenum>(depthFunction_));
        gl_.glDepthMask(depthMask_);
        gl_.glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode_[0]));
        gl_.glClampColor(GL_CLAMP_READ_COLOR, static_cast<GLenum>(clampRead_));
        for (std::size_t i = 0; i < enables_.size(); ++i)
            enables_[i] ? gl_.glEnable(kEnables[i]) : gl_.glDisable(kEnables[i]);
        for (GLuint i = 0; i < 2; ++i) {
            gl_.glColorMaski(i, masks_[i][0], masks_[i][1], masks_[i][2], masks_[i][3]);
            const auto& blend = blends_[i];
            blend.enabled ? gl_.glEnablei(GL_BLEND, i) : gl_.glDisablei(GL_BLEND, i);
            gl_.glBlendFuncSeparatei(i, static_cast<GLenum>(blend.values[0]),
                                    static_cast<GLenum>(blend.values[1]),
                                    static_cast<GLenum>(blend.values[2]),
                                    static_cast<GLenum>(blend.values[3]));
            gl_.glBlendEquationSeparatei(i, static_cast<GLenum>(blend.values[4]),
                                        static_cast<GLenum>(blend.values[5]));
        }
        for (std::size_t i = 0; i < kPixelStores.size(); ++i)
            gl_.glPixelStorei(kPixelStores[i], pixelStores_[i]);
    }
    GlStateGuard(const GlStateGuard&) = delete;
    GlStateGuard& operator=(const GlStateGuard&) = delete;

  private:
    static constexpr std::array<GLenum, 7> kEnables{
        GL_FRAMEBUFFER_SRGB, GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_MULTISAMPLE, GL_DITHER,
        GL_RASTERIZER_DISCARD};
    static constexpr std::array<GLenum, 6> kBlendFields{
        GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA,
        GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA};
    static constexpr std::array<GLenum, 10> kPixelStores{
        GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
        GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
        GL_PACK_SWAP_BYTES, GL_UNPACK_SWAP_BYTES};
    struct Blend { GLboolean enabled = GL_FALSE; std::array<GLint, 6> values{}; };
    QOpenGLFunctions_4_1_Core& gl_;
    GLint drawFramebuffer_ = 0, readFramebuffer_ = 0, program_ = 0, vao_ = 0;
    GLint arrayBuffer_ = 0, renderbuffer_ = 0, packBuffer_ = 0, unpackBuffer_ = 0;
    GLint textureBuffer_ = 0, bufferTexture_ = 0;
    GLint activeTexture_ = GL_TEXTURE0, texture_ = 0, sampler_ = 0;
    GLint depthFunction_ = GL_LESS, clampRead_ = GL_FIXED_ONLY;
    GLboolean depthMask_ = GL_TRUE;
    std::array<GLint, 4> viewport_{};
    std::array<GLint, 2> polygonMode_{};
    std::array<GLboolean, 7> enables_{};
    std::array<std::array<GLboolean, 4>, 2> masks_{};
    std::array<Blend, 2> blends_{};
    std::array<GLint, 10> pixelStores_{};
};

void preparePixelTransfer(QOpenGLFunctions_4_1_Core& gl) {
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (const auto value : {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
                              GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
                              GL_PACK_SWAP_BYTES, GL_UNPACK_SWAP_BYTES})
        gl.glPixelStorei(static_cast<GLenum>(value), 0);
    gl.glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindSampler(0, 0);
}

bool glSucceeded(QOpenGLFunctions_4_1_Core& gl, QString& error) {
    const auto code = gl.glGetError();
    if (code == GL_NO_ERROR) return true;
    error = QStringLiteral("冻结渲染 OpenGL 错误：0x%1。").arg(code, 0, 16);
    return false;
}

bool completeFence(QOpenGLFunctions_4_1_Core& gl, QString& error) {
    const auto fence = gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) {
        error = QStringLiteral("无法建立冻结渲染 GPU 完成回执。");
        return false;
    }
    const auto status = gl.glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
    gl.glDeleteSync(fence);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        error = QStringLiteral("GPU 完成回执超时或失败，不能宣布资源已完成释放。");
        return false;
    }
    return glSucceeded(gl, error);
}

GLuint buildProgram(QOpenGLFunctions_4_1_Core& gl, const QByteArray& vertex,
                    const QByteArray& fragment, QString& error) {
    const GLuint program = gl.glCreateProgram();
    std::array<GLuint, 2> shaders{};
    const std::array<QByteArray, 2> sources{vertex, fragment};
    bool ready = program != 0;
    for (std::size_t i = 0; i < shaders.size() && ready; ++i) {
        shaders[i] = gl.glCreateShader(i == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        const char* source = sources[i].constData();
        const auto length = static_cast<GLint>(sources[i].size());
        gl.glShaderSource(shaders[i], 1, &source, &length);
        gl.glCompileShader(shaders[i]);
        GLint compiled = GL_FALSE;
        gl.glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &compiled);
        ready = compiled == GL_TRUE;
        if (ready) gl.glAttachShader(program, shaders[i]);
        else {
            GLint logLength = 0;
            gl.glGetShaderiv(shaders[i], GL_INFO_LOG_LENGTH, &logLength);
            QByteArray log(std::max(logLength, 1), '\0');
            gl.glGetShaderInfoLog(shaders[i], static_cast<GLsizei>(log.size()), nullptr, log.data());
            error = QStringLiteral("冻结渲染 shader 编译失败：%1").arg(QString::fromUtf8(log));
        }
    }
    if (ready) {
        gl.glLinkProgram(program);
        GLint linked = GL_FALSE;
        gl.glGetProgramiv(program, GL_LINK_STATUS, &linked);
        ready = linked == GL_TRUE;
        if (!ready) {
            GLint logLength = 0;
            gl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            QByteArray log(std::max(logLength, 1), '\0');
            gl.glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            error = QStringLiteral("冻结渲染 shader 链接失败：%1").arg(QString::fromUtf8(log));
        }
    }
    for (const auto shader : shaders) if (shader != 0) gl.glDeleteShader(shader);
    if (!ready) {
        if (program != 0) gl.glDeleteProgram(program);
        return 0;
    }
    return program;
}

struct FloatTarget {
    QOpenGLFunctions_4_1_Core& gl;
    GLuint framebuffer = 0, depth = 0;
    std::array<GLuint, 2> textures{};
    explicit FloatTarget(QOpenGLFunctions_4_1_Core& functions) : gl(functions) {}
    ~FloatTarget() { destroy(); }
    void destroy() {
        if (depth) gl.glDeleteRenderbuffers(1, &depth);
        if (framebuffer) gl.glDeleteFramebuffers(1, &framebuffer);
        gl.glDeleteTextures(2, textures.data());
        depth = framebuffer = 0;
        textures = {};
    }
    bool create(int width, int height, QString& error) {
        gl.glGenFramebuffers(1, &framebuffer);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl.glGenTextures(2, textures.data());
        for (GLuint i = 0; i < 2; ++i) {
            gl.glBindTexture(GL_TEXTURE_2D, textures[i]);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, i == 0 ? GL_RGBA32F : GL_R32F, width, height,
                            0, i == 0 ? GL_RGBA : GL_RED, GL_FLOAT, nullptr);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                                      GL_TEXTURE_2D, textures[i], 0);
        }
        gl.glGenRenderbuffers(1, &depth);
        gl.glBindRenderbuffer(GL_RENDERBUFFER, depth);
        gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width, height);
        gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        constexpr std::array<GLenum, 2> attachments{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        gl.glDrawBuffers(2, attachments.data());
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error = QStringLiteral("RGBA32F/R32F 冻结渲染目标不完整。");
            return false;
        }
        return glSucceeded(gl, error);
    }
};

// 每灯两个 RGBA32F texel；私有 TBO 必须在宿主状态恢复前释放。
struct SunBuffer {
    QOpenGLFunctions_4_1_Core& gl;
    GLuint buffer = 0, texture = 0;
    explicit SunBuffer(QOpenGLFunctions_4_1_Core& functions) : gl(functions) {}
    ~SunBuffer() { destroy(); }
    SunBuffer(const SunBuffer&) = delete;
    SunBuffer& operator=(const SunBuffer&) = delete;
    void destroy() {
        if (texture) gl.glDeleteTextures(1, &texture);
        if (buffer) gl.glDeleteBuffers(1, &buffer);
        texture = buffer = 0;
    }
    bool create(const std::vector<glm::vec4>& data, QString& error) {
        gl.glGenBuffers(1, &buffer);
        gl.glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        gl.glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(glm::vec4)),
                         data.data(), GL_STATIC_DRAW);
        gl.glGenTextures(1, &texture);
        gl.glBindTexture(GL_TEXTURE_BUFFER, texture);
        gl.glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffer);
        return glSucceeded(gl, error);
    }
};

struct DrawMesh {
    const core::EvaluatedSceneNode* node = nullptr;
    const core::EvaluatedGeometryStage* stage = nullptr;
    const core::PbrMaterialParameters* material = nullptr;
    std::unique_ptr<GpuMesh> gpu;
};

void setVector(QOpenGLFunctions_4_1_Core& gl, GLuint program, const char* name,
               const glm::vec3& value) {
    gl.glUniform3fv(gl.glGetUniformLocation(program, name), 1, glm::value_ptr(value));
}
void setScalar(QOpenGLFunctions_4_1_Core& gl, GLuint program, const char* name, float value) {
    gl.glUniform1f(gl.glGetUniformLocation(program, name), value);
}
void drawMeshes(QOpenGLFunctions_4_1_Core& gl, GLuint program, const std::vector<DrawMesh>& meshes) {
    for (const auto& draw : meshes) {
        const glm::mat4 world(draw.node->worldAffine);
        const glm::mat3 normal(draw.node->worldNormal);
        gl.glUniformMatrix4fv(gl.glGetUniformLocation(program, "uModel"), 1, GL_FALSE,
                              glm::value_ptr(world));
        gl.glUniformMatrix3fv(gl.glGetUniformLocation(program, "uNormal"), 1, GL_FALSE,
                              glm::value_ptr(normal));
        setVector(gl, program, "uBaseColor", glm::vec3(draw.material->baseColor));
        setScalar(gl, program, "uRoughness", static_cast<float>(draw.material->roughness));
        setScalar(gl, program, "uMetallic", static_cast<float>(draw.material->metallic));
        draw.gpu->draw();
    }
}
} // namespace

struct FrozenStateRenderer::Impl {
    QOpenGLFunctions_4_1_Core* gl = nullptr;
    QOpenGLContext* context = nullptr;
    QThread* thread = nullptr;
    std::shared_ptr<const OcioDisplayTransform> color;
    GLuint pbrProgram = 0, displayProgram = 0, fullscreenVao = 0;
    std::uint64_t completed = 0;
    bool releaseVerified = false;
    bool ownsCurrentContext() const {
        return gl && context && QOpenGLContext::currentContext() == context &&
               QThread::currentThread() == thread;
    }
};

FrozenStateRenderer::FrozenStateRenderer() : impl_(std::make_unique<Impl>()) {}
FrozenStateRenderer::~FrozenStateRenderer() { destroy(); }

bool FrozenStateRenderer::initialize(QOpenGLFunctions_4_1_Core& gl,
                                     std::shared_ptr<const OcioDisplayTransform> color,
                                     QString& error) {
    error.clear();
    if (impl_->gl) {
        error = QStringLiteral("冻结渲染模块已拥有 context，必须先在所属 context 销毁。");
        return false;
    }
    auto* context = QOpenGLContext::currentContext();
    if (!context || context->isOpenGLES() || !color || !gl.initializeOpenGLFunctions()) {
        error = QStringLiteral("冻结渲染缺少真实 GL4.1 core context 或固定 OCIO 服务。");
        return false;
    }
    GLint major = 0, minor = 0, profile = 0;
    gl.glGetIntegerv(GL_MAJOR_VERSION, &major);
    gl.glGetIntegerv(GL_MINOR_VERSION, &minor);
    gl.glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (major < 4 || (major == 4 && minor < 1) || (profile & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
        error = QStringLiteral("实际 context 不满足原生 OpenGL4.1 core。");
        return false;
    }
    GlStateGuard guard(gl);
    impl_->gl = &gl;
    impl_->context = context;
    impl_->thread = QThread::currentThread();
    impl_->color = std::move(color);
    impl_->releaseVerified = false;
    impl_->pbrProgram = buildProgram(gl, kMeshVertex, kMeshFragment, error);
    const QByteArray fragment = QByteArray("#version 410 core\n") + impl_->color->gpuShaderText() +
        R"glsl(
in vec2 uv;
uniform sampler2D uBeauty;
layout(location=0) out vec4 displayColor;
void main() {
    vec4 associated = texture(uBeauty, vec2(uv.x, 1.0 - uv.y));
    // 黑背景在 linear 域预合；最终显示 alpha 为不透明，不改变母版 alpha。
    vec3 composed = associated.rgb + (1.0 - associated.a) * vec3(0.0);
    displayColor = vec4(mini3dOcioDisplay(vec4(composed, 1.0)).rgb, 1.0);
}
)glsl";
    if (impl_->pbrProgram) impl_->displayProgram = buildProgram(gl, kFullscreenVertex, fragment, error);
    if (impl_->displayProgram) gl.glGenVertexArrays(1, &impl_->fullscreenVao);
    if (!impl_->pbrProgram || !impl_->displayProgram || !impl_->fullscreenVao || !glSucceeded(gl, error)) {
        if (error.isEmpty()) error = QStringLiteral("冻结渲染 GPU 初始化失败。");
        destroy();
        return false;
    }
    return true;
}

std::optional<FrozenRenderPixels>
FrozenStateRenderer::render(const core::EvaluatedSceneState& state,
                            const FrozenRenderCamera& camera, int width, int height,
                            QString& error) {
    error.clear();
    if (!impl_->ownsCurrentContext() || !impl_->pbrProgram) {
        error = QStringLiteral("冻结渲染必须在创建它的 context 和线程中调用。");
        return std::nullopt;
    }
    auto& gl = *impl_->gl;
    GLint maximumSize = 0;
    gl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumSize);
    if (width <= 0 || height <= 0 || width > maximumSize || height > maximumSize) {
        error = QStringLiteral("冻结渲染目标尺寸非法或超出实际设备能力。");
        return std::nullopt;
    }
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    std::uint64_t gpuBytes = count * 24;
    if (gpuBytes > kGpuBudget || !finiteVector(camera.position) ||
        !finiteVector(glm::vec3(camera.position)) || !floatMatrix(camera.viewMatrix) ||
        !floatMatrix(camera.projectionMatrix) ||
        !floatMatrix(camera.projectionMatrix * camera.viewMatrix) ||
        !floatMatrix(glm::inverse(camera.projectionMatrix * camera.viewMatrix))) {
        error = QStringLiteral("冻结渲染目标超出切片附件预算或相机不可转换为 FLOAT32。");
        return std::nullopt;
    }
    if (!core::isSourceIdentityValid(state.source) || !state.content ||
        !state.source.resourceSetIdentity.empty() ||
        state.content->geometryStages.size() != state.content->nodes.size()) {
        error = QStringLiteral("冻结渲染来源/完整状态无效，外部资源尚未支持。");
        return std::nullopt;
    }
    const glm::mat4 viewProjection(camera.projectionMatrix * camera.viewMatrix);
    std::vector<DrawMesh> meshes;
    for (const auto& node : state.content->nodes) {
        const auto found = state.content->geometryStages.find(node.entity);
        if (found == state.content->geometryStages.end() || found->second.identity.entity != node.entity ||
            found->second.geometry.entity != node.entity || found->second.identity.worldAffine != node.worldAffine ||
            found->second.identity.contentVersion != found->second.geometry.contentVersion ||
            found->second.identity.primitive != found->second.geometry.primitive ||
            found->second.identity.mesh != found->second.geometry.editableMesh) {
            error = QStringLiteral("冻结几何 stage 与有效节点不一致。");
            return std::nullopt;
        }
        const auto& stage = found->second;
        if (!node.visible || !stage.geometry.hasGeometry) continue;
        if (node.materialSlots.size() != 1) {
            error = QStringLiteral("Unsupported：PBR 几何必须有唯一有效 typed 材质槽。");
            return std::nullopt;
        }
        const auto& slot = node.materialSlots.front();
        const auto material = state.content->materials.find(slot.material);
        if (!slot.id.isValid() || material == state.content->materials.end() ||
            material->second.author.id != slot.material ||
            material->second.author.model != core::MaterialModel::PbrMetallicRoughness ||
            !core::isPbrMaterialValid(material->second.effective)) {
            error = QStringLiteral("冻结 PBR 材质引用或有效值非法。");
            return std::nullopt;
        }
        if (!material->second.author.textures.empty() || material->second.effective.coverage != 1) {
            error = QStringLiteral("Unsupported：J1 仅支持无纹理、coverage=1 的 OPAQUE PBR。");
            return std::nullopt;
        }
        if (stage.geometry.editableContent &&
            std::any_of(stage.geometry.editableContent->source.faces.begin(),
                        stage.geometry.editableContent->source.faces.end(),
                        [](const auto& face) { return face.material != core::kInvalidAsset; })) {
            error = QStringLiteral("Unsupported：J1 尚无逐面 typed 材质槽消费。");
            return std::nullopt;
        }
        const auto& mesh = stage.geometry.mesh;
        if (!mesh || mesh->vertices.empty() || mesh->indices.empty() || mesh->indices.size() % 3 != 0 ||
            !floatMatrix(node.worldAffine) || !floatMatrix(node.worldNormal)) {
            error = QStringLiteral("冻结 CPU 几何或有效矩阵不可消费。");
            return std::nullopt;
        }
        if (std::any_of(mesh->indices.begin(), mesh->indices.end(),
                        [&](std::uint32_t index) { return index >= mesh->vertices.size(); })) {
            error = QStringLiteral("冻结 CPU 几何三角形索引越界。");
            return std::nullopt;
        }
        const glm::mat4 world(node.worldAffine);
        const glm::mat3 normal(node.worldNormal);
        for (const auto& vertex : mesh->vertices) {
            const auto point = world * glm::vec4(vertex.position, 1);
            const auto direction = normal * vertex.normal;
            if (!finiteVector(point) || !finiteVector(viewProjection * point) ||
                !finiteVector(direction) || !std::isfinite(glm::length(direction)) ||
                glm::length(direction) <= 0) {
                error = QStringLiteral("冻结几何实际 FLOAT32 点/法线/投影不可消费。");
                return std::nullopt;
            }
        }
        gpuBytes += mesh->vertices.size() * sizeof(core::MeshVertex) + mesh->indices.size() * sizeof(std::uint32_t);
        if (gpuBytes > kGpuBudget) {
            error = QStringLiteral("冻结网格和浮点附件超出 J1 可归因 GPU 预算。");
            return std::nullopt;
        }
        meshes.push_back({&node, &stage, &material->second.effective, nullptr});
    }
    const auto sunCount = state.content->visibleSuns.size();
    GLint maximumSunTexels = 0;
    gl.glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximumSunTexels);
    if (maximumSunTexels < 2 || sunCount > static_cast<std::size_t>(maximumSunTexels) / 2) {
        error = QStringLiteral("冻结 Sun 数据超过实际 GL_TEXTURE_BUFFER texel 设备上限。");
        return std::nullopt;
    }
    // 零灯仍绑定最小有效 TBO；不以固定灯数代替实际设备和剩余预算。
    const auto sunBytes = std::max<std::uint64_t>(sunCount, 1) * 2 * sizeof(glm::vec4);
    if (sunBytes > kGpuBudget - gpuBytes) {
        error = QStringLiteral("冻结 Sun、网格和浮点附件超出 J1 可归因 GPU 预算。");
        return std::nullopt;
    }
    std::vector<glm::vec4> sunData(std::max<std::size_t>(sunCount, 1) * 2, glm::vec4(0));
    for (std::size_t index = 0; index < sunCount; ++index) {
        const auto& sun = state.content->visibleSuns[index];
        const glm::vec3 direction(sun.directionToLight);
        if (!core::isSunComponentValid(sun.effective) ||
            !std::isfinite(static_cast<float>(sun.effective.irradiance)) ||
            !finiteVector(direction) || !std::isfinite(glm::length(direction)) || glm::length(direction) <= 0) {
            error = QStringLiteral("冻结 Sun 实际 FLOAT32 辐照度或 toward-light 方向不可消费。");
            return std::nullopt;
        }
        sunData[index * 2] = glm::vec4(direction, static_cast<float>(sun.effective.irradiance));
        sunData[index * 2 + 1] = glm::vec4(glm::vec3(sun.effective.color), 0);
    }
    GlStateGuard guard(gl);
    preparePixelTransfer(gl);
    for (const auto capability : {GL_FRAMEBUFFER_SRGB, GL_CULL_FACE, GL_SCISSOR_TEST,
                                   GL_MULTISAMPLE, GL_DITHER, GL_RASTERIZER_DISCARD})
        gl.glDisable(static_cast<GLenum>(capability));
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LESS);
    gl.glDepthMask(GL_TRUE);
    gl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    for (GLuint attachment = 0; attachment < 2; ++attachment) {
        gl.glDisablei(GL_BLEND, attachment);
        gl.glColorMaski(attachment, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    FloatTarget target(gl);
    if (!target.create(width, height, error)) return std::nullopt;
    SunBuffer suns(gl);
    if (!suns.create(sunData, error)) return std::nullopt;
    gl.glViewport(0, 0, width, height);
    constexpr std::array<GLfloat, 4> zero{};
    constexpr GLfloat one = 1;
    gl.glClearBufferfv(GL_COLOR, 0, zero.data());
    gl.glClearBufferfv(GL_COLOR, 1, zero.data());
    gl.glClearBufferfv(GL_DEPTH, 0, &one);
    for (auto& draw : meshes) {
        draw.gpu = std::make_unique<GpuMesh>();
        if (!draw.gpu->upload(gl, *draw.stage->geometry.mesh)) {
            error = QStringLiteral("冻结 CPU mesh 的真实 GPU 上传失败。");
            return std::nullopt;
        }
    }
    gl.glUseProgram(impl_->pbrProgram);
    gl.glUniformMatrix4fv(gl.glGetUniformLocation(impl_->pbrProgram, "uViewProjection"),
                          1, GL_FALSE, glm::value_ptr(viewProjection));
    setVector(gl, impl_->pbrProgram, "uEye", glm::vec3(camera.position));
    gl.glUniform1i(gl.glGetUniformLocation(impl_->pbrProgram, "uSuns"), 0);
    gl.glUniform1i(gl.glGetUniformLocation(impl_->pbrProgram, "uSunCount"), static_cast<GLint>(sunCount));
    drawMeshes(gl, impl_->pbrProgram, meshes);
    if (!glSucceeded(gl, error)) return std::nullopt;
    FrozenRenderPixels pixels;
    pixels.source = state.source;
    pixels.camera = camera;
    pixels.color = impl_->color->identity();
    pixels.backendBuild = QString::fromLatin1(kBackendBuild);
    pixels.width = width;
    pixels.height = height;
    for (const auto& [entity, stage] : state.content->geometryStages) {
        (void)entity;
        pixels.geometryStages.push_back(stage.identity);
    }
    pixels.beauty.resize(static_cast<std::size_t>(count));
    pixels.coverage.resize(static_cast<std::size_t>(count));
    std::vector<glm::vec4> bottomUp(static_cast<std::size_t>(count));
    std::vector<float> bottomCoverage(static_cast<std::size_t>(count));
    gl.glReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, bottomUp.data());
    gl.glReadBuffer(GL_COLOR_ATTACHMENT1);
    gl.glReadPixels(0, 0, width, height, GL_RED, GL_FLOAT, bottomCoverage.data());
    if (!glSucceeded(gl, error) || !std::all_of(bottomUp.begin(), bottomUp.end(), finiteRgba) ||
        !std::all_of(bottomCoverage.begin(), bottomCoverage.end(),
                     [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; })) {
        if (error.isEmpty()) error = QStringLiteral("真实 GPU 母版出现非有限或非法 alpha/coverage。");
        return std::nullopt;
    }
    for (int y = 0; y < height; ++y) {
        const auto source = static_cast<std::size_t>(height - 1 - y) * width;
        const auto destination = static_cast<std::size_t>(y) * width;
        std::copy_n(bottomUp.begin() + source, width, pixels.beauty.begin() + destination);
        std::copy_n(bottomCoverage.begin() + source, width, pixels.coverage.begin() + destination);
    }
    meshes.clear();
    suns.destroy();
    target.destroy();
    if (!completeFence(gl, error)) return std::nullopt;
    ++impl_->completed;
    return pixels;
}

bool FrozenStateRenderer::present(const FrozenRenderPixels& pixels,
                                  unsigned int destinationFramebuffer, QString& error) {
    error.clear();
    if (!impl_->ownsCurrentContext() || !impl_->displayProgram) {
        error = QStringLiteral("冻结显示必须在创建它的 context 和线程中调用。");
        return false;
    }
    const auto count = static_cast<std::uint64_t>(std::max(pixels.width, 0)) * std::max(pixels.height, 0);
    if (pixels.width <= 0 || pixels.height <= 0 || count * 16 > kGpuBudget ||
        pixels.beauty.size() != count || pixels.coverage.size() != count ||
        !core::isSourceIdentityValid(pixels.source) || pixels.color != impl_->color->identity() ||
        pixels.backendBuild != QString::fromLatin1(kBackendBuild) ||
        !std::all_of(pixels.beauty.begin(), pixels.beauty.end(), finiteRgba) ||
        !std::all_of(pixels.coverage.begin(), pixels.coverage.end(),
                     [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; })) {
        error = QStringLiteral("冻结显示输入不是有效的同配置 linear 母版。");
        return false;
    }
    auto& gl = *impl_->gl;
    GlStateGuard guard(gl);
    preparePixelTransfer(gl);
    GLuint texture = 0;
    gl.glGenTextures(1, &texture);
    gl.glBindTexture(GL_TEXTURE_2D, texture);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, pixels.width, pixels.height,
                    0, GL_RGBA, GL_FLOAT, pixels.beauty.data());
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destinationFramebuffer);
    gl.glViewport(0, 0, pixels.width, pixels.height);
    for (const auto capability : {GL_FRAMEBUFFER_SRGB, GL_DEPTH_TEST, GL_CULL_FACE,
                                   GL_SCISSOR_TEST, GL_MULTISAMPLE, GL_DITHER,
                                   GL_RASTERIZER_DISCARD})
        gl.glDisable(static_cast<GLenum>(capability));
    gl.glDisablei(GL_BLEND, 0);
    gl.glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.glUseProgram(impl_->displayProgram);
    gl.glUniform1i(gl.glGetUniformLocation(impl_->displayProgram, "uBeauty"), 0);
    gl.glBindVertexArray(impl_->fullscreenVao);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glDeleteTextures(1, &texture);
    return glSucceeded(gl, error) && completeFence(gl, error);
}

void FrozenStateRenderer::destroy() {
    if (!impl_->gl) return;
    if (!impl_->ownsCurrentContext()) {
        qWarning() << "冻结渲染资源未释放：当前 context/thread 不是创建者。";
        return;
    }
    auto& gl = *impl_->gl;
    const auto pbr = impl_->pbrProgram;
    const auto display = impl_->displayProgram;
    const auto vao = impl_->fullscreenVao;
    if (pbr) gl.glDeleteProgram(pbr);
    if (display) gl.glDeleteProgram(display);
    if (vao) gl.glDeleteVertexArrays(1, &vao);
    QString error;
    impl_->releaseVerified = completeFence(gl, error) &&
        (pbr == 0 || gl.glIsProgram(pbr) == GL_FALSE) &&
        (display == 0 || gl.glIsProgram(display) == GL_FALSE) &&
        (vao == 0 || gl.glIsVertexArray(vao) == GL_FALSE);
    if (!impl_->releaseVerified) qWarning() << "冻结渲染释放回执失败：" << error;
    impl_->pbrProgram = impl_->displayProgram = impl_->fullscreenVao = 0;
    impl_->color.reset();
    impl_->gl = nullptr;
    impl_->context = nullptr;
    impl_->thread = nullptr;
}

std::uint64_t FrozenStateRenderer::completedRenderCount() const { return impl_->completed; }
bool FrozenStateRenderer::isInitialized() const { return impl_->gl && impl_->pbrProgram && impl_->displayProgram; }
bool FrozenStateRenderer::hasVerifiedRelease() const { return impl_->releaseVerified; }

} // namespace mini3d::renderer_gl
