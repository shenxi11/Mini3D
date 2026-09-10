# 第三方组件与候选包使用说明

本项目代码尚未由所有者选择许可证。本地候选包用于验收，不代表可公开分发，
更不代表已完成法律合规审查；公开发布前必须确定项目许可证和以下组件的分发义务。

| 组件 | 本机版本 | 许可/用途 |
| --- | --- | --- |
| Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets 与插件 | 6.8.3 | LGPL/GPL/商业多许可，需按实际使用方案核查；动态链接 |
| fastgltf | 0.9.0 | MIT，glTF 读取；随包 DLL |
| simdjson | 4.6.4 | Apache-2.0 或 MIT，fastgltf 依赖；随包 DLL |
| GLM | 1.0.3 | MIT 或 Happy Bunny，数学头文件 |
| nlohmann/json | 3.12.0 | MIT，场景 JSON 头文件 |
| spdlog / fmt | 1.17.0 / 12.1.0 | MIT，构建依赖（当前主程序不直接链接其 DLL） |
| Catch2 | 3.15.0 | BSL-1.0，仅测试，不随用户包部署 |
| MSVC 运行库 | VS 2022 | 微软可再分发组件，按随 Visual Studio 的许可核查 |

打包脚本复制 vcpkg 安装树的原始 copyright 文件和 Qt 安装提供的 SPDX SBOM，
不以本表替代原始许可。Qt 补充第三方组件（例如图片插件）以随包 SBOM 为准。
公开分发前仍须补齐适用的 Qt 许可全文、版权声明、源码获取/替换链接等义务，
以及 MSVC 运行库的分发条件；当前候选包不是已完成这些核验的正式发布。

样例模型的来源、署名、限制和原始许可见 [sample-assets.md](sample-assets.md)
及 `licenses/`。showcase.m3dscene 只设置实例与相对引用，没有修改原模型。
演示使用现有 BoxTextured 和基础几何，不冒称原方案中的机器人模型。

## 中文翻译资源

当前中文版内嵌 Qt 套件提供的 qtbase_zh_CN.qm（Qt Translations），不是本项目自有译文。
它随应用一起进入验收包，适用许可与版权义务也须纳入 Qt 发布核查；不因 --no-translations
部署选项而免除。未翻译或改写第三方法律文本，公开分发仍待上述合规确认。
