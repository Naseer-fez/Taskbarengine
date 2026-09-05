# PowerShell script to deploy binaries and create Desktop shortcuts
$ErrorActionPreference = "Stop"

$repoRoot = "D:\CODE\Utlities\Taskbar"
$buildApp = "$repoRoot\build_msvc\App"
$buildCore = "$repoRoot\build_msvc\Core"
$buildConfig = "$repoRoot\build_msvc\Config"

# 1. Copy EngineDLL.dll into build_msvc\App
if (Test-Path "$buildCore\EngineDLL.dll") {
    try {
        Copy-Item "$buildCore\EngineDLL.dll" "$buildApp\EngineDLL.dll" -Force
    } catch {
        $tempOld = "$buildApp\EngineDLL.dll.old." + (Get-Random)
        Move-Item "$buildApp\EngineDLL.dll" $tempOld -Force
        Copy-Item "$buildCore\EngineDLL.dll" "$buildApp\EngineDLL.dll" -Force
    }
    Write-Host "Copied EngineDLL.dll to $buildApp" -ForegroundColor Green
}

# 2. Copy Config into build_msvc\Config
if (-not (Test-Path $buildConfig)) {
    New-Item -ItemType Directory -Path $buildConfig -Force | Out-Null
}
Copy-Item "$repoRoot\Config\default_config.jsonc" "$buildConfig\default_config.jsonc" -Force
Write-Host "Copied default_config.jsonc to $buildConfig" -ForegroundColor Green

# 2a. Sync to LocalAppData\TaskbarEngine if present
$appDataDir = "$env:LOCALAPPDATA\TaskbarEngine"
if (Test-Path $appDataDir) {
    Copy-Item "$repoRoot\Config\default_config.jsonc" "$appDataDir\config.jsonc" -Force
    Write-Host "Synced config.jsonc to $appDataDir" -ForegroundColor Green
}

# 2b. Verify and deploy TaskbarResize plugin
$buildPlugin = "$repoRoot\build_msvc\Modules\taskbar_resize"
if (-not (Test-Path $buildPlugin)) {
    New-Item -ItemType Directory -Path $buildPlugin -Force | Out-Null
}
if (Test-Path "$buildPlugin\taskbar_resize.dll") {
    Write-Host "Verified taskbar_resize.dll in $buildPlugin" -ForegroundColor Green
}

# Copy app.ico to buildApp
if (Test-Path "$repoRoot\App\res\app.ico") {
    Copy-Item "$repoRoot\App\res\app.ico" "$buildApp\app.ico" -Force
    Write-Host "Copied app.ico to $buildApp" -ForegroundColor Green
}

# 3. Create Desktop Shortcuts
$desktop = [Environment]::GetFolderPath("Desktop")
$wsh = New-Object -ComObject WScript.Shell

# TaskbarEngine shortcut
$shortcutPath = Join-Path $desktop "TaskbarEngine.lnk"
$shortcut = $wsh.CreateShortcut($shortcutPath)
$shortcut.TargetPath = "$buildApp\TaskbarEngine.exe"
$shortcut.WorkingDirectory = $buildApp
$shortcut.Description = "TaskbarEngine - Windows 11 Taskbar Extension Host"
$icoPath = "$repoRoot\App\res\app.ico"
if (Test-Path $icoPath) {
    $shortcut.IconLocation = "$icoPath,0"
}
$shortcut.Save()
Write-Host "Created Desktop Shortcut: $shortcutPath" -ForegroundColor Cyan

# TaskbarEngine Settings shortcut
$settingsShortcutPath = Join-Path $desktop "TaskbarEngine Settings.lnk"
$settingsShortcut = $wsh.CreateShortcut($settingsShortcutPath)
$settingsShortcut.TargetPath = "$buildApp\TaskbarEngineSettings.exe"
$settingsShortcut.WorkingDirectory = $buildApp
$settingsShortcut.Description = "TaskbarEngine Settings - Configuration UI"
if (Test-Path $icoPath) {
    $settingsShortcut.IconLocation = "$icoPath,0"
}
$settingsShortcut.Save()
Write-Host "Created Desktop Shortcut: $settingsShortcutPath" -ForegroundColor Cyan

Write-Host "Deployment and shortcut creation completed successfully!" -ForegroundColor Green
