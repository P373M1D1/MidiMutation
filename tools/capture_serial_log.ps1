<#
Capture serial log to timestamped file and "latest" file.
Start by running "powershell -ExecutionPolicy Bypass -File capture_serial_log.ps1" from an admin command prompt, or run the script directly from PowerShell. Use Ctrl+C to stop capture. You can specify the COM port, baud rate, log directory, whether to append to the latest log, and whether to skip the automatic analyzer report. For example:
.\capture_serial_log.ps1 -Port COM3 -Baud 9600 -LogDir "C:\SerialLogs" -AppendLatest
#>
param(
    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [string]$LogDir = "logs",
    [switch]$AppendLatest,
    [switch]$SkipAnalysis
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
$resolvedLogDir = $LogDir
if (-not [System.IO.Path]::IsPathRooted($resolvedLogDir))
{
    $resolvedLogDir = Join-Path $repoRoot $resolvedLogDir
}

New-Item -ItemType Directory -Path $resolvedLogDir -Force | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safePortLabel = ($Port -replace '[^A-Za-z0-9._-]', '_')
$sessionLog = Join-Path $resolvedLogDir ("serial_" + $safePortLabel + "_" + $timestamp + ".log")
$latestLog = Join-Path $resolvedLogDir "serial_latest.log"
$analyzerScript = Join-Path $PSScriptRoot "analyze_sync_log.ps1"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

if (-not $AppendLatest)
{
    [System.IO.File]::WriteAllText($latestLog, "", $utf8NoBom)
}

function Write-SerialLogLine
{
    param([string]$Line)

    Write-Host $Line
    [System.IO.File]::AppendAllText($sessionLog, $Line + [Environment]::NewLine, $utf8NoBom)
    [System.IO.File]::AppendAllText($latestLog, $Line + [Environment]::NewLine, $utf8NoBom)
}

$serialPort = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$serialPort.Handshake = [System.IO.Ports.Handshake]::None
$serialPort.NewLine = "`r`n"
$serialPort.ReadTimeout = 250

try
{
    $serialPort.Open()
    Write-SerialLogLine ("---- Opened serial port " + $Port + " at " + $Baud + " baud ----")
    Write-SerialLogLine ("---- Session log: " + $sessionLog + " ----")
    Write-SerialLogLine "---- Press Ctrl+C to stop capture ----"

    while ($true)
    {
        try
        {
            $line = $serialPort.ReadLine()
            if ($null -ne $line)
            {
                Write-SerialLogLine $line
            }
        }
        catch [System.TimeoutException]
        {
            # Keep looping until Ctrl+C.
        }
    }
}
finally
{
    if ($serialPort.IsOpen)
    {
        $serialPort.Close()
    }

    Write-SerialLogLine ("---- Closed serial port " + $Port + " ----")
    Write-Host ("Session log saved: " + $sessionLog)
    Write-Host ("Latest log saved: " + $latestLog)

    if (-not $SkipAnalysis)
    {
        if (Test-Path $analyzerScript)
        {
            Write-Host ""
            Write-Host "Running sync analyzer report..."
            & $analyzerScript -LogPath $sessionLog
            if ($LASTEXITCODE -ne 0)
            {
                Write-Host ("Sync analyzer returned exit code: " + $LASTEXITCODE)
            }
        }
        else
        {
            Write-Host ("Sync analyzer not found: " + $analyzerScript)
        }
    }
}