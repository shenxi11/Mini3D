# 模块名: mini3d_tests 构建定义
# 功能概述: 分别声明纯 CPU 数学测试和真实驱动的离屏 GPU 测试。
# 对外接口: mini3d_tests、mini3d_gpu_tests Target 与对应 CTest 入口
# 依赖关系: Catch2、GLM、mini3d_core；GPU 测试另依赖 mini3d_renderer_gl
# 输入输出: 输入实现与测试源码，输出按 headless/gpu 标签执行的测试程序。
# 异常与错误: 测试编译或断言失败时由 MSBuild、Catch2 和 CTest 报告。
# 维护说明: CPU 测试直接编译相机与几何源码，不引入 Qt DLL 或 OpenGL。

add_executable(
    mini3d_tests
    EditorCameraTests.cpp
    PrimitiveFactoryTests.cpp
    EditableMeshTests.cpp
    MeshDerivationTests.cpp
    EditableSceneTests.cpp
    ComponentSelectionTests.cpp
    VertexTransformTests.cpp
    ExtrudeRegionTests.cpp
    InsetFaceTests.cpp
    LoopCutTests.cpp
    FillDeleteTests.cpp
    MirrorTests.cpp
    Mirror02Tests.cpp
    ObjExporterTests.cpp
    TopologySelectionTests.cpp
    ProportionalTransformTests.cpp
    BevelEdgeTests.cpp
    SubdivisionTests.cpp
    SubdivisionSceneTests.cpp
    CollectionTests.cpp
    "${PROJECT_SOURCE_DIR}/src/editor/ComponentSelection.cpp"
    GeometryMathTests.cpp
    SceneTests.cpp
    SceneSerializerTests.cpp
    FileCompatibilityTests.cpp
    GizmoTests.cpp
    NumericInputBufferTests.cpp
    ObjectTransformMathTests.cpp
    "${PROJECT_SOURCE_DIR}/src/editor/operations/ObjectTransformMath.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/operations/NumericInputBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/GizmoController.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/EditorCamera.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/EditorCamera.h"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/MeshData.h"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/PrimitiveFactory.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/PrimitiveFactory.h"
)

target_include_directories(mini3d_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(
    mini3d_tests
    PRIVATE
        Catch2::Catch2WithMain
        glm::glm
        mini3d_core
        nlohmann_json::nlohmann_json
        mini3d_project_options
)

add_test(NAME mini3d_unit_tests COMMAND mini3d_tests)
set_tests_properties(mini3d_unit_tests PROPERTIES LABELS "unit;headless")

add_executable(mini3d_asset_tests GltfImporterTests.cpp RayCasterTests.cpp ComponentPickerTests.cpp
    ComponentPickerPerformanceTests.cpp
    "${PROJECT_SOURCE_DIR}/src/editor/operations/ComponentPicker.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/ComponentSelection.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/EditorCamera.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/RayCaster.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer_gl/PrimitiveFactory.cpp")
target_link_libraries(mini3d_asset_tests PRIVATE mini3d_assets Catch2::Catch2WithMain
                      nlohmann_json::nlohmann_json mini3d_project_options)
target_compile_definitions(mini3d_asset_tests PRIVATE
    MINI3D_SAMPLE_DIRECTORY="${PROJECT_SOURCE_DIR}/assets/samples")
add_test(NAME mini3d_asset_tests COMMAND mini3d_asset_tests)
set_tests_properties(mini3d_asset_tests PROPERTIES LABELS "unit;assets;headless"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

# 独立的 GPU 验收需要真实驱动；不会把 Qt 依赖带入 mini3d_tests。
add_executable(mini3d_gpu_tests GpuTextureTests.cpp InfiniteGridTests.cpp)
target_link_libraries(mini3d_gpu_tests PRIVATE mini3d_renderer_gl mini3d_project_options
                      Catch2::Catch2)
add_test(NAME mini3d_gpu_tests COMMAND mini3d_gpu_tests)
set_tests_properties(mini3d_gpu_tests PROPERTIES
    LABELS "integration;gpu"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# 编辑器联动验收与纯 CPU 测试隔离，需要真实窗口和 OpenGL 驱动。
add_executable(mini3d_editor_tests SceneEditorTests.cpp EditorCommandTests.cpp GizmoEditorTests.cpp
    SceneDocumentTests.cpp DocumentEditorTests.cpp LocalizationTests.cpp WorkbenchTests.cpp
    NumericFieldTests.cpp KeymapRouterTests.cpp OperatorSearchTests.cpp QuickFavoritesTests.cpp
    ObjectTransformSessionTests.cpp KeymapPreferencesTests.cpp ViewportNavigationTests.cpp
    AreaMaximizerTests.cpp EditableDocumentTests.cpp EditModeTests.cpp ComponentBoxTests.cpp
    ComponentTransformTests.cpp ExtrudeRegionEditorTests.cpp HistoryServiceTests.cpp
    HistoryEditorTests.cpp LastOperationPanelTests.cpp InsetFaceEditorTests.cpp LoopCutEditorTests.cpp
    FillDeleteEditorTests.cpp CursorEditorTests.cpp PivotEditorTests.cpp SnapEditorTests.cpp
    VisibilityEditorTests.cpp MirrorEditorTests.cpp FileCompatibilityEditorTests.cpp
    ObjExportEditorTests.cpp TopologySelectionEditorTests.cpp ProportionalEditorTests.cpp
    BevelEditorTests.cpp SubdivisionEditorTests.cpp CollectionEditorTests.cpp P1UiEditorTests.cpp
    V2AcceptanceTests.cpp)
target_link_libraries(mini3d_editor_tests PRIVATE mini3d_editor_ui Qt6::Test
                      Catch2::Catch2 mini3d_project_options)
target_compile_definitions(mini3d_editor_tests PRIVATE
    MINI3D_SAMPLE_DIRECTORY="${PROJECT_SOURCE_DIR}/assets/samples")
add_test(NAME mini3d_editor_tests COMMAND mini3d_editor_tests)
set_tests_properties(mini3d_editor_tests PROPERTIES
    LABELS "integration;ui;gpu"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
