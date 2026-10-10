$ErrorActionPreference='Stop'
if(Get-Process Procmon,Procmon64 -ErrorAction SilentlyContinue){throw 'An existing Process Monitor session is running.'}
$procmon=Join-Path $PSScriptRoot 'tools\procmon\Procmon64.exe'
$output=Join-Path $PSScriptRoot 'out\x64'
$capture=Join-Path $output 'registration-trace.pml'
$csv=Join-Path $output 'registration-trace.csv'
$signature=Get-AuthenticodeSignature -LiteralPath $procmon
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*Microsoft Corporation*'){throw 'Expected Microsoft-signed Process Monitor.'}
$monitor=Start-Process -FilePath $procmon -ArgumentList @('/AcceptEula','/Quiet','/Minimized','/BackingFile',('"'+$capture+'"'),'/Runtime','15') -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 1200
    $registration=Start-Process -FilePath (Join-Path $output 'RiumContextFixture.exe') -ArgumentList '--registration-external-test' -WindowStyle Hidden -PassThru
    if(!$registration.WaitForExit(40000)){throw 'Registration holder did not exit.'}
} finally {
    $stop=Start-Process -FilePath $procmon -ArgumentList '/Terminate','/Quiet' -WindowStyle Hidden -PassThru
    [void]$stop.WaitForExit(10000)
    [void]$monitor.WaitForExit(10000)
}
$export=Start-Process -FilePath $procmon -ArgumentList @('/AcceptEula','/Quiet','/OpenLog',('"'+$capture+'"'),'/SaveAs',('"'+$csv+'"')) -WindowStyle Hidden -PassThru
if(!$export.WaitForExit(30000)){throw 'Trace export did not complete.'}
if($registration.ExitCode -ne 0){exit $registration.ExitCode}
