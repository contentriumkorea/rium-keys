$ErrorActionPreference='Stop'
$statePath=(Get-ItemProperty 'HKCU:\Software\Contentrium\RiumKeysInput' -Name InstallState).InstallState
$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if($state.UserSid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){throw 'Run uninstall as the user who installed CONTENTRIUM Keys.'}
if([IO.Path]::GetFullPath($state.InstallRoot) -ne [IO.Path]::GetFullPath($PSScriptRoot)){
    throw 'This uninstaller does not own the current installation. Use the current installed-apps entry.'
}
$control=Join-Path $PSScriptRoot 'x64\RiumKeysControl.exe'
& $control --restore $state.PreviousTip $state.PreviousActiveTip
if($LASTEXITCODE){throw 'Could not restore the previous input method; registration has been preserved.'}
& $control --disable
if($LASTEXITCODE){throw 'Could not disable the RIUM input profile.'}
$shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$process=Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'install-machine.ps1')+'"'),'-Operation','Uninstall') -Verb RunAs -WindowStyle Hidden -PassThru
$process.WaitForExit()
if($process.ExitCode){throw 'Machine removal failed. See uninstall-result.log in the installed directory.'}
Remove-ItemProperty 'HKCU:\Software\Contentrium\RiumKeysInput' -Name InstallState -ErrorAction SilentlyContinue
'CONTENTRIUM Keys removed. Previous input method restored; settings and recovery files retained.'
