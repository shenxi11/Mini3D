# 模块名: mini3d_editor 构建定义
# 功能概述: 声明 Qt Widgets 编辑器应用及其当前源码边界。
# 对外接口: mini3d_editor 可执行 Target
# 依赖关系: Qt6::Widgets、mini3d_renderer_gl、mini3d_project_options
# 输入输出: 输入编辑器源码，输出 Mini3DStudio Windows 可执行程序。
# 异常与错误: 依赖或源码缺失时由 CMake 在 Configure/Build 阶段报告。
# 维护说明: 只链接已实现模块，Assets 和 Core 随对应里程碑接入。

qt_add_library(
    mini3d_editor_ui STATIC
    MainWindow.cpp
    MainWindow.h
    ChineseUi.cpp ChineseUi.h
    SceneViewModel.cpp SceneViewModel.h
    TransformEntityCommand.cpp TransformEntityCommand.h
    SubtreeCommand.cpp SubtreeCommand.h
    EditCommand.h
    AppearanceInspector.cpp AppearanceInspector.h
    SceneTreeModel.cpp SceneTreeModel.h
    SelectionModel.cpp SelectionModel.h
    TransformInspector.cpp TransformInspector.h
)
set_target_properties(mini3d_editor_ui PROPERTIES AUTOMOC ON)
target_link_libraries(mini3d_editor_ui PUBLIC mini3d_renderer_gl Qt6::Widgets
                      PRIVATE mini3d_project_options)

# 使用当前 Qt 套件的官方译文，随静态 UI 库嵌入，不依赖部署机的 Qt 目录。
get_target_property(mini3d_qmake Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${mini3d_qmake}" -query QT_INSTALL_TRANSLATIONS
    OUTPUT_VARIABLE mini3d_qt_translations OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
set(mini3d_qt_chinese "${mini3d_qt_translations}/qtbase_zh_CN.qm")
if(NOT EXISTS "${mini3d_qt_chinese}")
    message(FATAL_ERROR "缺少 Qt 简体中文资源：${mini3d_qt_chinese}；请安装当前 Qt 套件的 Translations 组件。")
endif()
qt_add_resources(mini3d_editor_ui mini3d_chinese
    PREFIX "/mini3d/translations" BASE "${mini3d_qt_translations}"
    FILES "${mini3d_qt_chinese}")
qt_add_executable(mini3d_editor WIN32 main.cpp)

target_link_libraries(
    mini3d_editor
    PRIVATE
        mini3d_project_options
        mini3d_editor_ui
        Qt6::Widgets
)

set_target_properties(mini3d_editor PROPERTIES OUTPUT_NAME Mini3DStudio)
