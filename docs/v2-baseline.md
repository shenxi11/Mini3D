# 二期 M0 基线验证（2026-09-11）

## 结果

业务源码基线 `6dd209f38122aca837ec04077d58b78da2f47726`：Debug/Release 构建和四类
CTest 全部通过；200% 中文布局专项通过。此结果不表示二期功能已经完成。

| 验证 | 实际结果 | 证据 |
| --- | --- | --- |
| Debug build | exit 0，含编辑器/CPU/资产/GPU/UI/tools | 本轮构建命令输出 |
| Debug CTest | 4/4，13.88 秒 | out/validation/v2-baseline-debug.xml |
| Release build | exit 0 | 本轮构建命令输出 |
| Release CTest | 4/4，7.11 秒 | out/validation/v2-baseline-release.xml |
| Debug QT_SCALE_FACTOR=2，`[localization]` | 4 用例，127 断言，exit 0 | out/validation/v2-baseline-dpi200-*.png |

环境：Windows 10.0.26200，Qt 6.8.3 msvc2022_64，MSVC 14.40.33807，CMake 3.31.0-rc1，
PowerShell 7.6.6，OpenGL 4.1，NVIDIA GeForce RTX 3050 Laptop GPU，驱动报告 610.47。
200% 是单机缩放验证，不等于跨显示器/真实输入法候选窗口/全新 Windows 验收。

## 本机构建修复

第一次 build 在 vcpkg applocal 阶段失败：缓存调用已不存在的 PowerShell 7.6.4。
检查发现两个 CMake cache 项和被 Git 忽略的 `CMakeLocalConfig.cmake` 都固定了旧目录。
仅传 cache 值的首次 Configure 仍被本机配置路径校验拒绝。
随后备份该文件到 `out/validation/v2-before-20260911/CMakeLocalConfig.cmake`，只改
7.6.4 → 7.6.6 一行，向本次 Configure 传入当前实际 pwsh 路径，成功生成并构建。
未改系统 PATH、代理、Git 全局配置、项目依赖清单或 Qt Creator 用户配置。

```powershell
$v2Pwsh = (Get-Command pwsh).Source
& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe' -S . -B out/build/opengl-viewport-verify "-DZ_VCPKG_PWSH_PATH:FILEPATH=$v2Pwsh" "-DZ_VCPKG_POWERSHELL_PATH:INTERNAL=$v2Pwsh"
```

重新配置触发 vcpkg ABI 重建，使用缓存源码并恢复了同名依赖；manifest 未改。
当次版本：Catch2 3.15.0、fastgltf 0.9.0、fmt 12.1.0、GLM 1.0.3、nlohmann-json 3.12.0#2、
simdjson 4.6.4、spdlog 1.17.0。vcpkg 依赖构建目录及二进制缓存属于可再生成输出。
未来 PowerShell 商店包升级仍需核对本机配置和 cache 两处，不把机器专属路径提交为公共配置。

## 原始资料与遗留门槛

输入 SHA256：

- R2 DOCX：`6A80D08523033A2F73A48FCF1E33F29C718A8AFDC1E0F572C3F3B92808AAD55C`。
- 效果预览说明1.html：`DC6352B295F6FB26FEEF7D0EC1AA96B1638D152545A5744DB1C79CA733ED69A8`。
- 效果预览说明2.html：`A81C3DECFA0786DC1F1ABB52AC0A9CEF55B7EC8EDE9AA1CFBED07DF24D2B8828`。

V1 已补相机/灯、汉化、交互优化和 Git 基线，但 docs/week8.md 的旧包/视频没有更新。
尚未完成所有者许可决策、全新机器、跨显示器、30 分钟真实交互耐久及正式版本发布；
本轮不自行指定许可证、打 tag、上传 Release 或宣称 V1/V2 正式发布。
