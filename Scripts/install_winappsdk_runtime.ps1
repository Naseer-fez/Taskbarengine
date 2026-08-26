$downloadDir = "D:\Extras\ES\downloads"
if (!(Test-Path $downloadDir)) {
    New-Item -ItemType Directory -Path $downloadDir | Out-Null
}

$installer = "$downloadDir\windowsappruntimeinstall-x64.exe"
if (!(Test-Path $installer)) {
    Write-Host "Downloading Windows App SDK 1.5 Runtime installer..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri "https://aka.ms/windowsappsdk/1.5/1.5.240311000/windowsappruntimeinstall-x64.exe" -OutFile $installer
}

Write-Host "Running installer..."
Start-Process -FilePath $installer -ArgumentList "--quiet" -Wait

Write-Host "Checking installed Windows App SDK packages..."
Get-AppxPackage -Name "*Microsoft.WindowsAppRuntime*" | Select-Object Name, Version, Architecture
