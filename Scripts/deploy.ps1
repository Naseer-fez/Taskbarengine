param(
    [string]$BuildDir = "$PSScriptRoot\..\build\msvc-release",
    [string]$DeployDir = "$PSScriptRoot\..\ReleaseStaging"
)

$ErrorActionPreference = "Stop"
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$DeployDir = [System.IO.Path]::GetFullPath($DeployDir)
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$appDir = Join-Path $BuildDir "App"
$moduleDir = Join-Path $BuildDir "Modules"

$requiredExecutables = @("TaskbarEngine.exe", "TaskbarEngineHost.exe")
foreach ($name in $requiredExecutables) {
    $file = Join-Path $appDir $name
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required engine executable is missing: $file"
    }
}

$engineDll = Join-Path $BuildDir "Core\EngineDLL.dll"
if (-not (Test-Path -LiteralPath $engineDll -PathType Leaf)) {
    $engineDll = Join-Path $appDir "EngineDLL.dll"
}
if (-not (Test-Path -LiteralPath $engineDll -PathType Leaf)) {
    throw "EngineDLL.dll was not found in the build output."
}

New-Item -ItemType Directory -Path $DeployDir -Force | Out-Null
$deployPrefix = $DeployDir.TrimEnd('\') + '\'
foreach ($processName in @("TaskbarEngineSettings.exe", "TaskbarEngine.exe", "TaskbarEngineHost.exe")) {
    Get-CimInstance Win32_Process -Filter "Name='$processName'" | ForEach-Object {
        $runningPath = $_.ExecutablePath
        if ($runningPath -and $runningPath.StartsWith($deployPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $process = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
            if ($process -and $process.CloseMainWindow()) {
                $process.WaitForExit(5000) | Out-Null
            }
            $process = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
            if ($process) {
                if ($processName -ne "TaskbarEngineSettings.exe") {
                    throw "Stop the deployed engine through its tray menu or Settings before rebuilding: $runningPath"
                }
                Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop
                $process.WaitForExit(3000) | Out-Null
            }
        }
    }
}

Copy-Item -Path (Join-Path $appDir "*") -Destination $DeployDir -Recurse -Force

$deployModules = Join-Path $DeployDir "Modules"
New-Item -ItemType Directory -Path $deployModules -Force | Out-Null
if (Test-Path -LiteralPath $moduleDir -PathType Container) {
    Get-ChildItem -LiteralPath $moduleDir -Filter "*.dll" -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($moduleDir.Length).TrimStart('\', '/')
        $destination = Join-Path $deployModules $relative
        $destinationDir = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
        $needsCopy = $true
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            $sourceHash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            $needsCopy = $sourceHash -ne $destinationHash
        }
        if ($needsCopy) {
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        }
    }
}

$widgetsDir = Join-Path $BuildDir "Widgets"
if (Test-Path -LiteralPath $widgetsDir -PathType Container) {
    Copy-Item -LiteralPath $widgetsDir -Destination $DeployDir -Recurse -Force
}
$deployConfig = Join-Path $DeployDir "Config"
New-Item -ItemType Directory -Path $deployConfig -Force | Out-Null
Copy-Item -LiteralPath $engineDll -Destination (Join-Path $DeployDir "EngineDLL.dll") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "Config\default_config.jsonc") -Destination (Join-Path $deployConfig "default_config.jsonc") -Force
if (-not (Test-Path -LiteralPath (Join-Path $deployConfig "config.jsonc"))) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "Config\default_config.jsonc") -Destination (Join-Path $deployConfig "config.jsonc")
}

Write-Host "Deployed Settings, engine, runtime dependencies, plugins, and widget DLLs to $DeployDir"
