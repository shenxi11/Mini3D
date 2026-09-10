# 第八周演示材料

- [128 秒 MP4](media/mini3d-week8.mp4)：1440×900、H.264、640 帧、5 fps，无音轨。
- [16 秒 GIF 摘要](images/mini3d-week8.gif)：原回放约 8 倍速、720 像素宽。
- [手柄操作截图](images/mini3d-week8.png)。

材料来自真实程序窗口的自动化回放。静态步骤停留与动作播放速度为讲解而调整，
不冒充人工实时录屏，不作为 FPS 或耐久性证据；状态栏标有步骤和 automated replay。
素材使用随仓库的 BoxTextured，不是原方案设想的工业机器人模型。

| 视频时间 | 内容 |
| --- | --- |
| 00:00–00:16 | 应用定位、初始 Demo、Dock 布局 |
| 00:16–00:32 | 静态 GLB 导入与层级 |
| 00:32–00:48 | 实例乘色、场景方向光 |
| 00:48–01:04 | 中键 Orbit 观察视角 |
| 01:04–01:20 | 世界 X 轴手柄真实鼠标拖动 |
| 01:20–01:36 | Ctrl+Z / Ctrl+Y 回放 |
| 01:36–01:52 | 保存、关闭、创建新窗口并重新打开 |
| 01:52–02:08 | 缺失文档错误，当前场景保持不变 |

演示工具对关键状态和截图写入执行断言，640 帧全部生成成功；视频已完整解码检查，
并抽样查看手柄拖动与重开画面。未覆盖的父子编辑、复制/删除和异常输入详见自动化测试，
不把视频视为整个 V1 验收清单。

复现（所有输出必须选新路径，运行时会显示工具拥有的真实窗口）：

```powershell
cmake -S . -B out/build/opengl-viewport-verify -D MINI3D_BUILD_TOOLS=ON
cmake --build out/build/opengl-viewport-verify --config Release --target mini3d_demo
$env:PATH = 'E:/Qt5.15/6.8.3/msvc2022_64/bin;' + $env:PATH
& out/build/opengl-viewport-verify/tools/Release/mini3d_demo.exe assets/samples/BoxTextured.glb out/validation/demo-new
ffmpeg -n -framerate 5 -i out/validation/demo-new/%04d.png -c:v libx264 -crf 23 -pix_fmt yuv420p -movflags +faststart out/validation/demo-new.mp4
```

模型署名和许可见 [样例说明](sample-assets.md)，发布边界见 [第八周说明](week8.md)。
