# Read-only integration checks against the installed profile. No elevation.
param([string]$ControlRoot=(Join-Path $PSScriptRoot 'out\installable'))
$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run the read-only branding test in an ordinary, non-administrator session.'}
$control=Join-Path $ControlRoot 'x64\RiumKeysControl.exe'
function Invoke-Check([string]$Exe,[string]$Mode,[string]$Dll){
    $info=[Diagnostics.ProcessStartInfo]::new($Exe,($Mode+' "'+$Dll+'"'))
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    try {
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(!$process.WaitForExit(15000)){Stop-Process -InputObject $process;throw 'Branding check timed out.'}
        [pscustomobject]@{ExitCode=$process.ExitCode;Output=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()}
    }finally{$process.Dispose()}
}
$before=& $control --status | ConvertFrom-Json
if($LASTEXITCODE){throw 'Cannot snapshot input profile.'}
$keyPath='Software\Microsoft\CTF\TIP\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}\LanguageProfile\0x00000412\{EA007E57-6806-4596-BB29-88EBFBC620B5}'
$base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',[Microsoft.Win32.RegistryView]::Registry64)
try {
    $key=$base.OpenSubKey($keyPath)
    if(!$key){throw 'An installed CONTENTRIUM Keys profile is required.'}
    try {$icon=$key.GetValue('IconFile');$description=$key.GetValue('Description');$index=$key.GetValue('IconIndex')}finally{$key.Dispose()}
    foreach($architecture in @('x64','x86')){
        $exe=Join-Path $ControlRoot "$architecture\RiumKeysControl.exe"
        $verified=Invoke-Check $exe '--verify-branding' $icon
        if($verified.ExitCode){throw "$architecture cannot verify the installed CK brand resource: $($verified.Output)"}
        Write-Output $verified.Output
        $foreign=Invoke-Check $exe '--verify-branding' ($icon+'.wrong')
        if($foreign.ExitCode -ne 1 -or $foreign.Output -notmatch 'Registered brand DLL does not match'){throw 'A foreign icon path was not rejected at the ownership boundary.'}
        $ordinary=Invoke-Check $exe '--refresh-branding' $icon
        if($ordinary.ExitCode -ne 1 -or $ordinary.Output -notmatch 'requires administrator approval'){throw 'Brand registration must reject an unelevated caller before mutation.'}
    }
    $key=$base.OpenSubKey($keyPath)
    try {
        if($key.GetValue('IconFile') -ne $icon -or $key.GetValue('Description') -ne $description -or $key.GetValue('IconIndex') -ne $index){throw 'Read-only branding checks changed the profile.'}
    }finally{$key.Dispose()}
}finally{$base.Dispose()}
$after=& $control --status | ConvertFrom-Json
if($LASTEXITCODE){throw 'Cannot read input profile after branding checks.'}
foreach($field in @('registered','enabled','categories','active','koreanDefault','defaultTip','activeTip')){
    if($before.$field -ne $after.$field){throw "Branding checks changed $field."}
}
'PASS: x64/x86 shell brand extraction, foreign-path rejection, elevation guard and input state preservation.'
