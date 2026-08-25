add_executable(TaskbarEngine WIN32
    src/main.c
    src/tray.c
)

target_include_directories(TaskbarEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(TaskbarEngine PRIVATE
    te_sdk
    comctl32
    shell32
)

# MinGW needs -municode for wWinMain entry point
if(NOT MSVC)
    target_link_options(TaskbarEngine PRIVATE -municode)
endif()

# Embed the manifest (MSVC only)
if(MSVC)
    set_target_properties(TaskbarEngine PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest"
    )
endif()

