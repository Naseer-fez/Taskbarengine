cmake_minimum_required(VERSION 3.25)

project(TaskbarEngine LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Global Compiler Flags
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN STRICT)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    add_compile_definitions(TE_DEBUG)
endif()

# Testing
enable_testing()

# Subdirectories
add_subdirectory(SDK)
add_subdirectory(Core)
add_subdirectory(App)
add_subdirectory(Modules/dummy)
add_subdirectory(Modules/fault)
add_subdirectory(Modules/taskbar_resize)
add_subdirectory(Modules/icon_hover)
add_subdirectory(Tests)
add_subdirectory(Benchmarks)
