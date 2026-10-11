@{
    Version = '2.0.1'
    FileVersion = '2.0.1.0'
    UpgradeFrom = '2.0.0'
    Channel = 'manual-release'
    PayloadPaths = @(
        'x64\RiumKeysInput.dll', 'x86\RiumKeysInput.dll', 'x64\RiumKeysControl.exe',
        'x86\RiumKeysControl.exe', 'LICENSE', 'COPYRIGHT.md',
        'install-local.ps1', 'upgrade-local.ps1', 'install-machine.ps1', 'uninstall-local.ps1',
        'package-config.psd1', 'package-common.ps1', 'setup.ps1'
    )
}
