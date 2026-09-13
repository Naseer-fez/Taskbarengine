add_library(taskbar_transparency SHARED
    composition_api.c
    tray_discovery.c
    transparency_config.c
    taskbar_transparency.c
)

target_include_directories(taskbar_transparency PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
)

target_link_libraries(taskbar_transparency PRIVATE
    te_sdk
    cjson
    user32
    dwmapi
)

# Ensure the library name matches the folder name for auto-discovery
set_target_properties(taskbar_transparency PROPERTIES OUTPUT_NAME "taskbar_transparency")

# Multi-config post-build copy rule copying $<TARGET_FILE> to build modules directory
add_custom_command(TARGET taskbar_transparency POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Modules/taskbar_transparency
    COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:taskbar_transparency>
    ${CMAKE_BINARY_DIR}/Modules/taskbar_transparency/$<TARGET_FILE_NAME:taskbar_transparency>
)
