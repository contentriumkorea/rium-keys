param([ValidateSet('Install','Uninstall')][string]$Operation='Install',[string]$Transaction)
$ErrorActionPreference='Stop'
$version='2.0.0-preview.1'
$root=[IO.Path]::GetFullPath((Join-Path $env:ProgramFiles "RIUM Keys\$version"))
$class='Software\Classes\CLSID\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}'
$tip='Software\Microsoft\CTF\TIP\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}'
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
    $expected=[IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'RIUM Keys\2.0.0-preview.1'))
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
                if($existing){$existing.Dispose();throw 'Existing native registration found; upgrade is not supported by this preview installer.'}
            }
        }finally{$base.Dispose()}
    }
    if(Test-Path -LiteralPath $root){throw 'Version directory already exists; do not overwrite a possibly loaded DLL.'}
    $manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
    $required=@('x64\RiumKeysInput.dll','x64\RiumKeysControl.exe','x86\RiumKeysInput.dll','LICENSE','COPYRIGHT.md','uninstall-local.ps1','install-machine.ps1')
    if(@($manifest).Count -ne $required.Count -or @(Compare-Object ($manifest.Path | Sort-Object -Unique) ($required | Sort-Object)).Count){throw 'Incomplete or duplicate package manifest.'}
    foreach($entry in $manifest){
        if($entry.Path -notmatch '^(x64\\RiumKeys(Input\.dll|Control\.exe)|x86\\RiumKeysInput\.dll|LICENSE|COPYRIGHT\.md|uninstall-local\.ps1|install-machine\.ps1)$'){throw 'Unexpected package path.'}
        if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $entry.Path) -Algorithm SHA256).Hash -ne $entry.Sha256){throw 'Package hash mismatch.'}
    }
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
            $key.SetValue('DisplayName','RIUM Keys');$key.SetValue('DisplayVersion',$version)
            $key.SetValue('Publisher','Contentrium');$key.SetValue('InstallLocation',$root)
            $key.SetValue('DisplayIcon',(Join-Path $root 'x64\RiumKeysInput.dll'))
            $shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
            $key.SetValue('UninstallString',('"'+$shell+'" -NoProfile -File "'+(Join-Path $root 'uninstall-local.ps1')+'"'))
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
