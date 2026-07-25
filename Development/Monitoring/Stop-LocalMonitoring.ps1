[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$monitoringRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolsRoot = [System.IO.Path]::GetFullPath((Join-Path $monitoringRoot '.tools'))
$pidFile = Join-Path $monitoringRoot 'data\monitoring-pids.json'

if (-not (Test-Path $pidFile))
{
    Write-Host 'Local monitoring is not running (PID file not found).'
    exit 0
}

$savedPids = Get-Content -Raw -Encoding utf8 $pidFile | ConvertFrom-Json
foreach ($entry in $savedPids.PSObject.Properties)
{
    $process = Get-Process -Id ([int]$entry.Value) -ErrorAction SilentlyContinue
    if (-not $process)
    {
        continue
    }

    $processPath = [System.IO.Path]::GetFullPath($process.Path)
    if (-not $processPath.StartsWith($toolsRoot, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to stop PID $($process.Id): executable is outside the monitoring tools directory ($processPath)."
    }

    Stop-Process -Id $process.Id
    Write-Host "Stopped $($entry.Name) (PID $($process.Id))."
}

Remove-Item -LiteralPath $pidFile
