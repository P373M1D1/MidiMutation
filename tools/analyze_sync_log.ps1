param(
	[string]$LogPath = "logs/serial_latest.log"
)

$ErrorActionPreference = "Stop"

function Get-PatternCount
{
	param(
		[string]$Path,
		[string]$Pattern
	)

	return @(Select-String -Path $Path -Pattern $Pattern -AllMatches).Count
}

function Get-FirstLineNumber
{
	param(
		[string]$Path,
		[string]$Pattern
	)

	$match = Select-String -Path $Path -Pattern $Pattern | Select-Object -First 1
	if ($null -eq $match)
	{
		return "n/a"
	}

	return [string]$match.LineNumber
}

function Get-FieldInt
{
	param(
		[string]$Line,
		[string]$FieldName
	)

	$match = [regex]::Match($Line, ($FieldName + "=(\d+)"))
	if (-not $match.Success)
	{
		return $null
	}

	return [int]$match.Groups[1].Value
}

function New-Row
{
	param(
		[string]$Check,
		[string]$Result,
		[string]$Evidence
	)

	return [pscustomobject]@{
		Check = $Check
		Result = $Result
		Evidence = $Evidence
	}
}

function Escape-Pipe
{
	param([string]$Value)

	if ($null -eq $Value)
	{
		return ""
	}

	return $Value.Replace("|", "/")
}

if (-not (Test-Path $LogPath))
{
	Write-Host ("Log file not found: " + $LogPath)
	exit 1
}

$resolvedLog = (Resolve-Path $LogPath).Path
$allLines = Get-Content -Path $resolvedLog
$lineCount = $allLines.Count

$clkCount = Get-PatternCount -Path $resolvedLog -Pattern "CLKDIAG"
$evtCount = Get-PatternCount -Path $resolvedLog -Pattern "SYNCEVT"
$adaptCount = Get-PatternCount -Path $resolvedLog -Pattern "SYNCADAPT"

$openMarkers = Get-PatternCount -Path $resolvedLog -Pattern "^---- Opened serial port"
$closeMarkers = Get-PatternCount -Path $resolvedLog -Pattern "^---- Closed serial port"

$probFieldLines = Get-PatternCount -Path $resolvedLog -Pattern "adapt_prob_active="
$rollbackFieldLines = Get-PatternCount -Path $resolvedLog -Pattern "SYNCADAPT.*rollback="
$rollbackEq1 = Get-PatternCount -Path $resolvedLog -Pattern "rollback=1"
$probActiveEq1 = Get-PatternCount -Path $resolvedLog -Pattern "adapt_prob_active=1"
$adaptLastRollbackEq1 = Get-PatternCount -Path $resolvedLog -Pattern "adapt_last_rollback=1"

$lockedHoldoverMatches = @(Select-String -Path $resolvedLog -Pattern "sync_state=HOLDOVER.*last_transition=LOCKED->HOLDOVER")
$lockedHoldoverCount = $lockedHoldoverMatches.Count
$lockedHoldoverBad = 0

foreach ($match in $lockedHoldoverMatches)
{
	$lockLost = Get-FieldInt -Line $match.Line -FieldName "adapt_win_lock_lost"
	$holdover = Get-FieldInt -Line $match.Line -FieldName "adapt_win_holdover"
	if ($null -eq $lockLost -or $null -eq $holdover -or $lockLost -le 0 -or $holdover -le 0)
	{
		$lockedHoldoverBad++
	}
}

$rows = @()

$diagPass = ($lineCount -gt 0 -and $clkCount -gt 0 -and $evtCount -gt 0)
$diagResult = "FAIL"
if ($diagPass)
{
	$diagResult = "PASS"
}
$rows += New-Row -Check "Log captured with diagnostic volume" -Result $diagResult -Evidence (("lines={0}, CLKDIAG={1}, SYNCEVT={2}, SYNCADAPT={3}") -f $lineCount, $clkCount, $evtCount, $adaptCount)

$probPass = ($probFieldLines -gt 0 -and $rollbackFieldLines -gt 0)
$probResult = "FAIL"
if ($probPass)
{
	$probResult = "PASS"
}
$rows += New-Row -Check "Probation telemetry fields present" -Result $probResult -Evidence (("adapt_prob_active lines={0}, rollback field lines={1}, prob_active=1 count={2}") -f $probFieldLines, $rollbackFieldLines, $probActiveEq1)

$rollbackPass = ($rollbackEq1 -gt 0)
$rollbackResult = "FAIL"
if ($rollbackPass)
{
	$rollbackResult = "PASS"
}
$rows += New-Row -Check "Auto-rollback path exercised" -Result $rollbackResult -Evidence (("rollback=1 count={0}, adapt_last_rollback=1 count={1}, first rollback line={2}") -f $rollbackEq1, $adaptLastRollbackEq1, (Get-FirstLineNumber -Path $resolvedLog -Pattern "rollback=1"))

$holdoverResult = "WARN"
$holdoverEvidence = "No LOCKED->HOLDOVER CLKDIAG samples in this run"
if ($lockedHoldoverCount -gt 0)
{
	if ($lockedHoldoverBad -eq 0)
	{
		$holdoverResult = "PASS"
	}
	else
	{
		$holdoverResult = "FAIL"
	}

	$holdoverEvidence = (("samples={0}, bad={1}") -f $lockedHoldoverCount, $lockedHoldoverBad)
}
$rows += New-Row -Check "HOLDOVER latch consistency for LOCKED->HOLDOVER" -Result $holdoverResult -Evidence $holdoverEvidence

$closePass = ($openMarkers -ge 1 -and $closeMarkers -ge 1)
$closeResult = "FAIL"
if ($closePass)
{
	$closeResult = "PASS"
}
$rows += New-Row -Check "Closed-marker hygiene (clean end-of-capture)" -Result $closeResult -Evidence (("open markers={0}, close markers={1}, first open line={2}, first close line={3}") -f $openMarkers, $closeMarkers, (Get-FirstLineNumber -Path $resolvedLog -Pattern "^---- Opened serial port"), (Get-FirstLineNumber -Path $resolvedLog -Pattern "^---- Closed serial port"))

$firmwareRows = @($rows | Where-Object { $_.Check -ne "Closed-marker hygiene (clean end-of-capture)" })
$firmwareFails = @($firmwareRows | Where-Object { $_.Result -eq "FAIL" }).Count
$firmwareWarns = @($firmwareRows | Where-Object { $_.Result -eq "WARN" }).Count
$firmwareVerdict = "PASS"
if ($firmwareFails -gt 0)
{
	$firmwareVerdict = "FAIL"
}
elseif ($firmwareWarns -gt 0)
{
	$firmwareVerdict = "PASS (with WARN)"
}

$hygieneVerdict = "FAIL"
if ($closePass)
{
	$hygieneVerdict = "PASS"
}

Write-Output ("Sync Analyzer Report: " + $resolvedLog)
Write-Output ""
Write-Output "| Check | Result | Evidence |"
Write-Output "|---|---|---|"
foreach ($row in $rows)
{
	Write-Output (("| {0} | {1} | {2} |") -f (Escape-Pipe -Value $row.Check), (Escape-Pipe -Value $row.Result), (Escape-Pipe -Value $row.Evidence))
}

Write-Output ""
Write-Output ("Firmware behavior verdict: " + $firmwareVerdict)
Write-Output ("Capture hygiene verdict: " + $hygieneVerdict)
