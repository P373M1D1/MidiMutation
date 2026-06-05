param(
    [string]$LogPath = "./logs/serial_latest.log",
    [string]$ProfilePath = "",
    [switch]$UseLegacyAnalyzer
)

function Resolve-AnalyzerLogPath
{
    param(
        [string]$InputPath,
        [string]$RepoRoot
    )

    if ([System.IO.Path]::IsPathRooted($InputPath))
    {
        return $InputPath
    }

    if (Test-Path $InputPath)
    {
        return (Resolve-Path $InputPath).Path
    }

    $repoRelativePath = Join-Path $RepoRoot $InputPath
    if (Test-Path $repoRelativePath)
    {
        return (Resolve-Path $repoRelativePath).Path
    }

    if ($InputPath -notmatch '[\\/]')
    {
        $logsPath = Join-Path (Join-Path $RepoRoot "logs") $InputPath
        if (Test-Path $logsPath)
        {
            return (Resolve-Path $logsPath).Path
        }
    }

    return $repoRelativePath
}

$legacyScriptPath = Join-Path (Join-Path $PSScriptRoot "tools") "analyze_sync_log.ps1"
$monitorScriptPath = Join-Path (Join-Path $PSScriptRoot "tools") "analyze_monitor_output.ps1"

$invokePath = $legacyScriptPath
$resolvedLogPath = Resolve-AnalyzerLogPath -InputPath $LogPath -RepoRoot $PSScriptRoot
$invokeArgs = @{
    LogPath = $resolvedLogPath
}

if (-not $UseLegacyAnalyzer -and ($ProfilePath -ne ""))
{
    $invokePath = $monitorScriptPath
    $invokeArgs.ProfilePath = $ProfilePath
}

if (-not (Test-Path $invokePath))
{
    Write-Error ("Missing script: " + $invokePath)
    exit 1
}

& $invokePath @invokeArgs

if ($LASTEXITCODE -is [int])
{
    exit $LASTEXITCODE
}
