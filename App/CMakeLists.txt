add_executable(TaskbarEngine WIN32
    src/main.c
    src/tray.c
    src/tray_menu.c
    src/ipc_client.c
    src/crash_recovery.c
    ${CMAKE_SOURCE_DIR}/Core/src/ipc_protocol.c
    res/app.rc
)

target_include_directories(TaskbarEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${CMAKE_SOURCE_DIR}/Core/include
)

target_link_libraries(TaskbarEngine PRIVATE
    te_sdk
    shell32
    user32
)

set_target_properties(TaskbarEngine PROPERTIES
    OUTPUT_NAME "TaskbarEngine"
)
