add_library(IconHoverPlugin SHARED
    icon_hover.c
    magnification.c
    uia_discovery.cpp
    icon_capture.cpp
    dcomp_overlay.cpp
    frame_loop.cpp
)

target_include_directories(IconHoverPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/Core/include
    ${CMAKE_SOURCE_DIR}/ThirdParty
)

target_link_libraries(IconHoverPlugin PRIVATE
    te_sdk
    user32
    ole32
    oleaut32
    shell32
    dwmapi
)

set_target_properties(IconHoverPlugin PROPERTIES
    OUTPUT_NAME "icon_hover"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Modules"
)
