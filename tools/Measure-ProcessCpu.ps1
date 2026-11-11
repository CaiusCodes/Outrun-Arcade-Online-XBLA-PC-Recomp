[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$WaitSeconds=43200,
    [int]$MaxSeconds=600
)
# Read-only observer. Does not launch, activate, suspend or send input to a game.
$ErrorActionPreference='Stop'
$targetExe=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Executable)
$outputPath=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if(-not (Test-Path -LiteralPath $targetExe -PathType Leaf)){throw 'Target executable does not exist.'}
$processName=[IO.Path]::GetFileNameWithoutExtension($targetExe)
if(Test-Path -LiteralPath $outputPath){throw 'Measurement exists; choose a new output.'}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OutrunForegroundQuery {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
}
'@
$writer=[IO.StreamWriter]::new($outputPath,$false)
$writer.AutoFlush=$true
$observer=[Diagnostics.Process]::GetCurrentProcess()
$observerCpu=$observer.TotalProcessorTime.TotalMilliseconds
try {
    $writer.WriteLine((@{kind='waiting';utc=[DateTime]::UtcNow.ToString('o');exe=$targetExe} | ConvertTo-Json -Compress))
    $deadline=[DateTime]::UtcNow.AddSeconds($WaitSeconds)
    $targetProcess=$null
    $reportedUnreadable=@{}
    while([DateTime]::UtcNow -lt $deadline){
        $candidates=@()
        foreach($candidate in @(Get-Process -Name $processName -ErrorAction SilentlyContinue)){
            try { $candidatePath=$candidate.Path } catch { $candidatePath=$null }
            if($candidatePath -eq $targetExe){$candidates+=$candidate}
            elseif(-not $candidatePath -and -not $reportedUnreadable.ContainsKey($candidate.Id)){
                $reportedUnreadable[$candidate.Id]=$true
                $writer.WriteLine((@{kind='unreadable_process';utc=[DateTime]::UtcNow.ToString('o');pid=$candidate.Id;reason='Cannot read executable path; no measurement taken'} | ConvertTo-Json -Compress))
            }
        }
        if($candidates.Count -gt 1){throw 'More than one matching game process.'}
        if($candidates.Count -eq 1){$targetProcess=$candidates[0];break}
        Start-Sleep -Milliseconds 1000
    }
    if(-not $targetProcess){
        $writer.WriteLine((@{kind='timeout';utc=[DateTime]::UtcNow.ToString('o');wait_seconds=$WaitSeconds} | ConvertTo-Json -Compress))
        throw 'Timed out waiting for the user to launch the separate test copy.'
    }
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $old=@{}
    $lastMs=0.0
    $oldProcessCpu=$null
    $writer.WriteLine((@{kind='start';utc=[DateTime]::UtcNow.ToString('o');pid=$targetProcess.Id;logical_processors=[Environment]::ProcessorCount;cpu_units='percent of one logical CPU; process may exceed 100';exe=$targetExe} | ConvertTo-Json -Compress))
    while($clock.Elapsed.TotalSeconds -lt $MaxSeconds){
        $targetProcess.Refresh()
        if($targetProcess.HasExited){break}
        $elapsed=$clock.Elapsed.TotalMilliseconds
        $interval=$elapsed-$lastMs
        [uint32]$foregroundPid=0
        [void][OutrunForegroundQuery]::GetWindowThreadProcessId([OutrunForegroundQuery]::GetForegroundWindow(),[ref]$foregroundPid)
        $threads=@()
        foreach($thread in $targetProcess.Threads){
            try {
                $cpu=$thread.TotalProcessorTime.TotalMilliseconds
                if($old.ContainsKey($thread.Id) -and $interval -gt 0){
                    $threads+=@{tid=$thread.Id;cpu_percent=100*($cpu-$old[$thread.Id])/$interval}
                }
                $old[$thread.Id]=$cpu
            } catch { } # A thread may end between enumeration and query.
        }
        try { $total=$targetProcess.TotalProcessorTime.TotalMilliseconds } catch { break }
        if($null -ne $oldProcessCpu -and $interval -gt 0){
            $writer.WriteLine((@{kind='sample';utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$elapsed;interval_ms=$interval;focused=($foregroundPid -eq $targetProcess.Id);process_cpu_percent=100*($total-$oldProcessCpu)/$interval;threads=$threads} | ConvertTo-Json -Compress -Depth 5))
        }
        $oldProcessCpu=$total
        $lastMs=$elapsed
        Start-Sleep -Milliseconds 1000
    }
    $observer.Refresh()
    $writer.WriteLine((@{kind='end';utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$clock.Elapsed.TotalMilliseconds;observer_cpu_ms=$observer.TotalProcessorTime.TotalMilliseconds-$observerCpu} | ConvertTo-Json -Compress))
    Write-Output "CPU observation saved to $outputPath"
} finally { $writer.Dispose() }
