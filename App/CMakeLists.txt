add_executable(TaskbarEngine WIN32
    src/main.c
    src/tray.c
    src/tray_menu.c
    src/ipc_client.c
    src/crash_recovery.c
    src/scheduler.cpp
    res/app.rc
)

target_include_directories(TaskbarEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(TaskbarEngine PRIVATE
    te_sdk
    shell32
    user32
    taskschd
)

if(MSVC)
    target_link_libraries(TaskbarEngine PRIVATE comsuppw)
endif()

set_target_properties(TaskbarEngine PROPERTIES
    OUTPUT_NAME "TaskbarEngine"
)

# GUI Target (Requires C++17, MSVC, and Windows App SDK)
if(MSVC)
add_executable(TaskbarEngineSettings WIN32
    src/gui_main.cpp
    src/settings_page.cpp
    src/about_page.cpp
    src/gui_ipc_client.cpp
    src/config_io.cpp
    res/app.rc
)

target_include_directories(TaskbarEngineSettings PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/include
)

target_link_libraries(TaskbarEngineSettings PRIVATE
    te_sdk
    windowsapp
)
endif()
