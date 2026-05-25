param(
    [string]$LogPath = ".\\logs\\serial_latest.log"
)

$scriptPath = Join-Path $PSScriptRoot "tools\\analyze_sync_log.ps1"
if (-not (Test-Path $scriptPath))
{
    Write-Error ("Missing script: " + $scriptPath)
    exit 1
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath -LogPath $LogPath
exit $LASTEXITCODE
