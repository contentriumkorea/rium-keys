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
Check ($config.Version -eq '2.0.1' -and $config.UpgradeFrom -eq '2.0.0' -and $config.Channel -eq 'manual-release') 'release upgrades the previous installed build'
$legacy=Join-Path $testRoot 'legacy'
Check (!(Test-RiumLegacyMigration $legacy)) 'clean installation requires no legacy program'
New-Item -ItemType Directory -Path $legacy | Out-Null
Set-Content -LiteralPath (Join-Path $legacy 'RiumKeys.exe') -Value 'fixture'
Reject {Test-RiumLegacyMigration $legacy} 'partial legacy installation rejected'
Set-Content -LiteralPath (Join-Path $legacy 'Uninstall.exe') -Value 'fixture'
Check (Test-RiumLegacyMigration $legacy) 'complete legacy pair opts into migration'
foreach($name in @('install-local.ps1','upgrade-local.ps1','install-machine.ps1','uninstall-local.ps1','prepare-package.ps1','package-common.ps1','setup.ps1','build-setup.ps1')){
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
# Real registry value types and aliased handles, only in a disposable HKCU key.
$testKeyPath='Software\Contentrium\InstallerTests\'+[guid]::NewGuid()
$testKey=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($testKeyPath)
$alias=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testKeyPath,$true)
try {
    $productRoot=Join-Path $env:ProgramFiles 'RIUM Keys'
    $oldIcon=Join-Path $productRoot '2.0.0-preview.2\x64\RiumKeysInput.dll'
    $newIcon=Join-Path $productRoot ($config.Version+'\x64\RiumKeysInput.dll')
    $testKey.SetValue('Description','RIUM Keys')
    $testKey.SetValue('IconFile',$oldIcon)
    $testKey.SetValue('IconIndex',-100,[Microsoft.Win32.RegistryValueKind]::DWord)
    $first=Get-RiumProfileBranding $testKey $productRoot
    $second=Get-RiumProfileBranding $alias $productRoot
    Set-RiumProfileBranding $testKey $first $newIcon
    Set-RiumProfileBranding $alias $second $newIcon
    Check ((Get-RiumProfileBranding $alias $productRoot).IconFile -eq $newIcon) 'release icon owner accepted on repeat setup'
    Check ($alias.GetValue('IconFile') -eq $newIcon -and $alias.GetValue('Description') -eq 'CONTENTRIUM Keys') 'both aliased views upgrade stale icon and name'
    Set-RiumProfileBranding $testKey $first $newIcon -Restore
    Set-RiumProfileBranding $alias $second $newIcon -Restore
    Check ($alias.GetValue('IconFile') -eq $oldIcon -and $alias.GetValue('Description') -eq 'RIUM Keys') 'both aliased views restore original icon and name'
    $testKey.SetValue('IconFile','C:\Foreign\Input.dll')
    Reject {Get-RiumProfileBranding $testKey $productRoot} 'foreign icon owner rejected'
    Reject {Set-RiumProfileBranding $testKey $first $newIcon -Restore} 'rollback preserves concurrent foreign icon'
    Check ($testKey.GetValue('IconFile') -eq 'C:\Foreign\Input.dll') 'concurrent owner not overwritten'
    foreach($badVersion in @('2.0.0-foreign','2.0.0-preview.0','2.0.0\..\elsewhere')){
        $testKey.SetValue('IconFile',(Join-Path $productRoot ($badVersion+'\x64\RiumKeysInput.dll')))
        Reject {Get-RiumProfileBranding $testKey $productRoot} "invalid branding version rejected: $badVersion"
    }
} finally { $alias.Dispose(); $testKey.Dispose(); [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($testKeyPath) }
Write-Output "Installer script contracts: $script:checks checks passed. Only disposable HKCU test values changed; no elevation or installation. Fixtures retained: $testRoot"
