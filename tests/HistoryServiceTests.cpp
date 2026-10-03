/*
 * 模块名: HistoryServiceTests
 * 功能概述: 无窗口/无 GL 的真实 Scene 与唯一 Qt 历史夹具，验证 F9 原子替换。
 * 对外接口: Catch2 [history-service]；依赖关系: Scene、区域挤出、HistoryService。
 * 输入输出: 位移/故障到快照、选择、历史和保存点断言。
 * 异常与错误: 注入离线重算与通知副本准备的分配失败，不以成功路径替代失败验收。
 * 维护说明: 不改实际文件和用户配置；安装开始后不注入异常。
 */
#include "core/modeling/ExtrudeRegion.h"
#include "editor/EditCommand.h"
#include "editor/operations/HistoryService.h"

#include <catch2/catch_test_macros.hpp>
#include <new>

using namespace mini3d;
namespace {
struct CheckedNotification {
    bool& failCopy;
    int& notifications;
    CheckedNotification(bool& fail, int& count) : failCopy(fail), notifications(count) {}
    CheckedNotification(const CheckedNotification& other)
        : failCopy(other.failCopy), notifications(other.notifications) {
        if (failCopy)
            throw std::bad_alloc{};
    }
    void operator()() const {
        ++notifications;
    }
};
struct HistoryFixture {
    core::Scene scene;
    core::EntityId entity = scene.createEntity("Cube", 0, core::PrimitiveKind::Cube);
    QUndoStack stack;
    editor::HistoryService history{stack};
    editor::ComponentSelection selection;
    core::Scene::GeometrySnapshot before;
    bool failRecompute = false, failNotificationCopy = false;
    int computations = 0, notifications = 0;

    HistoryFixture() {
        std::string error;
        before =
            *scene.prepareEditableGeometry(entity, core::modeling::createEditableCube(), error);
        REQUIRE(scene.installGeometry(before));
        selection.setDomain(before.content()->source, editor::SelectionDomain::Face);
        selection.select(before.content()->source, {1}, editor::SelectionOperation::Replace);
    }
    core::EditableMeshRecord current() const {
        return *scene.editableMesh(scene.find(entity)->editableMesh);
    }
    void push() {
        editor::ReopenableGeometryEdit edit;
        edit.entity = entity;
        edit.label = QStringLiteral("区域挤出");
        edit.before = {before, selection};
        edit.parameters = glm::dvec3(0, 0, 0.25);
        edit.recompute = [this](const editor::GeometryOperationParameters& parameters,
                                std::string& error) {
            ++computations;
            if (failRecompute)
                throw std::bad_alloc{};
            const auto candidate = core::modeling::extrudeRegion(before.content()->source, {1},
                                                                 std::get<glm::dvec3>(parameters));
            if (!candidate.mesh) {
                error = candidate.error;
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            return scene.prepareEditableGeometry(entity, *candidate.mesh, error);
        };
        std::string error;
        edit.after = {*edit.recompute(edit.parameters, error), selection};
        edit.install = [this](editor::GeometryHistoryState state) {
            REQUIRE(scene.installGeometry(state.geometry));
            selection = std::move(state.selection);
        };
        edit.notify = CheckedNotification(failNotificationCopy, notifications);
        history.push(std::move(edit));
    }
};
} // namespace

TEST_CASE("History replacement recomputes original before and replays snapshots once",
          "[history-service]") {
    HistoryFixture f;
    f.push();
    const auto selected = f.selection;
    const auto* command = f.stack.command(0);
    QString error;
    for (double distance : {0.5, 0.75, -0.25}) {
        const auto revision = f.current().evaluationRevision;
        REQUIRE(f.history.adjust({0, 0, distance}, error));
        REQUIRE(error.isEmpty());
        REQUIRE(f.current().content->source.faces.size() == 10);
        REQUIRE(f.current().content->source.vertices.size() == 12);
        const auto& face = f.current().content->source.faces.front();
        REQUIRE(f.current().content->source.vertex(face.corners.front().vertex)->position.z ==
                float(0.5 + distance));
        REQUIRE(f.current().evaluationRevision > revision);
        REQUIRE(f.stack.command(0) == command);
        REQUIRE(f.stack.index() == 1);
        REQUIRE(f.stack.count() == 1);
        REQUIRE(f.selection == selected);
    }
    const auto after = f.current().content;
    const auto computations = f.computations;
    f.selection.clear();
    f.stack.undo();
    REQUIRE(f.current().content == f.before.content());
    REQUIRE(f.selection == selected);
    REQUIRE_FALSE(f.history.canAdjust());
    REQUIRE_FALSE(f.history.adjust({0, 0, 0.5}, error));
    f.stack.redo();
    REQUIRE(f.current().content == after);
    REQUIRE(f.history.canAdjust());
    REQUIRE(f.computations == computations);
}

TEST_CASE("History replacement preserves no-op clean points and rejects failures atomically",
          "[history-service]") {
    HistoryFixture f;
    f.push();
    f.stack.setClean();
    const auto before = f.current();
    const auto selected = f.selection;
    const auto offset = f.history.worldOffset();
    const auto notifications = f.notifications;
    QString error;
    REQUIRE(f.history.adjust(*offset, error));
    REQUIRE_FALSE(f.history.insetThickness());
    REQUIRE_FALSE(f.history.adjustInset(.25, error));
    REQUIRE(f.computations == 1);
    REQUIRE(f.history.adjust({0, 0, 0.250000000000001}, error));
    REQUIRE(f.history.worldOffset() == offset);
    REQUIRE(f.stack.isClean());
    REQUIRE_FALSE(f.history.adjust({0, 0, 0}, error));
    REQUIRE_FALSE(error.isEmpty());
    f.failRecompute = true;
    REQUIRE_FALSE(f.history.adjust({0, 0, 0.5}, error));
    REQUIRE(error.contains(QStringLiteral("内存不足")));
    f.failRecompute = false;
    f.failNotificationCopy = true;
    REQUIRE_FALSE(f.history.adjust({0, 0, 0.5}, error));
    REQUIRE(error.contains(QStringLiteral("内存不足")));
    REQUIRE(f.current().content == before.content);
    REQUIRE(f.current().evaluationRevision == before.evaluationRevision);
    REQUIRE(f.selection == selected);
    REQUIRE(f.history.worldOffset() == offset);
    REQUIRE(f.stack.isClean());
    REQUIRE(f.stack.cleanIndex() == 1);
    REQUIRE(f.stack.count() == 1);
    REQUIRE(f.stack.index() == 1);
    REQUIRE(f.notifications == notifications);
    f.failNotificationCopy = false;
    REQUIRE(f.history.adjust({0, 0, 0.5}, error));
    REQUIRE_FALSE(f.stack.isClean());
    REQUIRE(f.stack.cleanIndex() == -1);
    f.stack.undo();
    f.stack.redo();
    REQUIRE_FALSE(f.stack.isClean());
}

TEST_CASE("History adapter owns no second stack and preserves a saved before state",
          "[history-service]") {
    HistoryFixture f;
    REQUIRE_FALSE(f.history.canAdjust());
    f.stack.setClean();
    f.push();
    QString error;
    REQUIRE(f.history.adjust({0, 0, 0.5}, error));
    REQUIRE(f.stack.cleanIndex() == 0);
    f.stack.undo();
    REQUIRE(f.stack.isClean());
    f.stack.redo();
    f.stack.push(new editor::EditCommand(QStringLiteral("其他数据编辑"), [] {}, [] {}));
    REQUIRE_FALSE(f.history.canAdjust());
    REQUIRE_FALSE(f.history.adjust({0, 0, 0.75}, error));
    f.stack.undo();
    REQUIRE_FALSE(f.history.canAdjust());
    f.stack.clear();
    REQUIRE_FALSE(f.history.canAdjust());
    REQUIRE_FALSE(f.history.worldOffset());
    REQUIRE(f.history.target() == core::kInvalidEntity);
}
