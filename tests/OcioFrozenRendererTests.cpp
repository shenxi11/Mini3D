/*
 * 模块名: OcioFrozenRendererTests
 * 功能概述: 用真实冻结状态、离屏 GL 像素和独立 double BRDF 验证 J1 静态模块。
 * 对外接口: Catch2 [j1-static-render]；复用现有 GPU main，不创建作者 UI 或输出文件。
 * 依赖关系: Core producer、FrozenStateRenderer、实际 OCIO processor、Qt GL4.1、Catch2。
 * 输入输出: 运行前固定 manifest 到浮点母版、独立几何/BRDF oracle 及颜色反例断言。
 * 异常与错误: context、配置、GPU、数值或语义不符均失败，不跳过或调整阈值。
 * 维护说明: 采样网格在源码中固定；synthetic data 运输不冒充产品 AOV producer。
 */
#include "renderer_gl/FrozenStateRenderer.h"
#include "renderer_gl/PrimitiveFactory.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <numbers>
#include <span>

using namespace mini3d;
using renderer_gl::FrozenRenderCamera;
using renderer_gl::FrozenRenderPixels;
using renderer_gl::OcioDisplayTransform;

namespace {
constexpr char kManifestSha256[] =
    "BE7C46AEEB710E37D1B702FE71ECC17DD82201C2FE6A8DF39F0BE1F53E3B4848";
// 固定像素中心 grid；边缘掩码只由几何计算决定，不依据渲染误差挑点。
constexpr int kOracleStride = 8;
constexpr int kOracleOrigin = 4;

QByteArray fileBytes(const QString& path) {
    QFile file(path);
    INFO(path.toStdString());
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QByteArray sha256(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toUpper();
}

double number(const QJsonValue& value) {
    REQUIRE(value.isDouble());
    const auto result = value.toDouble();
    REQUIRE(std::isfinite(result));
    return result;
}

glm::dvec3 vector3(const QJsonValue& value) {
    const auto array = value.toArray();
    REQUIRE(array.size() == 3);
    return {number(array[0]), number(array[1]), number(array[2])};
}

struct Manifest {
    QJsonObject json;
    int width = 0, height = 0;
    core::EntityId cube = 0, sun = 0, cameraEntity = 0;
    core::MaterialId material;
    core::MaterialSlotId materialSlot;
    core::RationalFps fps;
    FrozenRenderCamera camera;

    Manifest() {
        const QString path = QString::fromUtf8(MINI3D_J1_RENDER_MANIFEST);
        const auto bytes = fileBytes(path);
        REQUIRE(sha256(bytes) == kManifestSha256);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        REQUIRE(error.error == QJsonParseError::NoError);
        REQUIRE(document.isObject());
        json = document.object();
        REQUIRE(json["task"].toString() == QStringLiteral("EXEC-010"));
        REQUIRE(json["contract"].toString() == QStringLiteral("J1-CONTRACT-2"));
        REQUIRE(json["moduleOnly"].toBool());
        const auto target = json["target"].toObject();
        width = static_cast<int>(number(target["width"]));
        height = static_cast<int>(number(target["height"]));
        REQUIRE(number(target["dpr"]) == 1);
        REQUIRE(number(target["samples"]) == 0);
        REQUIRE((target["pixelAspect"].toArray() == QJsonArray{1, 1}));
        const auto cameraData = json["camera"].toObject();
        cameraEntity = static_cast<core::EntityId>(number(cameraData["entity"]));
        camera.position = vector3(cameraData["position"]);
        camera.viewMatrix = glm::lookAt(camera.position, vector3(cameraData["target"]),
                                        vector3(cameraData["up"]));
        camera.projectionMatrix = glm::perspective(
            glm::radians(number(cameraData["fovDegrees"])),
            static_cast<double>(width) / height, number(cameraData["near"]),
            number(cameraData["far"]));
        cube = static_cast<core::EntityId>(number(json["cube"].toObject()["entity"]));
        sun = static_cast<core::EntityId>(number(json["sun"].toObject()["entity"]));
        const auto materialData = json["material"].toObject();
        material = core::MaterialId{static_cast<std::uint64_t>(number(materialData["id"]))};
        materialSlot = core::MaterialSlotId{
            static_cast<std::uint64_t>(number(materialData["slotId"]))};
        const auto rates = json["fps"].toArray();
        REQUIRE(rates.size() == 2);
        fps = {static_cast<std::uint32_t>(number(rates[0])),
               static_cast<std::uint32_t>(number(rates[1]))};
        REQUIRE(core::isRationalFpsValid(fps));
        const QDir root(QFileInfo(path).dir().absoluteFilePath(QStringLiteral("../..")));
        const auto cubeData = json["cube"].toObject();
        REQUIRE(sha256(fileBytes(root.absoluteFilePath(cubeData["meshSource"].toString()))) ==
                cubeData["meshSourceSha256"].toString().toLatin1());
    }

    std::array<int, 4> roi(const QString& property) const {
        const auto values = json["isolatedChanges"].toObject()[property].toObject()["roi"].toArray();
        REQUIRE(values.size() == 4);
        return {static_cast<int>(number(values[0])), static_cast<int>(number(values[1])),
                static_cast<int>(number(values[2])), static_cast<int>(number(values[3]))};
    }
};

core::SourceIdentity sourceFor(const Manifest& manifest, double frame,
                               std::uint64_t evaluation = 1) {
    core::SourceIdentity source;
    const auto document = core::parseDocumentUuid("b5678f26-07d8-49ae-8932-22a048e10f04");
    REQUIRE(document);
    source.documentUuid = *document;
    source.contentRevision = 1;
    source.sourceHandle = core::LiveSourceHandle{"j1-module-fixture", "frozen-author-fixture"};
    source.authorContextIdentity = {source.documentUuid, 1,
        std::get<core::LiveSourceHandle>(source.sourceHandle)};
    source.frameTime = frame;
    source.rationalFps = manifest.fps;
    source.evaluationSessionId.bytes =
        {0x48, 0xdb, 0xc0, 0x25, 0xa1, 0xe7, 0x41, 0x14,
         0x83, 0x62, 0x16, 0x08, 0x4c, 0x97, 0xaa, 0x30};
    source.evaluationProfile = core::EvaluationProfile::JointPreviewJ1;
    source.evaluationId = evaluation;
    REQUIRE(core::isSourceIdentityValid(source));
    return source;
}

struct Fixture {
    std::vector<core::AnimationPoseInput> inputs;
    std::shared_ptr<core::SceneAuthoringSnapshot> authors;
    std::shared_ptr<core::SceneMaterialData> materials;
    std::shared_ptr<core::SceneGeometrySnapshot> geometry;
    core::SceneAnimation animation;
};

Fixture fixtureFor(const Manifest& manifest, bool highlightQuad = false) {
    Fixture fixture;
    fixture.authors = std::make_shared<core::SceneAuthoringSnapshot>();
    fixture.materials = std::make_shared<core::SceneMaterialData>();
    fixture.geometry = std::make_shared<core::SceneGeometrySnapshot>();
    fixture.authors->documentUuid = sourceFor(manifest, 1).documentUuid;
    fixture.authors->materials = fixture.materials;
    core::MaterialDefinition material;
    material.id = manifest.material;
    const auto parameters = manifest.json["material"].toObject();
    material.parameters.baseColor = vector3(parameters["baseColor"]);
    material.parameters.metallic = number(parameters["metallic"]);
    material.parameters.coverage = number(parameters["coverage"]);
    const core::StableAssetId asset{manifest.material.value};
    fixture.materials->assets.emplace(asset,
        core::SceneAssetDefinition{asset, core::SceneAssetKind::Material, material});
    core::SceneNodeAuthoring cube;
    cube.entity = manifest.cube;
    cube.visible = true;
    cube.materialSlots = {{manifest.materialSlot, manifest.material}};
    fixture.authors->nodes.emplace(cube.entity, cube);
    core::SceneNodeAuthoring sun;
    sun.entity = manifest.sun;
    sun.visible = true;
    sun.sun = core::SunComponent{1, vector3(manifest.json["sun"].toObject()["color"])};
    fixture.authors->nodes.emplace(sun.entity, sun);
    core::SceneNodeAuthoring camera;
    camera.entity = manifest.cameraEntity;
    camera.visible = true;
    const auto cameraData = manifest.json["camera"].toObject();
    camera.camera = core::CameraComponent{
        static_cast<float>(number(cameraData["fovDegrees"])),
        static_cast<float>(number(cameraData["near"])),
        static_cast<float>(number(cameraData["far"]))};
    fixture.authors->nodes.emplace(camera.entity, camera);
    for (const auto entity : {manifest.cube, manifest.sun, manifest.cameraEntity}) {
        fixture.inputs.push_back({entity, 0, {}});
        core::SceneGeometryEntry entry;
        entry.entity = entity;
        fixture.geometry->entries.emplace(entity, entry);
    }
    fixture.inputs.back().base.position = glm::vec3(manifest.camera.position);
    auto mesh = std::make_shared<core::MeshData>();
    if (!highlightQuad) {
        *mesh = renderer_gl::PrimitiveFactory::createCube();
        const auto cubeData = manifest.json["cube"].toObject();
        REQUIRE(mesh->vertices.size() == static_cast<std::size_t>(number(cubeData["vertexCount"])));
        REQUIRE(mesh->indices.size() == static_cast<std::size_t>(number(cubeData["indexCount"])));
    } else {
        const auto quad = manifest.json["highlightQuad"].toObject();
        for (const auto value : quad["positions"].toArray())
            mesh->vertices.push_back({glm::vec3(vector3(value)), glm::vec3(vector3(quad["normal"]))});
        for (const auto value : quad["indices"].toArray())
            mesh->indices.push_back(static_cast<std::uint32_t>(number(value)));
        fixture.inputs.front().base.position = glm::vec3(vector3(quad["position"]));
        const auto rotation = core::quaternionFromEulerXYZDegrees(vector3(quad["rotationEulerXYZDegrees"]));
        REQUIRE(rotation);
        fixture.inputs.front().base.rotation = glm::quat(*rotation);
    }
    auto& cubeGeometry = fixture.geometry->entries.at(manifest.cube);
    cubeGeometry.primitive = highlightQuad ? core::PrimitiveKind::Plane : core::PrimitiveKind::Cube;
    cubeGeometry.hasGeometry = true;
    cubeGeometry.contentVersion = 1;
    cubeGeometry.localBounds = mesh->bounds();
    cubeGeometry.mesh = std::move(mesh);
    return fixture;
}

void setFixtureValues(Fixture& fixture, const Manifest& manifest,
                      double x, double roughness, double irradiance) {
    fixture.inputs.front().base.position.x = static_cast<float>(x);
    fixture.materials->assets.at(core::StableAssetId{manifest.material.value})
        .material->parameters.roughness = roughness;
    fixture.authors->nodes.at(manifest.sun).sun->irradiance = irradiance;
}

void addAnimatedValues(Fixture& fixture, const Manifest& manifest) {
    const auto samples = manifest.json["timeSamples"].toArray();
    REQUIRE(samples.size() == 3);
    const auto first = samples.first().toObject();
    const auto last = samples.last().toObject();
    fixture.animation.properties.rationalFps = manifest.fps;
    core::ActionDefinition action;
    action.id = core::ActionId{81};
    const std::array<core::PropertyId, 3> properties{
        core::PropertyId::Position, core::PropertyId::PbrRoughness, core::PropertyId::SunIrradiance};
    const std::array<QString, 3> names{QStringLiteral("x"), QStringLiteral("roughness"),
                                      QStringLiteral("irradiance")};
    for (std::size_t index = 0; index < properties.size(); ++index) {
        core::ScalarCurve curve;
        curve.id = core::CurveId{61 + index};
        curve.property = properties[index];
        curve.component = index == 0 ? core::PropertyComponent::X : core::PropertyComponent::Scalar;
        curve.keys = {
            {core::KeyId{71 + index * 2}, number(first["frame"]), number(first[names[index]])},
            {core::KeyId{72 + index * 2}, number(last["frame"]), number(last[names[index]])}};
        core::ActionSlotDefinition slot;
        slot.id = core::ActionSlotId{91 + index};
        slot.ownerKind = index == 1 ? core::PropertyOwnerKind::Material : core::PropertyOwnerKind::Entity;
        slot.curves.push_back(std::move(curve));
        action.actionSlots.push_back(slot);
        const core::OwnerRef owner = index == 1 ? core::OwnerRef{core::MaterialOwner{manifest.material}}
            : core::OwnerRef{core::EntityOwner{index == 0 ? manifest.cube : manifest.sun}};
        fixture.animation.properties.bindings.push_back(
            {fixture.authors->documentUuid, owner, action.id, slot.id});
    }
    fixture.animation.properties.actions.push_back(std::move(action));
}

core::EvaluatedSceneState evaluate(const Fixture& fixture, const Manifest& manifest,
                                  double frame = 1, std::uint64_t evaluation = 1) {
    auto authors = std::make_shared<core::SceneAuthoringSnapshot>(*fixture.authors);
    authors->materials = std::make_shared<const core::SceneMaterialData>(*fixture.materials);
    const auto geometry = std::make_shared<const core::SceneGeometrySnapshot>(*fixture.geometry);
    const auto result = core::evaluateSceneState(fixture.inputs, fixture.animation,
        std::move(authors), geometry, sourceFor(manifest, frame, evaluation));
    INFO(static_cast<int>(result.validation.error));
    REQUIRE(result.state);
    REQUIRE(result.validation.isValid());
    return *result.state;
}

std::shared_ptr<const OcioDisplayTransform> fixedColor(const Manifest& manifest) {
    const auto expected = manifest.json["color"].toObject();
    QString error;
    auto color = OcioDisplayTransform::createFixedStandard(expected["configResource"].toString(), error);
    INFO(error.toStdString());
    REQUIRE(color);
    REQUIRE(color->identity().libraryVersion == expected["ocioVersion"].toString());
    REQUIRE(color->identity().configSha256 == expected["configSha256"].toString().toLatin1());
    REQUIRE(color->identity().display == expected["display"].toString());
    REQUIRE(color->identity().view == expected["view"].toString());
    REQUIRE(color->identity().look == expected["look"].toString());
    REQUIRE(color->identity().exposure == number(expected["exposure"]));
    REQUIRE(color->identity().gamma == number(expected["gamma"]));
    REQUIRE_FALSE(color->identity().processorCacheId.isEmpty());
    REQUIRE(color->identity().shaderSha256 == sha256(color->gpuShaderText()));
    return color;
}

struct GpuContext {
    QOpenGLContext context;
    QOffscreenSurface surface;
    QOpenGLFunctions_4_1_Core gl;

    GpuContext() {
        QSurfaceFormat format;
        format.setVersion(4, 1);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setSamples(0);
        context.setFormat(format);
        REQUIRE(context.create());
        surface.setFormat(context.format());
        surface.create();
        REQUIRE(surface.isValid());
        REQUIRE(context.makeCurrent(&surface));
        REQUIRE(gl.initializeOpenGLFunctions());
        REQUIRE_FALSE(context.isOpenGLES());
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
    }
    ~GpuContext() { context.doneCurrent(); }
};

FrozenRenderPixels draw(renderer_gl::FrozenStateRenderer& renderer,
                         const core::EvaluatedSceneState& state, const Manifest& manifest) {
    QString error;
    auto pixels = renderer.render(state, manifest.camera, manifest.width, manifest.height, error);
    INFO(error.toStdString());
    REQUIRE(pixels);
    REQUIRE(error.isEmpty());
    REQUIRE(pixels->source == state.source);
    REQUIRE(pixels->camera == manifest.camera);
    REQUIRE(pixels->width == manifest.width);
    REQUIRE(pixels->height == manifest.height);
    REQUIRE(pixels->backendBuild == manifest.json["backend"].toString());
    REQUIRE(pixels->geometryStages.size() == state.content->geometryStages.size());
    for (const auto& stage : pixels->geometryStages)
        REQUIRE(stage == state.content->geometryStages.at(stage.entity).identity);
    return std::move(*pixels);
}

struct Hit {
    glm::dvec3 point{0}, normal{0}, barycentric{0};
    std::array<glm::dvec3, 3> triangle{};
    const core::PbrMaterialParameters* material = nullptr;
    double distance = std::numeric_limits<double>::infinity();
};

// 独立 double 射线/Möller–Trumbore；不调用产品 picking、PBR 或投影 helper。
std::optional<Hit> tracePixel(const core::EvaluatedSceneState& state,
                             const FrozenRenderCamera& camera, int width, int height, int x, int y) {
    const auto inverse = glm::inverse(camera.projectionMatrix * camera.viewMatrix);
    const glm::dvec2 ndc{2.0 * (x + 0.5) / width - 1.0, 1.0 - 2.0 * (y + 0.5) / height};
    const auto near4 = inverse * glm::dvec4(ndc, -1, 1);
    const auto far4 = inverse * glm::dvec4(ndc, 1, 1);
    const glm::dvec3 origin = glm::dvec3(near4) / near4.w;
    const glm::dvec3 direction = glm::normalize(glm::dvec3(far4) / far4.w - origin);
    std::optional<Hit> nearest;
    for (const auto& node : state.content->nodes) {
        const auto& geometry = state.content->geometryStages.at(node.entity).geometry;
        if (!node.visible || !geometry.hasGeometry) continue;
        REQUIRE(geometry.mesh);
        const auto& mesh = *geometry.mesh;
        for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
            Hit hit;
            std::array<glm::dvec3, 3> normals;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& vertex = mesh.vertices.at(mesh.indices.at(triangle + corner));
                hit.triangle[corner] = glm::dvec3(node.worldAffine * glm::dvec4(vertex.position, 1));
                normals[corner] = glm::dvec3(vertex.normal);
            }
            const auto edge1 = hit.triangle[1] - hit.triangle[0];
            const auto edge2 = hit.triangle[2] - hit.triangle[0];
            const auto crossDirection = glm::cross(direction, edge2);
            const double determinant = glm::dot(edge1, crossDirection);
            if (std::abs(determinant) < 1.0e-14) continue;
            const auto fromFirst = origin - hit.triangle[0];
            const double u = glm::dot(fromFirst, crossDirection) / determinant;
            if (u < 0 || u > 1) continue;
            const auto crossFirst = glm::cross(fromFirst, edge1);
            const double v = glm::dot(direction, crossFirst) / determinant;
            if (v < 0 || u + v > 1) continue;
            const double distance = glm::dot(edge2, crossFirst) / determinant;
            if (distance <= 0 || (nearest && distance >= nearest->distance)) continue;
            hit.distance = distance;
            hit.point = origin + distance * direction;
            hit.barycentric = {1.0 - u - v, u, v};
            hit.normal = glm::normalize(node.worldNormal *
                (hit.barycentric.x * normals[0] + u * normals[1] + v * normals[2]));
            hit.material = &state.content->materials.at(node.materialSlots.front().material).effective;
            nearest = hit;
        }
    }
    return nearest;
}

bool isOracleInterior(const Hit& hit, const Manifest& manifest, int x, int y) {
    const auto mask = manifest.json["oracleMask"].toObject();
    if (std::min({hit.barycentric.x, hit.barycentric.y, hit.barycentric.z}) <
        number(mask["minimumBarycentric"])) return false;
    std::array<glm::dvec2, 3> projected;
    const auto vp = manifest.camera.projectionMatrix * manifest.camera.viewMatrix;
    for (std::size_t index = 0; index < projected.size(); ++index) {
        const auto clip = vp * glm::dvec4(hit.triangle[index], 1);
        projected[index] = {(clip.x / clip.w * 0.5 + 0.5) * manifest.width,
                            (0.5 - clip.y / clip.w * 0.5) * manifest.height};
    }
    const glm::dvec2 pixel{x + 0.5, y + 0.5};
    for (std::size_t index = 0; index < projected.size(); ++index) {
        const auto edge = projected[(index + 1) % 3] - projected[index];
        const auto offset = pixel - projected[index];
        const double distance = std::abs(edge.x * offset.y - edge.y * offset.x) / glm::length(edge);
        if (distance < number(mask["minimumProjectedEdgeDistancePixels"])) return false;
    }
    return true;
}

// 以纯几何距离对射线未命中样本一致应用事前冻结的背景边界 mask。
double projectedEdgeDistancePixels(const core::EvaluatedSceneState& state,
                                   const Manifest& manifest, int x, int y) {
    const auto vp = manifest.camera.projectionMatrix * manifest.camera.viewMatrix;
    const glm::dvec2 pixel{x + 0.5, y + 0.5};
    double nearest = std::numeric_limits<double>::infinity();
    for (const auto& node : state.content->nodes) {
        const auto& geometry = state.content->geometryStages.at(node.entity).geometry;
        if (!node.visible || !geometry.hasGeometry) continue;
        const auto& mesh = *geometry.mesh;
        for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
            std::array<glm::dvec2, 3> projected;
            for (std::size_t corner = 0; corner < projected.size(); ++corner) {
                const auto& vertex = mesh.vertices.at(mesh.indices.at(triangle + corner));
                const auto clip = vp * node.worldAffine * glm::dvec4(vertex.position, 1);
                projected[corner] = {(clip.x / clip.w * 0.5 + 0.5) * manifest.width,
                                     (0.5 - clip.y / clip.w * 0.5) * manifest.height};
            }
            for (std::size_t corner = 0; corner < projected.size(); ++corner) {
                const auto edge = projected[(corner + 1) % 3] - projected[corner];
                const auto offset = pixel - projected[corner];
                const double parameter = std::clamp(glm::dot(offset, edge) / glm::dot(edge, edge),
                                                     0.0, 1.0);
                nearest = std::min(nearest, glm::length(offset - parameter * edge));
            }
        }
    }
    return nearest;
}

glm::dvec3 referenceBrdf(const Hit& hit, const FrozenRenderCamera& camera,
                         std::span<const core::EvaluatedSun> suns) {
    glm::dvec3 total{0};
    const auto view = glm::normalize(camera.position - hit.point);
    const double nv = glm::dot(hit.normal, view);
    if (nv <= 0) return total;
    const auto& material = *hit.material;
    const double alpha = std::max(material.roughness * material.roughness, 0.001);
    const double alphaSquared = alpha * alpha;
    const glm::dvec3 f0 = (1.0 - material.metallic) * glm::dvec3(0.04) +
                          material.metallic * material.baseColor;
    for (const auto& sun : suns) {
        const auto light = glm::normalize(sun.directionToLight);
        const double nl = glm::dot(hit.normal, light);
        if (nl <= 0 || glm::length(view + light) == 0) continue;
        const auto half = glm::normalize(view + light);
        const double nh = std::clamp(glm::dot(hit.normal, half), 0.0, 1.0);
        const double vh = std::clamp(glm::dot(view, half), 0.0, 1.0);
        // 直接采用合同 double 公式，与产品的稳定 float 展开独立。
        const double denominator = nh * nh * (alphaSquared - 1.0) + 1.0;
        const double distribution = alphaSquared /
            (std::numbers::pi * denominator * denominator);
        const double visibility = 0.5 /
            (nl * std::sqrt(nv * nv * (1.0 - alphaSquared) + alphaSquared) +
             nv * std::sqrt(nl * nl * (1.0 - alphaSquared) + alphaSquared));
        const auto fresnel = f0 + (glm::dvec3(1) - f0) * std::pow(1.0 - vh, 5.0);
        const auto diffuse = (1.0 - material.metallic) * (glm::dvec3(1) - fresnel) *
                             material.baseColor / std::numbers::pi;
        total += (diffuse + distribution * visibility * fresnel) *
                  sun.effective.irradiance * sun.effective.color * nl;
    }
    return total;
}

struct Tolerance {
    double absolute = 0, relative = 0;
};

Tolerance toleranceFor(const Manifest& manifest, const QString& kind) {
    const auto values = manifest.json["tolerances"].toObject()[kind].toObject();
    return {number(values["absolute"]), number(values["relative"])};
}

bool isNear(double actual, double expected, const Tolerance& tolerance) {
    return std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= tolerance.absolute + tolerance.relative * std::abs(expected);
}

void requireOracle(const FrozenRenderPixels& pixels, const core::EvaluatedSceneState& state,
                    const Manifest& manifest) {
    const auto tolerance = toleranceFor(manifest, QStringLiteral("brdf"));
    const double minimumEdgeDistance = number(
        manifest.json["oracleMask"].toObject()["minimumProjectedEdgeDistancePixels"]);
    std::size_t compared = 0, background = 0;
    for (int y = kOracleOrigin; y < manifest.height; y += kOracleStride)
        for (int x = kOracleOrigin; x < manifest.width; x += kOracleStride) {
            const auto index = static_cast<std::size_t>(y) * manifest.width + x;
            const auto hit = tracePixel(state, manifest.camera, manifest.width, manifest.height, x, y);
            if (!hit) {
                const double edgeDistance = projectedEdgeDistancePixels(state, manifest, x, y);
                if (edgeDistance < minimumEdgeDistance) continue;
                const auto& actual = pixels.beauty[index];
                INFO("background pixel " << x << ", " << y << " RGBA "
                     << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w
                     << " coverage " << pixels.coverage[index]);
                INFO("nearest projected edge distance pixels " << edgeDistance);
                REQUIRE(pixels.beauty[index] == glm::vec4(0));
                REQUIRE(pixels.coverage[index] == 0);
                ++background;
                continue;
            }
            if (!isOracleInterior(*hit, manifest, x, y)) continue;
            const auto expected = referenceBrdf(*hit, manifest.camera, state.content->visibleSuns);
            INFO("pixel " << x << ", " << y);
            for (int channel = 0; channel < 3; ++channel) {
                INFO("channel " << channel << " actual " << pixels.beauty[index][channel]
                                << " expected " << expected[channel]);
                REQUIRE(isNear(pixels.beauty[index][channel], expected[channel], tolerance));
            }
            REQUIRE(pixels.beauty[index].w == 1);
            REQUIRE(pixels.coverage[index] == 1);
            ++compared;
        }
    REQUIRE(compared >= 16);
    REQUIRE(background >= 16);
}

void requireChanged(const FrozenRenderPixels& first, const FrozenRenderPixels& second,
                     const Manifest& manifest, const QString& property) {
    const auto roi = manifest.roi(property);
    const auto thresholds = manifest.json["tolerances"].toObject();
    const double threshold = number(thresholds["changedPixelRgbThreshold"]);
    std::size_t changed = 0;
    for (int y = roi[1]; y < roi[3]; ++y)
        for (int x = roi[0]; x < roi[2]; ++x) {
            const auto index = static_cast<std::size_t>(y) * manifest.width + x;
            bool different = false;
            for (int channel = 0; channel < 3; ++channel)
                different = different || std::abs(first.beauty[index][channel] -
                                                  second.beauty[index][channel]) > threshold;
            if (different) ++changed;
        }
    INFO(property.toStdString() << " changed RGB pixels: " << changed);
    REQUIRE(changed >= static_cast<std::size_t>(number(thresholds["minimumChangedPixels"])));
}
} // namespace

TEST_CASE("Frozen IR time samples drive actual PBR pixels without mutating authors",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    auto fixture = fixtureFor(manifest);
    addAnimatedValues(fixture, manifest);
    const auto beforeAnimation = fixture.animation;
    const auto beforeMaterial = *fixture.materials;
    const auto beforeCube = fixture.inputs.front().base;
    const auto beforeSun = *fixture.authors->nodes.at(manifest.sun).sun;
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, fixedColor(manifest), error));
    std::uint64_t evaluation = 1;
    for (const auto value : manifest.json["timeSamples"].toArray()) {
        const auto sample = value.toObject();
        const auto state = evaluate(fixture, manifest, number(sample["frame"]), evaluation++);
        REQUIRE(std::abs(state.find(manifest.cube)->localPosition.x - number(sample["x"])) <= 1.0e-12);
        REQUIRE(std::abs(state.content->materials.at(manifest.material).effective.roughness -
                         number(sample["roughness"])) <= 1.0e-12);
        REQUIRE(state.content->visibleSuns.size() == 1);
        REQUIRE(std::abs(state.content->visibleSuns.front().effective.irradiance -
                         number(sample["irradiance"])) <= 1.0e-12);
        REQUIRE(state.content->visibleSuns.front().directionToLight ==
                vector3(manifest.json["sun"].toObject()["directionToLight"]));
        const auto pixels = draw(renderer, state, manifest);
        requireOracle(pixels, state, manifest);
    }
    REQUIRE(renderer.completedRenderCount() == 3);
    REQUIRE(fixture.animation == beforeAnimation);
    REQUIRE(*fixture.materials == beforeMaterial);
    REQUIRE(fixture.inputs.front().base.position == beforeCube.position);
    REQUIRE(fixture.inputs.front().base.rotation == beforeCube.rotation);
    REQUIRE(fixture.inputs.front().base.scale == beforeCube.scale);
    REQUIRE(*fixture.authors->nodes.at(manifest.sun).sun == beforeSun);
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
    REQUIRE_FALSE(renderer.isInitialized());
    REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
}

TEST_CASE("Position roughness and irradiance independently change frozen ROI RGB pixels",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, fixedColor(manifest), error));
    const auto isolated = manifest.json["isolatedChanges"].toObject();
    for (const auto property : {QStringLiteral("position"), QStringLiteral("irradiance"),
                                QStringLiteral("roughness")}) {
        const auto values = isolated[property].toObject();
        auto fixture = fixtureFor(manifest, property == QStringLiteral("roughness"));
        std::array<FrozenRenderPixels, 2> images;
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            const double x = property == QStringLiteral("position") ? number(values["x"].toArray()[endpoint])
                : property == QStringLiteral("roughness") ? fixture.inputs.front().base.position.x : number(values["x"]);
            const double roughness = property == QStringLiteral("roughness")
                ? number(values["roughness"].toArray()[endpoint]) : number(values["roughness"]);
            const double irradiance = property == QStringLiteral("irradiance")
                ? number(values["irradiance"].toArray()[endpoint]) : number(values["irradiance"]);
            setFixtureValues(fixture, manifest, x, roughness, irradiance);
            const auto state = evaluate(fixture, manifest, 1, endpoint + 1);
            images[endpoint] = draw(renderer, state, manifest);
            requireOracle(images[endpoint], state, manifest);
        }
        requireChanged(images[0], images[1], manifest, property);
    }
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
}

TEST_CASE("GGX endpoints preserve authored roughness and visible Suns add without ambient",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, fixedColor(manifest), error));
    for (const double roughness : {0.0, 1.0}) {
        auto fixture = fixtureFor(manifest, true);
        setFixtureValues(fixture, manifest, 1, roughness, 1.5);
        const auto state = evaluate(fixture, manifest);
        requireOracle(draw(renderer, state, manifest), state, manifest);
        REQUIRE(state.content->materials.at(manifest.material).effective.roughness == roughness);
        REQUIRE(fixture.materials->find(manifest.material)->parameters.roughness == roughness);
    }
    // 固定相机下另验正面 r=0 的中央近法向高光，包含 HDR，不用换相机逃避正则化。
    auto frontalFixture = fixtureFor(manifest);
    setFixtureValues(frontalFixture, manifest, 1, 0, 1.5);
    const auto frontal = evaluate(frontalFixture, manifest);
    const auto frontalPixels = draw(renderer, frontal, manifest);
    requireOracle(frontalPixels, frontal, manifest);
    constexpr std::array<std::array<int, 2>, 2> centralPixels{{{633, 359}, {646, 360}}};
    const auto tolerance = toleranceFor(manifest, QStringLiteral("brdf"));
    for (const auto& coordinate : centralPixels) {
        const auto hit = tracePixel(frontal, manifest.camera, manifest.width, manifest.height,
                                    coordinate[0], coordinate[1]);
        REQUIRE(hit);
        REQUIRE(isOracleInterior(*hit, manifest, coordinate[0], coordinate[1]));
        const auto expected = referenceBrdf(*hit, manifest.camera, frontal.content->visibleSuns);
        const auto& actual = frontalPixels.beauty[
            static_cast<std::size_t>(coordinate[1]) * manifest.width + coordinate[0]];
        REQUIRE(actual.x > 1);
        for (int channel = 0; channel < 3; ++channel)
            REQUIRE(isNear(actual[channel], expected[channel], tolerance));
    }
    auto darkFixture = fixtureFor(manifest);
    setFixtureValues(darkFixture, manifest, 1, 0.5, 0);
    for (const bool reversed : {false, true}) {
        if (reversed) {
            darkFixture.authors->nodes.at(manifest.sun).sun->irradiance = 1.5;
            darkFixture.inputs[1].base.rotation = glm::angleAxis(glm::radians(180.0F), glm::vec3(0, 1, 0));
        }
        const auto state = evaluate(darkFixture, manifest);
        const auto pixels = draw(renderer, state, manifest);
        requireOracle(pixels, state, manifest);
        REQUIRE(std::all_of(pixels.beauty.begin(), pixels.beauty.end(),
                            [](const auto& pixel) { return glm::vec3(pixel) == glm::vec3(0); }));
    }
    auto unlitFixture = fixtureFor(manifest);
    setFixtureValues(unlitFixture, manifest, 1, 0.5, 1.5);
    unlitFixture.authors->nodes.at(manifest.sun).visible = false;
    const auto unlitState = evaluate(unlitFixture, manifest);
    REQUIRE(unlitState.content->visibleSuns.empty());
    requireOracle(draw(renderer, unlitState, manifest), unlitState, manifest);
    auto singleFixture = fixtureFor(manifest);
    setFixtureValues(singleFixture, manifest, 1, 0.5, 1);
    const auto singleState = evaluate(singleFixture, manifest);
    const auto single = draw(renderer, singleState, manifest);
    auto multipleFixture = fixtureFor(manifest);
    setFixtureValues(multipleFixture, manifest, 1, 0.5, 1);
    auto secondSun = multipleFixture.authors->nodes.at(manifest.sun);
    secondSun.entity = 22;
    secondSun.sun->irradiance = 2;
    multipleFixture.authors->nodes.emplace(22, secondSun);
    multipleFixture.inputs.push_back({22, 0, {}});
    core::SceneGeometryEntry empty;
    empty.entity = 22;
    multipleFixture.geometry->entries.emplace(22, empty);
    const auto multipleState = evaluate(multipleFixture, manifest);
    REQUIRE(multipleState.content->visibleSuns.size() == 2);
    const auto multiple = draw(renderer, multipleState, manifest);
    requireOracle(multiple, multipleState, manifest);
    bool additive = true;
    for (std::size_t index = 0; index < single.beauty.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel)
            additive = additive && isNear(multiple.beauty[index][channel],
                                           3.0 * single.beauty[index][channel], tolerance);
        additive = additive && multiple.beauty[index].w == single.beauty[index].w &&
                                multiple.coverage[index] == single.coverage[index];
    }
    REQUIRE(additive);
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
}

TEST_CASE("Legal overlapping mesh entities retain the single surface pixel oracle",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, fixedColor(manifest), error));
    auto fixture = fixtureFor(manifest);
    setFixtureValues(fixture, manifest, 1, 0.5, 1.5);
    const auto singleState = evaluate(fixture, manifest);
    requireOracle(draw(renderer, singleState, manifest), singleState, manifest);

    SECTION("Distinct entities with identical surfaces") {
        // mesh、world 和材质完全相同，合法的不同 entity 不能把同一表面重复计光。
        constexpr core::EntityId overlappingEntity = 42;
        auto overlappingAuthor = fixture.authors->nodes.at(manifest.cube);
        overlappingAuthor.entity = overlappingEntity;
        // producer 要求全场景槽 ID 唯一；独立槽仍引用相同材质。
        overlappingAuthor.materialSlots.front().id = core::MaterialSlotId{52};
        REQUIRE(fixture.authors->nodes.emplace(overlappingEntity, overlappingAuthor).second);
        auto overlappingGeometry = fixture.geometry->entries.at(manifest.cube);
        overlappingGeometry.entity = overlappingEntity;
        REQUIRE(fixture.geometry->entries.emplace(overlappingEntity, overlappingGeometry).second);
        auto overlappingInput = fixture.inputs.front();
        overlappingInput.entity = overlappingEntity;
        fixture.inputs.push_back(overlappingInput);
        const auto overlappingState = evaluate(fixture, manifest, 1, 2);
        REQUIRE(overlappingState.find(overlappingEntity));
        REQUIRE(overlappingState.find(overlappingEntity)->worldAffine ==
                singleState.find(manifest.cube)->worldAffine);
        REQUIRE(overlappingState.content->geometryStages.at(overlappingEntity).geometry.mesh ==
                singleState.content->geometryStages.at(manifest.cube).geometry.mesh);
        REQUIRE(overlappingState.find(overlappingEntity)->materialSlots.front().material ==
                singleState.find(manifest.cube)->materialSlots.front().material);
        const auto overlappingPixels = draw(renderer, overlappingState, manifest);
        requireOracle(overlappingPixels, singleState, manifest);
    }
    SECTION("Repeated triangles in one legal mesh") {
        auto repeated = std::make_shared<core::MeshData>(
            *fixture.geometry->entries.at(manifest.cube).mesh);
        const auto indices = repeated->indices;
        repeated->indices.insert(repeated->indices.end(), indices.begin(), indices.end());
        fixture.geometry->entries.at(manifest.cube).mesh = std::move(repeated);
        const auto repeatedState = evaluate(fixture, manifest, 1, 2);
        REQUIRE(repeatedState.content->geometryStages.at(manifest.cube).geometry.mesh->indices.size() ==
                2 * singleState.content->geometryStages.at(manifest.cube).geometry.mesh->indices.size());
        requireOracle(draw(renderer, repeatedState, manifest), singleState, manifest);
    }
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
    REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
}

namespace {
constexpr char kTransportVertex[] = R"glsl(#version 410 core
void main() {
    vec2 point = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(point, 0.0, 1.0);
}
)glsl";

void compileProgram(QOpenGLShaderProgram& program, const QByteArray& fragment) {
    const bool vertex = program.addShaderFromSourceCode(QOpenGLShader::Vertex, kTransportVertex);
    INFO(program.log().toStdString());
    REQUIRE(vertex);
    const bool pixel = program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment);
    INFO(program.log().toStdString());
    REQUIRE(pixel);
    const bool linked = program.link();
    INFO(program.log().toStdString());
    REQUIRE(linked);
}

void cleanTransfer(QOpenGLFunctions_4_1_Core& gl) {
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (const auto value : {GL_PACK_ALIGNMENT, GL_UNPACK_ALIGNMENT})
        gl.glPixelStorei(static_cast<GLenum>(value), 1);
    for (const auto value : {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
                              GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
                              GL_PACK_SWAP_BYTES, GL_UNPACK_SWAP_BYTES})
        gl.glPixelStorei(static_cast<GLenum>(value), 0);
    gl.glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
}

struct ColorTarget {
    QOpenGLFunctions_4_1_Core& gl;
    GLuint framebuffer = 0;
    std::array<GLuint, 2> textures{};
    bool dataAttachment = false;
    int width = 0, height = 0;

    ColorTarget(QOpenGLFunctions_4_1_Core& functions, int columns, int rows, bool data = false)
        : gl(functions), dataAttachment(data), width(columns), height(rows) {
        cleanTransfer(gl);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glGenFramebuffers(1, &framebuffer);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl.glGenTextures(data ? 2 : 1, textures.data());
        for (int index = 0; index < (data ? 2 : 1); ++index) {
            gl.glBindTexture(GL_TEXTURE_2D, textures[index]);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, index == 0 ? GL_RGBA32F : GL_R32UI,
                width, height, 0, index == 0 ? GL_RGBA : GL_RED_INTEGER,
                index == 0 ? GL_FLOAT : GL_UNSIGNED_INT, nullptr);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index,
                                      GL_TEXTURE_2D, textures[index], 0);
        }
        constexpr std::array<GLenum, 2> attachments{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        gl.glDrawBuffers(data ? 2 : 1, attachments.data());
        REQUIRE(gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
    }
    ~ColorTarget() {
        gl.glDeleteTextures(dataAttachment ? 2 : 1, textures.data());
        gl.glDeleteFramebuffers(1, &framebuffer);
    }
    std::vector<glm::vec4> readTopDown() {
        cleanTransfer(gl);
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        gl.glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::vector<glm::vec4> bottom(static_cast<std::size_t>(width) * height), result(bottom.size());
        gl.glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, bottom.data());
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
        for (int row = 0; row < height; ++row)
            std::copy_n(bottom.begin() + static_cast<std::size_t>(height - 1 - row) * width,
                         width, result.begin() + static_cast<std::size_t>(row) * width);
        return result;
    }
};

std::vector<glm::vec4> linearSamples(const Manifest& manifest) {
    std::vector<glm::vec4> samples;
    for (const auto value : manifest.json["linearColorSamples"].toArray()) {
        const auto array = value.toArray();
        REQUIRE(array.size() == 4);
        samples.emplace_back(static_cast<float>(number(array[0])), static_cast<float>(number(array[1])),
                             static_cast<float>(number(array[2])), static_cast<float>(number(array[3])));
    }
    REQUIRE(samples.size() == 4);
    return samples;
}

bool samePixelBits(std::span<const glm::vec4> left, std::span<const glm::vec4> right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        for (int component = 0; component < 4; ++component)
            if (std::bit_cast<std::uint32_t>(left[index][component]) !=
                std::bit_cast<std::uint32_t>(right[index][component])) return false;
    return true;
}

bool matchesColor(std::span<const glm::vec4> actual, std::span<const glm::vec4> expected,
                  const Manifest& manifest) {
    if (actual.size() != expected.size()) return false;
    const auto tolerance = toleranceFor(manifest, QStringLiteral("ocio"));
    for (std::size_t index = 0; index < actual.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel)
            if (!isNear(actual[index][channel], expected[index][channel], tolerance)) return false;
        if (std::bit_cast<std::uint32_t>(actual[index].w) !=
            std::bit_cast<std::uint32_t>(expected[index].w)) return false;
    }
    return true;
}

// GPU 仅运输预冻 synthetic RGBA/整数 data，实际编译产品 processor 生成的 shader。
std::vector<glm::vec4> gpuColorTransport(GpuContext& gpu, const OcioDisplayTransform& color,
                                         std::span<const glm::vec4> input,
                                         const std::array<std::uint32_t, 4>& data) {
    auto& gl = gpu.gl;
    ColorTarget target(gl, 4, 1, true);
    QOpenGLShaderProgram program;
    const QByteArray fragment = QByteArray("#version 410 core\n") + color.gpuShaderText() + R"glsl(
uniform sampler2D uColor;
uniform usampler2D uData;
layout(location=0) out vec4 colorOutput;
layout(location=1) out uint dataOutput;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 source = texelFetch(uColor, pixel, 0);
    colorOutput = vec4(mini3dOcioDisplay(source).rgb, source.a);
    dataOutput = texelFetch(uData, pixel, 0).r;
}
)glsl";
    compileProgram(program, fragment);
    std::array<GLuint, 2> inputTextures{};
    GLuint vao = 0;
    gl.glGenTextures(2, inputTextures.data());
    for (GLuint index = 0; index < 2; ++index) {
        gl.glActiveTexture(GL_TEXTURE0 + index);
        gl.glBindSampler(index, 0);
        gl.glBindTexture(GL_TEXTURE_2D, inputTextures[index]);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, index == 0 ? GL_RGBA32F : GL_R32UI, 4, 1, 0,
                        index == 0 ? GL_RGBA : GL_RED_INTEGER,
                        index == 0 ? GL_FLOAT : GL_UNSIGNED_INT,
                        index == 0 ? static_cast<const void*>(input.data()) : data.data());
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    gl.glViewport(0, 0, 4, 1);
    for (const auto capability : {GL_FRAMEBUFFER_SRGB, GL_DEPTH_TEST, GL_CULL_FACE,
                                   GL_SCISSOR_TEST, GL_DITHER, GL_MULTISAMPLE})
        gl.glDisable(static_cast<GLenum>(capability));
    for (GLuint index = 0; index < 2; ++index) {
        gl.glDisablei(GL_BLEND, index);
        gl.glColorMaski(index, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    REQUIRE(program.bind());
    program.setUniformValue("uColor", 0);
    program.setUniformValue("uData", 1);
    gl.glGenVertexArrays(1, &vao);
    gl.glBindVertexArray(vao);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
    auto pixels = target.readTopDown();
    std::array<std::uint32_t, 4> actualData{};
    gl.glReadBuffer(GL_COLOR_ATTACHMENT1);
    gl.glReadPixels(0, 0, 4, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, actualData.data());
    REQUIRE(actualData == data);
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
    gl.glBindVertexArray(0);
    program.release();
    gl.glDeleteVertexArrays(1, &vao);
    gl.glDeleteTextures(2, inputTextures.data());
    REQUIRE(gl.glIsVertexArray(vao) == GL_FALSE);
    for (const auto texture : inputTextures) REQUIRE(gl.glIsTexture(texture) == GL_FALSE);
    return pixels;
}
} // namespace

TEST_CASE("Actual OCIO CPU and GPU preserve signed HDR alpha and integer data",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    const auto color = fixedColor(manifest);
    const auto input = linearSamples(manifest);
    const auto unchanged = input;
    std::vector<glm::vec4> expected(input.size());
    QString error;
    REQUIRE(color->applyCpuRgba(input, expected, error));
    REQUIRE(samePixelBits(input, unchanged));
    for (std::size_t index = 0; index < input.size(); ++index)
        REQUIRE(std::bit_cast<std::uint32_t>(input[index].w) ==
                std::bit_cast<std::uint32_t>(expected[index].w));
    REQUIRE(expected.front().x < 0);
    REQUIRE(expected.front().z > 1);
    REQUIRE(expected.front().w == 0);
    REQUIRE(expected.front().z != 0);
    GpuContext gpu;
    const std::array<std::uint32_t, 4> data{0U, 0xffffffffU, 0x80000000U, 0x01234567U};
    const auto actual = gpuColorTransport(gpu, *color, input, data);
    REQUIRE(matchesColor(actual, expected, manifest));
    const auto controls = manifest.json["negativeControls"].toArray();
    REQUIRE(controls.size() == 4);
    for (const auto value : controls) {
        const auto control = value.toString();
        auto badInput = input;
        std::vector<glm::vec4> corrupted(input.size());
        if (control == QStringLiteral("double-display-transform")) {
            REQUIRE(color->applyCpuRgba(expected, corrupted, error));
        } else {
            for (auto& pixel : badInput) {
                if (control == QStringLiteral("clamp-negative-rgb")) {
                    for (int channel = 0; channel < 3; ++channel)
                        pixel[channel] = std::max(pixel[channel], 0.0F);
                } else if (control == QStringLiteral("clear-zero-alpha-rgb")) {
                    if (pixel.w == 0) pixel = glm::vec4(0);
                } else {
                    REQUIRE(control == QStringLiteral("multiply-associated-rgb-by-alpha"));
                    pixel = glm::vec4(glm::vec3(pixel) * pixel.w, pixel.w);
                }
            }
            REQUIRE(color->applyCpuRgba(badInput, corrupted, error));
        }
        INFO(control.toStdString());
        REQUIRE_FALSE(matchesColor(corrupted, expected, manifest));
    }
    auto invalid = input;
    invalid[1].x = std::numeric_limits<float>::quiet_NaN();
    auto unpublished = unchanged;
    REQUIRE_FALSE(color->applyCpuRgba(invalid, unpublished, error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(samePixelBits(unpublished, unchanged));
    REQUIRE_FALSE(OcioDisplayTransform::createFixedStandard(
        QString::fromUtf8(MINI3D_J1_RENDER_MANIFEST), error));
    REQUIRE_FALSE(error.isEmpty());
}

TEST_CASE("Product display composites once in linear domain without altering raw master",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    const auto color = fixedColor(manifest);
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, color, error));
    auto fixture = fixtureFor(manifest);
    setFixtureValues(fixture, manifest, 1, 0.5, 1.5);
    auto pixels = draw(renderer, evaluate(fixture, manifest), manifest);
    const auto samples = linearSamples(manifest);
    // 保持固定1280×720；头尾两行放不同synthetic样本，同时检验TopDown显示方向。
    for (int index = 0; index < 4; ++index) {
        pixels.beauty[index] = samples[index];
        pixels.beauty[pixels.beauty.size() - 4 + index] = samples[3 - index];
    }
    const auto before = pixels.beauty;
    const auto beforeCoverage = pixels.coverage;
    ColorTarget target(gpu.gl, manifest.width, manifest.height);
    REQUIRE(renderer.present(pixels, target.framebuffer, error));
    const auto displayed = target.readTopDown();
    auto opaque = before;
    for (auto& pixel : opaque) pixel.w = 1;
    std::vector<glm::vec4> expected(opaque.size());
    REQUIRE(color->applyCpuRgba(opaque, expected, error));
    REQUIRE(matchesColor(displayed, expected, manifest));
    REQUIRE(samePixelBits(pixels.beauty, before));
    REQUIRE(pixels.coverage == beforeCoverage);
    REQUIRE(displayed.front().x < 0);
    REQUIRE(displayed.front().z > 1);
    REQUIRE(displayed.front().w == 1);
    auto corrupt = pixels;
    corrupt.coverage[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(renderer.present(corrupt, target.framebuffer, error));
    REQUIRE_FALSE(error.isEmpty());
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
    REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
}

namespace {
struct GlSnapshot {
    std::vector<GLint> values;
    bool operator==(const GlSnapshot&) const = default;
};

// 测试自己查询驱动，不读取产品 guard 或模拟状态。
GlSnapshot snapshot(QOpenGLFunctions_4_1_Core& gl) {
    GlSnapshot result;
    const auto append = [&](GLenum field, int count = 1) {
        std::array<GLint, 4> values{};
        gl.glGetIntegerv(field, values.data());
        result.values.insert(result.values.end(), values.begin(), values.begin() + count);
    };
    for (const auto field : {GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_FRAMEBUFFER_BINDING,
                              GL_CURRENT_PROGRAM, GL_VERTEX_ARRAY_BINDING, GL_ARRAY_BUFFER_BINDING,
                              GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_RENDERBUFFER_BINDING,
                              GL_PIXEL_PACK_BUFFER_BINDING, GL_PIXEL_UNPACK_BUFFER_BINDING,
                              GL_TEXTURE_BUFFER_BINDING,
                              GL_ACTIVE_TEXTURE, GL_DEPTH_FUNC, GL_CLAMP_READ_COLOR,
                              GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
                              GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
                              GL_UNPACK_SKIP_PIXELS, GL_PACK_SWAP_BYTES, GL_UNPACK_SWAP_BYTES})
        append(static_cast<GLenum>(field));
    append(GL_VIEWPORT, 4);
    append(GL_SCISSOR_BOX, 4);
    append(GL_POLYGON_MODE, 2);
    GLint active = 0;
    gl.glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    for (const GLuint unit : {0U, 3U}) {
        gl.glActiveTexture(GL_TEXTURE0 + unit);
        append(GL_TEXTURE_BINDING_2D);
        append(GL_TEXTURE_BINDING_BUFFER);
        GLint sampler = 0;
        gl.glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &sampler);
        result.values.push_back(sampler);
    }
    gl.glActiveTexture(static_cast<GLenum>(active));
    GLboolean depthMask = GL_FALSE;
    gl.glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    result.values.push_back(depthMask);
    for (const auto capability : {GL_FRAMEBUFFER_SRGB, GL_DEPTH_TEST, GL_CULL_FACE,
                                   GL_SCISSOR_TEST, GL_MULTISAMPLE, GL_DITHER,
                                   GL_RASTERIZER_DISCARD})
        result.values.push_back(gl.glIsEnabled(static_cast<GLenum>(capability)));
    for (GLuint attachment = 0; attachment < 2; ++attachment) {
        std::array<GLboolean, 4> mask{};
        gl.glGetBooleani_v(GL_COLOR_WRITEMASK, attachment, mask.data());
        for (const auto value : mask) result.values.push_back(value);
        result.values.push_back(gl.glIsEnabledi(GL_BLEND, attachment));
        for (const auto field : {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA,
                                  GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA}) {
            GLint value = 0;
            gl.glGetIntegeri_v(static_cast<GLenum>(field), attachment, &value);
            result.values.push_back(value);
        }
    }
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
    return result;
}

struct HostObjects {
    QOpenGLFunctions_4_1_Core& gl;
    QOpenGLShaderProgram program;
    GLuint vao = 0, renderbuffer = 0, bufferTexture = 0;
    std::array<GLuint, 5> buffers{};
    std::array<GLuint, 2> textures{}, samplers{};

    explicit HostObjects(QOpenGLFunctions_4_1_Core& functions) : gl(functions) {
        compileProgram(program, R"glsl(#version 410 core
layout(location=0) out vec4 color;
void main() { color = vec4(0.25, 0.5, 0.75, 1.0); }
)glsl");
        gl.glGenVertexArrays(1, &vao);
        gl.glBindVertexArray(vao);
        gl.glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        constexpr std::array<GLenum, 5> targets{
            GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_PIXEL_PACK_BUFFER, GL_PIXEL_UNPACK_BUFFER,
            GL_TEXTURE_BUFFER};
        for (std::size_t index = 0; index < buffers.size(); ++index) {
            gl.glBindBuffer(targets[index], buffers[index]);
            gl.glBufferData(targets[index], 64, nullptr, GL_STATIC_DRAW);
        }
        gl.glGenTextures(2, textures.data());
        gl.glGenSamplers(2, samplers.data());
        for (std::size_t index = 0; index < textures.size(); ++index) {
            const GLuint unit = index == 0 ? 0 : 3;
            gl.glActiveTexture(GL_TEXTURE0 + unit);
            gl.glBindTexture(GL_TEXTURE_2D, textures[index]);
            gl.glBindSampler(unit, samplers[index]);
        }
        gl.glGenTextures(1, &bufferTexture);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_BUFFER, bufferTexture);
        gl.glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffers.back());
        REQUIRE(bufferTexture != 0);
        REQUIRE(buffers.back() != 0);
        gl.glActiveTexture(GL_TEXTURE3);
        gl.glGenRenderbuffers(1, &renderbuffer);
        gl.glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        REQUIRE(program.bind());
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
    }
    ~HostObjects() {
        program.release();
        gl.glDeleteVertexArrays(1, &vao);
        gl.glDeleteRenderbuffers(1, &renderbuffer);
        gl.glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        gl.glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        gl.glDeleteTextures(1, &bufferTexture);
        gl.glDeleteSamplers(static_cast<GLsizei>(samplers.size()), samplers.data());
    }

    void makeHostile(GLuint drawFramebuffer, GLuint readFramebuffer) {
        gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        gl.glViewport(7, 9, 23, 17);
        gl.glScissor(3, 4, 5, 6);
        for (const auto capability : {GL_FRAMEBUFFER_SRGB, GL_DEPTH_TEST, GL_CULL_FACE,
                                       GL_SCISSOR_TEST, GL_MULTISAMPLE, GL_DITHER,
                                       GL_RASTERIZER_DISCARD})
            gl.glEnable(static_cast<GLenum>(capability));
        gl.glDepthFunc(GL_GREATER);
        gl.glDepthMask(GL_FALSE);
        gl.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        gl.glClampColor(GL_CLAMP_READ_COLOR, GL_TRUE);
        gl.glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        gl.glColorMaski(1, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        gl.glEnablei(GL_BLEND, 0);
        gl.glDisablei(GL_BLEND, 1);
        gl.glBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        gl.glBlendEquationSeparatei(0, GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        gl.glBlendFuncSeparatei(1, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_ONE, GL_ZERO);
        gl.glBlendEquationSeparatei(1, GL_MAX, GL_MIN);
        for (const auto field : {GL_PACK_ALIGNMENT, GL_UNPACK_ALIGNMENT})
            gl.glPixelStorei(static_cast<GLenum>(field), 8);
        for (const auto field : {GL_PACK_ROW_LENGTH, GL_UNPACK_ROW_LENGTH})
            gl.glPixelStorei(static_cast<GLenum>(field), 13);
        for (const auto field : {GL_PACK_SKIP_ROWS, GL_UNPACK_SKIP_ROWS})
            gl.glPixelStorei(static_cast<GLenum>(field), 3);
        for (const auto field : {GL_PACK_SKIP_PIXELS, GL_UNPACK_SKIP_PIXELS})
            gl.glPixelStorei(static_cast<GLenum>(field), 2);
        gl.glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
        gl.glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
    }
};

template <class Mutation>
core::EvaluatedSceneState corruptedState(const core::EvaluatedSceneState& good, Mutation mutate) {
    auto changed = std::make_shared<core::EvaluatedSceneContent>(*good.content);
    mutate(*changed);
    auto state = good;
    state.content = std::move(changed);
    return state;
}
} // namespace

TEST_CASE("Frozen render and present restore actual hostile host OpenGL state",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, fixedColor(manifest), error));
    ColorTarget destination(gpu.gl, manifest.width, manifest.height);
    ColorTarget readHost(gpu.gl, 8, 8);
    HostObjects host(gpu.gl);
    host.makeHostile(destination.framebuffer, readHost.framebuffer);
    const auto before = snapshot(gpu.gl);
    auto fixture = fixtureFor(manifest);
    setFixtureValues(fixture, manifest, 1, 0.5, 1.5);
    const auto state = evaluate(fixture, manifest);
    const auto pixels = draw(renderer, state, manifest);
    REQUIRE(snapshot(gpu.gl) == before);
    requireOracle(pixels, state, manifest);
    REQUIRE(renderer.present(pixels, destination.framebuffer, error));
    REQUIRE(snapshot(gpu.gl) == before);
    const auto actual = destination.readTopDown();
    auto opaque = pixels.beauty;
    for (auto& pixel : opaque) pixel.w = 1;
    std::vector<glm::vec4> expected(opaque.size());
    REQUIRE(fixedColor(manifest)->applyCpuRgba(opaque, expected, error));
    REQUIRE(matchesColor(actual, expected, manifest));
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
}

TEST_CASE("Wrong context cannot consume or destroy owned native GPU resources",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    GpuContext owner;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(owner.gl, fixedColor(manifest), error));
    const auto state = evaluate(fixtureFor(manifest), manifest);
    const auto before = draw(renderer, state, manifest);
    const auto count = renderer.completedRenderCount();
    {
        GpuContext other;
        REQUIRE(QOpenGLContext::currentContext() == &other.context);
        REQUIRE_FALSE(renderer.render(state, manifest.camera, manifest.width, manifest.height, error));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE_FALSE(renderer.present(before, 0, error));
        REQUIRE_FALSE(error.isEmpty());
        renderer.destroy();
        REQUIRE(renderer.isInitialized());
        REQUIRE_FALSE(renderer.hasVerifiedRelease());
        REQUIRE(renderer.completedRenderCount() == count);
        REQUIRE(other.gl.glGetError() == GL_NO_ERROR);
    }
    REQUIRE(owner.context.makeCurrent(&owner.surface));
    const auto after = draw(renderer, state, manifest);
    REQUIRE(samePixelBits(after.beauty, before.beauty));
    REQUIRE(after.coverage == before.coverage);
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
    REQUIRE_FALSE(renderer.isInitialized());
    REQUIRE(owner.gl.glGetError() == GL_NO_ERROR);
}

TEST_CASE("Unsupported and corrupted frozen inputs reject atomically without old success",
          "[gpu][j1-static-render]") {
    const Manifest manifest;
    GpuContext gpu;
    renderer_gl::FrozenStateRenderer renderer;
    QString error;
    REQUIRE(renderer.initialize(gpu.gl, fixedColor(manifest), error));
    const auto good = evaluate(fixtureFor(manifest), manifest);
    const auto old = draw(renderer, good, manifest);
    const auto count = renderer.completedRenderCount();
    const auto reject = [&](const core::EvaluatedSceneState& state) {
        REQUIRE_FALSE(renderer.render(state, manifest.camera, manifest.width, manifest.height, error));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE(renderer.completedRenderCount() == count);
    };
    const auto unsupported = manifest.json["unsupported"].toArray();
    REQUIRE(unsupported.size() == 4);
    for (const auto value : unsupported) {
        const auto kind = value.toString();
        auto fixture = fixtureFor(manifest);
        if (kind == QStringLiteral("missing-slot")) {
            fixture.authors->nodes.at(manifest.cube).materialSlots.clear();
        } else if (kind == QStringLiteral("multiple-slots")) {
            fixture.authors->nodes.at(manifest.cube).materialSlots.push_back(
                {core::MaterialSlotId{52}, manifest.material});
        } else if (kind == QStringLiteral("coverage-other-than-one")) {
            fixture.materials->assets.at(core::StableAssetId{manifest.material.value})
                .material->parameters.coverage = 0.5;
        } else {
            REQUIRE(kind == QStringLiteral("declared-texture"));
            fixture.materials->assets.at(core::StableAssetId{manifest.material.value})
                .material->textures.push_back({core::MaterialTextureRole::BaseColor, core::StableAssetId{32}});
            const auto producer = core::evaluateSceneState(fixture.inputs, fixture.animation,
                fixture.authors, fixture.geometry, sourceFor(manifest, 1));
            REQUIRE_FALSE(producer.state);
            REQUIRE(producer.validation.error == core::EvaluatedSceneError::MissingResource);
            // 故意损坏IR另验消费者防线，不声称producer支持未解析纹理。
            reject(corruptedState(good, [&](auto& content) {
                content.materials.at(manifest.material).author.textures.push_back(
                    {core::MaterialTextureRole::BaseColor, core::StableAssetId{32}});
            }));
            continue;
        }
        INFO(kind.toStdString());
        reject(evaluate(fixture, manifest));
        REQUIRE(error.startsWith(QStringLiteral("Unsupported")));
    }
    reject(corruptedState(good, [&](auto& content) { content.geometryStages.erase(manifest.sun); }));
    reject(corruptedState(good, [&](auto& content) {
        ++content.geometryStages.at(manifest.cube).identity.contentVersion;
    }));
    reject(corruptedState(good, [&](auto& content) {
        auto mesh = std::make_shared<core::MeshData>(*content.geometryStages.at(manifest.cube).geometry.mesh);
        mesh->indices[0] = static_cast<std::uint32_t>(mesh->vertices.size());
        content.geometryStages.at(manifest.cube).geometry.mesh = std::move(mesh);
    }));
    reject(corruptedState(good, [&](auto& content) {
        content.materials.at(manifest.material).effective.roughness = -0.01;
    }));
    reject(corruptedState(good, [&](auto& content) {
        content.visibleSuns.front().effective.irradiance = std::numeric_limits<double>::max();
    }));
    reject(corruptedState(good, [&](auto& content) {
        content.visibleSuns.front().directionToLight = glm::dvec3(0);
    }));
    // 作者域允许的FLOAT32上限Sun仍可能在实际高光乘法中溢出；不能发布旧母版。
    auto overflowFixture = fixtureFor(manifest);
    setFixtureValues(overflowFixture, manifest, 1, 0, std::numeric_limits<float>::max());
    reject(evaluate(overflowFixture, manifest));
    auto missing = good;
    missing.content.reset();
    reject(missing);
    auto invalidSource = good;
    invalidSource.source.contentRevision = 0;
    reject(invalidSource);
    auto singularCamera = manifest.camera;
    singularCamera.projectionMatrix = glm::dmat4(0);
    REQUIRE_FALSE(renderer.render(good, singularCamera, manifest.width, manifest.height, error));
    REQUIRE_FALSE(renderer.render(good, manifest.camera, 0, manifest.height, error));
    REQUIRE_FALSE(renderer.render(good, manifest.camera, 4096, 4096, error));
    REQUIRE(renderer.completedRenderCount() == count);
    const auto recovered = draw(renderer, good, manifest);
    REQUIRE(samePixelBits(old.beauty, recovered.beauty));
    REQUIRE(old.coverage == recovered.coverage);
    renderer.destroy();
    REQUIRE(renderer.hasVerifiedRelease());
}
