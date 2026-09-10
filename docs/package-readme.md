# Mini3D Studio 中文本地验收包

这是现有功能的 Windows x64 验收包，不是正式 v1.0.0；程序内部版本仍为 0.1.0。
无需 Qt Creator。解压后运行根目录 Mini3DStudio.exe，保留 DLL、插件和 assets 的相对位置。
需要 Windows 10/11 x64 和支持 OpenGL 4.1 的显卡驱动。不要从 ZIP 内直接运行。

文件 → 打开场景 打开 assets/scenes/showcase.m3dscene。先另存副本，再编辑材质、光照、
层级和视角。Ctrl+S 保存、Ctrl+O 打开、Ctrl+N 新建、Ctrl+Shift+S 另存。
中键旋转视角，Shift+中键平移，滚轮缩放，F 聚焦，W 开关世界移动手柄。
Ctrl+Z 撤销，Ctrl+Y 重做，Ctrl+D 复制，Delete 删除（对象快捷键在树/视口生效）。

中文界面与当前操作见 docs/localization.md、docs/camera-light.md；第七/八周文档与视频是历史记录。移走场景时也需保持模型/纹理的相对路径；
场景与模型须在同一盘符。创建 → 相机/方向光 可添加独立设备，在属性面板编辑或预览。
保存写入格式 2，兼容读取格式 1/2；旧第八周程序不能读取新文件，请先另存副本。
程序默认简体中文，Qt 标准译文已内嵌，无需单独安装 Qt 或拷贝 translations。
用户名称、导入名称和原始驱动诊断保留；尚无自动保存或发布安装器。

本项目许可证尚未确定，第三方许可条件仍需正式发布核查。
请阅读 docs/third-party-notices.md；不要把本地候选包视为已获授权的公开分发版本。
manifest.json 提供各文件 SHA256，用于检查交付完整性，不是代码签名。
