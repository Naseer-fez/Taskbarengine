Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public sealed class TaskbarPatternForm : Form
{
    private readonly Timer timer = new Timer();
    private int phase;
    private readonly Color[] colors = { Color.Magenta, Color.Lime, Color.Cyan, Color.Yellow };

    public TaskbarPatternForm(Rectangle bounds)
    {
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false;
        TopMost = false;
        StartPosition = FormStartPosition.Manual;
        Rectangle taskbar = GetPrimaryTaskbarBounds();
        Bounds = new Rectangle(bounds.Left + 120, taskbar.Top - 300, bounds.Width - 240, taskbar.Height + 300);
        BackColor = Color.Magenta;
        timer.Interval = 300;
        timer.Tick += (s, e) => { phase = (phase + 1) % colors.Length; Invalidate(); };
        timer.Start();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        const int tile = 48;
        for (int y = 0; y < ClientSize.Height; y += tile)
        for (int x = 0; x < ClientSize.Width; x += tile)
        {
            int index = ((x / tile) + (y / tile) + phase) % colors.Length;
            using (var brush = new SolidBrush(colors[index]))
                e.Graphics.FillRectangle(brush, x, y, tile, tile);
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindow(string className, string windowName);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern IntPtr WindowFromPoint(POINT point);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr hwnd, StringBuilder className, int maxCount);

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int X, Y; }

    public static void EnablePhysicalPixelCoordinates()
    {
        if (!SetProcessDPIAware()) throw new InvalidOperationException("SetProcessDPIAware failed.");
    }

    public static Point GetCursorPosition()
    {
        POINT point;
        if (!GetCursorPos(out point)) throw new InvalidOperationException("GetCursorPos failed.");
        return new Point(point.X, point.Y);
    }

    public static IntPtr MinimizeForegroundWindow()
    {
        IntPtr hwnd = GetForegroundWindow();
        if (hwnd != IntPtr.Zero) ShowWindow(hwnd, 6);
        return hwnd;
    }

    public static void RestoreForegroundWindow(IntPtr hwnd)
    {
        if (hwnd != IntPtr.Zero) { ShowWindow(hwnd, 9); SetForegroundWindow(hwnd); }
    }

    public static void MoveCursor(int x, int y)
    {
        if (!SetCursorPos(x, y)) throw new InvalidOperationException("SetCursorPos failed.");
    }

    public static Rectangle GetPrimaryTaskbarBounds()
    {
        IntPtr hwnd = FindWindow("Shell_TrayWnd", null);
        RECT r;
        if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out r))
            throw new InvalidOperationException("Could not locate the primary Shell_TrayWnd rectangle.");
        return Rectangle.FromLTRB(r.Left, r.Top, r.Right, r.Bottom);
    }

    public static string GetClassAtPoint(int x, int y)
    {
        IntPtr hwnd = WindowFromPoint(new POINT { X = x, Y = y });
        StringBuilder name = new StringBuilder(256);
        GetClassName(hwnd, name, name.Capacity);
        return name.ToString() + " hwnd=" + hwnd.ToString("X");
    }

    public static void PlacePatternBehindTaskbar(IntPtr pattern)
    {
        IntPtr taskbar = FindWindow("Shell_TrayWnd", null);
        if (taskbar == IntPtr.Zero || !SetWindowPos(pattern, IntPtr.Zero, 0, 0, 0, 0, 0x0013) ||
            !SetWindowPos(taskbar, new IntPtr(-1), 0, 0, 0, 0, 0x0013))
            throw new InvalidOperationException("Could not keep Shell_TrayWnd above the contrast pattern.");
    }

    public static Bitmap CaptureScreen(Rectangle bounds)
    {
        var bitmap = new Bitmap(bounds.Width, bounds.Height);
        using (var graphics = Graphics.FromImage(bitmap))
            graphics.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
        return bitmap;
    }

    public static int CountChanged(Bitmap a, Bitmap b, Rectangle screenRegion, int step)
    {
        int changed = 0;
        for (int y = screenRegion.Top; y < screenRegion.Bottom; y += step)
        for (int x = screenRegion.Left; x < screenRegion.Right; x += step)
        {
            Color ca = a.GetPixel(x, y), cb = b.GetPixel(x, y);
            if (Math.Abs(ca.R - cb.R) + Math.Abs(ca.G - cb.G) + Math.Abs(ca.B - cb.B) > 45)
                changed++;
        }
        return changed;
    }
}
'@ -ReferencedAssemblies System.Windows.Forms,System.Drawing

$researchDir = $PSScriptRoot
[TaskbarPatternForm]::EnablePhysicalPixelCoordinates()
$taskbarBounds = [TaskbarPatternForm]::GetPrimaryTaskbarBounds()
$screenBounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$originalCursor = [TaskbarPatternForm]::GetCursorPosition()
$previousForeground = [TaskbarPatternForm]::MinimizeForegroundWindow()
$form = [TaskbarPatternForm]::new($screenBounds)
$form.Show()
$form.Activate()
[TaskbarPatternForm]::PlacePatternBehindTaskbar($form.Handle)
[TaskbarPatternForm]::MoveCursor(10, $screenBounds.Bottom - 1)
[System.Windows.Forms.Application]::DoEvents()
Start-Sleep -Milliseconds 1400

function Get-PatternSamples([int]$DurationMs) {
    $frames = [System.Collections.Generic.List[System.Drawing.Bitmap]]::new()
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $DurationMs) {
        [System.Windows.Forms.Application]::DoEvents()
        $frames.Add([TaskbarPatternForm]::CaptureScreen($screenBounds))
        Start-Sleep -Milliseconds 180
    }
    $timer.Stop()
    $taskbarChanged = 0
    $aboveChanged = 0
    $sampleRegion = [System.Drawing.Rectangle]::new($taskbarBounds.Left + 200, $taskbarBounds.Top, $taskbarBounds.Width - 400, $taskbarBounds.Height)
    $above = [System.Drawing.Rectangle]::new($sampleRegion.Left, [Math]::Max(0, $taskbarBounds.Top - $taskbarBounds.Height), $sampleRegion.Width, $taskbarBounds.Height)
    for ($i = 1; $i -lt $frames.Count; $i++) {
        $taskbarChanged = [Math]::Max($taskbarChanged, [TaskbarPatternForm]::CountChanged($frames[$i - 1], $frames[$i], $sampleRegion, 4))
        $aboveChanged = [Math]::Max($aboveChanged, [TaskbarPatternForm]::CountChanged($frames[$i - 1], $frames[$i], $above, 4))
    }
    foreach ($frame in $frames) { $frame.Dispose() }
    return [pscustomobject]@{ Frames = $frames.Count; TaskbarChangedSamples = $taskbarChanged; AboveTaskbarChangedSamples = $aboveChanged }
}

$before = Get-PatternSamples 3600
$beforeHitTest = [TaskbarPatternForm]::GetClassAtPoint($taskbarBounds.Left + [int]($taskbarBounds.Width / 2), $taskbarBounds.Top + [int]($taskbarBounds.Height / 2))
$afterShot = [TaskbarPatternForm]::CaptureScreen($screenBounds)
$afterShot.Save((Join-Path $researchDir 'taskbar-frosted-glass-xaml-pattern-after.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$afterShot.Dispose()

$hostExe = Join-Path (Split-Path $PSScriptRoot -Parent) 'build_msvc\Deploy\TaskbarEngineHost.exe'
Start-Process -FilePath $hostExe -ArgumentList '--exit' -Wait
Start-Sleep -Seconds 2
$restored = Get-PatternSamples 3600
$restoredHitTest = [TaskbarPatternForm]::GetClassAtPoint($taskbarBounds.Left + [int]($taskbarBounds.Width / 2), $taskbarBounds.Top + [int]($taskbarBounds.Height / 2))
$restoredShot = [TaskbarPatternForm]::CaptureScreen($screenBounds)
$restoredShot.Save((Join-Path $researchDir 'taskbar-frosted-glass-xaml-pattern-restored.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$restoredShot.Dispose()

$form.Close()
$form.Dispose()
[TaskbarPatternForm]::MoveCursor($originalCursor.X, $originalCursor.Y)
[TaskbarPatternForm]::RestoreForegroundWindow($previousForeground)

@(
    "TaskbarBounds=$($taskbarBounds.X),$($taskbarBounds.Y),$($taskbarBounds.Width),$($taskbarBounds.Height)"
    "BeforeShutdownFrames=$($before.Frames) TaskbarChangedSamples=$($before.TaskbarChangedSamples) AboveTaskbarChangedSamples=$($before.AboveTaskbarChangedSamples)"
    "BeforeShutdownWindowAtTaskbarCenter=$beforeHitTest"
    "AfterRestoreFrames=$($restored.Frames) TaskbarChangedSamples=$($restored.TaskbarChangedSamples) AboveTaskbarChangedSamples=$($restored.AboveTaskbarChangedSamples)"
    "AfterRestoreWindowAtTaskbarCenter=$restoredHitTest"
) | Set-Content -LiteralPath (Join-Path $researchDir 'taskbar-frosted-glass-xaml-pattern-pixels.txt') -Encoding utf8

$before
$restored
