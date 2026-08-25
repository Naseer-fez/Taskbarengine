cmake_minimum_required(VERSION 3.24)
project(TaskbarEngine VERSION 1.0.0 LANGUAGES C CXX)

# C17 for all C code, C++17 for C++ code
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict warning levels
if(MSVC)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:/W4>
        $<$<COMPILE_LANGUAGE:C>:/WX>
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/WX>
    )
else()
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:-Wall>
        $<$<COMPILE_LANGUAGE:C>:-Wextra>
        $<$<COMPILE_LANGUAGE:C>:-Werror>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Werror>
    )
endif()

# Release optimization flags
if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /GL")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# Sub-projects
add_subdirectory(ThirdParty/cJSON)
add_subdirectory(SDK)
add_subdirectory(Core)
add_subdirectory(Modules/dummy)
add_subdirectory(App)

# Tests (only when building tests)
option(TE_BUILD_TESTS "Build unit tests" ON)
if(TE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
