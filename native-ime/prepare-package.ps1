$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
$config=Get-RiumPackageConfig $PSScriptRoot
# Mandatory: never write candidate packages before the actual native suites pass.
& (Join-Path $PSScriptRoot 'test-package.ps1')
$verification=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'out\package-verification.json') -Raw | ConvertFrom-Json
if(!$verification.ContractChecksPassed){throw 'Native verification report did not pass.'}
foreach($architecture in @('x64','x86')){
    Assert-RiumCandidateVersion (Join-Path $PSScriptRoot "out\installable\$architecture\RiumKeysInput.dll") $config.Version
}
$sources=@{
    'x64\RiumKeysInput.dll'='out\installable\x64\RiumKeysInput.dll'
    'x86\RiumKeysInput.dll'='out\installable\x86\RiumKeysInput.dll'
    'x64\RiumKeysControl.exe'='out\installable\x64\RiumKeysControl.exe'
    'x86\RiumKeysControl.exe'='out\installable\x86\RiumKeysControl.exe'
    'LICENSE'='third_party\jamotong\LICENSE'
    'COPYRIGHT.md'='third_party\jamotong\COPYRIGHT.md'
}
foreach($relative in $config.PayloadPaths){if(!$sources.ContainsKey($relative)){$sources[$relative]=$relative}}
$output=Join-Path $PSScriptRoot 'out'
$package=Assert-RiumContainedPath $output (Join-Path $output 'local-package')
$stage=Assert-RiumContainedPath $output (Join-Path $output ('package-stage-'+[guid]::NewGuid()))
$backup=Assert-RiumContainedPath $output (Join-Path $output ('package-retained-'+[guid]::NewGuid()))
New-Item -ItemType Directory -Path $stage | Out-Null
$manifest=@()
foreach($relative in $config.PayloadPaths){
    $destination=Assert-RiumContainedPath $stage (Join-Path $stage $relative)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $sources[$relative]) -Destination $destination
    $manifest+=[pscustomobject]@{Path=$relative;Sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash}
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding UTF8
[void](Assert-RiumManifest $stage $config)
foreach($architecture in @('x64','x86')){
    $tested=@($verification.Checks | Where-Object Architecture -eq $architecture | Select-Object -ExpandProperty DllSha256 -Unique)
    if($tested.Count -ne 1 -or (Get-FileHash -LiteralPath (Join-Path $stage "$architecture\RiumKeysInput.dll")).Hash -ne $tested[0]){throw 'Staged candidate changed after native verification.'}
}
# Retain old packages; never recursively delete files from a caller-supplied path.
$hadPackage=Test-Path -LiteralPath $package
if($hadPackage){
    [void](Assert-RiumContainedPath $output $package)
    [void](Assert-RiumContainedPath $output $backup)
    Move-Item -LiteralPath $package -Destination $backup
}
try {
    [void](Assert-RiumContainedPath $output $stage)
    [void](Assert-RiumContainedPath $output $package)
    Move-Item -LiteralPath $stage -Destination $package
} catch {
    if($hadPackage -and !(Test-Path -LiteralPath $package)){
        [void](Assert-RiumContainedPath $output $backup)
        [void](Assert-RiumContainedPath $output $package)
        Move-Item -LiteralPath $backup -Destination $package
    }
    throw
}
Write-Output "Prepared $($config.Version) release package: $package"
Write-Output 'No stable updater metadata or public release was generated.'
