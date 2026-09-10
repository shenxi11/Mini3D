/*
 * 模块名: GltfImporter
 * 功能概述: 使用 fastgltf 验证并转换静态三角形、节点和基础材质。
 * 对外接口: GltfImporter::read
 * 依赖关系: fastgltf、GLM、Qt Gui 图片解码
 * 输入输出: 输入本机 glTF/GLB 文件，输出暂存 CPU 资源与节点图。
 * 异常与错误: 字节边界、格式和层级异常统一转为带路径/阶段的错误。
 * 维护说明: Accessor 迭代前检查实际缓冲区，避免第三方工具内部断言越界。
 */
#include "GltfImporter.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <filesystem>
#include <functional>
#include <numeric>
#include <span>
#include <stdexcept>

namespace mini3d::assets {
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
std::span<const std::byte> bytesOf(const fastgltf::DataSource& source) {
    if (const auto* data = std::get_if<fastgltf::sources::Array>(&source)) {
        return {data->bytes.data(), data->bytes.size()};
    }
    throw std::runtime_error("缓冲区或图像数据未载入");
}
std::span<const std::byte> viewBytes(const fastgltf::Asset& asset, std::size_t index) {
    require(index < asset.bufferViews.size(), "缓冲区视图（bufferView）索引越界");
    const auto& view = asset.bufferViews[index];
    require(view.bufferIndex < asset.buffers.size(), "缓冲区索引越界");
    const auto& buffer = asset.buffers[view.bufferIndex];
    const auto bytes = bytesOf(buffer.data);
    require(view.byteOffset <= buffer.byteLength &&
                view.byteLength <= buffer.byteLength - view.byteOffset &&
                view.byteOffset <= bytes.size() &&
                view.byteLength <= bytes.size() - view.byteOffset,
            "缓冲区视图超出声明或实际缓冲区长度");
    require(!view.meshoptCompression, "不支持 Meshopt 压缩");
    return bytes.subspan(view.byteOffset, view.byteLength);
}
const fastgltf::Accessor& accessor(const fastgltf::Asset& asset, std::size_t index,
                                   fastgltf::AccessorType type) {
    require(index < asset.accessors.size(), "访问器（accessor）索引越界");
    const auto& value = asset.accessors[index];
    require(value.type == type, "访问器数据形状不匹配");
    require(!value.sparse.has_value(), "当前导入器不支持稀疏访问器");
    require(value.bufferViewIndex.has_value(), "访问器缺少缓冲区视图");
    const auto bytes = viewBytes(asset, *value.bufferViewIndex);
    const auto& view = asset.bufferViews[*value.bufferViewIndex];
    const auto elementSize = fastgltf::getElementByteSize(value.type, value.componentType);
    const std::size_t stride = view.byteStride.value_or(elementSize);
    require(value.count > 0 && elementSize > 0 && stride >= elementSize, "访问器数量或步长无效");
    require(value.byteOffset <= bytes.size() && elementSize <= bytes.size() - value.byteOffset,
            "访问器超出缓冲区视图");
    require(value.count - 1 <= (bytes.size() - value.byteOffset - elementSize) / stride,
            "访问器数量或步长超出缓冲区视图");
    return value;
}
bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
core::Transform readTransform(const fastgltf::Node& node) {
    core::Transform result;
    if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
        result.position = {trs->translation[0], trs->translation[1], trs->translation[2]};
        result.rotation = {trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
        result.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
    } else {
        const auto& source = std::get<fastgltf::math::fmat4x4>(node.transform);
        glm::mat4 matrix(1);
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                matrix[col][row] = source[col][row];
            }
        }
        result.position = glm::vec3(matrix[3]);
        for (int axis = 0; axis < 3; ++axis) {
            result.scale[axis] = glm::length(glm::vec3(matrix[axis]));
        }
        if (glm::determinant(glm::mat3(matrix)) < 0) {
            result.scale.x = -result.scale.x;
        }
        require(result.isValid(), "矩阵包含奇异或非有限缩放");
        const glm::mat3 rotation(glm::vec3(matrix[0]) / result.scale.x,
                                 glm::vec3(matrix[1]) / result.scale.y,
                                 glm::vec3(matrix[2]) / result.scale.z);
        result.rotation = glm::quat_cast(rotation);
        const auto reconstructed = result.localMatrix();
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                require(std::isfinite(matrix[col][row]) &&
                            std::abs(matrix[col][row] - reconstructed[col][row]) <=
                                1.0e-4F * std::max(1.0F, std::abs(matrix[col][row])),
                        "矩阵无法表示为仿射 TRS（包含剪切或透视）");
            }
        }
    }
    require(result.isValid(), "对象变换无效（TRS 须为有限数值且缩放绝对值不得小于 0.001）");
    result.rotation = glm::normalize(result.rotation);
    return result;
}
QImage decodeImage(const fastgltf::Asset& asset, const fastgltf::Image& image) {
    std::span<const std::byte> bytes;
    if (const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
        bytes = viewBytes(asset, view->bufferViewIndex);
    } else {
        bytes = bytesOf(image.data);
    }
    QByteArray encoded(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<qsizetype>(bytes.size()));
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg") {
        return {};
    }
    return reader.read();
}
MeshAsset readPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive) {
    require(primitive.type == fastgltf::PrimitiveType::Triangles, "仅支持三角形（TRIANGLES）");
    require(primitive.targets.empty() && !primitive.dracoCompression,
            "不支持形态目标或 Draco 压缩");
    const auto positions = primitive.findAttribute("POSITION");
    require(positions != primitive.attributes.end(), "子网格缺少顶点位置（POSITION）");
    const auto& position = accessor(asset, positions->accessorIndex, fastgltf::AccessorType::Vec3);
    require(position.componentType == fastgltf::ComponentType::Float && !position.normalized,
            "顶点位置（POSITION）必须为浮点三维向量");
    require(position.count <= std::numeric_limits<std::uint32_t>::max(), "顶点数量过多");
    MeshAsset output;
    output.data.vertices.resize(position.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        asset, position, [&](glm::vec3 point, std::size_t i) {
            require(finite(point), "顶点位置（POSITION）包含非有限数值");
            output.data.vertices[i].position = point;
            output.data.vertices[i].normal = glm::vec3(0);
        });
    if (primitive.indicesAccessor) {
        const auto& indices =
            accessor(asset, *primitive.indicesAccessor, fastgltf::AccessorType::Scalar);
        require(!indices.normalized &&
                    (indices.componentType == fastgltf::ComponentType::UnsignedByte ||
                     indices.componentType == fastgltf::ComponentType::UnsignedShort ||
                     indices.componentType == fastgltf::ComponentType::UnsignedInt),
                "索引分量类型无效");
        fastgltf::iterateAccessor<std::uint32_t>(asset, indices, [&](std::uint32_t index) {
            require(index < position.count, "索引超出顶点数组");
            output.data.indices.push_back(index);
        });
    } else {
        output.data.indices.resize(position.count);
        std::iota(output.data.indices.begin(), output.data.indices.end(), 0U);
    }
    require(!output.data.indices.empty() && output.data.indices.size() % 3 == 0,
            "三角形索引不能为空且数量须为 3 的倍数");
    const auto normals = primitive.findAttribute("NORMAL");
    if (normals != primitive.attributes.end()) {
        const auto& normal = accessor(asset, normals->accessorIndex, fastgltf::AccessorType::Vec3);
        require(normal.componentType == fastgltf::ComponentType::Float &&
                    normal.count == position.count,
                "法线（NORMAL）必须为浮点三维向量且数量与顶点一致");
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset, normal, [&](glm::vec3 value, std::size_t i) {
                require(finite(value) && glm::length(value) > 1.0e-8F, "法线（NORMAL）无效");
                output.data.vertices[i].normal = glm::normalize(value);
            });
    } else {
        for (std::size_t i = 0; i < output.data.indices.size(); i += 3) {
            auto& a = output.data.vertices[output.data.indices[i]];
            auto& b = output.data.vertices[output.data.indices[i + 1]];
            auto& c = output.data.vertices[output.data.indices[i + 2]];
            const auto normal = glm::cross(b.position - a.position, c.position - a.position);
            a.normal += normal;
            b.normal += normal;
            c.normal += normal;
        }
        for (auto& vertex : output.data.vertices) {
            require(finite(vertex.normal) && glm::length(vertex.normal) > 1.0e-8F,
                    "无法为退化或未使用的顶点生成法线");
            vertex.normal = glm::normalize(vertex.normal);
        }
    }
    const auto uvs = primitive.findAttribute("TEXCOORD_0");
    if (uvs != primitive.attributes.end()) {
        const auto& uv = accessor(asset, uvs->accessorIndex, fastgltf::AccessorType::Vec2);
        require(uv.count == position.count, "纹理坐标（TEXCOORD_0）数量不匹配");
        require(uv.componentType == fastgltf::ComponentType::Float ||
                    ((uv.componentType == fastgltf::ComponentType::UnsignedByte ||
                      uv.componentType == fastgltf::ComponentType::UnsignedShort) &&
                     uv.normalized),
                "不支持此纹理坐标（TEXCOORD_0）分量类型");
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset, uv, [&](glm::vec2 value, std::size_t i) {
                require(std::isfinite(value.x) && std::isfinite(value.y), "UV 包含非有限数值");
                output.data.vertices[i].uv = {value.x, 1.0F - value.y};
            });
    }
    if (primitive.materialIndex) {
        require(*primitive.materialIndex < asset.materials.size(), "材质索引越界");
        output.material = *primitive.materialIndex + 1;
        if (asset.materials[*primitive.materialIndex].pbrData.baseColorTexture) {
            require(uvs != primitive.attributes.end(), "带纹理的子网格缺少纹理坐标（TEXCOORD_0）");
        }
    }
    return output;
}
} // namespace
ImportData GltfImporter::read(const QString& path) {
    ImportData result;
    QString stage = QStringLiteral("解析");
    try {
        const auto suffix = QFileInfo(path).suffix().toLower();
        require(suffix == "glb" || suffix == "gltf", "需要 .glb 或 .gltf 文件");
        const std::filesystem::path source(path.toStdWString());
        auto data = fastgltf::GltfDataBuffer::FromPath(source);
        require(data.error() == fastgltf::Error::None, "无法读取源文件");
        fastgltf::Parser parser;
        auto parsed = parser.loadGltf(data.get(), source.parent_path(),
                                      fastgltf::Options::LoadExternalBuffers |
                                          fastgltf::Options::LoadExternalImages);
        if (parsed.error() != fastgltf::Error::None) {
            throw std::runtime_error(std::string(fastgltf::getErrorMessage(parsed.error())));
        }
        const auto& asset = parsed.get();
        stage = QStringLiteral("校验");
        require(fastgltf::validate(asset) == fastgltf::Error::None, "glTF 结构或引用无效");
        require(asset.animations.empty() && asset.skins.empty(), "静态模型导入不支持动画或蒙皮");
        for (const auto& extension : asset.extensionsUsed) {
            result.scene.warnings.push_back(
                QStringLiteral("已忽略可选扩展：%1")
                    .arg(QString::fromUtf8(extension.data(),
                                           static_cast<qsizetype>(extension.size()))));
        }
        for (std::size_t i = 0; i < asset.textures.size(); ++i) {
            stage = QStringLiteral("纹理 %1").arg(i);
            const auto& sourceTexture = asset.textures[i];
            require(sourceTexture.imageIndex && *sourceTexture.imageIndex < asset.images.size(),
                    "纹理图像索引越界");
            TextureAsset texture;
            texture.image = decodeImage(asset, asset.images[*sourceTexture.imageIndex]);
            if (texture.image.isNull()) {
                result.scene.warnings.push_back(
                    stage + QStringLiteral("：PNG/JPEG 解码失败，已改用基础颜色"));
            }
            if (sourceTexture.samplerIndex) {
                require(*sourceTexture.samplerIndex < asset.samplers.size(), "采样器索引越界");
                const auto& sampler = asset.samplers[*sourceTexture.samplerIndex];
                texture.sampler = {
                    static_cast<int>(
                        sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear)),
                    static_cast<int>(sampler.magFilter.value_or(fastgltf::Filter::Linear)),
                    static_cast<int>(sampler.wrapS), static_cast<int>(sampler.wrapT)};
            }
            result.textures.push_back(std::move(texture));
        }
        for (std::size_t i = 0; i < asset.materials.size(); ++i) {
            stage = QStringLiteral("材质 %1").arg(i);
            const auto& sourceMaterial = asset.materials[i];
            MaterialAsset material;
            for (int component = 0; component < 4; ++component) {
                material.baseColor[component] =
                    static_cast<float>(sourceMaterial.pbrData.baseColorFactor[component]);
                require(std::isfinite(material.baseColor[component]),
                        "基础颜色（baseColorFactor）包含非有限数值");
            }
            material.doubleSided = sourceMaterial.doubleSided;
            if (const auto& info = sourceMaterial.pbrData.baseColorTexture; info) {
                require(info->texCoordIndex == 0,
                        "基础颜色纹理（baseColorTexture）使用了不支持的 UV 集");
                require(info->textureIndex < result.textures.size(), "纹理索引越界");
                if (!result.textures[info->textureIndex].image.isNull()) {
                    material.baseColorTexture = info->textureIndex + 1;
                }
            }
            if (sourceMaterial.alphaMode != fastgltf::AlphaMode::Opaque) {
                result.scene.warnings.push_back(stage +
                                                QStringLiteral("：透明模式暂按不透明方式渲染"));
            }
            result.materials.push_back(material);
        }
        std::vector<std::vector<core::AssetId>> meshPrimitives(asset.meshes.size());
        for (std::size_t i = 0; i < asset.meshes.size(); ++i) {
            const auto& mesh = asset.meshes[i];
            for (std::size_t p = 0; p < mesh.primitives.size(); ++p) {
                stage = QStringLiteral("网格 %1（%2），子网格 %3")
                            .arg(i)
                            .arg(QString::fromUtf8(mesh.name.data(),
                                                   static_cast<qsizetype>(mesh.name.size())))
                            .arg(p);
                result.meshes.push_back(readPrimitive(asset, mesh.primitives[p]));
                meshPrimitives[i].push_back(result.meshes.size());
            }
        }
        result.scene.nodes.resize(asset.nodes.size());
        std::vector<int> parents(asset.nodes.size(), 0);
        for (std::size_t i = 0; i < asset.nodes.size(); ++i) {
            stage = QStringLiteral("对象 %1").arg(i);
            const auto& sourceNode = asset.nodes[i];
            auto& node = result.scene.nodes[i];
            require(!sourceNode.skinIndex, "不支持蒙皮对象");
            node.name = sourceNode.name.empty() ? "对象 " + std::to_string(i)
                                                : std::string(sourceNode.name);
            node.transform = readTransform(sourceNode);
            node.children.assign(sourceNode.children.begin(), sourceNode.children.end());
            for (const auto child : node.children) {
                require(child < asset.nodes.size(), "子对象索引越界");
                require(++parents[child] == 1, "对象具有多个父对象");
            }
            if (sourceNode.meshIndex) {
                require(*sourceNode.meshIndex < meshPrimitives.size(), "对象网格索引越界");
                node.meshes = meshPrimitives[*sourceNode.meshIndex];
            }
        }
        stage = QStringLiteral("层级");
        std::vector<int> visited(asset.nodes.size(), 0);
        std::function<void(std::size_t)> visit = [&](std::size_t index) {
            require(visited[index] != 1, "对象层级存在循环");
            if (visited[index] == 2) {
                return;
            }
            visited[index] = 1;
            for (const auto child : result.scene.nodes[index].children) {
                visit(child);
            }
            visited[index] = 2;
        };
        for (std::size_t i = 0; i < asset.nodes.size(); ++i) {
            visit(i);
        }
        if (!asset.scenes.empty()) {
            const auto selected = asset.defaultScene.value_or(0);
            require(selected < asset.scenes.size(), "默认场景索引越界");
            const auto& roots = asset.scenes[selected].nodeIndices;
            result.scene.roots.assign(roots.begin(), roots.end());
        } else {
            for (std::size_t i = 0; i < parents.size(); ++i) {
                if (parents[i] == 0) {
                    result.scene.roots.push_back(i);
                }
            }
        }
        std::vector<bool> rootSeen(asset.nodes.size(), false);
        for (const auto root : result.scene.roots) {
            require(root < asset.nodes.size() && parents[root] == 0 && !rootSeen[root],
                    "场景根对象无效或重复");
            rootSeen[root] = true;
        }
        require(!result.scene.roots.empty(), "默认场景不包含对象");
    } catch (const std::exception& exception) {
        result = {};
        result.error = QStringLiteral("导入失败：%1［%2］：%3")
                           .arg(path, stage, QString::fromUtf8(exception.what()));
    }
    return result;
}
} // namespace mini3d::assets
