# Test harness for gamemode.ps1 against fake apps only. Run under Windows PowerShell 5.1:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\build-fakes.ps1   (once)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-tests.ps1
# Instrumentation applied to the copy under test (never to the real files):
#   mutex renamed to GameModeToggleMutex-test, Show-Toast writes to the log instead of showing a toast.
param()
$ErrorActionPreference = 'Continue'
$fakeSvc = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'fake-services.ps1'
Set-Content $fakeSvc '$allow = @()'
$env:GAMEMODE_SERVICES_FILE = $fakeSvc
Remove-Item Env:GAMEMODE_TEST_LOGON_AGE_SEC -ErrorAction SilentlyContinue
$L    = Split-Path -Parent $MyInvocation.MyCommand.Path
$src  = Split-Path -Parent $L   # the scripts under test live one level up
$work = Join-Path $L 'work'
$bin  = Join-Path $L 'bin'
$ev   = Join-Path $L 'out\events.log'
$plan = ((powercfg /getactivescheme) -replace '.*([0-9a-f]{8}-[0-9a-f-]{27}).*', '$1').Trim()

$baseApps = @(
    [ordered]@{ name = 'Fake Window';  processes = @('GmFakeWin') },
    [ordered]@{ name = 'Fake Tray';    processes = @('GmFakeTray') },
    [ordered]@{ name = 'Fake Deaf';    processes = @('GmFakeDeaf') },
    [ordered]@{ name = 'Fake Quit';    processes = @('GmFakeQuit'); quitArgs = '--quit' },
    [ordered]@{ name = 'Fake Args';    processes = @('GmFakeArgs') },
    [ordered]@{ name = 'Fake Browser'; processes = @('GmFakeSlow') }
)
if ($true) { $baseApps[5].forceKill = $false; $baseApps[5].closeTimeoutSec = 20; $baseApps[5].relaunchArgs = '--slow 12' }

function Reset-Lab {
    Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 500
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $work | Out-Null
    $s = Get-Content "$src\gamemode.ps1" -Raw
    $s = $s.Replace("'GameModeToggleMutex'", "'GameModeToggleMutex-test'")
    $s = $s.Replace('function Show-Toast($title, $msg) {', 'function Show-Toast($title, $msg) { Write-Log "  [test] toast: $title - $msg"; return')
    Set-Content "$work\gamemode.ps1" $s
    Copy-Item "$src\dialog.ps1" $work
    $cfg = [ordered]@{ gamePowerPlan = $plan; pauseApps = $baseApps; services = @(); gameFolders = @("$L\Games"); excludePathParts = @() }
    $cfg | ConvertTo-Json -Depth 5 | Set-Content "$work\config.json"
    if (Test-Path "$bin\GmFakeArgs.exe.bak") { Move-Item "$bin\GmFakeArgs.exe.bak" "$bin\GmFakeArgs.exe" -Force }
    Remove-Item $ev -ErrorAction SilentlyContinue
}

function Start-Fakes {
    Start-Process "$bin\GmFakeWin.exe" -WindowStyle Minimized
    Start-Process "$bin\GmFakeTray.exe" -ArgumentList '--tray'
    Start-Process "$bin\GmFakeDeaf.exe" -ArgumentList '--deaf'
    Start-Process "$bin\GmFakeQuit.exe" -ArgumentList '--deaf'
    Start-Process "$bin\GmFakeArgs.exe" -ArgumentList '--profile "My Profile" -x' -WindowStyle Minimized
    Start-Process "$bin\GmFakeSlow.exe" -ArgumentList '--slow 12 -osint -url https://example.test/oauth?state=SECRET123' -WindowStyle Minimized
    $g = Start-Process "$L\Games\GmFakeGame.exe" -WindowStyle Minimized -PassThru
    Start-Sleep 2
    $g.PriorityClass = 'BelowNormal'   # an off-game change that must survive ON/OFF
    Remove-Item $ev -ErrorAction SilentlyContinue
}

function Invoke-GM($argText, [switch]$NoWait) {
    $o = "$work\stdout.txt"; $e = "$work\stderr.txt"
    # No -Wait: in 5.1 it also waits for every child, and OFF relaunches the fakes as children.
    $p = Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$work\gamemode.ps1`" $argText" `
        -WorkingDirectory $work -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
    $null = $p.Handle
    if ($NoWait) { return $p }
    if (-not $p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; "    TIMEOUT after 120s" }
    $err = (Get-Content $e -ErrorAction SilentlyContinue | Where-Object { $_ -match '\S' } | Select-Object -First 2) -join ' / '
    "    exit=$($p.ExitCode)$(if ($err) { "  stderr: $err" })"
}

function Show-Fakes { "    running: " + ((@(Get-Process GmFake* -ErrorAction SilentlyContinue | ForEach-Object ProcessName | Sort-Object -Unique)) -join ', ') }
function Show-Events($label = 'events') { if (Test-Path $ev) { Get-Content $ev | ForEach-Object { "    $label | $_" } } }
function Show-Log { if (Test-Path "$work\gamemode.log") { Get-Content "$work\gamemode.log" | ForEach-Object { "    log | " + ($_ -replace '^\S+ ', '') } } }
function Show-State { "    state.json exists: $(Test-Path "$work\state.json")" + $(if (Test-Path "$work\state.json") { "  (SECRET in it: $([bool](Select-String -Path "$work\state.json" -Pattern SECRET -Quiet)))" }) }
function Show-GamePrio { "    game priority: $((Get-Process GmFakeGame -ErrorAction SilentlyContinue).PriorityClass)" }

"################ tests (power plan in test config = active plan $plan, so no plan change)"

"`n=== T1 round trip: ON then OFF, snapshot/restore equality, off-game change persists"
Reset-Lab; Start-Fakes; Show-GamePrio
$t = Get-Date; Invoke-GM '-Quiet'; "    ON took {0:N0}s" -f ((Get-Date) - $t).TotalSeconds
Show-Fakes; Show-State; Show-GamePrio; Show-Events 'on'
Remove-Item $ev -ErrorAction SilentlyContinue
Invoke-GM '-Quiet'; Start-Sleep 3
Show-Fakes; Show-State; Show-GamePrio; Show-Events 'off'; Show-Log

"`n=== T2 -Exclude 'Fake Window','Fake Args' through -File (single quotes, as typed in cmd / Run / a .lnk)"
Reset-Lab; Start-Fakes
Invoke-GM "-Exclude 'Fake Window','Fake Args'"
Show-Fakes; Show-State; "    png files written: " + ((Get-ChildItem $work -File | Where-Object { $_.Name -notmatch '\.(ps1|json|txt|log)$' } | ForEach-Object Name) -join ', ')
Show-Log
if ($true) {
    "  -- same, with the supported form -Exclude `"Fake Window,Fake Args`" -Quiet"
    Invoke-GM '-Exclude "Fake Window,Fake Args" -Quiet'; Show-Fakes
}

"`n=== T3 corrupt state.json (half-written), pressed twice"
Reset-Lab
Set-Content "$work\state.json" '{ "startedAt": "2026-11-24T10:00:00", "apps": [ { "name": "Fake Window", "wasRun'
Invoke-GM '-Quiet'; Show-State
Invoke-GM '-Quiet'; Show-State; Show-Log
"    files: " + ((Get-ChildItem $work -Filter 'state*' | ForEach-Object Name) -join ', ')

"`n=== T4 empty state.json"
Reset-Lab; Set-Content "$work\state.json" ''
Invoke-GM '-Quiet'; Show-State; Show-Log

"`n=== T5 ON killed mid-way (after the snapshot, while closing), then logon -Recover"
Reset-Lab; Start-Fakes
$p = Invoke-GM '-Quiet' -NoWait
$deadline = (Get-Date).AddSeconds(30); while (-not (Test-Path "$work\state.json") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
Start-Sleep 3; Stop-Process -Id $p.Id -Force; "    killed ON after state.json appeared + 3s"
Show-Fakes; Show-State
Invoke-GM '-Recover -Quiet'; Start-Sleep 3
Show-Fakes; Show-State; Show-Log

"`n=== T6 partial failure on OFF (an app's exe vanished), then press again after it is back"
Reset-Lab; Start-Fakes
Invoke-GM '-Quiet' | Out-Null
Move-Item "$bin\GmFakeArgs.exe" "$bin\GmFakeArgs.exe.bak"
Invoke-GM '-Quiet'; Start-Sleep 2; Show-State
Move-Item "$bin\GmFakeArgs.exe.bak" "$bin\GmFakeArgs.exe"
"  -- exe restored, press again"
Invoke-GM '-Quiet'; Start-Sleep 2; Show-Fakes; Show-State; Show-Log

"`n=== T7 hotkey pressed while another run holds the mutex"
Reset-Lab
$holder = Start-Process powershell.exe -ArgumentList "-NoProfile -Command `"`$m = New-Object Threading.Mutex(`$false, 'GameModeToggleMutex-test'); [void]`$m.WaitOne(); Start-Sleep 8`"" -PassThru -WindowStyle Hidden
Start-Sleep 2
Invoke-GM '-WhatIf'; "    stdout: " + ((Get-Content "$work\stdout.txt") -join ' / '); Show-Log
"    log written: $(Test-Path "$work\gamemode.log")"
Stop-Process -Id $holder.Id -Force -ErrorAction SilentlyContinue

"`n=== T8 -WhatIf: is the dry run distinguishable in the log?"
Reset-Lab; Start-Fakes
Invoke-GM '-WhatIf' | Out-Null; Show-Fakes; Show-Log

"`n=== T9 a single-process app was reopened by hand during the game, then OFF"
Reset-Lab; Start-Fakes
Invoke-GM '-Quiet' | Out-Null
Start-Process "$bin\GmFakeWin.exe" -WindowStyle Minimized; Start-Sleep 2; "    user reopened Fake Window by hand"
Invoke-GM '-Quiet' | Out-Null; Start-Sleep 2
"    GmFakeWin instances after OFF: $(@(Get-Process GmFakeWin -ErrorAction SilentlyContinue).Count)"
Show-Log

Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force
"`n################ done"




