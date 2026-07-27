add_executable(TaskbarEngine WIN32
    src/main.c
    src/tray.c
    res/app.rc
)

target_include_directories(TaskbarEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(TaskbarEngine PRIVATE
    te_sdk
    shell32
    user32
)

set_target_properties(TaskbarEngine PROPERTIES
    OUTPUT_NAME "TaskbarEngine"
)
