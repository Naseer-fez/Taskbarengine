add_library(TaskbarResizePlugin SHARED taskbar_resize.c)

target_include_directories(TaskbarResizePlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty
)

target_link_libraries(TaskbarResizePlugin PRIVATE te_sdk user32)

set_target_properties(TaskbarResizePlugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "taskbar_resize"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Modules"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Modules"
)
