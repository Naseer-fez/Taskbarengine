add_library(DummyPlugin SHARED dummy_plugin.c)

target_include_directories(DummyPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty
)

target_link_libraries(DummyPlugin PRIVATE te_sdk)

set_target_properties(DummyPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules"
)
