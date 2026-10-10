$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
$config=Get-RiumPackageConfig $PSScriptRoot
$script:checks=0
function Check([bool]$Condition,[string]$Name){if(!$Condition){throw "FAIL: $Name"};$script:checks++;Write-Output "PASS: $Name"}
function Reject([scriptblock]$Action,[string]$Name){$rejected=$false;try{& $Action | Out-Null}catch{$rejected=$true};Check $rejected $Name}
$testRoot=Assert-RiumContainedPath (Join-Path $PSScriptRoot 'out') (Join-Path $PSScriptRoot ('out\installer-script-tests-'+[guid]::NewGuid()))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$entries=@()
foreach($relative in $config.PayloadPaths){
    $path=Join-Path $testRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    Set-Content -LiteralPath $path -Value "fixture:$relative" -Encoding UTF8
    $entries+=[pscustomobject]@{Path=$relative;Sha256=(Get-FileHash -LiteralPath $path).Hash}
}
function Write-Manifest($Items){ConvertTo-Json -InputObject @($Items) | Set-Content -LiteralPath (Join-Path $testRoot 'manifest.json') -Encoding UTF8}
Write-Manifest $entries
Check (@(Assert-RiumManifest $testRoot $config).Count -eq $config.PayloadPaths.Count) 'complete standalone payload verifies'
Check ((Resolve-RiumPackageRoot $testRoot '') -eq $testRoot) 'extracted package resolves itself'
Check ((Resolve-RiumPackageRoot $PSScriptRoot $testRoot) -eq $testRoot) 'explicit package root resolves'
Write-Manifest @($entries | Select-Object -Skip 1)
Reject {Assert-RiumManifest $testRoot $config} 'missing payload rejected'
Write-Manifest @($entries + $entries[0])
Reject {Assert-RiumManifest $testRoot $config} 'duplicate payload rejected'
Write-Manifest $entries
Add-Content -LiteralPath (Join-Path $testRoot $entries[0].Path) -Value 'tampered'
Reject {Assert-RiumManifest $testRoot $config} 'hash mismatch rejected'
Reject {Assert-RiumContainedPath $testRoot (Join-Path $testRoot '..\escape')} 'parent traversal rejected'
Reject {Assert-RiumContainedPath $testRoot $testRoot} 'root itself cannot be moved as child output'
$badConfig=@{PayloadPaths=@('..\escape')}
Write-Manifest @([pscustomobject]@{Path='..\escape';Sha256=('A'*64)})
Reject {Assert-RiumManifest $testRoot $badConfig} 'manifest traversal rejected even if configured'
Reject {Assert-RiumCandidateVersion (Join-Path $testRoot 'x64\RiumKeysInput.dll') $config.Version} 'unversioned candidate rejected'
Check ($config.Version -eq '2.0.0-preview.9' -and $config.UpgradeFrom -eq '2.0.0-preview.8' -and $config.Channel -eq 'manual-prerelease') 'approved upgrade contract'
$legacy=Join-Path $testRoot 'legacy'
Check (!(Test-RiumLegacyMigration $legacy)) 'clean installation requires no legacy program'
New-Item -ItemType Directory -Path $legacy | Out-Null
Set-Content -LiteralPath (Join-Path $legacy 'RiumKeys.exe') -Value 'fixture'
Reject {Test-RiumLegacyMigration $legacy} 'partial legacy installation rejected'
Set-Content -LiteralPath (Join-Path $legacy 'Uninstall.exe') -Value 'fixture'
Check (Test-RiumLegacyMigration $legacy) 'complete legacy pair opts into migration'
foreach($name in @('install-local.ps1','upgrade-local.ps1','install-machine.ps1','uninstall-local.ps1','prepare-package.ps1','package-common.ps1')){
    $tokens=$null;$errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) "$name parses"
}
# Exercise the actual packaging entry point with a deliberately failing native gate.
# The isolated copy has no runnable native payload; this cannot register or load an IME.
$gateRoot=Join-Path $testRoot 'gate-check'
New-Item -ItemType Directory -Path (Join-Path $gateRoot 'out\local-package') -Force | Out-Null
foreach($name in @('prepare-package.ps1','package-common.ps1','package-config.psd1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $gateRoot}
Set-Content -LiteralPath (Join-Path $gateRoot 'test-package.ps1') -Value "throw 'Injected native contract failure'"
$sentinel=Join-Path $gateRoot 'out\local-package\retained.txt'
Set-Content -LiteralPath $sentinel -Value 'previous package'
$hash=(Get-FileHash -LiteralPath $sentinel).Hash
Reject {& (Join-Path $gateRoot 'prepare-package.ps1')} 'native test failure blocks packaging'
Check ((Get-FileHash -LiteralPath $sentinel).Hash -eq $hash) 'native gate failure preserves previous package'
Check (@(Get-ChildItem -LiteralPath (Join-Path $gateRoot 'out') -Directory).Count -eq 1) 'native gate runs before staging writes'
Write-Output "Installer script contracts: $script:checks checks passed. No registry, elevation, native launch or installation exercised. Fixtures retained: $testRoot"
