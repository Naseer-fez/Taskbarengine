add_library(DummyPlugin SHARED
    dummy_plugin.c
)

target_link_libraries(DummyPlugin PRIVATE
    te_sdk
)

# Place output in Modules directory for plugin discovery
set_target_properties(DummyPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules/dummy"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules/dummy"
)
