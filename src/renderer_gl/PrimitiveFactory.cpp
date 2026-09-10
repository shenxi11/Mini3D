/*
 * 模块名: PrimitiveFactory
 * 功能概述: 实现固定 Cube、UV Sphere 和 XZ Plane 的顶点、法线、颜色与索引生成。
 * 对外接口: mini3d::renderer_gl::PrimitiveFactory
 * 依赖关系: MeshData、GLM、C++ 数学库
 * 输入输出: 输出可由 GpuMesh 直接上传的三角形列表数据。
 * 异常与错误: 无运行时异常；Sphere 使用独立极点避免零面积三角形。
 * 维护说明: 全部三角形从模型外侧观察为 CCW，适配 Renderer 的背面剔除状态。
 */

#include "PrimitiveFactory.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/vec3.hpp>
#include <numbers>

namespace mini3d::renderer_gl {
namespace {

constexpr float kPrimitiveRadius = 0.5F;
constexpr int kSphereLatitudeSegments = 16;
constexpr int kSphereLongitudeSegments = 24;

void appendFace(MeshData& meshData, const std::array<glm::vec3, 4>& positions,
                const glm::vec3& normal, const glm::vec3& color) {
    const auto firstVertex = static_cast<std::uint32_t>(meshData.vertices.size());
    const std::array<glm::vec2, 4> uvs{{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        meshData.vertices.push_back(MeshVertex{positions[index], normal, color, uvs[index]});
    }

    meshData.indices.insert(meshData.indices.end(),
                            {firstVertex, firstVertex + 1, firstVertex + 2, firstVertex,
                             firstVertex + 2, firstVertex + 3});
}

} // namespace

MeshData PrimitiveFactory::createCube() {
    MeshData meshData;
    meshData.vertices.reserve(24);
    meshData.indices.reserve(36);

    constexpr float low = -kPrimitiveRadius;
    constexpr float high = kPrimitiveRadius;
    appendFace(meshData,
               {{{low, low, high}, {high, low, high}, {high, high, high}, {low, high, high}}},
               {0.0F, 0.0F, 1.0F}, {0.22F, 0.58F, 0.95F});
    appendFace(meshData, {{{high, low, low}, {low, low, low}, {low, high, low}, {high, high, low}}},
               {0.0F, 0.0F, -1.0F}, {0.15F, 0.38F, 0.72F});
    appendFace(meshData,
               {{{high, low, high}, {high, low, low}, {high, high, low}, {high, high, high}}},
               {1.0F, 0.0F, 0.0F}, {0.95F, 0.42F, 0.18F});
    appendFace(meshData, {{{low, low, low}, {low, low, high}, {low, high, high}, {low, high, low}}},
               {-1.0F, 0.0F, 0.0F}, {0.72F, 0.24F, 0.18F});
    appendFace(meshData,
               {{{low, high, high}, {high, high, high}, {high, high, low}, {low, high, low}}},
               {0.0F, 1.0F, 0.0F}, {0.98F, 0.72F, 0.18F});
    appendFace(meshData, {{{low, low, low}, {high, low, low}, {high, low, high}, {low, low, high}}},
               {0.0F, -1.0F, 0.0F}, {0.32F, 0.24F, 0.18F});
    return meshData;
}

MeshData PrimitiveFactory::createSphere() {
    // 接缝两侧的位置相同但 U 分别为 0 和 1，避免三角形横跨整张纹理。
    constexpr int rowSize = kSphereLongitudeSegments + 1;
    MeshData meshData;
    meshData.vertices.reserve((kSphereLatitudeSegments + 1) * rowSize);
    meshData.indices.reserve(6 * kSphereLongitudeSegments * (kSphereLatitudeSegments - 1));
    const glm::vec3 sphereColor(0.24F, 0.76F, 0.62F);
    for (int latitude = 0; latitude <= kSphereLatitudeSegments; ++latitude) {
        const float v = static_cast<float>(latitude) / kSphereLatitudeSegments;
        const float phi = std::numbers::pi_v<float> * v;
        for (int longitude = 0; longitude <= kSphereLongitudeSegments; ++longitude) {
            const float u = static_cast<float>(longitude) / kSphereLongitudeSegments;
            const float theta = 2.0F * std::numbers::pi_v<float> * u;
            glm::vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi),
                             std::sin(phi) * std::sin(theta));
            if (latitude == 0 || latitude == kSphereLatitudeSegments) {
                normal = {0.0F, latitude == 0 ? 1.0F : -1.0F, 0.0F};
            }
            meshData.vertices.push_back(
                MeshVertex{normal * kPrimitiveRadius, normal, sphereColor, {u, 1.0F - v}});
        }
    }
    for (int latitude = 0; latitude < kSphereLatitudeSegments; ++latitude) {
        for (int longitude = 0; longitude < kSphereLongitudeSegments; ++longitude) {
            const auto upper = static_cast<std::uint32_t>(latitude * rowSize + longitude);
            const auto lower = upper + rowSize;
            // 极点只保留非退化的一半三角形。
            if (latitude != 0) {
                meshData.indices.insert(meshData.indices.end(), {upper, upper + 1, lower});
            }
            if (latitude != kSphereLatitudeSegments - 1) {
                meshData.indices.insert(meshData.indices.end(), {upper + 1, lower + 1, lower});
            }
        }
    }
    return meshData;
}

MeshData PrimitiveFactory::createPlane() {
    constexpr float halfExtent = 0.6F;
    const glm::vec3 normal(0.0F, 1.0F, 0.0F);
    const glm::vec3 planeColor(0.42F, 0.34F, 0.88F);

    MeshData meshData;
    meshData.vertices = {
        {{-halfExtent, 0.0F, -halfExtent}, normal, planeColor, {0.0F, 1.0F}},
        {{-halfExtent, 0.0F, halfExtent}, normal, planeColor, {0.0F, 0.0F}},
        {{halfExtent, 0.0F, halfExtent}, normal, planeColor, {1.0F, 0.0F}},
        {{halfExtent, 0.0F, -halfExtent}, normal, planeColor, {1.0F, 1.0F}},
    };
    meshData.indices = {0, 1, 2, 0, 2, 3};
    return meshData;
}

} // namespace mini3d::renderer_gl
