param(
    [string]$Port = "/dev/ttyACM0",
    [int]$Baud = 115200,
    [string]$LogDir = "./logs",
    [string]$LogPath = "",
    [string]$ProfilePath = "./tools/monitor_threshold_profile_default.json",
    [switch]$UseStrictProfile,
    [switch]$AnalyzeOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = $PSScriptRoot
$captureScript = Join-Path $repoRoot "capture_serial_log.ps1"
$analyzeScript = Join-Path $repoRoot "analyze_sync_log.ps1"
$strictProfilePath = Join-Path $repoRoot "tools/monitor_threshold_profile_strict.json"
$defaultProfilePath = Join-Path $repoRoot "tools/monitor_threshold_profile_default.json"

if (-not (Test-Path $captureScript))
{
    Write-Error ("Missing script: " + $captureScript)
    exit 1
}

if (-not (Test-Path $analyzeScript))
{
    Write-Error ("Missing script: " + $analyzeScript)
    exit 1
}

if ($UseStrictProfile)
{
    $effectiveProfilePath = $strictProfilePath
}
else
{
    if ([System.IO.Path]::IsPathRooted($ProfilePath))
    {
        $effectiveProfilePath = $ProfilePath
    }
    else
    {
        $effectiveProfilePath = Join-Path $repoRoot $ProfilePath
    }
}

if (-not (Test-Path $effectiveProfilePath))
{
    Write-Error ("Missing profile: " + $effectiveProfilePath)
    exit 1
}

if ($LogPath -eq "")
{
    if ([System.IO.Path]::IsPathRooted($LogDir))
    {
        $effectiveLogPath = Join-Path $LogDir "serial_latest.log"
    }
    else
    {
        $effectiveLogPath = Join-Path (Join-Path $repoRoot $LogDir) "serial_latest.log"
    }
}
else
{
    if ([System.IO.Path]::IsPathRooted($LogPath))
    {
        $effectiveLogPath = $LogPath
    }
    else
    {
        $effectiveLogPath = Join-Path $repoRoot $LogPath
    }
}

if (-not $AnalyzeOnly)
{
    Write-Host "Starting capture. Press Ctrl+C to stop and continue to analysis..."
    & $captureScript -Port $Port -Baud $Baud -LogDir $LogDir -SkipAnalysis
    if ($LASTEXITCODE -is [int] -and $LASTEXITCODE -ne 0)
    {
        Write-Error ("Capture failed with exit code: " + $LASTEXITCODE)
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path $effectiveLogPath))
{
    Write-Error ("Log file not found for analysis: " + $effectiveLogPath)
    exit 1
}

Write-Host ""
Write-Host ("Analyzing log: " + $effectiveLogPath)
Write-Host ("Using profile: " + $effectiveProfilePath)

& $analyzeScript -LogPath $effectiveLogPath -ProfilePath $effectiveProfilePath

if ($LASTEXITCODE -is [int])
{
    exit $LASTEXITCODE
}
