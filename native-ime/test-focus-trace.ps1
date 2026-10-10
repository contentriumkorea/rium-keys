param([ValidateSet('x64','x86')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$directory = Join-Path $PSScriptRoot "out\installable\$Architecture"
$testExe = Join-Path $directory 'RoutingTests.exe'
$dll = Join-Path $directory 'RiumKeysInput.dll'
$root = Join-Path $PSScriptRoot ('out\focus-trace-tests\' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $root -Force | Out-Null
$key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Contentrium\RiumKeysInput')
$saved = @{}
foreach ($name in @('FocusTraceImage','FocusTraceUntil')) {
    $exists = $key.GetValueNames() -contains $name
    $saved[$name] = @{ Exists = $exists; Value = $null; Kind = $null }
    if ($exists) { $saved[$name].Value = $key.GetValue($name); $saved[$name].Kind = $key.GetValueKind($name) }
}
$results = @()
try {
    foreach ($case in @('disabled','wrong-image','expired','too-far','enabled')) {
        $key.DeleteValue('FocusTraceImage', $false)
        $key.DeleteValue('FocusTraceUntil', $false)
        if ($case -ne 'disabled') {
            $imageName = if ($case -eq 'wrong-image') { 'Unrelated.exe' } else { 'RoutingTests.exe' }
            $minutes = switch ($case) { 'expired' { -1 }; 'too-far' { 60 }; default { 10 } }
            $key.SetValue('FocusTraceImage', $imageName, [Microsoft.Win32.RegistryValueKind]::String)
            $key.SetValue('FocusTraceUntil', [DateTime]::UtcNow.AddMinutes($minutes).ToFileTimeUtc(), [Microsoft.Win32.RegistryValueKind]::QWord)
        }
        $output = Join-Path $root $case
        New-Item -ItemType Directory -Path $output | Out-Null
        $start = [Diagnostics.ProcessStartInfo]::new($testExe)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.Arguments = '"' + $dll + '" --expect-known-workspace-bug'
        $start.EnvironmentVariables['TMP'] = $output
        $start.EnvironmentVariables['TEMP'] = $output
        $process = [Diagnostics.Process]::Start($start)
        try {
            if (!$process.WaitForExit(15000)) { $process.Kill(); throw 'Owned diagnostic test timed out.' }
            $stdout = $process.StandardOutput.ReadToEnd()
            $stderr = $process.StandardError.ReadToEnd()
            if ($process.ExitCode -ne 0) { throw "Baseline routing changed: $stdout $stderr" }
            $log = Join-Path $output ("RiumKeys-focus-{0}.log" -f $process.Id)
            $hasLog = Test-Path -LiteralPath $log
            if ($hasLog -ne ($case -eq 'enabled')) { throw "Trace opt-in mismatch: $case, log=$hasLog" }
            $rows = if ($hasLog) { @(Get-Content -LiteralPath $log) } else { @() }
            if ($hasLog -and (!$rows.Count -or $rows.Count -gt 512 -or @($rows | Where-Object { $_ -notmatch '^tid=\d+ context=.* client=-?\d+,-?\d+,-?\d+,-?\d+$' }).Count)) {
                throw 'Trace format or row bound failed.'
            }
            $results += [pscustomobject]@{ Case=$case; Passed=$true; Rows=$rows.Count }
        } finally { $process.Dispose() }
    }
} finally {
    foreach ($name in $saved.Keys) {
        if ($saved[$name].Exists) { $key.SetValue($name, $saved[$name].Value, $saved[$name].Kind) }
        else { $key.DeleteValue($name, $false) }
    }
    $key.Dispose()
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'result.json') -Encoding UTF8
$results | Format-Table -AutoSize
"Trace tests passed; original opt-in registry values restored. Evidence: $root"
