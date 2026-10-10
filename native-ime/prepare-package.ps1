$ErrorActionPreference='Stop'
$package=Join-Path $PSScriptRoot 'out\local-package'
New-Item -ItemType Directory -Force -Path $package | Out-Null
$sources=@{
    'x64\RiumKeysInput.dll'='out\installable\x64\RiumKeysInput.dll'
    'x86\RiumKeysInput.dll'='out\installable\x86\RiumKeysInput.dll'
    'x64\RiumKeysControl.exe'='out\installable\x64\RiumKeysControl.exe'
    'LICENSE'='third_party\jamotong\LICENSE'
    'COPYRIGHT.md'='third_party\jamotong\COPYRIGHT.md'
    'install-machine.ps1'='install-machine.ps1'
    'uninstall-local.ps1'='uninstall-local.ps1'
}
$manifest=@()
foreach($relative in $sources.Keys){
    $destination=Join-Path $package $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $sources[$relative]) -Destination $destination -Force
    $manifest+=[pscustomobject]@{Path=$relative;Sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash}
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding UTF8
Write-Output "Prepared $package"
