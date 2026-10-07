# 模块名: ApiLimits 配置
# 功能概述: 校验合同预算并生成内部 C++ 常量，避免跨语言限额漂移。
# 对外接口: 配置 generated/api/ApiLimits.h。
# 依赖关系: methods.json、ApiLimits.h.in。
# 输入输出: 严格正整数 JSON limits 到生成头。
# 异常与错误: 缺失、非正整数或不安全数字使配置失败。
# 维护说明: 只生成构建产物；不改变普通测试构建边界。
set(mini3d_api_methods "${PROJECT_SOURCE_DIR}/api/schema/methods.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mini3d_api_methods}")
file(READ "${mini3d_api_methods}" mini3d_api_contract)
foreach(mini3d_api_limit IN ITEMS
    requestBytes responseBytes jsonDepth jsonNodes connections queuedRequests
    meshVertices meshFaces meshCorners sourcePageDefault sourcePageMaximum
    batchItems candidateBytes captureLongestEdge capturePixels capturePngBytes
    animationTracks animationTotalKeyframes animationTrackKeyframes animationBatchItems
    animationSampleEntities animationPoseNodes animationPreflightVisits
    fileReadBytes fileReadDependencies importedEntities exportObjBytes
    mutationTimeoutMs mutationTimeoutMaximumMs captureTimeoutMs captureTimeoutMaximumMs
    cachedResultsPerSession cachedResultBytes sessions disconnectedSessionRetentionMs)
    string(JSON ${mini3d_api_limit} GET "${mini3d_api_contract}" limits "${mini3d_api_limit}")
    if(NOT "${${mini3d_api_limit}}" MATCHES "^[1-9][0-9]*$" OR ${${mini3d_api_limit}} GREATER 2147483647)
        message(FATAL_ERROR "Invalid API limit: ${mini3d_api_limit}")
    endif()
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/cmake/ApiLimits.h.in"
    "${PROJECT_BINARY_DIR}/generated/api/ApiLimits.h" @ONLY)
