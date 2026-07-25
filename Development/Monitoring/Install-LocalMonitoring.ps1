[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$monitoringRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolsRoot = Join-Path $monitoringRoot '.tools'
New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null

$packages = @(
    @{
        Name = 'Prometheus'
        Version = '3.12.0'
        Url = 'https://github.com/prometheus/prometheus/releases/download/v3.12.0/prometheus-3.12.0.windows-amd64.zip'
        Archive = 'prometheus-3.12.0.windows-amd64.zip'
        Directory = 'prometheus-3.12.0.windows-amd64'
    },
    @{
        Name = 'Grafana'
        Version = '13.1.0'
        Url = 'https://dl.grafana.com/grafana/release/13.1.0/grafana_13.1.0_28013217238_windows_amd64.tar.gz'
        Archive = 'grafana_13.1.0_28013217238_windows_amd64.tar.gz'
        Directory = 'grafana-13.1.0'
        Sha256 = 'cd103b83322976487c8424c49d4ea11a07b32c5e549ed2071b31848781492af4'
    }
)

foreach ($package in $packages)
{
    $destination = Join-Path $toolsRoot $package.Directory
    if (Test-Path $destination)
    {
        Write-Host "$($package.Name) $($package.Version) already exists."
        continue
    }

    $archivePath = Join-Path $toolsRoot $package.Archive
    Write-Host "Downloading $($package.Name) $($package.Version)..."
    Invoke-WebRequest -UseBasicParsing -Uri $package.Url -OutFile $archivePath

    if ($package.Sha256)
    {
        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash
        if ($actualHash -ne $package.Sha256)
        {
            throw "$($package.Name) SHA-256 mismatch: $actualHash"
        }
    }

    if ($archivePath.EndsWith('.tar.gz'))
    {
        & tar.exe -xzf $archivePath -C $toolsRoot
        if ($LASTEXITCODE -ne 0)
        {
            throw "$($package.Name) archive extraction failed with exit code $LASTEXITCODE."
        }
    }
    else
    {
        Expand-Archive -LiteralPath $archivePath -DestinationPath $toolsRoot
    }
    Remove-Item -LiteralPath $archivePath

    if (-not (Test-Path $destination))
    {
        throw "$($package.Name) archive did not create expected directory: $destination"
    }
}

Write-Host ''
Write-Host 'Local monitoring tools downloaded.'
Write-Host 'Run Start-LocalMonitoring.ps1 after setting MMO_GRAFANA_ADMIN_PASSWORD.'
