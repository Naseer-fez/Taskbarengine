add_executable(TaskbarEngine WIN32
    src/main.c
    src/tray.c
    src/tray_menu.c
    src/ipc_client.c
    src/crash_recovery.c
    src/scheduler.cpp
    res/app.rc
)

target_include_directories(TaskbarEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(TaskbarEngine PRIVATE
    te_sdk
    shell32
    user32
    taskschd
)

if(MSVC)
    target_link_libraries(TaskbarEngine PRIVATE comsuppw)
endif()

set_target_properties(TaskbarEngine PROPERTIES
    OUTPUT_NAME "TaskbarEngine"
)

# GUI Target (Requires C++17, MSVC, and Windows App SDK)
if(MSVC)

if(DEFINED ENV{WINAPPSDK_ROOT} AND NOT WINAPPSDK_ROOT)
    set(WINAPPSDK_ROOT "$ENV{WINAPPSDK_ROOT}" CACHE PATH "Path to Windows App SDK package root")
else()
    set(WINAPPSDK_ROOT "D:/Extras/ES/packages/Microsoft.WindowsAppSDK.1.5.240311000" CACHE PATH "Path to Windows App SDK package root")
endif()

if(DEFINED ENV{WINAPPSDK_CPPWINRT} AND NOT WINAPPSDK_CPPWINRT)
    set(WINAPPSDK_CPPWINRT "$ENV{WINAPPSDK_CPPWINRT}" CACHE PATH "Path to C++/WinRT headers for Windows App SDK")
else()
    set(WINAPPSDK_CPPWINRT "D:/Extras/ES/packages/winappsdk_cppwinrt" CACHE PATH "Path to C++/WinRT headers for Windows App SDK")
endif()

if(DEFINED ENV{WINAPPSDK_FRAMEWORK_DIR} AND NOT WINAPPSDK_FRAMEWORK_DIR)
    set(WINAPPSDK_FRAMEWORK_DIR "$ENV{WINAPPSDK_FRAMEWORK_DIR}" CACHE PATH "Path to extracted WinAppSDK framework runtime binaries")
else()
    set(WINAPPSDK_FRAMEWORK_DIR "${WINAPPSDK_ROOT}/tools/MSIX/win10-x64/extracted" CACHE PATH "Path to extracted WinAppSDK framework runtime binaries")
endif()

# Generate Merged Manifest using mt.exe
find_program(MT_EXECUTABLE mt.exe
    HINTS
        "D:/Extras/ES/msvc/Windows Kits/10/bin/10.0.19041.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22000.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
        "$ENV{WindowsSdkBinPath}/x64"
)

set(SETTINGS_BASE_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/res/settings.manifest")
set(WINAPPSDK_FOUNDATION_MANIFEST "${WINAPPSDK_ROOT}/manifests/Microsoft.WindowsAppSdk.Foundation.manifest")
set(WINAPPSDK_WINUI_MANIFEST "${WINAPPSDK_ROOT}/manifests/manifests/Microsoft.WindowsAppSdk.WinUI.manifest")
set(SETTINGS_MERGED_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/res/settings_merged.manifest")

if(MT_EXECUTABLE)
    add_custom_command(
        OUTPUT "${SETTINGS_MERGED_MANIFEST}"
        COMMAND "${MT_EXECUTABLE}" -nologo
            -manifest "${SETTINGS_BASE_MANIFEST}"
                      "${WINAPPSDK_FOUNDATION_MANIFEST}"
                      "${WINAPPSDK_WINUI_MANIFEST}"
            -out:"${SETTINGS_MERGED_MANIFEST}"
        DEPENDS
            "${SETTINGS_BASE_MANIFEST}"
            "${WINAPPSDK_FOUNDATION_MANIFEST}"
            "${WINAPPSDK_WINUI_MANIFEST}"
        COMMENT "Generating merged SxS manifest for TaskbarEngineSettings"
    )

    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/res/settings.rc"
        PROPERTIES OBJECT_DEPENDS "${SETTINGS_MERGED_MANIFEST}"
    )
endif()

add_executable(TaskbarEngineSettings WIN32
    src/gui_main.cpp
    src/settings_page.cpp
    src/about_page.cpp
    src/gui_ipc_client.cpp
    src/config_io.cpp
    res/settings.rc
)

target_compile_definitions(TaskbarEngineSettings PRIVATE
    MICROSOFT_WINDOWSAPPSDK_SELFCONTAINED=1
)

target_include_directories(TaskbarEngineSettings PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/include
    ${WINAPPSDK_CPPWINRT}
    ${WINAPPSDK_ROOT}/include
)

target_link_directories(TaskbarEngineSettings PRIVATE
    ${WINAPPSDK_ROOT}/lib/win10-x64
)

target_link_libraries(TaskbarEngineSettings PRIVATE
    te_sdk
    windowsapp
    Microsoft.WindowsAppRuntime
)

set_target_properties(TaskbarEngineSettings PROPERTIES
    VS_PACKAGE_REFERENCES "Microsoft.WindowsAppSDK_1.5.240311000;Microsoft.Windows.CppWinRT_2.0.240111.5"
)

# Post-build: Copy Windows App SDK runtime DLLs and resources next to the executable
add_custom_command(TARGET TaskbarEngineSettings POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Deploying self-contained Windows App SDK runtime binaries to bin folder..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${WINAPPSDK_FRAMEWORK_DIR}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMENT "Copying Windows App SDK self-contained binaries to output directory"
)

endif()
set_target_properties(TaskbarEngine PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
