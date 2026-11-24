@echo off
setlocal
cd /d "%~dp0"

echo Building all CMake targets. Pass -RunTests to include the test suite.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\build.ps1" %*
set "BUILD_EXIT=%ERRORLEVEL%"

if not "%BUILD_EXIT%"=="0" (
    echo.
    echo [ERROR] Build failed with exit code %BUILD_EXIT%.
)

pause
exit /b %BUILD_EXIT%
