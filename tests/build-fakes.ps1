# Compiles the fake apps the tests use (Windows PowerShell 5.1). Nothing here touches your real apps.
$T = Split-Path -Parent $MyInvocation.MyCommand.Path
New-Item -ItemType Directory -Force "$T\bin\listed", "$T\bin\out", "$T\Games", "$T\out" | Out-Null
Add-Type -TypeDefinition (Get-Content "$T\FakeApp.cs" -Raw) -ReferencedAssemblies System.Windows.Forms, System.Drawing -OutputAssembly "$T\bin\GmFakeWin.exe" -OutputType WindowsApplication
foreach ($n in 'GmFakeTray', 'GmFakeDeaf', 'GmFakeSlow', 'GmFakeArgs', 'GmFakeQuit') { Copy-Item "$T\bin\GmFakeWin.exe" "$T\bin\$n.exe" -Force }
Copy-Item "$T\bin\GmFakeWin.exe" "$T\bin\listed\GmFakeWin.exe" -Force
Copy-Item "$T\bin\GmFakeWin.exe" "$T\Games\GmFakeGame.exe" -Force
'fake apps built in ' + "$T\bin"
