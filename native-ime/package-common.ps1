# Pure package helpers: no registry, process, elevation or installation operations.
function Get-RiumPackageConfig([string]$Directory) {
    $config = Import-PowerShellDataFile -LiteralPath (Join-Path $Directory 'package-config.psd1')
    if ($config.Version -notmatch '^2\.0\.0-preview\.[1-9][0-9]*$' -or
        $config.UpgradeFrom -notmatch '^2\.0\.0-preview\.[1-9][0-9]*$' -or
        $config.Version -eq $config.UpgradeFrom -or $config.Channel -ne 'manual-prerelease') {
        throw 'Invalid manual preview package configuration.'
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
