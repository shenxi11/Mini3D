# FILE-01：场景保存兼容性

本批沿用已实现的写格式 3、读 1/2/3 和 QSaveFile 原子保存，补齐完整字段和失败保护验证。
2026-10-02 开发实现与 Release 定向验证完成；不作为最终 V2 全量验收结论。

## 坐标与字段

新文件在 `editorState.upAxis` 写固定 `"Y"`。没有该字段的旧文件仍按 Y-up 解释，
显式提供其他轴或错误类型时拒绝载入，不对用户模型做隐式旋转。
游标只有格式 3 解释；自由观察相机仍使用原 `editorCamera`，正交模式不持久化。

源多边形、64 位点/面/面角编号、UV、颜色、可选硬法线、局部 TRS/WXYZ、
相机/灯光、Mirror 的全部参数及游标随文件保存，不写求值缓存、选择或 Undo 历史。
逐面材质当前仅接受 0；非零外部材质引用明确拒绝，未新增材质资源重映射。

## 失败与旧文件

载入先构建临时 Scene/Assets，坏 JSON、坏拓扑/绑定、缺资产、错误 Mirror 或坐标字段
均不得替换现有场景、资源、选区、历史、文件路径、游标与保存点。
对象变换和组件预览也保持：只有成功读完新文档后，才取消旧文档的活动事务并切换。
资源继续使用工程相对路径和 glTF primitive `meshIndex`。
格式 1/2 首次升级须另存新路径；写失败不改保存点，不覆盖旧工程。

## 定向验证入口

CPU `[file-compatibility]`、Editor `[file-compatibility-editor]`，
并复用既有 `[serializer]`、`[editable-document]`、场景文件资源测试。
验证侧重完整往返、损坏原子拒绝和旧工程保护；两配置全量、跨屏/输入法及外部工具联调后置。

Release：CPU 3 用例 / 123 断言、Editor 5 用例 / 271 断言通过。
其中活动预览回归先稳定复现 2 个失败，再修正载入取消时点后通过。
日志位于 `out/validation/v2-completion-file01/`，失败证据保留在
`out/validation/v2-completion-file01-preview-repro/`。
