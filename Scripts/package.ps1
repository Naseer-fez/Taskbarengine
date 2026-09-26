param (
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0",
    [string]$BuildDir = "$PSScriptRoot\..\build_msvc",
    [string]$OutDir = "$PSScriptRoot\..\ReleaseStaging",
    [string]$DestinationZip = "$PSScriptRoot\..\TaskbarEngine-Portable.zip"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "     TaskbarEngine Portable Packaging Utility           " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# Resolve absolute paths
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$StagingDir = [System.IO.Path]::GetFullPath($OutDir)
$DestinationZip = [System.IO.Path]::GetFullPath($DestinationZip)
$RootDir = [System.IO.Path]::GetFullPath("$PSScriptRoot\..")

Write-Host "Build Directory:       $BuildDir" -ForegroundColor Gray
Write-Host "Staging Directory:     $StagingDir" -ForegroundColor Gray
Write-Host "Destination Archive:   $DestinationZip" -ForegroundColor Gray

# Ensure build artifacts exist
$requiredBins = @(
    "$BuildDir\App\TaskbarEngine.exe",
    "$BuildDir\App\TaskbarEngineHost.exe",
    "$BuildDir\App\TaskbarEngineSettings.exe"
)

foreach ($bin in $requiredBins) {
    if (-not (Test-Path $bin)) {
        Write-Error "Required binary not found: $bin. Please run build.bat first."
        exit 1
    }
}

# 1. Clean and prepare staging directory
Write-Host "`n[1/7] Preparing staging directories..." -ForegroundColor Yellow
if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir -ErrorAction Stop
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\Config" -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\Core" -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\Modules" -Force | Out-Null

# 2. Copy Executables and Core DLL
Write-Host "[2/7] Copying core application binaries..." -ForegroundColor Yellow
Copy-Item "$BuildDir\App\TaskbarEngine.exe" -Destination "$StagingDir\" -Force
Write-Host "  -> TaskbarEngine.exe" -ForegroundColor Green
Copy-Item "$BuildDir\App\TaskbarEngineHost.exe" -Destination "$StagingDir\" -Force
Write-Host "  -> TaskbarEngineHost.exe" -ForegroundColor Green
Copy-Item "$BuildDir\App\TaskbarEngineSettings.exe" -Destination "$StagingDir\" -Force
Write-Host "  -> TaskbarEngineSettings.exe" -ForegroundColor Green

# Copy EngineDLL.dll into root (for TaskbarEngine.exe/Host) and Core/ (for installer/core discovery)
$engineDll = "$BuildDir\Core\EngineDLL.dll"
if (-not (Test-Path $engineDll)) {
    $engineDll = "$BuildDir\App\EngineDLL.dll"
}
Copy-Item $engineDll -Destination "$StagingDir\" -Force
Copy-Item $engineDll -Destination "$StagingDir\Core\" -Force
Write-Host "  -> EngineDLL.dll" -ForegroundColor Green

# 3. Copy Plugins / Modules
Write-Host "[3/7] Copying plugin modules..." -ForegroundColor Yellow
$modules = @(
    @{ Name = "taskbar_resize"; Dll = "taskbar_resize.dll"; Src = "$BuildDir\Modules\taskbar_resize\taskbar_resize.dll" },
    @{ Name = "icon_hover"; Dll = "icon_hover.dll"; Src = "$BuildDir\Modules\icon_hover\icon_hover.dll" },
    @{ Name = "TaskbarPayload"; Dll = "TaskbarPayload.dll"; Src = "$BuildDir\Modules\TaskbarPayload\TaskbarPayload.dll" },
    @{ Name = "dummy"; Dll = "DummyPlugin.dll"; Src = "$BuildDir\Modules\dummy\DummyPlugin.dll" }
)

foreach ($mod in $modules) {
    if (Test-Path $mod.Src) {
        $modDir = "$StagingDir\Modules\$($mod.Name)"
        New-Item -ItemType Directory -Path $modDir -Force | Out-Null
        Copy-Item $mod.Src -Destination "$modDir\$($mod.Dll)" -Force
        # Also copy flat into Modules/ for maximum compatibility
        Copy-Item $mod.Src -Destination "$StagingDir\Modules\$($mod.Dll)" -Force
        Write-Host "  -> Module: $($mod.Name) ($($mod.Dll))" -ForegroundColor Green
    } else {
        Write-Warning "Module binary not found: $($mod.Src)"
    }
}

# 4. Copy Configurations and Assets
Write-Host "[4/7] Copying configurations and graphical assets..." -ForegroundColor Yellow
$configSrc = "$RootDir\Config"
if (Test-Path $configSrc) {
    Copy-Item "$configSrc\*" -Destination "$StagingDir\Config\" -Recurse -Force
    if (Test-Path "$configSrc\default_config.jsonc") {
        Copy-Item "$configSrc\default_config.jsonc" -Destination "$StagingDir\Config\config.jsonc" -Force
        Copy-Item "$configSrc\default_config.jsonc" -Destination "$StagingDir\config.jsonc" -Force
    }
    Write-Host "  -> Config files staged" -ForegroundColor Green
}

# 5. Copy Self-Contained Windows App SDK Runtime Dependencies
Write-Host "[5/7] Deploying self-contained Windows App SDK runtime binaries..." -ForegroundColor Yellow
$appDir = "$BuildDir\App"

# Copy all runtime DLLs, PRIs, winmd, manifest files
Get-ChildItem -Path $appDir -File | Where-Object {
    $_.Extension -in @(".dll", ".pri", ".xbf", ".winmd", ".xml", ".man", ".png") -and
    $_.Name -notin @("TaskbarEngineHost.lib", "TaskbarEngineHost.exp", "TaskbarEngineSettings.lib", "TaskbarEngineSettings.exp")
} | ForEach-Object {
    Copy-Item $_.FullName -Destination $StagingDir -Force
}

# Copy WinAppSDK resource directories (Microsoft.UI.Xaml, Assets, locale folders)
Get-ChildItem -Path $appDir -Directory | Where-Object {
    $_.Name -notin @("CMakeFiles", "Config", "AppxMetadata")
} | ForEach-Object {
    Copy-Item $_.FullName -Destination "$StagingDir\$($_.Name)" -Recurse -Force
}
Write-Host "  -> Windows App SDK self-contained runtime deployed" -ForegroundColor Green

# Copy MSVC Visual C++ 64-bit runtime DLLs for 100% portable execution
$msvcHostDirs = @(
    "D:\Extras\ES\msvc\VC\Tools\MSVC\*\bin\Hostx64\x64",
    "C:\Program Files\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\*"
)

$vcDllNames = @(
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll"
)

foreach ($vcDll in $vcDllNames) {
    $found = $false
    foreach ($pat in $msvcHostDirs) {
        $candidate = Get-ChildItem -Path $pat -Filter $vcDll -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($candidate) {
            Copy-Item $candidate.FullName -Destination $StagingDir -Force
            $found = $true
            break
        }
    }
    if (-not $found -and (Test-Path "C:\Windows\System32\$vcDll")) {
        Copy-Item "C:\Windows\System32\$vcDll" -Destination $StagingDir -Force
        $found = $true
    }
    if ($found) {
        Write-Host "  -> Bundled CRT: $vcDll" -ForegroundColor Gray
    }
}

# 6. Create Portable Launcher Batch Scripts and Instructions
Write-Host "[6/7] Creating portable launcher batch scripts..." -ForegroundColor Yellow

# Run_TaskbarEngine.bat & Start_TaskbarEngine.bat
$runBatContent = @'
@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo               TaskbarEngine (Portable)
echo ========================================================
echo.

tasklist /FI "IMAGENAME eq TaskbarEngine.exe" 2>NUL | find /I /N "TaskbarEngine.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo [INFO] TaskbarEngine is already running!
    echo Look for the TaskbarEngine icon in your system tray.
    echo.
    timeout /t 3 >NUL
    exit /b 0
)

echo Starting TaskbarEngine in the system notification tray...
start "" "%~dp0TaskbarEngine.exe"

timeout /t 1 >NUL
echo.
echo TaskbarEngine is running!
echo  - Right-click or double-click the tray icon to configure.
echo  - Run Open_Settings.bat to open the Settings UI.
echo  - Run Stop_TaskbarEngine.bat to stop the engine.
echo.
timeout /t 3 >NUL
'@
Set-Content -Path "$StagingDir\Run_TaskbarEngine.bat" -Value $runBatContent -Encoding ASCII
Set-Content -Path "$StagingDir\Start_TaskbarEngine.bat" -Value $runBatContent -Encoding ASCII
Write-Host "  -> Run_TaskbarEngine.bat" -ForegroundColor Green
Write-Host "  -> Start_TaskbarEngine.bat" -ForegroundColor Green

# Open_Settings.bat
$settingsBatContent = @'
@echo off
setlocal
cd /d "%~dp0"

echo Launching TaskbarEngine Settings...
start "" "%~dp0TaskbarEngineSettings.exe"
'@
Set-Content -Path "$StagingDir\Open_Settings.bat" -Value $settingsBatContent -Encoding ASCII
Write-Host "  -> Open_Settings.bat" -ForegroundColor Green

# Stop_TaskbarEngine.bat
$stopBatContent = @'
@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo             Stopping TaskbarEngine...
echo ========================================================
echo.

rem Gracefully close TaskbarEngine so it sends TE_IPC_MSG_SHUTDOWN and cleanly unloads hook from explorer.exe
taskkill /IM TaskbarEngine.exe 2>NUL
timeout /t 1 >NUL

rem Force-terminate any lingering processes
taskkill /F /IM TaskbarEngine.exe 2>NUL
taskkill /F /IM TaskbarEngineHost.exe 2>NUL
taskkill /F /IM TaskbarEngineSettings.exe 2>NUL

echo TaskbarEngine has been stopped and unloaded from explorer.exe.
timeout /t 2 >NUL
'@
Set-Content -Path "$StagingDir\Stop_TaskbarEngine.bat" -Value $stopBatContent -Encoding ASCII
Write-Host "  -> Stop_TaskbarEngine.bat" -ForegroundColor Green

# PORTABLE_INSTRUCTIONS.txt
$instructionsContent = @'
================================================================================
                    TaskbarEngine (Portable Edition)
================================================================================

TaskbarEngine is a high-performance Windows 11 taskbar customization tool.
This portable edition requires NO INSTALLATION and NO ADMINISTRATOR PRIVILEGES.

REQUIREMENTS:
- Windows 11 (Version 22H2 - 24H2, Build 22621+)
- 64-bit (x64) architecture

HOW TO RUN:
1. Double-click "Run_TaskbarEngine.bat" (or "TaskbarEngine.exe").
   - The application starts in your system tray (notification area).
   - Look for the TaskbarEngine icon next to your system clock.

HOW TO CONFIGURE:
1. Double-click "Open_Settings.bat" (or "TaskbarEngineSettings.exe") to launch
   the modern WinUI 3 graphical settings interface.
2. Alternatively, right-click or double-click the TaskbarEngine system tray icon.
3. You can also customize settings by editing "Config\config.jsonc". Changes reload
   automatically in real time!

HOW TO EXIT:
1. Right-click the system tray icon and select "Exit", OR
2. Double-click "Stop_TaskbarEngine.bat".
   This safely detaches TaskbarEngine from explorer.exe and restores default taskbar.

PORTABILITY:
- You can place this entire folder on a USB flash drive or any folder on any PC.
- No registry entries or system files are required to run.
- To remove, simply stop the app and delete this folder.
================================================================================
'@
Set-Content -Path "$StagingDir\PORTABLE_INSTRUCTIONS.txt" -Value $instructionsContent -Encoding ASCII

# Copy README and LICENSE
if (Test-Path "$RootDir\README.md") { Copy-Item "$RootDir\README.md" -Destination $StagingDir -Force }
if (Test-Path "$RootDir\LICENSE") { Copy-Item "$RootDir\LICENSE" -Destination $StagingDir -Force }

# 7. Create Zip Archive in Root
Write-Host "[7/7] Compressing into portable release zip archive..." -ForegroundColor Yellow

if (Test-Path $DestinationZip) {
    Remove-Item -Force $DestinationZip -ErrorAction Stop
}

# Zip the contents of the staging directory
Compress-Archive -Path "$StagingDir\*" -DestinationPath $DestinationZip -CompressionLevel Optimal -Force

$zipFileInfo = Get-Item $DestinationZip
$sizeMB = [math]::Round($zipFileInfo.Length / 1MB, 2)
Write-Host "Created archive: $DestinationZip ($sizeMB MB)" -ForegroundColor Green

# Generate SHA256 Checksum
$hash = (Get-FileHash -Path $DestinationZip -Algorithm SHA256).Hash
Set-Content -Path "$DestinationZip.sha256" -Value "$hash  $(Split-Path $DestinationZip -Leaf)"
Write-Host "SHA256: $hash" -ForegroundColor Green

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host " Portable Package Ready: $(Split-Path $DestinationZip -Leaf)" -ForegroundColor Cyan
Write-Host " Location: $DestinationZip" -ForegroundColor Cyan
Write-Host " Size: $sizeMB MB" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
