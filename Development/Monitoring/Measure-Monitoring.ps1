[CmdletBinding()]
param(
    [string[]]$Targets = @(
        'registry=http://127.0.0.1:19001/metrics',
        'login=http://127.0.0.1:19010/metrics',
        'communication=http://127.0.0.1:19020/metrics',
        'gateway=http://127.0.0.1:19100/metrics',
        'game=http://127.0.0.1:19201/metrics'
    ),
    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 60,
    [ValidateRange(1, 3600)]
    [int]$ScrapeIntervalSeconds = 15,
    [ValidateRange(1, 60000)]
    [double]$MaxScrapeP95Ms = 100,
    [ValidateRange(1, 1000000)]
    [int]$MaxSeriesPerTarget = 10000,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http

function Get-Percentile
{
    param([double[]]$Values, [double]$Percentile)

    if ($Values.Count -eq 0)
    {
        return 0.0
    }

    $sorted = $Values | Sort-Object
    $index = [Math]::Max(0, [Math]::Ceiling($Percentile * $sorted.Count) - 1)
    return [double]$sorted[$index]
}

function Get-ServerProcesses
{
    $names = @('RegistryServer', 'LoginServer', 'CommunicationServer', 'GatewayServer', 'GameServer')
    $snapshots = @{}
    foreach ($process in Get-Process -Name $names -ErrorAction SilentlyContinue)
    {
        $key = "$($process.ProcessName)#$($process.Id)"
        $snapshots[$key] = [PSCustomObject]@{
            name = $process.ProcessName
            pid = $process.Id
            cpu_seconds = [double]$process.CPU
            private_bytes = [int64]$process.PrivateMemorySize64
            working_set_bytes = [int64]$process.WorkingSet64
        }
    }
    return $snapshots
}

$parsedTargets = @()
foreach ($target in $Targets)
{
    $separator = $target.IndexOf('=')
    if ($separator -le 0 -or $separator -eq $target.Length - 1)
    {
        throw "Target must use name=url format: $target"
    }

    $name = $target.Substring(0, $separator).Trim()
    $url = $target.Substring($separator + 1).Trim()
    $uri = $null
    if (-not [Uri]::TryCreate($url, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -ne 'http')
    {
        throw "Target must be an absolute HTTP URL: $target"
    }

    $parsedTargets += [PSCustomObject]@{ name = $name; url = $url }
}

$monitoringRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($OutputPath))
{
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputPath = Join-Path $monitoringRoot "results\monitoring-$timestamp.json"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null

$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(5)

$targetSamples = @{}
foreach ($target in $parsedTargets)
{
    $targetSamples[$target.name] = [PSCustomObject]@{
        target = $target
        durations_ms = [System.Collections.Generic.List[double]]::new()
        response_bytes = [System.Collections.Generic.List[int64]]::new()
        series_counts = [System.Collections.Generic.List[int]]::new()
        family_counts = [System.Collections.Generic.List[int]]::new()
        errors = [System.Collections.Generic.List[string]]::new()
        top_families = @()
    }
}

$startedAt = [DateTimeOffset]::Now
$startProcesses = Get-ServerProcesses
$peakProcesses = @{}
foreach ($entry in $startProcesses.GetEnumerator())
{
    $peakProcesses[$entry.Key] = [PSCustomObject]@{
        private_bytes = $entry.Value.private_bytes
        working_set_bytes = $entry.Value.working_set_bytes
    }
}

$sampleCount = [Math]::Ceiling($DurationSeconds / [double]$ScrapeIntervalSeconds) + 1
$measurementClock = [Diagnostics.Stopwatch]::StartNew()
try
{
    for ($sampleIndex = 0; $sampleIndex -lt $sampleCount; $sampleIndex++)
    {
        $scheduledSeconds = [Math]::Min($DurationSeconds, $sampleIndex * $ScrapeIntervalSeconds)
        $waitMs = ($scheduledSeconds * 1000) - $measurementClock.ElapsedMilliseconds
        if ($waitMs -gt 0)
        {
            Start-Sleep -Milliseconds ([int]$waitMs)
        }

        foreach ($target in $parsedTargets)
        {
            $samples = $targetSamples[$target.name]
            $timer = [Diagnostics.Stopwatch]::StartNew()
            try
            {
                $body = $client.GetStringAsync($target.url).GetAwaiter().GetResult()
                $timer.Stop()

                $series = @($body -split "`n" | Where-Object { $_ -and -not $_.StartsWith('#') })
                $families = @{}
                foreach ($line in $series)
                {
                    $sampleName = ($line -split '\s+', 2)[0]
                    $familyName = ($sampleName -split '\{', 2)[0]
                    $families[$familyName] = 1 + [int]$families[$familyName]
                }

                $samples.durations_ms.Add($timer.Elapsed.TotalMilliseconds)
                $samples.response_bytes.Add([Text.Encoding]::UTF8.GetByteCount($body))
                $samples.series_counts.Add($series.Count)
                $samples.family_counts.Add($families.Count)

                if ($sampleIndex -eq 0)
                {
                    $samples.top_families = @($families.GetEnumerator() |
                        Sort-Object Value -Descending |
                        Select-Object -First 10 |
                        ForEach-Object { [PSCustomObject]@{ name = $_.Key; series = $_.Value } })
                }
            }
            catch
            {
                $timer.Stop()
                $samples.errors.Add($_.Exception.Message)
            }
        }

        foreach ($entry in (Get-ServerProcesses).GetEnumerator())
        {
            if (-not $peakProcesses.ContainsKey($entry.Key))
            {
                $peakProcesses[$entry.Key] = [PSCustomObject]@{ private_bytes = 0L; working_set_bytes = 0L }
            }
            $peakProcesses[$entry.Key].private_bytes = [Math]::Max($peakProcesses[$entry.Key].private_bytes, $entry.Value.private_bytes)
            $peakProcesses[$entry.Key].working_set_bytes = [Math]::Max($peakProcesses[$entry.Key].working_set_bytes, $entry.Value.working_set_bytes)
        }

    }
}
finally
{
    $client.Dispose()
    $handler.Dispose()
}

$endedAt = [DateTimeOffset]::Now
$elapsedSeconds = [Math]::Max(0.001, ($endedAt - $startedAt).TotalSeconds)
$endProcesses = Get-ServerProcesses
$processReports = @()
foreach ($entry in $startProcesses.GetEnumerator())
{
    if (-not $endProcesses.ContainsKey($entry.Key))
    {
        continue
    }

    $end = $endProcesses[$entry.Key]
    $cpuDelta = [Math]::Max(0.0, $end.cpu_seconds - $entry.Value.cpu_seconds)
    $processReports += [PSCustomObject]@{
        name = $entry.Value.name
        pid = $entry.Value.pid
        average_cpu_percent = [Math]::Round(100.0 * $cpuDelta / $elapsedSeconds / [Environment]::ProcessorCount, 3)
        peak_private_bytes = $peakProcesses[$entry.Key].private_bytes
        peak_working_set_bytes = $peakProcesses[$entry.Key].working_set_bytes
    }
}

$targetReports = @()
$passed = $true
foreach ($target in $parsedTargets)
{
    $samples = $targetSamples[$target.name]
    $p95 = Get-Percentile $samples.durations_ms.ToArray() 0.95
    $maxSeries = if ($samples.series_counts.Count) { ($samples.series_counts | Measure-Object -Maximum).Maximum } else { 0 }
    $targetPassed = $samples.errors.Count -eq 0 -and $samples.durations_ms.Count -eq $sampleCount -and $p95 -le $MaxScrapeP95Ms -and $maxSeries -le $MaxSeriesPerTarget
    $passed = $passed -and $targetPassed

    $targetReports += [PSCustomObject]@{
        name = $target.name
        url = $target.url
        passed = $targetPassed
        successful_scrapes = $samples.durations_ms.Count
        failed_scrapes = $samples.errors.Count
        scrape_duration_ms = [PSCustomObject]@{
            average = if ($samples.durations_ms.Count) { [Math]::Round(($samples.durations_ms | Measure-Object -Average).Average, 3) } else { 0 }
            p95 = [Math]::Round($p95, 3)
            maximum = if ($samples.durations_ms.Count) { [Math]::Round(($samples.durations_ms | Measure-Object -Maximum).Maximum, 3) } else { 0 }
        }
        response_bytes_max = if ($samples.response_bytes.Count) { ($samples.response_bytes | Measure-Object -Maximum).Maximum } else { 0 }
        series_max = $maxSeries
        metric_families_max = if ($samples.family_counts.Count) { ($samples.family_counts | Measure-Object -Maximum).Maximum } else { 0 }
        top_families = $samples.top_families
        errors = $samples.errors.ToArray()
    }
}

$report = [PSCustomObject]@{
    schema_version = 1
    passed = $passed
    started_at = $startedAt.ToString('o')
    ended_at = $endedAt.ToString('o')
    requested_duration_seconds = $DurationSeconds
    actual_duration_seconds = [Math]::Round($elapsedSeconds, 3)
    scrape_interval_seconds = $ScrapeIntervalSeconds
    thresholds = [PSCustomObject]@{
        max_scrape_p95_ms = $MaxScrapeP95Ms
        max_series_per_target = $MaxSeriesPerTarget
    }
    host = [PSCustomObject]@{
        machine_name = [Environment]::MachineName
        logical_processors = [Environment]::ProcessorCount
        os_version = [Environment]::OSVersion.VersionString
    }
    targets = $targetReports
    processes = $processReports
}

$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 -LiteralPath $OutputPath
foreach ($target in $targetReports)
{
    Write-Host ("{0}: pass={1}, scrapes={2}, p95={3}ms, max_series={4}, max_bytes={5}" -f
        $target.name, $target.passed, $target.successful_scrapes, $target.scrape_duration_ms.p95, $target.series_max, $target.response_bytes_max)
}
Write-Host "Report: $OutputPath"

if (-not $passed)
{
    exit 1
}
