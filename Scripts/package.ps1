param (
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0",
    [string]$BuildDir = "build_msvc",
    [string]$OutDir = "",
    [string]$DestinationZip = ""
)

if ([string]::IsNullOrWhiteSpace($DestinationZip)) {
    $DestinationZip = "$PSScriptRoot\..\TaskbarEngine-v$Version.zip"
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $StagingDir = "$BuildDir\package_staging"
} else {
    $StagingDir = $OutDir
}

Write-Host "Staging release package to $StagingDir..." -ForegroundColor Cyan

if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir -ErrorAction Stop }
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\Modules" -Force | Out-Null

$FilesToPackage = @(
    @{ Name = "TaskbarEngine.exe"; Dest = "" },
    @{ Name = "TaskbarEngineSettings.exe"; Dest = "" },
    @{ Name = "EngineDLL.dll"; Dest = "" },
    @{ Name = "taskbar_resize.dll"; Dest = "Modules\" },
    @{ Name = "icon_hover.dll"; Dest = "Modules\" }
)

foreach ($item in $FilesToPackage) {
    $file = Get-ChildItem -Path $BuildDir -Filter $item.Name -Recurse | Where-Object { $_.FullName -like "*\bin\*" -or $_.FullName -like "*$Configuration*" } | Select-Object -First 1
    if ($file) {
        Copy-Item $file.FullName -Destination "$StagingDir\$($item.Dest)" -Force
        Write-Host "  -> Included $($item.Name)"
    } else {
        Write-Warning "File not found: $($item.Name)"
    }
}

# Copy all self-contained Windows App SDK runtime DLLs, PRIs, and dependencies from bin
$binDir = "$BuildDir\bin"
if (Test-Path $binDir) {
    Get-ChildItem -Path $binDir -File | Where-Object { 
        $_.Extension -in @(".dll", ".pri", ".xbf") -and 
        $_.Name -notin @("EngineDLL.dll", "taskbar_resize.dll", "icon_hover.dll")
    } | ForEach-Object {
        Copy-Item $_.FullName -Destination $StagingDir -Force
    }

    if (Test-Path "$binDir\Microsoft.UI.Xaml") {
        Copy-Item "$binDir\Microsoft.UI.Xaml" -Destination "$StagingDir\Microsoft.UI.Xaml" -Recurse -Force
    }
}

Copy-Item "Config\default_config.jsonc" -Destination "$StagingDir\config.jsonc" -Force
Copy-Item "Scripts\uninstall.ps1" -Destination $StagingDir -Force
Copy-Item "LICENSE" -Destination $StagingDir -Force
Copy-Item "README.md" -Destination $StagingDir -Force
Copy-Item "Docs" -Destination "$StagingDir\Docs" -Recurse -Force

try {
    if (Test-Path $DestinationZip) { Remove-Item -Force $DestinationZip -ErrorAction Stop }
    Compress-Archive -Path "$StagingDir\*" -DestinationPath $DestinationZip -Force
    Write-Host "Created $DestinationZip successfully." -ForegroundColor Green

    # Generate SHA256 checksum
    $hash = (Get-FileHash -Path $DestinationZip -Algorithm SHA256).Hash
    Set-Content -Path "$DestinationZip.sha256" -Value "$hash  $(Split-Path $DestinationZip -Leaf)"
    Write-Host "Generated SHA256: $hash" -ForegroundColor Green
} catch {
    $fallbackZip = "$PSScriptRoot\..\TaskbarEngine-v$Version.zip"
    if (Test-Path $fallbackZip) { Remove-Item -Force $fallbackZip -ErrorAction SilentlyContinue }
    Compress-Archive -Path "$StagingDir\*" -DestinationPath $fallbackZip -Force
    Write-Host "Created $fallbackZip successfully." -ForegroundColor Green

    # Generate SHA256 checksum for fallback archive
    $hash = (Get-FileHash -Path $fallbackZip -Algorithm SHA256).Hash
    Set-Content -Path "$fallbackZip.sha256" -Value "$hash  $(Split-Path $fallbackZip -Leaf)"
    Write-Host "Generated SHA256: $hash" -ForegroundColor Green
}
