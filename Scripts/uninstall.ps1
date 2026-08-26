Write-Host "TaskbarEngine Uninstaller"
Write-Host "========================="

# Stop the process if running
$process = Get-Process -Name "TaskbarEngine" -ErrorAction SilentlyContinue
if ($process) {
    Write-Host "Stopping TaskbarEngine..."
    Stop-Process -Name "TaskbarEngine" -Force
    Start-Sleep -Seconds 2
}

$settingsProcess = Get-Process -Name "TaskbarEngineSettings" -ErrorAction SilentlyContinue
if ($settingsProcess) {
    Write-Host "Stopping TaskbarEngineSettings..."
    Stop-Process -Name "TaskbarEngineSettings" -Force
}

# Remove the scheduled task
Write-Host "Removing scheduled task..."
$task = Get-ScheduledTask -TaskName "TaskbarEngine_Logon" -ErrorAction SilentlyContinue
if ($task) {
    Unregister-ScheduledTask -TaskName "TaskbarEngine_Logon" -Confirm:$false
}

# Remove config directory
$configPath = "$env:LOCALAPPDATA\TaskbarEngine"
if (Test-Path $configPath) {
    Write-Host "Removing configuration files..."
    Remove-Item -Recurse -Force $configPath
}

Write-Host "Uninstallation complete. You can now delete this folder."
Read-Host "Press Enter to exit"
