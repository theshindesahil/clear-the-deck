# New v3 cases, fakes only. Windows PowerShell 5.1.
#   T10 services come from the helper allowlist (fake file, fake service name), config drift is logged
#   T11 -Recover waits for logon startup apps when the session is young, then reopens only what is missing
#   T12 -Recover does not wait when the session is old
# Safety: GAMEMODE_SERVICES_FILE points at a fake file, so the real helper and PostgreSQL are never read or touched.
$ErrorActionPreference = 'Continue'
$L    = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $L 'work-extra'
$bin  = Join-Path $L 'bin'
$plan = ((powercfg /getactivescheme) -replace '.*([0-9a-f]{8}-[0-9a-f-]{27}).*', '$1').Trim()
$fakeSvc = Join-Path $L 'fake-services-extra.ps1'
Set-Content $fakeSvc '$allow = @(''gm-fake-service-does-not-exist'')'
$env:GAMEMODE_SERVICES_FILE = $fakeSvc
Remove-Item Env:GAMEMODE_TEST_LOGON_AGE_SEC -ErrorAction SilentlyContinue

function Reset-Lab($cfgExtra) {
    Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 500
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $work | Out-Null
    $s = (Get-Content "$(Split-Path -Parent $L)\gamemode.ps1" -Raw).Replace("'GameModeToggleMutex'", "'GameModeToggleMutex-test'")
    $s = $s.Replace('function Show-Toast($title, $msg) {', 'function Show-Toast($title, $msg) { Write-Log "  [test] toast: $title - $msg"; return')
    Set-Content "$work\gamemode.ps1" $s; Copy-Item "$(Split-Path -Parent $L)\dialog.ps1" $work
    $cfg = [ordered]@{
        gamePowerPlan = $plan; services = @('some-other-service'); gameFolders = @("$L\Games"); excludePathParts = @(); closeOthers = $false
        pauseApps = @([ordered]@{ name = 'Fake Window'; processes = @('GmFakeWin') }, [ordered]@{ name = 'Fake Tray'; processes = @('GmFakeTray') })
    }
    foreach ($k in $cfgExtra.Keys) { $cfg[$k] = $cfgExtra[$k] }
    $cfg | ConvertTo-Json -Depth 5 | Set-Content "$work\config.json"
}
function Invoke-GM($argText) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$work\gamemode.ps1`" $argText" `
        -WorkingDirectory $work -NoNewWindow -PassThru -RedirectStandardOutput "$work\stdout.txt" -RedirectStandardError "$work\stderr.txt"
    $null = $p.Handle; [void]$p.WaitForExit(180000)
    $err = (Get-Content "$work\stderr.txt" -ErrorAction SilentlyContinue | Where-Object { $_ -match '\S' } | Select-Object -First 2) -join ' / '
    "    exit=$($p.ExitCode) took {0:N0}s$(if ($err) { "  stderr: $err" })" -f $sw.Elapsed.TotalSeconds
}
function Show-Fakes { "    running: " + ((@(Get-Process GmFake* -ErrorAction SilentlyContinue | ForEach-Object ProcessName | Sort-Object -Unique)) -join ', ') }
function Show-Log { Get-Content "$work\gamemode.log" -ErrorAction SilentlyContinue | ForEach-Object { "    log | " + ($_ -replace '^\S+ ', '') } }

"=== T10 services from the allowlist; config.json lists a different service"
Reset-Lab @{}
Start-Process "$bin\GmFakeWin.exe" -WindowStyle Minimized; Start-Sleep 2
Invoke-GM '-WhatIf'
"    stdout mentions the fake service: $([bool](Select-String -Path "$work\stdout.txt" -Pattern 'gm-fake-service' -Quiet))  (a nonexistent service is not running, so it is not offered)"
Show-Log | Select-String 'WARNING|snapshot'
"  -- with the helper file missing"
$env:GAMEMODE_SERVICES_FILE = "$L\no-such-file.ps1"; Reset-Lab @{ services = @() }; Invoke-GM '-WhatIf'; Show-Log | Select-String 'WARNING'
$env:GAMEMODE_SERVICES_FILE = $fakeSvc

"`n=== T11 -Recover with a young session (logon age 80s, settle 90s): waits ~10s, skips what Windows already started"
Reset-Lab @{ recoverSettleSec = 90 }
Start-Process "$bin\GmFakeWin.exe" -WindowStyle Minimized; Start-Process "$bin\GmFakeTray.exe" -ArgumentList '--tray'; Start-Sleep 2
Invoke-GM '-Quiet' | Out-Null; "    after ON:"; Show-Fakes
Start-Process "$bin\GmFakeTray.exe" -ArgumentList '--tray'; Start-Sleep 1; "    'Windows startup' launched Fake Tray by itself"
$env:GAMEMODE_TEST_LOGON_AGE_SEC = '80'
Invoke-GM '-Recover -Quiet'; Start-Sleep 2
Remove-Item Env:GAMEMODE_TEST_LOGON_AGE_SEC
Show-Fakes; "    GmFakeTray instances: $(@(Get-Process GmFakeTray -ErrorAction SilentlyContinue).Count)  state.json exists: $(Test-Path "$work\state.json")"
Show-Log | Select-String 'recover|restart|already|restored'

"`n=== T12 -Recover with an old session (logon age 600s): no wait"
Reset-Lab @{ recoverSettleSec = 90 }
Start-Process "$bin\GmFakeWin.exe" -WindowStyle Minimized; Start-Sleep 2
Invoke-GM '-Quiet' | Out-Null
$env:GAMEMODE_TEST_LOGON_AGE_SEC = '600'
Invoke-GM '-Recover -Quiet'; Start-Sleep 2
Remove-Item Env:GAMEMODE_TEST_LOGON_AGE_SEC
Show-Fakes; Show-Log | Select-String 'recover|restart|restored'

Get-Process GmFake* -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item Env:GAMEMODE_SERVICES_FILE -ErrorAction SilentlyContinue

