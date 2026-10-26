@{
    Version = '2.0.3'
    FileVersion = '2.0.3.0'
    UpgradeFrom = '2.0.2'
    AlsoUpgradeFrom = @('2.0.1')
    Channel = 'manual-release'
    PayloadPaths = @(
        'x64\RiumKeysInput.dll', 'x86\RiumKeysInput.dll', 'x64\RiumKeysControl.exe',
        'x86\RiumKeysControl.exe', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
        'licenses\jamotong\LICENSE', 'licenses\jamotong\COPYRIGHT.md',
        'licenses\LLVM-LICENSE.txt', 'licenses\MinGW-w64-runtime.txt', 'licenses\Winpthreads.txt',
        'install-local.ps1', 'upgrade-local.ps1', 'install-machine.ps1', 'uninstall-local.ps1',
        'package-config.psd1', 'package-common.ps1', 'setup.ps1'
    )
}
