@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo       TaskbarEngine Portable Packaging Script
echo ========================================================
echo.
echo This script clones the repository from GitHub, builds
echo all release binaries, and packages them into a portable
echo self-contained ZIP distribution.
echo.

set "REPO_URL=https://github.com/Naseer-fez/Taskbarengine.git"
set "REPO_BRANCH=V2"
set "TEMP_DIR=%~dp0temp_portable_build"
set "BUILD_DIR=%TEMP_DIR%\build_msvc"

rem --------------------------------------------------------
rem Step 1: Clone or update from GitHub
rem --------------------------------------------------------
if exist "%TEMP_DIR%\.git" (
    echo [1/3] Pulling latest changes from GitHub...
    pushd "%TEMP_DIR%"
    git fetch origin %REPO_BRANCH%
    git checkout %REPO_BRANCH%
    git reset --hard origin/%REPO_BRANCH%
    popd
) else (
    echo [1/3] Cloning repository from GitHub...
    if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"
    git clone --branch %REPO_BRANCH% "%REPO_URL%" "%TEMP_DIR%"
)

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Git clone/pull failed. Check your network connection.
    pause
    exit /b %ERRORLEVEL%
)

rem --------------------------------------------------------
rem Step 2: Build release binaries
rem --------------------------------------------------------
echo.
echo [2/3] Building release binaries from fresh clone...

pushd "%TEMP_DIR%"
call "%TEMP_DIR%\build_no_pause.bat"
popd

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed. Aborting packaging.
    pause
    exit /b %ERRORLEVEL%
)

rem Verify critical binaries exist
if not exist "%BUILD_DIR%\App\TaskbarEngine.exe" (
    echo [ERROR] TaskbarEngine.exe not found after build. Aborting.
    pause
    exit /b 1
)

rem --------------------------------------------------------
rem Step 3: Stage and package into portable ZIP
rem --------------------------------------------------------
echo.
echo [3/3] Running packaging script...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\package.ps1" -BuildDir "%BUILD_DIR%" -OutDir "%~dp0ReleaseStaging" -DestinationZip "%~dp0TaskbarEngine-Portable.zip"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Packaging failed with exit code %ERRORLEVEL%.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================================
echo [SUCCESS] TaskbarEngine-Portable.zip created successfully!
echo Archive: %~dp0TaskbarEngine-Portable.zip
echo ========================================================
pause
