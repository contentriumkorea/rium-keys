param([switch]$Preflight,[string]$PackageRoot)
$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Start installation from the ordinary user session.'}
. (Join-Path $PSScriptRoot 'package-common.ps1')
$package=Resolve-RiumPackageRoot $PSScriptRoot $PackageRoot
$config=Get-RiumPackageConfig $package
$verifiedManifest=@(Assert-RiumManifest $package $config)
foreach($architecture in @('x64','x86')){Assert-RiumCandidateVersion (Join-Path $package "$architecture\RiumKeysInput.dll") $config.Version}
$control=Join-Path $package 'x64\RiumKeysControl.exe'
$target=Join-Path $env:ProgramFiles ("RIUM Keys\"+$config.Version)
$legacyDir=Join-Path $env:LOCALAPPDATA 'Programs\RIUM Keys'
$legacyExe=Join-Path $legacyDir 'RiumKeys.exe'
$legacyUninstaller=Join-Path $legacyDir 'Uninstall.exe'
function Assert-NativeRegistryAbsent {
    foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
        try {
            foreach($path in @('Software\Classes\CLSID\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}','Software\Microsoft\CTF\TIP\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}','Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeysInput')){
                $key=$base.OpenSubKey($path)
                if($key){$key.Dispose();throw "Native registration remains: $view $path"}
            }
        }finally{$base.Dispose()}
    }
}
Assert-NativeRegistryAbsent
$beforeText=& $control --status
if($LASTEXITCODE){throw 'Cannot read the current input profile.'}
$before=$beforeText | ConvertFrom-Json
if($before.registered -or $before.categories -ne 0 -or (Test-Path -LiteralPath $target)){throw 'An existing native installation or version directory needs inspection.'}
if($before.koreanDefault -notmatch '^0x0412:\{[0-9A-Fa-f-]{36}\}\{[0-9A-Fa-f-]{36}\}$'){throw 'Cannot capture the previous Korean input profile.'}
$hasLegacy=Test-RiumLegacyMigration $legacyDir
if($Preflight){'PREFLIGHT PASS: package hashes/version, native absence, previous input profile and optional legacy migration verified.';exit 0}
$transaction=[guid]::NewGuid().ToString()
$recovery=Join-Path $env:LOCALAPPDATA ("Contentrium\RIUM Keys\Recovery\"+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$transaction)
New-Item -ItemType Directory -Path $recovery | Out-Null
if($hasLegacy){Copy-Item -LiteralPath $legacyExe,$legacyUninstaller -Destination $recovery}
$legacySettings=Get-ItemProperty 'HKCU:\Software\Contentrium\RiumKeys' -ErrorAction SilentlyContinue
$legacySettings | Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $recovery 'legacy-settings.json') -Encoding UTF8
$state=[ordered]@{UserSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value;PreviousTip=$before.defaultTip;PreviousActiveTip=$before.activeTip;InstallRoot=$target;LegacyDirectory=$legacyDir;Status='Preparing';Created=(Get-Date).ToString('o')}
$statePath=Join-Path $recovery 'install-state.json'
function Save-State([string]$status){$state.Status=$status;$state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8}
Save-State 'Preparing'
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\RIUM.Keys.Install.$transaction.Ready")
$done=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\RIUM.Keys.Install.$transaction.Done")
$commit=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\RIUM.Keys.Install.$transaction.Commit")
$machine=$null;$enabled=$false;$selected=$false;$committed=$false;$commitRequested=$false
$wasRunning=$hasLegacy -and @(Get-Process RiumKeys -ErrorAction SilentlyContinue).Count -gt 0
try {
    if($wasRunning){
        $stop=Start-Process -FilePath $legacyExe -ArgumentList '--exit' -WindowStyle Hidden -PassThru
        if(!$stop.WaitForExit(10000)){throw 'Legacy exit request timed out.'}
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        while(Get-Process RiumKeys -ErrorAction SilentlyContinue){if([DateTime]::UtcNow -gt $deadline){throw 'Legacy processes did not stop.'};Start-Sleep -Milliseconds 100}
    }
    Save-State 'AwaitingAdministrator'
    $shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $machine=Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $package 'install-machine.ps1')+'"'),'-Transaction',$transaction) -Verb RunAs -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while(!$ready.WaitOne(250)){
        if($machine.HasExited -or [DateTime]::UtcNow -gt $deadline){throw "Machine registration failed. See $target\install-result.log"}
    }
    $enabled=$true # an attempted call may partially succeed
    & $control --enable
    if($LASTEXITCODE){throw 'Could not enable the user input profile.'}
    New-Item -Path 'HKCU:\Software\Contentrium\RiumKeysInput' -Force | Out-Null
    Set-ItemProperty 'HKCU:\Software\Contentrium\RiumKeysInput' -Name InstallState -Value $statePath
    Save-State 'VerifyingInstallation'
    [void](Assert-RiumManifest $target $config)
    Test-RiumInstalledLoad $target $recovery
    Save-State 'SelectingNativeInput'
    $selected=$true # default selection changes before activation/readback
    & $control --select
    if($LASTEXITCODE){throw 'Could not select the native input method.'}
    $nativeText=& $control --status
    if($LASTEXITCODE){throw 'Cannot verify selected input method.'}
    $native=$nativeText | ConvertFrom-Json
    if(!$native.registered -or !$native.enabled -or !$native.active -or $native.categories -ne 6){throw 'Native registration or activation is incomplete.'}
    $commitRequested=$true
    [void]$commit.Set();[void]$done.Set()
    if(!$machine.WaitForExit(45000) -or $machine.ExitCode -ne 0){throw 'Machine installation did not commit.'}
    $committed=$true
    if($hasLegacy){
    Save-State 'NativeInstalledRemovingLegacy'
    $remove=Start-Process -FilePath $legacyUninstaller -ArgumentList '/S' -WindowStyle Hidden -PassThru
    if(!$remove.WaitForExit(30000)){throw 'Legacy uninstaller timed out; native IME remains installed.'}
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while((Test-Path -LiteralPath $legacyExe) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 150}
    if((Test-Path -LiteralPath $legacyExe) -or (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeys') -or (Get-Process RiumKeys -ErrorAction SilentlyContinue)){throw 'Legacy removal is incomplete; native IME remains installed.'}
    $run=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name RiumKeys -ErrorAction SilentlyContinue
    if($run){throw 'Legacy startup registration remains.'}
    }
    Save-State 'Installed'
    Get-Content -LiteralPath (Join-Path $target 'install-result.log')
    "INSTALLED: CONTENTRIUM Keys $($config.Version); native profile selected; legacy migration performed when present. Recovery: $statePath"
} catch {
    $failure=$_
    if($commitRequested -and !$committed -and $machine -and !$machine.HasExited){
        Save-State 'CommitStatusUnknown'
        throw "Installation commit is still running. User state was preserved; inspect $target\install-result.log before further changes. Original error: $failure"
    }
    if(!$committed -and !($commitRequested -and $machine.ExitCode -eq 0)){
        $rollbackErrors=@()
        # Enabling a profile can change selection before the explicit select step.
        if($enabled -or $selected){& $control --restore $before.defaultTip $before.activeTip;if($LASTEXITCODE){$rollbackErrors+='Previous default/active profile restoration failed.'}}
        if($enabled){& $control --disable;if($LASTEXITCODE){$rollbackErrors+='User profile disable failed.'}}
        [void]$done.Set()
        if($machine -and !$machine.WaitForExit(45000)){$rollbackErrors+='Machine rollback is still pending.'}
        $afterText=& $control --status
        if($LASTEXITCODE){$rollbackErrors+='Cannot verify rollback state.'}else{
            $after=$afterText | ConvertFrom-Json
            $after | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $recovery 'rollback-readback.json') -Encoding UTF8
            if($after.registered -or $after.enabled -or $after.categories -ne 0 -or $after.defaultTip -ne $before.defaultTip -or $after.activeTip -ne $before.activeTip){$rollbackErrors+='Input profile rollback readback mismatch.'}
        }
        try {Assert-NativeRegistryAbsent}catch{$rollbackErrors+=$_.Exception.Message}
        if($wasRunning -and (Test-Path -LiteralPath $legacyExe)){Start-Process -FilePath $legacyExe -WindowStyle Hidden}
        if($rollbackErrors.Count){Save-State 'RecoveryRequired';throw ("$failure Rollback incomplete: "+($rollbackErrors -join '; '))}
        Save-State 'FailedRolledBack'
    }else{Save-State 'NativeInstalledLegacyRemovalIncomplete'}
    throw $failure
} finally {$ready.Dispose();$done.Dispose();$commit.Dispose()}
