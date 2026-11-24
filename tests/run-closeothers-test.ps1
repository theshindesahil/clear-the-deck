# Live test of closeOthers against fake apps only. Windows PowerShell 5.1.
# Safety: GAMEMODE_SCOPE limits discovery to the lab's bin folder, the test config keeps the real apps by name as a
# second guard, and a -WhatIf pre-check aborts unless every unlisted candidate is a GmFake* exe.
$ErrorActionPreference = 'Continue'
$L    = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $L 'work-closeothers'
$bin  = Join-Path $L 'bin'
$ev   = Join-Path $L 'out\events.log'
$plan = ((powercfg /getactivescheme) -replace '.*([0-9a-f]{8}-[0-9a-f-]{27}).*', '$1').Trim()
$env:GAMEMODE_SCOPE = "$bin\"
$fakeSvc = Join-Path $L 'fake-services-co.ps1'; Set-Content $fakeSvc '$allow = @()'; $env:GAMEMODE_SERVICES_FILE = $fakeSvc

Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 500
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $work | Out-Null
$s = (Get-Content "$(Split-Path -Parent $L)\gamemode.ps1" -Raw).Replace("'GameModeToggleMutex'", "'GameModeToggleMutex-test'")
$s = $s.Replace('function Show-Toast($title, $msg) {', 'function Show-Toast($title, $msg) { Write-Log "  [test] toast: $title - $msg"; return')
Set-Content "$work\gamemode.ps1" $s; Copy-Item "$(Split-Path -Parent $L)\dialog.ps1" $work
[ordered]@{
    gamePowerPlan = $plan; services = @(); gameFolders = @("$L\Games"); excludePathParts = @()
    pauseApps = @([ordered]@{ name = 'Fake Window'; processes = @('GmFakeWin') })
    closeOthers = $true; othersTimeoutSec = 8; launchers = @()
    keepNames = @('GmFakeArgs', 'claude', 'zen', 'WhatsApp*', 'explorer', 'PowerToys*', 'Flow.Launcher', 'ShareX', 'fluidvoice', 'Rainmeter', 'Everything', 'wallpaper*', 'msedgewebview2', 'steam*')
    keepPaths = @("$env:ProgramFiles\*", "${env:ProgramFiles(x86)}\*", 'D:\SteamLibrary\*')
} | ConvertTo-Json -Depth 5 | Set-Content "$work\config.json"
Remove-Item $ev -ErrorAction SilentlyContinue

function Invoke-GM($argText) {
    $p = Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$work\gamemode.ps1`" $argText" `
        -WorkingDirectory $work -NoNewWindow -PassThru -RedirectStandardOutput "$work\stdout.txt" -RedirectStandardError "$work\stderr.txt"
    $null = $p.Handle; [void]$p.WaitForExit(120000)
    $err = (Get-Content "$work\stderr.txt" -ErrorAction SilentlyContinue | Where-Object { $_ -match '\S' } | Select-Object -First 2) -join ' / '
    "    exit=$($p.ExitCode)$(if ($err) { "  stderr: $err" })"
}
function Show-Fakes { "    running: " + ((@(Get-Process GmFake* -ErrorAction SilentlyContinue | ForEach-Object ProcessName | Sort-Object -Unique)) -join ', ') }

Start-Process "$bin\listed\GmFakeWin.exe" -WindowStyle Minimized
Start-Process "$bin\GmFakeTray.exe" -ArgumentList '--tray'
Start-Process "$bin\GmFakeDeaf.exe" -ArgumentList '--deaf'
Start-Process "$bin\GmFakeArgs.exe" -ArgumentList '--profile "My Profile"' -WindowStyle Minimized
Start-Process "$bin\GmFakeSlow.exe" -ArgumentList '--slow 3 -url https://example.test/?token=SECRET' -WindowStyle Minimized
Start-Process "$bin\GmFakeQuit.exe" -ArgumentList '--keepme' -WindowStyle Minimized
Start-Sleep 2; Remove-Item $ev, "$bin\out\events.log" -ErrorAction SilentlyContinue

"=== pre-check (-WhatIf): unlisted candidates must all be fakes"
Invoke-GM '-WhatIf'
$out = Get-Content "$work\stdout.txt"; $out | ForEach-Object { "    $_" }
$labels = @($out | Where-Object { $_ -match '^\s+\[x\] ' } | ForEach-Object { ($_ -replace '^\s+\[x\] ', '').Trim() })
$bad = @($labels | Where-Object { $_ -notmatch '^(Fake Window|GmFake.*)$' })
if ($bad.Count -gt 0) { "ABORT: non-fake candidates: $($bad -join ', ')"; Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force; exit 1 }
"    pre-check passed: $($labels -join ', ')"

"`n=== ON"
Invoke-GM '-Quiet'; Show-Fakes
Get-Content $ev, "$bin\out\events.log" -ErrorAction SilentlyContinue | Sort-Object | ForEach-Object { "    on  | $_" }; Remove-Item $ev, "$bin\out\events.log" -ErrorAction SilentlyContinue
"    state.json has SECRET: $([bool](Select-String -Path "$work\state.json" -Pattern SECRET -Quiet))"
"`n=== OFF"
Invoke-GM '-Quiet'; Start-Sleep 3; Show-Fakes
Get-Content $ev, "$bin\out\events.log" -ErrorAction SilentlyContinue | Sort-Object | ForEach-Object { "    off | $_" }
"    state.json exists: $(Test-Path "$work\state.json")"
Get-Content "$work\gamemode.log" | ForEach-Object { "    log | " + ($_ -replace '^\S+ ', '') }
Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force



