[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1의 Start-Process는 Path와 PATH가 함께 있으면 실패한다.
# 현재 process의 값을 하나의 이름으로 다시 만들어 중복만 제거한다.
$processPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $processPath, 'Process')

$monitoringRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolsRoot = Join-Path $monitoringRoot '.tools'
$dataRoot = Join-Path $monitoringRoot 'data'
$logsRoot = Join-Path $dataRoot 'logs'
$pidFile = Join-Path $dataRoot 'monitoring-pids.json'

if ([string]::IsNullOrWhiteSpace($env:MMO_GRAFANA_ADMIN_PASSWORD))
{
    throw 'Set MMO_GRAFANA_ADMIN_PASSWORD before starting monitoring.'
}

if (Test-Path $pidFile)
{
    throw "PID file already exists. Run Stop-LocalMonitoring.ps1 first: $pidFile"
}

$prometheusHome = Join-Path $toolsRoot 'prometheus-3.12.0.windows-amd64'
$grafanaHome = Join-Path $toolsRoot 'grafana-13.1.0'
$prometheusExe = Join-Path $prometheusHome 'prometheus.exe'
$grafanaExe = Join-Path $grafanaHome 'bin\grafana.exe'

foreach ($requiredFile in @($prometheusExe, $grafanaExe))
{
    if (-not (Test-Path $requiredFile))
    {
        throw "Missing monitoring executable. Run Install-LocalMonitoring.ps1 first: $requiredFile"
    }
}

New-Item -ItemType Directory -Force -Path $dataRoot, $logsRoot | Out-Null

$processes = @()
try
{
    $prometheus = Start-Process -FilePath $prometheusExe -WorkingDirectory $prometheusHome -WindowStyle Hidden -PassThru `
        -ArgumentList @(
            "--config.file=$(Join-Path $monitoringRoot 'prometheus\prometheus.yml')",
            "--storage.tsdb.path=$(Join-Path $dataRoot 'prometheus')",
            '--storage.tsdb.retention.time=15d',
            '--web.listen-address=127.0.0.1:9090',
            '--web.enable-lifecycle'
        ) `
        -RedirectStandardOutput (Join-Path $logsRoot 'prometheus.stdout.log') `
        -RedirectStandardError (Join-Path $logsRoot 'prometheus.stderr.log')
    $processes += $prometheus

    $env:GF_PATHS_PROVISIONING = Join-Path $monitoringRoot 'grafana\provisioning'
    $env:GF_PATHS_DATA = Join-Path $dataRoot 'grafana'
    $env:GF_PATHS_LOGS = Join-Path $logsRoot 'grafana'
    $env:GF_SECURITY_ADMIN_PASSWORD = $env:MMO_GRAFANA_ADMIN_PASSWORD
    $env:MMO_GRAFANA_DASHBOARDS = Join-Path $monitoringRoot 'grafana\dashboards'
    $env:GF_ANALYTICS_CHECK_FOR_UPDATES = 'false'
    $env:GF_ANALYTICS_CHECK_FOR_PLUGIN_UPDATES = 'false'
    $env:GF_PLUGINS_PREINSTALL_DISABLED = 'true'
    # bundled Zipkin backend가 RegistryServer 내부 통신 port 10001을 선점하지 않도록 사용하지 않는 plugin을 비활성화한다.
    $env:GF_PLUGINS_DISABLE_PLUGINS = 'zipkin'
    $env:GF_SERVER_HTTP_ADDR = '127.0.0.1'
    New-Item -ItemType Directory -Force -Path $env:GF_PATHS_DATA, $env:GF_PATHS_LOGS | Out-Null

    $grafana = Start-Process -FilePath $grafanaExe -WorkingDirectory $grafanaHome -WindowStyle Hidden -PassThru `
        -ArgumentList @('server', '--homepath', $grafanaHome)
    $processes += $grafana

    [PSCustomObject]@{
        prometheus = $prometheus.Id
        grafana = $grafana.Id
    } | ConvertTo-Json | Set-Content -Encoding utf8 $pidFile

    Start-Sleep -Seconds 3
    foreach ($process in $processes)
    {
        if ($process.HasExited)
        {
            throw "$($process.ProcessName) exited during startup with code $($process.ExitCode). Check $logsRoot."
        }
    }
}
catch
{
    foreach ($process in $processes)
    {
        if (-not $process.HasExited)
        {
            Stop-Process -Id $process.Id
        }
    }
    Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue
    throw
}

Write-Host 'Prometheus:   http://127.0.0.1:9090'
Write-Host 'Grafana:      http://127.0.0.1:3000'
