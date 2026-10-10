@{
    Version = '2.0.0-preview.9'
    UpgradeFrom = '2.0.0-preview.8'
    Channel = 'manual-prerelease'
    PayloadPaths = @(
        'x64\RiumKeysInput.dll', 'x86\RiumKeysInput.dll', 'x64\RiumKeysControl.exe',
        'x64\RiumImeFixture.exe', 'LICENSE', 'COPYRIGHT.md',
        'install-local.ps1', 'upgrade-local.ps1', 'install-machine.ps1', 'uninstall-local.ps1',
        'package-config.psd1', 'package-common.ps1'
    )
}
