/*
 * 模块名: QuickFavorites
 * 功能概述: 按插入顺序维护去重收藏，并在用户偏好中保存稳定操作 ID。
 * 对外接口: QuickFavorites
 * 依赖关系: Qt Core、OperatorRegistry
 * 输入输出: ID 列表到 workbench/v2/quickFavorites 设置键。
 * 异常与错误: 偏好中的未知/重复 ID 被忽略，不执行失效操作。
 * 维护说明: 初始空收藏，不预填尚未实现的建模工具；测试使用临时 INI。
 */
#include "QuickFavorites.h"

#include "OperatorRegistry.h"

#include <QSettings>

namespace mini3d::editor {
namespace {
const auto kPreferenceKey = QStringLiteral("workbench/v2/quickFavorites");
}

QuickFavorites::QuickFavorites(const OperatorRegistry& registry, QObject* parent)
    : QObject(parent), registry_(registry) {}

const QStringList& QuickFavorites::operatorIds() const {
    return ids_;
}

bool QuickFavorites::contains(const QString& id) const {
    return ids_.contains(id);
}

bool QuickFavorites::addOperator(const QString& id) {
    if (!registry_.descriptor(id) || contains(id)) {
        return false;
    }
    ids_.append(id);
    emit favoritesChanged();
    return true;
}

bool QuickFavorites::removeOperator(const QString& id) {
    if (!ids_.removeOne(id)) {
        return false;
    }
    emit favoritesChanged();
    return true;
}

void QuickFavorites::restorePreferences(QSettings& settings) {
    QStringList restored;
    for (const auto& id : settings.value(kPreferenceKey).toStringList()) {
        if (registry_.descriptor(id) && !restored.contains(id)) {
            restored.append(id);
        }
    }
    if (ids_ != restored) {
        ids_ = std::move(restored);
        emit favoritesChanged();
    }
}

void QuickFavorites::savePreferences(QSettings& settings) const {
    settings.setValue(kPreferenceKey, ids_);
    settings.sync();
}
} // namespace mini3d::editor
