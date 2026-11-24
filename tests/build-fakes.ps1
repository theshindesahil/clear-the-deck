# Compiles the fake apps the tests use (Windows PowerShell 5.1). Nothing here touches your real apps.
$T = Split-Path -Parent $MyInvocation.MyCommand.Path
New-Item -ItemType Directory -Force "$T\bin\listed", "$T\bin\out", "$T\Games", "$T\out" | Out-Null
Add-Type -TypeDefinition (Get-Content "$T\FakeApp.cs" -Raw) -ReferencedAssemblies System.Windows.Forms, System.Drawing -OutputAssembly "$T\bin\GmFakeWin.exe" -OutputType WindowsApplication
foreach ($n in 'GmFakeTray', 'GmFakeDeaf', 'GmFakeSlow', 'GmFakeArgs', 'GmFakeQuit', 'GmFakeHide') { Copy-Item "$T\bin\GmFakeWin.exe" "$T\bin\$n.exe" -Force }
Copy-Item "$T\bin\GmFakeWin.exe" "$T\bin\listed\GmFakeWin.exe" -Force
Copy-Item "$T\bin\GmFakeWin.exe" "$T\Games\GmFakeGame.exe" -Force
'fake apps built in ' + "$T\bin"

# A GUI-subsystem program that never creates a window (like a tray helper with no window to close).
Add-Type -TypeDefinition 'public static class P { public static void Main() { System.Threading.Thread.Sleep(System.Threading.Timeout.Infinite); } }' -OutputAssembly "$T\bin\GmFakeNoWin.exe" -OutputType WindowsApplication
'fake window-less app built'
