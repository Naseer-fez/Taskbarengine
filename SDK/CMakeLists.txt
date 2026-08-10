add_library(te_sdk STATIC
    src/te_jsonc.c
    src/te_log.c
    src/te_log_impl.c
    src/te_dpi.c
    src/te_easing.c
    src/te_ipc.c
    src/te_debug_trace.c
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON/cJSON.c
)

target_include_directories(te_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/cJSON>
)

target_compile_definitions(te_sdk PUBLIC _CRT_SECURE_NO_WARNINGS)
