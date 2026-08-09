add_library(FaultPlugin SHARED fault_plugin.c)

target_include_directories(FaultPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty
)

target_link_libraries(FaultPlugin PRIVATE te_sdk)

set_target_properties(FaultPlugin PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Tests/Modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Tests/Modules"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Tests/Modules"
)
