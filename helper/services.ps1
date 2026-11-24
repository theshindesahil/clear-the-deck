# Elevated helper. Installed to C:\Program Files\GameMode (admin-writable only) and run by scheduled tasks.
# It can ONLY touch the services in the allowlist below, whatever is passed in.
param([ValidateSet('Stop', 'Start')][string]$Action)
$allow = @('postgresql-x64-18')
foreach ($n in $allow) {
    $s = Get-Service $n -ErrorAction SilentlyContinue
    if (-not $s) { continue }
    if ($Action -eq 'Stop'  -and $s.Status -eq 'Running') { Stop-Service  $n -Force -ErrorAction SilentlyContinue }
    if ($Action -eq 'Start' -and $s.Status -ne 'Running') { Start-Service $n -ErrorAction SilentlyContinue }
}
