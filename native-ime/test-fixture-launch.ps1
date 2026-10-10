param([ValidateSet('x64','x86')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this regression test from an ordinary user session.'}
$directory=Join-Path $PSScriptRoot ('out\launch-tests\'+$Architecture+'-'+[guid]::NewGuid())
New-Item -ItemType Directory -Path $directory | Out-Null
$executable=Join-Path $directory 'RiumInstalledSmoke.exe'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "out\$Architecture\RiumImeFixture.exe") -Destination $executable
# The name contains Install deliberately: missing manifests caused error 740
# before the physical fixture could run in the ordinary user's session.
$start=[Diagnostics.ProcessStartInfo]::new($executable,'--launch-check')
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.WorkingDirectory=$directory
$process=[Diagnostics.Process]::Start($start)
if(!$process.WaitForExit(5000)){Stop-Process -InputObject $process;throw 'Fixture launch check timed out.'}
if($process.ExitCode -ne 0){throw "Fixture launch/ordinary-token check failed: $($process.ExitCode)"}
"PASS: $Architecture renamed fixture launches without elevation."
