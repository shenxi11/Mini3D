/*
 * 模块名: GltfImporterTests
 * 功能概述: 验证官方 GLB、外部 glTF、资源缓存和失败输入的事务边界。
 * 对外接口: Catch2 测试
 * 依赖关系: Assets、Qt Gui、nlohmann/json、Catch2
 * 输入输出: 固定样例及临时测试文件，输出解析断言。
 * 异常与错误: 不合法文件必须返回错误，不发生断言终止或资源污染。
 * 维护说明: 无窗口/OpenGL；临时文件由 QTemporaryDir 管理。
 */
#include "assets/AssetManager.h"

#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
using namespace mini3d;
using nlohmann::json;
namespace {
QString sample(const char* name) {
    return QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY) + "/" + QString::fromLatin1(name);
}
struct Fixture {
    QTemporaryDir directory;
    json document;
    QByteArray bytes;
    Fixture() {
        const std::array<float, 12> positions{0, 0, 0, 99, 1, 0, 0, 99, 0, 1, 0, 99};
        const std::array<float, 6> uvs{0, 0, 1, 0, 0, 1};
        const std::array<std::uint16_t, 3> indices{0, 1, 2};
        bytes.append(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
        bytes.append(reinterpret_cast<const char*>(uvs.data()), sizeof(uvs));
        bytes.append(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
        document = {
            {"asset", {{"version", "2.0"}}},
            {"buffers", json::array({{{"uri", "data.bin"}, {"byteLength", bytes.size()}}})},
            {"bufferViews",
             json::array(
                 {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 48}, {"byteStride", 16}},
                  {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 24}},
                  {{"buffer", 0}, {"byteOffset", 72}, {"byteLength", 6}}})},
            {"accessors",
             json::array(
                 {{{"bufferView", 0},
                   {"componentType", 5126},
                   {"count", 3},
                   {"type", "VEC3"},
                   {"min", {0, 0, 0}},
                   {"max", {1, 1, 0}}},
                  {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
                  {{"bufferView", 2}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}})},
            {"materials", json::array({{{"doubleSided", true},
                                        {"pbrMetallicRoughness",
                                         {{"baseColorFactor", {0.2, 0.4, 0.6, 1}},
                                          {"baseColorTexture", {{"index", 0}}}}}}})},
            {"images", json::array({{{"uri", "image.png"}}})},
            {"samplers",
             json::array(
                 {{{"wrapS", 33071}, {"wrapT", 33648}, {"minFilter", 9728}, {"magFilter", 9728}}})},
            {"textures", json::array({{{"source", 0}, {"sampler", 0}}})},
            {"meshes",
             json::array({{{"primitives",
                            json::array({{{"attributes", {{"POSITION", 0}, {"TEXCOORD_0", 1}}},
                                          {"indices", 2},
                                          {"material", 0}}})}}})},
            {"nodes",
             json::array({{{"name", "Parent"},
                           {"children", {1, 2}},
                           {"matrix", {-2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0.5, 0, 2, 3, 4, 1}}},
                          {{"name", "Triangle"}, {"mesh", 0}, {"translation", {1, 0, 0}}},
                          {{"name", "Instance"}, {"mesh", 0}}})},
            {"scenes", json::array({{{"nodes", {0}}}})},
            {"scene", 0}};
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.setPixelColor(0, 0, Qt::red);
        image.setPixelColor(1, 0, Qt::blue);
        image.setPixelColor(0, 1, Qt::green);
        image.setPixelColor(1, 1, Qt::yellow);
        REQUIRE(image.save(directory.filePath("image.png")));
    }
    QString write() {
        QFile buffer(directory.filePath("data.bin"));
        REQUIRE(buffer.open(QIODevice::WriteOnly));
        REQUIRE(buffer.write(bytes) == bytes.size());
        const auto text = document.dump();
        const QString path = directory.filePath(QStringLiteral("场景.gltf"));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(text.data(), static_cast<qint64>(text.size())) ==
                static_cast<qint64>(text.size()));
        return path;
    }
};
} // namespace
TEST_CASE("Khronos Box BoxTextured and Duck import with geometry and embedded textures",
          "[assets]") {
    for (const auto* name : {"Box.glb", "BoxTextured.glb", "Duck.glb"}) {
        const auto result = assets::GltfImporter::read(sample(name));
        INFO(result.error.toStdString());
        REQUIRE(result.error.isEmpty());
        REQUIRE_FALSE(result.meshes.empty());
        REQUIRE_FALSE(result.scene.roots.empty());
        REQUIRE(result.meshes.front().data.bounds().isValid());
        if (std::string(name) != "Box.glb") {
            REQUIRE_FALSE(result.textures.empty());
            REQUIRE_FALSE(result.textures.front().image.isNull());
        }
        if (std::string(name) == "Box.glb") {
            REQUIRE(result.meshes.front().data.vertices.size() == 24);
            REQUIRE(result.meshes.front().data.indices.size() == 36);
        }
    }
}
TEST_CASE("External images stride matrix generated normals and primitive instances survive import",
          "[assets]") {
    Fixture fixture;
    auto& primitives = fixture.document["meshes"][0]["primitives"];
    primitives.push_back(primitives[0]);
    const auto result = assets::GltfImporter::read(fixture.write());
    INFO(result.error.toStdString());
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.meshes.size() == 2);
    REQUIRE(result.scene.nodes[1].meshes == result.scene.nodes[2].meshes);
    REQUIRE(result.scene.nodes[1].meshes.size() == 2);
    REQUIRE(result.scene.nodes[0].transform.position == glm::vec3(2, 3, 4));
    REQUIRE(result.scene.nodes[0].transform.scale == glm::vec3(-2, 3, 0.5F));
    const auto& vertices = result.meshes[0].data.vertices;
    REQUIRE(vertices[1].position == glm::vec3(1, 0, 0));
    REQUIRE(vertices[0].normal == glm::vec3(0, 0, 1));
    REQUIRE(vertices[0].uv == glm::vec2(0, 1));
    REQUIRE(result.textures[0].image.pixelColor(0, 0) == QColor(Qt::red));
    REQUIRE(result.textures[0].sampler.wrapS == 33071);
    REQUIRE(result.textures[0].sampler.minFilter == 9728);
    REQUIRE(result.materials[0].baseColor.x == Catch::Approx(0.2));
    REQUIRE(result.materials[0].doubleSided);
}
TEST_CASE("AssetManager caches canonical sources and never allocates on import failure",
          "[assets]") {
    assets::AssetManager manager;
    const auto first = manager.importGltf(sample("Box.glb"));
    REQUIRE(first.scene != nullptr);
    REQUIRE_FALSE(first.cacheHit);
    const auto again = manager.importGltf(sample("./Box.glb"));
    REQUIRE(again.scene == first.scene);
    REQUIRE(again.cacheHit);
    REQUIRE(manager.meshCount() == 1);
    REQUIRE(manager.mesh(first.scene->nodes[1].meshes[0]) != nullptr);
    const auto failed = manager.importGltf(sample("missing.glb"));
    REQUIRE(failed.scene == nullptr);
    REQUIRE_FALSE(failed.error.isEmpty());
    REQUIRE(manager.meshCount() == 1);
}
TEST_CASE("Malformed accessor index and hierarchy fail safely", "[assets]") {
    for (int mutation = 0; mutation < 6; ++mutation) {
        Fixture fixture;
        switch (mutation) {
            case 0:
                fixture.document["accessors"][0]["count"] = 1000;
                break;
            case 1:
                fixture.document["accessors"][0]["type"] = "VEC2";
                break;
            case 2:
                fixture.bytes[72] = static_cast<char>(99);
                break;
            case 3:
                fixture.document["nodes"][1]["children"] = {0};
                break;
            case 4:
                fixture.document["nodes"][0]["matrix"][4] = 1;
                break;
            case 5:
                fixture.document["bufferViews"][0]["byteLength"] = 99999;
                break;
        }
        const auto result = assets::GltfImporter::read(fixture.write());
        INFO(mutation);
        REQUIRE_FALSE(result.error.isEmpty());
        REQUIRE(result.meshes.empty());
        REQUIRE(result.scene.nodes.empty());
    }
}
TEST_CASE("Texture decode fallback and JPEG are explicit while geometry corruption is fatal",
          "[assets]") {
    Fixture fixture;
    fixture.document["images"][0]["uri"] = "image.jpg";
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    REQUIRE(image.save(fixture.directory.filePath("image.jpg")));
    auto result = assets::GltfImporter::read(fixture.write());
    INFO(result.error.toStdString());
    REQUIRE(result.error.isEmpty());
    REQUIRE_FALSE(result.textures[0].image.isNull());
    QFile broken(fixture.directory.filePath("image.jpg"));
    REQUIRE(broken.open(QIODevice::WriteOnly));
    broken.write("invalid image");
    broken.close();
    result = assets::GltfImporter::read(fixture.write());
    REQUIRE(result.error.isEmpty());
    REQUIRE_FALSE(result.scene.warnings.empty());
    REQUIRE(result.materials[0].baseColorTexture == core::kInvalidAsset);
}

TEST_CASE("Index widths nonindexed triangles and normalized byte UVs decode", "[assets]") {
    for (int mode = 0; mode < 3; ++mode) {
        Fixture fixture;
        if (mode == 0) {
            fixture.bytes.resize(72);
            fixture.bytes.append(QByteArray::fromHex("000102"));
            fixture.document["accessors"][2]["componentType"] = 5121;
            fixture.document["bufferViews"][2]["byteLength"] = 3;
        } else if (mode == 1) {
            fixture.bytes.resize(72);
            const std::array<std::uint32_t, 3> indices{0, 1, 2};
            fixture.bytes.append(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
            fixture.document["accessors"][2]["componentType"] = 5125;
            fixture.document["bufferViews"][2]["byteLength"] = 12;
        } else {
            fixture.document["meshes"][0]["primitives"][0].erase("indices");
        }
        fixture.document["buffers"][0]["byteLength"] = fixture.bytes.size();
        const auto packedUvs = QByteArray::fromHex("0000ff0000ff");
        fixture.bytes.replace(48, 6, packedUvs);
        fixture.document["accessors"][1]["componentType"] = 5121;
        fixture.document["accessors"][1]["normalized"] = true;
        auto result = assets::GltfImporter::read(fixture.write());
        INFO(result.error.toStdString());
        REQUIRE(result.error.isEmpty());
        REQUIRE(result.meshes[0].data.indices == std::vector<std::uint32_t>{0, 1, 2});
        REQUIRE(result.meshes[0].data.vertices[1].uv == glm::vec2(1, 1));
    }
}

TEST_CASE("Truncated GLB unsupported extensions and missing external buffers fail", "[assets]") {
    Fixture fixture;
    const auto path = fixture.directory.filePath("broken.glb");
    QFile original(sample("Box.glb"));
    REQUIRE(original.open(QIODevice::ReadOnly));
    QFile broken(path);
    REQUIRE(broken.open(QIODevice::WriteOnly));
    broken.write(original.read(20));
    broken.close();
    REQUIRE_FALSE(assets::GltfImporter::read(path).error.isEmpty());
    fixture.document["extensionsUsed"] = {"KHR_draco_mesh_compression"};
    fixture.document["extensionsRequired"] = {"KHR_draco_mesh_compression"};
    REQUIRE_FALSE(assets::GltfImporter::read(fixture.write()).error.isEmpty());
    fixture.document.erase("extensionsRequired");
    fixture.document.erase("extensionsUsed");
    fixture.document["buffers"][0]["uri"] = "missing.bin";
    REQUIRE_FALSE(assets::GltfImporter::read(fixture.write()).error.isEmpty());
}
