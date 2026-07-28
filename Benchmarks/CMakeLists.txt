include(FetchContent)

if(MINGW)
    message(STATUS "Skipping Google Benchmark target for MinGW preset; use MSVC or clang-cl presets for te_benchmarks.")
    return()
endif()

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.0
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(te_benchmarks
    bench_event_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/Core/src/event_dispatch.c
    ${CMAKE_SOURCE_DIR}/Core/src/fault_isolation.c
)

target_include_directories(te_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/include
    ${CMAKE_SOURCE_DIR}/SDK/include
)

target_link_libraries(te_benchmarks PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    te_sdk
    ole32
)
