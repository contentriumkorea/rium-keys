param([switch]$Preflight,[string]$PackageRoot)
$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Start the upgrade in the ordinary user session.'}
. (Join-Path $PSScriptRoot 'package-common.ps1')
$package=Resolve-RiumPackageRoot $PSScriptRoot $PackageRoot
$config=Get-RiumPackageConfig $package
$verifiedManifest=@(Assert-RiumManifest $package $config)
foreach($architecture in @('x64','x86')){Assert-RiumCandidateVersion (Join-Path $package "$architecture\RiumKeysInput.dll") $config.Version}
$target=Join-Path $env:ProgramFiles ("RIUM Keys\"+$config.Version)
$previousRoot=Join-Path $env:ProgramFiles ("RIUM Keys\"+$config.UpgradeFrom)
$control=Join-Path $package 'x64\RiumKeysControl.exe'
$stateKey='HKCU:\Software\Contentrium\RiumKeysInput'
$previousStatePath=(Get-ItemProperty $stateKey -Name InstallState).InstallState
$previous=Get-Content -LiteralPath $previousStatePath -Raw | ConvertFrom-Json
if($previous.UserSid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value -or $previous.InstallRoot -ne $previousRoot -or $previous.Status -ne 'Installed'){throw 'The previous installation state needs inspection.'}
if(Test-Path -LiteralPath $target){throw 'The new version directory already exists; inspect the previous attempt.'}
foreach($entry in (Get-Content -LiteralPath (Join-Path $previousRoot 'manifest.json') -Raw | ConvertFrom-Json)){
    if((Get-FileHash -LiteralPath (Join-Path $previousRoot $entry.Path) -Algorithm SHA256).Hash -ne $entry.Sha256){throw 'Previous installation has unexpected changes.'}
}
$beforeText=& $control --status
if($LASTEXITCODE){throw 'Cannot capture input profile state.'}
$before=$beforeText | ConvertFrom-Json
if(!$before.registered -or !$before.enabled -or $before.categories -ne 6){throw 'The previous native input profile is incomplete.'}
if($Preflight){'PREFLIGHT PASS: previous install, original fallback state and package integrity verified.';exit 0}
$transaction=[guid]::NewGuid().ToString()
$recovery=Join-Path $env:LOCALAPPDATA ('Contentrium\RIUM Keys\Recovery\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$transaction)
New-Item -ItemType Directory -Path $recovery | Out-Null
Copy-Item -LiteralPath $previousStatePath -Destination (Join-Path $recovery 'previous-install-state.json')
$state=[ordered]@{
    UserSid=$previous.UserSid;PreviousTip=$previous.PreviousTip;PreviousActiveTip=$previous.PreviousActiveTip
    InstallRoot=$target;LegacyDirectory=$previous.LegacyDirectory;PreviousInstallRoot=$previousRoot
    PreviousInstallState=$previousStatePath;Status='PreparingUpgrade';Created=(Get-Date).ToString('o')
}
$statePath=Join-Path $recovery 'install-state.json'
function Save-State([string]$status){$state.Status=$status;$state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8}
Save-State 'PreparingUpgrade'
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\RIUM.Keys.Install.$transaction.Ready")
$done=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\RIUM.Keys.Install.$transaction.Done")
$commit=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\RIUM.Keys.Install.$transaction.Commit")
$machine=$null;$commitRequested=$false;$committed=$false
try {
    Save-State 'AwaitingAdministrator'
    $shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $machine=Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $package 'install-machine.ps1')+'"'),'-Operation','Upgrade','-Transaction',$transaction) -Verb RunAs -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    while(!$ready.WaitOne(250)){if($machine.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Machine upgrade did not become ready.'}}
    Save-State 'VerifyingInstallation'
    [void](Assert-RiumManifest $target $config)
    Test-RiumInstalledLoad $target $recovery
    Test-RiumInstalledBranding $target
    & $control --refresh
    if($LASTEXITCODE){throw 'Installed input name could not be read back.'}
    $afterText=& $control --status
    if($LASTEXITCODE){throw 'Input profile readback failed.'}
    $after=$afterText | ConvertFrom-Json
    foreach($field in @('registered','enabled','categories','active','koreanDefault','defaultTip','activeTip')){
        if($after.$field -ne $before.$field){throw "Brand registration changed $field."}
    }
    Save-State 'CommittingUpgrade';$commitRequested=$true
    [void]$commit.Set();[void]$done.Set()
    if(!$machine.WaitForExit(45000)){throw 'Machine commit status is not yet known.'}
    if($machine.ExitCode -ne 0){throw 'Machine upgrade did not commit.'}
    $committed=$true
    Save-State 'Installed'
    Set-ItemProperty $stateKey -Name InstallState -Value $statePath
    Get-Content -LiteralPath (Join-Path $target 'install-result.log')
    "INSTALLED: CONTENTRIUM Keys $($config.Version). Recovery: $statePath"
}catch {
    $failure=$_
    if($commitRequested -and $machine -and !$machine.HasExited){Save-State 'CommitStatusUnknown';throw "Inspect the machine log before retrying: $failure"}
    if(!$committed){
        [void]$done.Set()
        if($machine -and !$machine.WaitForExit(45000)){Save-State 'RecoveryRequired';throw "Machine rollback is still pending: $failure"}
        if($commitRequested -and $machine.ExitCode -eq 0){Save-State 'InstalledStateRepairRequired'}else{
            try {
                foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
                    $arch=if($view -eq 'Registry64'){'x64'}else{'x86'}
                    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
                    try {$key=$base.OpenSubKey('Software\Classes\CLSID\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}\InprocServer32');try {
                        if(!$key -or $key.GetValue('') -ne (Join-Path $previousRoot "$arch\RiumKeysInput.dll")){throw "COM rollback mismatch: $view"}
                    }finally{if($key){$key.Dispose()}}}finally{$base.Dispose()}
                }
                $app=Get-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeysInput'
                if($app.InstallLocation -ne $previousRoot -or $app.DisplayVersion -ne $config.UpgradeFrom){throw 'Installed-app rollback mismatch.'}
                $rollbackText=& $control --status
                if($LASTEXITCODE){throw 'Cannot read rollback input state.'}
                $rollback=$rollbackText | ConvertFrom-Json
                if(!$rollback.registered -or !$rollback.enabled -or $rollback.categories -ne 6 -or $rollback.defaultTip -ne $before.defaultTip -or $rollback.activeTip -ne $before.activeTip){throw 'Input profile rollback mismatch.'}
                Save-State 'FailedRolledBack'
            }catch {Save-State 'RecoveryRequired';throw "Rollback readback failed: $_. Original error: $failure"}
        }
    }else{Save-State 'InstalledStateRepairRequired'}
    throw $failure
}finally{$ready.Dispose();$done.Dispose();$commit.Dispose()}
