/*
 * 模块名: ComponentSelection
 * 功能概述: 按源网格身份执行选择集合运算及点边面投影。
 * 对外接口: ComponentSelection；依赖关系: EditableMesh、标准算法。
 * 输入输出: 单一域与稳定 ID 到确定性选区；不写几何、文档或历史。
 * 异常与错误: 不存在的命中不清空原选区；维护说明: 三角化对角线不构成源边。
 */
#include "ComponentSelection.h"

namespace mini3d::editor {
using core::modeling::EdgeKey;
using core::modeling::EditableMesh;

namespace {
std::set<ComponentId> matchingElements(const EditableMesh& mesh, SelectionDomain domain,
                                       const std::set<ComponentId>& ids) {
    std::set<ComponentId> result;
    if (ids.empty()) {
        return result;
    }
    const auto accept = [&](ComponentId id) {
        if (ids.contains(id)) {
            result.insert(id);
        }
        return result.size() == ids.size();
    };
    if (domain == SelectionDomain::Vertex) {
        for (const auto& vertex : mesh.vertices) {
            if (accept({vertex.id})) {
                return result;
            }
        }
    } else {
        for (const auto& face : mesh.faces) {
            if (domain == SelectionDomain::Face) {
                if (accept({face.id})) {
                    return result;
                }
            } else {
                for (std::size_t i = 0; i < face.corners.size(); ++i) {
                    if (accept(ComponentId::edge(
                            {face.corners[i].vertex,
                             face.corners[(i + 1) % face.corners.size()].vertex}))) {
                        return result;
                    }
                }
            }
        }
    }
    return result;
}
} // namespace

ComponentId ComponentId::edge(EdgeKey key) {
    return {key.first, key.second};
}
SelectionDomain ComponentSelection::domain() const {
    return domain_;
}
const std::set<ComponentId>& ComponentSelection::selectedIds() const {
    return selectedIds_;
}
std::optional<ComponentId> ComponentSelection::activeId() const {
    return activeId_;
}
std::set<ComponentId> ComponentSelection::elements(const EditableMesh& mesh,
                                                   SelectionDomain domain) {
    std::set<ComponentId> result;
    if (domain == SelectionDomain::Vertex) {
        for (const auto& vertex : mesh.vertices) {
            result.insert({vertex.id});
        }
    } else {
        for (const auto& face : mesh.faces) {
            if (domain == SelectionDomain::Face) {
                result.insert({face.id});
            } else {
                for (std::size_t i = 0; i < face.corners.size(); ++i) {
                    result.insert(
                        ComponentId::edge({face.corners[i].vertex,
                                           face.corners[(i + 1) % face.corners.size()].vertex}));
                }
            }
        }
    }
    return result;
}
void ComponentSelection::repairActive() {
    if (activeId_ && selectedIds_.contains(*activeId_)) {
        return;
    }
    activeId_ = selectedIds_.empty() ? std::nullopt : std::optional(*selectedIds_.begin());
}
bool ComponentSelection::clear() {
    if (selectedIds_.empty()) {
        return false;
    }
    selectedIds_.clear();
    activeId_.reset();
    return true;
}
bool ComponentSelection::selectAll(const EditableMesh& mesh) {
    const auto before = *this;
    selectedIds_ = elements(mesh, domain_);
    repairActive();
    return *this != before;
}
bool ComponentSelection::select(const EditableMesh& mesh, ComponentId id,
                                SelectionOperation operation) {
    if (matchingElements(mesh, domain_, {id}).empty()) {
        return false;
    }
    const auto before = *this;
    const bool remove = operation == SelectionOperation::Remove ||
                        (operation == SelectionOperation::Toggle && selectedIds_.contains(id));
    if (operation == SelectionOperation::Replace) {
        selectedIds_.clear();
    }
    if (remove) {
        selectedIds_.erase(id);
    } else {
        selectedIds_.insert(id);
        activeId_ = id;
    }
    repairActive();
    return *this != before;
}
bool ComponentSelection::reconcile(const EditableMesh& mesh) {
    const auto before = *this;
    const auto valid = matchingElements(mesh, domain_, selectedIds_);
    std::erase_if(selectedIds_, [&valid](ComponentId id) {
        return !valid.contains(id);
    });
    repairActive();
    return *this != before;
}
bool ComponentSelection::selectMany(const EditableMesh& mesh, const std::set<ComponentId>& ids,
                                    SelectionOperation operation) {
    const auto before = *this;
    const auto valid = matchingElements(mesh, domain_, ids);
    if (operation == SelectionOperation::Replace) {
        selectedIds_.clear();
    }
    for (const auto id : ids) {
        if (!valid.contains(id)) {
            continue;
        }
        if (operation == SelectionOperation::Remove ||
            (operation == SelectionOperation::Toggle && selectedIds_.contains(id))) {
            selectedIds_.erase(id);
        } else {
            selectedIds_.insert(id);
        }
    }
    repairActive();
    return *this != before;
}
bool ComponentSelection::setDomain(const EditableMesh& mesh, SelectionDomain domain) {
    if (domain_ == domain) {
        return false;
    }
    // 从当前源域投影，不把上一域隐藏存档再恢复，避免产生肉眼不可见的选中元素。
    std::set<ComponentId> projected;
    if (domain_ == SelectionDomain::Face) {
        for (const auto& face : mesh.faces) {
            if (!selectedIds_.contains({face.id})) {
                continue;
            }
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                if (domain == SelectionDomain::Vertex) {
                    projected.insert({face.corners[i].vertex});
                } else {
                    projected.insert(
                        ComponentId::edge({face.corners[i].vertex,
                                           face.corners[(i + 1) % face.corners.size()].vertex}));
                }
            }
        }
    } else if (domain == SelectionDomain::Vertex) {
        for (const auto id : selectedIds_) {
            projected.insert({id.first});
            projected.insert({id.second});
        }
    } else if (domain == SelectionDomain::Edge) {
        for (const auto& face : mesh.faces) {
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                const auto first = face.corners[i].vertex;
                const auto second = face.corners[(i + 1) % face.corners.size()].vertex;
                if (selectedIds_.contains({first}) && selectedIds_.contains({second})) {
                    projected.insert(ComponentId::edge({first, second}));
                }
            }
        }
    } else {
        for (const auto& face : mesh.faces) {
            bool complete = true;
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                complete =
                    complete && (domain_ == SelectionDomain::Vertex
                                     ? selectedIds_.contains({face.corners[i].vertex})
                                     : selectedIds_.contains(ComponentId::edge(
                                           {face.corners[i].vertex,
                                            face.corners[(i + 1) % face.corners.size()].vertex})));
            }
            if (complete) {
                projected.insert({face.id});
            }
        }
    }
    domain_ = domain;
    selectedIds_ = std::move(projected);
    activeId_.reset();
    repairActive();
    return true;
}
bool ComponentSelection::hasSelectionInDomain(const EditableMesh& mesh,
                                              SelectionDomain domain) const {
    if (domain_ == domain ||
        (domain_ == SelectionDomain::Edge && domain == SelectionDomain::Vertex)) {
        return !selectedIds_.empty();
    }
    for (const auto& face : mesh.faces) {
        if (domain_ == SelectionDomain::Face) {
            if (!face.corners.empty() && selectedIds_.contains({face.id})) {
                return true;
            }
        } else if (domain == SelectionDomain::Edge) {
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                if (selectedIds_.contains({face.corners[i].vertex}) &&
                    selectedIds_.contains({face.corners[(i + 1) % face.corners.size()].vertex})) {
                    return true;
                }
            }
        } else {
            bool complete = true;
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                complete =
                    complete && (domain_ == SelectionDomain::Vertex
                                     ? selectedIds_.contains({face.corners[i].vertex})
                                     : selectedIds_.contains(ComponentId::edge(
                                           {face.corners[i].vertex,
                                            face.corners[(i + 1) % face.corners.size()].vertex})));
            }
            if (complete) {
                return true;
            }
        }
    }
    return false;
}
std::optional<glm::vec3> ComponentSelection::activePosition(const EditableMesh& mesh) const {
    if (!activeId_) {
        return std::nullopt;
    }
    if (domain_ == SelectionDomain::Face) {
        for (const auto& face : mesh.faces) {
            if (face.id == activeId_->first) {
                glm::vec3 center{0};
                for (const auto& corner : face.corners) {
                    center += mesh.vertex(corner.vertex)->position;
                }
                return center / static_cast<float>(face.corners.size());
            }
        }
        return std::nullopt;
    }
    const auto* first = mesh.vertex(activeId_->first);
    if (!first) {
        return std::nullopt;
    }
    if (domain_ == SelectionDomain::Vertex) {
        return first->position;
    }
    const auto* second = mesh.vertex(activeId_->second);
    return second ? std::optional((first->position + second->position) * 0.5F) : std::nullopt;
}
std::set<core::modeling::VertexId>
ComponentSelection::selectedVertices(const EditableMesh& mesh) const {
    auto points = *this;
    points.setDomain(mesh, SelectionDomain::Vertex);
    points.reconcile(mesh);
    std::set<core::modeling::VertexId> result;
    for (const auto id : points.selectedIds()) {
        result.insert(id.first);
    }
    return result;
}
} // namespace mini3d::editor
