# Captures the mod's confirmation dialog (sample app names, nothing is closed) to PNG files.
# Usage: powershell -File capture-dialog.ps1 -Exe <test.exe> -OutDir <folder>
param([string]$Exe, [string]$OutDir)

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Cap -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr l);
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(System.IntPtr h, System.Text.StringBuilder sb, int n);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(System.IntPtr h, System.Text.StringBuilder sb, int n);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(System.IntPtr h, int a, out RECT r, int s);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@
[void][Cap.U]::SetProcessDPIAware()
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($mode in 'on', 'on-open', 'off') {
    $p = Start-Process $Exe -ArgumentList "dialog-$mode" -PassThru -RedirectStandardOutput "$env:TEMP\gm-dialog-$mode.txt"
    $script:found = [IntPtr]::Zero
    for ($i = 0; $i -lt 60 -and $script:found -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 200
        [void][Cap.U]::EnumWindows({
                param($h, $l)
                $x = 0
                [void][Cap.U]::GetWindowThreadProcessId($h, [ref]$x)
                if ($x -eq $p.Id -and [Cap.U]::IsWindowVisible($h)) {
                    $cls = New-Object System.Text.StringBuilder 64
                    $ttl = New-Object System.Text.StringBuilder 64
                    [void][Cap.U]::GetClassName($h, $cls, 64)
                    [void][Cap.U]::GetWindowText($h, $ttl, 64)
                    if ($cls.ToString() -eq 'GameModeToggleDialog') { $script:found = $h }
                }
                $true
            }, [IntPtr]::Zero)
    }
    if ($script:found -eq [IntPtr]::Zero) { "no dialog window for $mode"; continue }
    [void][Cap.U]::SetForegroundWindow($script:found)
    Start-Sleep -Milliseconds 700
    $r = New-Object Cap.U+RECT
    if ([Cap.U]::DwmGetWindowAttribute($script:found, 9, [ref]$r, [Runtime.InteropServices.Marshal]::SizeOf($r)) -ne 0 -or ($r.R - $r.L) -le 0) {
        [void][Cap.U]::GetWindowRect($script:found, [ref]$r)
    }
    $w = $r.R - $r.L
    $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $path = Join-Path $OutDir "confirm-$mode.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    "saved $path ($w x $h)"
    [void][Cap.U]::PostMessage($script:found, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE = Cancel
    [void]$p.WaitForExit(5000)
    if (-not $p.HasExited) { $p.Kill() }
}
