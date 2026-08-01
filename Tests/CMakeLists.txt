include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(te_tests
    test_jsonc.cpp
    test_config.cpp
    test_event_dispatch.cpp
    test_plugin_loader.cpp
    test_ring_buffer.cpp
    test_dpi.cpp
    test_ipc_protocol.cpp
    test_taskbar_resize.cpp
    test_crash_recovery.cpp
    test_state_store.cpp
    test_magnification.cpp
    test_icon_scales.cpp
    ${CMAKE_SOURCE_DIR}/Core/src/config.c
    ${CMAKE_SOURCE_DIR}/Core/src/event_dispatch.c
    ${CMAKE_SOURCE_DIR}/Core/src/plugin_loader.c
    ${CMAKE_SOURCE_DIR}/Core/src/fault_isolation.c
    ${CMAKE_SOURCE_DIR}/Core/src/ipc_protocol.c
    ${CMAKE_SOURCE_DIR}/Core/src/state_store.c
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_resize/taskbar_resize.c
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/magnification.c
    ${CMAKE_SOURCE_DIR}/App/src/crash_recovery.c
)

target_include_directories(te_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/include
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/App/include
    ${CMAKE_SOURCE_DIR}/Modules/taskbar_resize
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover
    ${CMAKE_SOURCE_DIR}/ThirdParty
)

target_compile_definitions(te_tests PRIVATE
    TE_TEST_MODULES_DIR=L"${CMAKE_BINARY_DIR}/Modules"
)

target_link_libraries(te_tests PRIVATE
    Catch2::Catch2WithMain
    te_sdk
    shlwapi
    ole32
    shell32
    user32
)

add_dependencies(te_tests DummyPlugin FaultPlugin)

if(MINGW)
    target_link_options(te_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()

include(CTest)
include(Catch)
catch_discover_tests(te_tests)
