$sig = @'
[DllImport("user32.dll", SetLastError=true)]
public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
'@
$w = Add-Type -MemberDefinition $sig -Name WinAPI -Namespace User32Check -PassThru
$hwnd = $w::FindWindow('Shell_TrayWnd', [NullString]::Value)
Write-Host "Shell_TrayWnd HWND: $hwnd"
$explorer = Get-Process explorer -ErrorAction SilentlyContinue
if ($explorer) {
    Write-Host "Explorer PID: $($explorer.Id)"
    Write-Host "Explorer running: True"
} else {
    Write-Host "Explorer running: False"
}
# Check for TaskbarEngine.exe
$te = Get-Process TaskbarEngine -ErrorAction SilentlyContinue
if ($te) {
    Write-Host "TaskbarEngine running: True (PID: $($te.Id))"
} else {
    Write-Host "TaskbarEngine running: False"
}
