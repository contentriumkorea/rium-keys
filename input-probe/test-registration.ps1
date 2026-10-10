param([switch]$Trace,[switch]$Interactive,[switch]$Native,[switch]$ReusedEngine,[switch]$TwoProcesses)
$ErrorActionPreference='Stop'
if($TwoProcesses){$ReusedEngine=$true}
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
    $fixturePaths=@($exe)
    if($TwoProcesses){
        # Two identical, independently activated processes; separate directories
        # isolate fixture config and result logs. No user application is copied.
        $secondDirectory=Join-Path $logDirectory 'second-fixture'
        New-Item -ItemType Directory -Force -Path $secondDirectory | Out-Null
        Copy-Item -LiteralPath $exe -Destination (Join-Path $secondDirectory 'RiumImeFixture.exe') -Force
        Copy-Item -LiteralPath (Join-Path $logDirectory 'RiumKeysInput.dll') -Destination (Join-Path $secondDirectory 'RiumKeysInput.dll') -Force
        $fixturePaths+=Join-Path $secondDirectory 'RiumImeFixture.exe'
    }
    if($Trace){$script=Join-Path $PSScriptRoot 'trace-registration.ps1';$registration=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-File',('"'+$script+'"')) -Verb RunAs -WindowStyle Hidden -PassThru}
    else {$registration=Start-Process -FilePath $exe -ArgumentList '--registration-external-test' -Verb RunAs -WindowStyle Hidden -PassThru}
    $fixtures=@()
    try {
        if(!$ready.WaitOne(10000)){throw 'Registration did not become ready.'}
        $fixtureMode=if($Native){'--native-fixture'}elseif($Interactive){'--interactive-fixture'}else{'--registered-fixture'}
        $fixtures=@()
        foreach($fixturePath in $fixturePaths){
            $fixtures+=Start-Process -FilePath $fixturePath -ArgumentList $fixtureMode -WindowStyle Hidden -PassThru
        }
        $deadline=[DateTime]::UtcNow.AddSeconds($(if($ReusedEngine){380}elseif($Interactive){100}else{50}))
        do {
            $pending=@($fixtures | Where-Object { !$_.WaitForExit(50) })
            if($pending.Count -and [DateTime]::UtcNow -ge $deadline){
                throw 'Fixture timed out.'
            }
            if($pending.Count){Start-Sleep -Milliseconds 100}
        }while($pending.Count)
        $passed=$true
        for($i=0;$i -lt $fixtures.Count;$i++){
            "Fixture $($i+1) exit: $($fixtures[$i].ExitCode)"
            Get-Content (Join-Path (Split-Path -Parent $fixturePaths[$i]) 'fixture-result.log')
            if($fixtures[$i].ExitCode -ne 0){$passed=$false}
        }
        if(!$passed){throw 'Real system activation or physical transition failed.'}
    } finally {
        # A failed second launch must not outlive temporary registration.
        $fixtureShutdownErrors=@()
        foreach($ownedFixture in $fixtures){
            try {
                if(!$ownedFixture.WaitForExit(0)){
                    # An exit racing with Stop-Process is harmless only if the
                    # process handle confirms termination before cleanup.
                    try { Stop-Process -InputObject $ownedFixture -ErrorAction Stop } catch { }
                    if(!$ownedFixture.WaitForExit(5000)){throw 'Termination was not confirmed within five seconds.'}
                }
            } catch {
                $fixtureShutdownErrors+="Fixture $($ownedFixture.Id): $($_.Exception.Message)"
            }
        }
        if($fixtureShutdownErrors.Count){
            throw ("Normal registration cleanup was not signaled because an owned fixture may still be running. The registration controller retains its timeout. " + ($fixtureShutdownErrors -join '; '))
        }
        [void]$done.Set()
        if(!$registration.WaitForExit(45000)){throw 'Registration cleanup has not returned.'}
        Get-Content (Join-Path $logDirectory 'registration-result.log')
        if($registration.ExitCode -ne 0){throw 'Registration or cleanup failed.'}
    }
} finally {$ready.Dispose();$done.Dispose()}
