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
)

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
