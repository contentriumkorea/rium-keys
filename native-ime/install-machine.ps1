param([ValidateSet('Install','Upgrade','Uninstall')][string]$Operation='Install',[string]$Transaction)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
$config=Get-RiumPackageConfig $PSScriptRoot
$version=$config.Version
$root=[IO.Path]::GetFullPath((Join-Path $env:ProgramFiles "RIUM Keys\$version"))
$class='Software\Classes\CLSID\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}'
$tip='Software\Microsoft\CTF\TIP\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}'
$profileDescription="$tip\LanguageProfile\0x00000412\{EA007E57-6806-4596-BB29-88EBFBC620B5}"
$uninstall='Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeysInput'
$views=@([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Machine registration requires administrator approval.'}
$control=Join-Path $root 'x64\RiumKeysControl.exe'
function Invoke-Control([string[]]$Arguments){
    & $control @Arguments
    if($LASTEXITCODE -ne 0){throw "Profile controller failed: $Arguments"}
}
function Assert-Owner {
    foreach($view in $views){
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
        try {
            $key=$base.OpenSubKey("$class\InprocServer32")
            if($key){try {
                $arch=if($view -eq 'Registry64'){'x64'}else{'x86'}
                if($key.GetValue('') -ne (Join-Path $root "$arch\RiumKeysInput.dll")){throw 'A different installation owns this profile.'}
            }finally{$key.Dispose()}}
        }finally{$base.Dispose()}
    }
}
function Remove-Registration {
    Assert-Owner
    Invoke-Control -Arguments @('--unregister')
    foreach($view in $views){
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
        try {
            $base.DeleteSubKeyTree($class,$false)
            $remaining=$base.OpenSubKey($tip)
            if($remaining){$remaining.Dispose();throw "TIP registration remains in $view"}
        }finally{$base.Dispose()}
    }
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',[Microsoft.Win32.RegistryView]::Registry64)
    try {$base.DeleteSubKeyTree($uninstall,$false)}finally{$base.Dispose()}
}
if($Operation -eq 'Uninstall'){
    $expected=[IO.Path]::GetFullPath((Join-Path $env:ProgramFiles "RIUM Keys\$version"))
    if($root -ne $expected -or [IO.Path]::GetFullPath($PSScriptRoot) -ne $expected){throw 'Uninstall must run from its installed version directory.'}
    Start-Transcript -Path (Join-Path $root 'uninstall-result.log') -Force | Out-Null
    try {
        Remove-Registration
        # Loaded DLLs remain in this version directory until their hosts exit.
        # No user application is stopped and no recursive filesystem deletion occurs.
        foreach($relative in @('x64\RiumKeysInput.dll','x86\RiumKeysInput.dll','x64\RiumKeysControl.exe')){
            $file=Join-Path $root $relative
            try {Remove-Item -LiteralPath $file -Force -ErrorAction Stop}catch{Write-Output "Deferred file cleanup: $file"}
        }
        'UNINSTALLED: profile, categories, COM registration and application entry removed.'
    }finally{Stop-Transcript | Out-Null}
    exit 0
}
if($Transaction -notmatch '^[0-9a-f-]{36}$'){throw 'Missing transaction identity.'}
$ready=[Threading.EventWaitHandle]::OpenExisting("Local\RIUM.Keys.Install.$Transaction.Ready")
$done=[Threading.EventWaitHandle]::OpenExisting("Local\RIUM.Keys.Install.$Transaction.Done")
$commit=[Threading.EventWaitHandle]::OpenExisting("Local\RIUM.Keys.Install.$Transaction.Commit")
if($Operation -eq 'Upgrade'){
    # Reuse the existing profile and user defaults; only the versioned binaries change.
    $previousRoot=Join-Path $env:ProgramFiles ("RIUM Keys\"+$config.UpgradeFrom)
    $changedViews=@();$oldBranding=@{};$oldAppValues=@{};$appChanged=$false;$transcript=$false;$upgradeCommitted=$false
    try {
        if(Test-Path -LiteralPath $root){throw 'New version directory already exists; refusing to overwrite.'}
        foreach($view in $views){
            $arch=if($view -eq 'Registry64'){'x64'}else{'x86'}
            $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
            try {
                $key=$base.OpenSubKey("$class\InprocServer32")
                if(!$key){throw "Missing previous COM registration: $view"}
                try {if($key.GetValue('') -ne (Join-Path $previousRoot "$arch\RiumKeysInput.dll") -or $key.GetValue('ThreadingModel') -ne 'Apartment'){throw 'Unexpected previous COM owner.'}}finally{$key.Dispose()}
            }finally{$base.Dispose()}
        }
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',[Microsoft.Win32.RegistryView]::Registry64)
        try {
            $key=$base.OpenSubKey($uninstall)
            if(!$key){throw 'Missing previous installed-apps entry.'}
            try {
                if($key.GetValue('InstallLocation') -ne $previousRoot -or $key.GetValue('DisplayVersion') -ne $config.UpgradeFrom){throw 'Unexpected installed version.'}
                foreach($name in @('DisplayName','DisplayVersion','InstallLocation','DisplayIcon','UninstallString')){$oldAppValues[$name]=$key.GetValue($name)}
            }finally{$key.Dispose()}
        }finally{$base.Dispose()}
        $manifest=@(Assert-RiumManifest $PSScriptRoot $config)
        foreach($architecture in @('x64','x86')){Assert-RiumCandidateVersion (Join-Path $PSScriptRoot "$architecture\RiumKeysInput.dll") $config.Version}

        New-Item -ItemType Directory -Path $root | Out-Null
        Start-Transcript -Path (Join-Path $root 'install-result.log') -Force | Out-Null;$transcript=$true
        foreach($entry in $manifest){
            $destination=Join-Path $root $entry.Path
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry.Path) -Destination $destination
            if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $entry.Sha256){throw 'Installed hash mismatch.'}
        }
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Destination $root
        foreach($view in $views){
            $arch=if($view -eq 'Registry64'){'x64'}else{'x86'}
            $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
            try {
                $key=$base.OpenSubKey("$class\InprocServer32",$true)
                try {
                    if($key.GetValue('') -ne (Join-Path $previousRoot "$arch\RiumKeysInput.dll")){throw 'COM owner changed during staging.'}
                    $changedViews+=$view
                    $key.SetValue('',(Join-Path $root "$arch\RiumKeysInput.dll"))
                    if($key.GetValue('') -ne (Join-Path $root "$arch\RiumKeysInput.dll")){throw 'COM path readback mismatch.'}
                }finally{$key.Dispose()}
            }finally{$base.Dispose()}
        }
        [void]$ready.Set()
        if(!$done.WaitOne(600000) -or !$commit.WaitOne(0)){throw 'Upgrade was not committed by the ordinary-user verifier.'}
        # Snapshot BOTH views before writing: CTF keys can alias. Include the
        # icon path because early builds left it pointing at the original K icon.
        foreach($view in $views){
            $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
            try {
                $key=$base.OpenSubKey($profileDescription)
                if(!$key){throw "Missing owned language profile: $view"}
                try {
                    $oldBranding[$view]=Get-RiumProfileBranding $key (Join-Path $env:ProgramFiles 'RIUM Keys')
                }finally{$key.Dispose()}
            }finally{$base.Dispose()}
        }
        foreach($view in $views){
            $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
            try {
                $key=$base.OpenSubKey($profileDescription,$true)
                if(!$key){throw "Missing owned language profile: $view"}
                try {
                    Set-RiumProfileBranding $key $oldBranding[$view] (Join-Path $root 'x64\RiumKeysInput.dll')
                }finally{$key.Dispose()}
            }finally{$base.Dispose()}
        }
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',[Microsoft.Win32.RegistryView]::Registry64)
        try {
            $key=$base.OpenSubKey($uninstall,$true);$appChanged=$true
            try {
                $key.SetValue('DisplayName','CONTENTRIUM Keys')
                $key.SetValue('DisplayVersion',$version);$key.SetValue('InstallLocation',$root)
                $key.SetValue('DisplayIcon',(Join-Path $root 'x64\RiumKeysInput.dll'))
                $shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
                $key.SetValue('UninstallString',('"'+$shell+'" -NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'uninstall-local.ps1')+'"'))
            }finally{$key.Dispose()}
        }finally{$base.Dispose()}
        $upgradeCommitted=$true
        "UPGRADED: verified $version; previous version retained; profile/defaults preserved."
    }catch {
        Write-Output $_
        $rollbackErrors=@()
        foreach($view in $changedViews){
            $arch=if($view -eq 'Registry64'){'x64'}else{'x86'}
            try {
                $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
                try {$key=$base.OpenSubKey("$class\InprocServer32",$true);try {
                    $oldDll=Join-Path $previousRoot "$arch\RiumKeysInput.dll"
                    if(!$key -or $key.GetValue('') -notin @($oldDll,(Join-Path $root "$arch\RiumKeysInput.dll"))){throw 'Rollback COM owner mismatch.'}
                    $key.SetValue('',$oldDll)
                    if($key.GetValue('') -ne $oldDll){throw 'Rollback COM readback mismatch.'}
                }finally{if($key){$key.Dispose()}}}finally{$base.Dispose()}
            }catch{$rollbackErrors+="$view`: $($_.Exception.Message)"}
        }
        foreach($view in $oldBranding.Keys){
            try {
                $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
                try {$key=$base.OpenSubKey($profileDescription,$true);try {
                    Set-RiumProfileBranding $key $oldBranding[$view] (Join-Path $root 'x64\RiumKeysInput.dll') -Restore
                }finally{if($key){$key.Dispose()}}}finally{$base.Dispose()}
            }catch{$rollbackErrors+=$_.Exception.Message}
        }
        if($appChanged){
            try {
                $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',[Microsoft.Win32.RegistryView]::Registry64)
                try {$key=$base.OpenSubKey($uninstall,$true);try {
                    foreach($name in $oldAppValues.Keys){$key.SetValue($name,$oldAppValues[$name]);if($key.GetValue($name) -ne $oldAppValues[$name]){throw "Installed-app rollback mismatch: $name"}}
                }finally{if($key){$key.Dispose()}}}finally{$base.Dispose()}
            }catch{$rollbackErrors+=$_.Exception.Message}
        }
        if($rollbackErrors.Count){throw ('Upgrade rollback requires recovery: '+($rollbackErrors -join '; '))}
        'Upgrade did not commit; previous COM paths restored. Staged files retained.'
    }finally {
        if($transcript){Stop-Transcript | Out-Null}
        $ready.Dispose();$done.Dispose();$commit.Dispose()
    }
    if(!$upgradeCommitted){exit 1};exit 0
}
$owned=$false
$createdViews=@()
$registrationAttempted=$false
$committed=$false
try {
    foreach($view in $views){
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
        try {
            foreach($path in @($class,$tip,$uninstall)){
                $existing=$base.OpenSubKey($path)
                if($existing){$existing.Dispose();throw 'Existing native registration found; use the supported upgrade or remove the previous input method first.'}
            }
        }finally{$base.Dispose()}
    }
    if(Test-Path -LiteralPath $root){throw 'Version directory already exists; do not overwrite a possibly loaded DLL.'}
    $manifest=@(Assert-RiumManifest $PSScriptRoot $config)
    foreach($architecture in @('x64','x86')){Assert-RiumCandidateVersion (Join-Path $PSScriptRoot "$architecture\RiumKeysInput.dll") $config.Version}

    New-Item -ItemType Directory -Path $root | Out-Null
    $owned=$true
    Start-Transcript -Path (Join-Path $root 'install-result.log') -Force | Out-Null
    foreach($entry in $manifest){
        $destination=Join-Path $root $entry.Path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry.Path) -Destination $destination
        if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $entry.Sha256){throw 'Installed file hash mismatch.'}
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Destination $root
    foreach($view in $views){
        $arch=if($view -eq 'Registry64'){'x64'}else{'x86'}
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
        try {
            $key=$base.CreateSubKey("$class\InprocServer32");$createdViews+=$view
            try {$key.SetValue('',(Join-Path $root "$arch\RiumKeysInput.dll"));$key.SetValue('ThreadingModel','Apartment')}finally{$key.Dispose()}
        }finally{$base.Dispose()}
    }
    $registrationAttempted=$true
    Invoke-Control -Arguments @('--register',(Join-Path $root 'x64\RiumKeysInput.dll'))
    [void]$ready.Set()
    if(!$done.WaitOne(600000) -or !$commit.WaitOne(0)){throw 'Installation was not committed by the user-session verifier.'}
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',[Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key=$base.CreateSubKey($uninstall)
        try {
            $key.SetValue('DisplayName','CONTENTRIUM Keys');$key.SetValue('DisplayVersion',$version)
            $key.SetValue('Publisher','Contentrium');$key.SetValue('InstallLocation',$root)
            $key.SetValue('DisplayIcon',(Join-Path $root 'x64\RiumKeysInput.dll'))
            $shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
            $key.SetValue('UninstallString',('"'+$shell+'" -NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'uninstall-local.ps1')+'"'))
            $key.SetValue('NoModify',1,[Microsoft.Win32.RegistryValueKind]::DWord)
            $key.SetValue('NoRepair',1,[Microsoft.Win32.RegistryValueKind]::DWord)
        }finally{$key.Dispose()}
    }finally{$base.Dispose()}
    $committed=$true
    'INSTALLED: both COM architectures and native keyboard profile verified.'
} catch {
    Write-Output $_
    if($registrationAttempted){Remove-Registration}
    elseif($createdViews.Count){
        foreach($view in $createdViews){$base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view);try{$base.DeleteSubKeyTree($class,$false)}finally{$base.Dispose()}}
    }
    exit 1
} finally {
    if($owned){Stop-Transcript | Out-Null}
    $ready.Dispose();$done.Dispose();$commit.Dispose()
}
if(!$committed){exit 1}
