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
    src/state_store.c
    src/ipc_server.c
    src/shell_hook.c
    src/power_device.c
    src/vdesktop_notify.cpp
)

set_target_properties(EngineDLL PROPERTIES PREFIX "")


target_include_directories(EngineDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
)

target_link_libraries(EngineDLL PRIVATE
    te_sdk
    comctl32
    shlwapi
)

# Export the CBT hook procedure
target_compile_definitions(EngineDLL PRIVATE TE_ENGINE_EXPORTS)

# Copy EngineDLL to the App directory next to TaskbarEngine.exe
add_custom_command(TARGET EngineDLL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:EngineDLL>
    ${CMAKE_BINARY_DIR}/App/$<TARGET_FILE_NAME:EngineDLL>
)
