cmake_minimum_required(VERSION 3.21)
project(TaskbarEngineHelloWidget LANGUAGES CXX)

add_library(hello SHARED hello_widget.cpp)
target_compile_features(hello PRIVATE cxx_std_17)
target_include_directories(hello PRIVATE
    "${TASKBARENGINE_SDK}/include"
)
target_link_libraries(hello PRIVATE d2d1 dwrite)
set_target_properties(hello PROPERTIES OUTPUT_NAME "hello")
