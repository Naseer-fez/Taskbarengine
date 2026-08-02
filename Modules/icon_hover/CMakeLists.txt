add_library(IconHoverPlugin SHARED
    icon_hover.c
    magnification.c
    icon_layout.c
    uia_discovery.cpp
    icon_capture.cpp
    dcomp_overlay.cpp
    frame_loop.cpp
)

target_include_directories(IconHoverPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover
)

target_link_libraries(IconHoverPlugin PRIVATE
    te_sdk
    dcomp
    d3d11
    d2d1
    dxgi
    dwmapi
    ole32
    user32
    gdi32
    shell32
    oleaut32
)

set_target_properties(IconHoverPlugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "icon_hover"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Modules"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Modules"
)
