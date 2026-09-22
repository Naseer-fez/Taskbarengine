include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Build a static test library from Core sources so unit tests can
# call internal functions without DLL export complexity.
add_library(te_core_testlib STATIC
    ${CMAKE_SOURCE_DIR}/Core/src/config.c
    ${CMAKE_SOURCE_DIR}/Core/src/event_dispatch.c
    ${CMAKE_SOURCE_DIR}/Core/src/fault_isolation.c
    ${CMAKE_SOURCE_DIR}/Core/src/state_store.c
    ${CMAKE_SOURCE_DIR}/Core/src/plugin_loader.c
    ${CMAKE_SOURCE_DIR}/Core/src/shell_hook.c
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_transparency/composition_api.c
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_transparency/tray_discovery.c
)

target_include_directories(te_core_testlib PUBLIC
    ${CMAKE_SOURCE_DIR}/Core/include
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_transparency
)

target_link_libraries(te_core_testlib PUBLIC
    te_sdk
    cjson
    shlwapi
)

add_library(te_resize_testlib STATIC
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_resize/taskbar_resize.c
)

target_include_directories(te_resize_testlib PUBLIC
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_resize
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
)

target_link_libraries(te_resize_testlib PUBLIC
    te_sdk
    cjson
    comctl32
    user32
)

# Static test library for magnification math (pure C, no Win32 deps)
add_library(te_magnify_testlib STATIC
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/magnification.c
)

target_include_directories(te_magnify_testlib PUBLIC
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover
)

target_compile_definitions(te_magnify_testlib PRIVATE _USE_MATH_DEFINES)

# Static test library for icon_hover module internals
add_library(te_icon_hover_testlib STATIC
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/icon_hover.c
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/magnification.c
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/uia_discovery.cpp
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/icon_capture.cpp
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/dcomp_overlay.cpp
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/frame_loop.cpp
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/dynamic_island.cpp
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/gsmtc_source.cpp
)

target_include_directories(te_icon_hover_testlib PUBLIC
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
)

target_link_libraries(te_icon_hover_testlib PUBLIC
    te_sdk
    dcomp
    d2d1
    d3d11
    dxguid
    ole32
    oleaut32
    shell32
    user32
    comctl32
    dwmapi
    uuid
    windowscodecs
    shlwapi
    windowsapp
    dwrite
)

target_compile_definitions(te_icon_hover_testlib PRIVATE _USE_MATH_DEFINES TE_HOVER_TESTLIB)

add_executable(te_tests
    test_jsonc.cpp
    test_plugin_loader.cpp
    test_event_dispatch.cpp
    test_dpi.cpp
    test_config.cpp
    test_ring_buffer.cpp
    test_ipc_protocol.cpp
    test_taskbar_resize.cpp
    test_crash_recovery.cpp
    test_magnification.cpp
    test_state_store.cpp
    test_icon_hover.cpp
    test_config_io.cpp
    ${CMAKE_SOURCE_DIR}/App/src/config_io.cpp
    test_dynamic_island.cpp
    test_phase1_verification.cpp
    test_phase2_verification.cpp
    test_phase3_verification.cpp
    test_phase4_verification.cpp
    test_phase5_verification.cpp
)

target_compile_definitions(te_tests PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(te_tests PRIVATE
    te_core_testlib
    te_resize_testlib
    te_magnify_testlib
    te_icon_hover_testlib
    te_sdk
    cjson
    shell32
    dcomp
    d2d1
    d3d11
    dxguid
    windowscodecs
    shlwapi
    windowsapp
    dwrite
    Catch2::Catch2WithMain
)

# Register tests with CTest
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(te_tests)
