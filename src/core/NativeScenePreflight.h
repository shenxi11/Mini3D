/*
 * 模块名: NativeScenePreflight
 * 功能概述: 固定内存识别原生版本，再在格式4构造DOM前校验结构预算。
 * 对外接口: NativeSceneProbe、probeNativeSceneVersion、validateNativeSceneStructure。
 * 依赖关系: 标准流、Animation；实现依赖nlohmann SAX，不依赖Qt。
 * 输入输出: 从流当前位置读取到EOF，输出版本路由/实际字节或中文诊断。
 * 异常与错误: 失败不发布场景；流和分配异常沿调用方处理。
 * 维护说明: 探针不证明JSON合法；旧版本不施加新的格式4深度/字节限制。
 */
#pragma once
#include <cstddef>
#include <istream>
#include <string>
#include <string_view>

namespace mini3d::core {
inline constexpr std::size_t kNativeSceneMaximumBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kNativeSceneMaximumDepth = 64;
struct NativeSceneProbe {
    std::size_t sourceBytes = 0;
    std::size_t versionFields = 0;
    bool includesVersion4 = false;
    bool operator==(const NativeSceneProbe&) const = default;
};
/** @brief 固定内存完整扫描；识别转义顶层键，记录所有版本字段，输出仅成功时更新。 */
bool probeNativeSceneVersion(std::istream& input, NativeSceneProbe& result, std::string& error);
/** @brief 已探测为4的流必须复位到起点；先预算，再SAX严格动画字段/计数，不建立DOM。 */
bool validateNativeSceneStructure(std::istream& input, const NativeSceneProbe& probe,
                                  std::string& error);
/** @brief 对已由调用者持有的文本零复制探针/SAX预检，不提前复制整份输入。 */
bool validateNativeSceneText(std::string_view text, NativeSceneProbe& result, std::string& error);
} // namespace mini3d::core
