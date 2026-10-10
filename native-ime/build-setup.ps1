param([string]$Nsis=(Join-Path $PSScriptRoot '..\tools\nsis-3.13\makensis.exe'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
$config=Get-RiumPackageConfig $PSScriptRoot
& (Join-Path $PSScriptRoot 'test-installer-scripts.ps1')
& (Join-Path $PSScriptRoot 'prepare-package.ps1')
$output=Join-Path $PSScriptRoot 'out\release'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$installer=Join-Path $output 'CONTENTRIUM-Keys-Setup.exe'
$sequence=($config.Version -split '\.')[-1]
& $Nsis /V2 ("/DAPP_VERSION="+$config.Version) ("/DFILE_VERSION=2.0.0."+$sequence) ("/DPAYLOAD="+(Join-Path $PSScriptRoot 'out\local-package')) ("/DOUTPUT="+$installer) (Join-Path $PSScriptRoot 'ContentriumKeys.nsi')
if($LASTEXITCODE -ne 0){throw 'Native installer build failed.'}
$digest=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),($digest+'  CONTENTRIUM-Keys-Setup.exe'+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
Write-Output "Built $($config.Version): $installer"
Write-Output "SHA256 $digest"
