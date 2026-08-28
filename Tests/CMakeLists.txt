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
)

target_include_directories(te_core_testlib PUBLIC
    ${CMAKE_SOURCE_DIR}/Core/include
)

target_link_libraries(te_core_testlib PUBLIC
    te_sdk
    cjson
    shlwapi
)

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
)

target_compile_definitions(te_tests PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(te_tests PRIVATE
    te_core_testlib
    te_sdk
    cjson
    Catch2::Catch2WithMain
)

# Register tests with CTest
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(te_tests)
