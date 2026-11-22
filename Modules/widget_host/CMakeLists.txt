add_library(widget_host SHARED
    widget_host.cpp
    widget_host_runtime.cpp
    widget_host_model.cpp
)

target_include_directories(widget_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover
)

target_link_libraries(widget_host PRIVATE
    te_sdk cjson user32 gdi32 comctl32 d2d1 dcomp d3d11 dxgi dwrite windowscodecs shlwapi
)
set_target_properties(widget_host PROPERTIES OUTPUT_NAME "widget_host")

foreach(widget_name IN ITEMS clock sysmon media)
    add_library(${widget_name}_widget SHARED widgets/${widget_name}_widget.cpp)
    target_include_directories(${widget_name}_widget PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        ${CMAKE_SOURCE_DIR}/SDK/include
        ${CMAKE_SOURCE_DIR}/ThirdParty/cJSON
    )
    target_link_libraries(${widget_name}_widget PRIVATE te_sdk cjson d2d1 dwrite advapi32)
    set_target_properties(${widget_name}_widget PROPERTIES OUTPUT_NAME "${widget_name}")
    add_custom_command(TARGET ${widget_name}_widget POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Widgets
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${widget_name}_widget>
                ${CMAKE_BINARY_DIR}/Widgets/$<TARGET_FILE_NAME:${widget_name}_widget>
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Widgets/${widget_name}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${widget_name}_widget>
                ${CMAKE_BINARY_DIR}/Widgets/${widget_name}/$<TARGET_FILE_NAME:${widget_name}_widget>
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/widgets/${widget_name}/widget.json
                ${CMAKE_BINARY_DIR}/Widgets/${widget_name}/widget.json
    )
endforeach()

target_sources(media_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/Modules/icon_hover/gsmtc_source.cpp
)
target_include_directories(media_widget PRIVATE ${CMAKE_SOURCE_DIR}/Modules/icon_hover)
target_link_libraries(media_widget PRIVATE windowsapp ole32 oleaut32)
