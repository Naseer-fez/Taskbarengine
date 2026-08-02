add_executable(TaskbarEngine WIN32
    src/main.c
    src/tray.c
    src/tray_menu.c
    src/ipc_client.c
    src/crash_recovery.c
    src/scheduler.cpp
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
    taskschd
    comsuppw
)

set_target_properties(TaskbarEngine PROPERTIES
    OUTPUT_NAME "TaskbarEngine"
)

# GUI Target (Requires C++17 and Windows App SDK)
add_executable(TaskbarEngineSettings WIN32
    src/gui_main.cpp
    src/settings_page.cpp
    src/about_page.cpp
    src/gui_ipc_client.cpp
    src/config_io.cpp
    ${CMAKE_SOURCE_DIR}/Core/src/ipc_protocol.c
    res/app.rc
)

target_include_directories(TaskbarEngineSettings PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/include
    ${CMAKE_SOURCE_DIR}/SDK/include
)

# Define WinUI 3 requirements here (find_package, target_link_libraries)
# Note: Actual NuGet restore/find_package(WindowsAppSDK) is expected to be handled
# by a wrapper script or vcpkg integration in a real build environment.
target_link_libraries(TaskbarEngineSettings PRIVATE
    te_sdk
    windowsapp
)
