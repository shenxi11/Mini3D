/*
 * 模块名: SceneAnimationDocumentTests
 * 功能概述: 验证正式动画的真实文件往返、载入隔离和有界读写发布。
 * 对外接口: Catch2 [animation][animation-document] 用例。
 * 依赖关系: SceneDocument、NativeSceneRead、Scene、Qt Core、nlohmann/json、Catch2。
 * 输入输出: 隔离临时文件和完整场景到精确 double 动画及文件字节断言。
 * 异常与错误: 非法输入保留 LoadedScene，编码和提交失败保留既有文件。
 * 维护说明: TMPDIR 必须由验证入口指向已检查的临时目录；不修改用户作品。
 */
#include "assets/NativeSceneRead.h"
#include "assets/SceneDocument.h"
#include "core/NativeScenePreflight.h"
#include "core/EvaluatedPose.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace mini3d;
namespace {
using Json = nlohmann::json;
constexpr core::EntityId rotorId = (core::EntityId{1} << 54) + 29;
constexpr core::EntityId editableId = rotorId + 1;
constexpr core::MeshId editableMeshId = 43;

QString temporaryTemplate() {
    const auto root = qEnvironmentVariable("TMPDIR");
    REQUIRE(QDir::isAbsolutePath(root));
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath("mini3d-animation-document-XXXXXX");
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    REQUIRE(file.flush());
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
void saveDocument(const QString& path, const assets::LoadedScene& document) {
    QString error;
    const auto saved = assets::SceneDocument::write(path, document.scene, *document.assets,
                                                    document.camera, error, document.cursor);
    INFO(error.toStdString());
    REQUIRE(saved);
}
std::string encodedState(const assets::LoadedScene& document) {
    core::SceneDocumentData data;
    data.nodes = document.scene.nodes();
    data.editableMeshes = document.scene.editableMeshes();
    data.collections = document.scene.collections();
    data.animation = document.scene.animation();
    data.lighting = document.scene.lighting();
    data.camera = document.camera;
    data.cursor = document.cursor;
    return core::SceneSerializer::encode(data);
}
core::NativeSceneProbe probeFile(const QString& path) {
    std::ifstream input(std::filesystem::path(path.toStdWString()), std::ios::binary);
    REQUIRE(input.is_open());
    core::NativeSceneProbe probe;
    std::string error;
    const auto probed = core::probeNativeSceneVersion(input, probe, error);
    INFO(error);
    REQUIRE(probed);
    return probe;
}
struct DocumentFixture {
    QTemporaryDir directory{temporaryTemplate()};
    assets::LoadedScene document;

    DocumentFixture() {
        REQUIRE(directory.isValid());
        document.assets = std::make_shared<assets::AssetManager>();
        core::SceneNode rotor;
        rotor.id = rotorId;
        rotor.name = "连续旋转父节点";
        rotor.primitive = core::PrimitiveKind::Cube;
        rotor.transform.position = {1.25F, -2.5F, 3.75F};
        rotor.transform.scale = {-2, 3, 0.5F};
        rotor.surface = {{0.2F, 0.4F, 0.6F}, false, true};
        rotor.visible = false;
        core::SceneNode editable;
        editable.id = editableId;
        editable.name = "可编辑子节点";
        editable.parent = rotorId;
        editable.editableMesh = editableMeshId;
        core::SceneNode camera;
        camera.id = rotorId + 2;
        camera.name = "场景相机";
        camera.parent = rotorId;
        camera.camera = core::CameraComponent{65, 0.2F, 250};
        core::SceneNode light;
        light.id = rotorId + 3;
        light.name = "方向灯";
        light.parent = rotorId;
        light.light = core::LightComponent{{0.3F, 0.6F, 1}, 2};
        auto cube = core::modeling::createEditableCube();
        cube.vertices[6].position.y = 1.4F;
        core::modeling::MirrorOptions mirror;
        mirror.axis = core::modeling::MirrorAxis::Z;
        mirror.enabled = false;
        mirror.merge = false;
        mirror.threshold = 0.0125;
        REQUIRE(document.scene.replaceNodes(
            {rotor, editable, camera, light},
            {{editableMeshId, cube, mirror, core::modeling::SubdivisionOptions{true, 1}}},
            {{31, "动画对象集合", false, {rotorId, editableId}}}));
        core::Lighting lighting;
        lighting.color = {0.25F, 0.5F, 0.75F};
        lighting.intensity = 1.5F;
        lighting.ambient = 0.5F;
        REQUIRE(document.scene.setLighting(lighting));
        document.camera = {{12.5F, 3.25F, 8.75F}, {1, 2, 3}, 2.5F, 125};
        document.cursor = {{1.25F, -2.5F, 3.75F}, false};
        core::SceneAnimation animation;
        animation.tracks[{rotorId, core::AnimationChannel::RotationEulerXYZDegrees}] = {
            {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
             {49, {0, 720.000000001, 0}, core::AnimationInterpolation::Constant},
             {100, {0, 3599999999.9, 0}, core::AnimationInterpolation::Linear},
             {250, {0, 3600000000.0, 0}, core::AnimationInterpolation::Constant}}};
        animation.tracks[{editableId, core::AnimationChannel::Position}] = {
            {{1, {1.000000000001, 2, 3}, core::AnimationInterpolation::Linear},
             {49, {-1.000000000001, 4, 5}, core::AnimationInterpolation::Constant}}};
        animation.tracks[{editableId, core::AnimationChannel::Scale}] = {
            {{1, {-0.001, 1, 1}, core::AnimationInterpolation::Linear},
             {49, {-0.001, 2.000000000001, 3}, core::AnimationInterpolation::Constant}}};
        std::string error;
        auto prepared = document.scene.prepareAnimation(animation, error);
        INFO(error);
        REQUIRE(prepared);
        REQUIRE(prepared->hasChanges());
        REQUIRE(document.scene.installPreparedAnimation(*prepared));
    }
};
} // namespace

TEST_CASE("Animated real documents preserve exact doubles and existing scene fields",
          "[animation][animation-document][document]") {
    DocumentFixture fixture;
    REQUIRE(fixture.document.sourceVersion == 4);
    const auto path = fixture.directory.filePath(QStringLiteral("连续角动画.m3dscene"));
    saveDocument(path, fixture.document);
    const auto bytes = readBytes(path);
    const auto json = Json::parse(bytes.toStdString());
    REQUIRE(json["version"] == 4);
    REQUIRE(json["animation"]["tracks"][0]["entityId"].get<core::EntityId>() == rotorId);
    REQUIRE(json["animation"]["tracks"][0]["keys"][1]["value"][1].get<double>() == 720.000000001);
    assets::LoadedScene loaded;
    QString error;
    const auto opened = assets::SceneDocument::read(path, loaded, error);
    INFO(error.toStdString());
    REQUIRE(opened);
    REQUIRE(loaded.sourceVersion == 4);
    REQUIRE(loaded.assets != nullptr);
    REQUIRE(loaded.scene.animation() == fixture.document.scene.animation());
    REQUIRE(loaded.scene.animation().settings == core::AnimationSettings{24, 1, 250});
    REQUIRE(loaded.camera == fixture.document.camera);
    REQUIRE(loaded.cursor == fixture.document.cursor);
    REQUIRE(loaded.scene.lighting() == fixture.document.scene.lighting());
    REQUIRE(loaded.scene.collections() == fixture.document.scene.collections());
    for (const auto& expected : fixture.document.scene.nodes()) {
        CAPTURE(expected.id);
        const auto* actual = loaded.scene.find(expected.id);
        REQUIRE(actual != nullptr);
        REQUIRE(actual->name == expected.name);
        REQUIRE(actual->parent == expected.parent);
        REQUIRE(actual->children == expected.children);
        REQUIRE(actual->transform.position == expected.transform.position);
        REQUIRE(actual->transform.rotation == expected.transform.rotation);
        REQUIRE(actual->transform.scale == expected.transform.scale);
        REQUIRE(actual->visible == expected.visible);
        REQUIRE(actual->primitive == expected.primitive);
        REQUIRE(actual->surface == expected.surface);
        REQUIRE(actual->camera == expected.camera);
        REQUIRE(actual->light == expected.light);
        REQUIRE(actual->editableMesh == expected.editableMesh);
    }
    REQUIRE(encodedState(loaded) == encodedState(fixture.document));
    const auto& rotation = loaded.scene.animation().tracks.at(
        {rotorId, core::AnimationChannel::RotationEulerXYZDegrees});
    REQUIRE(rotation.keys[1].value.y == 720.000000001);
    REQUIRE(rotation.keys[2].value.y == 3599999999.9);
    REQUIRE(rotation.keys[3].value.y == 3600000000.0);
    const auto midpoint =
        core::sampleAnimationTrack(core::AnimationChannel::RotationEulerXYZDegrees, rotation, 25);
    REQUIRE(midpoint);
    REQUIRE(midpoint->y == 360.0000000005);
    const auto* mesh = loaded.scene.editableMesh(editableMeshId);
    REQUIRE(mesh != nullptr);
    const auto* expectedMesh = fixture.document.scene.editableMesh(editableMeshId);
    REQUIRE(mesh->content->source == expectedMesh->content->source);
    REQUIRE(mesh->content->mirror == expectedMesh->content->mirror);
    REQUIRE(mesh->content->subdivision == expectedMesh->content->subdivision);
    const auto secondPath = fixture.directory.filePath("reopened.m3dscene");
    saveDocument(secondPath, loaded);
    REQUIRE(readBytes(secondPath) == bytes);
}

TEST_CASE("Malformed animation files preserve the complete previously loaded document",
          "[animation][animation-document][document]") {
    DocumentFixture fixture;
    const auto path = fixture.directory.filePath("valid.m3dscene");
    saveDocument(path, fixture.document);
    assets::LoadedScene loaded;
    QString error;
    REQUIRE(assets::SceneDocument::read(path, loaded, error));
    const auto before = encodedState(loaded);
    const auto oldAssets = loaded.assets;
    const auto* oldAnimation = &loaded.scene.animation();
    const auto good = Json::parse(readBytes(path).toStdString());
    const auto brokenPath = fixture.directory.filePath("invalid.m3dscene");
    for (int failure = 0; failure < 12; ++failure) {
        auto bad = good;
        switch (failure) {
            case 0:
                bad.erase("animation");
                break;
            case 1:
                bad["animation"]["fps"] = 24.0;
                break;
            case 2:
                bad["animation"]["tracks"].push_back(bad["animation"]["tracks"][0]);
                break;
            case 3:
                bad["animation"]["tracks"][0]["entityId"] = rotorId + 100;
                break;
            case 4:
                bad["animation"]["tracks"][0]["keys"][1]["frame"] = 1;
                break;
            case 5:
                bad["animation"]["tracks"][0]["keys"][1]["value"] = {0, 3600000000.1, 0};
                break;
            case 6:
                bad["animation"]["tracks"][2]["keys"][1]["value"] = {0.001, 2, 3};
                break;
            case 7:
                bad["animation"]["tracks"][0]["channel"] = "quaternion";
                break;
            case 8:
                bad["animation"]["tracks"][0]["unexpected"] = true;
                break;
            case 9:
                bad["version"] = 3;
                break;
            case 10:
                bad["entities"][0]["parent"] = rotorId + 100;
                break;
            case 11:
                break;
        }
        CAPTURE(failure);
        writeBytes(brokenPath,
                   failure == 11 ? QByteArray("{broken") : QByteArray::fromStdString(bad.dump()));
        auto kind = assets::FileReadFailure::None;
        REQUIRE_FALSE(assets::SceneDocument::read(brokenPath, loaded, error, {}, &kind));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE(kind == assets::FileReadFailure::InvalidData);
        REQUIRE(loaded.assets == oldAssets);
        REQUIRE(&loaded.scene.animation() == oldAnimation);
        REQUIRE(loaded.sourceVersion == 4);
        REQUIRE(encodedState(loaded) == before);
    }
}

TEST_CASE("Animated format four remaps relative glTF resources after directory migration",
          "[animation][animation-document][document]") {
    DocumentFixture fixture;
    QDir root(fixture.directory.path());
    REQUIRE(root.mkpath("project/assets"));
    const auto project = root.filePath("project");
    const auto resource = project + "/assets/model.glb";
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/Box.glb"), resource));
    REQUIRE(fixture.document.assets->importGltf(resource).scene != nullptr);
    const auto mesh = fixture.document.assets->meshFromSource(resource, 0);
    REQUIRE(mesh != core::kInvalidAsset);
    const auto entity = fixture.document.scene.createEntity("相对资源子节点", rotorId);
    REQUIRE(fixture.document.scene.setMeshRenderer(
        entity, {mesh, fixture.document.assets->mesh(mesh)->material}));
    saveDocument(project + "/animated.m3dscene", fixture.document);
    const auto json = Json::parse(readBytes(project + "/animated.m3dscene").toStdString());
    REQUIRE(json["assets"].size() == 1);
    REQUIRE(json["assets"][0]["path"] == "assets/model.glb");
    REQUIRE(root.rename("project", "moved"));
    assets::LoadedScene loaded;
    QString error;
    const auto opened =
        assets::SceneDocument::read(root.filePath("moved/animated.m3dscene"), loaded, error);
    INFO(error.toStdString());
    REQUIRE(opened);
    REQUIRE(loaded.scene.animation() == fixture.document.scene.animation());
    REQUIRE(loaded.scene.find(entity)->parent == rotorId);
    REQUIRE(loaded.scene.find(entity)->meshRenderer.has_value());
    REQUIRE(loaded.assets->meshCount() == 1);
    REQUIRE(loaded.assets->meshFromSource(root.filePath("moved/assets/model.glb"), 0) ==
            loaded.scene.find(entity)->meshRenderer->mesh);
    REQUIRE(loaded.scene.collections() == fixture.document.scene.collections());
    REQUIRE(loaded.camera == fixture.document.camera);
    REQUIRE(loaded.cursor == fixture.document.cursor);
    REQUIRE(loaded.scene.editableMesh(editableMeshId)->content->source ==
            fixture.document.scene.editableMesh(editableMeshId)->content->source);
}

TEST_CASE("Oversized format four writes publish no file and preserve existing bytes",
          "[animation][animation-document][document]") {
    QTemporaryDir directory{temporaryTemplate()};
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("oversized.m3dscene");
    const QByteArray previous("existing document bytes\n");
    auto mode = assets::SceneDocument::WriteMode::NewOnly;
    bool existing = false;
    SECTION("New target") {}
    SECTION("Existing replace target") {
        mode = assets::SceneDocument::WriteMode::ReplaceExisting;
        existing = true;
    }
    SECTION("Existing NewOnly target") {
        existing = true;
    }
    if (existing)
        writeBytes(path, previous);
    const auto entries = QDir(directory.path()).entryList(QDir::Files | QDir::Hidden);
    core::Scene scene;
    const auto id = scene.createEntity(std::string(core::kNativeSceneMaximumBytes + 1, 'x'), 0,
                                       core::PrimitiveKind::Cube);
    REQUIRE(id != 0);
    core::SceneAnimation animation;
    animation.tracks[{id, core::AnimationChannel::RotationEulerXYZDegrees}] = {
        {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
         {49, {0, 720.000000001, 0}, core::AnimationInterpolation::Linear}}};
    std::string reason;
    auto prepared = scene.prepareAnimation(animation, reason);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedAnimation(*prepared));
    assets::AssetManager assetManager;
    QString error;
    int guardCalls = 0;
    bool overwriteDenied = true;
    REQUIRE_FALSE(assets::SceneDocument::write(
        path, scene, assetManager, {}, error, {},
        [&] {
            ++guardCalls;
            return true;
        },
        mode, &overwriteDenied));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(guardCalls == 0);
    REQUIRE_FALSE(overwriteDenied);
    REQUIRE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden) == entries);
    if (existing)
        REQUIRE(readBytes(path) == previous);
    else
        REQUIRE_FALSE(QFileInfo::exists(path));
}

TEST_CASE("Final write guards and NewOnly publication cannot replace existing bytes",
          "[animation][animation-document][document]") {
    DocumentFixture fixture;
    const auto path = fixture.directory.filePath("protected.m3dscene");
    const auto previous = QByteArray::fromHex("000102deadbeefff");
    auto mode = assets::SceneDocument::WriteMode::ReplaceExisting;
    bool guardAllows = true;
    bool seedTarget = true;
    bool createInGuard = false;
    bool expectedDenial = false;
    SECTION("Replace commit guard rejects") {
        guardAllows = false;
    }
    SECTION("NewOnly target already exists") {
        mode = assets::SceneDocument::WriteMode::NewOnly;
        expectedDenial = true;
    }
    SECTION("NewOnly commit guard rejects") {
        mode = assets::SceneDocument::WriteMode::NewOnly;
        guardAllows = false;
    }
    SECTION("NewOnly target appears in the final guard") {
        mode = assets::SceneDocument::WriteMode::NewOnly;
        seedTarget = false;
        createInGuard = true;
        expectedDenial = true;
    }
    if (seedTarget)
        writeBytes(path, previous);
    QString error;
    int guardCalls = 0;
    bool overwriteDenied = false;
    REQUIRE_FALSE(assets::SceneDocument::write(
        path, fixture.document.scene, *fixture.document.assets, fixture.document.camera, error,
        fixture.document.cursor,
        [&] {
            ++guardCalls;
            if (createInGuard)
                writeBytes(path, previous);
            return guardAllows;
        },
        mode, &overwriteDenied));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(guardCalls == 1);
    REQUIRE(overwriteDenied == expectedDenial);
    REQUIRE(readBytes(path) == previous);
    REQUIRE(QDir(fixture.directory.path()).entryList(QDir::Files | QDir::Hidden) ==
            QStringList{QFileInfo(path).fileName()});
}

TEST_CASE("Real legacy files changed after probing reject with a bounded read",
          "[animation][animation-document][native-scene]") {
    QTemporaryDir directory{temporaryTemplate()};
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("changed-legacy.m3dscene");
    auto legacy = Json::parse(core::SceneSerializer::encode(core::SceneDocumentData{}));
    legacy["version"] = 3;
    legacy.erase("animation");
    const auto original = QByteArray::fromStdString(legacy.dump());
    writeBytes(path, original);
    const auto probe = probeFile(path);
    REQUIRE_FALSE(probe.includesVersion4);
    REQUIRE(probe.sourceBytes == static_cast<std::size_t>(original.size()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    SECTION("Growth consumes at most the probed length plus one") {
        REQUIRE(file.seek(file.size()));
        const QByteArray appended(16384, ' ');
        REQUIRE(file.write(appended) == appended.size());
        REQUIRE(file.flush());
    }
    SECTION("Truncation rejects instead of publishing partial bytes") {
        REQUIRE(file.resize(file.size() - 1));
        REQUIRE(file.flush());
    }
    REQUIRE(file.seek(0));
    const QByteArray previous("previous output bytes");
    auto output = previous;
    QString error;
    REQUIRE_FALSE(assets::detail::readPreflightedSceneBytes(file, probe, output, error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(file.pos() <= static_cast<qint64>(probe.sourceBytes) + 1);
    REQUIRE(output == previous);
}

TEST_CASE("Real source byte limits apply to format four without restricting legacy files",
          "[animation][animation-document][native-scene]") {
    QTemporaryDir directory{temporaryTemplate()};
    REQUIRE(directory.isValid());
    core::SceneDocumentData data;
    core::SceneNode node;
    node.id = 1;
    node.name = "静态旧格式对象";
    node.primitive = core::PrimitiveKind::Cube;
    data.nodes.push_back(node);
    auto json = Json::parse(core::SceneSerializer::encode(data));
    bool legacy = false;
    SECTION("Valid legacy file above 64MiB still opens") {
        legacy = true;
        json["version"] = 3;
        json.erase("animation");
    }
    SECTION("Valid format four file above 64MiB preserves the loaded output") {}
    const auto path = directory.filePath("large-source.m3dscene");
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        const auto header = QByteArray::fromStdString(json.dump());
        REQUIRE(file.write(header) == header.size());
        const QByteArray padding(1024 * 1024, ' ');
        for (std::size_t i = 0;
             i < core::kNativeSceneMaximumBytes / static_cast<std::size_t>(padding.size()); ++i)
            REQUIRE(file.write(padding) == padding.size());
        REQUIRE(file.flush());
    }
    const auto probe = probeFile(path);
    REQUIRE(probe.sourceBytes > core::kNativeSceneMaximumBytes);
    REQUIRE(probe.includesVersion4 == !legacy);
    assets::LoadedScene loaded;
    loaded.assets = std::make_shared<assets::AssetManager>();
    REQUIRE(loaded.scene.createEntity("保留对象", 0, core::PrimitiveKind::Sphere) != 0);
    loaded.sourceVersion = 2;
    const auto oldAssets = loaded.assets;
    const auto before = encodedState(loaded);
    QString error;
    auto kind = assets::FileReadFailure::InvalidData;
    const auto opened = assets::SceneDocument::read(path, loaded, error, {}, &kind);
    INFO(error.toStdString());
    if (legacy) {
        REQUIRE(opened);
        REQUIRE(error.isEmpty());
        REQUIRE(kind == assets::FileReadFailure::None);
        REQUIRE(loaded.sourceVersion == 3);
        REQUIRE(loaded.scene.nodes().size() == 1);
        REQUIRE(loaded.scene.find(1)->name == node.name);
        REQUIRE(loaded.scene.animation() == core::SceneAnimation{});
    } else {
        REQUIRE_FALSE(opened);
        REQUIRE(error.contains("64MiB"));
        REQUIRE(kind == assets::FileReadFailure::InvalidData);
        REQUIRE(loaded.sourceVersion == 2);
        REQUIRE(loaded.assets == oldAssets);
        REQUIRE(encodedState(loaded) == before);
    }
}

TEST_CASE("Published neutral animation sample preserves two turns and source across save reopen",
          "[animation][animation-document][animation-sample]") {
    const auto originalPath = QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY)
        + QStringLiteral("/native-animation-neutral.m3dscene");
    const auto originalBytes = readBytes(originalPath);
    assets::LoadedScene document;
    QString error;
    REQUIRE(assets::SceneDocument::read(originalPath, document, error));
    REQUIRE(document.sourceVersion == 4);
    REQUIRE(document.scene.nodes().size() == 5);
    REQUIRE(document.scene.animation().tracks.size() == 2);
    REQUIRE(document.scene.animation().settings.fps == 24);
    REQUIRE(document.scene.animation().settings.startFrame == 1);
    REQUIRE(document.scene.animation().settings.endFrame == 49);
    const auto& rotation = document.scene.animation().tracks.at(
        {3, core::AnimationChannel::RotationEulerXYZDegrees});
    REQUIRE(rotation.keys.back().value.y == 720.000000001);
    const auto source = encodedState(document);
    std::string inputError;
    const auto inputs = document.scene.animationPoseInputs(inputError);
    REQUIRE(inputs);
    const auto sampled = core::evaluateAnimationPose(*inputs, document.scene.animation(), 25);
    REQUIRE(sampled.validation.isValid());
    REQUIRE(sampled.pose);
    REQUIRE(sampled.pose->find(1)->local.position.x == 5);
    REQUIRE(sampled.pose->find(3)->rotationEulerXYZDegrees->y == 360.0000000005);
    REQUIRE_FALSE(sampled.pose->find(4)->rotationEulerXYZDegrees);
    REQUIRE(document.scene.find(4)->parent == 3);
    REQUIRE(encodedState(document) == source);
    QTemporaryDir directory{temporaryTemplate()};
    REQUIRE(directory.isValid());
    const auto savedPath = directory.filePath(QStringLiteral("中立动画另存.m3dscene"));
    saveDocument(savedPath, document);
    assets::LoadedScene reopened;
    REQUIRE(assets::SceneDocument::read(savedPath, reopened, error));
    REQUIRE(reopened.scene.animation() == document.scene.animation());
    REQUIRE(encodedState(reopened) == source);
    REQUIRE(readBytes(originalPath) == originalBytes);
}
