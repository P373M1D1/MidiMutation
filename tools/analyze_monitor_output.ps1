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

function Get-LineFieldMap
{
    param([string]$Line)

    $fieldMap = @{}
    $allMatches = [regex]::Matches($Line, "([A-Za-z0-9_]+)=(-?\d+)")
    foreach ($m in $allMatches)
    {
        $fieldMap[$m.Groups[1].Value] = [long]$m.Groups[2].Value
    }

    return $fieldMap
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
    max_led_beat_on_us = 1000
    max_clock_beat_service_us = 1000
    max_enc2_event_drop_delta = 0
    max_enc2_ui_us = 100000
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
$enc2TurnEvents = @()
$ledSamples = @()
$presetLatEnc2Samples = @()
$clkdiagLines = @()
$lockedSamples = @()

for ($index = 0; $index -lt $allLines.Count; $index++)
{
    $line = $allLines[$index]
    $lineNumber = $index + 1

    if ($line -match "^TBPRESS\s+(ENTER|UPDATE|EXIT)\s+")
    {
        $eventType = $Matches[1]
        $fieldMap = Get-LineFieldMap -Line $line

        $tbEvents += [pscustomobject]@{
            Type = $eventType
            LineNumber = $lineNumber
            Fields = $fieldMap
            RawLine = $line
        }
    }

    if ($line -match "^ENC2(?:TURN|PRESS)\s+(ENTER|UPDATE|EXIT)\s+")
    {
        $eventType = $Matches[1]
        $fieldMap = Get-LineFieldMap -Line $line

        $enc2TurnEvents += [pscustomobject]@{
            Type = $eventType
            LineNumber = $lineNumber
            Fields = $fieldMap
            RawLine = $line
        }
    }

    if ($line -match "^LEDDIAG\s+")
    {
        $ledSamples += [pscustomobject]@{
            LineNumber = $lineNumber
            Fields = Get-LineFieldMap -Line $line
            RawLine = $line
        }
    }

    if ($line -match "^PRESETLAT\s+.*kind=enc2")
    {
        $presetLatEnc2Samples += [pscustomobject]@{
            LineNumber = $lineNumber
            Fields = Get-LineFieldMap -Line $line
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
                beat_service_max_us = Get-LineFieldInt -Line $line -FieldName "beat_service_max_us"
                beat_service_samples = Get-LineFieldInt -Line $line -FieldName "beat_service_samples"
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
$enc2DropDeltaTotal = 0
$enc2MaxDropDelta = 0
$enc2MaxStepsDelta = 0
$enc2MaxActUs = 0
$enc2MaxLedUs = 0
$enc2MaxUiUs = 0
$enc2UiSamples = 0
$enc2LastType = "none"
$enc2OpenWindow = $null
$enc2WindowSizeCurrent = 0
$enc2WindowSizeMax = 0
$enc2Windows = @()
$enc2UnmatchedEnterCount = 0

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

foreach ($event in $enc2TurnEvents)
{
    $fields = $event.Fields
    $stepsDelta = if ($fields.ContainsKey("steps_d")) { [long]$fields["steps_d"] } else { 0 }
    $dropDelta = if ($fields.ContainsKey("drop_d")) { [long]$fields["drop_d"] } else { 0 }
    $actMaxUs = if ($fields.ContainsKey("act_max_us")) { [long]$fields["act_max_us"] } else { 0 }
    $ledMaxUs = if ($fields.ContainsKey("led_max_us")) { [long]$fields["led_max_us"] } else { 0 }
    $uiMaxUs = if ($fields.ContainsKey("ui_max_us")) { [long]$fields["ui_max_us"] } else { 0 }
    $uiSamples = if ($fields.ContainsKey("ui_samp")) { [long]$fields["ui_samp"] } else { 0 }

    if ($stepsDelta -gt $enc2MaxStepsDelta) { $enc2MaxStepsDelta = $stepsDelta }
    if ($dropDelta -gt $enc2MaxDropDelta) { $enc2MaxDropDelta = $dropDelta }
    if ($actMaxUs -gt $enc2MaxActUs) { $enc2MaxActUs = $actMaxUs }
    if ($ledMaxUs -gt $enc2MaxLedUs) { $enc2MaxLedUs = $ledMaxUs }
    if ($uiMaxUs -gt $enc2MaxUiUs) { $enc2MaxUiUs = $uiMaxUs }

    $enc2DropDeltaTotal += $dropDelta
    $enc2UiSamples += $uiSamples
    $enc2LastType = $event.Type

    if ($event.Type -eq "ENTER")
    {
        if ($null -ne $enc2OpenWindow)
        {
            $enc2UnmatchedEnterCount++
        }

        $enc2OpenWindow = [pscustomobject]@{
            enter_line = $event.LineNumber
            enter_steps = $stepsDelta
        }
        $enc2WindowSizeCurrent = 1
    }
    elseif ($event.Type -eq "UPDATE")
    {
        if ($null -ne $enc2OpenWindow)
        {
            $enc2WindowSizeCurrent++
        }
    }
    elseif ($event.Type -eq "EXIT")
    {
        if ($null -ne $enc2OpenWindow)
        {
            $enc2WindowSizeCurrent++
            if ($enc2WindowSizeCurrent -gt $enc2WindowSizeMax)
            {
                $enc2WindowSizeMax = $enc2WindowSizeCurrent
            }

            $enc2Windows += [pscustomobject]@{
                enter_line = $enc2OpenWindow.enter_line
                exit_line = $event.LineNumber
                event_count = $enc2WindowSizeCurrent
            }

            $enc2OpenWindow = $null
            $enc2WindowSizeCurrent = 0
        }
    }
}

if ($null -ne $enc2OpenWindow)
{
    $enc2UnmatchedEnterCount++
    if ($enc2WindowSizeCurrent -gt $enc2WindowSizeMax)
    {
        $enc2WindowSizeMax = $enc2WindowSizeCurrent
    }
}

$ledBeatMaxUs = 0
$ledBeatSamplesTotal = 0
$ledBeatRequestsTotal = 0
$ledBeatDuplicatesTotal = 0
$ledBeatSuppressedTotal = 0

foreach ($sample in $ledSamples)
{
    $fields = $sample.Fields
    $beatMaxUs = if ($fields.ContainsKey("beat_on_max_us")) { [long]$fields["beat_on_max_us"] } else { 0 }
    $beatSamples = if ($fields.ContainsKey("beat_on_samples")) { [long]$fields["beat_on_samples"] } else { 0 }
    $beatReq = if ($fields.ContainsKey("beat_req")) { [long]$fields["beat_req"] } else { 0 }
    $beatDup = if ($fields.ContainsKey("beat_dup")) { [long]$fields["beat_dup"] } else { 0 }
    $beatSupp = if ($fields.ContainsKey("beat_supp")) { [long]$fields["beat_supp"] } else { 0 }

    if ($beatMaxUs -gt $ledBeatMaxUs) { $ledBeatMaxUs = $beatMaxUs }
    $ledBeatSamplesTotal += $beatSamples
    $ledBeatRequestsTotal += $beatReq
    $ledBeatDuplicatesTotal += $beatDup
    $ledBeatSuppressedTotal += $beatSupp
}

foreach ($sample in $presetLatEnc2Samples)
{
    $fields = $sample.Fields
    $actUs = if ($fields.ContainsKey("act_us")) { [long]$fields["act_us"] } else { 0 }
    $ledUs = if ($fields.ContainsKey("led_us")) { [long]$fields["led_us"] } else { 0 }
    $uiUs = if ($fields.ContainsKey("ui_us")) { [long]$fields["ui_us"] } else { 0 }

    if ($actUs -gt $enc2MaxActUs) { $enc2MaxActUs = $actUs }
    if ($ledUs -gt $enc2MaxLedUs) { $enc2MaxLedUs = $ledUs }
    if ($uiUs -gt $enc2MaxUiUs) { $enc2MaxUiUs = $uiUs }
}

$maxLockedPkPk = 0
$lastLockedBacklog = $null
$clkdiagDropMax = 0
$clkdiagMissMax = 0
$clockBeatServiceMaxUs = 0
$clockBeatServiceSamples = 0

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

    if ($null -ne $sample.beat_service_max_us -and $sample.beat_service_max_us -gt $clockBeatServiceMaxUs)
    {
        $clockBeatServiceMaxUs = $sample.beat_service_max_us
    }

    if ($null -ne $sample.beat_service_samples)
    {
        $clockBeatServiceSamples += $sample.beat_service_samples
    }
}

$stressSeen = (($tbMaxBackNow -ge [int]$thresholds.stress_backlog_trigger) `
    -or ($tbMaxJitter -ge [int]$thresholds.stress_jitter_trigger_us) `
    -or ($enc2TurnEvents.Count -gt 0))

$rows = @()

$requireTbpressSignals = [bool]$thresholds.require_tbpress_signals
$enc2MonitorPresent = ($lineCount -gt 0 -and $enc2TurnEvents.Count -gt 0)
if ($requireTbpressSignals -and -not $enc2MonitorPresent)
{
    $volumePass = ($lineCount -gt 0 -and $tbEvents.Count -gt 0 -and $clkdiagLines.Count -gt 0)
    $volumeCheckName = "Monitor volume present (TBPRESS + CLKDIAG)"
}
else
{
    $volumePass = ($lineCount -gt 0 -and $clkdiagLines.Count -gt 0)
    $volumeCheckName = "Monitor volume present (CLKDIAG required, turn stress optional)"
}
$rows += New-Row -Check $volumeCheckName -Result ($(if ($volumePass) { "PASS" } else { "FAIL" })) -Evidence (("lines={0}, TBPRESS={1}, ENC2TURN={2}, CLKDIAG={3}") -f $lineCount, $tbEvents.Count, $enc2TurnEvents.Count, $clkdiagLines.Count)

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

$enc2WindowResult = "WARN"
$enc2WindowEvidence = "No ENC2 turn windows observed"
if ($enc2TurnEvents.Count -gt 0)
{
    $enc2WindowPass = ($enc2UnmatchedEnterCount -eq 0 -and $enc2Windows.Count -gt 0)
    $enc2WindowResult = if ($enc2WindowPass) { "PASS" } else { "FAIL" }
    $enc2WindowEvidence = (("windows={0}, unmatched_enters={1}, last_event={2}, max_steps_d={3}, max_events_while_active={4}") -f $enc2Windows.Count, $enc2UnmatchedEnterCount, $enc2LastType, $enc2MaxStepsDelta, $enc2WindowSizeMax)
}
$rows += New-Row -Check "ENC2 turn preset-change window observed" -Result $enc2WindowResult -Evidence $enc2WindowEvidence

$enc2DropResult = "WARN"
$enc2DropEvidence = "No ENC2 turn samples to score queue drops"
if ($enc2TurnEvents.Count -gt 0)
{
    $enc2DropPass = ($enc2DropDeltaTotal -le [int]$thresholds.max_enc2_event_drop_delta)
    $enc2DropResult = if ($enc2DropPass) { "PASS" } else { "FAIL" }
    $enc2DropEvidence = (("drop_delta_sum={0}, max_drop_delta={1}, threshold={2}") -f $enc2DropDeltaTotal, $enc2MaxDropDelta, [int]$thresholds.max_enc2_event_drop_delta)
}
$rows += New-Row -Check "ENC2 turn event queue drops bounded" -Result $enc2DropResult -Evidence $enc2DropEvidence

$enc2LatencyResult = "WARN"
$enc2LatencyEvidence = "No ENC2 turn preset latency samples captured"
if ($enc2UiSamples -gt 0 -or $presetLatEnc2Samples.Count -gt 0)
{
    $enc2LatencyPass = ($enc2MaxUiUs -le [int]$thresholds.max_enc2_ui_us)
    $enc2LatencyResult = if ($enc2LatencyPass) { "PASS" } else { "FAIL" }
    $enc2LatencyEvidence = (("act_max_us={0}, led_max_us={1}, ui_max_us={2}, ui_threshold={3}, presetlat_samples={4}") -f $enc2MaxActUs, $enc2MaxLedUs, $enc2MaxUiUs, [int]$thresholds.max_enc2_ui_us, $presetLatEnc2Samples.Count)
}
$rows += New-Row -Check "ENC2 turn preset activation metrics captured" -Result $enc2LatencyResult -Evidence $enc2LatencyEvidence

$ledBeatResult = "WARN"
$ledBeatEvidence = "No LEDDIAG beat samples captured"
if ($ledBeatSamplesTotal -gt 0 -or $ledBeatRequestsTotal -gt 0)
{
    $ledBeatPass = ($ledBeatMaxUs -le [int]$thresholds.max_led_beat_on_us)
    $ledBeatResult = if ($ledBeatPass) { "PASS" } else { "FAIL" }
    $ledBeatEvidence = (("beat_on_max_us={0}, threshold={1}, samples={2}, requests={3}, duplicate={4}, suppressed={5}") -f $ledBeatMaxUs, [int]$thresholds.max_led_beat_on_us, $ledBeatSamplesTotal, $ledBeatRequestsTotal, $ledBeatDuplicatesTotal, $ledBeatSuppressedTotal)
}
$rows += New-Row -Check "Beat LED on-edge latency bounded" -Result $ledBeatResult -Evidence $ledBeatEvidence

$clockBeatResult = "WARN"
$clockBeatEvidence = "No LOCKED beat_service samples captured"
if ($clockBeatServiceSamples -gt 0)
{
    $clockBeatPass = ($clockBeatServiceMaxUs -le [int]$thresholds.max_clock_beat_service_us)
    $clockBeatResult = if ($clockBeatPass) { "PASS" } else { "FAIL" }
    $clockBeatEvidence = (("beat_service_max_us={0}, threshold={1}, samples={2}") -f $clockBeatServiceMaxUs, [int]$thresholds.max_clock_beat_service_us, $clockBeatServiceSamples)
}
$rows += New-Row -Check "MIDI beat service latency bounded" -Result $clockBeatResult -Evidence $clockBeatEvidence

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
