add_library(TaskbarResizePlugin SHARED taskbar_resize.c)
target_include_directories(TaskbarResizePlugin PRIVATE ${CMAKE_SOURCE_DIR}/SDK/include ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON)
target_link_libraries(TaskbarResizePlugin PRIVATE comctl32 user32 te_sdk)

# Ensure the library name matches the folder name for auto-discovery
set_target_properties(TaskbarResizePlugin PROPERTIES OUTPUT_NAME "taskbar_resize")

# Copy the built plugin to the build modules directory
add_custom_command(TARGET TaskbarResizePlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:TaskbarResizePlugin>
    ${CMAKE_BINARY_DIR}/Modules/taskbar_resize/$<TARGET_FILE_NAME:TaskbarResizePlugin>
)
