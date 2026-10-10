param([switch]$Test)
$ErrorActionPreference = 'Stop'
$observerRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$observerCompiler = Join-Path $observerRepo 'upstream-research\toolchain\llvm-mingw-20261006-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'
$observerOutput = Join-Path $observerRepo 'native-ime\out\focus-contract-probe'
$observerExecutable = Join-Path $observerOutput 'event-observer.exe'
if (-not (Test-Path -LiteralPath $observerCompiler -PathType Leaf)) { throw "Missing compiler: $observerCompiler" }
New-Item -ItemType Directory -Path $observerOutput -Force | Out-Null
& $observerCompiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-municode' `
    (Join-Path $PSScriptRoot 'event-observer.cpp') `
    '-loleacc' '-loleaut32' '-lole32' '-luuid' '-luser32' '-luiautomationcore' '-o' $observerExecutable
if ($LASTEXITCODE -ne 0) { throw 'Event observer diagnostic build failed.' }
if ($Test) {
    & $observerExecutable '--self-test-fixtures'
    if ($LASTEXITCODE -ne 0) { throw "Callback fixture test failed: $LASTEXITCODE" }
    & $observerExecutable '--self-test-own-provider'
    if ($LASTEXITCODE -ne 0) { throw "Own hidden provider transport test failed: $LASTEXITCODE" }
    & $observerExecutable '--self-test-watchdog'
    if ($LASTEXITCODE -ne 124) { throw "Own-process watchdog did not produce expected exit 124: $LASTEXITCODE" }
    & $observerExecutable '4294967295' '0x1' '1'
    if ($LASTEXITCODE -ne 3) { throw "Invalid target was not rejected: $LASTEXITCODE" }
    & $observerExecutable '1' '0x1' '61'
    if ($LASTEXITCODE -ne 2) { throw "Over-budget duration was not rejected: $LASTEXITCODE" }
}
Write-Output "Built $observerExecutable. No external application was inspected."
