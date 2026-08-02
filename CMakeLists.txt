cmake_minimum_required(VERSION 3.25)

project(TaskbarEngine LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TE_VERSION_MAJOR 1)
set(TE_VERSION_MINOR 0)
set(TE_VERSION_PATCH 0)
set(TE_VERSION_STRING "${TE_VERSION_MAJOR}.${TE_VERSION_MINOR}.${TE_VERSION_PATCH}")
add_compile_definitions(TE_VERSION_STRING="${TE_VERSION_STRING}")

# Global Compiler Flags
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN STRICT)
else()
    add_compile_options(-Wall -Wextra -Werror)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

# Global Output Directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(WIN32)
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
    set(CMAKE_SHARED_MODULE_PREFIX "")
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
