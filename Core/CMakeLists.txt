add_library(EngineDLL SHARED
    src/dllmain.c
    src/engine_init.c
    src/core_manager.c
    src/config.c
    src/config_watcher.c
    src/event_dispatch.c
    src/plugin_loader.c
    src/fault_isolation.c
    src/taskbar_subclass.c
    src/ipc_server.c
    src/shell_hook.c
    src/power_device.c
    src/vdesktop_notify.cpp
    src/state_store.c
    src/te_msg_filter.c
    src/te_timer.c
)

target_include_directories(EngineDLL PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(EngineDLL PRIVATE
    te_sdk
    shlwapi
    comctl32
    ole32
    shell32
    powrprof
    advapi32
)

set_target_properties(EngineDLL PROPERTIES
    PREFIX ""
    OUTPUT_NAME "EngineDLL"
)
set_target_properties(EngineDLL PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
