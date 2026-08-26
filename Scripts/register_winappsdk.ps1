$msixDir = "D:\Extras\ES\packages\Microsoft.WindowsAppSDK.1.5.240311000\tools\MSIX\win10-x64"
Write-Host "Registering Windows App SDK 1.5 Framework..."
Add-AppxPackage -Path "$msixDir\Microsoft.WindowsAppRuntime.1.5.msix"
Add-AppxPackage -Path "$msixDir\Microsoft.WindowsAppRuntime.Main.1.5.msix"
Add-AppxPackage -Path "$msixDir\Microsoft.WindowsAppRuntime.Singleton.1.5.msix"
Add-AppxPackage -Path "$msixDir\Microsoft.WindowsAppRuntime.DDLM.1.5.msix"
Write-Host "Verifying registration:"
Get-AppxPackage -Name "*Microsoft.WindowsAppRuntime.1.5*" | Select-Object Name, Version, PackageFullName
