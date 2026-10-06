/*
 * 模块名: ApiDocumentState
 * 功能概述: 为编辑文档分配 UUID 并记录已确认提交版本。
 * 对外接口: ApiDocumentState.h。
 * 依赖关系: Qt UUID、ApiTypes。
 * 输入输出: 显式提交到运行期文档状态，不写场景文件。
 * 异常与错误: 调用者只在业务成功边界使用本模块。
 * 维护说明: 所有调用在拥有 ViewModel 的应用线程执行。
 */
#include "ApiDocumentState.h"

#include <QUuid>

namespace mini3d::editor::api {
namespace {
QString createUuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace
ApiDocumentState::ApiDocumentState() {
    static const QString instanceId = createUuid();
    state_.document.instanceId = instanceId;
    resetDocument();
}
const DocumentState& ApiDocumentState::state() const {
    return state_;
}
void ApiDocumentState::recordCommit(bool contentChanged, bool historyChanged) {
    if (contentChanged)
        ++state_.documentRevision;
    if (historyChanged)
        ++state_.historyRevision;
}
void ApiDocumentState::resetDocument() {
    state_.document.documentId = createUuid();
    state_.documentRevision = 1;
    state_.historyRevision = 1;
}
} // namespace mini3d::editor::api
