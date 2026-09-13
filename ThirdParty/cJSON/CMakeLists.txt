add_library(cjson STATIC
    cJSON.c
)

target_include_directories(cjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Suppress warnings in vendored code — this is third-party code we do not modify
if(MSVC)
    target_compile_options(cjson PRIVATE /W0)
else()
    target_compile_options(cjson PRIVATE -w)
endif()
