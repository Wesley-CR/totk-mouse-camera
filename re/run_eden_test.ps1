# Automated Eden test harness.
#
# Launches Eden with TOTK, samples CPU over a window, then reports:
#   * whether the process looks alive (CPU above threshold) or wedged
#   * every NativeMouse line the emulator logged
#
# The CPU heuristic comes from the user: a healthy boot climbs past ~15% CPU,
# a wedged one sits at idle on the loading screen.
#
# Usage:
#   pwsh -File re\run_eden_test.ps1 [-WaitSeconds 30] [-Threshold 15] [-Keep]

param(
    [int]$WaitSeconds = 30,
    [double]$Threshold = 15.0,
    [switch]$Keep
)

$ErrorActionPreference = 'Continue'

$eden     = "$env:USERPROFILE\Downloads\Eden-Windows-defddec47f-amd64-msvc-standard\eden.exe"
$repo     = Split-Path -Parent $PSScriptRoot
$rom      = Join-Path $repo "roms\[ROMsNS] The Legend of Zelda Tears of the Kingdom [0100F2C0115B6000][v0][US].xci"
$edenLog  = "$env:APPDATA\eden\log\eden_log.txt"

if (-not (Test-Path $eden)) { Write-Host "eden.exe not found: $eden"; exit 1 }
# -LiteralPath: the ROM filename contains [ ] which Test-Path would treat as wildcards.
if (-not (Test-Path -LiteralPath $rom))  { Write-Host "rom not found: $rom"; exit 1 }

# Fresh log so we only see this run.
if (Test-Path $edenLog) { Remove-Item $edenLog -Force -ErrorAction SilentlyContinue }

Write-Host "launching eden..."
# Build the argument string by hand and wrap the path in embedded quotes. Using
# -ArgumentList with an array lets PowerShell re-quote the path itself, which
# made Eden receive the filename *including* quote characters and fail with
# "Failed to obtain loader for ...". Embedding explicit \" avoids that.
$argString = '-g "' + $rom + '"'
Write-Host "args: $argString"
$proc = Start-Process -FilePath $eden -ArgumentList $argString -PassThru
Write-Host "pid $($proc.Id)"

# Sample CPU% across the wait window. Normalise by logical processor count so the
# number is comparable to the user's "15%" figure.
$cores = [Environment]::ProcessorCount
$samples = @()
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $WaitSeconds) {
    Start-Sleep -Seconds 2
    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($null -eq $p) { Write-Host "process exited early"; break }
    $cpu = $p.CPU                     # total CPU seconds consumed
    $samples += [pscustomobject]@{ T = [int]((Get-Date) - $t0).TotalSeconds; Cpu = $cpu }
    $p.Refresh()
}

$peak = 0.0
if ($samples.Count -ge 2) {
    # delta cpu seconds / delta wall seconds / cores * 100
    for ($i = 1; $i -lt $samples.Count; $i++) {
        $dc = $samples[$i].Cpu - $samples[$i-1].Cpu
        $dt = $samples[$i].T   - $samples[$i-1].T
        if ($dt -gt 0) {
            $pct = ($dc / $dt) / $cores * 100.0
            if ($pct -gt $peak) { $peak = $pct }
        }
    }
}

$alive = $peak -ge $Threshold
Write-Host ""
Write-Host ("CPU samples ({0} cores): {1}" -f $cores, (($samples | ForEach-Object { "$($_.T)s=$([math]::Round($_.Cpu,1))" }) -join ' '))
Write-Host ("peak CPU: {0:N1}%   threshold {1}%   => {2}" -f $peak, $Threshold, $(if ($alive) { "ALIVE" } else { "WEDGED/IDLE" }))

if (-not $Keep) {
    Write-Host ""
    Write-Host "stopping eden..."
    Get-Process eden -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
}

Write-Host ""
Write-Host "=== NativeMouse lines ==="
if (Test-Path $edenLog) {
    $m = Select-String -Path $edenLog -Pattern "NativeMouse" -ErrorAction SilentlyContinue
    if ($m) { $m | ForEach-Object { $_.Line -replace '^.*OutputDebugString: ','  ' } }
    else { Write-Host "  (none)" }
} else { Write-Host "  (no eden log)" }

Write-Host ""
Write-Host "=== exlaunch tail ==="
if (Test-Path $edenLog) {
    $e = Select-String -Path $edenLog -Pattern "exlaunch" -ErrorAction SilentlyContinue
    if ($e) { $e | Select-Object -Last 4 | ForEach-Object { $_.Line -replace '^.*OutputDebugString: ','  ' } }
    else { Write-Host "  (none - module may not have loaded)" }
}
