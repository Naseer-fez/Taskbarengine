# IconHover Plugin — macOS Dock-style icon magnification
# Mixed C/C++ module: C for plugin ABI + magnification math, C++ for COM/DComp

add_library(IconHoverPlugin SHARED
    icon_hover.c
    magnification.c
    uia_discovery.cpp
    icon_capture.cpp
    dcomp_overlay.cpp
    frame_loop.cpp
    gsmtc_source.cpp
    dynamic_island.cpp
)

target_include_directories(IconHoverPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(IconHoverPlugin PRIVATE
    te_sdk
    dcomp
    d2d1
    d3d11
    dxguid
    ole32
    oleaut32
    shell32
    user32
    comctl32
    dwmapi
    uuid
    windowscodecs
    shlwapi
    windowsapp
    dwrite
)

# _USE_MATH_DEFINES for M_PI in magnification.c
target_compile_definitions(IconHoverPlugin PRIVATE _USE_MATH_DEFINES)

# Ensure the library name matches the folder name for auto-discovery
set_target_properties(IconHoverPlugin PROPERTIES OUTPUT_NAME "icon_hover")

# Copy the built plugin to the build modules directory
add_custom_command(TARGET IconHoverPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:IconHoverPlugin>
    ${CMAKE_BINARY_DIR}/Modules/icon_hover/$<TARGET_FILE_NAME:IconHoverPlugin>
)
