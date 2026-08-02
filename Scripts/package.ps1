param (
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0"
)

$OutDir = "TaskbarEngine-v$Version"
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Path $OutDir | Out-Null
New-Item -ItemType Directory -Path "$OutDir\Modules" | Out-Null

$FilesToPackage = @(
    @{ Name = "TaskbarEngine.exe"; Dest = "" },
    @{ Name = "TaskbarEngineSettings.exe"; Dest = "" },
    @{ Name = "EngineDLL.dll"; Dest = "" },
    @{ Name = "taskbar_resize.dll"; Dest = "Modules\" },
    @{ Name = "icon_hover.dll"; Dest = "Modules\" }
)

foreach ($item in $FilesToPackage) {
    # Search for the file in the build directory
    $file = Get-ChildItem -Path "build" -Filter $item.Name -Recurse | Where-Object { $_.FullName -like "*$Configuration*" -or $_.FullName -like "*\bin\*" } | Select-Object -First 1
    if ($file) {
        Copy-Item $file.FullName -Destination "$OutDir\$($item.Dest)"
    } else {
        Write-Warning "File not found: $($item.Name)"
    }
}

Copy-Item "Config\default_config.jsonc" -Destination "$OutDir\config.jsonc"
Copy-Item "Scripts\uninstall.ps1" -Destination $OutDir
Copy-Item "LICENSE" -Destination $OutDir
Copy-Item "README.md" -Destination $OutDir
Copy-Item "Docs" -Destination "$OutDir\Docs" -Recurse

Compress-Archive -Path "$OutDir\*" -DestinationPath "${OutDir}.zip" -Force
Write-Host "Created ${OutDir}.zip successfully." -ForegroundColor Green
