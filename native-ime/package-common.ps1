# Package helpers never open registry keys or elevate. Branding helpers operate
# only on explicitly supplied keys, allowing rollback tests in an isolated hive.
function Get-RiumPackageConfig([string]$Directory) {
    # NSIS launched from a PowerShell 7 parent can inherit a module search path
    # that omits Windows PowerShell's inbox script functions. Import that exact
    # built-in module, not an arbitrary module from a caller's search path.
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
    $config = Import-PowerShellDataFile -LiteralPath (Join-Path $Directory 'package-config.psd1')
    if ($config.Version -notmatch '^2\.[0-9]+\.[0-9]+$' -or
        $config.FileVersion -notmatch ('^'+[regex]::Escape($config.Version)+'\.[0-9]+$') -or
        $config.UpgradeFrom -notmatch '^2\.[0-9]+\.[0-9]+(-preview\.[1-9][0-9]*)?$' -or
        $config.Version -eq $config.UpgradeFrom -or $config.Channel -ne 'manual-release') {
        throw 'Invalid release package configuration.'
    }
    if (@($config.PayloadPaths).Count -ne @($config.PayloadPaths | Sort-Object -Unique).Count) { throw 'Duplicate configured payload.' }
    return $config
}
function Resolve-RiumPackageRoot([string]$ScriptDirectory, [string]$PackageRoot) {
    if (!$PackageRoot) {
        $PackageRoot = if (Test-Path -LiteralPath (Join-Path $ScriptDirectory 'manifest.json')) { $ScriptDirectory } else { Join-Path $ScriptDirectory 'out\local-package' }
    }
    return (Resolve-Path -LiteralPath $PackageRoot).Path
}
function Assert-RiumContainedPath([string]$Root, [string]$Path) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes the owned directory.' }
    # Reject junctions/symlinks on every existing component, including the root.
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not valid package paths.' }
        }
        $cursor = Split-Path -Parent $cursor
    }
    return $full
}
function Assert-RiumManifest([string]$Directory, $Config) {
    # Windows PowerShell 5.1 emits a JSON array as one pipeline object.
    $parsed = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    $entries = @($parsed)
    if ($entries.Count -ne @($Config.PayloadPaths).Count -or
        @(Compare-Object @($entries.Path | Sort-Object -Unique) @($Config.PayloadPaths | Sort-Object)).Count) { throw 'Incomplete, duplicate or unexpected package manifest.' }
    foreach ($entry in $entries) {
        if ($entry.Path -notin $Config.PayloadPaths -or $entry.Path -match '(^[\\/]|:|(^|[\\/])\.\.([\\/]|$))' -or $entry.Sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid package entry.' }
        $file = Assert-RiumContainedPath $Directory (Join-Path $Directory $entry.Path)
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.Sha256) { throw "Package hash mismatch: $($entry.Path)" }
    }
    return $entries
}
function Assert-RiumCandidateVersion([string]$Dll, [string]$Version) {
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($Dll).ProductVersion -ne $Version) { throw "Candidate DLL does not match package version $Version`: $Dll" }
}
function Test-RiumLegacyMigration([string]$Directory) {
    $exe=Test-Path -LiteralPath (Join-Path $Directory 'RiumKeys.exe') -PathType Leaf
    $uninstaller=Test-Path -LiteralPath (Join-Path $Directory 'Uninstall.exe') -PathType Leaf
    if($exe -ne $uninstaller){throw 'Incomplete legacy installation needs inspection.'}
    return $exe
}

function Get-RiumProfileBranding($Key, [string]$ProductRoot) {
    if (!$Key) { throw 'Missing owned language profile.' }
    $values = @{}
    foreach ($name in @('Description','IconFile','IconIndex')) { $values[$name] = $Key.GetValue($name) }
    $prefix = [regex]::Escape([IO.Path]::GetFullPath($ProductRoot).TrimEnd('\'))
    if ($values.Description -notin @('RIUM Keys','CONTENTRIUM Keys') -or
        $Key.GetValueKind('Description') -ne [Microsoft.Win32.RegistryValueKind]::String -or
        $Key.GetValueKind('IconFile') -ne [Microsoft.Win32.RegistryValueKind]::String -or
        $Key.GetValueKind('IconIndex') -ne [Microsoft.Win32.RegistryValueKind]::DWord -or
        $values.IconFile -notmatch ('^' + $prefix + '\\2\.[0-9]+\.[0-9]+(-preview\.[1-9][0-9]*)?\\x64\\RiumKeysInput\.dll$') -or
        $values.IconIndex -ne -100) { throw 'Unexpected input profile branding owner.' }
    return $values
}

function Invoke-RiumLoadCheck([string]$Executable, [string]$Dll, [string]$LogPath) {
    # Windows PowerShell Start-Process can lose ExitCode for a short-lived child
    # with redirected output. Process.Start retains its handle and exit result.
    $info=[Diagnostics.ProcessStartInfo]::new($Executable,('--verify-install "'+$Dll+'"'))
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    try {
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(15000)) {
            Stop-Process -InputObject $process
            [void]$process.WaitForExit(5000)
            throw 'Installed input load check timed out.'
        }
        [IO.File]::WriteAllText($LogPath,$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText(($LogPath+'.err'),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
        Get-Content -LiteralPath $LogPath,($LogPath+'.err')
        if ($process.ExitCode -ne 0) { throw "Installed input load check failed with exit $($process.ExitCode). See $LogPath" }
    } finally { $process.Dispose() }
}
# Verify the registered COM object in a fresh ordinary-user process for each
# architecture. This never opens a typing window or changes the input selection.
function Test-RiumInstalledLoad([string]$InstallRoot, [string]$LogDirectory) {
    foreach ($architecture in @('x64','x86')) {
        Invoke-RiumLoadCheck (Join-Path $InstallRoot "$architecture\RiumKeysControl.exe") (Join-Path $InstallRoot "$architecture\RiumKeysInput.dll") (Join-Path $LogDirectory "automatic-check-$architecture.log")
    }
}
function Set-RiumProfileBranding($Key, $Before, [string]$TargetDll, [switch]$Restore) {
    $after = @{ Description = 'CONTENTRIUM Keys'; IconFile = $TargetDll; IconIndex = -100 }
    # Both registry views may reference the same CTF key. Accept a value already
    # written by the other view, but never overwrite a third-party concurrent edit.
    foreach ($name in $after.Keys) {
        if (!$Key -or $Key.GetValue($name) -notin @($Before[$name],$after[$name])) {
            throw "Input profile branding changed concurrently: $name"
        }
    }
    $desired = if ($Restore) { $Before } else { $after }
    foreach ($name in $desired.Keys) {
        $kind = if ($name -eq 'IconIndex') { [Microsoft.Win32.RegistryValueKind]::DWord } else { [Microsoft.Win32.RegistryValueKind]::String }
        $Key.SetValue($name,$desired[$name],$kind)
        if ($Key.GetValue($name) -ne $desired[$name]) { throw "Profile branding readback mismatch: $name" }
    }
}
