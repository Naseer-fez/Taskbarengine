param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [switch]$RunTests,
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build_msvc"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

function Get-LatestDirectory([string[]]$Candidates) {
    $directories = foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            Get-Item -LiteralPath $candidate
        }
    }
    $directories = @($directories | Sort-Object {
        if ($_.Name -match '(\d+\.\d+(?:\.\d+){0,2})') {
            try { [version]$Matches[1] } catch { [version]'0.0' }
        } else { [version]'0.0' }
    } -Descending)
    if ($directories.Count -gt 0) { return $directories[0].FullName }
    return $null
}

function Get-WindowsAppSdkRoot {
    $candidates = @()
    if ($env:WINAPPSDK_ROOT) { $candidates += $env:WINAPPSDK_ROOT }
    if ($env:USERPROFILE) {
        $nugetRoot = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.windowsappsdk"
        if (Test-Path -LiteralPath $nugetRoot) { $candidates += (Get-ChildItem -LiteralPath $nugetRoot -Directory | ForEach-Object FullName) }
    }
    if ($env:EXTRAS_ROOT) {
        $extrasPackages = Join-Path $env:EXTRAS_ROOT "packages"
        if (Test-Path -LiteralPath $extrasPackages) {
            $candidates += (Get-ChildItem -LiteralPath $extrasPackages -Directory -Filter "Microsoft.WindowsAppSDK.*" | ForEach-Object FullName)
        }
    }
    if (Test-Path -LiteralPath "D:\Extras\ES\packages") {
        $candidates += (Get-ChildItem -LiteralPath "D:\Extras\ES\packages" -Directory -Filter "Microsoft.WindowsAppSDK.*" | ForEach-Object FullName)
    }
    $root = Get-LatestDirectory $candidates
    if (-not $root -or -not (Test-Path -LiteralPath (Join-Path $root "include"))) {
        throw "Windows App SDK was not found. Install it or set WINAPPSDK_ROOT to its package directory."
    }
    return $root
}

function Get-CppWinRtInclude([string]$SdkRoot) {
    if ($env:WINAPPSDK_CPPWINRT -and (Test-Path -LiteralPath (Join-Path $env:WINAPPSDK_CPPWINRT "winrt\base.h"))) {
        return $env:WINAPPSDK_CPPWINRT
    }

    $candidates = @()
    if ($env:EXTRAS_ROOT) { $candidates += (Join-Path $env:EXTRAS_ROOT "packages\winappsdk_cppwinrt") }
    $candidates += (Join-Path (Split-Path -Parent $SdkRoot) "winappsdk_cppwinrt")
    if ($env:USERPROFILE) {
        $nugetRoot = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.windows.cppwinrt"
        if (Test-Path -LiteralPath $nugetRoot) {
            foreach ($package in (Get-ChildItem -LiteralPath $nugetRoot -Directory)) {
                $candidates += (Join-Path $package.FullName "include")
            }
        }
    }
    if (Test-Path -LiteralPath "D:\Extras\ES\packages\winappsdk_cppwinrt") {
        $candidates += "D:\Extras\ES\packages\winappsdk_cppwinrt"
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "winrt\base.h")) { return $candidate }
    }
    throw "C++/WinRT headers were not found. Set WINAPPSDK_CPPWINRT to the directory containing winrt\base.h."
}

function Import-MsvcEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vcvars = $null
    if ($env:TASKBARENGINE_MSVC_ROOT) {
        $candidate = Join-Path $env:TASKBARENGINE_MSVC_ROOT "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate }
    }
    if (-not $vcvars -and $env:ProgramFiles -and ${env:ProgramFiles(x86)}) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $vswhere) {
            $vsRoot = & $vswhere -latest -products '*' -property installationPath
            if ($vsRoot) {
                $candidate = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate }
            }
        }
    }
    if (-not $vcvars -and (Test-Path -LiteralPath "D:\Extras\ES\msvc\VC\Auxiliary\Build\vcvars64.bat")) {
        $vcvars = "D:\Extras\ES\msvc\VC\Auxiliary\Build\vcvars64.bat"
    }
    if (-not $vcvars) {
        throw "MSVC was not found. Install Visual Studio Build Tools or set TASKBARENGINE_MSVC_ROOT to the MSVC root."
    }

    $environment = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize the MSVC x64 build environment." }
    foreach ($line in $environment) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            [Environment]::SetEnvironmentVariable($name, $line.Substring($separator + 1), "Process")
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "MSVC environment initialization completed, but cl.exe is still unavailable."
    }
}

try {
    Import-MsvcEnvironment
    $cmake = Get-Command cmake.exe -ErrorAction Stop
    if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
        throw "Ninja was not found. Install Ninja and add it to PATH."
    }

    $sdkRoot = Get-WindowsAppSdkRoot
    $cppWinRt = Get-CppWinRtInclude $sdkRoot
    $frameworkDir = if ($env:WINAPPSDK_FRAMEWORK_DIR) {
        $env:WINAPPSDK_FRAMEWORK_DIR
    } else {
        Join-Path $sdkRoot "tools\MSIX\win10-x64\extracted"
    }
    if (-not (Test-Path -LiteralPath $frameworkDir -PathType Container)) {
        throw "Windows App SDK runtime files were not found at '$frameworkDir'. Set WINAPPSDK_FRAMEWORK_DIR to the extracted runtime directory."
    }

    $testSetting = if ($RunTests) { "ON" } else { "OFF" }
    Write-Host "Configuring $Configuration build at $BuildDir"
    & $cmake.Source -S $repoRoot -B $BuildDir -G Ninja `
        "-DCMAKE_BUILD_TYPE=$Configuration" `
        "-DCMAKE_C_COMPILER=cl" `
        "-DCMAKE_CXX_COMPILER=cl" `
        "-DWINAPPSDK_ROOT:PATH=$sdkRoot" `
        "-DWINAPPSDK_CPPWINRT:PATH=$cppWinRt" `
        "-DWINAPPSDK_FRAMEWORK_DIR:PATH=$frameworkDir" `
        "-DTE_BUILD_TESTS:BOOL=$testSetting"
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }

    Write-Host "Building all CMake targets (no target or source-file list is maintained in this script)."
    & $cmake.Source --build $BuildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

    if ($RunTests) {
        Write-Host "Running the requested test suite."
        & $cmake.Source --build $BuildDir --config $Configuration --target test
        if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }
    }

    if (-not $SkipDeploy) {
        $deployDir = Join-Path $BuildDir "Deploy"
        Write-Host "Deploying runtime files to $deployDir"
        & (Join-Path $PSScriptRoot "deploy.ps1") -BuildDir $BuildDir -DeployDir $deployDir
    }

    Write-Host "Build completed successfully." -ForegroundColor Green
    exit 0
} catch {
    Write-Host "[ERROR] $_" -ForegroundColor Red
    exit 1
}
