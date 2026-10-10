param([switch]$Test, [switch]$FixtureOnly)
$ErrorActionPreference = 'Stop'
$scopeRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$scopeCompiler = Join-Path $scopeRepo 'upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'
$scopeOutput = Join-Path $scopeRepo 'native-ime\out\input-scope-probe'
$scopeCore = Join-Path $PSScriptRoot 'input-scope-core.cpp'
if (-not (Test-Path -LiteralPath $scopeCompiler -PathType Leaf)) { throw "Missing compiler: $scopeCompiler" }
New-Item -ItemType Directory -Path $scopeOutput -Force | Out-Null
& $scopeCompiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' $scopeCore `
    (Join-Path $PSScriptRoot 'input-scope-fixture.cpp') '-lole32' '-loleaut32' '-luuid' '-luser32' `
    '-o' (Join-Path $scopeOutput 'input-scope-fixture.exe')
if ($LASTEXITCODE -ne 0) { throw 'InputScope fixture build failed.' }
if (-not $FixtureOnly) {
    $scopeProbe = Join-Path $PSScriptRoot 'input-scope-probe.cpp'
    & $scopeCompiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-shared' '-DINPUT_SCOPE_DLL' `
        $scopeCore $scopeProbe '-lole32' '-loleaut32' '-luuid' '-luser32' '-o' (Join-Path $scopeOutput 'input-scope-probe.dll')
    if ($LASTEXITCODE -ne 0) { throw 'InputScope DLL build failed.' }
    & $scopeCompiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-municode' `
        $scopeCore $scopeProbe '-lole32' '-loleaut32' '-luuid' '-luser32' '-o' (Join-Path $scopeOutput 'input-scope-probe.exe')
    if ($LASTEXITCODE -ne 0) { throw 'InputScope controller build failed.' }
}
if ($Test) {
    & (Join-Path $scopeOutput 'input-scope-fixture.exe')
    if ($LASTEXITCODE -ne 0) { throw "Own TSF fixture failed: $LASTEXITCODE" }
    if (-not $FixtureOnly) {
        & (Join-Path $scopeOutput 'input-scope-probe.exe') '--self-test-load' (Join-Path $scopeOutput 'input-scope-probe.dll')
        if ($LASTEXITCODE -ne 0) { throw 'Own DLL load test failed.' }
        & (Join-Path $scopeOutput 'input-scope-probe.exe') '--self-test-hooks' (Join-Path $scopeOutput 'input-scope-probe.dll')
        if ($LASTEXITCODE -ne 0) { throw 'Own thread hook cleanup test failed.' }
        & (Join-Path $scopeOutput 'input-scope-probe.exe') '4294967295' '0x1' (Join-Path $scopeOutput 'input-scope-probe.dll') '1'
        if ($LASTEXITCODE -ne 3) { throw 'Invalid target was not rejected before hook installation.' }
        & (Join-Path $scopeOutput 'input-scope-probe.exe') '1' '0x1' (Join-Path $scopeOutput 'input-scope-probe.dll') '11'
        if ($LASTEXITCODE -ne 2) { throw 'Over-budget duration was not rejected.' }
    }
}
Write-Output "Built InputScope diagnostic in $scopeOutput. No external app was probed."
exit 0
