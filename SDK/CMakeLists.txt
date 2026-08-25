add_library(te_sdk STATIC
    src/te_jsonc.c
    src/te_log_impl.c
    src/te_dpi.c
)

target_include_directories(te_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link against vendored cJSON
target_link_libraries(te_sdk PRIVATE cjson)
