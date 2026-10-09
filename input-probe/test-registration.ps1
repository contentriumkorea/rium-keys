param([switch]$Trace,[switch]$Interactive,[switch]$Native,[switch]$ReusedEngine)
$ErrorActionPreference='Stop'
if($ReusedEngine){$Native=$true}
if($Native){$Interactive=$true}
if($Trace -and $Interactive){throw 'Trace mode is bounded to the non-interactive test.'}
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=[Security.Principal.WindowsPrincipal]::new($identity)
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this script from an ordinary, non-administrator terminal.'}
$readyNew=$false
$doneNew=$false
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,'Local\RIUM.Keys.Probe.Ready',[ref]$readyNew)
$done=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,'Local\RIUM.Keys.Probe.Done',[ref]$doneNew)
try {
    if(!$readyNew -or !$doneNew){throw 'Another registration test is active.'}
    $exe=if($ReusedEngine){Join-Path $PSScriptRoot '..\native-ime\out\x64\RiumImeFixture.exe'}else{Join-Path $PSScriptRoot 'out\x64\RiumContextFixture.exe'}
    $logDirectory=Split-Path -Parent $exe
    if($Trace){$script=Join-Path $PSScriptRoot 'trace-registration.ps1';$registration=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-File',('"'+$script+'"')) -Verb RunAs -WindowStyle Hidden -PassThru}
    else {$registration=Start-Process -FilePath $exe -ArgumentList '--registration-external-test' -Verb RunAs -WindowStyle Hidden -PassThru}
    try {
        if(!$ready.WaitOne(10000)){throw 'Registration did not become ready.'}
        $fixtureMode=if($Native){'--native-fixture'}elseif($Interactive){'--interactive-fixture'}else{'--registered-fixture'}
        $fixture=Start-Process -FilePath $exe -ArgumentList $fixtureMode -WindowStyle Hidden -PassThru
        $deadline=[DateTime]::UtcNow.AddSeconds($(if($ReusedEngine){200}elseif($Interactive){100}else{50}))
        while(!$fixture.WaitForExit(1000)){if([DateTime]::UtcNow -ge $deadline){Stop-Process -Id $fixture.Id;throw 'Fixture timed out.'}}
        "Fixture exit: $($fixture.ExitCode)"
        Get-Content (Join-Path $logDirectory 'fixture-result.log')
        if($fixture.ExitCode -ne 0){throw 'Real system activation failed.'}
    } finally {
        [void]$done.Set()
        if(!$registration.WaitForExit(45000)){throw 'Registration cleanup has not returned.'}
        Get-Content (Join-Path $logDirectory 'registration-result.log')
        if($registration.ExitCode -ne 0){throw 'Registration or cleanup failed.'}
    }
} finally {$ready.Dispose();$done.Dispose()}
