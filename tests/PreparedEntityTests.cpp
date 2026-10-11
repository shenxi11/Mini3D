/*
 * 模块名: PreparedEntityTests
 * 功能概述: 验证完整对象和自定义源网格的离线准备、发布与撤销。
 * 对外接口: Catch2 用例；依赖关系: Core Scene、EditableMesh。
 * 输入输出: 合法/非法候选到场景结构和几何身份断言。
 * 异常与错误: 失败候选不得留下可见节点；维护说明: 不引入 Qt 或第二个 Scene。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"
#include "core/modeling/EditableMesh.h"

#include <catch2/catch_test_macros.hpp>

using namespace mini3d::core;

TEST_CASE("Commit mesh entity issues both IDs once and preserves them through reopening",
          "[api][scene][j1-allocation]") {
    Scene scene;
    Scene::EntityCreateOptions options;
    options.name = "Provisional mesh";
    const auto source = modeling::createEditableCube();
    const auto before = SceneSerializer::encode(SceneDocumentData(scene));
    const auto allocation = scene.allocationState();
    std::string error;
    auto prepared = scene.prepareEntityForCommit(options, error, &source);
    REQUIRE(prepared);
    REQUIRE(prepared->entityId() == allocation.nextEntityId);
    REQUIRE(prepared->meshId() == allocation.nextMeshId);
    REQUIRE(SceneSerializer::encode(SceneDocumentData(scene)) == before);
    SECTION("abandoning a provisional source mesh preserves complete V5") {
        prepared.reset();
        REQUIRE(SceneSerializer::encode(SceneDocumentData(scene)) == before);
    }
    SECTION("first publication closes both counters for every undo and redo") {
        REQUIRE(scene.canInstallPreparedEntity(*prepared));
        REQUIRE(scene.installPreparedEntity(*prepared));
        const SceneAllocationState issued{allocation.nextEntityId + 1, allocation.nextMeshId + 1,
                                          allocation.nextCollectionId};
        const auto published = SceneSerializer::encode(SceneDocumentData(scene));
        for (int replay = 0; replay < 3; ++replay) {
            REQUIRE(scene.allocationState() == issued);
            REQUIRE(scene.removePreparedEntity(*prepared));
            REQUIRE(scene.allocationState() == issued);
            REQUIRE(scene.installPreparedEntity(*prepared));
            REQUIRE(SceneSerializer::encode(SceneDocumentData(scene)) == published);
            REQUIRE(scene.find(prepared->entityId())->editableMesh == prepared->meshId());
        }
        REQUIRE(scene.removePreparedEntity(*prepared));
        SceneDocumentData loaded;
        REQUIRE(SceneSerializer::decode(SceneSerializer::encode(SceneDocumentData(scene)), loaded,
                                        error));
        Scene reopened;
        REQUIRE(reopened.replaceDocumentSnapshot(loaded, error));
        REQUIRE(reopened.allocationState() == issued);
        auto fresh = reopened.prepareEntityForCommit(options, error, &source);
        REQUIRE(fresh);
        REQUIRE(fresh->entityId() == issued.nextEntityId);
        REQUIRE(fresh->meshId() == issued.nextMeshId);
        REQUIRE(reopened.installPreparedEntity(*fresh));
    }
}

TEST_CASE("Commit mesh conflicts preserve guard reservations spent IDs and claimed mesh records",
          "[api][scene][j1-allocation]") {
    for (int mutation = 0; mutation < 4; ++mutation) {
        CAPTURE(mutation);
        Scene scene;
        const auto existing = scene.createEntity("Existing", 0, PrimitiveKind::Cube);
        Scene::EntityCreateOptions options;
        options.name = "Outer candidate";
        const auto source = modeling::createEditableCube();
        std::string error;
        auto prepared = scene.prepareEntityForCommit(options, error, &source);
        REQUIRE(prepared);
        if (mutation == 3) {
            const auto geometry = scene.prepareEditableGeometry(existing, source, error);
            REQUIRE(geometry);
            REQUIRE(scene.installGeometry(*geometry));
            REQUIRE(scene.find(existing)->editableMesh == prepared->meshId());
        } else {
            auto guard = scene.prepareEntity(options, error, &source);
            REQUIRE(guard);
            if (mutation != 1) {
                REQUIRE(scene.installPreparedEntity(*guard));
                if (mutation == 2)
                    REQUIRE(scene.removePreparedEntity(*guard));
            }
        }
        const auto afterGuard = SceneSerializer::encode(SceneDocumentData(scene));
        REQUIRE_FALSE(scene.canInstallPreparedEntity(*prepared));
        REQUIRE_FALSE(scene.installPreparedEntity(*prepared));
        prepared.reset();
        REQUIRE(SceneSerializer::encode(SceneDocumentData(scene)) == afterGuard);
        if (mutation == 3)
            REQUIRE(scene.editableMesh(scene.find(existing)->editableMesh)->content->source ==
                    source);
    }
}

TEST_CASE("Commit entity without source preserves an unrelated mesh-only reservation",
          "[api][scene][j1-allocation]") {
    Scene scene;
    const auto existing = scene.createEntity("Mesh reservation target", 0, PrimitiveKind::Cube);
    Scene::EntityCreateOptions options;
    options.name = "No source candidate";
    std::string error;
    auto prepared = scene.prepareEntityForCommit(options, error);
    REQUIRE(prepared);
    const auto source = modeling::createEditableCube();
    const auto guard = scene.prepareEditableGeometry(existing, source, error);
    REQUIRE(guard);
    const auto afterGuard = scene.allocationState();
    REQUIRE(scene.canInstallPreparedEntity(*prepared));
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(scene.allocationState().nextEntityId == afterGuard.nextEntityId + 1);
    REQUIRE(scene.allocationState().nextMeshId == afterGuard.nextMeshId);
    REQUIRE(scene.find(prepared->entityId())->editableMesh == 0);
}

TEST_CASE("Default entity prepare still reserves IDs before installation and abandonment",
          "[api][scene][j1-allocation]") {
    Scene scene;
    Scene::EntityCreateOptions options;
    options.name = "Core reservation";
    const auto source = modeling::createEditableCube();
    std::string error;
    auto prepared = scene.prepareEntity(options, error, &source);
    REQUIRE(prepared);
    const auto reserved = scene.allocationState();
    REQUIRE(reserved.nextEntityId == prepared->entityId() + 1);
    REQUIRE(reserved.nextMeshId == prepared->meshId() + 1);
    auto abandoned = scene.prepareEntity(options, error, &source);
    REQUIRE(abandoned);
    const auto spent = scene.allocationState();
    abandoned.reset();
    REQUIRE(scene.allocationState() == spent);
    REQUIRE(scene.canInstallPreparedEntity(*prepared));
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(scene.allocationState() == spent);
}

TEST_CASE("Prepared entity publishes complete properties and reuses identity", "[api][scene]") {
    Scene scene;
    const auto parent = scene.createEntity("parent");
    Scene::EntityCreateOptions options;
    options.name = "完整对象";
    options.parent = parent;
    options.primitive = PrimitiveKind::Cube;
    options.transform.position = {2, 3, 4};
    options.transform.scale = {-2, 1, 3};
    options.surface.tint = {0.2F, 0.4F, 0.6F};
    options.visible = false;
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    const auto id = prepared->entityId();
    CHECK(scene.find(id) == nullptr);
    CHECK(scene.find(parent)->children.empty());
    REQUIRE(scene.installPreparedEntity(*prepared));
    CHECK(scene.find(id)->name == options.name);
    CHECK(scene.find(id)->transform.position == options.transform.position);
    CHECK(scene.find(id)->surface == options.surface);
    CHECK_FALSE(scene.find(id)->visible);
    REQUIRE(scene.removePreparedEntity(*prepared));
    CHECK(scene.find(id) == nullptr);
    REQUIRE(scene.installPreparedEntity(*prepared));
    CHECK(scene.find(id)->parent == parent);
    CHECK(scene.find(parent)->children == std::vector<EntityId>{id});
}

TEST_CASE("Prepared entity failures preserve structure and old geometry snapshots",
          "[api][scene]") {
    Scene scene;
    const auto cube = scene.createEntity("cube", 0, PrimitiveKind::Cube);
    const auto before = scene.geometrySnapshot(cube);
    Scene::EntityCreateOptions options;
    options.name = "invalid";
    options.parent = 999;
    std::string error;
    CHECK_FALSE(scene.prepareEntity(options, error));
    CHECK(scene.nodes().size() == 1);
    options.parent = 0;
    options.transform.scale.x = 0;
    CHECK_FALSE(scene.prepareEntity(options, error));
    CHECK(scene.nodes().size() == 1);
    options.transform.scale.x = 1;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(before);
    CHECK(scene.installGeometry(*before));
    CHECK(scene.find(cube)->primitive == PrimitiveKind::Cube);
}

TEST_CASE("Prepared entity from a previous document or another scene is rejected", "[api][scene]") {
    Scene scene;
    Scene other;
    Scene::EntityCreateOptions options;
    options.name = "unpublished";
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    CHECK_FALSE(other.installPreparedEntity(*prepared));
    REQUIRE(scene.replaceNodes({}));
    CHECK_FALSE(scene.installPreparedEntity(*prepared));
    CHECK(scene.nodes().empty());
}

TEST_CASE("Prepared custom mesh publishes once and redo advances source revisions",
          "[api][scene]") {
    Scene scene;
    const auto existing = scene.createEntity("old", 0, PrimitiveKind::Cube);
    const auto snapshot = scene.geometrySnapshot(existing);
    Scene::EntityCreateOptions options;
    options.name = "装甲源网格";
    auto source = modeling::createEditableCube();
    std::string error;
    auto invalid = source;
    invalid.faces.front().corners.front().vertex = 999;
    CHECK_FALSE(scene.prepareEntity(options, error, &invalid));
    CHECK(scene.nodes().size() == 1);
    invalid = source;
    invalid.faces.front().material = 1;
    CHECK_FALSE(scene.prepareEntity(options, error, &invalid));
    auto prepared = scene.prepareEntity(options, error, &source);
    REQUIRE(prepared);
    CHECK(scene.find(prepared->entityId()) == nullptr);
    CHECK(scene.editableMesh(prepared->meshId()) == nullptr);
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(scene.editableMesh(prepared->meshId()));
    CHECK(scene.editableMesh(prepared->meshId())->content->source == source);
    const auto revision = scene.editableMesh(prepared->meshId())->geometryRevision;
    REQUIRE(scene.removePreparedEntity(*prepared));
    REQUIRE(scene.installPreparedEntity(*prepared));
    CHECK(scene.editableMesh(prepared->meshId())->geometryRevision > revision);
    REQUIRE(snapshot);
    CHECK(scene.installGeometry(*snapshot));
}

TEST_CASE("Confirmed transform snapshots install exactly and reject invalid input",
          "[api][scene]") {
    Scene scene;
    Scene::EntityCreateOptions options;
    options.name = "旋转快照";
    options.transform.rotation = {0.038737595081329346F, 0.16603848338127136F, -0.3174383044242859F,
                                  0.4536607563495636F};
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedEntity(*prepared));
    const auto id = prepared->entityId();
    const auto before = scene.find(id)->transform;
    auto after = before;
    after.position.x += 3;
    after.scale.z = -2;
    REQUIRE(scene.installTransformSnapshot(id, after));
    CHECK(scene.find(id)->transform.rotation == before.rotation);
    CHECK(scene.find(id)->transform.position == after.position);
    CHECK(scene.find(id)->transform.scale == after.scale);
    REQUIRE(scene.installTransformSnapshot(id, before));
    CHECK(scene.find(id)->transform.rotation == before.rotation);
    auto invalid = before;
    invalid.scale.x = 0;
    CHECK_FALSE(scene.installTransformSnapshot(id, invalid));
    CHECK_FALSE(scene.installTransformSnapshot(999, before));
    CHECK(scene.find(id)->transform.rotation == before.rotation);
    CHECK(scene.find(id)->transform.scale == before.scale);
}
