@echo off
setlocal

rem Clean conflicting GCC paths from PATH if present
set PATH=%PATH:C:\Users\FEZ NASEER\Codes\C\bin;=%
set PATH=%PATH:C:\Users\FEZ NASEER\Codes\C\bin=%

rem Setup MSVC environment
if exist "D:\Extras\ES\msvc\setup_x64.bat" (
    call "D:\Extras\ES\msvc\setup_x64.bat"
) else if exist "D:\Extras\ES\msvc\VC\Auxiliary\Build\vcvars64.bat" (
    call "D:\Extras\ES\msvc\VC\Auxiliary\Build\vcvars64.bat"
)

rem Configure with Ninja
cmake -B build_msvc -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DWINAPPSDK_CPPWINRT="D:/Extras/ES/packages/winappsdk_cppwinrt" ^
    -DWINAPPSDK_FRAMEWORK_DIR="D:/Extras/ES/packages/Microsoft.WindowsAppSDK.1.5.240311000/tools/MSIX/win10-x64/extracted" ^
    -DWINAPPSDK_ROOT="D:/Extras/ES/packages/Microsoft.WindowsAppSDK.1.5.240311000"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] CMake configuration failed.
    pause
    exit /b %ERRORLEVEL%
)

rem Build project
cmake --build build_msvc --config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b %ERRORLEVEL%
)

rem Run tests
echo.
echo Running unit tests...
cd build_msvc
ctest -C Release --output-on-failure
cd ..

echo.
echo ========================================
echo Build and tests completed successfully!
echo ========================================
pause
