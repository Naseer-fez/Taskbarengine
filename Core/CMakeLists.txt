add_library(EngineDLL SHARED
    src/dllmain.c
    src/engine_init.c
)

target_include_directories(EngineDLL PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(EngineDLL PRIVATE
    te_sdk
    shlwapi
)

set_target_properties(EngineDLL PROPERTIES
    OUTPUT_NAME "EngineDLL"
)
