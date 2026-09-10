/*
 * 模块名: GizmoController
 * 功能概述: 冻结手柄坐标基，以平面交点计算可撤销的 TRS 预览与步进吸附。
 * 对外接口: GizmoController
 * 依赖关系: Core Aabb/Ray、标准数学库
 * 输入输出: 输入射线，输出绝对预览，不触及 GPU。
 * 异常与错误: 平行或不有限的交点返回 false。
 * 维护说明: 世界平移不分解父矩阵，可处理父旋转/负非均匀缩放。
 */
#include "GizmoController.h"

#include "core/Aabb.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
namespace mini3d::renderer_gl {
namespace {
bool planePoint(const core::Ray& ray, glm::vec3 origin, glm::vec3 normal, glm::vec3& point) {
    const float denominator = glm::dot(ray.direction, normal);
    if (std::abs(denominator) < 0.05F) {
        return false;
    }
    const float t = glm::dot(origin - ray.origin, normal) / denominator;
    if (!std::isfinite(t) || t < 0) {
        return false;
    }
    point = ray.origin + ray.direction * t;
    return true;
}
} // namespace
// 只接收可精确表示的 TRS，保留初始缩放符号，不用分解近似吞掉剪切。
static bool extractTransform(const glm::mat4& matrix, const core::Transform& before,
                             core::Transform& result) {
    auto value = before;
    glm::mat3 rotation(1);
    for (int axis = 0; axis < 3; ++axis) {
        value.scale[axis] =
            glm::length(glm::vec3(matrix[axis])) * (before.scale[axis] < 0 ? -1.0F : 1.0F);
        if (std::abs(value.scale[axis]) < 0.001F) {
            return false;
        }
        rotation[axis] = glm::vec3(matrix[axis]) / value.scale[axis];
    }
    value.rotation = glm::normalize(glm::quat_cast(rotation));
    if (!value.isValid()) {
        return false;
    }
    const auto reconstructed = value.localMatrix();
    for (int col = 0; col < 3; ++col) {
        if (glm::length(glm::vec3(matrix[col] - reconstructed[col])) >
            1.0e-4F * std::max(1.0F, glm::length(glm::vec3(matrix[col])))) {
            return false;
        }
    }
    result = value;
    return true;
}
int GizmoController::pickAxis(const core::Ray& worldRay, const GizmoHandle& handle,
                              GizmoTool tool) {
    const auto inverseBasis = glm::transpose(handle.basis);
    const core::Ray ray{handle.origin + inverseBasis * (worldRay.origin - handle.origin),
                        inverseBasis * worldRay.direction};
    if (handle.length <= 0) {
        return -1;
    }
    int picked = -1;
    float nearest = std::numeric_limits<float>::infinity();
    if (tool == GizmoTool::Scale) {
        float distance;
        if (core::intersectRayAabb(ray,
                                   {handle.origin - glm::vec3(0.10F * handle.length),
                                    handle.origin + glm::vec3(0.10F * handle.length)},
                                   distance)) {
            return 3; // 中心方块为等比缩放。
        }
    }
    if (tool == GizmoTool::Move) {
        for (int normalAxis = 0; normalAxis < 3; ++normalAxis) {
            glm::vec3 normal(0), point;
            normal[normalAxis] = 1;
            if (!planePoint(ray, handle.origin, normal, point)) {
                continue;
            }
            const auto offset = (point - handle.origin) / handle.length;
            const int a = (normalAxis + 1) % 3, b = (normalAxis + 2) % 3;
            if (offset[a] >= 0.2F && offset[a] <= 0.4F && offset[b] >= 0.2F && offset[b] <= 0.4F) {
                const auto distance = glm::length(point - ray.origin);
                if (distance < nearest) {
                    picked = normalAxis + 3;
                    nearest = distance;
                }
            }
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (tool == GizmoTool::Rotate) {
            glm::vec3 normal(0), point;
            normal[axis] = 1;
            if (planePoint(ray, handle.origin, normal, point) &&
                std::abs(glm::length(point - handle.origin) - handle.length) <
                    handle.length * 0.07F) {
                const float distance = glm::length(point - ray.origin);
                if (distance < nearest) {
                    picked = axis;
                    nearest = distance;
                }
            }
            continue;
        }
        if (std::abs(ray.direction[axis]) > 0.998F) {
            continue;
        }
        core::Aabb bounds{handle.origin - glm::vec3(0.07F * handle.length),
                          handle.origin + glm::vec3(0.07F * handle.length)};
        bounds.minimum[axis] = handle.origin[axis] + 0.15F * handle.length;
        bounds.maximum[axis] = handle.origin[axis] + handle.length;
        float distance;
        if (core::intersectRayAabb(ray, bounds, distance) && distance < nearest) {
            picked = axis;
            nearest = distance;
        }
    }
    return picked;
}
bool GizmoController::begin(const core::Ray& ray, int axis, const GizmoHandle& handle,
                            const glm::mat4& parentWorld, const core::Transform& before,
                            GizmoTool tool) {
    end();
    if (axis < 0 ||
        axis > (tool == GizmoTool::Move    ? 5
                : tool == GizmoTool::Scale ? 3
                                           : 2) ||
        handle.length <= 0 || !before.isValid()) {
        return false;
    }
    const bool plane = tool == GizmoTool::Move && axis >= 3;
    axisDirection_ = handle.basis[axis < 3 ? axis : plane ? axis - 3 : 0];
    // 等比缩放使用视平面内的横向方向，中心方块可从任意视角拖动。
    if (axis == 3 && tool == GizmoTool::Scale) {
        axisDirection_ = handle.viewRight;
    }
    const auto projected = ray.direction - axisDirection_ * glm::dot(ray.direction, axisDirection_);
    if (tool != GizmoTool::Rotate && !plane && glm::length(projected) < 0.05F) {
        return false;
    }
    normal_ = tool == GizmoTool::Rotate || plane ? axisDirection_ : glm::normalize(projected);
    origin_ = handle.origin;
    glm::vec3 point;
    if (!planePoint(ray, origin_, normal_, point)) {
        return false;
    }
    inverseParent_ = glm::inverse(parentWorld);
    parentWorld_ = parentWorld;
    tool_ = tool;
    space_ = handle.space;
    length_ = handle.length;
    if (tool == GizmoTool::Rotate && glm::length(point - origin_) < 0.001F) {
        return false;
    }
    startDirection_ = tool == GizmoTool::Rotate ? glm::normalize(point - origin_) : glm::vec3(0);
    start_ = glm::dot(point - origin_, axisDirection_);
    startPoint_ = point;
    basis_ = handle.basis;
    before_ = before;
    axis_ = axis;
    return true;
}
bool GizmoController::preview(const core::Ray& ray, core::Transform& result, bool snap) const {
    glm::vec3 point;
    if (axis_ < 0 || !planePoint(ray, origin_, normal_, point)) {
        return false;
    }
    auto value = before_;
    const float displacement = glm::dot(point - origin_, axisDirection_) - start_;
    const auto snapped = [snap](float value, float step) {
        return snap ? std::round(value / step) * step : value;
    };
    if (tool_ == GizmoTool::Move) {
        auto delta = axisDirection_ * snapped(displacement, 0.5F);
        if (axis_ >= 3) {
            auto local = glm::transpose(basis_) * (point - startPoint_);
            local[axis_ - 3] = 0;
            for (int axis = 0; axis < 3; ++axis) {
                local[axis] = snapped(local[axis], 0.5F);
            }
            delta = basis_ * local;
        }
        value.position += glm::vec3(inverseParent_ * glm::vec4(delta, 0));
    } else {
        glm::mat4 operation(1);
        if (tool_ == GizmoTool::Rotate) {
            if (glm::length(point - origin_) < 0.001F) {
                return false;
            }
            const auto direction = glm::normalize(point - origin_);
            const float angle =
                snapped(std::atan2(glm::dot(axisDirection_, glm::cross(startDirection_, direction)),
                                   glm::dot(startDirection_, direction)),
                        glm::radians(15.0F));
            if (std::abs(angle) < 1.0e-6F) {
                result = before_;
                return true;
            }
            operation = glm::rotate(operation, angle, axisDirection_);
            if (space_ == GizmoSpace::Local) {
                glm::vec3 axis(0);
                axis[axis_] = 1;
                value.rotation = glm::normalize(before_.rotation * glm::angleAxis(angle, axis));
                result = value;
                return value.isValid();
            }
        } else {
            const float minimumScale =
                space_ == GizmoSpace::Local && axis_ < 3
                    ? std::abs(before_.scale[axis_])
                    : std::min({std::abs(before_.scale.x), std::abs(before_.scale.y),
                                std::abs(before_.scale.z)});
            const float factor =
                std::max(0.001F / minimumScale, 1.0F + snapped(displacement / length_, 0.1F));
            if (std::abs(factor - 1.0F) < 1.0e-6F) {
                result = before_;
                return true;
            }
            if (axis_ == 3) {
                value.scale *= factor;
                if (!value.isValid()) {
                    return false;
                }
                result = value;
                return true;
            }
            if (space_ == GizmoSpace::Local) {
                value.scale[axis_] *= factor;
                if (!value.isValid()) {
                    return false;
                }
                result = value;
                return true;
            }
            glm::mat3 stretch(1);
            stretch += (factor - 1.0F) * glm::outerProduct(axisDirection_, axisDirection_);
            operation = glm::mat4(stretch);
        }
        if (!extractTransform(inverseParent_ * operation * parentWorld_ * before_.localMatrix(),
                              before_, value)) {
            return false;
        }
    }
    if (!value.isValid()) {
        return false;
    }
    result = value;
    return true;
}
void GizmoController::end() {
    axis_ = -1;
}
int GizmoController::activeAxis() const {
    return axis_;
}
} // namespace mini3d::renderer_gl
