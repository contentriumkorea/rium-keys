# Read-only integration test against the currently installed input method.
param([string]$ControlRoot=(Join-Path $PSScriptRoot 'out\installable'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
$logRoot=Join-Path $PSScriptRoot ('out\installed-load-tests-'+[guid]::NewGuid())
New-Item -ItemType Directory -Path $logRoot | Out-Null
$control=Join-Path $ControlRoot 'x64\RiumKeysControl.exe'
$beforeText=& $control --status
if($LASTEXITCODE){throw 'Cannot snapshot input profile before load verification.'}
$before=$beforeText | ConvertFrom-Json
foreach($architecture in @('x64','x86')){
    $view=if($architecture -eq 'x64'){[Microsoft.Win32.RegistryView]::Registry64}else{[Microsoft.Win32.RegistryView]::Registry32}
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
    try {
        $key=$base.OpenSubKey('Software\Classes\CLSID\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}\InprocServer32')
        try {if(!$key){throw 'Install a registered test version before this integration test.'};$dll=$key.GetValue('')}finally{if($key){$key.Dispose()}}
    }finally{$base.Dispose()}
    $exe=Join-Path $ControlRoot "$architecture\RiumKeysControl.exe"
    Invoke-RiumLoadCheck $exe $dll (Join-Path $logRoot "$architecture.log")
    $rejected=$false
    try {Invoke-RiumLoadCheck $exe ($dll+'.wrong') (Join-Path $logRoot "$architecture-wrong.log")}catch{$rejected=$true}
    if(!$rejected){throw "$architecture incorrectly accepted the wrong DLL path."}
}
$afterText=& $control --status
if($LASTEXITCODE){throw 'Cannot read input profile after load verification.'}
$after=$afterText | ConvertFrom-Json
foreach($field in @('registered','enabled','categories','active','koreanDefault','defaultTip','activeTip')){
    if($before.$field -ne $after.$field){throw "Read-only load verification changed $field."}
}
'PASS: x64/x86 COM load, wrong-path rejection and input profile preservation. No interactive input or registration changes.'
