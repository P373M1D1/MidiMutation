param(
    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [string]$LogDir = ".\\logs",
    [switch]$AppendLatest,
    [switch]$SkipAnalysis
)

$scriptPath = Join-Path $PSScriptRoot "tools\\capture_serial_log.ps1"
if (-not (Test-Path $scriptPath))
{
    Write-Error ("Missing script: " + $scriptPath)
    exit 1
}

$invokeArgs = @{
    Port = $Port
    Baud = $Baud
    LogDir = $LogDir
}

if ($AppendLatest)
{
    $invokeArgs.AppendLatest = $true
}

if ($SkipAnalysis)
{
    $invokeArgs.SkipAnalysis = $true
}

& $scriptPath @invokeArgs

if ($LASTEXITCODE -is [int])
{
    exit $LASTEXITCODE
}
