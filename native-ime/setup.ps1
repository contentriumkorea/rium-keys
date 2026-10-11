param([switch]$Preflight)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
$logRoot=Join-Path $env:LOCALAPPDATA 'Contentrium\KeysSetup'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$log=Join-Path $logRoot ((Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid()+'.log')
$transcribing=$false
try {
    Start-Transcript -LiteralPath $log | Out-Null;$transcribing=$true
    if(![Environment]::Is64BitOperatingSystem -or ![Environment]::Is64BitProcess){throw 'This installer requires Windows x64 and 64-bit PowerShell.'}
    $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run Setup normally, not as administrator. Only its registration step requests elevation.'}
    $config=Get-RiumPackageConfig $PSScriptRoot
    [void](Assert-RiumManifest $PSScriptRoot $config)
    $app=Get-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RiumKeysInput' -ErrorAction SilentlyContinue
    $entry='install-local.ps1'
    if($app){
        if($app.DisplayVersion -eq $config.UpgradeFrom){$entry='upgrade-local.ps1'}
        elseif($app.DisplayVersion -eq $config.Version){
            $expected=Join-Path $env:ProgramFiles ('RIUM Keys\'+$config.Version)
            if($app.InstallLocation -ne $expected){throw 'Unexpected installation location.'}
            $stateFile=(Get-ItemProperty 'HKCU:\Software\Contentrium\RiumKeysInput' -Name InstallState).InstallState
            $state=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
            if($state.Status -ne 'Installed' -or $state.InstallRoot -ne $expected -or $state.UserSid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){throw 'Current installation requires recovery.'}
            [void](Assert-RiumManifest $expected $config)
            foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
                $architecture=if($view -eq 'Registry64'){'x64'}else{'x86'}
                $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
                try {
                    $key=$base.OpenSubKey('Software\Classes\CLSID\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}\InprocServer32')
                    try {if(!$key -or $key.GetValue('') -ne (Join-Path $expected "$architecture\RiumKeysInput.dll") -or $key.GetValue('ThreadingModel') -ne 'Apartment'){throw 'Installed COM registration requires recovery.'}}finally{if($key){$key.Dispose()}}
                    $key=$base.OpenSubKey('Software\Microsoft\CTF\TIP\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}\LanguageProfile\0x00000412\{EA007E57-6806-4596-BB29-88EBFBC620B5}')
                    try {
                        $branding=Get-RiumProfileBranding $key (Join-Path $env:ProgramFiles 'RIUM Keys')
                        if($branding.Description -ne 'CONTENTRIUM Keys' -or $branding.IconFile -ne (Join-Path $expected 'x64\RiumKeysInput.dll')){throw 'Installed profile branding requires recovery.'}
                    }finally{if($key){$key.Dispose()}}
                }finally{$base.Dispose()}
            }
            $statusText=& (Join-Path $expected 'x64\RiumKeysControl.exe') --status
            if($LASTEXITCODE){throw 'Cannot verify installed input profile.'}
            $status=$statusText | ConvertFrom-Json
            if(!$status.registered -or !$status.enabled -or $status.categories -ne 6){throw 'Installed input profile requires recovery.'}
            Test-RiumInstalledBranding $expected
            if(!$Preflight){
                Test-RiumInstalledLoad $expected $logRoot
                & (Join-Path $expected 'x64\RiumKeysControl.exe') --refresh
                if($LASTEXITCODE){throw 'Cannot refresh the installed input brand.'}
            }
            Write-Output 'ALREADY INSTALLED: same version and all installed files verified.'
            exit 0
        }else{throw "Unsupported upgrade from $($app.DisplayVersion). Remove the existing input method from Windows Installed Apps first; then run Setup again."}
    }
    $shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot $entry)+'"'),'-PackageRoot',('"'+$PSScriptRoot+'"'))
    if($Preflight){$arguments+='-Preflight'}
    $child=Start-Process -FilePath $shell -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput ($log+'.out') -RedirectStandardError ($log+'.err')
    Get-Content -LiteralPath ($log+'.out'),($log+'.err')
    if($child.ExitCode -ne 0){throw "Installation did not complete. Exit=$($child.ExitCode). See $log.out and $log.err"}
    Write-Output $(if($Preflight){'PREFLIGHT PASS: no installation or elevation performed.'}else{'INSTALLED: automatic installation checks passed. Restart open applications to load this version.'})
}catch { Write-Output $_;exit 1 }
finally { if($transcribing){Stop-Transcript | Out-Null} }
