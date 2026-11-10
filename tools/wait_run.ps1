# Waits for a walker run started by tools/train_walker.py without polling its log in a tight loop:
#
#     powershell -File tools/wait_run.ps1 -Run RUN_ID [-Results build\runs] [-UntilStep 10000000]
#
# Returns when the launcher has exited (the run ended or failed), or, with -UntilStep, when the trainer's last
# summary is at that step or beyond. Prints the last summaries, the checkpoints and the tail of the console.
param(
    [Parameter(Mandatory = $true)][string]$Run,
    [string]$Results = "build\runs",
    [long]$UntilStep = 0,
    [int]$Every = 60
)

$console = Join-Path $Results "$Run.console.txt"
$pidFile = Join-Path $Results "$Run.pid"

function LastStep {
    if (-not (Test-Path $console)) { return 0 }
    $line = Select-String -Path $console -Pattern 'Step: (\d+)\.' | Select-Object -Last 1
    if ($line) { return [long]$line.Matches[0].Groups[1].Value }
    return 0
}

function Alive {
    # The launcher writes "mlagents-learn exited" when the trainer is gone
    if (-not (Test-Path $console)) { return $true }
    return -not (Select-String -Path $console -Pattern 'mlagents-learn exited with' -Quiet)
}

$started = Get-Date
while ((Alive) -and ($UntilStep -le 0 -or (LastStep) -lt $UntilStep)) {
    Start-Sleep -Seconds $Every
    # No console after ten minutes: the launcher never started
    if (-not (Test-Path $console) -and ((Get-Date) - $started).TotalMinutes -gt 10) { break }
}

"{0}: step {1}, {2}" -f $Run, (LastStep), $(if (Alive) { "running" } else { "ended" })
if (Test-Path $console) {
    Select-String -Path $console -Pattern 'Step: ' | Select-Object -Last 4 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
    Get-Content $console -Tail 3 | ForEach-Object { $_.Substring(0, [Math]::Min(300, $_.Length)) }
}
$checkpoints = Join-Path $Results "$Run\HeidelBotBehavior"
if (Test-Path $checkpoints) { (Get-ChildItem $checkpoints -Filter *.onnx | Sort-Object LastWriteTime | ForEach-Object Name) -join " " }
