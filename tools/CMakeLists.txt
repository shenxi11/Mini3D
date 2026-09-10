# 模块名: Mini3D 验收工具构建
# 功能概述: 按需构建离屏性能与稳定性工具，不安装到用户包。
# 对外接口: mini3d_performance；依赖关系: editor_ui、Qt、Psapi。
# 输入输出: 工具源码到可执行程序；异常与错误: 由 CMake/MSBuild 报告。
# 维护说明: MINI3D_BUILD_TOOLS 默认关闭，不改变常规测试入口。
add_executable(mini3d_performance PerformanceProbe.cpp)
target_compile_definitions(mini3d_performance PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(mini3d_performance PRIVATE mini3d_editor_ui mini3d_project_options Psapi)

find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Test)
add_executable(mini3d_demo DemoCapture.cpp)
target_link_libraries(mini3d_demo PRIVATE mini3d_editor_ui mini3d_project_options Qt6::Test)
