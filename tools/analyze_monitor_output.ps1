param(
    [string]$LogPath = "logs/serial_latest.log",
    [string]$ProfilePath = "tools/monitor_threshold_profile_default.json"
)

$ErrorActionPreference = "Stop"

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

function Resolve-RepoPath
{
    param(
        [string]$PathValue,
        [string]$RepoRoot
    )

    if ([System.IO.Path]::IsPathRooted($PathValue))
    {
        return $PathValue
    }

    return (Join-Path $RepoRoot $PathValue)
}

function Get-LineFieldInt
{
    param(
        [string]$Line,
        [string]$FieldName
    )

    $match = [regex]::Match($Line, ($FieldName + "=(-?\d+)"))
    if (-not $match.Success)
    {
        return $null
    }

    return [int]$match.Groups[1].Value
}

$repoRoot = Split-Path $PSScriptRoot -Parent
$resolvedLogCandidate = Resolve-RepoPath -PathValue $LogPath -RepoRoot $repoRoot
$resolvedProfileCandidate = Resolve-RepoPath -PathValue $ProfilePath -RepoRoot $repoRoot

if (-not (Test-Path $resolvedLogCandidate))
{
    Write-Output ("Monitor analyzer: log not found: " + $resolvedLogCandidate)
    exit 1
}

$thresholds = @{
    min_locked_samples = 3
    max_locked_interval_pkpk_us = 250
    max_locked_backlog_now_final = 1
    require_tbpress_signals = $true
    max_tbpress_events_while_active = 40
    stress_backlog_trigger = 4
    stress_jitter_trigger_us = 700
    require_drop_on_stress = $true
    max_jitter_settle_us = 600
    jitter_exit_decay_ratio = 0.85
    min_tbpress_windows_for_jitter_check = 1
}

if (Test-Path $resolvedProfileCandidate)
{
    $profileObj = Get-Content -Raw -Path $resolvedProfileCandidate | ConvertFrom-Json
    foreach ($property in $profileObj.PSObject.Properties)
    {
        $thresholds[$property.Name] = $property.Value
    }
}

$allLines = Get-Content -Path $resolvedLogCandidate
$lineCount = $allLines.Count

$tbEvents = @()
$clkdiagLines = @()
$lockedSamples = @()

for ($index = 0; $index -lt $allLines.Count; $index++)
{
    $line = $allLines[$index]
    $lineNumber = $index + 1

    if ($line -match "^TBPRESS\s+(ENTER|UPDATE|EXIT)\s+")
    {
        $eventType = $Matches[1]
        $fieldMap = @{}
        $allMatches = [regex]::Matches($line, "([A-Za-z0-9_]+)=(-?\d+)")
        foreach ($m in $allMatches)
        {
            $fieldMap[$m.Groups[1].Value] = [int]$m.Groups[2].Value
        }

        $tbEvents += [pscustomobject]@{
            Type = $eventType
            LineNumber = $lineNumber
            Fields = $fieldMap
            RawLine = $line
        }
    }

    if ($line -match "CLKDIAG")
    {
        $clkdiagLines += [pscustomobject]@{
            LineNumber = $lineNumber
            RawLine = $line
        }

        $syncStateMatch = [regex]::Match($line, "sync_state=([A-Z_]+)")
        if ($syncStateMatch.Success -and $syncStateMatch.Groups[1].Value -eq "LOCKED")
        {
            $lockedSamples += [pscustomobject]@{
                LineNumber = $lineNumber
                interval_pkpk_us = Get-LineFieldInt -Line $line -FieldName "interval_pkpk_us"
                tb_cross_backlog_now = Get-LineFieldInt -Line $line -FieldName "tb_cross_backlog_now"
                tb_drop = Get-LineFieldInt -Line $line -FieldName "tb_drop"
                tb_miss = Get-LineFieldInt -Line $line -FieldName "tb_miss"
            }
        }
    }
}

$tbMaxBackNow = 0
$tbMaxJitter = 0
$tbDropDeltaTotal = 0
$tbMissDeltaTotal = 0
$tbLastBackNow = $null
$tbLastType = "none"
$tbOpenWindow = $null
$tbWindowSizeCurrent = 0
$tbWindowSizeMax = 0
$tbWindows = @()
$tbUnmatchedEnterCount = 0

foreach ($event in $tbEvents)
{
    $fields = $event.Fields
    $backNow = if ($fields.ContainsKey("back_now")) { [int]$fields["back_now"] } else { 0 }
    $jitterEstUs = if ($fields.ContainsKey("jit_est_us")) { [int]$fields["jit_est_us"] } else { 0 }
    $dropDelta = if ($fields.ContainsKey("drop_d")) { [int]$fields["drop_d"] } else { 0 }
    $missDelta = if ($fields.ContainsKey("miss_d")) { [int]$fields["miss_d"] } else { 0 }

    if ($backNow -gt $tbMaxBackNow)
    {
        $tbMaxBackNow = $backNow
    }

    if ($jitterEstUs -gt $tbMaxJitter)
    {
        $tbMaxJitter = $jitterEstUs
    }

    $tbDropDeltaTotal += $dropDelta
    $tbMissDeltaTotal += $missDelta

    $tbLastBackNow = $backNow
    $tbLastType = $event.Type

    if ($event.Type -eq "ENTER")
    {
        if ($null -ne $tbOpenWindow)
        {
            $tbUnmatchedEnterCount++
        }

        $tbOpenWindow = [pscustomobject]@{
            enter_line = $event.LineNumber
            enter_jitter = $jitterEstUs
            enter_back_now = $backNow
        }
        $tbWindowSizeCurrent = 1
    }
    elseif ($event.Type -eq "UPDATE")
    {
        if ($null -ne $tbOpenWindow)
        {
            $tbWindowSizeCurrent++
        }
    }
    elseif ($event.Type -eq "EXIT")
    {
        if ($null -ne $tbOpenWindow)
        {
            $tbWindowSizeCurrent++

            if ($tbWindowSizeCurrent -gt $tbWindowSizeMax)
            {
                $tbWindowSizeMax = $tbWindowSizeCurrent
            }

            $tbWindows += [pscustomobject]@{
                enter_line = $tbOpenWindow.enter_line
                exit_line = $event.LineNumber
                enter_jitter = $tbOpenWindow.enter_jitter
                exit_jitter = $jitterEstUs
                enter_back_now = $tbOpenWindow.enter_back_now
                exit_back_now = $backNow
                event_count = $tbWindowSizeCurrent
            }

            $tbOpenWindow = $null
            $tbWindowSizeCurrent = 0
        }
    }
}

if ($null -ne $tbOpenWindow)
{
    $tbUnmatchedEnterCount++
    if ($tbWindowSizeCurrent -gt $tbWindowSizeMax)
    {
        $tbWindowSizeMax = $tbWindowSizeCurrent
    }
}

$maxLockedPkPk = 0
$lastLockedBacklog = $null
$clkdiagDropMax = 0
$clkdiagMissMax = 0

foreach ($sample in $lockedSamples)
{
    if ($null -ne $sample.interval_pkpk_us -and $sample.interval_pkpk_us -gt $maxLockedPkPk)
    {
        $maxLockedPkPk = $sample.interval_pkpk_us
    }

    if ($null -ne $sample.tb_cross_backlog_now)
    {
        $lastLockedBacklog = $sample.tb_cross_backlog_now
    }

    if ($null -ne $sample.tb_drop -and $sample.tb_drop -gt $clkdiagDropMax)
    {
        $clkdiagDropMax = $sample.tb_drop
    }

    if ($null -ne $sample.tb_miss -and $sample.tb_miss -gt $clkdiagMissMax)
    {
        $clkdiagMissMax = $sample.tb_miss
    }
}

$stressSeen = (($tbMaxBackNow -ge [int]$thresholds.stress_backlog_trigger) `
    -or ($tbMaxJitter -ge [int]$thresholds.stress_jitter_trigger_us))

$rows = @()

$requireTbpressSignals = [bool]$thresholds.require_tbpress_signals
if ($requireTbpressSignals)
{
    $volumePass = ($lineCount -gt 0 -and $tbEvents.Count -gt 0 -and $clkdiagLines.Count -gt 0)
    $volumeCheckName = "Monitor volume present (TBPRESS + CLKDIAG)"
}
else
{
    $volumePass = ($lineCount -gt 0 -and $clkdiagLines.Count -gt 0)
    $volumeCheckName = "Monitor volume present (CLKDIAG required, TBPRESS optional)"
}
$rows += New-Row -Check $volumeCheckName -Result ($(if ($volumePass) { "PASS" } else { "FAIL" })) -Evidence (("lines={0}, TBPRESS={1}, CLKDIAG={2}") -f $lineCount, $tbEvents.Count, $clkdiagLines.Count)

$lockedCoveragePass = ($lockedSamples.Count -ge [int]$thresholds.min_locked_samples)
$rows += New-Row -Check "LOCKED diagnostic coverage" -Result ($(if ($lockedCoveragePass) { "PASS" } else { "FAIL" })) -Evidence (("locked_samples={0}, min_required={1}") -f $lockedSamples.Count, [int]$thresholds.min_locked_samples)

$intervalResult = "WARN"
$intervalEvidence = "No LOCKED samples to score interval_pkpk_us"
if ($lockedSamples.Count -gt 0)
{
    $intervalResult = if ($maxLockedPkPk -le [int]$thresholds.max_locked_interval_pkpk_us) { "PASS" } else { "FAIL" }
    $intervalEvidence = (("max_interval_pkpk_us={0}, threshold={1}") -f $maxLockedPkPk, [int]$thresholds.max_locked_interval_pkpk_us)
}
$rows += New-Row -Check "LOCKED interval spread bounded" -Result $intervalResult -Evidence $intervalEvidence

$pairingResult = "WARN"
$pairingEvidence = "No TBPRESS windows observed"
if ($tbEvents.Count -gt 0)
{
    $pairingResult = if ($tbUnmatchedEnterCount -eq 0) { "PASS" } else { "FAIL" }
    $pairingEvidence = (("windows={0}, unmatched_enters={1}, last_event={2}") -f $tbWindows.Count, $tbUnmatchedEnterCount, $tbLastType)
}
$rows += New-Row -Check "TBPRESS enter/exit pairing" -Result $pairingResult -Evidence $pairingEvidence

$activeWindowResult = "WARN"
$activeWindowEvidence = "No complete TBPRESS windows"
if ($tbWindows.Count -gt 0)
{
    $activeWindowResult = if ($tbWindowSizeMax -le [int]$thresholds.max_tbpress_events_while_active) { "PASS" } else { "FAIL" }
    $activeWindowEvidence = (("max_events_while_active={0}, threshold={1}") -f $tbWindowSizeMax, [int]$thresholds.max_tbpress_events_while_active)
}
$rows += New-Row -Check "TBPRESS active window bounded" -Result $activeWindowResult -Evidence $activeWindowEvidence

$recoveryResult = "WARN"
$recoveryEvidence = "No final backlog samples found"
if ($null -ne $tbLastBackNow -or $null -ne $lastLockedBacklog)
{
    $tbBack = if ($null -eq $tbLastBackNow) { -1 } else { $tbLastBackNow }
    $clkBack = if ($null -eq $lastLockedBacklog) { -1 } else { $lastLockedBacklog }
    $recoveryPass = (($tbBack -le [int]$thresholds.max_locked_backlog_now_final -or $tbBack -lt 0) `
        -and ($clkBack -le [int]$thresholds.max_locked_backlog_now_final -or $clkBack -lt 0))
    $recoveryResult = if ($recoveryPass) { "PASS" } else { "FAIL" }
    $recoveryEvidence = (("final_tb_back_now={0}, final_clkdiag_back_now={1}, threshold={2}") -f $tbBack, $clkBack, [int]$thresholds.max_locked_backlog_now_final)
}
$rows += New-Row -Check "Backlog recovers to idle" -Result $recoveryResult -Evidence $recoveryEvidence

$dropBehaviorResult = "WARN"
$dropBehaviorEvidence = "No stress condition observed"
if ($stressSeen)
{
    $dropObserved = (($tbDropDeltaTotal + $tbMissDeltaTotal) -gt 0 -or $clkdiagDropMax -gt 0 -or $clkdiagMissMax -gt 0)
    if ([bool]$thresholds.require_drop_on_stress)
    {
        $dropBehaviorResult = if ($dropObserved) { "PASS" } else { "FAIL" }
    }
    else
    {
        $dropBehaviorResult = "PASS"
    }
    $dropBehaviorEvidence = (("stress=1, tb_drop_delta_sum={0}, tb_miss_delta_sum={1}, clkdiag_tb_drop_max={2}, clkdiag_tb_miss_max={3}") -f $tbDropDeltaTotal, $tbMissDeltaTotal, $clkdiagDropMax, $clkdiagMissMax)
}
$rows += New-Row -Check "Strict-drop response under stress" -Result $dropBehaviorResult -Evidence $dropBehaviorEvidence

$jitterResult = "WARN"
$jitterEvidence = "No complete TBPRESS windows for jitter settle check"
if ($tbWindows.Count -ge [int]$thresholds.min_tbpress_windows_for_jitter_check)
{
    $lastWindow = $tbWindows[$tbWindows.Count - 1]
    $maxSettle = [int]$thresholds.max_jitter_settle_us
    $decayRatio = [double]$thresholds.jitter_exit_decay_ratio
    $decayLimit = [math]::Floor([double]$lastWindow.enter_jitter * $decayRatio)
    $windowPass = (($lastWindow.exit_jitter -le $maxSettle) -and ($lastWindow.exit_jitter -le $decayLimit))
    $jitterResult = if ($windowPass) { "PASS" } else { "FAIL" }
    $jitterEvidence = (("enter_jit={0}, exit_jit={1}, settle_threshold={2}, decay_limit={3}") -f $lastWindow.enter_jitter, $lastWindow.exit_jitter, $maxSettle, $decayLimit)
}
$rows += New-Row -Check "Scheduling jitter settles after stress" -Result $jitterResult -Evidence $jitterEvidence

$failCount = @($rows | Where-Object { $_.Result -eq "FAIL" }).Count
$warnCount = @($rows | Where-Object { $_.Result -eq "WARN" }).Count
$verdict = "PASS"
if ($failCount -gt 0)
{
    $verdict = "FAIL"
}
elseif ($warnCount -gt 0)
{
    $verdict = "PASS (with WARN)"
}

if (Test-Path $resolvedProfileCandidate)
{
    $thresholdProfileText = (Resolve-Path $resolvedProfileCandidate).Path
}
else
{
    $thresholdProfileText = "built-in defaults"
}

if ($stressSeen)
{
    $stressObservedText = "yes"
}
else
{
    $stressObservedText = "no"
}

Write-Output ("Monitor Analyzer Report: " + (Resolve-Path $resolvedLogCandidate).Path)
Write-Output ("Threshold profile: " + $thresholdProfileText)
Write-Output ""
Write-Output "| Check | Result | Evidence |"
Write-Output "|---|---|---|"
foreach ($row in $rows)
{
    Write-Output (("| {0} | {1} | {2} |") -f (Escape-Pipe -Value $row.Check), (Escape-Pipe -Value $row.Result), (Escape-Pipe -Value $row.Evidence))
}
Write-Output ""
Write-Output ("Stress observed: " + $stressObservedText)
Write-Output ("Overall verdict: " + $verdict)

if ($failCount -gt 0)
{
    exit 2
}

exit 0
