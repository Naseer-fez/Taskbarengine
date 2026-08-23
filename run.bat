@echo off
setlocal

set "BIN_DIR=%~dp0build_msvc\bin"
set "ENGINE_EXE=%BIN_DIR%\TaskbarEngine.exe"
set "SETTINGS_EXE=%BIN_DIR%\TaskbarEngineSettings.exe"

echo ===================================================
echo               Starting TaskbarEngine
echo ===================================================
echo.

if not exist "%ENGINE_EXE%" (
    echo [ERROR] Binary not found at:
    echo         "%ENGINE_EXE%"
    echo.
    echo Please run build.bat first to compile the project.
    echo.
    pause
    exit /b 1
)

rem Check if TaskbarEngine is already running
tasklist /FI "IMAGENAME eq TaskbarEngine.exe" 2>NUL | find /I /N "TaskbarEngine.exe" >NUL
if "%ERRORLEVEL%"=="0" (
    echo [INFO] TaskbarEngine is already running.
    echo [INFO] Restarting process...
    taskkill /F /IM TaskbarEngine.exe >NUL 2>&1
    timeout /t 1 /nobreak >NUL
)

rem Change directory to bin so all relative paths resolve properly
pushd "%BIN_DIR%"

echo [INFO] Launching TaskbarEngine background service and tray icon...
start "" "%ENGINE_EXE%"

echo [INFO] Launching Settings GUI...
start "" "%SETTINGS_EXE%"

popd

echo.
echo ===================================================
echo  TaskbarEngine and GUI started successfully!
echo  Check the system tray icon for quick access.
echo ===================================================
timeout /t 3 >NUL
