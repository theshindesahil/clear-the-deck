# Game Mode toggle. Run once to turn ON (snapshot + boost), run again to turn OFF (restore the snapshot).
# -WhatIf  : print what would happen, change nothing.
# -Recover : restore only if a snapshot exists (used at logon after a crash/reboot).
# -Quiet   : skip the confirmation info box.
# -Reset   : set a stuck or unwanted snapshot aside (state.reset-<time>.json) without restoring anything.
# -Exclude : app names (from config.json) to leave alone this run. Through powershell.exe -File, PowerShell
#            quoting is not parsed, so pass one double-quoted list: -Exclude "Zen Browser,Claude"
# -PreviewPng / -PreviewExpand : with -WhatIf only. Show the dialog, save a screenshot to the path, close it.
[CmdletBinding(PositionalBinding = $false)]
param([switch]$WhatIf, [switch]$Recover, [switch]$Quiet, [switch]$Reset, [string[]]$Exclude = @(), [string]$PreviewPng = '', [switch]$PreviewExpand)

# -File hands over 'A','B' literally, quotes included, so strip them after splitting on commas.
$Exclude = @($Exclude | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().Trim([char[]]"'`"").Trim() } | Where-Object { $_ })

$ErrorActionPreference = 'Continue'
$root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$cfg       = Get-Content "$root\config.json" -Raw | ConvertFrom-Json
$statePath = "$root\state.json"
$logPath   = "$root\gamemode.log"
$apps      = @($cfg.pauseApps | Where-Object { $Exclude -notcontains $_.name })
$run       = '{0:x4}' -f (Get-Random -Maximum 65536)
$sessionId = (Get-Process -Id $PID).SessionId
$balancedPlan = '381b4222-f694-41f0-9685-ff5bb260df2e'

# Keep the log small: above 200 KB, keep only the last 300 lines.
if ((Test-Path $logPath) -and (Get-Item $logPath).Length -gt 200KB) {
    $tail = Get-Content $logPath -Tail 300; Set-Content -Path $logPath -Value $tail
}

function Write-Log($m) { Add-Content -Path $logPath -Value ("{0} [{1}{2}] {3}" -f (Get-Date -Format s), $run, $(if ($WhatIf) { ' dry-run' } else { '' }), $m) }

$unknown = @($Exclude | Where-Object { @($cfg.pauseApps.name) -notcontains $_ })
if ($unknown.Count -gt 0) { Write-Log "WARNING: -Exclude names not in config.json, ignored: $($unknown -join ', ')"; Write-Host "Unknown -Exclude name(s): $($unknown -join ', ')" }
if ($PreviewPng -and -not $WhatIf) { Write-Log 'refused: -PreviewPng only works with -WhatIf'; Write-Host '-PreviewPng only works with -WhatIf'; exit 2 }

# The service list comes from the elevated helper's allowlist, the only list that helper will act on, so the two
# cannot drift. A "services" key left in config.json is ignored (a warning is logged if it differs).
# GAMEMODE_SERVICES_FILE is a test hook so the harness never points at the real helper.
$servicesFile = if ($env:GAMEMODE_SERVICES_FILE) { $env:GAMEMODE_SERVICES_FILE } else { 'C:\Program Files\GameMode\services.ps1' }
$svcNames = @()
if (Test-Path -LiteralPath $servicesFile) {
    $m = Select-String -LiteralPath $servicesFile -Pattern '^\s*\$allow\s*=\s*@\((.*)\)' | Select-Object -First 1
    if ($m) { $svcNames = @([regex]::Matches($m.Matches[0].Groups[1].Value, "'([^']+)'") | ForEach-Object { $_.Groups[1].Value }) }
    else { Write-Log "WARNING: no `$allow list found in $servicesFile; no services will be managed" }
} else { Write-Log "WARNING: $servicesFile not found; no services will be managed" }
if ($cfg.PSObject.Properties['services'] -and (@($cfg.services) -join ',') -ne ($svcNames -join ',')) {
    Write-Log "WARNING: config.json services [$(@($cfg.services) -join ',')] differ from the helper allowlist [$($svcNames -join ',')]; using the allowlist"
}

. "$root\dialog.ps1"

function Confirm-Action($headline, $items, $verbApp, $verbSvc, $infoLines, $hint, $yesText) {
    $allKeys = @($items | ForEach-Object { $_.Key })
    $all = [pscustomobject]@{ Confirmed = $true; Keys = $allKeys }
    if ($WhatIf -and -not $PreviewPng) {
        Write-Host "[dry-run] dialog: $headline"
        foreach ($i in $items) { Write-Host "  [x] $($i.Label)" }
        foreach ($l in $infoLines) { Write-Host "  $l" }
        return $all
    }
    if (($Quiet -or $Recover) -and -not $PreviewPng) { return $all }
    return (Show-GameModeDialog $headline $items $verbApp $verbSvc $infoLines $hint $yesText $PreviewPng $PreviewExpand.IsPresent)
}

# Runs one change. Returns $true on success. Cmdlet errors and native exit codes both count as failures.
function Invoke-Step($desc, [scriptblock]$action) {
    Write-Log $desc
    if ($WhatIf) { Write-Host "[dry-run] $desc"; return $true }
    try {
        $ErrorActionPreference = 'Stop'; $global:LASTEXITCODE = 0
        $null = & $action
        if ($LASTEXITCODE) { throw "exit code $LASTEXITCODE" }
        return $true
    } catch { Write-Log "  FAILED: $($_.Exception.Message)"; return $false }
}

function Show-Toast($title, $msg) {
    if ($WhatIf) { Write-Host "[dry-run] toast: $title - $msg"; return }
    try {
        [void][Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]
        $t = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent('ToastText02')
        $n = $t.GetElementsByTagName('text')
        [void]$n.Item(0).AppendChild($t.CreateTextNode($title))
        [void]$n.Item(1).AppendChild($t.CreateTextNode($msg))
        $appId = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
        [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($appId).Show([Windows.UI.Notifications.ToastNotification]::new($t))
    } catch { Write-Log "  toast failed: $($_.Exception.Message)" }
}

# Snapshot I/O. Written to a temp file, parsed back, then renamed over state.json, so a crash mid-write
# leaves either the old snapshot or the new one, never half of one.
function Save-State($state) {
    $tmp = "$statePath.tmp"
    $state | ConvertTo-Json -Depth 6 | Set-Content -Path $tmp -Encoding UTF8
    $null = Get-Content $tmp -Raw | ConvertFrom-Json
    Move-Item -Path $tmp -Destination $statePath -Force
}

function Move-StateAside($tag) {
    $dest = "$root\state.$tag-$(Get-Date -Format yyyyMMdd-HHmmss).json"
    Move-Item -Path $statePath -Destination $dest -Force
    return $dest
}

function Read-State {
    try {
        $raw = Get-Content $statePath -Raw -ErrorAction Stop
        if (-not $raw) { throw 'file is empty' }
        $s = $raw | ConvertFrom-Json -ErrorAction Stop
        if (-not $s.startedAt -or ($null -ne $s.schema -and $s.schema -ne 1)) { throw "unknown snapshot format (schema '$($s.schema)')" }
        return $s
    } catch {
        $dest = Move-StateAside 'corrupt'
        Write-Log "ERROR: snapshot unreadable ($($_.Exception.Message)). Moved to $dest. Nothing was restored."
        Show-Toast 'Game Mode: snapshot unreadable' "Nothing was restored. It was moved to $(Split-Path -Leaf $dest)."
        return $null
    }
}

function Get-ArgsFromCommandLine($cl) {
    if (-not $cl) { return '' }
    if ($cl.StartsWith('"')) { $i = $cl.IndexOf('"', 1); if ($i -lt 0) { return '' }; return $cl.Substring($i + 1).Trim() }
    $i = $cl.IndexOf(' '); if ($i -lt 0) { return '' }; return $cl.Substring($i + 1).Trim()
}

# Matches only processes in this logon session whose path this unelevated script can read. That leaves out
# services (session 0, such as Everything -svc) and elevated processes: it can neither close nor relaunch those.
function Get-CleanPath($p) { if ($p) { return ($p -replace '^\\\\\?\\', '') } }

function Get-AppProcesses($app, $all = $null) {
    if ($null -eq $all) { $all = @(Get-CimInstance Win32_Process) }   # callers polling many apps pass one snapshot
    $pats = @($app.processes)
    $list = @($all | Where-Object {
        $n = $_.Name -replace '\.exe$', ''
        $hit = $false
        foreach ($p in $pats) { if ($n -like $p) { $hit = $true; break } }
        $hit -and $_.SessionId -eq $sessionId -and $_.ExecutablePath
    })
    if ($app.exactPath) { $list = @($list | Where-Object { (Get-CleanPath $_.ExecutablePath) -eq $app.exactPath }) }
    if ($app.pathLike) { $list = @($list | Where-Object { $_.ExecutablePath -like $app.pathLike }) }
    if ($app.skipCmd)  { $list = @($list | Where-Object { -not $_.CommandLine -or $_.CommandLine -notlike "*$($app.skipCmd)*" }) }
    return $list
}

# WM_CLOSE to every top-level window the processes own, hidden ones included. CloseMainWindow only reaches a
# visible main window and taskkill without /F skips hidden windows, so tray apps never saw a close request.
function Initialize-Win32 {
    if (-not ('GameMode.Win32' -as [type])) {
        Add-Type -Namespace GameMode -Name Win32 -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
'@
    }
}

function Send-WmClose([int[]]$procIds) {
    Initialize-Win32
    $want = @{}; foreach ($i in $procIds) { $want[[uint32]$i] = $true }
    $cb = [GameMode.Win32+EnumProc] {
        param($h, $l)
        $owner = [uint32]0; [void][GameMode.Win32]::GetWindowThreadProcessId($h, [ref]$owner)
        if ($want.ContainsKey($owner)) { [void][GameMode.Win32]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
        $true
    }
    [void][GameMode.Win32]::EnumWindows($cb, [IntPtr]::Zero)
}

# PIDs that own at least one top-level window, visible or hidden. A process with none cannot be asked to close.
function Get-WindowOwnerPids {
    Initialize-Win32
    $set = @{}
    $cb = [GameMode.Win32+EnumProc] { param($h, $l) $o = [uint32]0; [void][GameMode.Win32]::GetWindowThreadProcessId($h, [ref]$o); $set[[int]$o] = $true; $true }
    [void][GameMode.Win32]::EnumWindows($cb, [IntPtr]::Zero)
    return $set
}

# PE subsystem of an exe: 2 = Windows GUI, 3 = console. Console tools (git, node, shells) are never closed.
$subsystemCache = @{}
function Get-ExeSubsystem($path) {
    if ($subsystemCache.ContainsKey($path)) { return $subsystemCache[$path] }
    $ss = 0
    try {
        $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite'); $br = New-Object IO.BinaryReader($fs)
        $fs.Position = 0x3C; $pe = $br.ReadInt32(); $fs.Position = $pe + 4 + 20 + 68; $ss = [int]$br.ReadUInt16(); $fs.Close()
    } catch { }
    $subsystemCache[$path] = $ss
    return $ss
}

# AUMID for a Store app's main exe (PackageFamilyName!AppId), or $null for helpers and desktop apps.
function Get-StoreAumid($path) {
    if ($path -notmatch '\\WindowsApps\\([^\\]+)\\(.+)$') { return $null }
    $folder = $Matches[1]; $rel = $Matches[2]
    try {
        $pkg = Get-AppxPackage -Name ($folder -split '_')[0] -ErrorAction Stop | Where-Object { $_.InstallLocation -like "*\$folder" } | Select-Object -First 1
        if (-not $pkg) { return $null }
        $appEl = @((Get-AppxPackageManifest $pkg).Package.Applications.Application) | Where-Object { $_.Executable -eq $rel } | Select-Object -First 1
        if ($appEl) { return "$($pkg.PackageFamilyName)!$($appEl.Id)" }
    } catch { }
    return $null
}

# "Everything else": every GUI app in this session that owns a window and is not kept, listed, a game, a console
# tool, part of Windows, or started by a game launcher. Closed politely only (never force-killed), all at once.
# GAMEMODE_SCOPE (path prefix) narrows discovery further; the test harness uses it to touch only fake apps.
function Get-OtherApps {
    if (-not $cfg.closeOthers) { return @() }
    $all = @(Get-CimInstance Win32_Process)
    $byPid = @{}; foreach ($p in $all) { $byPid[[int]$p.ProcessId] = $p }
    $withWindow = Get-WindowOwnerPids
    $systemParents = @('svchost.exe', 'dllhost.exe', 'RuntimeBroker.exe', 'sihost.exe', 'taskhostw.exe', 'services.exe', 'wininit.exe')

    $mine = @{}; $cur = $byPid[$PID]
    while ($cur -and -not $mine.ContainsKey([int]$cur.ProcessId)) { $mine[[int]$cur.ProcessId] = $true; $cur = $byPid[[int]$cur.ParentProcessId] }

    $listedPats = @($cfg.pauseApps | ForEach-Object { $_.processes })   # all listed apps, also ones excluded this run
    $launcherPids = @{}
    foreach ($p in $all) { foreach ($n in @($cfg.launchers)) { if (($p.Name -replace '\.exe$', '') -like $n) { $launcherPids[[int]$p.ProcessId] = $true } } }

    $isLauncherChild = {
        param($p)
        $child = $p
        for ($i = 0; $i -lt 10; $i++) {
            $parent = $byPid[[int]$child.ParentProcessId]
            if (-not $parent -or $parent.CreationDate -gt $child.CreationDate) { return $false }   # parent gone or PID reused
            if ($launcherPids.ContainsKey([int]$parent.ProcessId)) { return $true }
            $child = $parent
        }
        return $false
    }

    # Install folders of running listed apps: their helpers (Phone Link's YourPhoneAppProxyHost) belong to them.
    $listedRoots = @($all | Where-Object { $_.ExecutablePath -and $_.SessionId -eq $sessionId } | Where-Object {
            $b = $_.Name -replace '\.exe$', ''; @($listedPats | Where-Object { $b -like $_ }).Count -gt 0
        } | ForEach-Object {
            $p = Get-CleanPath $_.ExecutablePath
            if ($p -match '^(.*\\WindowsApps\\[^\\]+\\)') { $Matches[1] } else { (Split-Path -Parent $p) + '\' }
        } | Sort-Object -Unique)

    $cands = @(); $kept = @{}
    foreach ($p in $all) {
        $path = Get-CleanPath $p.ExecutablePath
        if ($p.SessionId -ne $sessionId -or -not $path) { continue }
        if ($path.StartsWith("$env:windir\", [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($env:GAMEMODE_SCOPE -and -not $path.StartsWith($env:GAMEMODE_SCOPE, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($mine.ContainsKey([int]$p.ProcessId)) { continue }
        $base = $p.Name -replace '\.exe$', ''
        if (@($listedPats | Where-Object { $base -like $_ }).Count -gt 0) { continue }
        $why = $null
        if (@($listedRoots | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { $why = 'part of a listed app' }
        elseif (@($cfg.gameFolders | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { $why = 'game folder' }
        elseif (@($cfg.keepNames | Where-Object { $base -like $_ }).Count -gt 0) { $why = 'keepNames' }
        elseif (@($cfg.keepPaths | Where-Object { $path -like $_ }).Count -gt 0) { $why = 'keepPaths' }
        elseif ((Get-ExeSubsystem $path) -ne 2) { $why = 'console tool' }
        elseif (& $isLauncherChild $p) { $why = 'started by a game launcher' }
        if ($why) { $kept[$base] = $why; continue }
        $cands += $p
    }

    $out = @()
    foreach ($g in @($cands | Group-Object { Get-CleanPath $_.ExecutablePath })) {
        if (@($g.Group | Where-Object { $withWindow.ContainsKey([int]$_.ProcessId) }).Count -eq 0) { $kept[[IO.Path]::GetFileNameWithoutExtension($g.Name)] = 'no window to close'; continue }
        $root = $g.Group | Where-Object { (Get-CleanPath $byPid[[int]$_.ParentProcessId].ExecutablePath) -ne $g.Name } | Select-Object -First 1
        if (-not $root) { $root = $g.Group[0] }
        $parentName = $byPid[[int]$root.ParentProcessId].Name
        $desc = $null; try { $desc = (Get-Item -LiteralPath $g.Name).VersionInfo.FileDescription } catch { }
        $name = if ($desc -and $desc.Trim()) { $desc.Trim() } else { [IO.Path]::GetFileNameWithoutExtension($g.Name) }
        if (@($cfg.pauseApps.name) -contains $name -or @($out.name) -contains $name) { $name = "$name ($([IO.Path]::GetFileName($g.Name)))" }
        $relArgs = Get-ArgsFromCommandLine $root.CommandLine
        if ($relArgs -match '://') { $relArgs = '' }   # a URL from a link handler, not a way to start the app
        $out += [pscustomobject]@{
            name = $name; processes = @([IO.Path]::GetFileNameWithoutExtension($g.Name)); exactPath = $g.Name
            aumid = (Get-StoreAumid $g.Name); args = $relArgs
            relaunch = -not ($parentName -and $systemParents -contains $parentName)   # started by a system host: it comes back on demand
        }
    }
    if ($WhatIf) { Write-Host ("[dry-run] kept: " + (($kept.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name) ($($_.Value))" }) -join ', ')) }
    return $out
}

function Test-GameProcessPath($path) {
    if (-not $path) { return $false }
    foreach ($ex in $cfg.excludePathParts) { if ($path -like "*$ex*") { return $false } }
    foreach ($f in $cfg.gameFolders) { if ($path.StartsWith($f, [StringComparison]::OrdinalIgnoreCase)) { return $true } }
    return $false
}

function Get-DisplayGpu {
    $a = Get-CimInstance Win32_VideoController | Where-Object { $_.CurrentHorizontalResolution } | Select-Object -First 1
    if ($a) { return $a.Name } else { return 'unknown' }
}

function Get-ActivePlanGuid {
    $o = powercfg /getactivescheme
    if ($o -match '([0-9a-fA-F]{8}-([0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12})') { return $Matches[1].ToLower() }
    return $null
}

function Test-PowerPlan($guid) { return [bool]((powercfg /list) -match [regex]::Escape($guid)) }

function Invoke-ServiceTask($taskName, $desired) {
    $null = Invoke-Step "run elevated task $taskName (services -> $desired)" {
        schtasks.exe /run /tn $taskName | Out-Null
        $deadline = (Get-Date).AddSeconds(40)
        do {
            Start-Sleep -Milliseconds 700
            $bad = @($svcNames | Where-Object { (Get-Service $_ -ErrorAction SilentlyContinue).Status -ne $desired })
        } while ($bad.Count -gt 0 -and (Get-Date) -lt $deadline)
        if ($bad.Count -gt 0) { throw "services not yet $desired : $($bad -join ',')" }
    }
}

function Start-GameMode {
    $state = [ordered]@{ schema = 1; startedAt = (Get-Date -Format s); powerPlan = (Get-ActivePlanGuid); apps = @(); services = @(); priorities = @() }

    # --- snapshot: apps ---
    foreach ($app in $apps) {
        $procs = @(Get-AppProcesses $app)   # @(): in 5.1 a lone CimInstance has no .Count
        if ($procs.Count -eq 0) { $state.apps += [ordered]@{ name = $app.name; wasRunning = $false }; continue }
        $cands = @($procs | Where-Object { $_.CommandLine -and $_.CommandLine -notlike '*--type=*' })
        if ($cands.Count -eq 0) { $cands = $procs }
        $main = $cands | Where-Object { ($_.Name -replace '\.exe$', '') -like $app.processes[0] } | Select-Object -First 1
        if (-not $main) { $main = $cands | Select-Object -First 1 }
        # relaunchArgs in config wins (a browser started by a link handler carries that URL in its command line).
        # Store apps relaunch by AUMID, so their command line is not kept at all.
        $relArgs = if ($app.PSObject.Properties['relaunchArgs']) { [string]$app.relaunchArgs } elseif ($app.aumid) { '' } else { Get-ArgsFromCommandLine $main.CommandLine }
        $state.apps += [ordered]@{ name = $app.name; wasRunning = $true; path = $main.ExecutablePath; args = $relArgs }
    }
    # --- snapshot: everything else (closeOthers) ---
    foreach ($o in @(Get-OtherApps)) {
        $state.apps += [ordered]@{ name = $o.name; wasRunning = $true; auto = $true; path = $o.exactPath; args = $o.args; aumid = $o.aumid; relaunch = $o.relaunch }
    }
    # --- snapshot: services ---
    foreach ($s in $svcNames) {
        $svc = Get-Service $s -ErrorAction SilentlyContinue
        $state.services += [ordered]@{ name = $s; wasRunning = ($svc -and $svc.Status -eq 'Running') }
    }
    # --- snapshot: priorities of running game processes ---
    $games = @()
    foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
        $path = $null; try { $path = $p.Path } catch { }
        if (Test-GameProcessPath $path) { $games += $p }
    }
    foreach ($g in $games) {
        $state.priorities += [ordered]@{ pid = $g.Id; name = $g.ProcessName; prior = [string]$g.PriorityClass }
    }

    $gamePlan  = $cfg.gamePowerPlan.ToLower()
    $closeList = @($state.apps | Where-Object wasRunning | ForEach-Object name)
    $svcList   = @($state.services | Where-Object wasRunning | ForEach-Object name)
    $planLine  = if ($state.powerPlan -ne $gamePlan) { 'Switch power plan to Ultimate Performance' } else { 'Power plan: already Ultimate Performance (no change)' }
    $items = @()
    foreach ($n in $closeList) { $items += [pscustomobject]@{ Key = "app:$n"; Label = $n; Kind = 'app' } }
    foreach ($n in $svcList)   { $items += [pscustomobject]@{ Key = "svc:$n"; Label = "$n (database service)"; Kind = 'service' } }
    $autoCount = @($state.apps | Where-Object { $_.auto -and $_.wasRunning }).Count
    $info = @($planLine, $(if ($autoCount) { "$autoCount of these are unlisted apps: asked to close, never forced" }), $(if ($games.Count -gt 0) { "High priority for $($games.Count) running game process(es)" } else { 'No game running yet. Launch it after this.' }))
    $info = @($info | Where-Object { $_ })
    $dlg = Confirm-Action 'Turn on Game Mode?' $items 'Closes' 'Stops' $info 'Press Ctrl+Alt+G again to restore everything exactly as it is now.' 'Turn on'
    if (-not $dlg.Confirmed) { Write-Log 'ON cancelled by user'; return }
    foreach ($e in $state.apps)     { if ($e.wasRunning -and $dlg.Keys -notcontains "app:$($e.name)") { $e.wasRunning = $false; Write-Log "skipped by user: $($e.name)" } }
    foreach ($e in $state.services) { if ($e.wasRunning -and $dlg.Keys -notcontains "svc:$($e.name)") { $e.wasRunning = $false; Write-Log "skipped by user: $($e.name)" } }
    $svcList   = @($state.services | Where-Object wasRunning | ForEach-Object name)

    if (-not $WhatIf) { Save-State $state }
    Write-Log "snapshot: plan=$($state.powerPlan) apps=$((@($state.apps | Where-Object wasRunning | ForEach-Object name)) -join ',') services=$($svcList -join ',') games=$((@($games | ForEach-Object ProcessName)) -join ',')"

    # --- boost ---
    $planSet = $false
    if ($state.powerPlan -ne $gamePlan) {
        if (Test-PowerPlan $gamePlan) { $planSet = Invoke-Step "power plan $($state.powerPlan) -> $gamePlan" { powercfg /setactive $gamePlan } }
        else { Write-Log "game power plan $gamePlan does not exist; plan left unchanged" }
    } else { Write-Log "power plan already $gamePlan" }

    # Close every app at once: quit commands first, then one WM_CLOSE round to all of them, then one shared wait.
    # Each app keeps its own deadline (closeTimeoutSec, default 8; othersTimeoutSec for unlisted apps) and its own
    # force-kill rule (listed apps: forceKill, default on; unlisted apps: never).
    $targets = @()
    foreach ($e in @($state.apps | Where-Object wasRunning)) {
        if ($e.auto) {
            $match = [pscustomobject]@{ name = $e.name; processes = @([IO.Path]::GetFileNameWithoutExtension($e.path)); exactPath = $e.path }
            $to = if ($cfg.othersTimeoutSec) { [int]$cfg.othersTimeoutSec } else { 10 }
            $targets += [pscustomobject]@{ entry = $e; app = $match; timeout = $to; mayKill = $false; quitArgs = $null }
        } else {
            $a = $apps | Where-Object { $_.name -eq $e.name } | Select-Object -First 1
            $to = if ($a.closeTimeoutSec) { [int]$a.closeTimeoutSec } else { 8 }
            $targets += [pscustomobject]@{ entry = $e; app = $a; timeout = $to; mayKill = ($a.forceKill -ne $false); quitArgs = $a.quitArgs }
        }
    }
    if ($targets.Count -gt 0) {
        $null = Invoke-Step "close $($targets.Count) app(s) at once: $(@($targets | ForEach-Object { $_.entry.name }) -join ', ')" {
            foreach ($t in $targets) { if ($t.quitArgs -and $t.entry.path) { Start-Process -FilePath $t.entry.path -ArgumentList $t.quitArgs } }
            $snap = @(Get-CimInstance Win32_Process)
            $pids = @($targets | ForEach-Object { Get-AppProcesses $_.app $snap } | ForEach-Object { [int]$_.ProcessId })
            if ($pids.Count -gt 0) { Send-WmClose $pids }
            $start = Get-Date
            $open = @($targets)
            while ($open.Count -gt 0) {
                Start-Sleep -Milliseconds 500
                $snap = @(Get-CimInstance Win32_Process)
                $elapsed = ((Get-Date) - $start).TotalSeconds
                $still = @()
                foreach ($t in $open) {
                    $left = @(Get-AppProcesses $t.app $snap)
                    if ($left.Count -eq 0) { Write-Log ("  {0}: closed cleanly after {1:N1}s" -f $t.entry.name, $elapsed); continue }
                    if ($elapsed -lt $t.timeout) { $still += $t; continue }
                    if ($t.mayKill) {
                        Write-Log "  $($t.entry.name): force-killing $($left.Count) process(es) after $($t.timeout)s"
                        $left | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
                    } else {
                        $t.entry.wasRunning = $false   # still open, so OFF must not relaunch it
                        Write-Log "  $($t.entry.name): left running, did not accept the close (may be asking to save)"
                    }
                }
                $open = $still
            }
        }
    }

    if ($svcList.Count -gt 0) { Invoke-ServiceTask 'GameMode-StopServices' 'Stopped' }

    foreach ($g in $games) {
        $null = Invoke-Step "priority High: $($g.ProcessName) (pid $($g.Id))" { $g.PriorityClass = 'High' }
    }

    if (-not $WhatIf) { Save-State $state }
    $closed = @($state.apps | Where-Object wasRunning).Count
    $msg = "Closed $closed app(s)" + $(if ($svcList.Count) { ", stopped $($svcList.Count) service(s)" } else { '' }) + $(if ($planSet) { ', power plan set' } else { ', power plan unchanged' }) + ". Display GPU: $(Get-DisplayGpu)."
    Show-Toast 'Game Mode ON' $msg
}

function Stop-GameMode {
    $state = Read-State
    if (-not $state) { return }

    $reopen = @($state.apps | Where-Object wasRunning | ForEach-Object name)
    $svcs   = @($state.services | Where-Object wasRunning | ForEach-Object name)
    $items = @()
    foreach ($n in $reopen) { $items += [pscustomobject]@{ Key = "app:$n"; Label = $n; Kind = 'app' } }
    foreach ($n in $svcs)   { $items += [pscustomobject]@{ Key = "svc:$n"; Label = "$n (database service)"; Kind = 'service' } }
    $info = @("Restores the power plan saved at $($state.startedAt)")
    $dlg = Confirm-Action 'Turn off Game Mode?' $items 'Reopens' 'Starts' $info 'Everything ticked above is put back exactly as it was before.' 'Turn off'
    if (-not $dlg.Confirmed) { Write-Log 'OFF cancelled by user'; return }
    foreach ($e in @($state.apps))     { if ($e.wasRunning -and $dlg.Keys -notcontains "app:$($e.name)") { $e.wasRunning = $false; Write-Log "not reopened (user choice): $($e.name)" } }
    foreach ($e in @($state.services)) { if ($e.wasRunning -and $dlg.Keys -notcontains "svc:$($e.name)") { $e.wasRunning = $false; Write-Log "not restarted (user choice): $($e.name)" } }
    $svcs = @($state.services | Where-Object wasRunning | ForEach-Object name)
    $failed = @()

    foreach ($p in @($state.priorities)) {
        $proc = Get-Process -Id $p.pid -ErrorAction SilentlyContinue
        if ($proc -and $proc.ProcessName -eq $p.name) { $null = Invoke-Step "priority $($p.prior): $($p.name)" { $proc.PriorityClass = $p.prior } }
    }
    $state.priorities = @()   # PIDs do not survive a retry or a reboot

    if ($svcs.Count -gt 0) {
        Invoke-ServiceTask 'GameMode-StartServices' 'Running'
        foreach ($e in @($state.services)) {
            if (-not $e.wasRunning) { continue }
            if ($WhatIf -or (Get-Service $e.name -ErrorAction SilentlyContinue).Status -eq 'Running') { $e.wasRunning = $false } else { $failed += "service $($e.name)" }
        }
    }

    # Power plan first: it needs no apps and should not wait for the logon settle below.
    if ($state.powerPlan -and (Get-ActivePlanGuid) -ne $state.powerPlan) {
        $target = $state.powerPlan
        if (-not (Test-PowerPlan $target)) { Write-Log "saved power plan $target no longer exists; using Balanced"; $target = $balancedPlan }
        if (Invoke-Step "power plan -> $target" { powercfg /setactive $target }) { $state.powerPlan = $null } else { $failed += 'power plan' }
    } else { $state.powerPlan = $null }

    # At logon, Windows is still launching its own startup apps (Run keys, Startup folder, Store startup tasks).
    # Wait until the session is recoverSettleSec old (default 90) so those apps come up by themselves first and the
    # "already running" check below skips them, instead of racing them into duplicates or stray windows.
    if ($Recover -and -not $WhatIf -and @($state.apps | Where-Object wasRunning).Count -gt 0) {
        $settle = if ($cfg.recoverSettleSec -ne $null) { [int]$cfg.recoverSettleSec } else { 90 }
        $age = 0.0
        if ($env:GAMEMODE_TEST_LOGON_AGE_SEC) { $age = [double]$env:GAMEMODE_TEST_LOGON_AGE_SEC }   # test hook
        else {
            $shell = Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sessionId } | Sort-Object StartTime | Select-Object -First 1
            if ($shell) { $age = ((Get-Date) - $shell.StartTime).TotalSeconds }
        }
        if ($settle - $age -gt 0) {
            Write-Log ("recover: session is {0:N0}s old, waiting {1:N0}s for logon startup apps before reopening anything" -f $age, ($settle - $age))
            Start-Sleep -Seconds ([int][math]::Ceiling($settle - $age))
        }
    }

    foreach ($a in @($state.apps)) {
        if (-not $a.wasRunning) { continue }
        $app = if ($a.auto) { [pscustomobject]@{ name = $a.name; processes = @([IO.Path]::GetFileNameWithoutExtension($a.path)); exactPath = $a.path; aumid = $a.aumid } }
               else { $cfg.pauseApps | Where-Object { $_.name -eq $a.name } | Select-Object -First 1 }
        if ($a.auto -and -not $a.relaunch) { Write-Log "$($a.name): started by a system host, comes back on its own; not relaunched"; $a.wasRunning = $false; continue }
        if ($app -and @(Get-AppProcesses $app).Count -gt 0) { Write-Log "$($a.name) already running, skip"; $a.wasRunning = $false; continue }
        $ok = Invoke-Step "restart $($a.name)" {
            if ($app -and $app.aumid) { Start-Process -FilePath 'explorer.exe' -ArgumentList "shell:AppsFolder\$($app.aumid)" }
            elseif ($a.path) {
                if (-not (Test-Path -LiteralPath $a.path)) { throw "not found: $($a.path)" }
                $sp = @{ FilePath = $a.path; WorkingDirectory = (Split-Path -Parent $a.path) }
                if ($a.args) { $sp.ArgumentList = $a.args }
                if ($app -and $app.hidden) { $sp.WindowStyle = 'Hidden' }
                Start-Process @sp
            } else { throw 'no path or app id to relaunch' }
        }
        if ($ok) { $a.wasRunning = $false } else { $failed += $a.name }
    }

    if ($WhatIf) { Write-Log 'restore preview done'; return }
    if ($failed.Count -gt 0) {
        Save-State $state
        Write-Log "restore incomplete, snapshot kept for retry. Failed: $($failed -join ', ')"
        Show-Toast 'Game Mode OFF: incomplete' "Not restored: $($failed -join ', '). Press Ctrl+Alt+G to retry."
        return
    }
    Remove-Item $statePath -Force -ErrorAction SilentlyContinue
    Write-Log 'restored, state removed'
    Show-Toast 'Game Mode OFF' 'Everything restored to how it was before.'
}

$mutex = New-Object System.Threading.Mutex($false, 'GameModeToggleMutex')
$got = $false
try { $got = $mutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) {
    Write-Log 'busy: a previous press is still running; this press was ignored'
    if (-not (New-Object -ComObject WScript.Shell).AppActivate('Game Mode')) { Show-Toast 'Game Mode is busy' 'The previous press is still working. Try again in a few seconds.' }
    exit
}
try {
    $on = Test-Path $statePath
    if ($Reset) { if ($on) { Write-Log "reset: snapshot moved to $(Move-StateAside 'reset'); nothing restored" } else { Write-Log 'reset: no snapshot' }; return }
    if ($Recover) { if ($on) { Write-Log 'recover at logon'; Stop-GameMode }; return }
    if ($on) { Stop-GameMode } else { Start-GameMode }
} finally { $mutex.ReleaseMutex() }
